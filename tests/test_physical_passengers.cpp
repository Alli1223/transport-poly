#include "test.hpp"
#include "core/serialize.hpp"
#include "sim/world.hpp"
#ifdef TP_TEST_RENDERING
#include "render/person_pose.hpp"
#endif
using namespace tp;

TEST_CASE("passengers: all 75 real people alight, walk and reach their destination") {
    World w;
    w.activateChunk({0, 0});
    const BuildingId home = w.buildings.create(), work = w.buildings.create();
    w.buildings[home].serial = 1; w.buildings[home].pos = {8, 8};
    w.buildings[work].serial = 2; w.buildings[work].pos = {20, 16};
    const StationId a = w.stations.create(), b = w.stations.create();
    w.stations[a].sign = {5, 16}; w.stations[b].sign = {16, 16};
    Vehicle v; v.model = 7; v.cars = {7, 11, 11, 11}; v.state = VehicleState::Loading;
    v.tile = {16, 16}; v.exit = Dir::E;
    v.trail = {{{16, 16}, Dir::W, Dir::E}, {{15, 16}, Dir::W, Dir::E}, {{14, 16}, Dir::W, Dir::E}};
    v.progress = 250;
    Order stop; stop.station = b; v.orders = {stop};
    const VehicleId train = w.vehicles.create(v);
    std::vector<PersonId> manifest;
    for (int i = 0; i < 75; ++i) {
        Person p; p.serial = static_cast<u32>(100 + i);
        p.home = w.buildings[home].ref(home); p.work = w.buildings[work].ref(work);
        p.purpose = TripPurpose::Work; p.state = PersonState::Riding;
        p.vehicle = train; p.station = a; p.targetStation = b; p.finalStation = b;
        p.tripTo2 = w.buildings[work].centre2();
        const PersonId id = w.people.create(p);
        manifest.push_back(id); w.vehicles[train].passengers.push_back(id);
    }
    CHECK_EQ(w.vehicles[train].passengers.size(), 75u);
#ifdef TP_TEST_RENDERING
    int seated = 0;
    for (const auto id : manifest) {
        const auto pose = gfx::personPose(w, w.people[id]);
        seated += pose && pose->visible;
    }
    CHECK_EQ(seated, 75);
#endif
    CHECK_EQ(passengers::exchangeAtStop(w, train, b), 75);
    CHECK(w.vehicles[train].passengers.empty());
    int walking = 0;
    for (const auto id : manifest) {
        CHECK(w.people.contains(id));
        const auto& p = w.people[id];
        walking += p.state == PersonState::FromStop;
        CHECK_EQ(p.transitMotion, 2);
        CHECK(p.transitEnd < p.nextEvent);
    }
    CHECK_EQ(walking, 75);
    World saved;
    fromBytes(toBytes(w, kSaveVersion), saved, kSaveVersion);
#ifdef TP_TEST_RENDERING
    int shown = 0;
    w.clock.tick = 160;
    for (const auto id : manifest) {
        const auto pose = gfx::personPose(w, w.people[id], 0.5);
        shown += pose && pose->visible;
    }
    CHECK_EQ(shown, 75);
    w.clock.tick = 0;
#endif
    for (int i = 0; i < 700; ++i) {
        ++w.clock.tick; ++saved.clock.tick;
        people::process(w); people::process(saved);
    }
    CHECK_EQ(w.stateHash(), saved.stateHash());
    for (const auto id : manifest) {
        CHECK(w.people.contains(id));
        CHECK(w.people[id].state == PersonState::AtWork);
    }
}
