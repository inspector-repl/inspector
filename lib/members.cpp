#include "members.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/Type.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace inspector {

namespace {

// `__x` and `_Upper` are library internals.
auto isReserved(llvm::StringRef name) -> bool {
  return name.starts_with("__") || (name.starts_with("_") && name.size() > 1 &&
                                    llvm::isUpper(name.drop_front().front()));
}

auto isListedMethod(const clang::CXXMethodDecl &method) -> bool {
  return !method.isImplicit() && method.getDeclName().isIdentifier() &&
         !isReserved(method.getName()) &&
         !llvm::isa<clang::CXXConstructorDecl, clang::CXXDestructorDecl>(
             method);
}

auto membersOf(const clang::CXXRecordDecl &definition) -> ClassMembers {
  const clang::ASTContext &context = definition.getASTContext();
  ClassMembers members{
      spell(context.getCanonicalTagType(&definition), context), {}, {}};
  for (const clang::FieldDecl *field : definition.fields()) {
    if (field->getDeclName().isIdentifier() && !field->getName().empty() &&
        !isReserved(field->getName())) {
      members.fields.emplace_back(field->getName().str(),
                                  spell(field->getType(), context));
    }
  }
  for (const clang::Decl *decl : definition.decls()) {
    const auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(decl);
    if (const auto *pattern =
            llvm::dyn_cast<clang::FunctionTemplateDecl>(decl)) {
      method =
          llvm::dyn_cast<clang::CXXMethodDecl>(pattern->getTemplatedDecl());
    }
    if (method != nullptr && isListedMethod(*method)) {
      members.methods.push_back(method->getName().str());
    }
  }
  std::ranges::sort(members.methods);
  const auto duplicates = std::ranges::unique(members.methods);
  members.methods.erase(duplicates.begin(), duplicates.end());
  return members;
}

} // namespace

auto spell(clang::QualType type, const clang::ASTContext &context)
    -> std::string {
  clang::PrintingPolicy policy(context.getLangOpts());
  policy.SuppressDefaultTemplateArgs = true;
  policy.FullyQualifiedName = true;
  // Canonical drops the generated inspector_repl::type<> wrapper.
  std::string name = type.getCanonicalType().getAsString(policy);
  const std::string longString = "std::basic_string<char>";
  size_t pos = 0;
  while ((pos = name.find(longString)) != std::string::npos) {
    name.replace(pos, longString.size(), "std::string");
  }
  return name;
}

auto listMembers(const clang::CXXRecordDecl &record)
    -> std::vector<ClassMembers> {
  llvm::SmallPtrSet<const clang::CXXRecordDecl *, 4> seen;
  std::vector<ClassMembers> out;
  // Depth first: bases are pushed in reverse so the first base comes next.
  std::vector<const clang::CXXRecordDecl *> pending{&record};
  while (!pending.empty()) {
    const clang::CXXRecordDecl *definition = pending.back()->getDefinition();
    pending.pop_back();
    if (definition == nullptr || !seen.insert(definition).second) {
      continue;
    }
    ClassMembers members = membersOf(*definition);
    // Tags and mixins without members are noise, except as the listed type.
    if (out.empty() || !members.fields.empty() || !members.methods.empty()) {
      out.push_back(std::move(members));
    }
    std::vector<const clang::CXXRecordDecl *> bases;
    for (const clang::CXXBaseSpecifier &base : definition->bases()) {
      if (const auto *baseRecord = base.getType()->getAsCXXRecordDecl()) {
        bases.push_back(baseRecord);
      }
    }
    pending.insert(pending.end(), bases.rbegin(), bases.rend());
  }
  return out;
}

} // namespace inspector
