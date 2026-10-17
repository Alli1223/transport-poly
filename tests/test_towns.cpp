#include "test.hpp"

#include "core/serialize.hpp"
#include "sim/road.hpp"
#include "sim/world.hpp"

using namespace tp;

namespace {

void explore(World& w, i32 radiusChunks) {
    for (i32 y = -radiusChunks; y <= radiusChunks; ++y)
        for (i32 x = -radiusChunks; x <= radiusChunks; ++x) w.activateChunk({x, y});
}

World makeWorld(u64 seed) {
    WorldSettings s;
    s.seed = seed;
    return World(s);
}

} // namespace

TEST_CASE("towns: sites are deterministic and spread out") {
    const TerrainGenerator gen(123);
    int sites = 0;
    for (i32 cy = -10; cy < 10; ++cy) {
        for (i32 cx = -10; cx < 10; ++cx) {
            const auto a = townSiteForCell(gen, cx, cy);
            const auto b = townSiteForCell(gen, cx, cy);
            CHECK(a.has_value() == b.has_value());
            if (!a) continue;
            ++sites;
            CHECK(a->centre == b->centre);
            CHECK(a->centre.x >= cx * kTownCellSize && a->centre.x < (cx + 1) * kTownCellSize);
            CHECK(gen.cornerHeight(a->centre.x, a->centre.y) >= 1);
        }
    }
    CHECK(sites > 20);
}

TEST_CASE("towns: exploring founds towns with roads, buildings and residents") {
    World w = makeWorld(20261017);
    explore(w, 6);
    REQUIRE(w.towns.size() > 0);
    i64 residents = 0;
    for (const auto [id, b] : w.buildings) residents += b.residents;
    CHECK_EQ(residents, static_cast<i64>(w.people.size()));
    for (const auto [id, t] : w.towns) {
        CHECK(!t.name.empty());
        CHECK(!t.roads.empty());
        CHECK(!t.buildings.empty());
        CHECK(t.population > 0);
        // Every building tile references its building.
        for (const BuildingId bid : t.buildings) {
            const Building& b = w.buildings[bid];
            const Tile* tile = w.map.tile(b.pos);
            REQUIRE(tile != nullptr);
            CHECK(tile->type == TileType::Building);
            CHECK_EQ(tile->index, bid.value);
        }
        // Road tiles carry road pieces.
        for (const TilePos r : t.roads) {
            const Tile* tile = w.map.tile(r);
            REQUIRE(tile != nullptr);
            CHECK(tile->type == TileType::Road);
            CHECK(roadPieces(*tile) != 0);
        }
    }
    // Town names are unique.
    for (const auto [a, ta] : w.towns)
        for (const auto [b, tb] : w.towns)
            if (a != b) CHECK(ta.name != tb.name);
}

TEST_CASE("towns: people commute on weekdays and shop at weekends") {
    World w = makeWorld(20261017);
    explore(w, 6);
    REQUIRE(w.people.size() > 0);
    i64 employed = 0;
    for (const auto [id, p] : w.people) employed += p.employed() ? 1 : 0;
    CHECK(employed > 0);
    // Simulate one week and count the trips made.
    for (i64 t = 0; t < 7 * kTicksPerDay; ++t) w.tick();
    i64 trips = 0;
    for (const auto [id, t] : w.towns) trips += t.thisMonth.trips + t.lastMonth.trips;
    CHECK(trips > static_cast<i64>(w.people.size()));
    // Everyone has a pending event (nobody gets stuck).
    for (const auto [id, p] : w.people) CHECK(p.nextEvent >= w.clock.tick);
}

TEST_CASE("towns: simulation with towns is deterministic and survives save/load") {
    World a = makeWorld(99);
    World b = makeWorld(99);
    explore(a, 5);
    explore(b, 5);
    CHECK_EQ(a.stateHash(), b.stateHash());
    for (i64 t = 0; t < 2 * kTicksPerDay; ++t) {
        a.tick();
        b.tick();
    }
    CHECK_EQ(a.stateHash(), b.stateHash());

    const std::vector<u8> bytes = toBytes(a, kSaveVersion);
    World c;
    fromBytes(bytes, c, kSaveVersion);
    CHECK_EQ(c.stateHash(), a.stateHash());
    for (i64 t = 0; t < 3 * kTicksPerDay; ++t) {
        a.tick();
        c.tick();
    }
    CHECK_EQ(a.stateHash(), c.stateHash());
    CHECK_EQ(a.people.size(), c.people.size());
}

TEST_CASE("towns: towns grow over time") {
    World w = makeWorld(99);
    explore(w, 5);
    REQUIRE(w.towns.size() > 0);
    usize before = w.buildings.size();
    for (i64 t = 0; t < 40 * kTicksPerDay; ++t) w.tick();
    CHECK(w.buildings.size() > before);
}
