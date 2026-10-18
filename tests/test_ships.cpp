#include "test.hpp"

#include "core/serialize.hpp"
#include "sim/commands.hpp"
#include "sim/water_path.hpp"
#include "sim/world.hpp"

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

TEST_CASE("ships: docks and shipyards need a shore facing water") {
    LakeWorld lw;
    CHECK(lw.run(CmdBuildDock{{9, 16}, Dir::E}).ok);
    CHECK(!lw.run(CmdBuildDock{{5, 16}, Dir::E}).ok);   // inland
    CHECK(!lw.run(CmdBuildDock{{20, 16}, Dir::E}).ok);  // on the water
    CHECK(!lw.run(CmdBuildShipDepot{{9, 12}, Dir::W}).ok); // facing land
    CHECK(lw.run(CmdBuildShipDepot{{9, 12}, Dir::E}).ok);
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
