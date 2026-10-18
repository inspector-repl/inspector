#include "inspector/repl.h"
#include "inspector/prompt.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Type.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Interpreter/Interpreter.h>
#include <clang/Interpreter/Value.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace inspector;

namespace {

// Routes clang diagnostics into a string so they reach the REPL user instead
// of the inspected program's stderr.
class DiagCapture {
  std::string buffer;
  llvm::raw_string_ostream os{buffer};
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
    auto *ci = interp.getCompilerInstance();
    auto *printer = new clang::TextDiagnosticPrinter(os, opts);
    // The interpreter's source file is already open; the replaced printer
    // would otherwise render without LangOptions and crash.
    printer->BeginSourceFile(ci->getLangOpts(), &ci->getPreprocessor());
    ci->getDiagnostics().setClient(printer, true);
  }
  void clear() { buffer.clear(); }
  std::string take() {
    std::string out = std::move(buffer);
    buffer.clear();
    return out;
  }
};

class Session {
  clang::Interpreter &interp;
  DiagCapture diags;
  std::string printed; // written by __inspector::print inside the JIT
  unsigned counter = 0;

  std::string lastError;

  bool run(const std::string &code, clang::Value *value = nullptr) {
    if (auto err = interp.ParseAndExecute(code, value)) {
      // JIT link errors (e.g. missing symbols) don't go through diagnostics.
      lastError = llvm::toString(std::move(err));
      return false;
    }
    return true;
  }

  std::string failure() {
    std::string msg = diags.take();
    return msg.empty() ? lastError : msg;
  }

  std::string typeName(const clang::Value &v) {
    clang::PrintingPolicy policy(v.getASTContext().getLangOpts());
    policy.SuppressDefaultTemplateArgs = true;
    policy.FullyQualifiedName = true;
    std::string name = v.getType().getAsString(policy);
    // Bound temporaries lose the std::string sugar.
    const std::string longString =
        "std::basic_string<char, std::char_traits<char>, "
        "std::allocator<char>>";
    for (size_t pos; (pos = name.find(longString)) != std::string::npos;)
      name.replace(pos, longString.size(), "std::string");
    return name;
  }

  bool isRecord(const clang::Value &v) {
    clang::QualType t = v.getType().getNonReferenceType();
    return t->isRecordType();
  }

  // decltype is unevaluated, so this probe instantiates no function bodies.
  // A failed instantiation inside a discarded input would otherwise leave
  // the JIT referencing symbols that were never emitted.
  bool isValueExpr(const std::string &code) {
    std::string alias = "__inspector_t" + std::to_string(counter++);
    clang::Value isVoid;
    bool ok = run("using " + alias + " = decltype((" + code + "));") &&
              run("std::is_void_v<" + alias + ">", &isVoid);
    diags.clear();
    return ok && isVoid.hasValue() && !isVoid.getBool();
  }

public:
  explicit Session(clang::Interpreter &interp) : interp(interp), diags(interp) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "static std::string &__inspector_out = *(std::string *)%p;",
             static_cast<void *>(&printed));
    run("#include <inspector/repl_prelude.h>\n" + std::string(buf));
    diags.clear();
  }

  Reply setup(const char *declare, const char *context) {
    diags.clear();
    bool ok = run(declare) && run(context);
    return {ok ? "" : failure(), !ok};
  }

  // Expressions are bound to a variable first so class types can be rendered
  // by the prelude and evaluated exactly once. Declarations/statements fail
  // that parse and are run verbatim.
  Reply eval(const std::string &input) {
    std::string trimmed = input;
    while (!trimmed.empty() && isspace(trimmed.back()))
      trimmed.pop_back();
    if (trimmed.empty())
      return {"", false};

    bool wantValue = trimmed.back() != ';' && trimmed.back() != '}' &&
                     trimmed.front() != '#';
    if (wantValue && isValueExpr(trimmed)) {
      std::string var = "__inspector_v" + std::to_string(counter++);
      diags.clear();
      if (run("auto &&" + var + " = (" + trimmed + ");")) {
        clang::Value value;
        if (run(var, &value) && value.hasValue()) {
          std::string type = typeName(value);
          if (isRecord(value)) {
            printed.clear();
            run("__inspector::print(__inspector_out, " + var + ");");
            return {"(" + type + ") " + printed, false};
          }
          std::string out;
          llvm::raw_string_ostream os(out);
          value.print(os);
          while (!out.empty() && out.back() == '\n')
            out.pop_back();
          return {out, false};
        }
        return {failure(), true};
      }
    }
    // Without ';' clang-repl parses a value-printing expression and silently
    // skips void calls like `v.push_back(1)`.
    if (wantValue)
      trimmed += ';';
    diags.clear();
    bool ok = run(trimmed);
    std::string msg = failure();
    // Warnings on success (e.g. [[nodiscard]]) are noise in a REPL.
    return {ok ? "" : msg, !ok};
  }
};

} // namespace

static void runRepl(Prompt &prompt, const char *path, unsigned lineNumber,
                    const char *declare, const char *context,
                    const char *const *includes) {
  prompt.sendInspectorLocation(path, lineNumber, context);

  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  clang::IncrementalCompilerBuilder CB;
  CB.SetTargetTriple(llvm::sys::getProcessTriple());
  std::vector<const char *> args = {"-resource-dir",
                                    INSPECTOR_CLANG_RESOURCE_DIR};
  for (auto p = includes; p && *p; ++p)
    args.push_back(*p);
  CB.SetCompilerArgs(args);

  auto CI = CB.CreateCpp();
  if (!CI)
    return prompt.sendReady({llvm::toString(CI.takeError()), true});
  auto interp = clang::Interpreter::create(std::move(*CI));
  if (!interp)
    return prompt.sendReady({llvm::toString(interp.takeError()), true});

  Session session(**interp);
  prompt.sendReady(session.setup(declare, context));
  while (true) {
    std::string statement = prompt.getCppStatement();
    if (statement == ".quit" || statement == ".abort") {
      prompt.sendResult({"", false});
      if (statement == ".abort")
        exit(1);
      return;
    }
    prompt.sendResult(session.eval(statement));
  }
}

static void inspectorRunReplImpl(const char *path, unsigned lineNumber,
                                 const char *declare, const char *context,
                                 const char *const *includes) {
  try {
    Prompt prompt(socketPath());
    runRepl(prompt, path, lineNumber, declare, context, includes);
  } catch (Disconnected &e) {
    // A breakpoint must never take the program down with it.
    std::cerr << "inspector: " << e.what() << " (is `inspector repl` running?)"
              << ", continuing at " << path << ":" << lineNumber << std::endl;
    if (getenv("INSPECTOR_REQUIRED"))
      exit(1);
  }
}

static void runReplVa(const char *path, unsigned lineNumber,
                      const char *clingDeclare, const char *clingContext,
                      const char *const *clingIncludes, va_list arglist) {
  va_list copy;
  va_copy(copy, arglist);
  int size = vsnprintf(nullptr, 0, clingContext, copy);
  va_end(copy);
  std::string context(size + 1, '\0');
  vsnprintf(context.data(), context.size(), clingContext, arglist);
  context.resize(size);
  inspectorRunReplImpl(path, lineNumber, clingDeclare, context.c_str(),
                       clingIncludes);
}

extern "C" {
void inspectorRunRepl(const char *path, unsigned lineNumber,
                      const char *clingDeclare, const char *clingContext,
                      const char *const *clingIncludes, ...) {
  va_list arglist;
  va_start(arglist, clingIncludes);
  runReplVa(path, lineNumber, clingDeclare, clingContext, clingIncludes,
            arglist);
  va_end(arglist);
}

// Generated headers declare inspectorRunRepl with C++ linkage.
__attribute__((weak)) void _Z16inspectorRunReplPKcjS0_S0_PKS0_z(
    const char *path, unsigned lineNumber, const char *clingDeclare,
    const char *clingContext, const char *const *clingIncludes, ...) {
  va_list arglist;
  va_start(arglist, clingIncludes);
  runReplVa(path, lineNumber, clingDeclare, clingContext, clingIncludes,
            arglist);
  va_end(arglist);
}
}
