#include "test.hpp"
#include "core/serialize.hpp"
#include "sim/commands.hpp"
#include "sim/rail_path.hpp"
#include "sim/road_path.hpp"
#include "sim/water_path.hpp"
#include "sim/world.hpp"

using namespace tp;

namespace {
struct SpanWorld {
    World w;
    CompanyId company;
    explicit SpanWorld(bool hill = false, bool vertical = false) {
        w.map.activate({0, 0});
        for (i32 y = 1; y < 32; ++y)
            for (i32 x = 1; x < 32; ++x) {
                const i32 along = vertical ? y : x;
                const i32 depth = std::clamp(std::min(along - 9, 24 - along), 0, 6);
                w.map.setCornerHeight(x, y, static_cast<u8>(hill ? 6 + depth : 6 - depth));
            }
        for (i32 y = 1; y < 31; ++y)
            for (i32 x = 1; x < 31; ++x) {
                Tile& t = *w.map.tile({x, y});
                t = Tile{};
                if (!hill && w.map.corners({x, y}).max() == 0) { t.type = TileType::Water; t.ground = Ground::Water; }
            }
        Command c;
        c.payload = CmdCreateCompany{"Span Co", 0};
        const auto result = executeCommand(w, c);
        REQUIRE(result.ok);
        company = CompanyId{static_cast<u8>(result.createdId)};
        w.companies[company].money = 10'000'000;
    }
    Command command(CommandPayload p) { Command c; c.company = company; c.payload = std::move(p); return c; }
    CommandResult run(CommandPayload p) { return executeCommand(w, command(std::move(p))); }
};
}

TEST_CASE("spans: road bridge connects banks and preserves navigable water underneath") {
    SpanWorld s;
    const auto before = toBytes(s.w, kSaveVersion);
    const auto preview = testCommand(s.w, s.command(CmdBuildSpan{{8, 16}, {24, 16}, false, false}));
    REQUIRE(preview.ok);
    CHECK(toBytes(s.w, kSaveVersion) == before);
    REQUIRE(s.run(CmdBuildSpan{{24, 16}, {8, 16}, false, false}).ok);
    REQUIRE(s.run(CmdBuildRoad{{5, 16}, {8, 16}}).ok);
    REQUIRE(s.run(CmdBuildRoad{{24, 16}, {28, 16}}).ok);
    CHECK(findRoadPath(s.w, {5, 16}, Dir::W, {{28, 16}}, VehicleType::Road, DepotId{}).has_value());
    CHECK(isWaterTile(s.w, {16, 16}));
    CHECK(canSail(s.w, {16, 15}, headingOf(Dir::S)));
    CHECK_EQ(s.w.map.tile({16, 16})->deckHeight, 6);
    CHECK(!s.run(CmdBuildRoadStop{{16, 16}, false}).ok);
    CHECK(!s.run(CmdRemoveRoad{{16, 16}, {16, 16}, false}).ok);
    Vehicle bus; bus.model = 0; bus.owner = s.company; bus.tile = {5, 16};
    bus.entry = Dir::W; bus.exit = Dir::E; bus.state = VehicleState::Driving; bus.stopped = false;
    const VehicleId busId = s.w.vehicles.create(bus);
    bool crossed = false;
    for (int i = 0; i < 200; ++i) {
        vehicles::tickAll(s.w);
        crossed = crossed || s.w.vehicles[busId].tile.x >= 26;
    }
    CHECK(crossed);
    s.w.vehicles.destroy(busId);
    World copy;
    fromBytes(toBytes(s.w, kSaveVersion), copy, kSaveVersion);
    CHECK_EQ(copy.map.tile({16, 16})->span, kSpanBridge | kSpanAxisX);
    REQUIRE(s.run(CmdRemoveSpan{{16, 16}}).ok);
    CHECK(s.w.map.tile({16, 16})->type == TileType::Water);
    CHECK_EQ(s.w.map.tile({8, 16})->span, 0);
    CHECK(!findRoadPath(s.w, {5, 16}, Dir::W, {{28, 16}}, VehicleType::Road, DepotId{}).has_value());
}

TEST_CASE("spans: rail tunnels route through covered terrain and reserve every tile") {
    SpanWorld s(true);
    REQUIRE(s.run(CmdBuildSpan{{8, 16}, {24, 16}, true, true}).ok);
    REQUIRE(s.run(CmdBuildRail{{5, 16}, {8, 16}}).ok);
    REQUIRE(s.run(CmdBuildRail{{24, 16}, {28, 16}}).ok);
    CHECK(findRailPath(s.w, {5, 16}, Dir::W, {{28, 16}}, DepotId{}).has_value());
    CHECK(!s.run(CmdCycleSignal{{16, 16}}).ok);
    s.w.map.tile({17, 16})->extra2 = 2;
    CHECK(!s.run(CmdRemoveSpan{{8, 16}}).ok);
    s.w.map.tile({17, 16})->extra2 = 0;
    Vehicle v; v.model = 7; v.tile = {17, 16}; v.state = VehicleState::Driving;
    const VehicleId id = s.w.vehicles.create(v);
    CHECK(!s.run(CmdRemoveSpan{{8, 16}}).ok);
    s.w.vehicles.destroy(id);
    REQUIRE(s.run(CmdRemoveSpan{{8, 16}}).ok);
    CHECK(s.w.map.tile({17, 16})->type == TileType::Clear);
}

TEST_CASE("spans: invalid terrain and partial edits fail without changing the world") {
    SpanWorld s;
    const auto before = toBytes(s.w, kSaveVersion);
    CHECK(!s.run(CmdBuildSpan{{8, 16}, {24, 17}, false, false}).ok);
    CHECK(!s.run(CmdBuildSpan{{8, 16}, {24, 16}, false, true}).ok);
    CHECK(!s.run(CmdBuildSpan{{8, 16}, {23, 16}, false, false}).ok);
    CHECK(toBytes(s.w, kSaveVersion) == before);
    REQUIRE(s.run(CmdBuildSpan{{8, 16}, {24, 16}, true, false}).ok);
    const auto built = toBytes(s.w, kSaveVersion);
    CHECK(!s.run(CmdRemoveRail{{12, 16}, {16, 16}}).ok);
    CHECK(!s.run(CmdClearArea{{16, 16}, {16, 16}}).ok);
    CHECK(toBytes(s.w, kSaveVersion) == built);
}

TEST_CASE("spans: a train traverses a tunnel and continues identically after save load") {
    SpanWorld s(true);
    REQUIRE(s.run(CmdBuildSpan{{8, 16}, {24, 16}, true, true}).ok);
    REQUIRE(s.run(CmdBuildRail{{5, 16}, {8, 16}}).ok);
    REQUIRE(s.run(CmdBuildRail{{24, 16}, {28, 16}}).ok);
    REQUIRE(s.run(CmdBuildRail{{2, 16}, {3, 16}}).ok);
    REQUIRE(s.run(CmdBuildRailStation{{3, 16}, {4, 16}}).ok);
    REQUIRE(s.run(CmdBuildRailStation{{25, 16}, {28, 16}}).ok);
    const auto depot = s.run(CmdBuildRailDepot{{1, 16}, Dir::E});
    REQUIRE(depot.ok);
    const auto purchase = s.run(CmdBuyVehicle{DepotId{depot.createdId}, 7});
    REQUIRE(purchase.ok);
    const VehicleId train{purchase.createdId};
    REQUIRE(s.run(CmdAddCarriage{train, 11}).ok);
    Order a, b; a.station = StationId{s.w.map.tile({3, 16})->index}; b.station = StationId{s.w.map.tile({25, 16})->index};
    REQUIRE(s.run(CmdSetOrders{train, {a, b}}).ok);
    REQUIRE(s.run(CmdStartStopVehicle{train}).ok);
    bool traversed = false;
    for (int i = 0; i < 3000; ++i) {
        s.w.tick();
        traversed = traversed || s.w.vehicles[train].tile.x >= 25;
    }
    CHECK(traversed);
    World copy;
    fromBytes(toBytes(s.w, kSaveVersion), copy, kSaveVersion);
    for (int i = 0; i < 1000; ++i) { s.w.tick(); copy.tick(); }
    CHECK(toBytes(s.w, kSaveVersion) == toBytes(copy, kSaveVersion));
}

TEST_CASE("spans: every road and rail mode connects in both axes") {
    for (const bool tunnel : {false, true})
        for (const bool rail : {false, true})
            for (const bool vertical : {false, true}) {
                SpanWorld s(tunnel, vertical);
                const TilePos a = vertical ? TilePos{16, 8} : TilePos{8, 16};
                const TilePos b = vertical ? TilePos{16, 24} : TilePos{24, 16};
                REQUIRE(s.run(CmdBuildSpan{a, b, rail, tunnel}).ok);
                const Dir travel = vertical ? Dir::S : Dir::E;
                if (rail) CHECK(findRailPath(s.w, a, opposite(travel), {b}, DepotId{}).has_value());
                else CHECK(findRoadPath(s.w, a, opposite(travel), {b}, VehicleType::Road, DepotId{}).has_value());
                REQUIRE(s.run(CmdRemoveSpan{a}).ok);
                CHECK_EQ(s.w.map.tile(a)->span, 0);
            }
}

TEST_CASE("spans: bridge dismantling restores canal ownership") {
    SpanWorld s;
    s.w.map.tile({16, 16})->owner = s.company.value;
    REQUIRE(s.run(CmdBuildSpan{{8, 16}, {24, 16}, false, false}).ok);
    REQUIRE(s.run(CmdRemoveSpan{{8, 16}}).ok);
    CHECK(s.w.map.tile({16, 16})->type == TileType::Water);
    CHECK_EQ(s.w.map.tile({16, 16})->owner, s.company.value);
    CHECK_EQ(s.w.map.tile({17, 16})->owner, kOwnerNone);
}
