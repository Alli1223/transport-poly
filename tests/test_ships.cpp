#include "test.hpp"

#include "core/serialize.hpp"
#include "sim/commands.hpp"
#include "sim/water_path.hpp"
#include "sim/world.hpp"
#include "sim/prices.hpp"

using namespace tp;

namespace {

Command cmd(CompanyId company, CommandPayload payload) {
    Command c;
    c.company = company;
    c.payload = std::move(payload);
    return c;
}

// Two chunks of low land around a lake (tiles 10..49 x 8..23) with an island in the middle.
struct LakeWorld {
    World w;
    CompanyId co;

    LakeWorld() : w(makeSettings()) {
        w.activateChunk({0, 0});
        w.activateChunk({1, 0});
        for (i32 y = 1; y < kChunkSize; ++y)
            for (i32 x = 1; x < 2 * kChunkSize; ++x) {
                const bool lake = x >= 10 && x <= 50 && y >= 8 && y <= 24;
                w.map.setCornerHeight(x, y, lake ? 0 : 1);
            }
        for (i32 y = 1; y < kChunkSize - 1; ++y)
            for (i32 x = 1; x < 2 * kChunkSize - 1; ++x) {
                Tile& t = *w.map.tile({x, y});
                t = Tile{};
                const bool water = x >= 10 && x <= 49 && y >= 8 && y <= 23;
                const bool island = x >= 26 && x <= 31 && y >= 11 && y <= 20;
                if (water && !island) t.type = TileType::Water;
            }
        co = CompanyId(static_cast<u8>(executeCommand(w, cmd(CompanyId{}, CmdCreateCompany{"Ferry Co", 0xFF})).createdId));
        w.companies[co].money = 50'000'000;
    }

    static WorldSettings makeSettings() {
        WorldSettings s;
        s.seed = 99002;
        return s;
    }

    CommandResult run(CommandPayload p) { return executeCommand(w, cmd(co, std::move(p))); }
};

} // namespace

TEST_CASE("ships: water paths stay on water and never cut corners") {
    LakeWorld lw;
    const TilePos from{12, 16}, to{46, 15};
    const TilePos goals[] = {to};
    const auto path = findWaterPath(lw.w, from, goals);
    REQUIRE(path.has_value());
    TilePos t = from;
    for (const u8 h : *path) {
        CHECK(canSail(lw.w, t, h));
        t = headingStep(t, h);
        CHECK(isWaterTile(lw.w, t));
    }
    CHECK(t == to);
    // Around the island, so longer than the straight line but still mostly diagonal / straight.
    CHECK(path->size() >= 34u);
    CHECK(path->size() < 50u);

    // Land-locked goal: no path.
    const TilePos inland[] = {TilePos{28, 15}};
    CHECK(!findWaterPath(lw.w, from, inland).has_value());
}

TEST_CASE("canals: flooding an island opens a shorter navigable route and charges once") {
    LakeWorld lw;
    const TilePos goals[] = {{46, 16}};
    const auto before = findWaterPath(lw.w, {12, 16}, goals);
    REQUIRE(before.has_value());
    const auto distance = [](const std::vector<u8>& path) {
        i32 total = 0;
        for (const u8 h : path) total += headingLength(h);
        return total;
    };
    lw.w.map.tile({28, 16})->trees = 3;
    const auto state = toBytes(lw.w, kSaveVersion);
    const Command flood = cmd(lw.co, CmdEditWater{{24, 16}, {34, 16}, true});
    const auto preview = testCommand(lw.w, flood);
    REQUIRE(preview.ok);
    CHECK(toBytes(lw.w, kSaveVersion) == state);
    const Money money = lw.w.companies[lw.co].money;
    const auto built = executeCommand(lw.w, flood);
    REQUIRE(built.ok);
    CHECK_EQ(built.cost, 6 * prices::kCanalTile + 3 * prices::kRemoveTree);
    CHECK_EQ(built.cost, preview.cost);
    CHECK_EQ(lw.w.companies[lw.co].money, money - built.cost);
    for (i32 x = 26; x <= 31; ++x) {
        const Tile& t = *lw.w.map.tile({x, 16});
        CHECK(t.type == TileType::Water);
        CHECK_EQ(t.owner, lw.co.value);
        CHECK_EQ(t.trees, 0);
    }
    const auto after = findWaterPath(lw.w, {12, 16}, goals);
    REQUIRE(after.has_value());
    CHECK(distance(*after) < distance(*before));
    CHECK_EQ(distance(*after), 34 * kTileUnits);
    CHECK(!executeCommand(lw.w, flood).ok);
    World copy;
    fromBytes(toBytes(lw.w, kSaveVersion), copy, kSaveVersion);
    CHECK(copy.map.chunk({0, 0})->tiles == lw.w.map.chunk({0, 0})->tiles);
    CHECK_EQ(copy.map.tile({28, 16})->owner, lw.co.value);
    REQUIRE(lw.run(CmdEditWater{{28, 16}, {28, 16}, false}).ok);
    CHECK(lw.w.map.tile({28, 16})->type == TileType::Clear);
    CHECK(lw.w.map.tile({28, 16})->ground == Ground::Sand);
    CHECK_EQ(lw.w.map.tile({28, 16})->owner, kOwnerNone);
}

TEST_CASE("canals: invalid areas fail atomically and owned canals cannot be stolen") {
    LakeWorld lw;
    REQUIRE(lw.run(CmdEditWater{{26, 16}, {31, 16}, true}).ok);
    const auto created = lw.run(CmdCreateCompany{"Competitor", 1});
    REQUIRE(created.ok);
    const CompanyId other(static_cast<u8>(created.createdId));
    CHECK(!executeCommand(lw.w, cmd(other, CmdEditWater{{28, 16}, {28, 16}, false})).ok);
    CHECK(!lw.run(CmdTerraform{{28, 16}, 1}).ok);
    lw.w.map.setCornerHeight(30, 18, 1);
    const auto before = toBytes(lw.w, kSaveVersion);
    CHECK(!lw.run(CmdEditWater{{26, 18}, {31, 18}, true}).ok);
    CHECK(toBytes(lw.w, kSaveVersion) == before);
    CHECK(!lw.run(CmdEditWater{{0, 0}, {65, 65}, true}).ok);
    CHECK(!lw.run(CmdEditWater{{70, 12}, {70, 12}, true}).ok);
    CHECK(toBytes(lw.w, kSaveVersion) == before);
}

TEST_CASE("canals: reclamation protects ships, their next tile, docks and shipyards") {
    LakeWorld lw;
    REQUIRE(lw.run(CmdBuildDock{{9, 16}, Dir::E}).ok);
    REQUIRE(lw.run(CmdBuildShipDepot{{9, 18}, Dir::E}).ok);
    CHECK(!lw.run(CmdEditWater{{10, 16}, {10, 16}, false}).ok);
    CHECK(!lw.run(CmdEditWater{{10, 18}, {10, 18}, false}).ok);
    Vehicle ship;
    ship.model = 13;
    ship.owner = lw.co;
    ship.state = VehicleState::Driving;
    ship.tile = {12, 16};
    ship.heading = headingOf(Dir::E);
    ship.moving = true;
    lw.w.vehicles.create(ship);
    CHECK(!lw.run(CmdEditWater{{12, 16}, {12, 16}, false}).ok);
    CHECK(!lw.run(CmdEditWater{{13, 16}, {13, 16}, false}).ok);
    REQUIRE(lw.run(CmdEditWater{{14, 16}, {14, 16}, false}).ok);
}

TEST_CASE("canals: monthly upkeep charges owned water and command packets round trip") {
    LakeWorld lw;
    lw.w.settings.interestPercent = 0;
    REQUIRE(lw.run(CmdEditWater{{26, 16}, {31, 16}, true}).ok);
    const Money before = lw.w.companies[lw.co].money;
    lw.w.clock.tick = lw.w.clock.tickOfDay(daysFromCivil(1950, 2, 1)) - 1;
    lw.w.tick();
    CHECK_EQ(lw.w.companies[lw.co].money, before - 6 * prices::kCanalMaintenance);
    const Command original = cmd(lw.co, CmdEditWater{{26, 16}, {31, 16}, false});
    Command decoded;
    fromBytes(toBytes(original), decoded);
    CHECK(toBytes(original) == toBytes(decoded));
    REQUIRE(executeCommand(lw.w, decoded).ok);
    CHECK(!isWaterTile(lw.w, {28, 16}));
}

TEST_CASE("ships: docks and shipyards need a shore facing water") {
    LakeWorld lw;
    CHECK(lw.run(CmdBuildDock{{9, 16}, Dir::E}).ok);
    CHECK(!lw.run(CmdBuildDock{{5, 16}, Dir::E}).ok);   // inland
    CHECK(!lw.run(CmdBuildDock{{20, 16}, Dir::E}).ok);  // on the water
    CHECK(!lw.run(CmdBuildShipDepot{{9, 12}, Dir::W}).ok); // facing land
    CHECK(lw.run(CmdBuildShipDepot{{9, 12}, Dir::E}).ok);
}

TEST_CASE("orders: ship sails via water and passes a dock without loading") {
    LakeWorld lw;
    REQUIRE(lw.run(CmdBuildDock{{9, 16}, Dir::E}).ok);
    REQUIRE(lw.run(CmdBuildDock{{50, 17}, Dir::W}).ok);
    const auto dep = lw.run(CmdBuildShipDepot{{9, 11}, Dir::E}); REQUIRE(dep.ok);
    u16 model = 0;
    for (u16 i = 0; i < vehicleModels().size(); ++i)
        if (vehicleModel(i).type == VehicleType::Ship && vehicleModel(i).introYear <= 1950) model = i;
    const auto buy = lw.run(CmdBuyVehicle{DepotId(dep.createdId), model}); REQUIRE(buy.ok);
    const VehicleId id(buy.createdId);
    const StationId a(lw.w.map.tile({9, 16})->index), b(lw.w.map.tile({50, 17})->index);
    Order via, pass, stop;
    via.kind = OrderKind::Via; via.waypoint = {18, 9};
    pass.station = a; pass.flags = kOrderNonStop; stop.station = b;
    REQUIRE(lw.run(CmdSetOrders{id, {via, pass, stop}}).ok);
    REQUIRE(lw.run(CmdStartStopVehicle{id}).ok);
    bool visitedVia = false, reachedB = false;
    for (int i = 0; i < 2500 && !reachedB; ++i) {
        lw.w.tick();
        visitedVia = visitedVia || lw.w.vehicles[id].tile == via.waypoint;
        CHECK_EQ(lw.w.stations[a].lastVisitTick, -1);
        reachedB = lw.w.stations[b].lastVisitTick >= 0;
    }
    CHECK(visitedVia); CHECK(reachedB);
}

TEST_CASE("ships: a ferry shuttles between two docks around an island") {
    LakeWorld lw;
    REQUIRE(lw.run(CmdBuildDock{{9, 16}, Dir::E}).ok);
    REQUIRE(lw.run(CmdBuildDock{{50, 17}, Dir::W}).ok);
    const CommandResult dep = lw.run(CmdBuildShipDepot{{9, 11}, Dir::E});
    REQUIRE(dep.ok);
    const StationId a(lw.w.map.tile({9, 16})->index);
    const StationId b(lw.w.map.tile({50, 17})->index);
    REQUIRE(a != b);
    u16 launch = 0;
    for (u16 i = 0; i < vehicleModels().size(); ++i)
        if (vehicleModel(i).type == VehicleType::Ship && vehicleModel(i).introYear <= 1950) launch = i;
    const CommandResult buy = lw.run(CmdBuyVehicle{DepotId(dep.createdId), launch});
    REQUIRE(buy.ok);
    const VehicleId ship(buy.createdId);
    Order oa, ob;
    oa.station = a;
    ob.station = b;
    REQUIRE(lw.run(CmdSetOrders{ship, {oa, ob}}).ok);
    REQUIRE(lw.run(CmdStartStopVehicle{ship}).ok);

    int visitsA = 0, visitsB = 0;
    i64 lastA = -1, lastB = -1;
    bool offWater = false;
    for (i64 t = 0; t < kTicksPerDay; ++t) {
        lw.w.tick();
        const Vehicle& v = lw.w.vehicles[ship];
        if (v.state != VehicleState::InDepot && !isWaterTile(lw.w, v.tile)) offWater = true;
        if (lw.w.stations[a].lastVisitTick != lastA) {
            lastA = lw.w.stations[a].lastVisitTick;
            ++visitsA;
        }
        if (lw.w.stations[b].lastVisitTick != lastB) {
            lastB = lw.w.stations[b].lastVisitTick;
            ++visitsB;
        }
    }
    const Vehicle& v = lw.w.vehicles[ship];
    CHECK(!offWater);
    CHECK(!v.lost);
    CHECK(visitsA >= 3);
    CHECK(visitsB >= 3);

    // Send it home: it sails back into the shipyard and stops there.
    REQUIRE(lw.run(CmdSendToDepot{ship}).ok);
    for (i64 t = 0; t < kTicksPerDay / 2 && lw.w.vehicles[ship].state != VehicleState::InDepot; ++t) lw.w.tick();
    CHECK(lw.w.vehicles[ship].state == VehicleState::InDepot);
    CHECK(lw.w.vehicles[ship].stopped);

    // Save / load while sailing continues identically.
    REQUIRE(lw.run(CmdStartStopVehicle{ship}).ok);
    for (i64 t = 0; t < 300; ++t) lw.w.tick();
    const auto bytes = toBytes(lw.w, kSaveVersion);
    World copy;
    fromBytes(bytes, copy, kSaveVersion);
    for (i64 t = 0; t < kTicksPerDay / 4; ++t) {
        lw.w.tick();
        copy.tick();
    }
    CHECK_EQ(copy.stateHash(), lw.w.stateHash());
}
