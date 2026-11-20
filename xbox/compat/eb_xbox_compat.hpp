// Xbox (nxdk) compatibility layer for the Phase Distorter sources.
//
// nxdk compiles C++ with -fno-exceptions against LLVM 10's libc++. The port's
// sources use `throw` for invariant failures and `try`/`catch` mostly for
// cleanup-and-rethrow. On Xbox every `throw` becomes a fatal error that is
// shown on screen, and `catch` bodies become dead code. This header is
// force-included (-include) into every translation unit of the Xbox build.
//
// Rules this relies on (enforced by xbox/tools/prepare_sources.py, which
// rewrites a COPY of the sources and never touches upstream files):
//   * `catch (...)`          -> `catch (const ::eb::xbox::CatchAll &eb_catch_N)`
//   * unnamed `catch (T &)`  -> named, so the dead handler still type-checks
//   * `throw;`               -> `::eb::xbox::rethrow();`
#pragma once

// ---------------------------------------------------------------------------
// 1. Freeze the std headers the port uses BEFORE defining keyword macros, so
//    libc++ never sees `throw`/`try`/`catch` redefined.
// ---------------------------------------------------------------------------
#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <exception>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <initializer_list>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <new>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

// snes_spc (external/spc_dsp) declares `operator new(...) throw(())`, which the
// keyword macros below would break. Defining this (empty) selects its
// `new (std::nothrow)` path instead.
#define BLARGG_DISABLE_NOTHROW

#include "eb_xbox_win32.hpp"

// ---------------------------------------------------------------------------
// 2. C++20 library pieces missing from LLVM 10 libc++.
// ---------------------------------------------------------------------------
namespace std {

#if !defined(__cpp_lib_bit_cast)
template <class To, class From>
[[nodiscard]] constexpr enable_if_t<sizeof(To) == sizeof(From) && is_trivially_copyable_v<To> &&
                                        is_trivially_copyable_v<From>,
                                    To>
bit_cast(const From &from) noexcept {
    return __builtin_bit_cast(To, from);
}
#endif

} // namespace std

namespace eb::xbox::detail {
// Three-way comparison of two values, falling back to operator< when `<=>` is not
// available, so defaulted operator<=> on structs holding std containers type-checks.
template <class T>
constexpr auto synth_three_way(const T &a, const T &b) {
    if constexpr (requires { a <=> b; })
        return a <=> b;
    else
        return a < b ? std::weak_ordering::less
                     : (b < a ? std::weak_ordering::greater : std::weak_ordering::equivalent);
}
template <class T>
using synth_result = decltype(synth_three_way(std::declval<const T &>(), std::declval<const T &>()));
} // namespace eb::xbox::detail

// libc++ 10 has no operator<=> for any standard type. Provide the ones the sources use.
namespace std {

template <class T, class A>
constexpr eb::xbox::detail::synth_result<T> operator<=>(const vector<T, A> &x, const vector<T, A> &y) {
    const size_t n = x.size() < y.size() ? x.size() : y.size();
    for (size_t i = 0; i < n; ++i)
        if (auto c = eb::xbox::detail::synth_three_way(x[i], y[i]); c != 0) return c;
    return x.size() <=> y.size();
}
template <class T, size_t N>
constexpr eb::xbox::detail::synth_result<T> operator<=>(const array<T, N> &x, const array<T, N> &y) {
    for (size_t i = 0; i < N; ++i)
        if (auto c = eb::xbox::detail::synth_three_way(x[i], y[i]); c != 0) return c;
    return eb::xbox::detail::synth_result<T>::equivalent;
}
template <class A, class B>
constexpr common_comparison_category_t<eb::xbox::detail::synth_result<A>, eb::xbox::detail::synth_result<B>>
operator<=>(const pair<A, B> &x, const pair<A, B> &y) {
    if (auto c = eb::xbox::detail::synth_three_way(x.first, y.first); c != 0) return c;
    return eb::xbox::detail::synth_three_way(x.second, y.second);
}
template <class T>
constexpr eb::xbox::detail::synth_result<T> operator<=>(const optional<T> &x, const optional<T> &y) {
    if (x && y) return eb::xbox::detail::synth_three_way(*x, *y);
    return static_cast<bool>(x) <=> static_cast<bool>(y);
}
template <class C, class Tr, class A>
constexpr strong_ordering operator<=>(const basic_string<C, Tr, A> &x, const basic_string<C, Tr, A> &y) {
    return x.compare(y) <=> 0;
}
template <class C, class Tr>
constexpr strong_ordering operator<=>(basic_string_view<C, Tr> x, basic_string_view<C, Tr> y) {
    return x.compare(y) <=> 0;
}

} // namespace std



// ---------------------------------------------------------------------------
// 3. Exception shim.
// ---------------------------------------------------------------------------
namespace eb::xbox {

// Implemented in xbox/src/xbox_fatal.cpp. Prints the message on the TV, writes
// it to the debug log and halts. Never returns.
[[noreturn]] void fatal(const char *what, const char *file, int line);

// Used by rewritten `throw;` statements (they only occur in dead catch bodies).
[[noreturn]] inline void rethrow() { fatal("rethrow reached", __FILE__, __LINE__); }

struct CatchAll {};

// Result type of a `throw` expression: convertible to whatever the other operand of `?:` is.
struct Never {
    template <class T>
    operator T() const noexcept {
        __builtin_unreachable();
    }
};

// `throw X;`  ->  `ThrowSink{__FILE__, __LINE__} = X;`
struct ThrowSink {
    const char *file;
    int line;
    template <class E>
    [[noreturn]] Never operator=(const E &e) const {
        if constexpr (std::is_base_of_v<std::exception, E>)
            fatal(e.what(), file, line);
        else
            fatal("non-standard exception thrown", file, line);
    }
};

// Lets a dead catch declaration (`T &name = AnyValue{}`) type-check for any T.
struct AnyValue {
    template <class T>
    operator T &() const {
        __builtin_unreachable();
    }
};

} // namespace eb::xbox

// nxdk's libc++ is built with _LIBCPP_HAS_NO_STDOUT, so std::cout does not exist.
// The prepared sources use these instead (prepare_sources.py rewrites std::cout and
// std::cerr); output goes to the debug log / on-screen console. Defined in
// xbox/src/xbox_log.cpp.
namespace eb::xbox {
extern std::ostream cout;
extern std::ostream cerr;
} // namespace eb::xbox

#define throw ::eb::xbox::ThrowSink{__FILE__, __LINE__} =
#define try if (true)
// `else if (false) for (decl = AnyValue{}; false;) {...}` chains correctly for
// multiple handlers and keeps the handler body compiled but never executed.
#define catch(...) else if (false) for (__VA_ARGS__ = ::eb::xbox::AnyValue{}; false;)
