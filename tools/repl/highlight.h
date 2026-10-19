#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace inspector {

enum class Style : std::uint8_t {
  Plain,
  Keyword,
  Type,
  Number,
  String,
  Comment,
  Preprocessor,
};

struct Span {
  std::size_t begin; // byte offsets into the input
  std::size_t end;
  Style style;
};

// Tokenizes with clang's raw lexer, so incomplete input (open strings,
// unbalanced brackets) still highlights.
auto highlight(std::string_view code) -> std::vector<Span>;

// ANSI-colored copy of `code` for printing source context.
auto colorize(std::string_view code) -> std::string;

// Brackets left open in `code`, ignoring those in strings and comments.
auto openBrackets(std::string_view code) -> int;

} // namespace inspector
