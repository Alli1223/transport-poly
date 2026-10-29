#include "core/serialize.hpp"
#include "sim/commands.hpp"
#include "sim/world.hpp"
#include "test.hpp"
#include <algorithm>
#include <set>

using namespace tp;
namespace {
struct Platforms {
    World w;
    CompanyId company;
    StationId station, east, west;
    std::vector<std::vector<TrackEdgeId>> platforms;
    BuildingId home;
    Platforms() {
        for (i32 y = 0; y < 2; ++y)
            for (i32 x = 0; x < 2; ++x) w.map.activate({x, y});
        for (i32 y = 1; y < 64; ++y)
            for (i32 x = 1; x < 64; ++x) {
                w.map.setCornerHeight(x, y, 4);
                *w.map.tile({x, y}) = Tile{};
            }
        Command create;
        create.payload = CmdCreateCompany{"Platforms", 0xFF};
        company = CompanyId(static_cast<u8>(executeCommand(w, create).createdId));
        w.companies[company].money = 50'000'000;
        TrackAnchor a;
        a.pos = tileCentre({10, 20});
        const auto built = run(CmdBuildTrackStation{a, tileCentre({18, 20})});
        REQUIRE(built.ok);
        station = StationId(built.createdId);
        const auto edge = track::platformsOf(w, station).front().front();
        REQUIRE(run(CmdAddPlatform{station, edge, 1, 8}).ok);
        platforms = track::platformsOf(w, station);
        east = w.stations.create();
        west = w.stations.create();
    }
    CommandResult run(CommandPayload payload) {
        Command c;
        c.company = company;
        c.payload = std::move(payload);
        return executeCommand(w, c);
    }
    VehicleId service(StationId destination, usize platform) {
        Vehicle v;
        v.model = 7;
        v.cars = {7, 11};
        v.owner = company;
        v.freeTrack = true;
        v.stopped = false;
        v.state = VehicleState::Driving;
        Order a;
        a.station = station;
        a.flags = kOrderPlatform;
        a.platform = track::platformPoint(w, platforms[platform]);
        Order b;
        b.station = destination;
        v.orders = {a, b};
        return w.vehicles.create(v);
    }
    std::vector<PersonId> queue(StationId destination, usize count) {
        std::vector<PersonId> out;
        const auto point = offset(track::platformPoint(w, platforms[0]), {0, kDirOne}, 200);
        // A home to come from: people without one move away when they next reconsider.
        if (!w.buildings.contains(home)) {
            home = w.buildings.create();
            w.buildings[home].serial = ++w.nextSerial;
            w.buildings[home].pos = {40, 40};
        }
        for (usize i = 0; i < count; ++i) {
            Person p;
            p.home = w.buildings[home].ref(home);
            p.serial = static_cast<u32>(w.people.size() + 1);
            p.state = PersonState::Waiting;
            p.station = station;
            p.targetStation = destination;
            p.finalStation = destination;
            p.walkPath = {{point}};
            const auto id = w.people.create(p);
            out.push_back(id);
            w.stations[station].waiting[destination].push_back(id);
            people::schedule(w, id, 10'000);
        }
        return out;
    }
    void settle() {
        for (const auto [id, p] : w.people) w.clock.tick = std::max(w.clock.tick, p.walkEnd);
    }
};
} // namespace

TEST_CASE("platform waiting: people scatter across both sides of their departure platforms") {
    Platforms f;
    f.service(f.east, 0);
    f.service(f.west, 1);
    const auto east = f.queue(f.east, 24), west = f.queue(f.west, 18);
    passengers::positionWaiting(f.w, f.station);
    for (const auto& group : {east, west}) {
        const int expected = group.front() == east.front() ? 1 : 2;
        std::vector<i32> positions;
        std::set<std::pair<i32, i32>> unique;
        std::set<i32> across;
        for (const auto id : group) {
            const auto& p = f.w.people[id];
            REQUIRE(!p.walkPath.empty());
            const auto at = p.walkPath.back().p;
            CHECK_EQ(track::platformNumber(f.w, f.station, at), expected);
            positions.push_back(at.x);
            unique.emplace(at.x, at.z);
            across.insert(at.z);
        }
        std::ranges::sort(positions);
        CHECK_EQ(unique.size(), group.size());
        CHECK(positions.back() - positions.front() > 5 * kTrackUnits);
        CHECK(across.size() > 6);
        const auto centre = track::platformPoint(f.w, f.platforms[expected-1]);
        CHECK(*across.begin() < centre.z - 100);
        CHECK(*across.rbegin() > centre.z + 100);
    }
    f.settle();
    const auto before = f.w.stateHash();
    passengers::positionWaiting(f.w, f.station);
    CHECK_EQ(before, f.w.stateHash());
    std::vector<std::vector<WalkPoint>> settledPaths;
    for (const auto id : east) settledPaths.push_back(f.w.people[id].walkPath);
    f.queue(f.east, 8);
    passengers::positionWaiting(f.w, f.station);
    for (usize i = 0; i < east.size(); ++i) CHECK(f.w.people[east[i]].walkPath == settledPaths[i]);
}

TEST_CASE("platform waiting: reserved arrival track overrides a provisional platform") {
    Platforms f;
    const auto train = f.service(f.east, 0);
    auto& v = f.w.vehicles[train];
    // A head on a plain approach with platform two reserved ahead (Any free platform).
    TrackAnchor from;
    const auto ends = track::platformEnds(f.w, f.platforms[1].front());
    from.node = ends.back().node;
    from.pos = f.w.trackNodes[from.node].pos;
    TrackAnchor to;
    to.pos = offset(from.pos, ends.back().outward, 4 * kTrackUnits);
    const auto built = f.run(CmdBuildTrack{from, to});
    REQUIRE(built.ok);
    TrackEdgeId approach;
    for (const auto [id, e] : f.w.trackEdges)
        if (e.kind == TrackKind::Plain) approach = id;
    REQUIRE(approach.valid());
    v.orders[0].flags = 0;
    v.runs = {{approach, false}};
    v.runsAhead = {{f.platforms[1].front(), false}};
    const auto people = f.queue(f.east, 12);
    const auto before = f.w.stateHash();
    CHECK(tracktrains::passengerPlatform(f.w, train, v.orders[0]) == f.platforms[1].front());
    CHECK_EQ(before, f.w.stateHash());
    passengers::positionWaiting(f.w, f.station);
    for (const auto id : people)
        CHECK_EQ(track::platformNumber(f.w, f.station, f.w.people[id].walkPath.back().p), 2);
}

TEST_CASE(
    "platform waiting: reassigned people board only after reaching the correct platform") {
    Platforms f;
    const auto train = f.service(f.east, 0);
    const auto passengers = f.queue(f.east, 10);
    passengers::positionWaiting(f.w, f.station);
    f.settle();
    auto& v = f.w.vehicles[train];
    v.orders[0].platform = track::platformPoint(f.w, f.platforms[1]);
    v.runs = {{f.platforms[1].front(), true}};
    v.progress = f.w.trackEdges[v.runs.front().edge].length - 6;
    v.state = VehicleState::Loading;
    v.loadingStartTick = f.w.clock.tick;
    v.loadingUntil = f.w.clock.tick + 2000;
    people::schedule(f.w, passengers.front(), f.w.clock.tick + 1);
    CHECK_EQ(passengers::exchangeAtStop(f.w, train, f.station), 0);
    CHECK(v.passengers.empty());
    bool elevated = false;
    for (const auto id : passengers) {
        const auto& p = f.w.people[id];
        CHECK(p.state == PersonState::Waiting);
        CHECK_EQ(track::platformNumber(f.w, f.station, p.walkPath.back().p), 2);
        for (const auto point : p.walkPath) elevated = elevated || point.height > 64;
    }
    CHECK(elevated);
    CHECK(f.w.people[passengers.front()].nextEvent > f.w.people[passengers.front()].walkEnd);
    World saved;
    fromBytes(toBytes(f.w, kSaveVersion), saved, kSaveVersion);
    f.settle();
    saved.clock.tick = f.w.clock.tick;
    vehicles::tickAll(f.w);
    vehicles::tickAll(saved);
    CHECK_EQ(f.w.stateHash(), saved.stateHash());
    CHECK_EQ(v.passengers.size(), passengers.size());
    CHECK(v.state == VehicleState::Loading);
    CHECK(v.loadingUntil > f.w.clock.tick);
    for (const auto id : passengers) CHECK(f.w.people[id].state == PersonState::Riding);
}

TEST_CASE("platform waiting: a transferring passenger does not block people already ready to board") {
    Platforms f;
    const auto train = f.service(f.east, 0);
    const auto people = f.queue(f.east, 2);
    passengers::positionWaiting(f.w, f.station);
    f.settle();
    auto& v = f.w.vehicles[train];
    v.state = VehicleState::Loading;
    v.runs = {{f.platforms[0].front(), true}};
    v.progress = f.w.trackEdges[v.runs.front().edge].length - 6;
    auto& transfer = f.w.people[people[0]];
    const auto point = transfer.walkPath.back().p;
    transfer.walkPath = {{point, 128}, {point, 64}};
    transfer.walkBegin = f.w.clock.tick;
    transfer.walkEnd = f.w.clock.tick + 100;
    CHECK_EQ(passengers::exchangeAtStop(f.w, train, f.station), 1);
    CHECK(f.w.people[people[0]].state == PersonState::Waiting);
    CHECK(f.w.people[people[1]].state == PersonState::Riding);
}

TEST_CASE("platform waiting: a real train waits for the concourse walk then carries the whole queue") {
    Platforms f;
    const auto ends = track::platformEnds(f.w, f.platforms[1].front());
    TrackAnchor from;
    from.node = ends.back().node;
    from.pos = f.w.trackNodes[from.node].pos;
    TrackAnchor to;
    to.pos = offset(from.pos, ends.back().outward, 12 * kTrackUnits);
    REQUIRE(f.run(CmdBuildTrack{from, to}).ok);
    to.node = track::nodeNear(f.w, to.pos, 16);
    const auto built = f.run(CmdBuildTrackStation{to, offset(to.pos, ends.back().outward, 8 * kTrackUnits)});
    REQUIRE(built.ok);
    const auto destination = StationId(built.createdId);
    const auto depot = f.run(CmdBuildTrackDepot{ends.front().node});
    REQUIRE(depot.ok);
    const auto bought = f.run(CmdBuyVehicle{DepotId(depot.createdId), 7});
    REQUIRE(bought.ok);
    const auto train = VehicleId(bought.createdId);
    REQUIRE(f.run(CmdAddCarriage{train, 11}).ok);
    Order a;
    a.station = f.station;
    a.flags = kOrderPlatform;
    a.platform = track::platformPoint(f.w, f.platforms[1]);
    Order b;
    b.station = destination;
    REQUIRE(f.run(CmdSetOrders{train, {a, b}}).ok);
    const auto queue = f.queue(destination, 20);
    REQUIRE(f.run(CmdStartStopVehicle{train}).ok);
    for (int tick = 0; tick < 80; ++tick) f.w.tick();
    World saved;
    fromBytes(toBytes(f.w, kSaveVersion), saved, kSaveVersion);
    for (int tick = 0; tick < 1600; ++tick) {
        f.w.tick();
        saved.tick();
    }
    CHECK_EQ(f.w.stateHash(), saved.stateHash());
    CHECK_EQ(f.w.stations[f.station].boardedThisMonth, static_cast<i32>(queue.size()));
    CHECK(f.w.stations[destination].lastVisitTick > 0);
    CHECK(!f.w.vehicles[train].lost);
}

TEST_CASE("platform waiting: carriage doors board concurrently and keep seats through partial unloading") {
    Platforms f;
    const auto train = f.service(f.east, 0);
    auto& v = f.w.vehicles[train];
    v.cars = {7, 11, 11};
    const auto people = f.queue(f.east, 48);
    passengers::positionWaiting(f.w, f.station);
    f.settle();
    v.state = VehicleState::Loading;
    v.runs = {{f.platforms[0].front(), true}};
    v.progress = f.w.trackEdges[v.runs.front().edge].length - 6;
    const auto now = f.w.clock.tick;
    REQUIRE(passengers::exchangeAtStop(f.w, train, f.station) == 48);
    CHECK(v.carLoad(1) > 0); CHECK(v.carLoad(2) > 0);
    std::set<u16> seats(v.passengerSeats.begin(), v.passengerSeats.end());
    CHECK_EQ(seats.size(), people.size());
    std::set<std::pair<i32,i32>> doors;
    for (const auto id : people) {
        const auto& p = f.w.people[id];
        CHECK_EQ(p.transitBegin, now);
        CHECK_EQ(p.walkBegin, now);
        REQUIRE(p.walkPath.size() >= 2);
        doors.emplace(p.walkPath.back().p.x, p.walkPath.back().p.z);
    }
    CHECK(doors.size() >= 4);
    const auto originalSeats = v.passengerSeats;
    f.w.clock.tick = v.loadingUntil;
    for (usize i = 0; i < 12; ++i) f.w.people[v.passengers[i]].targetStation = f.station;
    REQUIRE(passengers::exchangeAtStop(f.w, train, f.station) == 12);
    CHECK_EQ(v.passengers.size(), 36u);
    for (usize i = 0; i < v.passengerSeats.size(); ++i) CHECK_EQ(v.passengerSeats[i], originalSeats[i+12]);
    for (usize i = 0; i < 12; ++i) CHECK_EQ(f.w.people[people[i]].transitBegin, f.w.clock.tick);
    World saved;
    fromBytes(toBytes(f.w, kSaveVersion), saved, kSaveVersion);
    CHECK_EQ(f.w.stateHash(), saved.stateHash());
    CHECK(saved.vehicles[train].passengerSeats == v.passengerSeats);
}

TEST_CASE("platform waiting: loading progresses while station walkers do not extend the stop") {
    Platforms f;
    const auto train = f.service(f.east, 0);
    auto& v = f.w.vehicles[train];
    const auto ready = f.queue(f.east, 12);
    passengers::positionWaiting(f.w, f.station);
    f.settle();
    v.state = VehicleState::Loading;
    v.runs = {{f.platforms[0].front(), true}};
    v.progress = f.w.trackEdges[v.runs.front().edge].length - 6;
    v.loadingUntil = f.w.clock.tick + 40;
    v.loadingStartTick = f.w.clock.tick;
    // These passengers are still on the footbridge, not at the train's platform.
    const auto approaching = f.queue(f.east, 8);
    const auto location = track::platformPoint(f.w, f.platforms[0]);
    for (const auto id : approaching) {
        auto& p = f.w.people[id];
        p.walkPath = {{location, 128}, {location, 64}};
        p.walkBegin = f.w.clock.tick; p.walkEnd = f.w.clock.tick + 10'000;
    }
    // People already walking to scattered positions on the platform may start boarding now.
    auto& walkingOnPlatform = f.w.people[ready.front()];
    const auto readyPoint = walkingOnPlatform.walkPath.back();
    walkingOnPlatform.walkPath = {readyPoint, {{readyPoint.p.x + 20, readyPoint.p.z}, readyPoint.height}};
    walkingOnPlatform.walkBegin = f.w.clock.tick;
    walkingOnPlatform.walkEnd = f.w.clock.tick + 100;
    vehicles::tickAll(f.w);
    CHECK_EQ(v.passengers.size(), ready.size());
    CHECK(v.loadingUntil < f.w.people[approaching.front()].walkEnd);
    const auto departure = v.loadingUntil;
    for (const auto id : approaching) CHECK(f.w.people[id].state == PersonState::Waiting);
    for (f.w.clock.tick += 1; f.w.clock.tick <= departure && v.state == VehicleState::Loading; ++f.w.clock.tick)
        vehicles::tickAll(f.w);
    CHECK(v.state == VehicleState::Driving);
    CHECK_EQ(f.w.stations[f.station].waitingCount(), approaching.size());
}

TEST_CASE("platform waiting: every waiting spot is on the platform, either side of the track") {
    Platforms f;
    const auto destination = f.east;
    const auto queue = f.queue(destination, 64);
    f.service(destination, 0);
    passengers::positionWaiting(f.w, f.station);
    const TPoint middle = track::platformPoint(f.w, f.platforms[0]);
    int left = 0, right = 0;
    for (const PersonId id : queue) {
        const Person& p = f.w.people[id];
        REQUIRE(!p.walkPath.empty());
        const TPoint spot = p.walkPath.back().p;
        // On the platform: within a few tiles of its middle, never off across the map.
        CHECK(distance(spot, middle) < 6 * kTrackUnits);
        CHECK(p.walkEnd - p.walkBegin < 4 * kTicksPerHour);
        (spot.z < middle.z ? left : right) += 1;
    }
    CHECK(left > 0);
    CHECK(right > 0);
}
