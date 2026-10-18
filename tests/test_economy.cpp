#include "test.hpp"
#include "core/serialize.hpp"
#include "sim/commands.hpp"
#include "sim/world.hpp"
using namespace tp;

TEST_CASE("economy: inflation changes quotes and charges equally without inflating loans") {
    World w;
    w.activateChunk({0, 0});
    for (int y = 1; y < 31; ++y) for (int x = 1; x < 31; ++x) {
        w.map.setCornerHeight(x, y, 3); *w.map.tile({x, y}) = Tile{};
    }
    Command create; create.payload = CmdCreateCompany{"Economy", 0};
    const auto made = executeCommand(w, create); REQUIRE(made.ok);
    const CompanyId co(static_cast<u8>(made.createdId));
    Command road; road.company = co; road.payload = CmdBuildRoad{{8, 16}, {20, 16}};
    const Money base = testCommand(w, road).cost;
    w.economy.priceIndex = 20'000;
    const auto quote = testCommand(w, road); REQUIRE(quote.ok);
    CHECK_EQ(quote.cost, base * 2);
    const Money before = w.companies[co].money;
    const auto built = executeCommand(w, road); REQUIRE(built.ok);
    CHECK_EQ(built.cost, quote.cost);
    CHECK_EQ(w.companies[co].money, before - quote.cost);
    Command loan; loan.company = co; loan.payload = CmdSetLoan{10'000};
    const Money balance = w.companies[co].money;
    REQUIRE(executeCommand(w, loan).ok);
    CHECK_EQ(w.companies[co].money, balance + 10'000);
    CHECK_EQ(w.fareIncome(100), 200);
    w.economy.recessionMonths = 3;
    CHECK_EQ(w.fareIncome(100), 140);
}

TEST_CASE("economy: deterministic recessions recover and saved economies continue identically") {
    WorldSettings s; s.inflation = true; s.recessions = true;
    World w(s);
    w.economy.nextRecessionMonth = w.clock.date().year * 12;
    economy::monthly(w);
    CHECK(w.economy.recessionMonths >= 6);
    CHECK(w.economy.recessionMonths <= 12);
    CHECK(w.economy.priceIndex > 10'000);
    World copy;
    fromBytes(toBytes(w, kSaveVersion), copy, kSaveVersion);
    for (int i = 0; i < 18; ++i) { economy::monthly(w); economy::monthly(copy); }
    CHECK_EQ(copy.stateHash(), w.stateHash());
    CHECK_EQ(w.economy.recessionMonths, 0);
    CHECK(w.news.size() >= 2);
    s.inflation = false; s.recessions = false;
    World disabled(s);
    for (int i = 0; i < 120; ++i) economy::monthly(disabled);
    CHECK_EQ(disabled.economy.priceIndex, 10'000);
    CHECK_EQ(disabled.economy.recessionMonths, 0);
    World legacy;
    fromBytes(toBytes(w, 9), legacy, 9);
    CHECK(!legacy.settings.inflation);
    CHECK(!legacy.settings.recessions);
    CHECK_EQ(legacy.economy.priceIndex, 10'000);
}
