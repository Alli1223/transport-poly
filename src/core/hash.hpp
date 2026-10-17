#pragma once
// Integer hashing: coordinate hashes for procedural generation and an incremental
// hasher for game state checksums. All functions are bit-exact on every platform.

#include "core/types.hpp"

#include <string_view>
#include <type_traits>

namespace tp {

// "lowbias32" integer finaliser by Chris Wellons - good avalanche, cheap.
constexpr u32 mix32(u32 x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// SplitMix64 finaliser.
constexpr u64 mix64(u64 x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

// Hash of a 2D integer coordinate under a seed and a salt (selects independent streams).
constexpr u32 hashCoords(u64 seed, i32 x, i32 y, u32 salt = 0) {
    u64 h = seed ^ (static_cast<u64>(salt) * 0x9E3779B97F4A7C15ULL);
    h = mix64(h ^ static_cast<u32>(x));
    h = mix64(h ^ (static_cast<u64>(static_cast<u32>(y)) << 32));
    return static_cast<u32>(h ^ (h >> 32));
}

constexpr u64 hashCombine(u64 a, u64 b) { return mix64(a ^ (b + 0x9E3779B97F4A7C15ULL + (a << 6) + (a >> 2))); }

constexpr u64 hashString(std::string_view s) {
    u64 h = 0xcbf29ce484222325ULL;
    for (char c : s) {
        h ^= static_cast<u8>(c);
        h *= 0x100000001b3ULL;
    }
    return h;
}

// Incremental 64-bit hasher for world state checksums (desync detection).
class StateHasher {
public:
    template <class T>
        requires std::is_integral_v<T> || std::is_enum_v<T>
    constexpr void add(T value) {
        u64 v;
        if constexpr (std::is_enum_v<T>) {
            v = static_cast<u64>(static_cast<std::underlying_type_t<T>>(value));
        } else {
            v = static_cast<u64>(value);
        }
        state_ = mix64(state_ ^ (v + 0x9E3779B97F4A7C15ULL));
        ++count_;
    }

    template <class... Ts>
    constexpr void addAll(Ts... values) {
        (add(values), ...);
    }

    constexpr u64 value() const { return mix64(state_ ^ count_); }

private:
    u64 state_ = 0x243F6A8885A308D3ULL;
    u64 count_ = 0;
};

} // namespace tp
