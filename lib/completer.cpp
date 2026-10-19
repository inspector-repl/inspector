#include "completer.h"

#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/FileSystemOptions.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/LangStandard.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/FrontendActions.h> // NOLINT(misc-include-cleaner): completes unique_ptr<SyntaxOnlyAction>
#include <clang/Frontend/FrontendOptions.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Sema/CodeCompleteConsumer.h>
#include <clang/Sema/CodeCompleteOptions.h>
#include <clang/Sema/Sema.h>
#include <clang/Serialization/PCHContainerOperations.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/VirtualFileSystem.h>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace inspector;

namespace {

// Reserved names (`__x`, `_Upper`) are library internals and only clutter
// the list unless the user is explicitly typing one.
auto isReserved(llvm::StringRef name) -> bool {
  return name.starts_with("__") || (name.starts_with("_") && name.size() > 1 &&
                                    llvm::isUpper(name.drop_front().front()));
}

class Consumer : public clang::CodeCompleteConsumer {
  std::shared_ptr<clang::GlobalCodeCompletionAllocator> allocator =
      std::make_shared<clang::GlobalCodeCompletionAllocator>();
  clang::CodeCompletionTUInfo info{allocator};
  Completions out;

public:
  Consumer() : CodeCompleteConsumer(clang::CodeCompleteOptions{}) {}
  auto take() -> Completions { return std::move(out); }

  void ProcessCodeCompleteResults(clang::Sema &sema,
                                  clang::CodeCompletionContext /*context*/,
                                  clang::CodeCompletionResult *results,
                                  unsigned count) override {
    const llvm::StringRef prefix =
        sema.getPreprocessor().getCodeCompletionFilter();
    out.prefix = prefix.str();
    const bool wantReserved = prefix.starts_with("_");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): C API
    for (const auto &result : llvm::ArrayRef(results, count)) {
      llvm::StringRef name;
      if (result.Kind == clang::CodeCompletionResult::RK_Declaration) {
        if (result.Hidden ||
            !result.Declaration->getDeclName().isIdentifier()) {
          continue;
        }
        name = result.Declaration->getName();
      } else if (result.Kind == clang::CodeCompletionResult::RK_Keyword) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): Kind says so
        name = result.Keyword;
      } else {
        continue;
      }
      if (name.starts_with(prefix) && (wantReserved || !isReserved(name))) {
        out.names.push_back(name.str());
      }
    }
  }

  auto getAllocator() -> clang::CodeCompletionAllocator & override {
    return info.getAllocator();
  }
  auto getCodeCompletionTUInfo() -> clang::CodeCompletionTUInfo & override {
    return info;
  }
};

} // namespace

Completer::Completer(std::string path, unsigned line,
                     const clang::CompilerInvocation &interpreter)
    : path(std::move(path)), line(line),
      invocation(std::make_shared<clang::CompilerInvocation>(interpreter)),
      diagOpts(std::make_shared<clang::DiagnosticOptions>()),
      diags(clang::CompilerInstance::createDiagnostics(
          *llvm::vfs::getRealFileSystem(), *diagOpts, &ignore,
          /*ShouldOwnClient=*/false)),
      files(llvm::makeIntrusiveRefCnt<clang::FileManager>(
          clang::FileSystemOptions{}, llvm::vfs::getRealFileSystem())),
      pch(std::make_shared<clang::PCHContainerOperations>()) {
  std::ifstream file(this->path);
  for (std::string text; std::getline(file, text);) {
    lines.push_back(std::move(text));
  }
  // The reported line can be the one before a `#include INSPECTOR` directive.
  if (this->line < lines.size() &&
      lines.at(this->line).find("INSPECTOR") != std::string::npos) {
    ++this->line;
  }
  // Parse the file as the compiler did, not as incremental REPL input.
  invocation->getLangOpts().IncrementalExtensions = false;
  auto &frontend = invocation->getFrontendOpts();
  frontend.Inputs.clear();
  frontend.Inputs.emplace_back(this->path, clang::Language::CXX);
  frontend.ProgramAction = clang::frontend::ParseSyntaxOnly;
  auto &preprocessor = invocation->getPreprocessorOpts();
  // The interpreter remaps its input to a buffer it owns; a copied pointer
  // would be freed twice.
  preprocessor.clearRemappedFiles();
  // Other breakpoints in the file must not pull in their generated headers.
  preprocessor.addMacroDef("INSIDE_CLING=1");
  preprocessor.addMacroDef("INSPECTOR=<inspector/dummy.h>");
}

auto Completer::source(const std::string &atBreakpoint) const -> std::string {
  std::string out;
  for (size_t i = 0; i < lines.size(); ++i) {
    out += i + 1 == line ? atBreakpoint : lines.at(i);
    out += '\n';
  }
  return out;
}

auto Completer::complete(const std::string &before, const std::string &text)
    -> Completions {
  if (line == 0 || line > lines.size()) {
    return {};
  }
  if (!unit) {
    // Precompiling the preamble (the includes) once makes later requests
    // re-parse only the rest of the file.
    unit = clang::ASTUnit::LoadFromCompilerInvocation(
        invocation, pch, diagOpts, diags, files, /*OnlyLocalDecls=*/false,
        clang::CaptureDiagsKind::None, /*PrecompilePreambleAfterNParses=*/1);
    if (!unit) {
      return {};
    }
  }
  const std::string atBreakpoint = before.empty() ? text : before + "\n" + text;
  const auto lineOffset = static_cast<unsigned>(
      std::count(atBreakpoint.begin(), atBreakpoint.end(), '\n'));
  const size_t lastNewline = text.rfind('\n');
  const auto column = static_cast<unsigned>(
      (lastNewline == std::string::npos ? text.size()
                                        : text.size() - lastNewline - 1) +
      1);

  const auto buffer =
      llvm::MemoryBuffer::getMemBufferCopy(source(atBreakpoint), path);
  const std::pair<std::string, llvm::MemoryBuffer *> remapped{path,
                                                              buffer.get()};
  Consumer consumer;
  clang::LangOptions langOpts;
  const auto sources =
      llvm::makeIntrusiveRefCnt<clang::SourceManager>(*diags, *files);
  llvm::SmallVector<clang::StoredDiagnostic> stored;
  llvm::SmallVector<const llvm::MemoryBuffer *> owned;
  unit->CodeComplete(path, line + lineOffset, column, remapped,
                     /*IncludeMacros=*/false, /*IncludeCodePatterns=*/false,
                     /*IncludeBriefComments=*/false, consumer, pch, diags,
                     langOpts, sources, files, stored, owned,
                     /*Act=*/nullptr);
  // CodeComplete hands back ownership of every buffer it was given or made.
  for (const auto *owner : owned) {
    if (owner != buffer.get()) {
      delete owner; // NOLINT(cppcoreguidelines-owning-memory): clang API
    }
  }
  Completions out = consumer.take();
  std::sort(out.names.begin(), out.names.end());
  out.names.erase(std::unique(out.names.begin(), out.names.end()),
                  out.names.end());
  return out;
}
