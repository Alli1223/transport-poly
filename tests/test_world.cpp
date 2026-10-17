#include "test.hpp"

#include "core/serialize.hpp"
#include "net/server.hpp"
#include "net/session.hpp"
#include "sim/commands.hpp"
#include "sim/world.hpp"

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
