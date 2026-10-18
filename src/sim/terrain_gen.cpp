#include "sim/terrain_gen.hpp"

#include "core/hash.hpp"
#include "sim/chunk.hpp"

#include <algorithm>
#include <cstdlib>

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
    kRiverBend,
    kLakeSite,
};

// Piecewise linear flattening of the continent term with slopes <= 1: coastal plains.
constexpr i64 flatten(i64 h) {
    if (h <= 0) return h;
    if (h <= 8 * kOne) return h / 2;
    if (h <= 40 * kOne) return 4 * kOne + ((h - 8 * kOne) * 2) / 5;
    return 4 * kOne + (32 * kOne * 2) / 5 + (h - 40 * kOne);
}

// Slope budget (levels per tile, value noise with smoothstep has max derivative 1.5/cell):
//   continent  72*(0.6*1.5/1024 + 0.4*1.5/256)          = 0.232
//   hills      12*1.5/128 + 12*1.5/512                   = 0.176
//   mountains  24*(2*1.5/256 + 4*1.5/1024)               = 0.422
//   detail      1*1.5/16  +  1*1.5/512                   = 0.097
//   total                                                = 0.927 < 1
// The terrain test verifies the budget empirically over millions of samples.

} // namespace

i64 TerrainGenerator::rawHeight(i32 x, i32 y) const {
    const i64 c1 = valueNoise(seed_, kContinentLow, x, y, 10);
    const i64 c2 = valueNoise(seed_, kContinentHigh, x, y, 8);
    const i64 continent = (72 * (6 * c1 + 4 * c2)) / 10 - 28 * kOne;

    const i64 hillMask = valueNoise(seed_, kHillMask, x, y, 9);
    const i64 hills = (12 * valueNoise(seed_, kHills, x, y, 7) * hillMask) >> 16;

    const i64 mm = std::clamp<i64>((valueNoise(seed_, kMountainMask, x, y, 10) - (kOne * 50) / 100) * 4, 0, kOne);
    i64 mountains = 0;
    if (mm > 0) {
        const i64 n = valueNoise(seed_, kRidges, x, y, 8);
        const i64 ridge = kOne - (2 * n - kOne < 0 ? kOne - 2 * n : 2 * n - kOne);
        mountains = (((24 * ridge) >> 8) * mm) >> 8; // fixed point levels, at most 24
    }

    const i64 detail = (valueNoise(seed_, kDetail, x, y, 4) * hillMask) >> 16;

    const i64 terrain = flatten(continent) + hills + mountains + detail;
    if (!waterways_) return terrain; // preserve exploration boundaries in pre-waterway saves

    // Sea-level waterways are carved into the height field itself. Taking the minimum
    // of slope-bounded fields preserves the adjacent-corner rule, including at banks.
    // Rivers run across the world, with smooth bends independent of chunk activation.
    const i64 bend = 128 * valueNoise(seed_, kRiverBend, x, 0, 9);
    const i64 phase = static_cast<i64>(hashCoords(seed_, 0, 0, kRiverBend) % 1024) * kOne;
    constexpr i64 period = 1024 * kOne;
    i64 across = (static_cast<i64>(y) * kOne - bend - phase) % period;
    if (across < 0) across += period;
    const i64 river = std::min(across, period - across) / 2 - 3 * kOne;
    i64 waterBed = river;

    // One optional lake per 512-tile cell. Inspect neighbouring cells as well so
    // basins are identical on either side of cell and chunk boundaries.
    const i32 cellX = x >> 9, cellY = y >> 9;
    for (i32 dy = -1; dy <= 1; ++dy) {
        for (i32 dx = -1; dx <= 1; ++dx) {
            const u32 site = hashCoords(seed_, cellX + dx, cellY + dy, kLakeSite);
            if (site % 3 != 0) continue;
            const i64 cx = static_cast<i64>(cellX + dx) * 512 + 64 + ((site >> 2) % 384);
            const i64 cy = static_cast<i64>(cellY + dy) * 512 + 64 + ((site >> 11) % 384);
            const i64 ax = std::abs(static_cast<i64>(x) - cx);
            const i64 ay = std::abs(static_cast<i64>(y) - cy);
            const i64 distance = std::max(std::max(ax, ay), (ax + ay) * 3 / 4);
            const i64 radius = 20 + ((site >> 20) % 28);
            waterBed = std::min(waterBed, (distance - radius) * kOne / 2);
        }
    }
    return std::min(terrain, waterBed);
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
    } else if (jitter % 41 == 0) {
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
