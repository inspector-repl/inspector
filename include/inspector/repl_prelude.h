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

namespace __inspector {

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

inline void quote(std::ostream &os, std::string_view s) {
  os << '"';
  for (char c : s) {
    switch (c) {
    case '"':
      os << "\\\"";
      break;
    case '\\':
      os << "\\\\";
      break;
    case '\n':
      os << "\\n";
      break;
    case '\t':
      os << "\\t";
      break;
    default:
      os << c;
    }
  }
  os << '"';
}

// Long containers get truncated so a stray `bigVector` doesn't flood the REPL.
constexpr std::size_t max_elements = 100;

template <typename T> void show(std::ostream &os, const T &v) {
  using U = std::decay_t<T>;
  if constexpr (std::is_same_v<U, std::string> ||
                std::is_same_v<U, std::string_view>) {
    quote(os, v);
  } else if constexpr (std::is_same_v<U, const char *> ||
                       std::is_same_v<U, char *>) {
    if (v)
      quote(os, v);
    else
      os << "nullptr";
  } else if constexpr (is_pair<U>::value) {
    os << '{';
    show(os, v.first);
    os << ", ";
    show(os, v.second);
    os << '}';
  } else if constexpr (is_iterable<U>::value) {
    os << '{';
    std::size_t n = 0;
    for (const auto &e : v) {
      if (n)
        os << ", ";
      if (n == max_elements) {
        os << "...";
        break;
      }
      show(os, e);
      ++n;
    }
    os << '}';
  } else if constexpr (is_streamable<U>::value) {
    os << v;
  } else {
    os << "@" << static_cast<const void *>(&v);
  }
}

template <typename T> void print(std::string &out, const T &v) {
  std::ostringstream os;
  show(os, v);
  out = os.str();
}

} // namespace __inspector
