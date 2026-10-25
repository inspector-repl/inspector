// inspector-repl: waits for instrumented programs to reach INSPECTOR and
// gives the user a C++ prompt that runs inside them.

#include "command.h"
#include "connection.h"
#include "highlight.h"
#include "inspector/prompt.h"
#include "source.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/JSON.h>
#include <replxx.hxx>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace inspector;
using replxx::Replxx;

namespace {

constexpr std::string_view help =
    R"(Type C++ statements or expressions; expressions print their value.
A trailing ';' suppresses the result. Multi-line input continues while
brackets are open.

  .locals      list variables captured at this breakpoint with values
  .ls EXPR     list fields (with values) and methods of a class or pointer
  .where       show the surrounding source again
  .continue    resume the program (also .quit, Ctrl-D)
  .abort       terminate the program
  .help        this message

Tab completes, Ctrl-C clears the current input.)";

constexpr std::string_view bold = "\033[1m";
constexpr std::string_view red = "\033[31m";
constexpr std::string_view gray = "\033[90m";
constexpr std::string_view reset = "\033[0m";

// replxx counts positions in code points, clang in bytes.
auto isContinuation(char byte) -> bool {
  constexpr unsigned topTwoBits = 0xC0U;
  constexpr unsigned continuation = 0x80U; // UTF-8 10xxxxxx
  return (static_cast<unsigned char>(byte) & topTwoBits) == continuation;
}

auto codePoints(std::string_view text) -> int {
  return static_cast<int>(
      std::count_if(text.begin(), text.end(),
                    [](char byte) -> bool { return !isContinuation(byte); }));
}

auto replxxColor(Style style) -> Replxx::Color {
  switch (style) {
  case Style::Keyword:
    return Replxx::Color::BRIGHTBLUE;
  case Style::Type:
    return Replxx::Color::CYAN;
  case Style::Number:
    return Replxx::Color::MAGENTA;
  case Style::String:
    return Replxx::Color::BROWN;
  case Style::Comment:
    return Replxx::Color::GRAY;
  case Style::Preprocessor:
    return Replxx::Color::GREEN;
  case Style::Plain:
    break;
  }
  return Replxx::Color::DEFAULT;
}

auto historyPath() -> std::filesystem::path { return stateDir() / "history"; }

// The argument of `name` in `cmd` ("" if there is none), or nullopt if
// `cmd` isn't `name`.
auto argument(llvm::StringRef cmd, llvm::StringRef name)
    -> std::optional<llvm::StringRef> {
  if (!cmd.consume_front(name) || (!cmd.empty() && cmd.front() != ' ')) {
    return std::nullopt;
  }
  return cmd.trim();
}

auto terminalWidth() -> size_t {
  constexpr size_t fallback = 80;
  // <sys/ioctl.h> provides these through platform-internal headers (glibc
  // bits/, macOS sys/ttycom.h), and ioctl is variadic.
  // NOLINTBEGIN(misc-include-cleaner,cppcoreguidelines-pro-type-vararg)
  winsize size{};
  const bool known =
      ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col != 0;
  // NOLINTEND(misc-include-cleaner,cppcoreguidelines-pro-type-vararg)
  if (!known) {
    return fallback;
  }
  return size.ws_col;
}

auto resumes(llvm::StringRef cmd) -> bool {
  return cmd == ".continue" || cmd == ".quit" || cmd == ".c" || cmd == ".q" ||
         cmd == ".abort";
}

class Breakpoint {
  Connection *conn;
  Replxx *editor;
  std::string file;
  unsigned line;
  std::vector<Local> locals;

  static void show(const llvm::json::Object &reply) {
    std::string value = reply.getString("value").value_or("").str();
    while (!value.empty() && value.back() == '\n') {
      value.pop_back();
    }
    if (value.empty()) {
      return;
    }
    if (reply.getBoolean("error").value_or(false)) {
      std::cout << red << value << reset << "\n";
    } else {
      std::cout << value << "\n";
    }
  }

  void where() const {
    const auto lines = readLines(file);
    const unsigned first = line > 5 ? line - 5 : 1;
    const unsigned last = std::min<unsigned>(line + 3, lines.size());
    std::cout << bold << "From: " << file << " @ line " << line << ":" << reset
              << "\n";
    const auto width = std::to_string(last).size();
    for (unsigned i = first; i <= last; ++i) {
      const std::string number = std::to_string(i);
      std::cout << (i == line ? "\033[1;32m-->\033[0m " : "    ")
                << std::string(width - number.size(), ' ') << number << ": "
                << colorize(lines.at(i - 1)) << "\n";
    }
  }

  struct Row {
    std::string name;
    std::string type;
    std::string value;
    bool error;
  };

  // Names, types and values in aligned columns. Long types don't widen
  // the column for all others.
  static void showTable(std::vector<Row> rows) {
    constexpr size_t widest = 40;
    // Template-heavy types (boost maps) would fill several lines.
    constexpr size_t longest = 50;
    for (Row &row : rows) {
      if (row.type.size() > longest) {
        row.type = row.type.substr(0, longest - 1) + "\u2026";
      }
    }
    size_t nameWidth = 0;
    size_t typeWidth = 0;
    for (const Row &row : rows) {
      nameWidth = std::max(nameWidth, row.name.size());
      typeWidth = std::max(typeWidth, std::min(row.type.size(), widest));
    }
    for (const Row &row : rows) {
      const size_t typePad =
          row.type.size() < typeWidth ? typeWidth - row.type.size() : 0;
      std::cout << "  " << bold << row.name << reset
                << std::string(nameWidth - row.name.size() + 1, ' ') << gray
                << row.type << reset << std::string(typePad + 1, ' ') << "= "
                << (row.error ? red : reset) << row.value << reset << "\n";
    }
  }

  static auto toRow(const llvm::json::Value &json) -> Row {
    const llvm::json::Object *object = json.getAsObject();
    if (object == nullptr) {
      return {"", "", "?", true};
    }
    return {object->getString("name").value_or("").str(),
            object->getString("type").value_or("").str(),
            object->getString("value").value_or("").str(),
            object->getBoolean("error").value_or(false)};
  }

  void showLocals() {
    if (locals.empty()) {
      std::cout << "no local variables captured\n";
      return;
    }
    llvm::json::Array names;
    for (const auto &local : locals) {
      names.emplace_back(local.name);
    }
    const auto reply =
        conn->request(llvm::json::Object{{"values", std::move(names)}});
    const llvm::json::Array *values =
        reply ? reply->getArray("values") : nullptr;
    std::vector<Row> got;
    if (values != nullptr) {
      for (const auto &value : *values) {
        got.push_back(toRow(value));
      }
    }
    std::vector<Row> rows;
    for (size_t i = 0; i < locals.size(); ++i) {
      Row row{locals.at(i).name, locals.at(i).type, "?", true};
      if (i < got.size()) {
        row.value = got.at(i).value;
        row.error = got.at(i).error;
      }
      rows.push_back(std::move(row));
    }
    showTable(rows);
  }

  static void showClass(const llvm::json::Object &cls) {
    std::cout << bold << cls.getString("name").value_or("").str() << reset
              << "\n";
    std::vector<Row> rows;
    if (const auto *fields = cls.getArray("fields")) {
      for (const auto &field : *fields) {
        rows.push_back(toRow(field));
      }
    }
    showTable(rows);
    if (const auto *methods = cls.getArray("methods");
        methods != nullptr && !methods->empty()) {
      const std::string label = "  methods:";
      std::cout << gray << label << reset;
      size_t column = label.size();
      const size_t width = terminalWidth();
      for (const auto &method : *methods) {
        const std::string name = method.getAsString().value_or("").str();
        if (column + 1 + name.size() > width && column > label.size()) {
          std::cout << "\n" << std::string(label.size(), ' ');
          column = label.size();
        }
        std::cout << " " << name;
        column += 1 + name.size();
      }
      std::cout << "\n";
    }
  }

  void showMembers(llvm::StringRef expr) {
    const auto reply = conn->request(llvm::json::Object{{"ls", expr.str()}});
    if (!reply) {
      return;
    }
    const llvm::json::Array *classes = reply->getArray("classes");
    if (classes == nullptr) {
      show(*reply);
      return;
    }
    for (const auto &entry : *classes) {
      if (const auto *cls = entry.getAsObject()) {
        showClass(*cls);
      }
    }
  }

  auto complete(const std::string &input, int &contextLen)
      -> Replxx::completions_t {
    Replxx::completions_t out;
    if (!conn->send(llvm::json::Object{{"complete", input}})) {
      return out;
    }
    // The first request may wait for the program to parse the file.
    constexpr std::chrono::milliseconds patience{150};
    const bool slow = !conn->wait(patience);
    if (slow) {
      std::cout << "\0337" << gray << " (parsing "
                << std::filesystem::path(file).filename().string() << "…)"
                << reset << std::flush;
    }
    const auto reply = conn->receive();
    if (slow) {
      std::cout << "\0338\033[K" << std::flush;
    }
    if (!reply) {
      return out;
    }
    contextLen = codePoints(reply->getString("prefix").value_or(""));
    if (const auto *names = reply->getArray("completions")) {
      for (const auto &name : *names) {
        if (const auto text = name.getAsString()) {
          out.emplace_back(text->str());
        }
      }
    }
    return out;
  }

  void configure() {
    editor->set_highlighter_callback(
        [](const std::string &input, Replxx::colors_t &colors) -> void {
          // colors has one entry per code point, spans are in bytes.
          std::vector<Style> styles(input.size(), Style::Plain);
          for (const Span &span : highlight(input)) {
            std::fill(styles.begin() + static_cast<std::ptrdiff_t>(span.begin),
                      styles.begin() + static_cast<std::ptrdiff_t>(span.end),
                      span.style);
          }
          auto color = colors.begin();
          auto style = styles.begin();
          for (const char byte : input) {
            if (!isContinuation(byte) && color != colors.end()) {
              *color++ = replxxColor(*style);
            }
            ++style;
          }
        });
    editor->set_completion_callback(
        [this](const std::string &input,
               int &contextLen) -> Replxx::completions_t {
          return complete(input, contextLen);
        });
    editor->bind_key(Replxx::KEY::ENTER,
                     [this](char32_t code) -> Replxx::ACTION_RESULT {
                       const Replxx::State state = editor->get_state();
                       if (openBrackets(state.text()) > 0) {
                         return editor->invoke(Replxx::ACTION::NEW_LINE, code);
                       }
                       return editor->invoke(Replxx::ACTION::COMMIT_LINE, code);
                     });
    // replxx binds these to completion cycling, readline users expect history.
    editor->bind_key_internal(Replxx::KEY::control('P'), "history_previous");
    editor->bind_key_internal(Replxx::KEY::control('N'), "history_next");
  }

public:
  Breakpoint(Connection &conn, Replxx &editor, const llvm::json::Object &spec)
      : conn(&conn), editor(&editor),
        file(spec.getString("file").value_or("").str()),
        line(static_cast<unsigned>(spec.getInteger("line").value_or(0))),
        locals(parseLocals(spec.getString("locals").value_or("").str())) {}

  // Evaluates `input` in the program; false once it hung up.
  auto evaluate(const std::string &input) -> bool {
    const auto reply = conn->request(llvm::json::Object{{"input", input}});
    if (!reply) {
      std::cout << "program disconnected.\n\n";
      return false;
    }
    show(*reply);
    return true;
  }

  // Handles one line of input; false once the program resumed or hung up.
  auto dispatch(llvm::StringRef cmd, const std::string &input) -> bool {
    if (cmd == ".help") {
      std::cout << help << "\n";
    } else if (cmd == ".locals") {
      showLocals();
    } else if (const auto expr = argument(cmd, ".ls")) {
      if (expr->empty()) {
        showLocals();
      } else {
        showMembers(*expr);
      }
    } else if (cmd == ".where") {
      where();
    } else if (resumes(cmd)) {
      const bool abort = cmd == ".abort";
      conn->request(llvm::json::Object{{"input", abort ? ".abort" : ".quit"}});
      std::cout << (abort ? "program aborted.\n\n" : "continuing program.\n\n");
      return false;
    } else {
      return evaluate(input);
    }
    return true;
  }

  void run() {
    where();
    std::cout << "starting interpreter...\r" << std::flush;
    const auto ready = conn->receive();
    if (!ready) {
      std::cout << "program disconnected during startup\n";
      return;
    }
    show(*ready);
    std::cout << "Type .help for commands.\n";
    // Completion is likely wanted here, unlike in scripts.
    (void)conn->send(llvm::json::Object{{"prepare", true}});
    configure();
    const std::string history = historyPath().string();
    const std::string prompt = std::filesystem::path(file).filename().string() +
                               ":" + std::to_string(line) + "> ";
    while (true) {
      errno = 0;
      const char *raw = editor->input(prompt);
      if (raw == nullptr && errno == EAGAIN) {
        continue; // Ctrl-C clears the line
      }
      const std::string input = raw != nullptr ? raw : ".continue";
      const llvm::StringRef cmd = llvm::StringRef(input).trim();
      if (cmd.empty()) {
        continue;
      }
      editor->history_add(input);
      editor->history_sync(history);
      if (!dispatch(cmd, input)) {
        return;
      }
    }
  }
};

auto serve() -> int {
  if (serverRunning()) {
    std::cerr << "inspector-repl: a background server owns the socket, "
                 "`inspector-repl stop` ends it\n";
    return 1;
  }
  const Listener listener(socketPath());
  std::filesystem::create_directories(historyPath().parent_path());
  Replxx editor(std::cin, std::cout, STDIN_FILENO, STDOUT_FILENO,
                STDERR_FILENO);
  editor.install_window_change_handler();
  constexpr int historySize = 10000;
  editor.set_max_history_size(historySize);
  editor.history_load(historyPath().string());
  editor.set_word_break_characters(" \t\n.,-+*/%=<>!&|^~()[]{};:?\"'");
  // Ctrl-C while waiting just exits; the next start replaces the socket.
  while (true) {
    std::cout << "Waiting for a program to reach INSPECTOR ("
              << listener.path().string() << ")... Ctrl-C to exit\n";
    Connection conn(listener.accept());
    if (const auto spec = conn.receive()) {
      Breakpoint(conn, editor, *spec).run();
    }
  }
}

} // namespace

auto main(int argc, char **argv) -> int {
  try {
    if (argc > 1) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): argv
      return runCommand({std::next(argv), std::next(argv, argc)});
    }
    return serve();
  } catch (const std::exception &e) {
    std::cerr << "inspector-repl: " << e.what() << "\n";
    return 1;
  }
}
