#include "highlight.h"

#include <clang/Basic/IdentifierTable.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/TokenKinds.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Token.h>
#include <llvm/ADT/StringRef.h>

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

using namespace inspector;

namespace {

auto cppOptions() -> const clang::LangOptions & {
  static const clang::LangOptions opts = []() -> clang::LangOptions {
    clang::LangOptions lang;
    lang.CPlusPlus = lang.CPlusPlus11 = lang.CPlusPlus14 = lang.CPlusPlus17 =
        lang.CPlusPlus20 = true;
    lang.Bool = lang.WChar = lang.LineComment = lang.Char8 = true;
    return lang;
  }();
  return opts;
}

auto isTypeKeyword(clang::tok::TokenKind kind) -> bool {
  switch (kind) {
  case clang::tok::kw_auto:
  case clang::tok::kw_bool:
  case clang::tok::kw_char:
  case clang::tok::kw_char8_t:
  case clang::tok::kw_char16_t:
  case clang::tok::kw_char32_t:
  case clang::tok::kw_double:
  case clang::tok::kw_float:
  case clang::tok::kw_int:
  case clang::tok::kw_long:
  case clang::tok::kw_short:
  case clang::tok::kw_signed:
  case clang::tok::kw_unsigned:
  case clang::tok::kw_void:
  case clang::tok::kw_wchar_t:
    return true;
  default:
    return false;
  }
}

// Calls `visit(token, beginOffset)` for every token in `code`.
void lex(const std::string &code,
         const std::function<void(const clang::Token &, size_t)> &visit) {
  const llvm::StringRef buffer(code);
  clang::Lexer lexer(clang::SourceLocation(), cppOptions(), buffer.begin(),
                     buffer.begin(), buffer.end());
  lexer.SetCommentRetentionState(true);
  clang::Token token{};
  while (true) {
    lexer.LexFromRawLexer(token);
    if (token.is(clang::tok::eof)) {
      break;
    }
    const size_t end = lexer.getCurrentBufferOffset();
    visit(token, end - token.getLength());
  }
}

auto styleOf(const clang::Token &token, clang::IdentifierTable &keywords,
             std::string_view text) -> Style {
  if (token.is(clang::tok::raw_identifier)) {
    const auto kind = keywords.get(token.getRawIdentifier()).getTokenID();
    if (kind == clang::tok::identifier) {
      return Style::Plain;
    }
    if (kind == clang::tok::kw_true || kind == clang::tok::kw_false ||
        kind == clang::tok::kw_nullptr) {
      return Style::Number;
    }
    return isTypeKeyword(kind) ? Style::Type : Style::Keyword;
  }
  if (token.is(clang::tok::numeric_constant)) {
    return Style::Number;
  }
  if (token.is(clang::tok::comment)) {
    return Style::Comment;
  }
  // Unterminated literals come back as `unknown`; they are still strings.
  if (clang::tok::isStringLiteral(token.getKind()) ||
      token.is(clang::tok::char_constant) ||
      token.is(clang::tok::utf8_char_constant) ||
      token.is(clang::tok::wide_char_constant) ||
      (token.is(clang::tok::unknown) &&
       (text.front() == '"' || text.front() == '\''))) {
    return Style::String;
  }
  return Style::Plain;
}

auto ansi(Style style) -> const char * {
  switch (style) {
  case Style::Keyword:
    return "\033[1;34m";
  case Style::Type:
    return "\033[36m";
  case Style::Number:
    return "\033[35m";
  case Style::String:
    return "\033[33m";
  case Style::Comment:
    return "\033[90m";
  case Style::Preprocessor:
    return "\033[32m";
  case Style::Plain:
    break;
  }
  return "";
}

} // namespace

auto inspector::highlight(std::string_view code) -> std::vector<Span> {
  const std::string text(code);
  clang::IdentifierTable keywords(cppOptions());
  std::vector<Span> spans;
  bool directive = false; // inside a `#...` line
  lex(text, [&](const clang::Token &token, size_t begin) -> void {
    const size_t end = begin + token.getLength();
    if (token.isAtStartOfLine()) {
      directive = token.is(clang::tok::hash);
    }
    Style style =
        directive ? Style::Preprocessor
                  : styleOf(token, keywords,
                            std::string_view(text).substr(begin, end - begin));
    if (directive && token.is(clang::tok::comment)) {
      style = Style::Comment;
    }
    if (style != Style::Plain) {
      spans.push_back({begin, end, style});
    }
  });
  return spans;
}

auto inspector::colorize(std::string_view code) -> std::string {
  std::string out;
  size_t pos = 0;
  for (const Span &span : highlight(code)) {
    out.append(code.substr(pos, span.begin - pos));
    out += ansi(span.style);
    out.append(code.substr(span.begin, span.end - span.begin));
    out += "\033[0m";
    pos = span.end;
  }
  out.append(code.substr(pos));
  return out;
}

auto inspector::openBrackets(std::string_view code) -> int {
  int depth = 0;
  lex(std::string(code),
      [&](const clang::Token &token, size_t /*begin*/) -> void {
        if (token.isOneOf(clang::tok::l_paren, clang::tok::l_brace,
                          clang::tok::l_square)) {
          ++depth;
        } else if (token.isOneOf(clang::tok::r_paren, clang::tok::r_brace,
                                 clang::tok::r_square)) {
          --depth;
        }
      });
  return depth;
}
