#pragma once

#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/FileManager.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Serialization/PCHContainerOperations.h>
#include <llvm/ADT/IntrusiveRefCntPtr.h>

#include <memory>
#include <string>
#include <vector>

namespace inspector {

struct Completions {
  std::vector<std::string> names;
  std::string prefix; // the part of the input the names replace
};

// Completes REPL input as if it were typed at the breakpoint in the original
// source file. Unlike clang's ReplCodeCompleter this sees locals, fields and
// template instantiations, because it parses real code instead of importing
// the interpreter's AST.
class Completer {
  std::string path;
  unsigned line;
  // The file split around the breakpoint line, which input replaces.
  std::string head;
  std::string tail;
  bool found = false;
  std::shared_ptr<clang::CompilerInvocation> invocation;
  std::shared_ptr<clang::DiagnosticOptions> diagOpts;
  clang::IgnoringDiagConsumer ignore; // completion errors are expected noise
  llvm::IntrusiveRefCntPtr<clang::DiagnosticsEngine> diags;
  llvm::IntrusiveRefCntPtr<clang::FileManager> files;
  std::shared_ptr<clang::PCHContainerOperations> pch;
  std::unique_ptr<clang::ASTUnit> unit; // parsed lazily; caches the preamble

public:
  Completer(std::string path, unsigned line,
            const clang::CompilerInvocation &interpreter);

  // `before` is the REPL input preceding `text` (earlier statements whose
  // declarations should be visible); `text` ends at the cursor.
  auto complete(const std::string &before, const std::string &text)
      -> Completions;
};

} // namespace inspector
