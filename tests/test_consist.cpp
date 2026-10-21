#include "test.hpp"
#include "core/serialize.hpp"
#include "sim/commands.hpp"
#include "sim/world.hpp"
using namespace tp;

namespace {
struct ConsistWorld {
    World w;
    CompanyId owner;
    VehicleId train;
    static WorldSettings settings() { WorldSettings s; s.startYear = 2000; return s; }
    ConsistWorld() : w(settings()) {
        Command c; c.payload = CmdCreateCompany{"Consist test", 0xFF};
        owner = CompanyId(static_cast<u8>(executeCommand(w,c).createdId));
        w.companies[owner].money = 10'000'000;
        Vehicle v; v.owner = owner; v.model = 7; v.cars = {7,11,19,11,8};
        v.value = 500'000;
        CargoBatch batch; batch.quantity = 25; v.cargo.push_back(batch);
        train = w.vehicles.create(v);
    }
    Command command(CommandPayload payload) {
        Command c; c.company = owner; c.payload = std::move(payload); return c;
    }
};
}

TEST_CASE("consist: rear engines and duplicate wagons reorder through serialized commands") {
    ConsistWorld f;
    const auto before = f.w.stateHash();
    auto c = f.command(CmdReorderConsist{f.train,{1,2,3,0,4}});
    CHECK(testCommand(f.w,c).ok);
    CHECK_EQ(before,f.w.stateHash());
    Command received;
    fromBytes(toBytes(c),received);
    REQUIRE(executeCommand(f.w,received).ok);
    const auto& v = f.w.vehicles[f.train];
    CHECK(v.cars == (std::vector<u16>{11,19,11,7,8}));
    CHECK(v.info().powered);
    CHECK_EQ(v.capacity(),72u);
    CHECK_EQ(v.cargoCapacity(CargoType::Grain),50u);
    CHECK_EQ(v.carLoad(1),25u);
    CHECK_EQ(v.value,500'000);
    World saved;
    fromBytes(toBytes(f.w,kSaveVersion),saved,kSaveVersion);
    CHECK_EQ(f.w.stateHash(),saved.stateHash());
    for (int i=0;i<100;++i) { f.w.tick(); saved.tick(); }
    CHECK_EQ(f.w.stateHash(),saved.stateHash());
}

TEST_CASE("consist: invalid, foreign and moving edits have no side effects") {
    ConsistWorld f;
    for (const auto& order : {std::vector<u16>{0,1}, std::vector<u16>{0,1,1,3,4}, std::vector<u16>{0,1,2,3,5}}) {
        const auto before = f.w.stateHash();
        CHECK(!executeCommand(f.w,f.command(CmdReorderConsist{f.train,order})).ok);
        CHECK_EQ(before,f.w.stateHash());
    }
    auto c = f.command(CmdReorderConsist{f.train,{4,3,2,1,0}});
    c.company = CompanyId{};
    CHECK(!executeCommand(f.w,c).ok);
    c.company = f.owner;
    f.w.vehicles[f.train].stopped = false;
    CHECK(!executeCommand(f.w,c).ok);
    f.w.vehicles[f.train].stopped = true;
    f.w.vehicles[f.train].state = VehicleState::Driving;
    CHECK(!executeCommand(f.w,c).ok);
}

TEST_CASE("consist: removing the front engine retains a powered train and protects the final engine") {
    ConsistWorld f;
    REQUIRE(executeCommand(f.w,f.command(CmdRemoveCarriage{f.train,0})).ok);
    auto& v = f.w.vehicles[f.train];
    CHECK(v.cars == (std::vector<u16>{11,19,11,8}));
    CHECK_EQ(v.model,8);
    CHECK(!executeCommand(f.w,f.command(CmdRemoveCarriage{f.train,3})).ok);
    REQUIRE(executeCommand(f.w,f.command(CmdRemoveCarriage{f.train,0})).ok);
    CHECK_EQ(v.capacity(),36u);
    CHECK_EQ(v.cargoCapacity(CargoType::Grain),50u);
}

TEST_CASE("consist: six new passenger carriage types buy, add capacity and survive saving") {
    ConsistWorld f;
    CHECK_EQ(std::string(vehicleModel(11).name), std::string("Wooden Coach"));
    CHECK_EQ(std::string(vehicleModel(31).name), std::string("Deck Cargo Coaster"));
    u32 capacity = f.w.vehicles[f.train].capacity();
    for (u16 model = 32; model < 38; ++model) {
        const auto& info = vehicleModel(model);
        CHECK(info.type == VehicleType::Rail);
        CHECK(!info.powered); CHECK(info.capacity > 0);
        const auto cash = f.w.companies[f.owner].money;
        REQUIRE(executeCommand(f.w, f.command(CmdAddCarriage{f.train, model})).ok);
        capacity += info.capacity;
        CHECK_EQ(f.w.vehicles[f.train].capacity(), capacity);
        CHECK_EQ(f.w.companies[f.owner].money, cash - f.w.inflated(info.price));
    }
    World loaded;
    fromBytes(toBytes(f.w, kSaveVersion), loaded, kSaveVersion);
    CHECK_EQ(f.w.stateHash(), loaded.stateHash());
    CHECK_EQ(loaded.vehicles[f.train].capacity(), capacity);
    f.w.settings.startYear = 1950;
    f.w.clock.startDay = daysFromCivil(1950, 1, 2);
    CHECK(!testCommand(f.w, f.command(CmdAddCarriage{f.train, 37})).ok);
}
