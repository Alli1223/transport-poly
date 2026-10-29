#include "test.hpp"

#include "core/parallel.hpp"
#include "core/serialize.hpp"
#include "net/net_client.hpp"
#include "net/net_server.hpp"
#include "sim/walking.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace tp;

namespace {

// Restores the default thread count when a test ends, even a failed one.
struct ThreadCount {
    explicit ThreadCount(usize threads) { setParallelThreads(threads); }
    ~ThreadCount() { setParallelThreads(0); }
};

// A generated world with towns and people, run for a day with network changes along the way.
std::vector<u8> runGeneratedWorld(usize threads, u64& hash) {
    const ThreadCount count(threads);
    WorldSettings s;
    s.seed = 5150;
    World w(s);
    for (i32 y = -2; y <= 2; ++y)
        for (i32 x = -2; x <= 2; ++x) w.activateChunk({x, y});
    REQUIRE(w.people.size() > 100);
    for (i64 t = 0; t < kTicksPerDay; ++t) {
        if (t % 700 == 0) walking::invalidate(w); // as any construction does: replan everyone at home
        w.tick();
    }
    hash = w.stateHash();
    return toBytes(w, kSaveVersion);
}

struct Sample {
    u8 a = 7;
    u32 b = 0xDEADBEEF;
    i64 c = -5;
    std::string text = "transport";
    std::vector<u16> list{1, 2, 3, 65535};
    std::map<u32, std::string> names{{3, "three"}, {9, "nine"}};
    template <class Ar>
    void serialize(Ar& ar) {
        ar(a, b, c, text, list, names);
    }
};

} // namespace

TEST_CASE("parallel: every item runs exactly once, errors reach the caller, nested calls run serially") {
    for (const usize threads : {usize{1}, usize{4}, usize{0}}) {
        const ThreadCount count(threads);
        std::vector<std::atomic<int>> runs(1000);
        parallelFor(runs.size(), [&](usize i) { ++runs[i]; }, 7);
        CHECK(std::ranges::all_of(runs, [](const std::atomic<int>& r) { return r == 1; }));

        std::atomic<int> inner{0};
        parallelFor(8, [&](usize) { parallelFor(10, [&](usize) { ++inner; }); });
        CHECK_EQ(inner.load(), 80);

        bool thrown = false;
        try {
            parallelFor(100, [](usize i) {
                if (i == 42) throw std::runtime_error("item 42");
            });
        } catch (const std::runtime_error&) {
            thrown = true;
        }
        CHECK(thrown);
    }
    CHECK(parallelThreads() >= 1);
}

TEST_CASE("serialize: hashing sees exactly the bytes a save would write") {
    const Sample x;
    HashSink fromBytes;
    const std::vector<u8> bytes = toBytes(x);
    fromBytes.bytes(bytes.data(), bytes.size());
    HashSink streamed;
    HashWriter ar(streamed);
    ar(x);
    CHECK_EQ(streamed.size(), bytes.size());
    CHECK_EQ(streamed.value(), fromBytes.value());

    // Any changed byte changes the hash, as does a trailing zero.
    for (usize i = 0; i < bytes.size(); ++i) {
        std::vector<u8> changed = bytes;
        changed[i] ^= 0x01;
        HashSink h;
        h.bytes(changed.data(), changed.size());
        CHECK(h.value() != fromBytes.value());
    }
    std::vector<u8> longer = bytes;
    longer.push_back(0);
    HashSink h;
    h.bytes(longer.data(), longer.size());
    CHECK(h.value() != fromBytes.value());
}

TEST_CASE("parallel: the simulation and its state hash do not depend on the number of threads") {
    u64 serialHash = 0, parallelHash = 0;
    const std::vector<u8> serial = runGeneratedWorld(1, serialHash);
    const std::vector<u8> parallel = runGeneratedWorld(8, parallelHash);
    CHECK_EQ(serialHash, parallelHash);
    CHECK(serial == parallel);
    // Writing the world for a save is unchanged by the parallel hashing hooks.
    World loaded;
    fromBytes(serial, loaded, kSaveVersion);
    CHECK(toBytes(loaded, kSaveVersion) == serial);
    CHECK_EQ(loaded.stateHash(), serialHash);
}

TEST_CASE("network: a client plays ticks back smoothly even when the server sends them in bursts") {
    using namespace tp::net;
    REQUIRE(initNetworking());
    WorldSettings s;
    s.seed = 77;
    auto world = std::make_unique<World>(s);
    for (i32 y = -1; y <= 1; ++y)
        for (i32 x = -1; x <= 1; ++x) world->activateChunk({x, y});
    NetServer::Options opt;
    opt.port = 0;
    opt.hashInterval = 8;
    NetServer server(std::move(world), opt);
    std::string err;
    REQUIRE(server.start(err));
    NetClientSession client("127.0.0.1", server.options().port, "Watcher", kJoinSpectator);

    // 60 frames a second on the client; the server only gets to run every fourth frame (a busy
    // host), so ticks arrive in bursts of two.
    const double dt = 1.0 / 60.0;
    double previous = -1.0;
    int frames = 0, stalls = 0, jumps = 0, backwards = 0;
    for (int i = 0; i < 1500; ++i) {
        if (i % 4 == 0) server.update(4 * dt);
        client.update(dt);
        if (i % 16 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!client.ready()) continue;
        const double shown = static_cast<double>(client.world().clock.tick) + client.tickFraction();
        if (previous >= 0.0 && ++frames > 120) { // after settling in
            const double step = shown - previous;
            const double expected = dt * kTicksPerSecond;
            if (step < -1e-9) ++backwards;
            if (step < expected * 0.5) ++stalls;
            if (step > expected * 1.6) ++jumps;
        }
        previous = shown;
    }
    REQUIRE(client.phase() == NetClientSession::Phase::Playing); // no desync on the way
    CHECK(frames > 1000);
    CHECK_EQ(backwards, 0);
    CHECK_EQ(stalls, 0);
    CHECK_EQ(jumps, 0);
    CHECK(client.playbackDelay() <= 0.2);
    CHECK(client.framesBehind() <= 8);
}
