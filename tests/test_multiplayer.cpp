#include "test.hpp"

#include "net/net_client.hpp"
#include "net/net_server.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

using namespace tp;
using namespace tp::net;

namespace {

// A small server on a free port.
struct Game {
    std::unique_ptr<NetServer> server;
    u16 port = 0;

    explicit Game(std::string password = {}) {
        REQUIRE(initNetworking());
        WorldSettings s;
        s.seed = 9001;
        auto world = std::make_unique<World>(s);
        for (i32 y = -1; y <= 1; ++y)
            for (i32 x = -1; x <= 1; ++x) world->activateChunk({x, y});
        NetServer::Options opt;
        opt.port = 0;
        opt.password = std::move(password);
        server = std::make_unique<NetServer>(std::move(world), opt);
        std::string err;
        REQUIRE(server->start(err));
        port = server->options().port;
    }

    // Runs the server and clients until `done` holds (or gives up).
    bool run(std::vector<NetClientSession*> clients, const std::function<bool()>& done, int limit = 3000) {
        const double dt = 1.0 / kTicksPerSecond;
        for (int i = 0; i < limit; ++i) {
            server->update(dt);
            for (NetClientSession* c : clients) c->update(dt);
            if (done()) return true;
            if (i % 16 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            else std::this_thread::yield();
        }
        return false;
    }
};

} // namespace

TEST_CASE("multiplayer: players choose a company name and colour in the lobby") {
    Game g;
    NetClientSession a("127.0.0.1", g.port, "Alice", kJoinAsk);
    REQUIRE(g.run({&a}, [&] { return a.choosingCompany(); }));
    CHECK(!a.localCompany().valid());
    a.joinCompany(kJoinNewCompany, "Alice Express", 6);
    REQUIRE(g.run({&a}, [&] { return a.localCompany().valid(); }));
    const Company& co = a.world().companies[a.localCompany()];
    CHECK_EQ(co.name, std::string("Alice Express"));
    CHECK_EQ(co.colour, 6);
    CHECK(!a.choosingCompany());

    // A second player cannot take the same name; a taken colour falls back to a free one.
    NetClientSession b("127.0.0.1", g.port, "Bob", kJoinAsk);
    REQUIRE(g.run({&a, &b}, [&] { return b.choosingCompany(); }));
    b.joinCompany(kJoinNewCompany, "Alice Express", 6);
    std::vector<std::string> notices;
    REQUIRE(g.run({&a, &b}, [&] {
        for (std::string& n : b.takeNotices()) notices.push_back(std::move(n));
        return !notices.empty();
    }));
    CHECK(b.choosingCompany());
    b.joinCompany(kJoinNewCompany, "Bob Freight", 6);
    REQUIRE(g.run({&a, &b}, [&] { return b.localCompany().valid(); }));
    CHECK(b.localCompany() != a.localCompany());
    CHECK(b.world().companies[b.localCompany()].colour != 6);
}

TEST_CASE("multiplayer: co-op players share a company, which a password can lock") {
    Game g;
    NetClientSession a("127.0.0.1", g.port, "Alice", kJoinAsk);
    NetClientSession b("127.0.0.1", g.port, "Bob", kJoinAsk);
    REQUIRE(g.run({&a, &b}, [&] { return a.choosingCompany() && b.choosingCompany(); }));
    a.joinCompany(kJoinNewCompany, "Shared Rail", 2);
    REQUIRE(g.run({&a, &b}, [&] { return a.localCompany().valid(); }));
    const CompanyId shared = a.localCompany();

    a.setCompanyPassword("hunter2");
    REQUIRE(g.run({&a, &b}, [&] { return !b.lockedCompanies().empty(); }));
    CHECK(g.server->companyLocked(shared));
    b.joinCompany(shared.value);
    std::vector<std::string> notices;
    REQUIRE(g.run({&a, &b}, [&] {
        for (std::string& n : b.takeNotices()) notices.push_back(std::move(n));
        return !notices.empty();
    }));
    CHECK(!b.localCompany().valid());
    b.joinCompany(shared.value, {}, 0xFF, "wrong");
    notices.clear();
    REQUIRE(g.run({&a, &b}, [&] {
        for (std::string& n : b.takeNotices()) notices.push_back(std::move(n));
        return !notices.empty();
    }));
    b.joinCompany(shared.value, {}, 0xFF, "hunter2");
    REQUIRE(g.run({&a, &b}, [&] { return b.localCompany() == shared; }));

    // Both players act for the same company: the loan they take adds up.
    const Money before = g.server->server().world().companies[shared].loan;
    a.submit(CmdSetLoan{100'000});
    b.submit(CmdSetLoan{100'000});
    REQUIRE(g.run({&a, &b}, [&] { return g.server->server().world().companies[shared].loan == before + 200'000; }));
    CHECK_EQ(g.server->server().world().companies.size(), 1u);
}

TEST_CASE("multiplayer: a server password keeps strangers out") {
    Game g("opensesame");
    NetClientSession stranger("127.0.0.1", g.port, "Mallory", kJoinAsk);
    REQUIRE(g.run({&stranger}, [&] { return stranger.phase() == NetClientSession::Phase::Failed; }));
    CHECK(stranger.error().find("password") != std::string::npos);
    NetClientSession friendly("127.0.0.1", g.port, "Alice", kJoinAsk, "opensesame");
    REQUIRE(g.run({&friendly}, [&] { return friendly.choosingCompany(); }));
}

TEST_CASE("multiplayer: chat goes to everyone, to a company or to one player") {
    Game g;
    NetClientSession a("127.0.0.1", g.port, "Alice", kJoinAsk);
    NetClientSession b("127.0.0.1", g.port, "Bob", kJoinAsk);
    NetClientSession c("127.0.0.1", g.port, "Carol", kJoinAsk);
    REQUIRE(g.run({&a, &b, &c}, [&] { return a.choosingCompany() && b.choosingCompany() && c.choosingCompany(); }));
    a.joinCompany(kJoinNewCompany, "Team A", 1);
    REQUIRE(g.run({&a, &b, &c}, [&] { return a.localCompany().valid(); }));
    b.joinCompany(a.localCompany().value);
    c.joinCompany(kJoinNewCompany, "Team C", 4);
    REQUIRE(g.run({&a, &b, &c}, [&] { return b.localCompany().valid() && c.localCompany().valid(); }));

    std::vector<ChatBroadcastMsg> got[3];
    NetClientSession* who[3] = {&a, &b, &c};
    const auto collect = [&] {
        for (int i = 0; i < 3; ++i)
            for (auto& m : who[i]->takeChat()) got[i].push_back(std::move(m));
    };
    a.sendChat("hello all");
    a.sendChat("team only", ChatScope::Company);
    c.sendChat("psst", ChatScope::Private, b.clientId());
    REQUIRE(g.run({&a, &b, &c}, [&] {
        collect();
        return got[1].size() >= 3 && got[0].size() >= 2 && got[2].size() >= 2;
    }));
    g.run({&a, &b, &c}, [&] { collect(); return false; }, 60); // anything else still in flight
    const auto has = [&](int i, const std::string& text) {
        return std::ranges::any_of(got[i], [&](const ChatBroadcastMsg& m) { return m.text == text; });
    };
    CHECK(has(0, "hello all"));
    CHECK(has(1, "hello all"));
    CHECK(has(2, "hello all"));
    CHECK(has(0, "team only"));
    CHECK(has(1, "team only"));
    CHECK(!has(2, "team only"));
    CHECK(has(1, "psst"));
    CHECK(has(2, "psst")); // the sender sees their own message
    CHECK(!has(0, "psst"));
}

TEST_CASE("multiplayer: players see each other's pointers, and the host can remove a player") {
    Game g;
    NetClientSession a("127.0.0.1", g.port, "Alice", kJoinSpectator);
    NetClientSession b("127.0.0.1", g.port, "Bob", kJoinSpectator);
    REQUIRE(g.run({&a, &b}, [&] { return a.ready() && b.ready(); }));
    a.setCursor(CursorInfo{0, 10 * kCursorScale, 4 * kCursorScale, -3 * kCursorScale});
    REQUIRE(g.run({&a, &b}, [&] {
        return std::ranges::any_of(b.cursors(), [&](const CursorInfo& c) { return c.client == a.clientId() && c.x == 10 * kCursorScale; });
    }));
    a.setCursor(std::nullopt);
    REQUIRE(g.run({&a, &b}, [&] {
        return std::ranges::none_of(b.cursors(), [&](const CursorInfo& c) { return c.client == a.clientId(); });
    }));

    REQUIRE(g.server->kick(b.clientId(), "Too many trains"));
    REQUIRE(g.run({&a, &b}, [&] { return b.phase() == NetClientSession::Phase::Failed; }));
    CHECK(b.error().find("Too many trains") != std::string::npos);
    REQUIRE(g.run({&a}, [&] { return a.clients().size() == 1; }));
}
