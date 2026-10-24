#include "inspector/repl.h"
#include "compat.h"
#include "completer.h"
#include "inspector/prompt.h"
#include "members.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclarationName.h>
#include <clang/AST/Type.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Interpreter/IncrementalExecutor.h>
#include <clang/Interpreter/Interpreter.h>
#include <clang/Interpreter/Value.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/ExecutionEngine/JITSymbol.h>
#include <llvm/ExecutionEngine/Orc/AbsoluteSymbols.h>
#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/CoreContainers.h>
#include <llvm/ExecutionEngine/Orc/Debugging/DebuggerSupport.h>
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/MapperJITLinkMemoryManager.h>
#include <llvm/ExecutionEngine/Orc/MemoryMapper.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace inspector;

namespace {

auto hexAddress(const void *ptr) -> std::string {
  return "0x" + llvm::utohexstr(reinterpret_cast<uintptr_t>(ptr)); // NOLINT
}

// Replaces each `%p` in the generated reference prelude with the matching
// address; the generator emits exactly one per captured variable.
auto fillAddresses(const std::string &context, const void *const *addresses)
    -> std::string {
  std::string out;
  size_t start = 0;
  size_t pos = 0;
  size_t index = 0;
  while ((pos = context.find("%p", start)) != std::string::npos) {
    out.append(context, start, pos - start);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): C API
    out += hexAddress(addresses[index++]);
    start = pos + 2;
  }
  out.append(context, start);
  return out;
}

// Routes clang diagnostics into a string so they reach the REPL user instead
// of the inspected program's stderr.
class DiagCapture {
  std::string buffer;
  llvm::raw_string_ostream stream{buffer};
  clang::DiagnosticOptions opts;

public:
  explicit DiagCapture(clang::Interpreter &interp) {
    // Locations point into synthetic "input_line_N" buffers; the source
    // excerpt below each diagnostic is what matters.
    opts.ShowLocation = false;
    inspector::compat::showColors(opts);
    auto *compiler = interp.getCompilerInstance();
    auto printer = std::make_unique<clang::TextDiagnosticPrinter>(stream, opts);
    // The interpreter's source file is already open; the replaced printer
    // would otherwise render without LangOptions and crash.
    printer->BeginSourceFile(compiler->getLangOpts(),
                             &compiler->getPreprocessor());
    compiler->getDiagnostics().setClient(printer.release(),
                                         /*ShouldOwnClient=*/true);
  }
  void clear() { buffer.clear(); }
  auto take() -> std::string { return std::exchange(buffer, {}); }
};

auto typeName(const clang::Value &value) -> std::string {
  return spell(value.getType(), value.getASTContext());
}

// One line of at most `limit` bytes, for listings.
auto oneLine(std::string text) -> std::string {
  constexpr size_t limit = 100;
  std::ranges::replace(text, '\n', ' ');
  if (text.size() > limit) {
    text.resize(limit);
    text += "\u2026";
  }
  return text;
}

auto isRecord(const clang::Value &value) -> bool {
  return value.getType().getNonReferenceType()->isRecordType();
}

// ORC reports the root cause of a link failure (e.g. "Symbols not found")
// only to the session's error reporter. The error returned to the caller
// just lists every symbol that failed with it, often thousands.
class JitErrors {
  std::mutex mutex;
  std::string causes;

public:
  void report(llvm::Error err) {
    llvm::handleAllErrors(
        std::move(err),
        // Consequences of a cause reported on its own.
        [](const llvm::orc::FailedToMaterialize &) -> void {},
        [this](const llvm::ErrorInfoBase &info) -> void {
          const std::scoped_lock lock(mutex);
          causes += info.message() + "\n";
        });
  }

  auto take() -> std::string {
    const std::scoped_lock lock(mutex);
    return std::exchange(causes, {});
  }
};

class Session {
  clang::Interpreter &interp;
  JitErrors &jitErrors;
  DiagCapture diags;
  std::string printed; // written by inspector_repl::print inside the JIT
  std::string lastError;
  bool threw = false;
  bool linkFailed = false;
  std::string statements; // successful input, so completion sees its names
  unsigned counter = 0;

  auto run(const std::string &code, clang::Value *value = nullptr) -> bool {
    llvm::Error err = llvm::Error::success();
    threw = true;
    linkFailed = false;
    try {
      err = interp.ParseAndExecute(code, value);
    } catch (const std::exception &e) {
      // An exception thrown by REPL code must not unwind into the program.
      lastError = std::string("uncaught exception: ") + e.what();
      return false;
    } catch (...) {
      lastError = "uncaught exception";
      return false;
    }
    threw = false;
    linkFailed = false;
    if (!err) {
      return true;
    }
    // JIT link errors (e.g. missing symbols) don't go through diagnostics.
    const std::string cause = jitErrors.take();
    // Parse errors are only "Parsing failed.", the diagnostics say why.
    linkFailed = !cause.empty() || err.isA<llvm::orc::FailedToMaterialize>();
    // Undo leaves the initializer queued, a failing runCtors() drains it.
    if (err.isA<llvm::orc::FailedToMaterialize>()) {
      llvm::consumeError(interp.Undo());
      if (auto engine = interp.getExecutionEngine()) {
        llvm::consumeError(engine->runCtors());
      } else {
        llvm::consumeError(engine.takeError());
      }
      jitErrors.take();
    }
    if (cause.empty()) {
      lastError = llvm::toString(std::move(err));
    } else {
      llvm::consumeError(std::move(err));
      lastError = cause;
    }
    return false;
  }

  auto failure() -> std::string {
    const std::string msg = diags.take();
    return msg.empty() ? lastError : msg;
  }

  auto firstLineOfFailure() -> Reply {
    const std::string error = failure();
    diags.clear();
    return {oneLine(error.substr(0, error.find('\n'))), true};
  }

  auto fresh(const char *prefix) -> std::string {
    return prefix + std::to_string(counter++);
  }

  // decltype is unevaluated, so this probe instantiates no function bodies.
  // A failed instantiation inside a discarded input would otherwise leave
  // the JIT referencing symbols that were never emitted.
  // Returns the alias naming the type of `code`, or "" if it has none.
  auto valueType(const std::string &code) -> std::string {
    const std::string alias = fresh("__inspector_t");
    clang::Value isVoid;
    const bool typed = run("using " + alias + " = decltype((" + code + "));") &&
                       run("std::is_void_v<" + alias + ">", &isVoid);
    diags.clear();
    return typed && isVoid.hasValue() && !isVoid.getBool() ? alias : "";
  }

  auto show(const std::string &var) -> Reply {
    clang::Value value;
    if (!run(var, &value) || !value.hasValue()) {
      return {failure(), true};
    }
    if (isRecord(value)) {
      printed.clear();
      run("inspector_repl::print(__inspector_out, " + var + ");");
      return {"(" + typeName(value) + ") " + printed, false};
    }
    std::string out;
    llvm::raw_string_ostream stream(out);
    value.print(stream);
    while (!out.empty() && out.back() == '\n') {
      out.pop_back();
    }
    return {out, false};
  }

  // The type a valueType() alias names, null if there is none.
  auto aliasedType(const std::string &alias) -> clang::QualType {
    auto &context = interp.getCompilerInstance()->getASTContext();
    const auto found = context.getTranslationUnitDecl()->lookup(
        clang::DeclarationName(&context.Idents.get(alias)));
    const auto *decl =
        found.empty() ? nullptr
                      : llvm::dyn_cast<clang::TypedefNameDecl>(found.front());
    return decl == nullptr ? clang::QualType() : decl->getUnderlyingType();
  }

public:
  Session(clang::Interpreter &interp, JitErrors &jitErrors)
      : interp(interp), jitErrors(jitErrors), diags(interp) {
    run("#include <inspector/repl_prelude.h>\n"
        "static std::string &__inspector_out = *(std::string *)" +
        hexAddress(&printed) + ";");
    diags.clear();
  }

  // Each line of `context` binds one local. If they don't all load at once,
  // they are retried one by one so a single unloadable local only costs
  // itself; the skipped ones are reported without failing the session.
  auto setup(const std::string &declare, const std::string &context) -> Reply {
    diags.clear();
    if (!run(declare)) {
      return {failure(), true};
    }
    diags.clear();
    if (run(context)) {
      return {"", false};
    }
    std::string skipped;
    std::istringstream lines(context);
    for (std::string line; std::getline(lines, line);) {
      diags.clear();
      if (!run(line)) {
        skipped += failure();
      }
    }
    diags.clear();
    return {skipped.empty() ? "" : "some locals are unavailable:\n" + skipped,
            false};
  }

  // Expressions are bound to a variable first so class types can be rendered
  // by the prelude and evaluated exactly once.
  auto eval(std::string input) -> Reply {
    while (!input.empty() && std::isspace(input.back()) != 0) {
      input.pop_back();
    }
    if (input.empty()) {
      return {"", false};
    }
    const bool wantValue =
        input.back() != ';' && input.back() != '}' && input.front() != '#';
    const std::string type = wantValue ? valueType(input) : "";
    if (wantValue && type.empty() && input.find('[') != std::string::npos) {
      // decltype rejects lambdas before C++20.
      const std::string var = fresh("__inspector_v");
      diags.clear();
      if (run("auto &&" + var + " = (" + input + ");")) {
        return show(var);
      }
      if (threw || linkFailed) {
        return {lastError, true};
      }
    }
    if (!type.empty()) {
      const std::string var = fresh("__inspector_v");
      diags.clear();
      // (void) keeps a user-defined operator, out of the comma.
      if (run("inspector_repl::stored<" + type + "> " + var +
              " = ((void)inspector_repl::runtime(), (" + input + "));")) {
        return show(var);
      }
      // Re-running would repeat side effects that led up to the throw.
      if (threw || linkFailed) {
        return {lastError, true};
      }
    }
    // Without ';' clang-repl parses a value-printing expression and silently
    // skips void calls like `v.push_back(1)`.
    if (wantValue) {
      input += ';';
    }
    diags.clear();
    const bool succeeded = run(input);
    if (succeeded) {
      statements += input + "\n";
    }
    // Warnings on success (e.g. [[nodiscard]]) are noise in a REPL.
    return {succeeded ? "" : failure(), !succeeded};
  }

  // An lvalue for `text` that code in the JIT can assign to.
  static auto slot(std::string &text) -> std::string {
    return "(*(std::string *)" + hexAddress(&text) + ")";
  }

  // The value of `code` on one line, without its type.
  auto brief(const std::string &code) -> Reply {
    std::string text;
    diags.clear();
    // A block: later inputs can't see names declared here, but neither
    // can they see stale copies of internal-linkage variables.
    if (!run("{ inspector_repl::print(" + slot(text) + ", (" + code +
             ")); }")) {
      return firstLineOfFailure();
    }
    return {oneLine(text), false};
  }

  // Fields with values and methods of the class `expr` evaluates to (or
  // points to), for `.ls`.
  auto ls(const std::string &expr) -> llvm::json::Object {
    const auto error = [](const std::string &message) -> llvm::json::Object {
      return llvm::json::Object{{"value", message}, {"error", true}};
    };
    const std::string alias = valueType(expr);
    clang::QualType listed =
        alias.empty() ? clang::QualType() : aliasedType(alias);
    if (listed.isNull()) {
      diags.clear();
      run("(void)(" + expr + ");");
      const std::string why = failure();
      return error(why.empty() ? expr + " has no value" : why);
    }
    listed = listed.getNonReferenceType();
    std::string object = "__inspector_o";
    if (const auto *pointer = listed->getAs<clang::PointerType>()) {
      listed = pointer->getPointeeType();
      object = "(*__inspector_o)";
    }
    const auto *record = listed->getAsCXXRecordDecl();
    const auto &context = interp.getCompilerInstance()->getASTContext();
    if (record == nullptr || !record->hasDefinition()) {
      return error(spell(listed, context) + " is not a class");
    }
    std::vector<ClassMembers> members = listMembers(*record);
    std::vector<std::string> prints; // one statement per field
    for (const ClassMembers &cls : members) {
      for (const auto &field : cls.fields) {
        prints.push_back(object + "." + field.first);
      }
    }
    std::vector<Reply> values(prints.size(), Reply{"", false});
    const auto printing = [&](size_t index) -> std::string {
      return "inspector_repl::print(" + slot(values.at(index).value) + ", " +
             prints.at(index) + ");\n";
    };
    const std::string bind = "{ auto &&__inspector_o = (" + expr + ");\n";
    // One evaluation of `expr` for all fields. Should a field not print,
    // each is retried alone so only that one is missing.
    std::string block = bind;
    for (size_t i = 0; i < prints.size(); ++i) {
      block += printing(i);
    }
    diags.clear();
    if (!run(block + "}")) {
      for (size_t i = 0; i < prints.size(); ++i) {
        diags.clear();
        if (!run(bind + printing(i) + "}")) {
          values.at(i) = firstLineOfFailure();
        }
      }
    }
    llvm::json::Array classes;
    auto value = values.begin();
    for (ClassMembers &cls : members) {
      llvm::json::Array fields;
      for (auto &[name, type] : cls.fields) {
        fields.push_back(llvm::json::Object{{"name", std::move(name)},
                                            {"type", std::move(type)},
                                            {"value", oneLine(value->value)},
                                            {"error", value->error}});
        ++value;
      }
      llvm::json::Array methods;
      for (auto &method : cls.methods) {
        methods.emplace_back(std::move(method));
      }
      classes.push_back(llvm::json::Object{{"name", std::move(cls.name)},
                                           {"fields", std::move(fields)},
                                           {"methods", std::move(methods)}});
    }
    return llvm::json::Object{{"type", spell(listed, context)},
                              {"classes", std::move(classes)}};
  }

  [[nodiscard]] auto history() const -> const std::string & {
    return statements;
  }
};

// Mirrors clang's default executor, except for the memory manager. The
// default one maps every module separately, so in programs that reserve huge
// address ranges (e.g. Boehm GC in nix) later modules land gigabytes away
// from earlier ones. That breaks 32-bit relocations to symbols the JIT
// itself defines, like __dso_handle. A single slab keeps them close.
auto executorBuilder(JitErrors &jitErrors)
    -> llvm::Expected<std::unique_ptr<clang::IncrementalExecutorBuilder>> {
  auto target = llvm::orc::JITTargetMachineBuilder::detectHost();
  if (!target) {
    return target.takeError();
  }
  auto jit = std::make_unique<llvm::orc::LLJITBuilder>();
  jit->setJITTargetMachineBuilder(std::move(*target));
  jit->setPrePlatformSetup(
      [&jitErrors](llvm::orc::LLJIT &lljit) -> llvm::Error {
        lljit.getExecutionSession().setErrorReporter(
            [&jitErrors](llvm::Error err) -> void {
              jitErrors.report(std::move(err));
            });
        // Lets gdb see JIT code, best effort as in clang.
        llvm::consumeError(llvm::orc::enableDebuggerSupport(lljit));
        return llvm::Error::success();
      });
  jit->setPlatformSetUp(
      [](llvm::orc::LLJIT &lljit) -> llvm::Expected<llvm::orc::JITDylibSP> {
        auto platform = llvm::orc::setUpGenericLLVMIRPlatform(lljit);
        if (!platform) {
          return platform.takeError();
        }
        const char *atexit = inspector::compat::threadAtexitName();
        if (atexit == nullptr) {
          return platform;
        }
        llvm::orc::SymbolMap interposed;
        interposed.try_emplace(lljit.mangleAndIntern(atexit),
                               inspector::compat::threadAtexit(),
                               llvm::JITSymbolFlags::Exported);
        if (auto err = (*platform)->define(
                llvm::orc::absoluteSymbols(std::move(interposed)))) {
          return err;
        }
        return platform;
      });
  // Address space only: pages are committed as modules are linked.
  constexpr std::size_t slab = std::size_t{1} << 30U;
  auto memory = llvm::orc::MapperJITLinkMemoryManager::CreateWithMapper<
      llvm::orc::InProcessMemoryMapper>(slab);
  if (!memory) {
    return memory.takeError();
  }
  if (auto err =
          inspector::compat::setMemoryManager(*jit, std::move(*memory))) {
    return err;
  }
  auto builder = std::make_unique<clang::IncrementalExecutorBuilder>();
  builder->JITBuilder = std::move(jit);
  return builder;
}

// thread_local destructors run at thread exit, after the session.
struct InterpreterOwner {
  std::unique_ptr<clang::Interpreter> interp;
  explicit InterpreterOwner(std::unique_ptr<clang::Interpreter> interp)
      : interp(std::move(interp)) {}
  InterpreterOwner(const InterpreterOwner &) = delete;
  InterpreterOwner(InterpreterOwner &&) = delete;
  auto operator=(const InterpreterOwner &) -> InterpreterOwner & = delete;
  auto operator=(InterpreterOwner &&) -> InterpreterOwner & = delete;
  ~InterpreterOwner() {
    if (inspector::compat::threadLocalsRegistered()) {
      // NOLINTNEXTLINE(bugprone-unused-return-value): leaked on purpose
      interp.release();
    }
  }
};

// Parsing the file takes seconds in large projects. Only interactive clients
// ask to prepare it in the background, scripted ones never complete.
class LazyCompleter {
  std::string path;
  unsigned line;
  const clang::CompilerInvocation *invocation;
  std::unique_ptr<Completer> completer;
  // Declared after `completer`, so it is joined before that is destroyed.
  std::jthread preparing;

  void make() {
    if (!completer) {
      completer = std::make_unique<Completer>(path, line, *invocation);
    }
  }

public:
  LazyCompleter(std::string path, unsigned line,
                const clang::CompilerInvocation &invocation)
      : path(std::move(path)), line(line), invocation(&invocation) {}

  void prepare() {
    if (!completer) {
      make();
      preparing = std::jthread([this]() -> void { completer->prepare(); });
    }
  }

  // The completer, once its background parse finished.
  auto get() -> Completer & {
    make();
    finish();
    return *completer;
  }

  void finish() {
    if (preparing.joinable()) {
      preparing.join();
    }
  }
};

auto briefValues(Session &session, const std::vector<std::string> &codes)
    -> llvm::json::Object {
  llvm::json::Array values;
  for (const auto &code : codes) {
    const Reply value = session.brief(code);
    values.push_back(
        llvm::json::Object{{"value", value.value}, {"error", value.error}});
  }
  return llvm::json::Object{{"values", std::move(values)}};
}

void runRepl(Prompt &prompt, const char *path, unsigned lineNumber,
             const char *declare, const std::string &context,
             const char *const *includes) {
  prompt.sendInspectorLocation(path, lineNumber, context);

  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  clang::IncrementalCompilerBuilder builder;
  builder.SetTargetTriple(llvm::sys::getProcessTriple());
  // A debugger has to show private state, as gdb does.
  std::vector<const char *> args = {
      "-resource-dir", INSPECTOR_CLANG_RESOURCE_DIR, "-fno-access-control"};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): C API
  for (const char *const *arg = includes; *arg != nullptr; ++arg) {
    args.push_back(*arg);
  }
  builder.SetCompilerArgs(args);

  auto compiler = builder.CreateCpp();
  if (!compiler) {
    prompt.sendReady({llvm::toString(compiler.takeError()), true});
    return;
  }
  JitErrors jitErrors;
  auto executor = executorBuilder(jitErrors);
  if (!executor) {
    prompt.sendReady({llvm::toString(executor.takeError()), true});
    return;
  }
  auto interp =
      clang::Interpreter::create(std::move(*compiler), std::move(*executor));
  if (!interp) {
    prompt.sendReady({llvm::toString(interp.takeError()), true});
    return;
  }
  const InterpreterOwner owner{std::move(*interp)};

  Session session(*owner.interp, jitErrors);
  prompt.sendReady(session.setup(declare, context));
  LazyCompleter completer(path, lineNumber,
                          owner.interp->getCompilerInstance()->getInvocation());
  while (true) {
    const Request request = prompt.getRequest();
    switch (request.kind) {
    case Request::Kind::Prepare:
      completer.prepare();
      continue;
    case Request::Kind::Complete: {
      const Completions found =
          completer.get().complete(session.history(), request.text);
      prompt.sendCompletions(found.names, found.prefix);
      continue;
    }
    case Request::Kind::Ls:
      prompt.sendObject(session.ls(request.text));
      continue;
    case Request::Kind::Values:
      prompt.sendObject(briefValues(session, request.list));
      continue;
    case Request::Kind::Input:
      break;
    }
    if (request.text == ".quit" || request.text == ".abort") {
      prompt.sendResult({"", false});
      if (request.text == ".abort") {
        completer.finish();
        std::exit(1); // NOLINT(concurrency-mt-unsafe): user asked to abort
      }
      return;
    }
    prompt.sendResult(session.eval(request.text));
  }
}

// INSPECTOR_WAIT lets a program start before the REPL.
auto connectPrompt() -> Prompt {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): nothing modifies the environment
  const bool wait = std::getenv("INSPECTOR_WAIT") != nullptr;
  constexpr std::chrono::milliseconds retry{100};
  while (true) {
    try {
      return Prompt(socketPath());
    } catch (Disconnected &) {
      if (!wait) {
        throw;
      }
      std::this_thread::sleep_for(retry);
    }
  }
}

} // namespace

extern "C" void inspectorRunRepl(const char *path, unsigned lineNumber,
                                 const char *declare, const char *context,
                                 const char *const *includes,
                                 const void *const *addresses) {
  try {
    Prompt prompt = connectPrompt();
    runRepl(prompt, path, lineNumber, declare,
            fillAddresses(context, addresses), includes);
  } catch (Disconnected &e) {
    // A breakpoint must never take the program down with it.
    std::cerr << "inspector: " << e.what()
              << " (is `inspector repl` running?), continuing at " << path
              << ":" << lineNumber << "\n";
    // NOLINTNEXTLINE(concurrency-mt-unsafe): nothing modifies the environment
    if (std::getenv("INSPECTOR_REQUIRED") != nullptr) {
      std::exit(1); // NOLINT(concurrency-mt-unsafe): opted in
    }
  }
}
