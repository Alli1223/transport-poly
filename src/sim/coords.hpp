#pragma once
// Tile, corner and chunk coordinates plus compass directions.
//
// Tile x grows towards the east, tile y towards the south. Corner (cx, cy) is the point at the
// north-west corner of tile (cx, cy); tile (x, y) therefore has the corners (x, y) NW,
// (x+1, y) NE, (x+1, y+1) SE and (x, y+1) SW.

#include "core/types.hpp"

#include <compare>
#include <format>

namespace tp {

inline constexpr i32 kChunkShift = 5;
inline constexpr i32 kChunkSize = 1 << kChunkShift; // tiles per chunk edge
inline constexpr i32 kChunkMask = kChunkSize - 1;
inline constexpr i32 kChunkTiles = kChunkSize * kChunkSize;
inline constexpr i32 kChunkCorners = kChunkSize + 1; // corner points per chunk edge

struct TilePos {
    i32 x = 0;
    i32 y = 0;

    constexpr TilePos operator+(TilePos o) const { return {x + o.x, y + o.y}; }
    constexpr TilePos operator-(TilePos o) const { return {x - o.x, y - o.y}; }
    constexpr TilePos& operator+=(TilePos o) {
        x += o.x;
        y += o.y;
        return *this;
    }
    friend constexpr auto operator<=>(const TilePos&, const TilePos&) = default;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(x, y);
    }
};

struct ChunkPos {
    i32 x = 0;
    i32 y = 0;

    constexpr u64 key() const { return (static_cast<u64>(static_cast<u32>(x)) << 32) | static_cast<u32>(y); }
    static constexpr ChunkPos fromKey(u64 k) {
        return {static_cast<i32>(static_cast<u32>(k >> 32)), static_cast<i32>(static_cast<u32>(k))};
    }
    friend constexpr auto operator<=>(const ChunkPos&, const ChunkPos&) = default;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(x, y);
    }
};

constexpr ChunkPos chunkOf(TilePos t) { return {t.x >> kChunkShift, t.y >> kChunkShift}; }
constexpr TilePos chunkOrigin(ChunkPos c) { return {c.x * kChunkSize, c.y * kChunkSize}; }
constexpr i32 localCoord(i32 v) { return v & kChunkMask; }

enum class Dir : u8 { N, E, S, W };

inline constexpr Dir kDirs[4] = {Dir::N, Dir::E, Dir::S, Dir::W};

constexpr TilePos dirOffset(Dir d) {
    switch (d) {
    case Dir::N: return {0, -1};
    case Dir::E: return {1, 0};
    case Dir::S: return {0, 1};
    case Dir::W: return {-1, 0};
    }
    return {0, 0};
}
constexpr Dir opposite(Dir d) { return static_cast<Dir>((static_cast<u8>(d) + 2) & 3); }
constexpr Dir rotateCW(Dir d) { return static_cast<Dir>((static_cast<u8>(d) + 1) & 3); }
constexpr Dir rotateCCW(Dir d) { return static_cast<Dir>((static_cast<u8>(d) + 3) & 3); }
constexpr u8 dirBit(Dir d) { return static_cast<u8>(1u << static_cast<u8>(d)); }
constexpr bool isAxisX(Dir d) { return d == Dir::E || d == Dir::W; }
constexpr TilePos neighbour(TilePos t, Dir d) { return t + dirOffset(d); }

enum class Corner : u8 { NW, NE, SE, SW };

inline constexpr Corner kCorners[4] = {Corner::NW, Corner::NE, Corner::SE, Corner::SW};

// Offset of a tile corner point relative to the tile's NW corner point.
constexpr TilePos cornerOffset(Corner c) {
    switch (c) {
    case Corner::NW: return {0, 0};
    case Corner::NE: return {1, 0};
    case Corner::SE: return {1, 1};
    case Corner::SW: return {0, 1};
    }
    return {0, 0};
}

} // namespace tp

template <>
struct std::formatter<tp::TilePos> : std::formatter<std::string_view> {
    auto format(tp::TilePos p, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "({}, {})", p.x, p.y);
    }
};

template <>
struct std::formatter<tp::ChunkPos> : std::formatter<std::string_view> {
    auto format(tp::ChunkPos p, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "[{}, {}]", p.x, p.y);
    }
};
