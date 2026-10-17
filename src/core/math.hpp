#pragma once
// Integer maths helpers. Everything here is exact and platform independent, so it is
// safe to use inside the deterministic simulation.

#include "core/types.hpp"

#include <concepts>

namespace tp {

// Division rounding towards negative infinity (C++ '/' truncates towards zero).
template <std::signed_integral T>
constexpr T floorDiv(T a, T b) {
    const T q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

// Modulo with the sign of the divisor, i.e. always in [0, b) for positive b.
template <std::signed_integral T>
constexpr T floorMod(T a, T b) {
    const T r = a % b;
    return (r != 0 && ((r < 0) != (b < 0))) ? r + b : r;
}

template <class T>
constexpr T iabs(T v) {
    return v < 0 ? -v : v;
}

template <class T>
constexpr T isign(T v) {
    return static_cast<T>((v > 0) - (v < 0));
}

template <class T>
constexpr T iclamp(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Floor of the square root.
constexpr u64 isqrt(u64 n) {
    if (n < 2) return n;
    u64 x = n;
    u64 y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

// Linear interpolation a + (b - a) * t / tMax using 64-bit intermediates.
constexpr i64 ilerp(i64 a, i64 b, i64 t, i64 tMax) { return a + (b - a) * t / tMax; }

// Integer Euclidean distance (rounded down).
constexpr i64 idistance(i64 dx, i64 dy) { return static_cast<i64>(isqrt(static_cast<u64>(dx * dx + dy * dy))); }

constexpr i64 manhattan(i64 dx, i64 dy) { return iabs(dx) + iabs(dy); }

// Chebyshev (king move) distance.
constexpr i64 chebyshev(i64 dx, i64 dy) {
    const i64 ax = iabs(dx);
    const i64 ay = iabs(dy);
    return ax > ay ? ax : ay;
}

} // namespace tp
