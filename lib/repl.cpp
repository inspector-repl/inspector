#include "inspector/repl.h"
#include "compat.h"
#include "completer.h"
#include "inspector/prompt.h"

#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/Type.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Interpreter/IncrementalExecutor.h>
#include <clang/Interpreter/Interpreter.h>
#include <clang/Interpreter/Value.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/ExecutionEngine/Orc/Debugging/DebuggerSupport.h>
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/MapperJITLinkMemoryManager.h>
#include <llvm/ExecutionEngine/Orc/MemoryMapper.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
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
  clang::PrintingPolicy policy(value.getASTContext().getLangOpts());
  policy.SuppressDefaultTemplateArgs = true;
  policy.FullyQualifiedName = true;
  // Canonical drops the generated inspector_repl::type<> wrapper.
  std::string name = value.getType().getCanonicalType().getAsString(policy);
  const std::string longString = "std::basic_string<char>";
  size_t pos = 0;
  while ((pos = name.find(longString)) != std::string::npos) {
    name.replace(pos, longString.size(), "std::string");
  }
  return name;
}

auto isRecord(const clang::Value &value) -> bool {
  return value.getType().getNonReferenceType()->isRecordType();
}

class Session {
  clang::Interpreter &interp;
  DiagCapture diags;
  std::string printed; // written by inspector_repl::print inside the JIT
  std::string lastError;
  bool threw = false;
  std::string statements; // successful input, so completion sees its names
  unsigned counter = 0;

  auto run(const std::string &code, clang::Value *value = nullptr) -> bool {
    llvm::Error err = llvm::Error::success();
    threw = true;
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
    if (err) {
      // JIT link errors (e.g. missing symbols) don't go through diagnostics.
      lastError = llvm::toString(std::move(err));
      return false;
    }
    return true;
  }

  auto failure() -> std::string {
    const std::string msg = diags.take();
    return msg.empty() ? lastError : msg;
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

public:
  explicit Session(clang::Interpreter &interp) : interp(interp), diags(interp) {
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
    if (!type.empty()) {
      const std::string var = fresh("__inspector_v");
      diags.clear();
      // (void) keeps a user-defined operator, out of the comma.
      if (run("inspector_repl::stored<" + type + "> " + var +
              " = ((void)inspector_repl::runtime(), (" + input + "));")) {
        return show(var);
      }
      // Re-running would repeat side effects that led up to the throw.
      if (threw) {
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

  [[nodiscard]] auto history() const -> const std::string & {
    return statements;
  }
};

// Mirrors clang's default executor, except for the memory manager. The
// default one maps every module separately, so in programs that reserve huge
// address ranges (e.g. Boehm GC in nix) later modules land gigabytes away
// from earlier ones. That breaks 32-bit relocations to symbols the JIT
// itself defines, like __dso_handle. A single slab keeps them close.
auto executorBuilder()
    -> llvm::Expected<std::unique_ptr<clang::IncrementalExecutorBuilder>> {
  auto target = llvm::orc::JITTargetMachineBuilder::detectHost();
  if (!target) {
    return target.takeError();
  }
  auto jit = std::make_unique<llvm::orc::LLJITBuilder>();
  jit->setJITTargetMachineBuilder(std::move(*target));
  jit->setPrePlatformSetup([](llvm::orc::LLJIT &lljit) -> llvm::Error {
    // Lets gdb see JIT code, best effort as in clang.
    llvm::consumeError(llvm::orc::enableDebuggerSupport(lljit));
    return llvm::Error::success();
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

void runRepl(Prompt &prompt, const char *path, unsigned lineNumber,
             const char *declare, const std::string &context,
             const char *const *includes) {
  prompt.sendInspectorLocation(path, lineNumber, context);

  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  clang::IncrementalCompilerBuilder builder;
  builder.SetTargetTriple(llvm::sys::getProcessTriple());
  std::vector<const char *> args = {"-resource-dir",
                                    INSPECTOR_CLANG_RESOURCE_DIR};
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
  auto executor = executorBuilder();
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

  Session session(**interp);
  prompt.sendReady(session.setup(declare, context));
  // Built on the first completion request: the parse costs a few hundred ms.
  std::unique_ptr<Completer> completer;
  while (true) {
    const Request request = prompt.getRequest();
    if (request.kind == Request::Kind::Complete) {
      if (!completer) {
        completer = std::make_unique<Completer>(
            path, lineNumber,
            (*interp)->getCompilerInstance()->getInvocation());
      }
      const Completions found =
          completer->complete(session.history(), request.text);
      prompt.sendCompletions(found.names, found.prefix);
      continue;
    }
    if (request.text == ".quit" || request.text == ".abort") {
      prompt.sendResult({"", false});
      if (request.text == ".abort") {
        std::exit(1); // NOLINT(concurrency-mt-unsafe): user asked to abort
      }
      return;
    }
    prompt.sendResult(session.eval(request.text));
  }
}

} // namespace

extern "C" void inspectorRunRepl(const char *path, unsigned lineNumber,
                                 const char *declare, const char *context,
                                 const char *const *includes,
                                 const void *const *addresses) {
  try {
    Prompt prompt(socketPath());
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
