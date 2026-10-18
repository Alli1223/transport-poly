#include "sim/terrain_gen.hpp"

#include "core/hash.hpp"
#include "core/math.hpp"
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

// Value noise at a fixed point position (1/65536 tiles), lattice spacing 2^shift tiles, so that
// warped coordinates vary smoothly.
i64 valueNoiseAt(u64 seed, u32 salt, i64 x, i64 y, int shift) {
    const int bits = 16 + shift;
    const i64 ix = x >> bits, iy = y >> bits;
    const i64 fx = (x - (ix << bits)) >> shift;
    const i64 fy = (y - (iy << bits)) >> shift;
    const auto lattice = [&](i64 a, i64 b) -> i64 {
        return static_cast<i64>(hashCoords(seed, static_cast<i32>(a), static_cast<i32>(b), salt) >> 16);
    };
    const i64 v00 = lattice(ix, iy), v10 = lattice(ix + 1, iy);
    const i64 v01 = lattice(ix, iy + 1), v11 = lattice(ix + 1, iy + 1);
    const i64 sx = fade(fx), sy = fade(fy);
    const i64 a = v00 + (((v10 - v00) * sx) >> 16);
    const i64 b = v01 + (((v11 - v01) * sx) >> 16);
    return a + (((b - a) * sy) >> 16);
}

// 1 at a noise value of one half, falling to 0 at 0 and 1: ridges along the noise's mid line.
constexpr i64 ridge(i64 n) { return kOne - (2 * n - kOne < 0 ? kOne - 2 * n : 2 * n - kOne); }

constexpr i64 mul(i64 a, i64 b) { return (a * b) >> 16; }

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
    kWarpX = 40,
    kWarpY,
    kLand,
    kRiverField = 50,
    kLakeField = 60,
    kHeat = 70,
    kWet,
    kDune,
};

// Per landmass: lattice spacing of the land noise (2^shift tiles), the share of the noise range
// that is sea, how fast the ground rises from the shore and how far inland hills and
// mountains reach full size.
struct LandShape {
    int shift;
    i64 sea;     // land where the land noise exceeds this (of kOne)
    i64 rise;    // levels per kOne of land noise above the shore
    i64 inland;  // land noise above the shore at which hills and mountains are full size
};

constexpr LandShape landShape(Landmass m) {
    switch (m) {
    case Landmass::Mainland: return {10, kOne * 30 / 100, 46, kOne * 12 / 100};
    case Landmass::Islands: return {8, kOne * 56 / 100, 70, kOne * 8 / 100};
    case Landmass::Archipelago: return {7, kOne * 58 / 100, 90, kOne * 5 / 100};
    case Landmass::SingleIsland: return {8, kOne * 50 / 100, 50, kOne * 10 / 100};
    case Landmass::Continents: break;
    }
    return {10, kOne * 48 / 100, 46, kOne * 12 / 100};
}

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

TerrainGenerator::ClimateSample TerrainGenerator::climateAt(i32 x, i32 y) const {
    ClimateSample c;
    const Climate climate = terrain_.climate;
    if (climate == Climate::Temperate || !smooth_ || terrain_.style == 0) return c;
    const i64 px = static_cast<i64>(x) << 16, py = static_cast<i64>(y) << 16;
    const i64 wet = (2 * valueNoiseAt(seed_, kWet, px, py, 9) + valueNoiseAt(seed_, kWet + 1, px, py, 7)) / 3;
    i64 heat = kOne / 2;
    switch (climate) {
    case Climate::Arid: heat = kOne * 85 / 100; break;
    case Climate::Arctic: heat = kOne * 12 / 100; break;
    case Climate::Tropical: heat = kOne * 88 / 100; break;
    default: heat = (2 * valueNoiseAt(seed_, kHeat, px, py, 11) + valueNoiseAt(seed_, kHeat + 1, px, py, 9)) / 3; break;
    }
    c.heat = heat;
    c.wet = wet;
    // Hot and dry: desert, hot and middling: steppe or savanna, hot and wet: jungle; cold and dry:
    // tundra, cold and wet: taiga; in between, temperate country.
    const i64 hot = kOne * 64 / 100, cold = kOne * 36 / 100;
    if (heat >= hot) {
        const bool arid = climate == Climate::Arid || (climate == Climate::Varied && heat > kOne * 70 / 100);
        const i64 dry = arid ? kOne * 56 / 100 : climate == Climate::Tropical ? kOne * 37 / 100 : kOne * 44 / 100;
        if (wet < dry) c.biome = arid ? Biome::Desert : Biome::Savanna;
        else if (arid && wet < kOne * 62 / 100) c.biome = Biome::Steppe;
        else c.biome = climate == Climate::Arid ? Biome::Steppe : Biome::Jungle;
    } else if (heat <= cold) {
        c.biome = wet < kOne * 50 / 100 ? Biome::Tundra : Biome::Taiga;
    }
    // How far into desert country a point lies, for the mesas there.
    if (heat >= hot && climate != Climate::Tropical) {
        const i64 drySide = std::clamp<i64>((kOne * 56 / 100 - wet) * 6, 0, kOne);
        const i64 hotSide = std::clamp<i64>((heat - hot) * 8, 0, kOne);
        c.desert = mul(drySide, hotSide);
    }
    return c;
}

Biome TerrainGenerator::biomeAt(i32 x, i32 y) const { return climateAt(x, y).biome; }

TreeType TerrainGenerator::treeFor(TilePos t, i32 level) const {
    const u32 h = hashCoords(seed_, t.x, t.y, kTreeJitter);
    switch (biomeAt(t.x, t.y)) {
    case Biome::Desert: return h % 3 == 0 ? TreeType::Shrub : TreeType::Cactus;
    case Biome::Steppe: return h % 2 ? TreeType::Shrub : TreeType::Acacia;
    case Biome::Savanna: return h % 4 == 0 ? TreeType::Palm : TreeType::Acacia;
    case Biome::Jungle: return h % 3 == 0 ? TreeType::Deciduous : TreeType::Palm;
    case Biome::Tundra: return h % 3 == 0 ? TreeType::Shrub : TreeType::Conifer;
    case Biome::Taiga: return TreeType::Conifer;
    case Biome::Temperate: break;
    }
    return level >= 14 || (level >= 9 && (h >> 8) % 3 == 0) ? TreeType::Conifer : TreeType::Deciduous;
}

TerrainGenerator::TerrainGenerator(u64 seed, bool waterways, bool smooth, TerrainSettings terrain)
    : seed_(seed), waterways_(waterways), smooth_(smooth), terrain_(terrain) {
    // A new company never begins in open sea: wherever the land noise is low at the start, the
    // land around it is raised just enough to stand clear of the water there.
    if (smooth_ && terrain_.style != 0 && terrain_.landmass != Landmass::SingleIsland) {
        static constexpr i64 kMargin[kLandmassCount] = {kOne * 14 / 100, kOne * 12 / 100, kOne * 10 / 100, kOne * 7 / 100, 0};
        const i64 wx = (valueNoiseAt(seed_, kWarpX, 0, 0, 9) - kOne / 2) * 128 + (valueNoiseAt(seed_, kWarpX + 1, 0, 0, 6) - kOne / 2) * 32;
        const i64 wy = (valueNoiseAt(seed_, kWarpY, 0, 0, 9) - kOne / 2) * 128 + (valueNoiseAt(seed_, kWarpY + 1, 0, 0, 6) - kOne / 2) * 32;
        const int m = static_cast<int>(terrain_.landmass);
        startLift_ = std::max<i64>(0, landShape(terrain_.landmass).sea + kMargin[m] - landNoise(wx, wy));
    }
}

i64 TerrainGenerator::landNoise(i64 qx, i64 qy) const {
    const int shift = landShape(terrain_.landmass).shift;
    return (55 * valueNoiseAt(seed_, kLand, qx, qy, shift) + 25 * valueNoiseAt(seed_, kLand + 1, qx, qy, shift - 1) +
            12 * valueNoiseAt(seed_, kLand + 2, qx, qy, shift - 2) + 8 * valueNoiseAt(seed_, kLand + 3, qx, qy, shift - 3)) /
           100;
}

i64 TerrainGenerator::rawHeight(i32 x, i32 y) const {
    if (!smooth_) return legacyHeight(x, y);
    return terrain_.style == 0 ? smoothHeight(x, y) : shapedHeight(x, y);
}

// The current terrain. Land and sea come from warped multi-octave noise, so coastlines fold
// into bays, headlands and islands; the ground rises from beaches through lowlands to hills,
// and ridged mountain ranges (as many as the settings ask for) stand inland. Rivers wind
// through the lowlands and lakes fill low basins, both at sea level.
i64 TerrainGenerator::shapedHeight(i32 x, i32 y) const {
    const TerrainSettings& t = terrain_;
    const LandShape shape = landShape(t.landmass);
    const i64 px = static_cast<i64>(x) << 16, py = static_cast<i64>(y) << 16;
    // Domain warp, two octaves: up to +-64 tiles across, +-16 tiles of finer folding.
    const i64 wx = (valueNoiseAt(seed_, kWarpX, px, py, 9) - kOne / 2) * 128 + (valueNoiseAt(seed_, kWarpX + 1, px, py, 6) - kOne / 2) * 32;
    const i64 wy = (valueNoiseAt(seed_, kWarpY, px, py, 9) - kOne / 2) * 128 + (valueNoiseAt(seed_, kWarpY + 1, px, py, 6) - kOne / 2) * 32;
    const i64 qx = px + wx, qy = py + wy;

    // Land noise: four octaves, the coarsest at the landmass's scale.
    i64 land = landNoise(qx, qy);
    const i64 dx = qx >> 16, dy = qy >> 16;
    const i64 d = static_cast<i64>(isqrt(static_cast<u64>(dx * dx + dy * dy)));
    if (t.landmass == Landmass::SingleIsland) {
        // One island around the origin: the land noise leans on a falloff from the centre.
        constexpr i64 kRadius = 760; // the coast lies about half this far out
        land = land * 45 / 100 + (kOne - d * kOne / kRadius) * 55 / 100;
    } else if (startLift_ > 0) {
        constexpr i64 kReach = 600;
        if (d < kReach) land += startLift_ * fade((kReach - d) * kOne / kReach) / kOne;
    }
    const i64 above = land - shape.sea; // of kOne: negative at sea

    i64 height;
    if (above < 0) {
        // Sea floor: shelving away from the shore.
        height = std::max<i64>(above * 60, -24 * kOne);
    } else {
        // Beach, lowland, upland: the ground rises gently near the shore, then faster.
        const i64 beach = kOne * 3 / 100;
        height = above < beach ? above * shape.rise / 3 : beach * shape.rise / 3 + (above - beach) * shape.rise;
        const i64 inland = std::clamp<i64>(above * kOne / shape.inland, 0, kOne);
        const i64 inlandSq = mul(inland, inland);

        const i64 hillMask = valueNoiseAt(seed_, kHillMask, px, py, 9);
        const i64 hills = mul(16 * valueNoiseAt(seed_, kHills, qx, qy, 7) + 5 * valueNoiseAt(seed_, kHills + 100, qx, qy, 5), hillMask);
        height += mul(hills, inland);

        if (t.mountains > 0) {
            // Ranges where the mountain mask is high; more mountains lower the bar and raise peaks.
            static constexpr i64 kBar[5] = {kOne, kOne * 58 / 100, kOne * 49 / 100, kOne * 41 / 100, kOne * 33 / 100};
            static constexpr i64 kPeak[5] = {0, 44, 56, 66, 84};
            const i64 mask = (2 * valueNoiseAt(seed_, kMountainMask, qx, qy, 10) + valueNoiseAt(seed_, kMountainMask + 1, qx, qy, 8)) / 3;
            const u8 m = std::min<u8>(t.mountains, kTerrainAmountMax);
            const i64 mm = std::clamp<i64>((mask - kBar[m]) * 4, 0, kOne);
            if (mm > 0) {
                // Ridged multifractal: each finer octave is weighted by the ridge above it, so
                // crests carry sharp detail while valleys stay broad.
                const i64 r1 = mul(ridge(valueNoiseAt(seed_, kRidges, qx, qy, 8)), ridge(valueNoiseAt(seed_, kRidges, qx, qy, 8)));
                const i64 w1 = std::min<i64>(2 * r1, kOne);
                const i64 n2 = ridge(valueNoiseAt(seed_, kRidges + 100, qx, qy, 6));
                const i64 r2 = mul(mul(n2, n2), w1);
                const i64 w2 = std::min<i64>(2 * r2, kOne);
                const i64 n3 = ridge(valueNoiseAt(seed_, kRidges + 200, qx, qy, 4));
                const i64 r3 = mul(mul(n3, n3), w2);
                const i64 ridges = (4 * r1 + 2 * r2 + r3) / 7;
                height += mul(mul(ridges * kPeak[m], mul(mm, mm)), inlandSq);
            }
        }

        const i64 detail = mul(3 * valueNoiseAt(seed_, kDetail, px, py, 3) + 2 * valueNoiseAt(seed_, kDetail + 100, px, py, 5), hillMask) / 2;
        height += mul(detail, std::clamp<i64>(above * 50, 0, kOne));

        if (t.climate != Climate::Temperate) {
            const ClimateSample c = climateAt(x, y);
            if (c.desert > 0 && height > 2 * kOne) {
                // Mesas and buttes: the land rises in flat steps with steep sides, and
                // wind-blown dunes ripple across the lower desert.
                constexpr i64 kStep = 6 * kOne;
                const i64 base = 2 * kOne, above2 = height - base;
                const i64 k = above2 / kStep, f = above2 - k * kStep;
                const i64 rise = fade(std::clamp<i64>((f * kOne / kStep - kOne * 68 / 100) * 100 / 32, 0, kOne));
                const i64 terraced = base + k * kStep + mul(rise, kStep);
                height += mul(terraced - height, c.desert);
            }
            if (c.desert > 0) {
                const i64 dune = ridge(valueNoiseAt(seed_, kDune, qx + qy / 3, qy, 4));
                height += mul(mul(dune, dune) * 2, mul(c.desert, std::clamp<i64>(above * 30, 0, kOne)));
            }
        }
    }
    if (!waterways_) return height;

    // Rivers: thin bands along the mid line of slowly varying noise, winding across the land.
    // Their beds rise with the height of the land around, so in hills and mountains they become
    // dry valleys and the water stays in the lowlands.
    if (t.rivers > 0) {
        const i64 lift = std::max<i64>(0, height - 16 * kOne) / 2;
        static constexpr int kShift[4] = {0, 10, 9, 9};
        static constexpr i64 kWidth[4] = {0, kOne * 9 / 1000, kOne * 10 / 1000, kOne * 11 / 1000};
        const int fields = t.rivers >= 3 ? 2 : 1;
        for (int f = 0; f < fields; ++f) {
            const i64 n = valueNoiseAt(seed_, kRiverField + f, qx, qy, kShift[std::min<u8>(t.rivers, kRiverAmountMax)]);
            const i64 across = n - kOne / 2 < 0 ? kOne / 2 - n : n - kOne / 2;
            const i64 bed = (across - kWidth[std::min<u8>(t.rivers, kRiverAmountMax)]) * 220 + lift;
            height = std::min(height, std::max<i64>(bed, -3 * kOne));
        }
    }
    // Lakes: basins where the lake noise peaks, filled to sea level where the land is low.
    if (t.lakes > 0) {
        const i64 lift = std::max<i64>(0, height - 12 * kOne) * 3 / 4;
        static constexpr i64 kBar[5] = {kOne, kOne * 76 / 100, kOne * 70 / 100, kOne * 64 / 100, kOne * 58 / 100};
        const i64 n = (2 * valueNoiseAt(seed_, kLakeField, qx, qy, 7) + valueNoiseAt(seed_, kLakeField + 1, qx, qy, 5)) / 3;
        const i64 bed = (kBar[std::min<u8>(t.lakes, kTerrainAmountMax)] - n) * 110 + lift;
        height = std::min(height, std::max<i64>(bed, -4 * kOne));
    }
    return height;
}

// Smooth terrain: broad lowlands, rolling hills and steep ridged mountains whose inclines vary
// continuously. Slopes reach a little over two levels per tile on the steepest mountainsides.
i64 TerrainGenerator::smoothHeight(i32 x, i32 y) const {
    const i64 c1 = valueNoise(seed_, kContinentLow, x, y, 10);
    const i64 c2 = valueNoise(seed_, kContinentHigh, x, y, 8);
    const i64 continent = (72 * (6 * c1 + 4 * c2)) / 10 - 28 * kOne;

    const i64 hillMask = valueNoise(seed_, kHillMask, x, y, 9);
    const i64 hills = ((16 * valueNoise(seed_, kHills, x, y, 7) + 5 * valueNoise(seed_, kHills + 100, x, y, 5)) * hillMask) >> 16;

    const i64 mm = std::clamp<i64>((valueNoise(seed_, kMountainMask, x, y, 10) - (kOne * 48) / 100) * 3, 0, kOne);
    i64 mountains = 0;
    if (mm > 0) {
        const auto ridged = [&](u32 salt, int shift) {
            const i64 n = valueNoise(seed_, salt, x, y, shift);
            return kOne - (2 * n - kOne < 0 ? kOne - 2 * n : 2 * n - kOne);
        };
        const i64 big = ridged(kRidges, 7);
        const i64 small = ridged(kRidges + 100, 5);
        // Ridges sharpen with height: squaring keeps valleys broad and peaks pointed.
        const i64 r = ((big * big) >> 16) * 46 + ((small * small) >> 16) * 11;
        mountains = (r * ((mm * mm) >> 16)) >> 16;
    }

    const i64 detail = ((3 * valueNoise(seed_, kDetail, x, y, 3) + 2 * valueNoise(seed_, kDetail + 100, x, y, 5)) * hillMask) >> 17;

    const i64 terrain = flatten(continent) + hills + mountains + detail;
    if (!waterways_) return terrain;
    return std::min(terrain, waterBed(x, y));
}

i64 TerrainGenerator::legacyHeight(i32 x, i32 y) const {
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
    // Taking the minimum of slope-bounded fields preserves the adjacent-corner rule.
    return std::min(terrain, waterBed(x, y));
}

// Sea-level waterways are carved into the height field itself. Rivers run across the world,
// with smooth bends independent of chunk activation.
i64 TerrainGenerator::waterBed(i32 x, i32 y) const {
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
    return waterBed;
}

u8 TerrainGenerator::quantise(i64 raw) {
    if (raw <= 0) return 0;
    return static_cast<u8>(std::min<i64>(raw >> 16, kMaxHeight));
}

u16 TerrainGenerator::quantiseFine(i64 raw) {
    if (raw <= 0) return 0;
    return static_cast<u16>(std::min<i64>((raw * kSubLevels + kOne / 2) >> 16, kMaxSmoothHeight * kSubLevels));
}

u16 TerrainGenerator::cornerFine(i32 cx, i32 cy) const {
    const i64 raw = rawHeight(cx, cy);
    return smooth_ ? quantiseFine(raw) : fineOf(quantise(raw));
}

i64 TerrainGenerator::forestDensity(i32 x, i32 y) const {
    return (7 * valueNoise(seed_, kForest, x, y, 7) + 3 * valueNoise(seed_, kForestDetail, x, y, 5)) / 10;
}

Tile TerrainGenerator::generateTile(TilePos t, const i64 raw[4], const u16 fine[4]) const {
    Tile tile;
    const bool allWet = raw[0] < 0 && raw[1] < 0 && raw[2] < 0 && raw[3] < 0;
    if (allWet) {
        tile.type = TileType::Water;
        tile.ground = Ground::Water;
        return tile;
    }
    const u16 fineLo = std::min(std::min(fine[0], fine[1]), std::min(fine[2], fine[3]));
    const u16 fineHi = std::max(std::max(fine[0], fine[1]), std::max(fine[2], fine[3]));
    const i32 lo = fineLo / kSubLevels;
    // Rock where the ground is steep: two levels across a tile (stepped terrain) or a little
    // more than that on smooth terrain.
    const bool steep = smooth_ ? fineHi - fineLo >= 2 * kSubLevels + kSubLevels / 2 : fineHi - fineLo >= 2 * kSubLevels;
    const bool nearSea = raw[0] < kOne / 2 || raw[1] < kOne / 2 || raw[2] < kOne / 2 || raw[3] < kOne / 2;
    const u32 jitter = hashCoords(seed_, t.x, t.y, kGroundJitter);

    const Biome biome = terrain_.climate == Climate::Temperate ? Biome::Temperate : biomeAt(t.x, t.y);
    i32 snowLine = smooth_ ? kSnowLine + 16 : kSnowLine;
    // Ground in the open by biome; how dense the woods grow there (forest noise must exceed the
    // start; lower starts mean more trees).
    Ground open = Ground::Grass;
    i64 forestStart = (kOne * 56) / 100;
    switch (biome) {
    case Biome::Desert: open = Ground::Desert; snowLine = 200; forestStart = kOne * 80 / 100; break;
    case Biome::Steppe: open = Ground::Steppe; snowLine = 200; forestStart = kOne * 70 / 100; break;
    case Biome::Savanna: open = Ground::Steppe; snowLine = 200; forestStart = kOne * 62 / 100; break;
    case Biome::Jungle: open = Ground::Jungle; snowLine = 200; forestStart = kOne * 44 / 100; break;
    case Biome::Tundra: open = Ground::Tundra; snowLine = 24; forestStart = kOne * 66 / 100; break;
    case Biome::Taiga: open = Ground::Grass; snowLine = 32; forestStart = kOne * 46 / 100; break;
    case Biome::Temperate: break;
    }
    if (lo >= snowLine) {
        tile.ground = Ground::Snow;
    } else if (steep) {
        tile.ground = Ground::Rock;
    } else if (lo >= snowLine - 5 && jitter % 3 == 0) {
        tile.ground = Ground::Rock;
    } else if (nearSea) {
        tile.ground = biome == Biome::Tundra || biome == Biome::Taiga ? Ground::Rough : Ground::Sand;
    } else if (jitter % 41 == 0 && (biome == Biome::Temperate || biome == Biome::Taiga)) {
        tile.ground = Ground::Rough;
    } else {
        tile.ground = open;
    }

    const bool growing = tile.ground == open || tile.ground == Ground::Grass || tile.ground == Ground::Rough;
    if (growing || (tile.ground == Ground::Snow && lo < snowLine + 3)) {
        const i64 forest = forestDensity(t.x, t.y);
        const u32 treeHash = hashCoords(seed_, t.x, t.y, kTreeJitter);
        const i64 kForestStart = forestStart;
        if (forest > kForestStart) {
            const i64 dens = (forest - kForestStart) * 12 / kOne; // 0..5
            const i64 count = std::clamp<i64>(dens + static_cast<i64>(treeHash % 3) - 1, 0, 4);
            tile.trees = static_cast<u8>(count);
        } else if (treeHash % (biome == Biome::Desert ? 23 : 47) == 0) {
            tile.trees = 1;
        }
        if (biome == Biome::Desert || biome == Biome::Steppe) tile.trees = std::min<u8>(tile.trees, 2);
        if (tile.trees > 0) {
            if (biome == Biome::Temperate) {
                const bool conifer = lo >= 14 || (lo >= 9 && (treeHash >> 8) % 3 == 0);
                tile.treeType = conifer ? TreeType::Conifer : TreeType::Deciduous;
            } else {
                tile.treeType = treeFor(t, lo);
            }
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
            chunk.fine(lx, ly) = smooth_ ? quantiseFine(r) : fineOf(quantise(r));
        }
    }
    const auto rawAt = [&](i32 lx, i32 ly) { return raw[static_cast<usize>(ly * kChunkCorners + lx)]; };
    for (i32 ly = 0; ly < kChunkSize; ++ly) {
        for (i32 lx = 0; lx < kChunkSize; ++lx) {
            const i64 r[4] = {rawAt(lx, ly), rawAt(lx + 1, ly), rawAt(lx + 1, ly + 1), rawAt(lx, ly + 1)};
            const u16 h[4] = {chunk.fine(lx, ly), chunk.fine(lx + 1, ly), chunk.fine(lx + 1, ly + 1),
                              chunk.fine(lx, ly + 1)};
            chunk.tile(lx, ly) = generateTile({origin.x + lx, origin.y + ly}, r, h);
        }
    }
}

} // namespace tp
