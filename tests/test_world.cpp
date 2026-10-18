#include "test.hpp"

#include "core/serialize.hpp"
#include "net/server.hpp"
#include "net/session.hpp"
#include "sim/commands.hpp"
#include "sim/world.hpp"
#include "sim/prices.hpp"

#include <memory>

using namespace tp;

namespace {

Command makeCommand(CompanyId company, CommandPayload payload) {
    Command c;
    c.company = company;
    c.payload = std::move(payload);
    return c;
}

CompanyId createCompany(World& w) {
    const CommandResult r = executeCommand(w, makeCommand(CompanyId{}, CmdCreateCompany{"Test Co", 0xFF}));
    REQUIRE(r.ok);
    return CompanyId(static_cast<u8>(r.createdId));
}

} // namespace

TEST_CASE("world: companies start with the initial loan as cash") {
    World w;
    const CompanyId id = createCompany(w);
    const Company& co = w.companies[id];
    CHECK_EQ(co.money, w.settings.initialLoan);
    CHECK_EQ(co.loan, w.settings.initialLoan);
    CHECK_EQ(co.name, std::string("Test Co"));
}

TEST_CASE("world: loan rules") {
    World w;
    const CompanyId id = createCompany(w);
    CHECK(executeCommand(w, makeCommand(id, CmdSetLoan{100'000})).ok);
    CHECK_EQ(w.companies[id].loan, w.settings.initialLoan + 100'000);
    CHECK(!executeCommand(w, makeCommand(id, CmdSetLoan{12'345})).ok);              // not a step multiple
    CHECK(!executeCommand(w, makeCommand(id, CmdSetLoan{w.settings.maxLoan})).ok);  // above the maximum
    CHECK(executeCommand(w, makeCommand(id, CmdSetLoan{-w.companies[id].loan})).ok); // repay all
    CHECK_EQ(w.companies[id].loan, 0);
}

TEST_CASE("world: monthly interest is charged") {
    World w;
    const CompanyId id = createCompany(w);
    const Money before = w.companies[id].money;
    // Run until the first day of the next month.
    const i32 startDay = w.clock.day();
    const CivilDate start = civilFromDays(startDay);
    const i32 nextMonth = daysFromCivil(start.year, start.month == 12 ? 1 : start.month + 1, 1) +
                          (start.month == 12 ? 365 : 0);
    while (w.clock.day() < nextMonth) w.tick();
    CHECK(w.companies[id].money < before);
}

TEST_CASE("world: exploration activates chunks via commands") {
    World w;
    CHECK(executeCommand(w, makeCommand(CompanyId{}, CmdActivateChunks{{{0, 0}, {1, 0}}})).ok);
    CHECK(w.map.isActive({1, 0}));
    CHECK(!executeCommand(w, makeCommand(CompanyId{}, CmdActivateChunks{{{0, 0}}})).ok); // already explored
}

TEST_CASE("world: monthly upkeep respects separate road and track ownership") {
    WorldSettings settings;
    settings.interestPercent = 0;
    World w(settings);
    const CompanyId roadCo = createCompany(w);
    const CompanyId trackCo = createCompany(w);
    w.map.activate({0, 0}); // no towns: isolate maintenance from growth
    Tile& mixed = *w.map.tile({2, 2});
    mixed = Tile{};
    mixed.type = TileType::Road;
    mixed.road = 0x55; // two road and two tram pieces
    mixed.roadOwner = roadCo.value;
    mixed.owner = trackCo.value;
    Tile& rail = *w.map.tile({3, 2});
    rail = Tile{};
    rail.type = TileType::Rail;
    rail.rail = 1;
    rail.extra = 1;
    rail.owner = trackCo.value;
    Tile& stop = *w.map.tile({4, 2});
    stop = Tile{};
    stop.type = TileType::Station;
    stop.owner = trackCo.value;
    Tile& depot = *w.map.tile({5, 2});
    depot = Tile{};
    depot.type = TileType::Depot;
    depot.owner = roadCo.value;
    Tile& publicRoad = *w.map.tile({6, 2});
    publicRoad = mixed;
    publicRoad.roadOwner = kOwnerTown;
    publicRoad.owner = kOwnerNone;
    const Money roadBefore = w.companies[roadCo].money;
    const Money trackBefore = w.companies[trackCo].money;
    w.clock.tick = w.clock.tickOfDay(daysFromCivil(settings.startYear, 2, 1)) - 1;
    w.tick();
    const Money roadCost = 2 * prices::kRoadMaintenance + prices::kDepotMaintenance;
    const Money trackCost = 2 * prices::kTramMaintenance + prices::kRailMaintenance +
                            prices::kSignalMaintenance + prices::kStationMaintenance;
    CHECK_EQ(w.companies[roadCo].money, roadBefore - roadCost);
    CHECK_EQ(w.companies[trackCo].money, trackBefore - trackCost);
    CHECK_EQ(w.companies[trackCo].thisYear.amounts[static_cast<usize>(Expense::PropertyMaintenance)], -trackCost);
    w.tick();
    CHECK_EQ(w.companies[roadCo].money, roadBefore - roadCost);
}

TEST_CASE("world: terraform costs money and requires explored land") {
    World w;
    const CompanyId id = createCompany(w);
    CHECK(!executeCommand(w, makeCommand(id, CmdTerraform{{10, 10}, 1})).ok);
    for (i32 y = -1; y <= 1; ++y)
        for (i32 x = -1; x <= 1; ++x) w.activateChunk({x, y});
    const Money before = w.companies[id].money;
    const int h = w.map.cornerHeight(10, 10);
    const CommandResult r = executeCommand(w, makeCommand(id, h < kMaxHeight ? CommandPayload(CmdTerraform{{10, 10}, 1})
                                                                            : CommandPayload(CmdTerraform{{10, 10}, -1})));
    CHECK(r.ok);
    CHECK(r.cost > 0);
    CHECK_EQ(w.companies[id].money, before - r.cost);
    CHECK(w.map.cornerHeight(10, 10) != h);
}

TEST_CASE("world: identical command streams give identical state (determinism)") {
    WorldSettings s;
    s.seed = 777;
    World a(s), b(s);
    const CompanyId ia = createCompany(a);
    const CompanyId ib = createCompany(b);
    CHECK(ia == ib);
    const auto script = [&](World& w, CompanyId id, i64 tick) {
        if (tick == 5) executeCommand(w, makeCommand(id, CmdActivateChunks{{{0, 0}, {0, 1}, {1, 0}, {1, 1}}}));
        if (tick == 20) executeCommand(w, makeCommand(id, CmdTerraform{{16, 16}, 1}));
        if (tick == 30) executeCommand(w, makeCommand(id, CmdSetLoan{50'000}));
        if (tick == 40) executeCommand(w, makeCommand(id, CmdClearArea{{0, 0}, {20, 20}}));
    };
    for (i64 t = 0; t < 3 * kTicksPerDay; ++t) {
        script(a, ia, t);
        script(b, ib, t);
        a.tick();
        b.tick();
        if (t % 97 == 0) CHECK_EQ(a.stateHash(), b.stateHash());
    }
    CHECK_EQ(a.stateHash(), b.stateHash());
}

TEST_CASE("world: save and load round trip continues identically") {
    WorldSettings s;
    s.seed = 4242;
    World a(s);
    const CompanyId id = createCompany(a);
    executeCommand(a, makeCommand(id, CmdActivateChunks{{{0, 0}, {1, 0}}}));
    for (int i = 0; i < 500; ++i) a.tick();

    const std::vector<u8> bytes = toBytes(a, kSaveVersion);
    World b;
    fromBytes(bytes, b, kSaveVersion);
    CHECK_EQ(a.stateHash(), b.stateHash());
    CHECK_EQ(b.map.activeChunks().size(), 2u);
    for (int i = 0; i < 2000; ++i) {
        a.tick();
        b.tick();
    }
    CHECK_EQ(a.stateHash(), b.stateHash());
    CHECK(a.map.chunk({1, 0})->tiles == b.map.chunk({1, 0})->tiles);
}

TEST_CASE("server: local session queues commands for the next tick") {
    WorldSettings s;
    s.seed = 9;
    LocalSession session(s, "Mine");
    REQUIRE(session.world().companies.contains(session.localCompany()));
    session.submit(CmdActivateChunks{{{0, 0}}});
    CHECK(!session.world().map.isActive({0, 0}));
    session.server().step();
    CHECK(session.world().map.isActive({0, 0}));
    const auto feedback = session.takeFeedback();
    REQUIRE(feedback.size() == 1);
    CHECK(feedback[0].result.ok);
    session.setSpeed(2);
    const int ticks = session.server().update(1.0);
    CHECK(ticks > 0);
}

TEST_CASE("world: old saves retain their original terrain when exploring") {
    WorldSettings settings;
    settings.seed = 12345;
    settings.waterways = false;
    World original(settings);
    original.map.activate({-1, -1});
    World loaded;
    fromBytes(toBytes(original, 4), loaded, 4);
    CHECK(!loaded.settings.waterways);
    original.map.activate({0, -1});
    loaded.map.activate({0, -1});
    CHECK(original.map.chunk({0, -1})->heights == loaded.map.chunk({0, -1})->heights);
    CHECK(original.map.chunk({0, -1})->tiles == loaded.map.chunk({0, -1})->tiles);
    // Saving the migrated world in the current format must retain that choice.
    World resaved;
    fromBytes(toBytes(loaded, kSaveVersion), resaved, kSaveVersion);
    CHECK(!resaved.settings.waterways);
    CHECK_EQ(resaved.map.generator().cornerHeight(700, -30), original.map.generator().cornerHeight(700, -30));
}

TEST_CASE("finance: monthly periods close before new month and new year charges") {
    World w;
    const CompanyId id = createCompany(w);
    w.companies[id].spend(Expense::Construction, 1234);
    w.clock.tick = w.clock.tickOfDay(daysFromCivil(1950, 2, 1)) - 1;
    w.tick();
    const Company& co = w.companies[id];
    REQUIRE(co.monthlyHistory.size() == 1);
    CHECK_EQ(co.monthlyHistory[0].month, 1);
    CHECK_EQ(co.monthlyHistory[0].total(), -1234);
    CHECK_EQ(co.thisMonth.month, 2);
    CHECK(co.thisMonth.amounts[static_cast<usize>(Expense::LoanInterest)] < 0);
    const Money oldYear = co.thisYear.total();
    w.clock.tick = w.clock.tickOfDay(daysFromCivil(1951, 1, 1)) - 1;
    w.tick();
    CHECK_EQ(co.history.back().total(), oldYear);
    CHECK_EQ(co.thisYear.year, 1951);
    CHECK_EQ(co.thisMonth.year, 1951);
    CHECK_EQ(co.thisMonth.month, 1);
    CHECK(co.thisYear.amounts[static_cast<usize>(Expense::LoanInterest)] < 0);
}

TEST_CASE("finance: four debt quarters liquidate vehicles and release infrastructure") {
    WorldSettings settings;
    settings.interestPercent = 0;
    World w(settings);
    const CompanyId co = createCompany(w);
    w.map.activate({0, 0});
    Tile& t = *w.map.tile({10, 10});
    t = Tile{}; t.type = TileType::Road; t.road = 5; t.roadOwner = co.value;
    Vehicle bus; bus.owner = co; bus.model = 0;
    w.vehicles.create(bus);
    w.companies[co].money = -100;
    for (const auto date : {CivilDate{1950, 4, 1}, CivilDate{1950, 7, 1}, CivilDate{1950, 10, 1}, CivilDate{1951, 1, 1}}) {
        w.clock.tick = w.clock.tickOfDay(daysFromCivil(date.year, date.month, date.day)) - 1;
        w.tick();
    }
    CHECK(w.companies[co].bankrupt);
    CHECK_EQ(w.companies[co].loan, 0);
    CHECK_EQ(w.companies[co].money, 0);
    CHECK(w.vehicles.empty());
    CHECK_EQ(t.roadOwner, kOwnerTown);
    CHECK(!executeCommand(w, makeCommand(co, CmdSetLoan{10'000})).ok);
    World copy;
    fromBytes(toBytes(w, kSaveVersion), copy, kSaveVersion);
    CHECK(copy.companies[co].bankrupt);
    CHECK_EQ(copy.stateHash(), w.stateHash());
}

TEST_CASE("finance: recovering cash resets debt quarters and company details are validated") {
    WorldSettings settings; settings.interestPercent = 0;
    World w(settings);
    const CompanyId co = createCompany(w);
    w.companies[co].quartersInDebt = 3;
    w.clock.tick = w.clock.tickOfDay(daysFromCivil(1950, 2, 1)) - 1;
    w.tick();
    CHECK_EQ(w.companies[co].quartersInDebt, 0);
    REQUIRE(executeCommand(w, makeCommand(co, CmdUpdateCompany{"New name", 3})).ok);
    CHECK_EQ(w.companies[co].name, std::string("New name"));
    CHECK_EQ(w.companies[co].colour, 3);
    CHECK(!executeCommand(w, makeCommand(co, CmdUpdateCompany{"", 3})).ok);
    CHECK(!executeCommand(w, makeCommand(co, CmdUpdateCompany{"Name", 255})).ok);
}

TEST_CASE("world: hash covers terrain, orders and ledgers but ignores rendering caches") {
    World w;
    const CompanyId co = createCompany(w);
    w.map.activate({0, 0});
    u64 hash = w.stateHash();
    ++w.map.chunk({0, 0})->revision;
    CHECK_EQ(w.stateHash(), hash);
    w.map.tile({2, 2})->rail = 1;
    CHECK(w.stateHash() != hash);
    hash = w.stateHash();
    w.companies[co].thisYear.amounts[0] -= 5;
    CHECK(w.stateHash() != hash);
    Vehicle vehicle; vehicle.owner = co;
    const VehicleId id = w.vehicles.create(vehicle);
    hash = w.stateHash();
    w.vehicles[id].orders.push_back(Order{});
    CHECK(w.stateHash() != hash);
}
