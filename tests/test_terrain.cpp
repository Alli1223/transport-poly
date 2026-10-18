#include "test.hpp"

#include "core/rng.hpp"
#include "core/serialize.hpp"
#include "sim/chunk.hpp"
#include "sim/map.hpp"
#include "sim/terraform.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <array>
#include <tuple>
#include <cstdio>
#include <cstdlib>

using namespace tp;

TEST_CASE("terrain: adjacent corners never differ by more than one level") {
    const TerrainGenerator gen(20261017);
    Rng rng(99);
    int worst = 0;
    int land = 0;
    int samples = 0;
    // Random probes over a huge range of coordinates, including very far from the origin.
    for (int i = 0; i < 200000; ++i) {
        const i32 scale = (i % 4 == 0) ? 1 << 30 : (i % 4 == 1 ? 1 << 20 : 4096);
        const i32 x = rng.range(-scale, scale);
        const i32 y = rng.range(-scale, scale);
        const int h = gen.cornerHeight(x, y);
        const int hx = gen.cornerHeight(x + 1, y);
        const int hy = gen.cornerHeight(x, y + 1);
        worst = std::max({worst, std::abs(h - hx), std::abs(h - hy)});
        land += h > 0;
        ++samples;
    }
    CHECK(worst <= 1);
    // Both land and sea should exist in reasonable proportions.
    CHECK(land > samples / 5);
    CHECK(land < samples * 19 / 20);
}

TEST_CASE("terrain: dense scan of a region respects the height rule") {
    const TerrainGenerator gen(7);
    int worst = 0;
    int maxH = 0;
    for (i32 y = -300; y < 300; ++y) {
        int prev = gen.cornerHeight(-300, y);
        for (i32 x = -299; x < 300; ++x) {
            const int h = gen.cornerHeight(x, y);
            worst = std::max(worst, std::abs(h - prev));
            worst = std::max(worst, std::abs(h - gen.cornerHeight(x, y + 1)));
            maxH = std::max(maxH, h);
            prev = h;
        }
    }
    CHECK(worst <= 1);
    CHECK(maxH > 0);
}

TEST_CASE("terrain: chunk generation is deterministic and seamless") {
    const TerrainGenerator gen(12345);
    Chunk a, b, east;
    gen.generateChunk({3, -2}, a);
    gen.generateChunk({3, -2}, b);
    CHECK(a.heights == b.heights);
    CHECK(a.tiles == b.tiles);
    gen.generateChunk({4, -2}, east);
    for (i32 ly = 0; ly < kChunkCorners; ++ly) CHECK_EQ(a.height(kChunkSize, ly), east.height(0, ly));
    // Water tiles must be flat at sea level.
    for (i32 ly = 0; ly < kChunkSize; ++ly)
        for (i32 lx = 0; lx < kChunkSize; ++lx)
            if (a.tile(lx, ly).type == TileType::Water) CHECK_EQ(a.corners(lx, ly).max(), 0);
}

TEST_CASE("terrain: waterways are seamless in every chunk activation order") {
    for (const u64 seed : {1ULL, 7ULL, 12345ULL}) {
        const TerrainGenerator gen(seed);
        for (const ChunkPos cp : {ChunkPos{-17, -33}, ChunkPos{0, 0}, ChunkPos{31, 15}}) {
            Chunk centre, west, north, again;
            gen.generateChunk({cp.x - 1, cp.y}, west);
            gen.generateChunk({cp.x, cp.y - 1}, north);
            gen.generateChunk(cp, centre);
            gen.generateChunk(cp, again);
            CHECK(centre.tiles == again.tiles);
            for (i32 i = 0; i < kChunkCorners; ++i) {
                CHECK_EQ(west.height(kChunkSize, i), centre.height(0, i));
                CHECK_EQ(north.height(i, kChunkSize), centre.height(i, 0));
            }
            const TilePos o = chunkOrigin(cp);
            for (i32 y = 0; y < kChunkSize; ++y)
                for (i32 x = 0; x < kChunkSize; ++x) {
                    CHECK_EQ(centre.height(x, y), gen.cornerHeight(o.x + x, o.y + y));
                    if (centre.tile(x, y).type == TileType::Water) {
                        CHECK_EQ(centre.corners(x, y).max(), 0);
                        CHECK_EQ(centre.tile(x, y).trees, 0);
                    }
                }
        }
    }
}

TEST_CASE("terrain: rivers continue across a full region of inland terrain") {
    const TerrainGenerator gen(20261017);
    // Each column across several chunk borders contains a navigable-width channel.
    // The full river period is searched, without depending on its internal seed salts.
    for (i32 x = -70; x <= 70; x += 7) {
        bool channel = false;
        for (i32 y = -1024; y < 1024 && !channel; ++y) {
            channel = true;
            for (i32 dy = 0; dy <= 4; ++dy)
                if (gen.rawHeight(x, y + dy) >= 0 || gen.rawHeight(x + 1, y + dy) >= 0) {
                    channel = false;
                    break;
                }
        }
        CHECK(channel);
    }
}

TEST_CASE("terrain: lakes form inland basins surrounded by dry land") {
    const TerrainGenerator gen(12345);
    const TerrainGenerator land(12345, false);
    bool found = false;
    for (i32 y = -1024; y < 1024 && !found; y += 16)
        for (i32 x = -1024; x < 1024 && !found; x += 16) {
            if (land.rawHeight(x, y) <= 5 * kHeightOne || gen.rawHeight(x, y) >= 0) continue;
            bool dryRing = true;
            for (i32 dy = -1; dy <= 1; ++dy)
                for (i32 dx = -1; dx <= 1; ++dx)
                    if ((dx || dy) && gen.rawHeight(x + 80 * dx, y + 80 * dy) <= 0) dryRing = false;
            found = dryRing;
        }
    CHECK(found);
}

TEST_CASE("map: activation, tile access and corner sharing") {
    Map map(42);
    CHECK(!map.isActive({0, 0}));
    CHECK(map.tile({5, 5}) == nullptr);
    CHECK(map.activate({0, 0}));
    CHECK(!map.activate({0, 0}));
    REQUIRE(map.tile({5, 5}) != nullptr);
    CHECK(map.tile({-1, 5}) == nullptr);
    // Inactive areas still report generated heights.
    CHECK_EQ(map.cornerHeight(-10, -10), map.generator().cornerHeight(-10, -10));

    // A corner on the chunk border is only editable when both chunks are active.
    CHECK(!map.cornerEditable(32, 5));
    map.activate({1, 0});
    CHECK(map.cornerEditable(32, 5));
    map.setCornerHeight(32, 5, 9);
    CHECK_EQ(map.chunk({0, 0})->height(32, 5), 9);
    CHECK_EQ(map.chunk({1, 0})->height(0, 5), 9);
    CHECK_EQ(map.cornerHeight(32, 5), 9);
}

TEST_CASE("map: serialisation round trip") {
    Map map(5);
    map.activate({0, 0});
    map.activate({-1, 2});
    map.tile({3, 3})->trees = 4;
    const auto bytes = toBytes(map);
    Map loaded(5);
    fromBytes(bytes, loaded);
    CHECK_EQ(loaded.activeChunks().size(), 2u);
    CHECK(loaded.activeChunks()[1] == (ChunkPos{-1, 2}));
    CHECK_EQ(loaded.tile({3, 3})->trees, 4);
    CHECK(loaded.chunk({-1, 2})->tiles == map.chunk({-1, 2})->tiles);
}

TEST_CASE("terraform: raising propagates and keeps the height rule") {
    Map map(1);
    for (i32 y = -1; y <= 1; ++y)
        for (i32 x = -1; x <= 1; ++x) map.activate({x, y});
    // Flatten a region to height 3 by direct edits, then raise the centre repeatedly.
    for (i32 y = 0; y <= 20; ++y)
        for (i32 x = 0; x <= 20; ++x) map.setCornerHeight(x, y, 3);
    for (int i = 0; i < 4; ++i) {
        const TerraformPlan plan = planTerraform(map, 10, 10, +1);
        REQUIRE(plan.ok);
        applyTerraform(map, plan);
    }
    CHECK_EQ(map.cornerHeight(10, 10), 7);
    CHECK_EQ(map.cornerHeight(11, 10), 6);
    CHECK_EQ(map.cornerHeight(13, 10), 4);
    CHECK_EQ(map.cornerHeight(14, 10), 3);
    for (i32 y = 0; y < 20; ++y)
        for (i32 x = 0; x < 20; ++x) {
            CHECK(std::abs(map.cornerHeight(x, y) - map.cornerHeight(x + 1, y)) <= 1);
            CHECK(std::abs(map.cornerHeight(x, y) - map.cornerHeight(x, y + 1)) <= 1);
        }
    const TerraformPlan down = planTerraform(map, 10, 10, -1);
    REQUIRE(down.ok);
    CHECK_EQ(down.changes.size(), 1u);
    // Cannot terraform next to unexplored land.
    CHECK(!planTerraform(map, 64, 0, +1).ok);
}

TEST_CASE("terrain: smooth terrain has varied inclines within a sane bound") {
    const TerrainGenerator gen(20261017, true, true);
    int worst = 0;
    std::vector<int> slopes(64, 0);
    Rng rng(4);
    for (int i = 0; i < 300000; ++i) {
        const i32 x = rng.range(-20000, 20000), y = rng.range(-20000, 20000);
        const int h = gen.cornerFine(x, y);
        const int d = std::max(std::abs(h - gen.cornerFine(x + 1, y)), std::abs(h - gen.cornerFine(x, y + 1)));
        worst = std::max(worst, d);
        ++slopes[static_cast<usize>(std::min(d, 63))];
    }
    if (std::getenv("TP_SLOPES"))
        for (int d = 0; d < 64; ++d)
            if (slopes[static_cast<usize>(d)]) std::printf("slope %d: %d\n", d, slopes[static_cast<usize>(d)]);
    // Steep mountainsides, but no cliffs.
    CHECK(worst <= 4 * kSubLevels);
    // Inclines are not all one step: many different gradients occur, including fractions of a level.
    int distinct = 0;
    for (int d = 1; d < 64; ++d) distinct += slopes[static_cast<usize>(d)] > 0;
    CHECK(distinct >= 12);
    CHECK(slopes[3] > 0);
    CHECK(slopes[9] > 0);
}

TEST_CASE("terrain: structures snap their corners to whole levels") {
    Map map(5, true, true);
    for (i32 y = -1; y <= 1; ++y)
        for (i32 x = -1; x <= 1; ++x) map.activate({x, y});
    // Find a tile with fractional corners.
    TilePos t{0, 0};
    for (i32 i = 0; i < 400; ++i) {
        const TilePos p{4 + i % 20, 4 + i / 20};
        const FineCorners c = map.fineCorners(p);
        if (c.h[0] % kSubLevels || c.h[1] % kSubLevels || c.h[2] % kSubLevels || c.h[3] % kSubLevels) {
            t = p;
            break;
        }
    }
    const TileCorners levels = map.corners(t);
    map.tile(t)->type = TileType::Road;
    map.touch(t);
    const FineCorners after = map.fineCorners(t);
    for (int i = 0; i < 4; ++i) CHECK_EQ(after.h[i], fineOf(levels.h[i]));
}

TEST_CASE("terraform: the brush shapes land smoothly and leaves structures alone") {
    Map map(77, true, true);
    for (i32 y = -1; y <= 1; ++y)
        for (i32 x = -1; x <= 1; ++x) map.activate({x, y});
    for (i32 y = 0; y <= 20; ++y)
        for (i32 x = 0; x <= 20; ++x) map.setCornerFine(x, y, 10 * kSubLevels);
    const auto none = [](i32, i32) { return false; };
    BrushPlan plan = planBrush(map, {10, 10}, 4, BrushMode::Raise, 0, none);
    applyBrush(map, plan);
    // Highest in the middle, falling off towards the rim, untouched outside.
    CHECK_EQ(map.cornerFine(10, 10), 11 * kSubLevels);
    CHECK(map.cornerFine(12, 10) > 10 * kSubLevels);
    CHECK(map.cornerFine(12, 10) < map.cornerFine(11, 10));
    CHECK_EQ(map.cornerFine(15, 10), 10 * kSubLevels);
    // Locked corners do not move.
    const auto lockedCentre = [](i32 cx, i32 cy) { return cx == 10 && cy == 10; };
    plan = planBrush(map, {10, 10}, 4, BrushMode::Lower, 0, lockedCentre);
    applyBrush(map, plan);
    CHECK_EQ(map.cornerFine(10, 10), 11 * kSubLevels);
    // Levelling pulls everything near the centre to the target height.
    for (int i = 0; i < 6; ++i) applyBrush(map, planBrush(map, {10, 10}, 3, BrushMode::Level, 10 * kSubLevels, none));
    CHECK_EQ(map.cornerFine(10, 10), 10 * kSubLevels);
    CHECK_EQ(map.cornerFine(11, 10), 10 * kSubLevels);
}

namespace {

TerrainSettings landOf(Landmass m, u8 mountains = 2, u8 lakes = 2, u8 rivers = 2) {
    TerrainSettings t;
    t.landmass = m;
    t.mountains = mountains;
    t.lakes = lakes;
    t.rivers = rivers;
    return t;
}

struct LandStats {
    int waterPercent = 0;
    int peak = 0;        // levels
    int highPermille = 0; // of land above 40 levels
};

LandStats survey(const TerrainGenerator& gen, i32 radius = 1536, i32 step = 8) {
    int water = 0, samples = 0, high = 0;
    LandStats st;
    for (i32 y = -radius; y < radius; y += step)
        for (i32 x = -radius; x < radius; x += step) {
            const i64 r = gen.rawHeight(x, y);
            ++samples;
            if (r < 0) {
                ++water;
                continue;
            }
            if (r > 40 * kHeightOne) ++high;
            st.peak = std::max<int>(st.peak, static_cast<int>(r / kHeightOne));
        }
    st.waterPercent = water * 100 / samples;
    st.highPermille = high * 1000 / std::max(1, samples - water);
    return st;
}

} // namespace

TEST_CASE("terrain: land types range from mainland to scattered islands") {
    const auto water = [](Landmass m) { return survey(TerrainGenerator(4242, true, true, landOf(m))).waterPercent; };
    const int mainland = water(Landmass::Mainland), continents = water(Landmass::Continents);
    const int islands = water(Landmass::Islands), archipelago = water(Landmass::Archipelago);
    CHECK(mainland < 20);
    CHECK(mainland < continents);
    CHECK(continents > 35);
    CHECK(continents < islands);
    CHECK(islands > 55);
    CHECK(archipelago > 55);
    // A single island: land at the start, open sea all around it far out.
    const TerrainGenerator island(4242, true, true, landOf(Landmass::SingleIsland));
    CHECK(island.rawHeight(0, 0) > 0);
    for (const TilePos far : {TilePos{1200, 0}, TilePos{-1200, 0}, TilePos{0, 1200}, TilePos{900, -900}})
        CHECK(island.rawHeight(far.x, far.y) < 0);
}

TEST_CASE("terrain: every land type starts on land") {
    for (int m = 0; m < kLandmassCount; ++m)
        for (u64 seed = 1; seed <= 16; ++seed) {
            const TerrainGenerator gen(seed * 7919, true, true, landOf(static_cast<Landmass>(m)));
            int land = 0, samples = 0;
            for (i32 y = -200; y <= 200; y += 10)
                for (i32 x = -200; x <= 200; x += 10) {
                    ++samples;
                    land += gen.rawHeight(x, y) >= 0;
                }
            CHECK(land * 100 / samples >= 10);
        }
}

TEST_CASE("terrain: mountain settings range from rolling hills to alpine peaks") {
    const LandStats none = survey(TerrainGenerator(99, true, true, landOf(Landmass::Mainland, 0)));
    const LandStats normal = survey(TerrainGenerator(99, true, true, landOf(Landmass::Mainland, 2)));
    const LandStats alpine = survey(TerrainGenerator(99, true, true, landOf(Landmass::Mainland, 4)));
    CHECK(none.peak < 40);
    CHECK_EQ(none.highPermille, 0);
    CHECK(normal.peak > 50);
    CHECK(alpine.peak > 80);
    CHECK(alpine.highPermille > normal.highPermille);
    CHECK(normal.highPermille > 0);
}

TEST_CASE("terrain: more lakes and rivers make more inland water") {
    const LandStats dry = survey(TerrainGenerator(7, true, true, landOf(Landmass::Mainland, 2, 0, 0)));
    const LandStats lakes = survey(TerrainGenerator(7, true, true, landOf(Landmass::Mainland, 2, 4, 0)));
    const LandStats rivers = survey(TerrainGenerator(7, true, true, landOf(Landmass::Mainland, 2, 0, 3)));
    CHECK(lakes.waterPercent > dry.waterPercent + 2);
    CHECK(rivers.waterPercent > dry.waterPercent + 1);
}

TEST_CASE("terrain: shaped terrain is deterministic and seamless across chunks") {
    const TerrainSettings ts = landOf(Landmass::Islands, 3, 3, 3);
    const TerrainGenerator a(31337, true, true, ts), b(31337, true, true, ts);
    Chunk left, right, below;
    a.generateChunk({4, -2}, left);
    b.generateChunk({5, -2}, right);
    a.generateChunk({4, -1}, below);
    for (i32 i = 0; i <= kChunkSize; ++i) {
        CHECK_EQ(left.fine(kChunkSize, i), right.fine(0, i));
        CHECK_EQ(left.fine(i, kChunkSize), below.fine(i, 0));
    }
    for (i32 y = -300; y < 300; y += 37)
        for (i32 x = -300; x < 300; x += 41) CHECK_EQ(a.rawHeight(x, y), b.rawHeight(x, y));
}

TEST_CASE("terrain: worlds saved before terrain settings keep their land") {
    WorldSettings s;
    s.seed = 2024;
    s.terrain = landOf(Landmass::Archipelago, 4, 1, 3);
    World w(s);
    // A save from before the settings existed reads as the original terrain.
    World old;
    fromBytes(toBytes(w, 18), old, 18);
    CHECK(old.settings.terrain == TerrainSettings::original());
    const TerrainGenerator original(2024, true, true, TerrainSettings::original());
    for (i32 x = -500; x < 500; x += 97) CHECK_EQ(old.map.generator().rawHeight(x, 3 * x), original.rawHeight(x, 3 * x));
    // A current save keeps the choices.
    World copy;
    fromBytes(toBytes(w, kSaveVersion), copy, kSaveVersion);
    CHECK(copy.settings.terrain == s.terrain);
    for (i32 x = -500; x < 500; x += 97) CHECK_EQ(copy.map.generator().rawHeight(x, -x), w.map.generator().rawHeight(x, -x));
}

TEST_CASE("terrain: climates grow their own biomes, ground and trees") {
    const auto count = [](Climate climate) {
        TerrainSettings ts;
        ts.climate = climate;
        const TerrainGenerator gen(77, true, true, ts);
        std::array<int, 16> ground{}, trees{}, biomes{};
        for (i32 y = -1024; y < 1024; y += 16)
            for (i32 x = -1024; x < 1024; x += 16) {
                const i64 raw[4] = {gen.rawHeight(x, y), gen.rawHeight(x + 1, y), gen.rawHeight(x + 1, y + 1), gen.rawHeight(x, y + 1)};
                const u16 fine[4] = {gen.cornerFine(x, y), gen.cornerFine(x + 1, y), gen.cornerFine(x + 1, y + 1), gen.cornerFine(x, y + 1)};
                const Tile t = gen.generateTile({x, y}, raw, fine);
                ++ground[static_cast<usize>(t.ground)];
                if (t.trees) ++trees[static_cast<usize>(t.treeType)];
                ++biomes[static_cast<usize>(gen.biomeAt(x, y))];
            }
        return std::tuple{ground, trees, biomes};
    };
    const auto [aridGround, aridTrees, aridBiomes] = count(Climate::Arid);
    CHECK(aridGround[static_cast<usize>(Ground::Desert)] > aridGround[static_cast<usize>(Ground::Grass)]);
    CHECK(aridTrees[static_cast<usize>(TreeType::Cactus)] > 0);
    CHECK_EQ(aridTrees[static_cast<usize>(TreeType::Deciduous)], 0);
    const auto [arcticGround, arcticTrees, arcticBiomes] = count(Climate::Arctic);
    CHECK(arcticGround[static_cast<usize>(Ground::Tundra)] + arcticGround[static_cast<usize>(Ground::Snow)] > 0);
    CHECK(arcticTrees[static_cast<usize>(TreeType::Conifer)] > arcticTrees[static_cast<usize>(TreeType::Deciduous)]);
    const auto [tropicGround, tropicTrees, tropicBiomes] = count(Climate::Tropical);
    CHECK(tropicGround[static_cast<usize>(Ground::Jungle)] > 0);
    CHECK(tropicTrees[static_cast<usize>(TreeType::Palm)] > 0);
    const auto [variedGround, variedTrees, variedBiomes] = count(Climate::Varied);
    int kinds = 0;
    for (const int n : variedBiomes) kinds += n > 0;
    CHECK(kinds >= 4);
    // Temperate worlds are made exactly as before climates existed.
    const auto [temperateGround, temperateTrees, temperateBiomes] = count(Climate::Temperate);
    CHECK_EQ(temperateBiomes[static_cast<usize>(Biome::Temperate)], 128 * 128);
    CHECK_EQ(temperateGround[static_cast<usize>(Ground::Desert)] + temperateGround[static_cast<usize>(Ground::Jungle)], 0);
}

TEST_CASE("terrain: saves from before climates are temperate") {
    WorldSettings s;
    s.terrain.climate = Climate::Arid;
    World w(s);
    World old;
    fromBytes(toBytes(w, 19), old, 19);
    CHECK(old.settings.terrain.climate == Climate::Temperate);
    World copy;
    fromBytes(toBytes(w, kSaveVersion), copy, kSaveVersion);
    CHECK(copy.settings.terrain.climate == Climate::Arid);
}
