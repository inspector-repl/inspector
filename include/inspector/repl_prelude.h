// Loaded into the REPL interpreter (never into the inspected program) to
// render class-type results, which clang-repl only prints as an address.
#pragma once

#include <cstddef>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace inspector_repl {

// Lets generated declarations name array and function pointer types.
template <typename T> using type = T;

template <typename T, typename = void>
struct is_streamable : std::false_type {};
template <typename T>
struct is_streamable<T, std::void_t<decltype(std::declval<std::ostream &>()
                                             << std::declval<const T &>())>>
    : std::true_type {};

template <typename T, typename = void> struct is_iterable : std::false_type {};
template <typename T>
struct is_iterable<T,
                   std::void_t<decltype(std::begin(std::declval<const T &>())),
                               decltype(std::end(std::declval<const T &>()))>>
    : std::true_type {};

template <typename T, typename = void> struct is_pair : std::false_type {};
template <typename T>
struct is_pair<T, std::void_t<decltype(std::declval<T>().first),
                              decltype(std::declval<T>().second)>>
    : std::true_type {};

inline void quote(std::ostream &out, std::string_view text) {
  out << '"';
  for (const char chr : text) {
    switch (chr) {
    case '"':
      out << "\\\"";
      break;
    case '\\':
      out << "\\\\";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      out << chr;
    }
  }
  out << '"';
}

// Long containers get truncated so a stray `bigVector` doesn't flood the REPL.
constexpr std::size_t max_elements = 100;

template <typename T> void show(std::ostream &out, const T &value) {
  using U = std::decay_t<T>;
  if constexpr (std::is_same_v<U, std::string> ||
                std::is_same_v<U, std::string_view>) {
    quote(out, value);
  } else if constexpr (std::is_same_v<U, const char *> ||
                       std::is_same_v<U, char *>) {
    if (value != nullptr) {
      quote(out, value);
    } else {
      out << "nullptr";
    }
  } else if constexpr (is_pair<U>::value) {
    out << '{';
    show(out, value.first);
    out << ", ";
    show(out, value.second);
    out << '}';
  } else if constexpr (is_iterable<U>::value) {
    out << '{';
    std::size_t count = 0;
    for (const auto &element : value) {
      if (count != 0) {
        out << ", ";
      }
      if (count == max_elements) {
        out << "...";
        break;
      }
      show(out, element);
      ++count;
    }
    out << '}';
  } else if constexpr (is_streamable<U>::value) {
    out << value;
  } else {
    out << "@" << static_cast<const void *>(&value);
  }
}

template <typename T> void print(std::string &result, const T &value) {
  std::ostringstream out;
  show(out, value);
  result = out.str();
}

} // namespace inspector_repl
