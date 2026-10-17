#pragma once
// Deterministic pseudo random number generation. Never use <random> distributions in the
// simulation: their output differs between standard library implementations.

#include "core/types.hpp"

namespace tp {

class SplitMix64 {
public:
    constexpr explicit SplitMix64(u64 seed) : state_(seed) {}
    constexpr u64 next() {
        u64 z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

private:
    u64 state_;
};

// PCG32 (XSH-RR variant) by Melissa O'Neill.
class Rng {
public:
    constexpr Rng() { seed(0); }
    constexpr explicit Rng(u64 seedValue, u64 stream = 0) { seed(seedValue, stream); }

    constexpr void seed(u64 seedValue, u64 stream = 0) {
        SplitMix64 sm(seedValue ^ (stream * 0xD1B54A32D192ED03ULL));
        state_ = 0;
        inc_ = (sm.next() << 1u) | 1u;
        next();
        state_ += sm.next();
        next();
    }

    constexpr u32 next() {
        const u64 old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        const u32 xorshifted = static_cast<u32>(((old >> 18u) ^ old) >> 27u);
        const u32 rot = static_cast<u32>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
    }

    constexpr u64 next64() { return (static_cast<u64>(next()) << 32) | next(); }

    // Uniform integer in [0, bound). bound must be > 0. (Lemire's unbiased method.)
    constexpr u32 below(u32 bound) {
        u64 m = static_cast<u64>(next()) * bound;
        u32 low = static_cast<u32>(m);
        if (low < bound) {
            const u32 threshold = (0u - bound) % bound;
            while (low < threshold) {
                m = static_cast<u64>(next()) * bound;
                low = static_cast<u32>(m);
            }
        }
        return static_cast<u32>(m >> 32);
    }

    // Uniform integer in [lo, hi] (inclusive).
    constexpr i32 range(i32 lo, i32 hi) {
        const u32 span = static_cast<u32>(static_cast<i64>(hi) - lo + 1);
        if (span == 0) return static_cast<i32>(next()); // full 32-bit range
        return static_cast<i32>(static_cast<i64>(lo) + below(span));
    }

    // True with probability numerator / denominator.
    constexpr bool chance(u32 numerator, u32 denominator) { return below(denominator) < numerator; }

    template <class Ar>
    void serialize(Ar& ar) {
        ar(state_, inc_);
    }

    constexpr u64 stateForHash() const { return state_ ^ inc_; }

    friend constexpr bool operator==(const Rng&, const Rng&) = default;

private:
    u64 state_ = 0;
    u64 inc_ = 1;
};

} // namespace tp
