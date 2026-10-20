#include "core/serialize.hpp"
#include "sim/commands.hpp"
#include "sim/world.hpp"
#include "test.hpp"
using namespace tp;
namespace {
struct Interchange {
    World w;
    CompanyId company;
    StationId a, b, local, homeStop;
    VehicleId train, feeder;
    BuildingRef home, work;
    CommandResult run(CommandPayload payload) {
        Command c;
        c.company = company;
        c.payload = std::move(payload);
        return executeCommand(w, c);
    }
    BuildingRef building(TilePos at) {
        const auto id = w.buildings.create();
        auto& house = w.buildings[id];
        house.pos = at;
        house.type = 0;
        house.serial = id.value + 1;
        house.facing = Dir::S;
        auto& tile = *w.map.tile(at);
        tile.type = TileType::Building;
        tile.index = id.value;
        return house.ref(id);
    }
    Interchange(bool tram) {
        for (i32 z = 0; z < 2; ++z)
            for (i32 x = 0; x < 2; ++x) w.map.activate({x, z});
        for (i32 z = 1; z < 64; ++z)
            for (i32 x = 1; x < 64; ++x) {
                w.map.setCornerHeight(x, z, 4);
                *w.map.tile({x, z}) = Tile{};
            }
        const auto created = run(CmdCreateCompany{"Interchange", 0xFF});
        REQUIRE(created.ok);
        company = CompanyId(static_cast<u8>(created.createdId));
        w.companies[company].money = 50'000'000;
        REQUIRE(run(CmdBuildRoad{{6, 18}, {57, 18}}).ok);
        if (tram) REQUIRE(run(CmdBuildRoad{{6, 18}, {57, 18}, true}).ok);
        for (int n = 0; n < 2; ++n) {
            TrackAnchor anchor;
            anchor.pos = tileCentre({n ? 34 : 10, 10});
            const auto built = run(CmdBuildTrackStation{anchor, tileCentre({n ? 42 : 18, 10})});
            REQUIRE(built.ok);
            (n ? b : a) = StationId(built.createdId);
            REQUIRE(run(CmdStationEntrance{StationId(built.createdId), {n ? 38 : 12, 18}}).ok);
        }
        REQUIRE(run(CmdBuildRoadStop{{41, 18}, tram}).ok);
        local = StationId(w.map.tile({41, 18})->index);
        REQUIRE(run(CmdBuildRoadStop{{53, 18}, tram}).ok);
        homeStop = StationId(w.map.tile({53, 18})->index);
        home = building({54, 17});
        work = building({8, 17});
        Vehicle rail;
        rail.model = 7;
        rail.cars = {7, 11};
        rail.owner = company;
        rail.state = VehicleState::Loading;
        rail.stopped = false;
        Order oa;
        oa.station = a;
        Order ob;
        ob.station = b;
        rail.orders = {oa, ob};
        rail.orderIndex = 1;
        rail.freeTrack = true;
        const auto edge = track::platformsOf(w, b).front().front();
        rail.runs = {{edge, true}};
        rail.progress = w.trackEdges[edge].length - 6;
        rail.tile = {40, 10};
        train = w.vehicles.create(rail);
        Vehicle bus;
        bus.model = tram ? 6 : 0;
        bus.owner = company;
        bus.state = VehicleState::Loading;
        bus.stopped = false;
        Order localOrder;
        localOrder.station = local;
        Order homeOrder;
        homeOrder.station = homeStop;
        bus.orders = {localOrder, homeOrder};
        bus.tile = {41, 18};
        bus.entry = Dir::W;
        bus.exit = Dir::E;
        bus.progress = kPathStraight / 2;
        feeder = w.vehicles.create(bus);
        passengers::updateRouting(w);
    }
    PersonId rider(VehicleId vehicle, StationId from, StationId next, StationId final, bool goingHome) {
        Person p;
        p.serial = 1;
        p.home = home;
        p.work = work;
        p.vehicle = vehicle;
        p.station = from;
        p.targetStation = next;
        p.finalStation = final;
        p.state = PersonState::Riding;
        p.purpose = goingHome ? TripPurpose::Home : TripPurpose::Work;
        p.tripTo2 = w.buildings[goingHome ? home.id : work.id].centre2();
        const auto id = w.people.create(p);
        w.vehicles[vehicle].passengers.push_back(id);
        return id;
    }
};
} // namespace
TEST_CASE("interchanges: train arrivals walk to a nearby bus or tram and ride home") {
    for (bool tram : {false, true}) {
        Interchange f(tram);
        passengers::TransitPlan plan;
        REQUIRE(passengers::planTrip(f.w, f.w.buildings[f.work.id].centre2(),
                                     f.w.buildings[f.home.id].centre2(), plan));
        CHECK(plan.origin == f.a);
        CHECK(plan.destination == f.homeStop);
        const auto id =
            f.rider(f.train, f.a, f.b, f.b, true); // also upgrades an existing rail-only itinerary
        REQUIRE(passengers::exchangeAtStop(f.w, f.train, f.b) == 1);
        auto& p = f.w.people[id];
        CHECK(p.finalStation == f.homeStop);
        CHECK(p.state == PersonState::ToStop);
        CHECK(p.station == f.local);
        REQUIRE(p.walkPath.size() > 3);
        CHECK_EQ(f.w.stations[f.local].waitingCount(), 0u);
        World saved;
        fromBytes(toBytes(f.w, kSaveVersion), saved, kSaveVersion);
        passengers::updateRouting(saved);
        f.w.clock.tick = p.walkEnd;
        saved.clock.tick = p.walkEnd;
        people::process(f.w);
        people::process(saved);
        CHECK_EQ(f.w.stateHash(), saved.stateHash());
        REQUIRE(p.state == PersonState::Waiting);
        CHECK(p.targetStation == f.homeStop);
        vehicles::tickAll(f.w);
        REQUIRE(p.state == PersonState::Riding);
        CHECK(p.vehicle == f.feeder);
        f.w.clock.tick = f.w.vehicles[f.feeder].loadingUntil;
        f.w.vehicles[f.feeder].tile = {53, 18};
        f.w.vehicles[f.feeder].orderIndex = 1;
        REQUIRE(passengers::exchangeAtStop(f.w, f.feeder, f.homeStop) == 1);
        REQUIRE(p.state == PersonState::FromStop);
        f.w.clock.tick = p.walkEnd;
        people::process(f.w);
        CHECK(p.state == PersonState::AtHome);
    }
}
TEST_CASE("interchanges: bus and tram arrivals walk into the train station for work") {
    for (bool tram : {false, true}) {
        Interchange f(tram);
        const auto id = f.rider(f.feeder, f.homeStop, f.local, f.a, false);
        REQUIRE(passengers::exchangeAtStop(f.w, f.feeder, f.local) == 1);
        auto& p = f.w.people[id];
        REQUIRE(p.state == PersonState::ToStop);
        CHECK(p.station == f.b);
        f.w.clock.tick = p.walkEnd;
        people::process(f.w);
        REQUIRE(p.state == PersonState::Waiting);
        CHECK(p.targetStation == f.a);
        // Let the passenger reach the assigned platform spot, then use the real loading tick.
        f.w.clock.tick = p.walkEnd;
        vehicles::tickAll(f.w);
        CHECK(p.state == PersonState::Riding);
        CHECK(p.vehicle == f.train);
    }
}
TEST_CASE("interchanges: disconnected pavements and withdrawn feeder services create no transfer") {
    Interchange f(true);
    REQUIRE(f.w.routing.find(f.a, f.homeStop));
    *f.w.map.tile({39, 18}) = Tile{};
    passengers::markDirty(f.w);
    passengers::updateRouting(f.w);
    CHECK(!f.w.routing.find(f.a, f.homeStop));
    REQUIRE(f.run(CmdBuildRoad{{38, 18}, {40, 18}}).ok);
    passengers::updateRouting(f.w);
    REQUIRE(f.w.routing.find(f.a, f.homeStop));
    f.w.vehicles[f.feeder].stopped = true;
    f.w.vehicles[f.feeder].state = VehicleState::InDepot;
    passengers::markDirty(f.w);
    passengers::updateRouting(f.w);
    CHECK(!f.w.routing.find(f.a, f.homeStop));
}
