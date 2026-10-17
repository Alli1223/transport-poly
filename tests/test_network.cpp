#include "test.hpp"

#include "core/serialize.hpp"
#include "net/net_client.hpp"
#include "net/net_server.hpp"
#include "sim/savegame.hpp"

#include <chrono>
#include <memory>
#include <thread>

using namespace tp;
using namespace tp::net;

namespace {

u16 testPort() {
    const auto t = std::chrono::steady_clock::now().time_since_epoch().count();
    return static_cast<u16>(40000 + (t / 1000) % 20000);
}

} // namespace

TEST_CASE("network: snapshot round trip preserves the world") {
    WorldSettings s;
    s.seed = 31337;
    World w(s);
    for (i32 y = -1; y <= 1; ++y)
        for (i32 x = -1; x <= 1; ++x) w.activateChunk({x, y});
    for (int i = 0; i < 100; ++i) w.tick();
    const auto bytes = snapshotWorld(w, CompanyId{});
    CompanyId c;
    const auto restored = restoreWorld(bytes, c);
    REQUIRE(restored != nullptr);
    CHECK_EQ(restored->stateHash(), w.stateHash());
}

TEST_CASE("network: two clients stay in sync with the server") {
    REQUIRE(initNetworking());
    WorldSettings s;
    s.seed = 4711;
    auto world = std::make_unique<World>(s);
    for (i32 y = -2; y <= 2; ++y)
        for (i32 x = -2; x <= 2; ++x) world->activateChunk({x, y});
    NetServer::Options opt;
    opt.port = testPort();
    opt.hashInterval = 8;
    NetServer server(std::move(world), opt);
    std::string err;
    REQUIRE(server.start(err));

    NetClientSession a("127.0.0.1", opt.port, "Alice");
    NetClientSession b("127.0.0.1", opt.port, "Bob");
    const double dt = 1.0 / kTicksPerSecond;
    // Join both clients and wait until each owns a company.
    for (int i = 0; i < 3000 && !(a.ready() && b.ready() && a.localCompany().valid() && b.localCompany().valid()); ++i) {
        server.update(dt);
        a.update(dt);
        b.update(dt);
        std::this_thread::yield();
    }
    REQUIRE(a.phase() == NetClientSession::Phase::Playing);
    REQUIRE(b.phase() == NetClientSession::Phase::Playing);
    REQUIRE(a.localCompany().valid());
    REQUIRE(b.localCompany().valid());
    CHECK(a.localCompany() != b.localCompany());

    // Both players issue commands; run for a while with hash checks every 8 ticks.
    a.submit(CmdActivateChunks{{{3, 0}, {3, 1}}});
    b.submit(CmdSetLoan{100'000});
    for (int i = 0; i < 600; ++i) {
        server.update(dt);
        a.update(dt);
        b.update(dt);
        if (i == 100) a.submit(CmdTerraform{{10, 10}, 1});
        std::this_thread::yield();
    }
    // Stop the clock and let the clients catch up to the server's last frame.
    server.server().setSpeed(0);
    for (int i = 0; i < 500 && (a.framesBehind() > 0 || b.framesBehind() > 0); ++i) {
        server.update(dt);
        a.update(dt);
        b.update(dt);
        std::this_thread::yield();
    }
    CHECK(a.phase() == NetClientSession::Phase::Playing);
    CHECK(b.phase() == NetClientSession::Phase::Playing);
    const World& sw = server.server().world();
    CHECK_EQ(a.world().clock.tick, sw.clock.tick);
    CHECK_EQ(b.world().clock.tick, sw.clock.tick);
    CHECK_EQ(a.world().stateHash(), sw.stateHash());
    CHECK_EQ(b.world().stateHash(), sw.stateHash());
    CHECK(sw.map.isActive({3, 1}));
    CHECK_EQ(sw.companies[b.localCompany()].loan, sw.settings.initialLoan + 100'000);
    // Command results reached the issuing client.
    bool sawFeedback = false;
    for (const auto& f : a.takeFeedback()) sawFeedback = sawFeedback || f.name == "Explore";
    CHECK(sawFeedback);
    server.stop();
}
