#include "test.hpp"

#include "core/rng.hpp"
#include "core/serialize.hpp"
#include "sim/map.hpp"
#include "sim/terraform.hpp"

#include <algorithm>
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
