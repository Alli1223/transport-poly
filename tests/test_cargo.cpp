#include "test.hpp"
#include "core/serialize.hpp"
#include "sim/world.hpp"
#include "sim/commands.hpp"
using namespace tp;

TEST_CASE("freight: farm factory shop chain, payment and save continuation") {
    World w;
    const auto company = w.companies.create();
    w.companies[company].thisMonth.month = static_cast<u8>(w.clock.date().month);
    w.companies[company].thisMonth.year = w.clock.date().year;
    const auto farm = w.buildings.create(), factory = w.buildings.create(), shop = w.buildings.create();
    w.buildings[farm].serial = 1; w.buildings[farm].type = 12; w.buildings[farm].pos = {0, 0};
    w.buildings[factory].serial = 2; w.buildings[factory].type = 11; w.buildings[factory].pos = {20, 0};
    w.buildings[shop].serial = 3; w.buildings[shop].type = 5; w.buildings[shop].pos = {40, 0};
    const auto a = w.stations.create(), b = w.stations.create(), c = w.stations.create();
    w.stations[a].sign = {0, 1}; w.stations[a].tiles = {{0, 1}};
    w.stations[b].sign = {20, 1}; w.stations[b].tiles = {{20, 1}};
    w.stations[c].sign = {40, 1}; w.stations[c].tiles = {{40, 1}};
    const auto grain = w.vehicles.create(), food = w.vehicles.create();
    w.vehicles[grain].model = 16; w.vehicles[grain].owner = company; w.vehicles[grain].stopped = false;
    w.vehicles[food].model = 17; w.vehicles[food].owner = company; w.vehicles[food].stopped = false;
    Order oa; oa.station = a; Order ob; ob.station = b; Order oc; oc.station = c;
    w.vehicles[grain].orders = {oa, ob}; w.vehicles[food].orders = {ob, oc};
    cargo::daily(w);
    REQUIRE(!w.stations[a].cargoWaiting.empty());
    CHECK_EQ(cargo::exchangeAtStop(w, grain, a), 30);
    CHECK_EQ(cargo::quantity(w.vehicles[grain].cargo, CargoType::Grain), 30u);
    CHECK_EQ(w.vehicles[grain].cargoLoaded[0], 30u);
    CHECK_EQ(w.vehicles[grain].cargoUnloaded[0], 0u);
    CHECK_EQ(w.vehicles[grain].capacity(), 0u);
    w.vehicles[grain].orderIndex = 1;
    CHECK_EQ(cargo::exchangeAtStop(w, grain, b), 30);
    CHECK_EQ(w.vehicles[grain].cargoUnloaded[0], 30u);
    CHECK_EQ(w.vehicles[grain].cargoLoaded[0], 0u);
    CHECK(w.companies[company].money > 0);
    cargo::daily(w);
    CHECK_EQ(cargo::exchangeAtStop(w, food, b), 25);
    w.vehicles[food].orderIndex = 1;
    CHECK_EQ(cargo::exchangeAtStop(w, food, c), 25);
    CHECK_EQ(w.industries[2].delivered, 25u);
    World loaded;
    fromBytes(toBytes(w, kSaveVersion), loaded, kSaveVersion);
    CHECK_EQ(w.stateHash(), loaded.stateHash());
    for (int day = 0; day < 4; ++day) { cargo::daily(w); cargo::daily(loaded); }
    CHECK_EQ(w.stateHash(), loaded.stateHash());
}

TEST_CASE("freight: mixed consists separate seats and cargo capacities") {
    Vehicle v; v.model = 7; v.cars = {7, 11, 19, 20, 21};
    CHECK_EQ(v.capacity(), 36u);
    CHECK_EQ(v.cargoCapacity(CargoType::Grain), 50u);
    CHECK_EQ(v.cargoCapacity(CargoType::Food), 40u);
    CHECK_EQ(v.cargoCapacity(CargoType::Goods), 45u);
    CHECK(!v.full());
    v.passengers.resize(36);
    for (usize c = 0; c < kCargoTypes; ++c) {
        CargoBatch b; b.type = static_cast<CargoType>(c); b.quantity = v.cargoCapacity(b.type); v.cargo.push_back(b);
    }
    CHECK(v.full());
    CHECK_EQ(v.carLoad(0), 0u);
    CHECK_EQ(v.carLoad(1), 36u);
    CHECK_EQ(v.carLoad(2), 50u);
    CHECK_EQ(v.carLoad(3), 40u);
    CHECK_EQ(v.carLoad(4), 45u);
}

TEST_CASE("freight: disconnected and stopped services cannot dispatch") {
    World w;
    const auto a = w.stations.create(), b = w.stations.create();
    const auto vid = w.vehicles.create(); auto& v = w.vehicles[vid];
    v.model = 16;
    Order oa; oa.station = a; Order ob; ob.station = b; v.orders = {oa, ob};
    CHECK(!cargo::nextHop(w, a, b, CargoType::Grain).valid());
    v.stopped = false;
    CHECK(cargo::nextHop(w, a, b, CargoType::Grain) == b);
    CHECK(!cargo::nextHop(w, a, b, CargoType::Food).valid());
    passengers::updateRouting(w);
    CHECK(w.routing.routes.empty());
    v.orders[0].flags = kOrderNoLoading;
    CHECK(!cargo::nextHop(w, a, b, CargoType::Grain).valid());
    v.orders[0].flags = kOrderUnloadOnly;
    CHECK(!cargo::nextHop(w, a, b, CargoType::Grain).valid());
}

TEST_CASE("freight: transfers preserve quantities and order modes prevent loading") {
    World w;
    const auto a = w.stations.create(), b = w.stations.create(), c = w.stations.create();
    const auto truck = w.vehicles.create(), onward = w.vehicles.create();
    Order oa; oa.station = a; Order ob; ob.station = b; Order oc; oc.station = c;
    auto& v = w.vehicles[truck]; v.model = 16; v.stopped = false; v.orders = {oa,ob};
    auto& t = w.vehicles[onward]; t.model = 16; t.stopped = false; t.orders = {ob,oc};
    CargoBatch batch; batch.quantity = 50; batch.origin = a; batch.destination = c;
    w.stations[a].cargoWaiting.push_back(batch);
    CHECK_EQ(cargo::exchangeAtStop(w,truck,a), 30);
    CHECK_EQ(cargo::quantity(w.stations[a].cargoWaiting,CargoType::Grain), 20u);
    v.orderIndex = 1;
    CHECK_EQ(cargo::exchangeAtStop(w,truck,b), 30);
    CHECK(v.cargo.empty());
    CHECK_EQ(cargo::quantity(w.stations[b].cargoWaiting,CargoType::Grain), 30u);
    t.orders[0].flags = kOrderNoLoading;
    CHECK_EQ(cargo::exchangeAtStop(w,onward,b), 0);
    t.orders[0].flags = 0;
    CHECK_EQ(cargo::exchangeAtStop(w,onward,b), 30);
    CHECK(w.stations[b].cargoWaiting.empty());
    CHECK_EQ(cargo::quantity(t.cargo,CargoType::Grain), 30u);
    CHECK_EQ(t.cargo.front().origin.value, a.value);
    CHECK_EQ(t.cargo.front().destination.value, c.value);
}

TEST_CASE("freight: pre-freight saves load with empty inventories and AI disabled") {
    WorldSettings s; s.aiCompetitors = 3;
    World w(s), old;
    w.industries.push_back({});
    const auto v = w.vehicles.create();
    w.vehicles[v].cargo.push_back({});
    fromBytes(toBytes(w, 16), old, 16);
    CHECK_EQ(old.settings.aiCompetitors, 0);
    CHECK(old.industries.empty());
    CHECK(old.vehicles[v].cargo.empty());
    CHECK_EQ(old.vehicles[v].loadingStartTick, old.clock.tick);
}

TEST_CASE("freight: loaded wagons cannot be removed below the actual cargo load") {
    World w;
    const auto co = w.companies.create();
    const auto id = w.vehicles.create(); auto& v = w.vehicles[id];
    v.owner = co; v.model = 7; v.cars = {7,19,19};
    CargoBatch batch; batch.quantity = 75; v.cargo.push_back(batch);
    Command command; command.company = co; command.payload = CmdRemoveCarriage{id,2};
    CHECK(!executeCommand(w,command).ok);
    CHECK_EQ(v.cars.size(), 3u);
    v.cargo[0].quantity = 25;
    CHECK(executeCommand(w,command).ok);
    CHECK_EQ(v.cars.size(), 2u);
    CHECK_EQ(v.carLoad(1), 25u);
}
