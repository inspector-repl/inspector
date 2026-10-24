#include "completer.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Type.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/FileSystemOptions.h>
#include <clang/Basic/IdentifierTable.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/LangStandard.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/FrontendActions.h> // NOLINT(misc-include-cleaner): completes unique_ptr<SyntaxOnlyAction>
#include <clang/Frontend/FrontendOptions.h>
#include <clang/Lex/Lexer.h>
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
#include <llvm/Support/Casting.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/VirtualFileSystem.h>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <memory>
#include <optional>
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
  Consumer() : CodeCompleteConsumer(options()) {}

  static auto options() -> clang::CodeCompleteOptions {
    clang::CodeCompleteOptions opts;
    // Members behind `.` on a pointer come with a fix-it to `->`.
    opts.IncludeFixIts = true;
    return opts;
  }
  auto take() -> Completions { return std::move(out); }

  void ProcessCodeCompleteResults(clang::Sema &sema,
                                  clang::CodeCompletionContext /*context*/,
                                  clang::CodeCompletionResult *results,
                                  unsigned count) override {
    const llvm::StringRef prefix =
        sema.getPreprocessor().getCodeCompletionFilter();
    const bool wantReserved = prefix.starts_with("_");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): C API
    const llvm::ArrayRef all(results, count);
    // The reply has one prefix. With a fix-it (`.` to `->`) every name
    // carries its operator, so the prefix spans the typed `.` as well.
    std::string removed;
    for (const auto &result : all) {
      if (!result.FixIts.empty()) {
        removed = clang::Lexer::getSourceText(result.FixIts.front().RemoveRange,
                                              sema.getSourceManager(),
                                              sema.getLangOpts())
                      .str();
        break;
      }
    }
    out.prefix = removed + prefix.str();
    for (const auto &result : all) {
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
      if (!name.starts_with(prefix) || (!wantReserved && isReserved(name))) {
        continue;
      }
      std::string access = removed;
      for (const auto &fix : result.FixIts) {
        access = fix.CodeToInsert;
      }
      out.names.push_back(access + name.str());
    }
  }

  auto getAllocator() -> clang::CodeCompletionAllocator & override {
    return info.getAllocator();
  }
  auto getCodeCompletionTUInfo() -> clang::CodeCompletionTUInfo & override {
    return info;
  }
};

// Like qualified lookup, plus the members of anonymous namespaces, which
// DeclContext::lookup leaves to Sema's using-directive handling.
auto lookupIn(const clang::DeclContext &scope,
              const clang::IdentifierInfo &name) -> const clang::NamedDecl * {
  const auto found = scope.lookup(&name);
  if (!found.empty()) {
    return found.front();
  }
  for (const clang::Decl *member : scope.decls()) {
    const auto *anonymous = llvm::dyn_cast<clang::NamespaceDecl>(member);
    if (anonymous != nullptr && anonymous->isAnonymousNamespace()) {
      const auto inner = anonymous->lookup(&name);
      if (!inner.empty()) {
        return inner.front();
      }
    }
  }
  return nullptr;
}

// The scope `decl` names, for the next part of a qualified name.
auto scopeOf(const clang::NamedDecl &decl) -> const clang::DeclContext * {
  const clang::Decl *named = &decl;
  if (const auto *alias = llvm::dyn_cast<clang::TypedefNameDecl>(named)) {
    named = alias->getUnderlyingType()->getAsTagDecl();
  } else if (const auto *pattern =
                 llvm::dyn_cast<clang::ClassTemplateDecl>(named)) {
    named = pattern->getTemplatedDecl();
  } else if (const auto *alias =
                 llvm::dyn_cast<clang::NamespaceAliasDecl>(named)) {
    named = alias->getNamespace();
  }
  if (const auto *tag = llvm::dyn_cast_or_null<clang::TagDecl>(named)) {
    named = tag->getDefinition();
  }
  return llvm::dyn_cast_or_null<clang::DeclContext>(named);
}

auto definitionOf(const clang::NamedDecl &decl) -> clang::SourceLocation {
  const clang::Decl *named = &decl;
  if (const auto *pattern = llvm::dyn_cast<clang::TemplateDecl>(named)) {
    named = pattern->getTemplatedDecl();
  }
  const clang::Decl *definition = nullptr;
  if (const auto *tag = llvm::dyn_cast<clang::TagDecl>(named)) {
    definition = tag->getDefinition();
  } else if (const auto *function =
                 llvm::dyn_cast<clang::FunctionDecl>(named)) {
    definition = function->getDefinition();
  } else if (const auto *variable = llvm::dyn_cast<clang::VarDecl>(named)) {
    definition = variable->getDefinition();
  }
  return (definition != nullptr ? definition : &decl)->getLocation();
}

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
  unsigned number = 0;
  for (std::string text; std::getline(file, text);) {
    ++number;
    if (number == this->line) {
      found = true;
    } else {
      (number < this->line ? head : tail) += text + "\n";
    }
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

void Completer::prepare() {
  if (found) {
    load();
  }
}

auto Completer::load() -> bool {
  if (!unit) {
    // Precompiling the preamble (the includes) once makes later requests
    // re-parse only the rest of the file.
    unit = clang::ASTUnit::LoadFromCompilerInvocation(
        invocation, pch, diagOpts, diags, files, /*OnlyLocalDecls=*/false,
        clang::CaptureDiagsKind::None, /*PrecompilePreambleAfterNParses=*/1);
  }
  return unit != nullptr;
}

auto Completer::locate(llvm::StringRef name) -> std::optional<Location> {
  if (!load()) {
    return std::nullopt;
  }
  clang::ASTContext &context = unit->getASTContext();
  const clang::DeclContext *scope = context.getTranslationUnitDecl();
  const clang::NamedDecl *decl = nullptr;
  name.consume_front("::");
  while (!name.empty()) {
    const auto [part, rest] = name.split("::");
    name = rest;
    if (scope == nullptr) {
      return std::nullopt;
    }
    decl = lookupIn(*scope, context.Idents.get(part.trim()));
    if (decl == nullptr) {
      return std::nullopt;
    }
    scope = scopeOf(*decl);
  }
  if (decl == nullptr) {
    return std::nullopt;
  }
  const clang::SourceManager &sources = context.getSourceManager();
  const clang::PresumedLoc where =
      sources.getPresumedLoc(sources.getExpansionLoc(definitionOf(*decl)));
  if (where.isInvalid()) {
    return std::nullopt;
  }
  return Location{where.getFilename(), where.getLine()};
}

auto Completer::complete(const std::string &before, const std::string &text)
    -> Completions {
  if (!found || !load()) {
    return {};
  }
  const std::string atBreakpoint = before.empty() ? text : before + "\n" + text;
  const auto lineOffset = static_cast<unsigned>(
      std::count(atBreakpoint.begin(), atBreakpoint.end(), '\n'));
  const size_t lastNewline = text.rfind('\n');
  const auto column = static_cast<unsigned>(
      (lastNewline == std::string::npos ? text.size()
                                        : text.size() - lastNewline - 1) +
      1);

  const auto buffer = llvm::MemoryBuffer::getMemBufferCopy(
      head + atBreakpoint + "\n" + tail, path);
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
  std::ranges::sort(out.names);
  const auto duplicates = std::ranges::unique(out.names);
  out.names.erase(duplicates.begin(), duplicates.end());
  return out;
}
