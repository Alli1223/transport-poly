#pragma once
// Tile slope classification derived from the four corner heights.

#include "core/types.hpp"
#include "sim/coords.hpp"

#include <algorithm>
#include <optional>

namespace tp {

// Corner heights of one tile in the order NW, NE, SE, SW.
struct TileCorners {
    u8 h[4] = {0, 0, 0, 0};

    constexpr u8 at(Corner c) const { return h[static_cast<u8>(c)]; }
    constexpr u8 min() const { return std::min(std::min(h[0], h[1]), std::min(h[2], h[3])); }
    constexpr u8 max() const { return std::max(std::max(h[0], h[1]), std::max(h[2], h[3])); }
    friend constexpr bool operator==(const TileCorners&, const TileCorners&) = default;
};

enum SlopeBits : u8 {
    kSlopeFlat = 0,
    kSlopeNW = 1 << 0,
    kSlopeNE = 1 << 1,
    kSlopeSE = 1 << 2,
    kSlopeSW = 1 << 3,
    kSlopeSteep = 1 << 4, // one corner is two levels above the lowest one
};

struct Slope {
    u8 bits = kSlopeFlat; // raised corners relative to `base`
    u8 base = 0;          // height of the lowest corner

    constexpr bool isFlat() const { return bits == kSlopeFlat; }
    constexpr bool isSteep() const { return (bits & kSlopeSteep) != 0; }

    // For a simple ramp (two adjacent corners raised) returns the direction in which the
    // ramp ascends; roads and rails may run straight up such a slope.
    constexpr std::optional<Dir> rampUphill() const {
        switch (bits) {
        case kSlopeNW | kSlopeNE: return Dir::N;
        case kSlopeNE | kSlopeSE: return Dir::E;
        case kSlopeSE | kSlopeSW: return Dir::S;
        case kSlopeSW | kSlopeNW: return Dir::W;
        default: return std::nullopt;
        }
    }
    constexpr bool isRamp() const { return rampUphill().has_value(); }
    // Height of the highest point of the tile.
    constexpr u8 top() const { return static_cast<u8>(base + (isSteep() ? 2 : (bits ? 1 : 0))); }
};

constexpr Slope computeSlope(const TileCorners& c) {
    Slope s;
    s.base = c.min();
    for (int i = 0; i < 4; ++i) {
        const int d = c.h[i] - s.base;
        if (d >= 1) s.bits |= static_cast<u8>(1u << i);
        if (d >= 2) s.bits |= kSlopeSteep;
    }
    return s;
}

} // namespace tp
