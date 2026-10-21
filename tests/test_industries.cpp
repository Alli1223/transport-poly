#include "test.hpp"

#include "core/serialize.hpp"
#include "sim/commands.hpp"
#include "sim/industries.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <cstdio>
#include <set>
#include <string_view>

using namespace tp;

namespace {

void explore(World& w, i32 radiusChunks) {
    for (i32 y = -radiusChunks; y <= radiusChunks; ++y)
        for (i32 x = -radiusChunks; x <= radiusChunks; ++x) w.activateChunk({x, y});
}

} // namespace

TEST_CASE("industries: countryside industries are founded as land is explored") {
    WorldSettings s;
    s.seed = 77;
    World w(s);
    explore(w, 8);
    std::set<u16> kinds;
    std::set<u8> levels;
    std::set<u8> sizes;
    int count = 0;
    for (const auto [id, b] : w.buildings)
        if (industries::isCountryside(b.type)) {
            ++count;
            kinds.insert(b.type);
            sizes.insert(b.size());
            CHECK(b.size() > b.info().size);
        }
    for (const Industry& industry : w.industries) levels.insert(industry.level);
    std::printf("industries: %d of %zu kinds\n", count, kinds.size());
    CHECK(count >= 6);
    CHECK(kinds.size() >= 4);
    CHECK(levels.size() >= 3);
    CHECK(sizes.size() >= 3);
}

namespace {

struct ChainWorld {
    World w;
    CompanyId company;
    ChainWorld() {
        company = w.companies.create();
        w.companies[company].thisMonth.month = static_cast<u8>(w.clock.date().month);
        w.companies[company].thisMonth.year = w.clock.date().year;
    }
    BuildingId building(u16 type, TilePos pos, TownId town = {}) {
        const BuildingId id = w.buildings.create();
        w.buildings[id].serial = ++w.nextSerial;
        w.buildings[id].type = type;
        w.buildings[id].pos = pos;
        w.buildings[id].town = town;
        return id;
    }
    StationId station(TilePos at) {
        const StationId id = w.stations.create();
        w.stations[id].sign = at;
        w.stations[id].tiles = {at};
        return id;
    }
    VehicleId lorry(u16 model, std::vector<StationId> stops) {
        const VehicleId id = w.vehicles.create();
        w.vehicles[id].model = model;
        w.vehicles[id].owner = company;
        w.vehicles[id].stopped = false;
        for (const StationId s : stops) {
            Order o;
            o.station = s;
            w.vehicles[id].orders.push_back(o);
        }
        return id;
    }
    const Industry& industry(BuildingId id) const {
        return *std::ranges::find_if(w.industries, [&](const Industry& i) { return i.building.id == id; });
    }
};

u16 modelNamed(std::string_view name) {
    const auto models = vehicleModels();
    for (usize i = 0; i < models.size(); ++i)
        if (models[i].name == name) return static_cast<u16>(i);
    return 0;
}

} // namespace

TEST_CASE("industries: logs go to the sawmill, planks to the town that needs them") {
    ChainWorld cw;
    World& w = cw.w;
    const TownId town = w.towns.create();
    w.towns[town].name = "Plankton";
    w.towns[town].needs = {CargoType::Planks};
    const BuildingId camp = cw.building(industries::kLoggingCamp, {0, 0});
    const BuildingId mill = cw.building(industries::kSawmill, {20, 0});
    const BuildingId shop = cw.building(5, {40, 0}, town);
    const StationId a = cw.station({0, 2}), b = cw.station({20, 2}), c = cw.station({40, 1});
    const u16 timber = modelNamed("Timber Lorry");
    REQUIRE(timber != 0);
    const VehicleId logs = cw.lorry(timber, {a, b});
    const VehicleId planks = cw.lorry(timber, {b, c});

    cargo::daily(w);
    REQUIRE(cargo::quantity(w.stations[a].cargoWaiting, CargoType::Logs) > 0);
    CHECK(cargo::exchangeAtStop(w, logs, a) > 0);
    const u32 carried = cargo::quantity(w.vehicles[logs].cargo, CargoType::Logs);
    CHECK_EQ(carried, 28u); // a timber lorry's flatbed
    w.vehicles[logs].orderIndex = 1;
    cargo::exchangeAtStop(w, logs, b);
    CHECK_EQ(cw.industry(mill).stock[static_cast<usize>(CargoType::Logs)], carried);
    CHECK_EQ(cw.industry(camp).exportedThisMonth, carried);
    CHECK(w.companies[cw.company].money > 0);

    // The sawmill turns the logs into planks and sends them to the town's shop.
    cargo::daily(w);
    CHECK_EQ(cw.industry(mill).stock[static_cast<usize>(CargoType::Logs)], 0u);
    REQUIRE(cargo::quantity(w.stations[b].cargoWaiting, CargoType::Planks) > 0);
    CHECK(cargo::exchangeAtStop(w, planks, b) > 0);
    w.vehicles[planks].orderIndex = 1;
    cargo::exchangeAtStop(w, planks, c);
    CHECK(w.towns[town].supplied[static_cast<usize>(CargoType::Planks)] > 0);
    CHECK(w.towns[town].goodsReceived > 0);
    CHECK(cw.industry(shop).delivered > 0);
    (void)camp;
}

TEST_CASE("industries: every recipe expands with sustained completed exports") {
    for (const IndustryRecipe& recipe : industryRecipes()) {
        ChainWorld cw;
        const BuildingId id = cw.building(recipe.building, {0, 0});
        Industry entry{cw.w.buildings[id].ref(id)};
        entry.level = 2;
        cw.w.industries.push_back(entry);
        for (int month = 0; month < 4; ++month) {
            cw.w.industries[0].exportedThisMonth = recipe.rate * 30;
            cargo::monthly(cw.w);
        }
        CHECK_EQ(cw.w.industries[0].level, 3u);
        CHECK_EQ(cw.w.industries[0].growthProgress, 0u);
        // Processing capacity rises too, while still consuming all required inputs.
        auto& grown = cw.w.industries[0];
        for (CargoType input : recipe.inputs)
            if (input != CargoType::None) grown.stock[static_cast<usize>(input)] = 1000;
        cargo::daily(cw.w);
        CHECK_EQ(grown.producedThisMonth, recipe.rate * 3u / 2u);
    }
}

TEST_CASE("industries: station dispatch and tiny exports cannot trigger expansion") {
    ChainWorld cw;
    const BuildingId id = cw.building(industries::kSheepFarm, {0, 0});
    Industry entry{cw.w.buildings[id].ref(id)};
    entry.level = 4;
    entry.growthProgress = 50;
    cw.w.industries.push_back(entry);
    for (int month = 0; month < 12; ++month) {
        auto& industry = cw.w.industries[0];
        industry.shippedThisMonth = 4000;
        industry.exportedThisMonth = 1;
        cargo::monthly(cw.w);
    }
    CHECK_EQ(cw.w.industries[0].level, 4u);
    CHECK_EQ(cw.w.industries[0].growthProgress, 0u);
    cw.w.industries[0].level = 8;
    cw.w.industries[0].exportedThisMonth = 100000;
    cargo::monthly(cw.w);
    CHECK_EQ(cw.w.industries[0].level, 8u);
}

TEST_CASE("towns: receiving passengers and goods increases growth and rolls over monthly") {
    World w;
    const TownId id = w.towns.create();
    Town& town = w.towns[id];
    town.population = 200;
    towns::growAll(w);
    const i32 baseline = town.growthPoints;
    town.growthPoints = 0;
    town.arrivalsLastMonth = 400;
    town.goodsReceivedLastMonth = 100;
    towns::growAll(w);
    CHECK(town.growthPoints > baseline);
    town.arrivals = 123;
    town.goodsReceived = 45;
    towns::monthly(w);
    CHECK_EQ(town.arrivalsLastMonth, 123u);
    CHECK_EQ(town.goodsReceivedLastMonth, 45u);
    CHECK_EQ(town.arrivals, 0u);
    CHECK_EQ(town.goodsReceived, 0u);
}

TEST_CASE("towns: completed transit visits benefit the destination town once") {
    ChainWorld cw;
    const TownId destination = cw.w.towns.create();
    cw.w.towns[destination].centre = {-20, -20};
    const BuildingId home = cw.building(0, {0, 0});
    const BuildingId shop = cw.building(5, {-20, -20}, destination);
    const PersonId person = cw.w.people.create();
    auto& visitor = cw.w.people[person];
    visitor.home = cw.w.buildings[home].ref(home);
    visitor.visit = cw.w.buildings[shop].ref(shop);
    visitor.purpose = TripPurpose::Visit;
    visitor.state = PersonState::FromStop;
    visitor.tripTo2 = cw.w.buildings[shop].centre2();
    people::schedule(cw.w, person, cw.w.clock.tick);
    people::process(cw.w);
    CHECK_EQ(cw.w.towns[destination].arrivals, 1u);
    people::process(cw.w);
    CHECK_EQ(cw.w.towns[destination].arrivals, 1u);
}

TEST_CASE("industries: growth progress, footprints and shipment provenance survive saves") {
    ChainWorld cw;
    const BuildingId id = cw.building(industries::kSheepFarm, {0, 0});
    cw.w.buildings[id].footprint = 6;
    Industry entry{cw.w.buildings[id].ref(id)};
    entry.level = 5;
    entry.growthProgress = 73;
    entry.exportedThisMonth = 120;
    cw.w.industries.push_back(entry);
    const StationId station = cw.station({0, 6});
    CargoBatch batch;
    batch.producer = entry.building;
    cw.w.stations[station].cargoWaiting.push_back(batch);
    World copy;
    fromBytes(toBytes(cw.w, kSaveVersion), copy, kSaveVersion);
    CHECK_EQ(copy.stateHash(), cw.w.stateHash());
    CHECK_EQ(copy.buildings[id].size(), 6u);
    CHECK_EQ(copy.industries[0].growthProgress, 73u);
    CHECK(copy.stations[station].cargoWaiting[0].producer == entry.building);
    World legacy;
    fromBytes(toBytes(cw.w, 26), legacy, 26);
    CHECK_EQ(legacy.buildings[id].size(), buildingType(industries::kSheepFarm).size);
    CHECK_EQ(legacy.industries[0].growthProgress, 0u);
}

TEST_CASE("industries: large plots have full catchment and demolish completely") {
    ChainWorld cw;
    cw.w.map.activate({0, 0});
    const BuildingId farm = towns::placeBuilding(cw.w, {}, industries::kSheepFarm, {4, 4}, Dir::S, 0, 6);
    const StationId station = cw.station({9, 9});
    cw.w.stations[station].catchment = 0;
    CHECK(cargo::covers(cw.w, station, cw.w.buildings[farm]));
    CHECK_EQ(cw.w.buildings[farm].centre2().x, 14);
    towns::removeBuilding(cw.w, farm);
    for (i32 y = 4; y < 10; ++y)
        for (i32 x = 4; x < 10; ++x) CHECK(cw.w.map.tile({x, y})->type == TileType::Clear);
}

TEST_CASE("industries: a steel mill needs both iron ore and coal") {
    ChainWorld cw;
    const BuildingId mill = cw.building(industries::kSteelMill, {0, 0});
    cargo::daily(cw.w);
    auto& stock = std::ranges::find_if(cw.w.industries, [&](const Industry& i) { return i.building.id == mill; })->stock;
    stock[static_cast<usize>(CargoType::IronOre)] = 50;
    cargo::daily(cw.w);
    CHECK_EQ(stock[static_cast<usize>(CargoType::Steel)], 0u);
    stock[static_cast<usize>(CargoType::Coal)] = 30;
    cargo::daily(cw.w);
    CHECK_EQ(stock[static_cast<usize>(CargoType::Steel)], 30u);
    CHECK_EQ(stock[static_cast<usize>(CargoType::IronOre)], 20u);
    CHECK_EQ(stock[static_cast<usize>(CargoType::Coal)], 0u);
}

TEST_CASE("freight: each class of wagon carries all of its cargo, sharing its space") {
    Vehicle v;
    v.model = 7;
    v.cars = {7, 19, modelNamed("Flat Wagon")};
    CHECK_EQ(v.cargoCapacity(CargoType::Grain), 50u);
    CHECK_EQ(v.cargoCapacity(CargoType::Stone), 50u);
    CHECK_EQ(v.cargoCapacity(CargoType::Coal), 50u);
    CHECK_EQ(v.cargoCapacity(CargoType::Planks), 48u);
    CHECK_EQ(v.cargoCapacity(CargoType::Steel), 48u);
    CHECK_EQ(v.cargoCapacity(CargoType::Milk), 0u);
    v.cargo.push_back({CargoType::Grain, 30});
    v.cargo.push_back({CargoType::Stone, 20});
    CHECK_EQ(cargo::classQuantity(v.cargo, CargoClass::Bulk), 50u);
    CHECK_EQ(v.carLoad(1), 50u);
    CHECK_EQ(v.carLoad(2), 0u);
}

TEST_CASE("towns: needs grow more elaborate as towns grow, and hold growth back") {
    World w;
    const TownId id = w.towns.create();
    Town& t = w.towns[id];
    t.name = "Needham";
    t.rng.seed(5, 0x707E);
    t.population = 100;
    towns::updateNeeds(w, t);
    REQUIRE(t.needs.size() == 1u);
    const auto tier1 = {CargoType::Food, CargoType::Planks, CargoType::Cheese};
    CHECK(std::ranges::find(tier1, t.needs[0]) != tier1.end());
    t.population = 1200;
    towns::updateNeeds(w, t);
    CHECK_EQ(t.needs.size(), 3u);
    for (usize i = 0; i < t.needs.size(); ++i)
        for (usize j = i + 1; j < t.needs.size(); ++j) CHECK(t.needs[i] != t.needs[j]);

    // Grown to the size of its next need without its needs met: it stops growing.
    t.population = 2600;
    t.growthPoints = 0;
    towns::growAll(w);
    CHECK_EQ(t.growthPoints, 0);
    for (const CargoType c : t.needs) t.suppliedLastMonth[static_cast<usize>(c)] = towns::needDemand(t);
    for (const CargoType c : t.needs) CHECK(towns::needMet(t, c));
    towns::growAll(w);
    CHECK(t.growthPoints > 0);
}

TEST_CASE("industries: saves from before industries keep without them") {
    World w;
    explore(w, 3);
    World old;
    fromBytes(toBytes(w, 20), old, 20);
    CHECK(!old.settings.industries);
    World copy;
    fromBytes(toBytes(w, kSaveVersion), copy, kSaveVersion);
    CHECK(copy.settings.industries);
    CHECK_EQ(copy.industries.size(), w.industries.size());
    CHECK_EQ(copy.stateHash(), w.stateHash());
}
