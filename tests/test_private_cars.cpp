#include "test.hpp"
#include "core/serialize.hpp"
#include "sim/world.hpp"
#include "sim/road.hpp"
#include "sim/private_cars.hpp"
#include "sim/commands.hpp"
#include "sim/passengers.hpp"
#ifdef TP_TEST_RENDERING
#include "render/vehicle_pose.hpp"
#endif
using namespace tp;
namespace {
struct Streets {
    World w;
    TownId town;
    BuildingId home, work;
    Streets() {
        w.map.activate({0, 0}); w.map.activate({1, 0});
        for (int y = 1; y < 32; ++y) for (int x = 1; x < 63; ++x) {
            *w.map.tile({x, y}) = Tile{}; w.map.setCornerHeight(x, y, 4);
        }
        town = w.towns.create(); w.towns[town].centre = {18, 15}; w.towns[town].radius = 23;
        home = building({8, 9}, 0); work = building({28, 9}, 8);
        road({8, 10}, {8, 18}); road({8, 18}, {28, 18}); road({28, 18}, {28, 10});
    }
    BuildingId building(TilePos at, u16 type) {
        auto id = w.buildings.create(); auto& b = w.buildings[id];
        b.pos = at; b.type = type; b.town = town; b.serial = ++w.nextSerial;
        auto* t = w.map.tile(at); t->type = TileType::Building; t->index = id.value;
        w.towns[town].buildings.push_back(id);
        return id;
    }
    void road(TilePos a, TilePos b) {
        while (a != b) {
            const Dir d = a.x < b.x ? Dir::E : a.x > b.x ? Dir::W : a.y < b.y ? Dir::S : Dir::N;
            REQUIRE(connectRoad(w.map, a, d, kOwnerTown)); a = neighbour(a, d);
        }
    }
    PersonId person() {
        Person p; p.serial = ++w.nextSerial; p.home = w.buildings[home].ref(home); p.work = w.buildings[work].ref(work);
        p.town = town; p.carOwner = true; p.purpose = TripPurpose::Work;
        return w.people.create(p);
    }
    void depart(PersonId id) { people::travelAbstract(w, id, w.buildings[home].centre2(), w.buildings[work].centre2(), true); }
};
}
TEST_CASE("private cars: follow connected town roads around a block and complete a real citizen journey") {
    Streets f; const auto id = f.person(); f.depart(id);
    REQUIRE(f.w.people[id].state == PersonState::Driving);
    REQUIRE(f.w.people[id].carPath.size() > 30);
    CHECK_EQ(f.w.towns[f.town].thisMonth.drove, 1);
    bool turn = false;
    for (const auto tile : f.w.people[id].carPath) {
        CHECK(f.w.map.tile(tile)->type == TileType::Road);
        CHECK(towns::townAt(f.w, tile) == f.town);
        if (tile.y == 18) turn = true;
    }
    CHECK(turn);
    for (int i = 0; i < 500 && f.w.people[id].state == PersonState::Driving; ++i) {
        f.w.tick();
#ifdef TP_TEST_RENDERING
        const auto& p = f.w.people[id];
        if (p.state == PersonState::Driving && p.carStarted) {
            const auto pose = gfx::privateCarPose(f.w, p, .5);
            REQUIRE(pose);
            const auto tile = p.carPath[p.carIndex];
            CHECK(pose->pos.x >= tile.x && pose->pos.x <= tile.x + 1);
            CHECK(pose->pos.z >= tile.y && pose->pos.z <= tile.y + 1);
        }
#endif
    }
    CHECK(f.w.people[id].state == PersonState::AtWork);
    CHECK(f.w.people[id].carPath.empty());
    CHECK(f.w.privateCarTiles.empty());
}
TEST_CASE("private cars: restrict routes to one town and require a connected road") {
    Streets f;
    const auto from = f.w.buildings[f.home].centre2(), to = f.w.buildings[f.work].centre2();
    REQUIRE(privatecars::route(f.w, f.town, from, to));
    f.w.map.tile({18, 18})->road = 0;
    CHECK(!privatecars::route(f.w, f.town, from, to));
    f.w.map.tile({18, 18})->road = axisBits(Dir::E);
    f.w.towns[f.town].centre = {18, 0}; f.w.towns[f.town].radius = 14; // endpoints inside, detour outside
    CHECK(!privatecars::route(f.w, f.town, from, to));
    f.w.towns[f.town].centre = {18, 15}; f.w.towns[f.town].radius = 23;
    const auto other = f.w.towns.create(); f.w.towns[other].centre = {28, 10}; f.w.towns[other].radius = 3;
    CHECK(!privatecars::route(f.w, f.town, from, to));
    const auto id = f.person(); f.depart(id);
    CHECK(f.w.people[id].state != PersonState::Driving);
    CHECK(f.w.people[id].carPath.empty());
}
TEST_CASE("private cars: saved traffic resumes identically and older saves have no visible cars") {
    Streets f; const auto id = f.person(); f.depart(id);
    for (int i = 0; i < 88; ++i) f.w.tick();
    REQUIRE(f.w.people[id].carStarted);
    World copy;
    fromBytes(toBytes(f.w, kSaveVersion), copy, kSaveVersion);
    CHECK_EQ(copy.stateHash(), f.w.stateHash());
    CHECK(!copy.privateCarTiles.empty());
    World legacy;
    fromBytes(toBytes(f.w, 27), legacy, 27);
    CHECK(legacy.people[id].state == PersonState::Driving);
    CHECK(legacy.people[id].carPath.empty());
    CHECK(legacy.privateCarTiles.empty());
    for (int i = 0; i < 240; ++i) { f.w.tick(); copy.tick(); }
    CHECK_EQ(copy.stateHash(), f.w.stateHash());
}
TEST_CASE("private cars: queue behind each other and recover from removed streets") {
    Streets f; const auto a = f.person(), b = f.person(); f.depart(a); f.depart(b);
    for (int i = 0; i < kDriveOverheadTicks; ++i) f.w.tick();
    CHECK(f.w.people[a].carStarted);
    CHECK(!f.w.people[b].carStarted);
    for (int i = 0; i < 12; ++i) f.w.tick();
    CHECK(f.w.people[b].carStarted);
    CHECK(f.w.people[a].carIndex > f.w.people[b].carIndex);
    const auto index = f.w.people[a].carIndex;
    const auto next = f.w.people[a].carPath[index + 1];
    f.w.map.tile(next)->road = 0;
    for (int i = 0; i < 20; ++i) f.w.tick();
    CHECK(f.w.people[a].carPath.empty());
    CHECK(f.w.people[a].state != PersonState::Driving);
}
TEST_CASE("private cars: ownership increases over time and short trips remain walks") {
    int early = 0, late = 0;
    for (u32 serial = 1; serial <= 1000; ++serial) {
        const bool before = privatecars::ownsCar(serial, 1950), after = privatecars::ownsCar(serial, 2020);
        early += before; late += after; CHECK(!before || after);
    }
    CHECK(early > 70 && early < 180);
    CHECK(late > 650 && late < 820);
    Streets f;
    const auto id = f.person();
    people::travelAbstract(f.w, id, {17, 21}, {17, 29}, true);
    CHECK(f.w.people[id].state == PersonState::Walking);
    CHECK(f.w.people[id].carPath.empty());
}
TEST_CASE("private cars: an available local bus remains the first choice for a car owner") {
    Streets f;
    Command command; command.payload = CmdCreateCompany{"Town bus", 0};
    const auto created = executeCommand(f.w, command);
    REQUIRE(created.ok);
    command.company = CompanyId(static_cast<u8>(created.createdId));
    f.w.companies[command.company].money = 10'000'000;
    command.payload = CmdBuildRoadStop{{8, 12}, false};
    const auto first = executeCommand(f.w, command); REQUIRE(first.ok);
    command.payload = CmdBuildRoadStop{{28, 12}, false};
    const auto second = executeCommand(f.w, command); REQUIRE(second.ok);
    Vehicle bus; bus.model = 0; bus.owner = command.company; bus.stopped = false;
    bus.state = VehicleState::Driving; bus.tile = {8, 12};
    Order a, b; a.station = StationId(f.w.map.tile({8, 12})->index); b.station = StationId(f.w.map.tile({28, 12})->index); bus.orders = {a, b};
    f.w.vehicles.create(bus);
    passengers::markDirty(f.w); passengers::updateRouting(f.w);
    REQUIRE(f.w.routing.find(a.station, b.station));
    REQUIRE(walking::toStation(f.w, walking::endpoint(f.w, f.w.buildings[f.home].centre2()), a.station));
    REQUIRE(walking::fromStation(f.w, b.station, walking::endpoint(f.w, f.w.buildings[f.work].centre2())));
    passengers::TransitPlan plan;
    REQUIRE(passengers::planTrip(f.w, f.w.buildings[f.home].centre2(), f.w.buildings[f.work].centre2(), plan));
    const auto id = f.person();
    f.w.people[id].state = PersonState::AtHome;
    people::schedule(f.w, id, f.w.clock.tick + 1);
    ++f.w.clock.tick; people::process(f.w);
    CHECK(f.w.people[id].state == PersonState::ToStop);
    CHECK(f.w.people[id].carPath.empty());
}
