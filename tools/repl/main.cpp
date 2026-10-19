// inspector-repl: waits for instrumented programs to reach INSPECTOR and
// gives the user a C++ prompt that runs inside them.

#include "connection.h"
#include "highlight.h"
#include "inspector/prompt.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/JSON.h>
#include <replxx.hxx>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
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

  .locals      list variables captured at this breakpoint
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

auto historyPath() -> std::filesystem::path {
  // NOLINTBEGIN(concurrency-mt-unsafe): nothing modifies the environment
  const char *state = std::getenv("XDG_STATE_HOME");
  const char *home = std::getenv("HOME");
  // NOLINTEND(concurrency-mt-unsafe)
  const std::filesystem::path base =
      state != nullptr && *state != '\0'
          ? std::filesystem::path(state)
          : std::filesystem::path(home != nullptr ? home : ".") / ".local" /
                "state";
  return base / "inspector" / "history";
}

auto readLines(const std::string &path) -> std::vector<std::string> {
  std::vector<std::string> lines;
  std::ifstream file(path);
  for (std::string line; std::getline(file, line);) {
    lines.push_back(std::move(line));
  }
  return lines;
}

struct Local {
  std::string name;
  std::string type;
};

// Recovers (name, type) from the generated reference prelude.
auto parseLocals(const std::string &prelude) -> std::vector<Local> {
  static const std::regex decl(R"(^inspector_repl::type<(.*)>& (\w+) = \*\()");
  std::vector<Local> out;
  std::istringstream lines(prelude);
  for (std::string line; std::getline(lines, line);) {
    std::smatch match;
    if (std::regex_search(line, match, decl)) {
      out.push_back({match.str(2), match.str(1)});
    }
  }
  return out;
}

class Breakpoint {
  Connection *conn;
  Replxx *editor;
  std::string file;
  unsigned line;
  std::vector<Local> locals;
  unsigned count = 0;

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

  void showLocals() const {
    if (locals.empty()) {
      std::cout << "no local variables captured\n";
    }
    for (const auto &local : locals) {
      std::cout << "  " << bold << local.name << reset << " " << gray
                << local.type << reset << "\n";
    }
  }

  auto complete(const std::string &input, int &contextLen)
      -> Replxx::completions_t {
    Replxx::completions_t out;
    const auto reply = conn->request(llvm::json::Object{{"complete", input}});
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
  }

public:
  Breakpoint(Connection &conn, Replxx &editor, const llvm::json::Object &spec)
      : conn(&conn), editor(&editor),
        file(spec.getString("file").value_or("").str()),
        line(static_cast<unsigned>(spec.getInteger("line").value_or(0))),
        locals(parseLocals(spec.getString("locals").value_or("").str())) {}

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
    configure();
    const std::string history = historyPath().string();
    while (true) {
      errno = 0;
      const char *raw = editor->input("[" + std::to_string(count) + "] ");
      if (raw == nullptr && errno == EAGAIN) {
        continue; // Ctrl-C clears the line
      }
      const std::string input = raw != nullptr ? raw : ".continue";
      const llvm::StringRef cmd = llvm::StringRef(input).trim();
      if (cmd.empty()) {
        continue;
      }
      ++count;
      editor->history_add(input);
      editor->history_sync(history);
      if (cmd == ".help") {
        std::cout << help << "\n";
      } else if (cmd == ".locals") {
        showLocals();
      } else if (cmd == ".where") {
        where();
      } else if (cmd == ".continue" || cmd == ".quit" || cmd == ".c" ||
                 cmd == ".q" || cmd == ".abort") {
        const bool abort = cmd == ".abort";
        conn->request(
            llvm::json::Object{{"input", abort ? ".abort" : ".quit"}});
        std::cout << (abort ? "program aborted.\n\n"
                            : "continuing program.\n\n");
        return;
      } else if (const auto reply =
                     conn->request(llvm::json::Object{{"input", input}})) {
        show(*reply);
      } else {
        std::cout << "program disconnected.\n\n";
        return;
      }
    }
  }
};

auto serve() -> int {
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

auto main() -> int {
  try {
    return serve();
  } catch (const std::exception &e) {
    std::cerr << "inspector-repl: " << e.what() << "\n";
    return 1;
  }
}
