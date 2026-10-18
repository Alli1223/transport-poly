#pragma once
// Deterministic infinite terrain. Every value is a pure function of (seed, coordinate) computed
// with integer arithmetic only, so all peers generate identical chunks independently.
//
// The land field is a sum of value-noise octaves whose combined per-tile slope is provably
// below one height level; flooring it to integer levels therefore guarantees that adjacent
// corners never differ by more than one level - the OpenTTD terrain rule that keeps every tile
// a buildable slope type - without any neighbour-dependent fix-up across chunk borders.
// Rivers and lakes carve this field with slope-bounded sea-level basins.

#include "core/types.hpp"
#include "sim/coords.hpp"
#include "sim/tile.hpp"

namespace tp {

struct Chunk;

inline constexpr i32 kMaxHeight = 64;
inline constexpr i32 kSnowLine = 34;
inline constexpr i32 kHeightOne = 1 << 16; // fixed point scale of raw heights

class TerrainGenerator {
public:
    explicit TerrainGenerator(u64 seed = 1, bool waterways = true) : seed_(seed), waterways_(waterways) {}

    u64 seed() const { return seed_; }

    // Unclamped height at a corner point in 1/65536 levels; negative below sea level.
    i64 rawHeight(i32 cx, i32 cy) const;
    // Integer corner height in [0, kMaxHeight].
    u8 cornerHeight(i32 cx, i32 cy) const;
    static u8 quantise(i64 raw);

    // Fills heights and tiles (ground, water, trees) of a freshly activated chunk.
    void generateChunk(ChunkPos pos, Chunk& chunk) const;

    // Classifies a tile given its corner raw heights / integer heights (NW, NE, SE, SW).
    Tile generateTile(TilePos t, const i64 raw[4], const u8 h[4]) const;

    // Forest density in [0, 65536) - also used by town placement to avoid dense forests.
    i64 forestDensity(i32 x, i32 y) const;

private:
    u64 seed_;
    bool waterways_;
};

} // namespace tp
