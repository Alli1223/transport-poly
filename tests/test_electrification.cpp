#include "test.hpp"

#include "core/serialize.hpp"
#include "sim/commands.hpp"
#include "sim/track.hpp"
#include "sim/world.hpp"

#include <string_view>

using namespace tp;

namespace {

Command cmd(CompanyId company, CommandPayload payload) {
    Command c;
    c.company = company;
    c.payload = std::move(payload);
    return c;
}

// Empty level land in 1990, when electric trains are on sale.
struct WiredWorld {
    World w;
    CompanyId co, rival;

    WiredWorld() : w(makeSettings()) {
        for (i32 y = 0; y < 2; ++y)
            for (i32 x = 0; x < 2; ++x) w.activateChunk({x, y});
        for (i32 y = 1; y < 2 * kChunkSize; ++y)
            for (i32 x = 1; x < 2 * kChunkSize; ++x) w.map.setCornerFine(x, y, 4 * kSubLevels);
        for (i32 y = 0; y < 2 * kChunkSize; ++y)
            for (i32 x = 0; x < 2 * kChunkSize; ++x) *w.map.tile({x, y}) = Tile{};
        co = CompanyId(static_cast<u8>(executeCommand(w, cmd(CompanyId{}, CmdCreateCompany{"Wire Co", 0xFF})).createdId));
        rival = CompanyId(static_cast<u8>(executeCommand(w, cmd(CompanyId{}, CmdCreateCompany{"Rival Co", 0xFE})).createdId));
        w.companies[co].money = 50'000'000;
        w.companies[rival].money = 50'000'000;
    }

    static WorldSettings makeSettings() {
        WorldSettings s;
        s.seed = 777;
        s.startYear = 1990;
        return s;
    }

    CommandResult run(CommandPayload p, CompanyId as = {}) { return executeCommand(w, cmd(as.valid() ? as : co, std::move(p))); }
};

TPoint at(double tx, double ty) { return {static_cast<i32>(tx * kTrackUnits), static_cast<i32>(ty * kTrackUnits)}; }

TrackAnchor freeAnchor(TPoint p) {
    TrackAnchor a;
    a.pos = p;
    return a;
}

TrackAnchor nodeAnchor(const World& w, TPoint p, TDir into) {
    TrackAnchor a;
    a.node = track::nodeNear(w, p, 40, true);
    a.pos = p;
    a.into = into;
    return a;
}

StationId stationAt(const World& w, TPoint p) {
    for (const auto [id, e] : w.trackEdges)
        if (e.kind == TrackKind::Platform && e.samples.size() > 1)
            if (distance(track::pointOn(e, true, e.length / 2).p, p) < 4 * kTrackUnits) return e.station;
    return {};
}

u16 modelNamed(std::string_view name) {
    const auto models = vehicleModels();
    for (u16 m = 0; m < models.size(); ++m)
        if (models[m].name == name) return m;
    return 0;
}

} // namespace

TEST_CASE("electrification: electric trains run only under overhead wires") {
    WiredWorld tw;
    REQUIRE(tw.run(CmdBuildTrackStation{freeAnchor(at(8, 16)), at(14, 16)}).ok);
    REQUIRE(tw.run(CmdBuildTrack{nodeAnchor(tw.w, at(14, 16), TDir{kDirOne, 0}), freeAnchor(at(40, 18))}).ok);
    TrackAnchor bStart;
    bStart.node = track::nodeNear(tw.w, at(40, 18), 40);
    REQUIRE(bStart.node.valid());
    REQUIRE(tw.run(CmdBuildTrackStation{bStart, at(46, 20)}).ok);
    const CommandResult dep = tw.run(CmdBuildTrackDepot{track::nodeNear(tw.w, at(8, 16), 40)});
    REQUIRE(dep.ok);
    const StationId a = stationAt(tw.w, at(11, 16));
    const StationId b = stationAt(tw.w, at(43, 19));
    REQUIRE(a.valid());
    REQUIRE(b.valid());

    const u16 electric = modelNamed("Continental Electric");
    const u16 diesel = modelNamed("Highball Express");
    REQUIRE(vehicleModel(electric).electric);
    CHECK(!vehicleModel(diesel).electric);
    const CommandResult buy = tw.run(CmdBuyVehicle{DepotId(dep.createdId), electric});
    REQUIRE(buy.ok);
    const VehicleId train(buy.createdId);
    REQUIRE(tw.run(CmdAddCarriage{train, modelNamed("Intercity Coach")}).ok);
    CHECK(tw.w.vehicles[train].needsWires());
    Order oa, ob;
    oa.station = a;
    ob.station = b;
    REQUIRE(tw.run(CmdSetOrders{train, {oa, ob}}).ok);
    REQUIRE(tw.run(CmdStartStopVehicle{train}).ok);

    // Without wires it cannot find a way out along the line.
    const auto countVisits = [&](i64 ticks) {
        i64 lastB = tw.w.stations[b].lastVisitTick, visits = 0;
        for (i64 t = 0; t < ticks; ++t) {
            tw.w.tick();
            if (tw.w.stations[b].lastVisitTick != lastB) {
                lastB = tw.w.stations[b].lastVisitTick;
                ++visits;
            }
        }
        return visits;
    };
    CHECK_EQ(countVisits(kTicksPerDay / 4), 0);

    // Wiring up everything joined to one edge electrifies the whole line, at a price.
    TrackEdgeId any;
    for (const auto [id, e] : tw.w.trackEdges) {
        any = id;
        break;
    }
    CHECK(!tw.run(CmdElectrifyTrack{any, true, false}, tw.rival).ok);
    const Money before = tw.w.companies[tw.co].money;
    const CommandResult wired = tw.run(CmdElectrifyTrack{any, true, false});
    REQUIRE(wired.ok);
    CHECK(wired.cost > 0);
    CHECK(tw.w.companies[tw.co].money < before);
    for (const auto [id, e] : tw.w.trackEdges) CHECK(e.electrified);
    CHECK(!tw.run(CmdElectrifyTrack{any, false, false}).ok); // already electrified

    CHECK(countVisits(kTicksPerDay) >= 2);
    CHECK(!tw.w.vehicles[train].lost);

    // The wires over track an electric train holds stay up.
    bool refused = false;
    for (const auto [id, e] : tw.w.trackEdges)
        if (e.reservedBy == train) refused = refused || !tw.run(CmdElectrifyTrack{id, false, true}).ok;
    CHECK(refused);

    // Saves keep the wires.
    World copy;
    fromBytes(toBytes(tw.w, kSaveVersion), copy, kSaveVersion);
    for (const auto [id, e] : tw.w.trackEdges) CHECK_EQ(copy.trackEdges[id].electrified, e.electrified);
    CHECK_EQ(copy.stateHash(), tw.w.stateHash());
}

TEST_CASE("electrification: a train with any diesel engine needs no wires") {
    Vehicle v;
    v.cars = {modelNamed("Continental Electric"), modelNamed("Intercity Coach")};
    CHECK(v.needsWires());
    v.cars.push_back(modelNamed("Highball Express"));
    CHECK(!v.needsWires());
    v.cars = {modelNamed("Regional EMU")};
    CHECK(v.needsWires());
    v.cars = {modelNamed("Steel Coach")};
    CHECK(!v.needsWires()); // no engine at all
}
