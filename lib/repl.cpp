#include "inspector/repl.h"
#include "inspector/prompt.h"

#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/Type.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Interpreter/Interpreter.h>
#include <clang/Interpreter/Value.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
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
#if LLVM_VERSION_MAJOR >= 23
    opts.setShowColors(clang::ShowColorsKind::On);
#else
    opts.ShowColors = true;
#endif
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
  std::string name = value.getType().getAsString(policy);
  // Bound temporaries lose the std::string sugar.
  const std::string longString =
      "std::basic_string<char, std::char_traits<char>, std::allocator<char>>";
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
  unsigned counter = 0;

  auto run(const std::string &code, clang::Value *value = nullptr) -> bool {
    if (auto err = interp.ParseAndExecute(code, value)) {
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
  auto isValueExpr(const std::string &code) -> bool {
    const std::string alias = fresh("__inspector_t");
    clang::Value isVoid;
    const bool typed = run("using " + alias + " = decltype((" + code + "));") &&
                       run("std::is_void_v<" + alias + ">", &isVoid);
    diags.clear();
    return typed && isVoid.hasValue() && !isVoid.getBool();
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

  auto setup(const std::string &declare, const std::string &context) -> Reply {
    diags.clear();
    const bool loaded = run(declare) && run(context);
    return {loaded ? "" : failure(), !loaded};
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
    if (wantValue && isValueExpr(input)) {
      const std::string var = fresh("__inspector_v");
      diags.clear();
      if (run("auto &&" + var + " = (" + input + ");")) {
        return show(var);
      }
    }
    // Without ';' clang-repl parses a value-printing expression and silently
    // skips void calls like `v.push_back(1)`.
    if (wantValue) {
      input += ';';
    }
    diags.clear();
    const bool succeeded = run(input);
    // Warnings on success (e.g. [[nodiscard]]) are noise in a REPL.
    return {succeeded ? "" : failure(), !succeeded};
  }
};

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
  auto interp = clang::Interpreter::create(std::move(*compiler));
  if (!interp) {
    prompt.sendReady({llvm::toString(interp.takeError()), true});
    return;
  }

  Session session(**interp);
  prompt.sendReady(session.setup(declare, context));
  while (true) {
    const std::string statement = prompt.getCppStatement();
    if (statement == ".quit" || statement == ".abort") {
      prompt.sendResult({"", false});
      if (statement == ".abort") {
        std::exit(1); // NOLINT(concurrency-mt-unsafe): user asked to abort
      }
      return;
    }
    prompt.sendResult(session.eval(statement));
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
