#pragma once
// 16.16 signed fixed point number for deterministic fractional maths in the simulation.

#include "core/types.hpp"

#include <compare>

namespace tp {

struct Fixed {
    static constexpr int kFracBits = 16;
    static constexpr i32 kOneRaw = 1 << kFracBits;

    i32 raw = 0;

    static constexpr Fixed fromRaw(i32 r) {
        Fixed f;
        f.raw = r;
        return f;
    }
    static constexpr Fixed fromInt(i32 v) { return fromRaw(v * kOneRaw); }
    // num / den rounded towards zero.
    static constexpr Fixed fromRatio(i64 num, i64 den) {
        return fromRaw(static_cast<i32>((num * kOneRaw) / den));
    }
    static constexpr Fixed one() { return fromRaw(kOneRaw); }
    static constexpr Fixed half() { return fromRaw(kOneRaw / 2); }

    // Rounds towards negative infinity.
    constexpr i32 floor() const { return raw >> kFracBits; }
    constexpr i32 ceil() const { return (raw + kOneRaw - 1) >> kFracBits; }
    constexpr i32 round() const { return (raw + kOneRaw / 2) >> kFracBits; }
    constexpr i32 frac() const { return raw & (kOneRaw - 1); }

    // For rendering / UI only - never feed the result back into the simulation.
    constexpr double toDouble() const { return static_cast<double>(raw) / kOneRaw; }
    constexpr float toFloat() const { return static_cast<float>(raw) / kOneRaw; }

    constexpr Fixed operator-() const { return fromRaw(-raw); }
    constexpr Fixed& operator+=(Fixed o) {
        raw += o.raw;
        return *this;
    }
    constexpr Fixed& operator-=(Fixed o) {
        raw -= o.raw;
        return *this;
    }
    constexpr Fixed& operator*=(Fixed o) {
        raw = static_cast<i32>((static_cast<i64>(raw) * o.raw) >> kFracBits);
        return *this;
    }
    constexpr Fixed& operator/=(Fixed o) {
        raw = static_cast<i32>((static_cast<i64>(raw) * kOneRaw) / o.raw);
        return *this;
    }
    constexpr Fixed& operator*=(i32 v) {
        raw *= v;
        return *this;
    }
    constexpr Fixed& operator/=(i32 v) {
        raw /= v;
        return *this;
    }

    friend constexpr Fixed operator+(Fixed a, Fixed b) { return a += b; }
    friend constexpr Fixed operator-(Fixed a, Fixed b) { return a -= b; }
    friend constexpr Fixed operator*(Fixed a, Fixed b) { return a *= b; }
    friend constexpr Fixed operator/(Fixed a, Fixed b) { return a /= b; }
    friend constexpr Fixed operator*(Fixed a, i32 b) { return a *= b; }
    friend constexpr Fixed operator*(i32 a, Fixed b) { return b *= a; }
    friend constexpr Fixed operator/(Fixed a, i32 b) { return a /= b; }

    friend constexpr auto operator<=>(Fixed, Fixed) = default;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(raw);
    }
};

} // namespace tp
