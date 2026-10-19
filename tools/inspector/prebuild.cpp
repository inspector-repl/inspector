#include "prebuild.h"

#include "driver.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/ASTTypeTraits.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/ParentMapContext.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/Type.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Driver/CreateInvocationFromArgs.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Serialization/PCHContainerOperations.h>
#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/VirtualFileSystem.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace inspector::prebuild {

namespace {

// A local the REPL can reach by reference.
struct Captured {
  std::string name;
  clang::QualType type;
};

auto isBitField(const clang::BindingDecl &binding) -> bool {
  const clang::Expr *expr = binding.getBinding();
  if (expr == nullptr) {
    return false;
  }
  const auto *member =
      llvm::dyn_cast<clang::MemberExpr>(expr->IgnoreImplicit());
  if (member == nullptr) {
    return false;
  }
  const auto *field = llvm::dyn_cast<clang::FieldDecl>(member->getMemberDecl());
  return field != nullptr && field->isBitField();
}

auto systemIncludeFlags(const Job &job) -> std::vector<std::string> {
  std::vector<std::string> flags;
  flags.reserve(job.systemIncludes.size());
  for (const auto &dir : job.systemIncludes) {
    flags.push_back("-isystem" + dir);
  }
  return flags;
}

// Variables `stmt` declares into its enclosing scope.
void declared(const clang::Stmt *stmt, std::vector<Captured> &out) {
  const auto *decls = llvm::dyn_cast_if_present<clang::DeclStmt>(stmt);
  if (decls == nullptr) {
    return;
  }
  for (const clang::Decl *decl : decls->decls()) {
    // Structured bindings: the bindings are addressable unless they
    // denote a bit-field; the hidden variable has no name.
    if (const auto *bindings = llvm::dyn_cast<clang::DecompositionDecl>(decl)) {
      for (const clang::BindingDecl *binding : bindings->bindings()) {
        if (!isBitField(*binding)) {
          out.push_back({binding->getName().str(), binding->getType()});
        }
      }
    } else if (const auto *var = llvm::dyn_cast<clang::VarDecl>(decl);
               var != nullptr && !var->isImplicit() &&
               var->getIdentifier() != nullptr) {
      out.push_back({var->getName().str(), var->getType()});
    }
  }
}

// Declarations `parent` makes visible to its child `node`: earlier sibling
// statements, or the parameters of a function. Returns false at a function
// or class, whose enclosing locals are out of reach.
auto scope(const clang::DynTypedNode &parent, const clang::DynTypedNode &node,
           std::vector<Captured> &out) -> bool {
  const auto *function = parent.get<clang::FunctionDecl>();
  if (const auto *lambda = parent.get<clang::LambdaExpr>()) {
    function = lambda->getCallOperator();
  }
  if (function != nullptr) {
    for (const clang::ParmVarDecl *param : function->parameters()) {
      if (param->getIdentifier() != nullptr) {
        out.push_back({param->getName().str(), param->getType()});
      }
    }
    return false;
  }
  if (parent.get<clang::RecordDecl>() != nullptr) {
    return false;
  }
  if (const auto *stmt = parent.get<clang::Stmt>()) {
    for (const clang::Stmt *sibling : stmt->children()) {
      if (sibling == node.get<clang::Stmt>()) {
        break;
      }
      declared(sibling, out);
    }
  }
  return true;
}

// Locals visible at `breakpoint` in declaration order, inner ones
// shadowing outer ones of the same name.
auto visibleAt(clang::ASTContext &context, const clang::Stmt &breakpoint)
    -> std::vector<Captured> {
  std::vector<Captured> visible; // innermost last-declared first
  std::set<std::string> seen;
  auto node = clang::DynTypedNode::create(breakpoint);
  for (bool open = true; open;) {
    const auto parents = context.getParents(node);
    if (parents.empty()) {
      break;
    }
    std::vector<Captured> declaredHere;
    open = scope(*parents.begin(), node, declaredHere);
    for (const auto &local : std::views::reverse(declaredHere)) {
      if (seen.insert(local.name).second) {
        visible.push_back(local);
      }
    }
    node = *parents.begin();
  }
  std::ranges::reverse(visible);
  return visible;
}

// Spells `type` so the REPL can name it, or returns "" if it can't.
auto spell(const clang::ASTContext &context, clang::QualType type)
    -> std::string {
  // A template's locals have no concrete type until instantiation.
  if (type->isDependentType()) {
    return "";
  }
  clang::PrintingPolicy policy(context.getLangOpts());
  // (anonymous namespace)::X is reachable as X: the REPL includes the file.
  policy.SuppressUnwrittenScope = true;
  // &ref already points at the referenced object.
  std::string spelling =
      type.getNonReferenceType().getCanonicalType().getAsString(policy);
  // Lambdas and unnamed types have no spelling the REPL could parse.
  for (const char *unnamed : {"(lambda at", "(unnamed", "(anonymous"}) {
    if (spelling.find(unnamed) != std::string::npos) {
      return "";
    }
  }
  return spelling;
}

auto header(const std::string &file, unsigned line,
            const std::vector<Captured> &locals,
            const clang::ASTContext &context,
            const std::vector<std::string> &replFlags) -> std::string {
  std::string prelude;
  std::string addresses;
  for (const auto &local : locals) {
    const std::string type = spell(context, local.type);
    if (!type.empty()) {
      // A template argument keeps arrays and function pointers valid.
      prelude += llvm::formatv(
          "{0}inspector_repl::type<{1}>& {2} = *(inspector_repl::type<{1}>*) "
          "%p;",
          prelude.empty() ? "" : "\n", type, local.name);
      addresses += llvm::formatv("{0}(const void*) &{1}",
                                 addresses.empty() ? "" : ", ", local.name);
    }
  }
  std::string includes;
  for (const auto &flag : replFlags) {
    includes += llvm::formatv("\"{0}\",\n        ", escapeCString(flag));
  }
  const std::string declare =
      llvm::formatv("#define INSIDE_CLING 1\n#define INSPECTOR "
                    "<inspector/dummy.h>\n#include \"{0}\"",
                    file);
  return llvm::formatv(R"(
#ifndef INSIDE_CLING /* yo dawg, I heard you like repls */
{{
    static const char* const __inspector_includes[] = {{
        {0}0
    };
    const void* const __inspector_addresses[] = {{ {1} };
    /* C linkage comes from <inspector/repl.h>, force-included by the build. */
    inspectorRunRepl(
        /* file         */ "{2}",
        /* line         */  {3},
        /* declare      */ "{4}",
        /* prelude      */ "{5}",
        /* includes     */ __inspector_includes,
        /* addresses    */ __inspector_addresses
    );
}
#endif
)",
                       includes, addresses.empty() ? "0" : addresses,
                       escapeCString(file), line, escapeCString(declare),
                       escapeCString(prelude));
}

auto readFile(const fs::path &path) -> std::string {
  auto buffer = llvm::MemoryBuffer::getFile(path.string());
  return buffer ? (*buffer)->getBuffer().str() : "";
}

// Where `#include INSPECTOR` at `file`:`line` resolves to. The kernel walks
// every component of the unnormalized path, so each directory before a
// ".." must exist, including names like `"..` from the quoted __FILE__.
auto headerPath(const fs::path &outdir, llvm::StringRef file, unsigned line)
    -> fs::path {
  const fs::path raw = driver::includeRoot(outdir) / "inspector" /
                       llvm::formatv("\"{0}\"-{1}", file, line).str();
  const fs::path resolved = raw.lexically_normal();
  if (*resolved.lexically_relative(outdir).begin() == "..") {
    throw std::runtime_error(
        llvm::formatv("{0} climbs more than {1} directories up. Compile it "
                      "from a closer directory.",
                      file, driver::climbLimit)
            .str());
  }
  fs::path walked;
  for (const auto &part : raw.parent_path()) {
    walked /= part;
    if (part != "..") {
      fs::create_directories(walked);
    }
  }
  return resolved;
}

// Keeps the mtime stable so build systems don't recompile needlessly.
void writeIfChanged(const fs::path &path, const std::string &content) {
  if (fs::exists(path) && readFile(path) == content) {
    return;
  }
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << content;
}

auto parse(const Job &job) -> std::unique_ptr<clang::ASTUnit> {
  std::vector<std::string> args = {"clang++", "-x", "c++"};
  args.insert(args.end(), job.flags.begin(), job.flags.end());
  args.insert(args.end(), {"-I" + driver::paths().include.string(),
                           "-DINSPECTOR=<inspector/dummy.h>", "-resource-dir",
                           INSPECTOR_CLANG_RESOURCE_DIR,
                           "-working-directory=" + job.directory.string()});
  const auto system = systemIncludeFlags(job);
  args.insert(args.end(), system.begin(), system.end());
  args.push_back(job.source);
  std::vector<const char *> argv;
  argv.reserve(args.size());
  for (const auto &arg : args) {
    argv.push_back(arg.c_str());
  }
  // Code the compiler rejects fails later with a better message, so parse
  // as far as possible and only give up without an AST.
  clang::IgnoringDiagConsumer ignore;
  const auto diagOpts = std::make_shared<clang::DiagnosticOptions>();
  const auto diags = clang::CompilerInstance::createDiagnostics(
      *llvm::vfs::getRealFileSystem(), *diagOpts, &ignore,
      /*ShouldOwnClient=*/false);
  clang::CreateInvocationOptions options;
  options.Diags = diags;
  options.RecoverOnError = true;
  const std::shared_ptr<clang::CompilerInvocation> invocation =
      clang::createInvocation(argv, options);
  if (!invocation) {
    throw std::runtime_error("cannot parse the command line for " + job.source);
  }
  const auto files = llvm::makeIntrusiveRefCnt<clang::FileManager>(
      invocation->getFileSystemOpts(), llvm::vfs::getRealFileSystem());
  auto unit = clang::ASTUnit::LoadFromCompilerInvocation(
      invocation, std::make_shared<clang::PCHContainerOperations>(), diagOpts,
      diags, files, /*OnlyLocalDecls=*/false, clang::CaptureDiagsKind::None);
  if (!unit) {
    throw std::runtime_error("unable to parse " + job.source);
  }
  return unit;
}

} // namespace

auto escapeCString(const std::string &text) -> std::string {
  std::string out;
  llvm::raw_string_ostream stream(out);
  for (const char byte : text) {
    const auto value = static_cast<unsigned char>(byte);
    constexpr unsigned char firstPrintable = ' ';
    constexpr unsigned char lastPrintable = '~';
    if (value < firstPrintable || value > lastPrintable || byte == '\\' ||
        byte == '"') {
      stream << llvm::format("\\%03o", value);
    } else {
      stream << byte;
    }
  }
  return out;
}

auto generate(const Job &job) -> std::vector<fs::path> {
  // Cheap enough for every compile, so unrelated files never pay for a
  // parse.
  if (!job.scanAll && readFile(job.directory / job.source).find("INSPECTOR") ==
                          std::string::npos) {
    return {};
  }
  const auto unit = parse(job);
  clang::ASTContext &context = unit->getASTContext();
  const clang::SourceManager &sources = context.getSourceManager();

  std::vector<std::string> forRepl =
      driver::replFlags(job.flags, job.directory);
  forRepl.push_back("-I" + driver::paths().include.string());
  const auto system = systemIncludeFlags(job);
  forRepl.insert(forRepl.end(), system.begin(), system.end());

  // `#include INSPECTOR` pulls in dummy.h's empty block: where it was
  // included is the breakpoint, the block's place in the AST its scope.
  using namespace clang::ast_matchers;
  std::vector<fs::path> written;
  for (const auto &match :
       match(compoundStmt(isExpansionInFileMatching("inspector/dummy\\.h$"))
                 .bind("breakpoint"),
             context)) {
    const auto *block = match.getNodeAs<clang::CompoundStmt>("breakpoint");
    const clang::SourceLocation include = sources.getExpansionLoc(
        sources.getIncludeLoc(sources.getFileID(block->getBeginLoc())));
    const clang::PresumedLoc presumed = sources.getPresumedLoc(include);
    const auto source =
        sources.getFileEntryRefForID(sources.getFileID(include));
    if (presumed.isInvalid() || !source) {
      continue;
    }
    const fs::path path =
        headerPath(job.outdir, presumed.getFilename(), presumed.getLine());
    writeIfChanged(path, header((job.directory / source->getName().str())
                                    .lexically_normal()
                                    .string(),
                                presumed.getLine(), visibleAt(context, *block),
                                context, forRepl));
    written.push_back(path);
  }
  return written;
}

} // namespace inspector::prebuild
