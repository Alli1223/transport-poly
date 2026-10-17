#include "sim/terrain_gen.hpp"

#include "core/hash.hpp"
#include "sim/chunk.hpp"

#include <algorithm>

namespace tp {

namespace {

constexpr i64 kOne = kHeightOne;

// Smoothstep on [0, kOne]; maximum derivative 1.5.
constexpr i64 fade(i64 t) { return ((t * t) >> 16) * (3 * kOne - 2 * t) >> 16; }

// Value noise in [0, kOne) with lattice spacing 2^shift tiles. Lipschitz <= 1.5 / 2^shift per tile.
i64 valueNoise(u64 seed, u32 salt, i32 x, i32 y, int shift) {
    const i32 ix = x >> shift;
    const i32 iy = y >> shift;
    const i32 mask = (1 << shift) - 1;
    const i64 fx = static_cast<i64>(x & mask) << (16 - shift);
    const i64 fy = static_cast<i64>(y & mask) << (16 - shift);
    const auto lattice = [&](i32 a, i32 b) -> i64 { return static_cast<i64>(hashCoords(seed, a, b, salt) >> 16); };
    const i64 v00 = lattice(ix, iy);
    const i64 v10 = lattice(ix + 1, iy);
    const i64 v01 = lattice(ix, iy + 1);
    const i64 v11 = lattice(ix + 1, iy + 1);
    const i64 sx = fade(fx);
    const i64 sy = fade(fy);
    const i64 a = v00 + (((v10 - v00) * sx) >> 16);
    const i64 b = v01 + (((v11 - v01) * sx) >> 16);
    return a + (((b - a) * sy) >> 16);
}

// Noise salts (independent streams).
enum Salt : u32 {
    kContinentLow = 1,
    kContinentHigh,
    kHillMask,
    kHills,
    kMountainMask,
    kRidges,
    kDetail,
    kForest,
    kForestDetail,
    kGroundJitter,
    kTreeJitter,
};

// Piecewise linear flattening of the continent term with slopes <= 1: wide coastal plains.
constexpr i64 flatten(i64 h) {
    if (h <= 0) return h;
    if (h <= 16 * kOne) return h / 4;
    return 4 * kOne + (h - 16 * kOne);
}

// Slope budget (levels per tile, see terrain_gen.hpp):
//   continent  72*(0.6*1.5/1024 + 0.4*1.5/256)          = 0.232
//   hills       8*1.5/64 + 8*1.5/512                     = 0.211
//   mountains  18*(2*1.5/256 + 4*1.5/1024)               = 0.316
//   detail    1.5*1.5/16 + 1.5*1.5/512                   = 0.145
//   total                                                = 0.904 < 1
// The terrain test verifies the budget empirically over millions of samples.

} // namespace

i64 TerrainGenerator::rawHeight(i32 x, i32 y) const {
    const i64 c1 = valueNoise(seed_, kContinentLow, x, y, 10);
    const i64 c2 = valueNoise(seed_, kContinentHigh, x, y, 8);
    const i64 continent = (72 * (6 * c1 + 4 * c2)) / 10 - 30 * kOne;

    const i64 hillMask = valueNoise(seed_, kHillMask, x, y, 9);
    const i64 hills = (8 * valueNoise(seed_, kHills, x, y, 6) * hillMask) >> 16;

    const i64 mm = std::clamp<i64>((valueNoise(seed_, kMountainMask, x, y, 10) - (kOne * 55) / 100) * 4, 0, kOne);
    i64 mountains = 0;
    if (mm > 0) {
        const i64 n = valueNoise(seed_, kRidges, x, y, 8);
        const i64 ridge = kOne - (2 * n - kOne < 0 ? kOne - 2 * n : 2 * n - kOne);
        mountains = (((18 * ridge) >> 8) * mm) >> 8; // fixed point levels, at most 18
    }

    const i64 detail = (3 * valueNoise(seed_, kDetail, x, y, 4) * hillMask) >> 17;

    return flatten(continent) + hills + mountains + detail;
}

u8 TerrainGenerator::quantise(i64 raw) {
    if (raw <= 0) return 0;
    return static_cast<u8>(std::min<i64>(raw >> 16, kMaxHeight));
}

u8 TerrainGenerator::cornerHeight(i32 cx, i32 cy) const { return quantise(rawHeight(cx, cy)); }

i64 TerrainGenerator::forestDensity(i32 x, i32 y) const {
    return (7 * valueNoise(seed_, kForest, x, y, 7) + 3 * valueNoise(seed_, kForestDetail, x, y, 5)) / 10;
}

Tile TerrainGenerator::generateTile(TilePos t, const i64 raw[4], const u8 h[4]) const {
    Tile tile;
    const bool allWet = raw[0] < 0 && raw[1] < 0 && raw[2] < 0 && raw[3] < 0;
    if (allWet) {
        tile.type = TileType::Water;
        tile.ground = Ground::Water;
        return tile;
    }
    const u8 lo = std::min(std::min(h[0], h[1]), std::min(h[2], h[3]));
    const u8 hi = std::max(std::max(h[0], h[1]), std::max(h[2], h[3]));
    const bool nearSea = raw[0] < kOne / 2 || raw[1] < kOne / 2 || raw[2] < kOne / 2 || raw[3] < kOne / 2;
    const u32 jitter = hashCoords(seed_, t.x, t.y, kGroundJitter);

    if (lo >= kSnowLine) {
        tile.ground = Ground::Snow;
    } else if (hi - lo >= 2) {
        tile.ground = Ground::Rock;
    } else if (lo >= kSnowLine - 5 && jitter % 3 == 0) {
        tile.ground = Ground::Rock;
    } else if (nearSea) {
        tile.ground = Ground::Sand;
    } else if (jitter % 23 == 0) {
        tile.ground = Ground::Rough;
    } else {
        tile.ground = Ground::Grass;
    }

    if (tile.ground == Ground::Grass || tile.ground == Ground::Rough ||
        (tile.ground == Ground::Snow && lo < kSnowLine + 3)) {
        const i64 forest = forestDensity(t.x, t.y);
        const u32 treeHash = hashCoords(seed_, t.x, t.y, kTreeJitter);
        constexpr i64 kForestStart = (kOne * 56) / 100;
        if (forest > kForestStart) {
            const i64 dens = (forest - kForestStart) * 12 / kOne; // 0..5
            const i64 count = std::clamp<i64>(dens + static_cast<i64>(treeHash % 3) - 1, 0, 4);
            tile.trees = static_cast<u8>(count);
        } else if (treeHash % 47 == 0) {
            tile.trees = 1;
        }
        if (tile.trees > 0) {
            const bool conifer = lo >= 14 || (lo >= 9 && (treeHash >> 8) % 3 == 0);
            tile.treeType = conifer ? TreeType::Conifer : TreeType::Deciduous;
        }
    }
    return tile;
}

void TerrainGenerator::generateChunk(ChunkPos pos, Chunk& chunk) const {
    chunk.pos = pos;
    const TilePos origin = chunkOrigin(pos);
    std::array<i64, kChunkCorners * kChunkCorners> raw{};
    for (i32 ly = 0; ly < kChunkCorners; ++ly) {
        for (i32 lx = 0; lx < kChunkCorners; ++lx) {
            const i64 r = rawHeight(origin.x + lx, origin.y + ly);
            raw[static_cast<usize>(ly * kChunkCorners + lx)] = r;
            chunk.height(lx, ly) = quantise(r);
        }
    }
    const auto rawAt = [&](i32 lx, i32 ly) { return raw[static_cast<usize>(ly * kChunkCorners + lx)]; };
    for (i32 ly = 0; ly < kChunkSize; ++ly) {
        for (i32 lx = 0; lx < kChunkSize; ++lx) {
            const i64 r[4] = {rawAt(lx, ly), rawAt(lx + 1, ly), rawAt(lx + 1, ly + 1), rawAt(lx, ly + 1)};
            const u8 h[4] = {chunk.height(lx, ly), chunk.height(lx + 1, ly), chunk.height(lx + 1, ly + 1),
                             chunk.height(lx, ly + 1)};
            chunk.tile(lx, ly) = generateTile({origin.x + lx, origin.y + ly}, r, h);
        }
    }
}

} // namespace tp
