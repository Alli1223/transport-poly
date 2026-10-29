#include "core/serialize.hpp"
#include "sim/commands.hpp"
#include "sim/industries.hpp"
#include "sim/prices.hpp"
#include "sim/world.hpp"
#include "test.hpp"
#include <algorithm>
#include <string_view>
using namespace tp;
namespace {
struct AccessSite {
    World w;
    CompanyId company;
    StationId station;
    TownId town;
    AccessSite(bool angled = false) {
        for (i32 y = 0; y < 2; ++y)
            for (i32 x = 0; x < 2; ++x) w.map.activate({x, y});
        for (i32 y = 1; y < 64; ++y)
            for (i32 x = 1; x < 64; ++x) {
                w.map.setCornerHeight(x, y, 4);
                *w.map.tile({x, y}) = Tile{};
            }
        Command c;
        c.payload = CmdCreateCompany{"Town expansion", 0xFF};
        company = CompanyId(static_cast<u8>(executeCommand(w, c).createdId));
        w.companies[company].money = 10'000'000;
        TrackAnchor a;
        a.pos = tileCentre({10, 10});
        const auto built =
            run(CmdBuildTrackStation{a, tileCentre(angled ? TilePos{16, 16} : TilePos{18, 10})});
        if (!built.ok) test::reportFailure("station fixture", built.error, std::source_location::current());
        REQUIRE(built.ok);
        station = StationId(built.createdId);
        REQUIRE(run(CmdBuildRoad{{6, 18}, {26, 18}}).ok);
        town = w.towns.create();
        w.towns[town].centre = {12, 18};
        w.towns[town].name = "Expansion";
    }
    Command command(CommandPayload payload) {
        Command c;
        c.company = company;
        c.payload = std::move(payload);
        return c;
    }
    CommandResult run(CommandPayload payload) { return executeCommand(w, command(std::move(payload))); }
    BuildingRef building(TilePos pos, u16 type = 0) {
        const auto id = w.buildings.create();
        auto& b = w.buildings[id];
        b.pos = pos;
        b.type = type;
        b.serial = id.value + 100;
        b.town = town;
        w.towns[town].buildings.push_back(id);
        for (i32 y = 0; y < b.info().size; ++y)
            for (i32 x = 0; x < b.info().size; ++x) {
                auto& tile = *w.map.tile({pos.x + x, pos.y + y});
                tile.type = TileType::Building;
                tile.index = id.value;
            }
        return b.ref(id);
    }
};
} // namespace
TEST_CASE("station access: demolishing a roadside house permits road or plot entrance placement") {
    for (const bool angled : {false, true}) {
        AccessSite f(angled);
        const auto house = f.building({12, 17});
        f.building({12, 19});
        CHECK(!f.run(CmdStationEntrance{f.station, {12, 18}}).ok);
        REQUIRE(f.run(CmdDemolishBuilding{house}).ok);
        World copy;
        fromBytes(toBytes(f.w, kSaveVersion), copy, kSaveVersion);
        const auto before = f.w.stateHash();
        auto plot = f.command(CmdStationEntrance{f.station, {12, 17}});
        REQUIRE(testCommand(f.w, plot).ok);
        CHECK_EQ(before, f.w.stateHash());
        REQUIRE(executeCommand(f.w, plot).ok);
        auto road = f.command(CmdStationEntrance{f.station, {12, 18}});
        REQUIRE(executeCommand(copy, road).ok);
        for (const World* w : {&f.w, &copy}) {
            const auto& entry = w->stations[f.station].entrances.front();
            CHECK(entry.road == (TilePos{12, 18}));
            CHECK(entry.tiles.front() == (TilePos{12, 17}));
            CHECK(w->map.tile({12, 18})->type == TileType::Road);
            for (const auto tile : entry.tiles) CHECK(w->map.tile(tile)->type == TileType::Footpath);
            REQUIRE(walking::toStation(*w, entry.path.front(), f.station));
        }
    }
}
TEST_CASE("building demolition: one quote clears the entire footprint and permits a road extension") {
    AccessSite f;
    const auto shop = f.building({12, 16}, 7);
    auto command = f.command(CmdDemolishBuilding{shop});
    Command restored;
    fromBytes(toBytes(command, kSaveVersion), restored, kSaveVersion);
    CHECK(std::get<CmdDemolishBuilding>(restored.payload).building == shop);
    const auto before = f.w.stateHash();
    const auto quote = testCommand(f.w, command);
    REQUIRE(quote.ok);
    CHECK_EQ(before, f.w.stateHash());
    CHECK_EQ(quote.cost, prices::kDemolishBuilding);
    const auto money = f.w.companies[f.company].money;
    const auto result = executeCommand(f.w, command);
    REQUIRE(result.ok);
    CHECK_EQ(result.cost, quote.cost);
    CHECK_EQ(f.w.companies[f.company].money, money - quote.cost);
    CHECK(!f.w.buildings.contains(shop.id));
    for (i32 y = 16; y < 18; ++y)
        for (i32 x = 12; x < 14; ++x) CHECK(f.w.map.tile({x, y})->type == TileType::Clear);
    REQUIRE(f.run(CmdBuildRoad{{12, 18}, {12, 15}}).ok);
    const auto extended = f.w.stateHash();
    CHECK(!executeCommand(f.w, command).ok);
    CHECK_EQ(extended, f.w.stateHash());
    CHECK(f.w.map.tile({12, 17})->type == TileType::Road);
}
TEST_CASE("building demolition: affordability and town refusal preserve the building and money") {
    AccessSite f;
    const auto house = f.building({12, 17});
    f.w.buildings[house.id].residents = 3;
    const auto command = f.command(CmdDemolishBuilding{house});
    CHECK_EQ(testCommand(f.w, command).cost, prices::kDemolishBuilding + 3 * prices::kDemolishPerResident);
    f.w.companies[f.company].money = 1;
    const auto poor = f.w.stateHash();
    CHECK(!executeCommand(f.w, command).ok);
    CHECK_EQ(poor, f.w.stateHash());
    f.w.companies[f.company].money = 100'000;
    f.w.towns[f.town].ratings[f.company.value] = -1;
    const auto refused = f.w.stateHash();
    CHECK(!executeCommand(f.w, command).ok);
    CHECK_EQ(refused, f.w.stateHash());
}
TEST_CASE("station access: a clear approach beyond sixteen tiles reaches the platform") {
    AccessSite f;
    REQUIRE(f.run(CmdBuildRoad{{6, 48}, {26, 48}}).ok);
    const auto command = f.command(CmdStationEntrance{f.station, {12, 48}});
    const auto before = f.w.stateHash();
    const auto quote = testCommand(f.w, command);
    REQUIRE(quote.ok);
    CHECK_EQ(before, f.w.stateHash());
    const auto built = executeCommand(f.w, command);
    REQUIRE(built.ok);
    CHECK_EQ(quote.cost, built.cost);
    const auto& entry = f.w.stations[f.station].entrances.front();
    CHECK(entry.tiles.size() > 16);
    REQUIRE(walking::toStation(f.w, entry.path.front(), f.station));
}
TEST_CASE("station access: an entrance placed on a house demolishes it and charges for the demolition") {
    // A one-tile house, and a department store whose whole 2x2 footprint goes.
    for (const u16 type : {u16{1}, u16{7}}) {
        AccessSite f;
        const auto house = f.building(type == 7 ? TilePos{12, 16} : TilePos{12, 17}, type);
        f.w.buildings[house.id].residents = 4;
        const auto& b = f.w.buildings[house.id];
        const TilePos corner = b.pos;
        const i32 size = b.size();
        const auto command = f.command(CmdStationEntrance{f.station, {12, 17}});
        Command restored;
        fromBytes(toBytes(command, kSaveVersion), restored, kSaveVersion);
        // Copies to replay the same command on, and to build the same entrance in two steps.
        World replay, twoSteps;
        fromBytes(toBytes(f.w, kSaveVersion), replay, kSaveVersion);
        fromBytes(toBytes(f.w, kSaveVersion), twoSteps, kSaveVersion);
        const auto demolition = executeCommand(twoSteps, f.command(CmdDemolishBuilding{house}));
        REQUIRE(demolition.ok);
        CHECK_EQ(demolition.cost, prices::kDemolishBuilding + 4 * prices::kDemolishPerResident);
        const auto entrance = executeCommand(twoSteps, command);
        REQUIRE(entrance.ok);

        const auto before = f.w.stateHash();
        const auto quote = testCommand(f.w, command);
        REQUIRE(quote.ok);
        CHECK_EQ(before, f.w.stateHash());
        CHECK_EQ(quote.cost, demolition.cost + entrance.cost);
        const auto money = f.w.companies[f.company].money;
        const auto built = executeCommand(f.w, command);
        REQUIRE(built.ok);
        CHECK_EQ(built.cost, quote.cost);
        CHECK_EQ(f.w.companies[f.company].money, money - quote.cost);
        CHECK(!f.w.buildings.contains(house.id));
        CHECK(std::ranges::find(f.w.towns[f.town].buildings, house.id) == f.w.towns[f.town].buildings.end());
        for (i32 y = 0; y < size; ++y)
            for (i32 x = 0; x < size; ++x) CHECK(f.w.map.tile({corner.x + x, corner.y + y})->type != TileType::Building);
        const auto& entry = f.w.stations[f.station].entrances.front();
        CHECK(entry.road == (TilePos{12, 18}));
        CHECK(entry.tiles.front() == (TilePos{12, 17}));
        REQUIRE(walking::toStation(f.w, entry.path.front(), f.station));
        // Every peer that runs the command ends with the same world, the same as doing it in two steps.
        REQUIRE(executeCommand(replay, restored).ok);
        CHECK_EQ(replay.stateHash(), f.w.stateHash());
        CHECK_EQ(twoSteps.stateHash(), f.w.stateHash());
    }
}
TEST_CASE("station access: an entrance on a building that can't be demolished is refused and changes nothing") {
    AccessSite f;
    const auto refused = [&](TilePos at, std::string_view why, std::source_location where = std::source_location::current()) {
        const auto before = f.w.stateHash();
        const auto money = f.w.companies[f.company].money;
        const auto r = f.run(CmdStationEntrance{f.station, at});
        if (r.ok || r.error.find(why) == std::string::npos) test::reportFailure("refusal", r.ok ? "built" : r.error, where);
        CHECK_EQ(before, f.w.stateHash());
        CHECK_EQ(money, f.w.companies[f.company].money);
        CHECK(f.w.stations[f.station].entrances.empty());
    };
    const auto farm = f.building({20, 15}, industries::kGrainFarm);
    refused({21, 17}, "industries can't be demolished");
    CHECK(f.w.buildings.contains(farm.id));
    const auto inland = f.building({12, 14});
    refused({12, 14}, "directly beside a surface road");
    CHECK(f.w.buildings.contains(inland.id));
    const auto house = f.building({12, 17});
    f.w.towns[f.town].ratings[f.company.value] = -1;
    refused({12, 17}, "local authority refuses");
    f.w.towns[f.town].ratings[f.company.value] = 0;
    const auto quote = testCommand(f.w, f.command(CmdStationEntrance{f.station, {12, 17}}));
    REQUIRE(quote.ok);
    f.w.companies[f.company].money = quote.cost - 1;
    refused({12, 17}, "Not enough money");
    CHECK(f.w.buildings.contains(house.id));
    f.w.companies[f.company].money = quote.cost;
    CHECK(f.run(CmdStationEntrance{f.station, {12, 17}}).ok);
    CHECK(!f.w.buildings.contains(house.id));
}
