#include "test.hpp"

#include "core/math.hpp"
#include "core/serialize.hpp"
#include "sim/commands.hpp"
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

TEST_CASE("towns: local authorities refuse companies that demolish too much") {
    World w = makeWorld(20261017);
    explore(w, 6);
    Command create;
    create.payload = CmdCreateCompany{"Wrecking Co", 0xFF};
    const CompanyId co(static_cast<u8>(executeCommand(w, create).createdId));
    REQUIRE(w.companies.contains(co));
    w.companies[co].money = 100'000'000;
    TownId tid;
    for (const auto [id, t] : w.towns)
        if (t.buildings.size() >= 12) tid = id;
    REQUIRE(tid.valid());
    CHECK_EQ(towns::rating(w.towns[tid], co), kRatingInitial);

    const std::vector<BuildingId> buildings = w.towns[tid].buildings;
    int demolished = 0;
    std::string refusal;
    for (const BuildingId b : buildings) {
        if (!w.buildings.contains(b)) continue;
        const TilePos p = w.buildings[b].pos;
        Command c;
        c.company = co;
        c.payload = CmdClearArea{p, p};
        const CommandResult r = executeCommand(w, c);
        if (!r.ok) {
            refusal = r.error;
            break;
        }
        ++demolished;
    }
    // 200 -> 150 -> 100 -> 50 -> 0 -> -50: five demolitions, then the authority says no.
    CHECK_EQ(demolished, 5);
    CHECK(refusal.find("local authority") != std::string::npos);
    CHECK(towns::rating(w.towns[tid], co) < 0);
    CHECK_EQ(std::string(ratingName(towns::rating(w.towns[tid], co))), std::string("Poor"));

    // Without any presence in town the rating slowly recovers.
    for (int m = 0; m < 3; ++m) towns::monthly(w);
    CHECK(towns::rating(w.towns[tid], co) > -50);
}

TEST_CASE("towns: new villages spawn in open explored land") {
    World w = makeWorld(424242);
    explore(w, 6);
    const usize before = w.towns.size();
    for (int month = 0; month < 48; ++month) towns::spawnNewTowns(w);
    CHECK(w.towns.size() > before);
    for (const auto [a, ta] : w.towns)
        for (const auto [b, tb] : w.towns)
            if (a.value < b.value && (ta.cellKey == 0 || tb.cellKey == 0))
                CHECK(chebyshev(ta.centre.x - tb.centre.x, ta.centre.y - tb.centre.y) >= 40);
}
