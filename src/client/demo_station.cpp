#include "client/demo_station.hpp"
#include "core/log.hpp"
#include "sim/commands.hpp"

namespace tp {
StationId buildDemoStation(World& world, CompanyId company) {
    WorldSettings settings = world.settings;
    settings.aiCompetitors = 0;
    World w(settings);
    Command create;
    create.payload = CmdCreateCompany{"Station demonstration", 0xFF};
    const auto created = executeCommand(w, create);
    if (!created.ok || created.createdId != company.value) return {};
    w.companies[company].money = 50'000'000;
    for (i32 y = 0; y < 2; ++y)
        for (i32 x = 0; x < 2; ++x) w.map.activate({x, y});
    for (i32 y = 1; y < 64; ++y)
        for (i32 x = 1; x < 64; ++x) {
            w.map.setCornerHeight(x, y, 4);
            *w.map.tile({x, y}) = Tile{};
            w.map.touch({x, y});
        }
    const auto run = [&](CommandPayload payload) {
        Command c;
        c.company = company;
        c.payload = std::move(payload);
        const auto result = executeCommand(w, c);
        if (!result.ok) log::warn("Station demo: {}", result.error);
        return result;
    };
    if (!run(CmdBuildRoad{{8, 28}, {58, 28}}).ok) return {};
    TrackAnchor a;
    a.pos = tileCentre({18, 20});
    const auto built = run(CmdBuildTrackStation{a, tileCentre({30, 20})});
    if (!built.ok) return {};
    const StationId first(built.createdId);
    const auto edge = track::platformsOf(w, first).front().front();
    if (!run(CmdAddPlatform{first, edge, 1, 12}).ok) return {};
    const auto ends = track::platformEnds(w, edge);
    TrackAnchor end;
    end.node = ends.back().node;
    end.pos = w.trackNodes[end.node].pos;
    TrackAnchor next;
    next.pos = tileCentre({44, 20});
    if (!run(CmdBuildTrack{end, next}).ok) return {};
    next.node = track::nodeNear(w, next.pos, 16);
    const auto second = run(CmdBuildTrackStation{next, tileCentre({54, 20})});
    if (!second.ok) return {};
    if (!run(CmdStationEntrance{first, {22, 28}}).ok ||
        !run(CmdStationEntrance{StationId(second.createdId), {44, 28}}).ok)
        return {};
    const auto depot = run(CmdBuildTrackDepot{ends.front().node});
    if (!depot.ok) return {};
    const auto buy = run(CmdBuyVehicle{DepotId(depot.createdId), 7});
    if (!buy.ok) return {};
    const VehicleId train(buy.createdId);
    run(CmdAddCarriage{train, 11});
    run(CmdAddCarriage{train, 11});
    Order oa;
    oa.station = first;
    Order ob;
    ob.station = StationId(second.createdId);
    run(CmdSetOrders{train, {oa, ob}});
    run(CmdStartStopVehicle{train});
    const auto building = [&](TilePos p, u16 type, Dir facing) {
        const auto id = w.buildings.create();
        auto& b = w.buildings[id];
        b.pos = p;
        b.type = type;
        b.facing = facing;
        b.serial = id.value + 1;
        auto& tile = *w.map.tile(p);
        tile.type = TileType::Building;
        tile.index = id.value;
        w.map.touch(p);
        return id;
    };
    const auto home = building({20, 27}, 3, Dir::S), work = building({46, 27}, 8, Dir::S),
               shop = building({22, 29}, 5, Dir::N);
    w.clock.tick = 7 * kTicksPerHour;
    for (u32 i = 0; i < 28; ++i) {
        Person p;
        p.serial = i + 1;
        p.nameSeed = i + 1;
        p.home = w.buildings[home].ref(home);
        p.work = w.buildings[work].ref(work);
        p.shop = w.buildings[shop].ref(shop);
        p.purpose = i < 22 ? TripPurpose::Work : TripPurpose::Shop;
        const auto id = w.people.create(p);
        people::schedule(w, id, w.clock.tick + i * 8);
    }
    w.buildings[home].residents = 28;
    w.buildings[work].workers = 22;
    w.buildings[shop].customers = 28;
    passengers::updateRouting(w);
    world = std::move(w);
    return first;
}
void buildPassengerShowcase(World& world, CompanyId company) {
    World w(world.settings);
    Command create;
    create.payload = CmdCreateCompany{"Passenger windows", 0xFF};
    executeCommand(w, create);
    w.companies[company].money = 50'000'000;
    for (i32 y = 0; y < 2; ++y)
        for (i32 x = 0; x < 2; ++x) w.map.activate({x, y});
    for (i32 y = 1; y < 64; ++y)
        for (i32 x = 1; x < 64; ++x) {
            w.map.setCornerHeight(x, y, 4);
            *w.map.tile({x, y}) = Tile{};
            w.map.touch({x, y});
        }
    const auto run = [&](CommandPayload payload) {
        Command c;
        c.company = company;
        c.payload = std::move(payload);
        return executeCommand(w, c);
    };
    TrackAnchor a, b;
    a.pos = tileCentre({10, 20});
    b.pos = tileCentre({50, 20});
    run(CmdBuildTrack{a, b});
    run(CmdBuildRoad{{40, 22}, {52, 22}});
    run(CmdBuildRoad{{40, 22}, {52, 22}, true});
    run(CmdBuildRoad{{46, 22}, {46, 27}});
    run(CmdBuildRoad{{46, 22}, {46, 27}, true});
    Vehicle train;
    train.owner = company;
    train.model = 7;
    train.cars = {7, 11, 32, 33, 35, 37};
    train.freeTrack = true;
    train.state = VehicleState::Driving;
    train.stopped = true;
    std::vector<TrackEdgeId> edges;
    for (const auto [id, edge] : w.trackEdges) edges.push_back(id);
    for (auto it = edges.rbegin(); it != edges.rend(); ++it) train.runs.push_back({*it, true});
    if (!train.runs.empty()) train.progress = w.trackEdges[train.runs.front().edge].length - 2 * kTrackUnits;
    train.tile = {48, 20};
    Vehicle tram;
    tram.owner = company;
    tram.model = 6;
    tram.state = VehicleState::Driving;
    tram.stopped = true;
    tram.tile = {46, 22};
    tram.entry = Dir::W;
    tram.exit = Dir::S;
    tram.progress = pathLength(tram.entry, tram.exit) / 2;
    tram.trail = {{{45, 22}, Dir::W, Dir::E}, {{44, 22}, Dir::W, Dir::E}};
    for (const auto id : {w.vehicles.create(train), w.vehicles.create(tram)}) {
        const auto capacity = w.vehicles[id].capacity();
        for (u32 i = 0; i < capacity; ++i) {
            Person p;
            p.serial = ++w.nextSerial;
            p.nameSeed = p.serial * 2654435761u;
            p.state = PersonState::Riding;
            p.vehicle = id;
            w.vehicles[id].passengers.push_back(w.people.create(p));
        }
    }
    world = std::move(w);
}
} // namespace tp
