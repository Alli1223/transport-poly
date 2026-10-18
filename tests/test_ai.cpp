#include "test.hpp"
#include "sim/ai.hpp"
#include "sim/commands.hpp"
#include "sim/road.hpp"
#include "sim/world.hpp"
#include "core/serialize.hpp"
using namespace tp;

TEST_CASE("AI: builds affordable operating bus routes and survives save continuation") {
    WorldSettings settings; settings.aiCompetitors = 1;
    World w(settings);
    w.map.activate({0, 0});
    for (i32 y = 1; y < 31; ++y) for (i32 x = 1; x < 31; ++x) {
        w.map.setCornerHeight(x, y, 3); *w.map.tile({x,y}) = Tile{};
    }
    const auto town = w.towns.create();
    w.towns[town].name = "Testville"; w.towns[town].centre = {15, 15};
    for (i32 x = 4; x < 26; ++x) {
        CHECK(connectRoad(w.map, {x,15}, Dir::E, kOwnerTown, false));
        w.towns[town].roads.push_back({x,15});
    }
    Command human; human.payload = CmdCreateCompany{"Human", 0xFF};
    REQUIRE(executeCommand(w, human).ok);
    ai::daily(w);
    CHECK_EQ(w.companies.size(), 2u);
    REQUIRE(w.vehicles.size() > 0);
    CHECK_EQ(w.stations.size(), 2u);
    for (const auto [id,v] : w.vehicles) {
        CHECK(w.companies[v.owner].aiControlled);
        CHECK(!v.stopped); CHECK_EQ(v.orders.size(), 2u);
        CHECK(w.companies[v.owner].money >= 50'000);
    }
    World loaded;
    fromBytes(toBytes(w, kSaveVersion), loaded, kSaveVersion);
    CHECK_EQ(w.stateHash(), loaded.stateHash());
    ai::daily(w); ai::daily(loaded);
    for (int i=0; i<500; ++i) { w.tick(); loaded.tick(); }
    CHECK_EQ(w.stateHash(), loaded.stateHash());
}

TEST_CASE("AI: disabled by default and failed plans leave no infrastructure") {
    World w;
    Command human; human.payload = CmdCreateCompany{"Human", 0xFF};
    REQUIRE(executeCommand(w, human).ok);
    ai::daily(w); CHECK_EQ(w.companies.size(), 1u);
    w.settings.aiCompetitors = 1;
    ai::daily(w); CHECK_EQ(w.companies.size(), 2u);
    CHECK(w.stations.empty()); CHECK(w.depots.empty()); CHECK(w.vehicles.empty());
    const auto hash = w.stateHash(); ai::daily(w); CHECK_EQ(hash, w.stateHash());
}
