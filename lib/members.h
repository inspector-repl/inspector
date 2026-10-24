#pragma once

#include <clang/AST/DeclCXX.h>
#include <clang/AST/Type.h>

#include <string>
#include <utility>
#include <vector>

namespace inspector {

// Members of one class, as `.ls` shows them.
struct ClassMembers {
  std::string name;
  std::vector<std::pair<std::string, std::string>> fields; // name, type
  std::vector<std::string> methods;                        // sorted, unique
};

// How the REPL spells types: qualified, without default template arguments
// and with std::string for std::basic_string<char>.
auto spell(clang::QualType type, const clang::ASTContext &context)
    -> std::string;

// `record` followed by its bases, depth first, each listed once. Reserved
// names, operators, constructors and compiler-generated members are left
// out, and so are bases left without members.
auto listMembers(const clang::CXXRecordDecl &record)
    -> std::vector<ClassMembers>;

} // namespace inspector
