// Headless dedicated server.
//
//   transport-poly-server [--port N] [--seed N] [--load file] [--name text] [--password text] [--autosave months]
//                         [--threads N]
//   transport-poly-server --load file --bench ticks [--bench-reroute ticks] [--bench-save file]
//
// Console commands (stdin): status, save <file>, speed <0-16>, say <text>, quit

#include "core/calendar.hpp"
#include "core/log.hpp"
#include "core/parallel.hpp"
#include "core/version.hpp"
#include "net/net_server.hpp"
#include "sim/league.hpp"
#include "sim/savegame.hpp"
#include "sim/passengers.hpp"
#include "sim/walking.hpp"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

using namespace tp;

namespace {

std::mutex g_consoleMutex;
std::queue<std::string> g_consoleLines;
std::atomic<bool> g_quit{false};

void consoleThread() {
    std::string line;
    while (!g_quit && std::getline(std::cin, line)) {
        std::scoped_lock lock(g_consoleMutex);
        g_consoleLines.push(line);
    }
}

double percentile(std::vector<double> sorted, double p) {
    if (sorted.empty()) return 0.0;
    const usize i = std::min(sorted.size() - 1, static_cast<usize>(p * static_cast<double>(sorted.size())));
    return sorted[i];
}

// --bench: runs the loaded world flat out and reports how long ticks take, the slowest ticks, what
// the desync check costs and what a network change costs. --bench-reroute makes such a change every
// so many ticks; --bench-save keeps the world as it is after the ticks (to compare builds).
int runBenchmark(World& w, i64 ticks, const std::string& savePath, i64 rerouteEvery) {
    using Clock = std::chrono::steady_clock;
    const auto ms = [](Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
    log::info("Benchmark: {} ticks from {} (tick {}), {} people, {} vehicles, {} stations", ticks, formatDate(w.clock.day()),
              w.clock.tick, w.people.size(), w.vehicles.size(), w.stations.size());
    struct Sample {
        double ms;
        i64 tick;
    };
    std::vector<Sample> samples;
    samples.reserve(static_cast<usize>(ticks));
    const auto begin = Clock::now();
    for (i64 i = 0; i < ticks; ++i) {
        const auto t0 = Clock::now();
        // --bench-reroute: what a construction command sets off, every so many ticks.
        if (rerouteEvery > 0 && i % rerouteEvery == rerouteEvery - 1) walking::invalidate(w);
        w.tick();
        samples.push_back({ms(Clock::now() - t0), w.clock.tick});
    }
    const double total = ms(Clock::now() - begin);
    if (std::string err; !savePath.empty() && !saveGame(w, CompanyId{}, savePath, err)) log::error("{}", err);
    std::vector<double> sorted;
    for (const Sample& s : samples) sorted.push_back(s.ms);
    std::ranges::sort(sorted);
    const double interval = 1000.0 / kTicksPerSecond;
    const auto over = std::ranges::count_if(sorted, [&](double v) { return v > interval; });
    log::info("Ticks: mean {:.3f} ms, p50 {:.3f}, p90 {:.3f}, p99 {:.3f}, p99.9 {:.3f}, max {:.3f} ms; {} over {:.1f} ms",
              total / static_cast<double>(ticks), percentile(sorted, 0.5), percentile(sorted, 0.9), percentile(sorted, 0.99),
              percentile(sorted, 0.999), sorted.back(), over, interval);
    std::ranges::sort(samples, std::greater<>{}, &Sample::ms);
    for (usize i = 0; i < std::min<usize>(10, samples.size()); ++i) {
        const Sample& s = samples[i];
        const i64 ofDay = s.tick % kTicksPerDay;
        log::info("  slow tick {} ({:.2f} ms): {} {:02}:{:02}{}", s.tick, s.ms, formatDate(w.clock.startDay + static_cast<i32>(s.tick / kTicksPerDay)),
                  ofDay / kTicksPerHour, ofDay % kTicksPerHour / kTicksPerMinute, ofDay == 0 ? " (new day)" : "");
    }
    const usize threads = parallelThreads();
    for (const usize count : {usize{1}, threads}) {
        setParallelThreads(count);
        const auto t0 = Clock::now();
        u64 h = 0;
        for (int i = 0; i < 5; ++i) h = w.stateHash();
        log::info("State hash on {} thread(s): {:.2f} ms ({:016x})", parallelThreads(), ms(Clock::now() - t0) / 5.0, h);
    }
    {
        // What any construction command costs: walking routes are checked and the passenger
        // routing is rebuilt, which replans the day of everyone at home.
        const auto t0 = Clock::now();
        walking::invalidate(w);
        const auto t1 = Clock::now();
        passengers::updateRouting(w);
        log::info("Network change: {:.2f} ms walking check, {:.2f} ms routing rebuild", ms(t1 - t0), ms(Clock::now() - t1));
    }
    {
        const auto t0 = Clock::now();
        const auto bytes = snapshotWorld(w, CompanyId{});
        log::info("Snapshot: {:.2f} ms, {} bytes", ms(Clock::now() - t0), bytes.size());
    }
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {
    log::info("{} dedicated server {} (protocol {})", kGameName, kVersionString, kProtocolVersion);
    net::NetServer::Options options;
    WorldSettings settings;
    settings.seed = static_cast<u64>(std::chrono::system_clock::now().time_since_epoch().count()) & 0x7FFFFFFF;
    std::string loadPath;
    i32 autosaveMonths = 1;
    i64 benchTicks = 0;
    std::string benchSave;
    i64 benchReroute = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (arg == "--port") options.port = static_cast<u16>(std::atoi(next().c_str()));
        else if (arg == "--seed") settings.seed = std::strtoull(next().c_str(), nullptr, 10);
        else if (arg == "--load") loadPath = next();
        else if (arg == "--name") options.name = next();
        else if (arg == "--password") options.password = next();
        else if (arg == "--goal-value") settings.goalValue = std::max<Money>(0, std::atoll(next().c_str()));
        else if (arg == "--goal-years") {
            const int years = std::atoi(next().c_str());
            settings.goalYear = years > 0 ? settings.startYear + years : 0;
        }
        else if (arg == "--autosave") autosaveMonths = std::atoi(next().c_str());
        else if (arg == "--bench") benchTicks = std::max<i64>(1, std::atoll(next().c_str()));
        else if (arg == "--bench-save") benchSave = next();
        else if (arg == "--bench-reroute") benchReroute = std::atoll(next().c_str());
        else if (arg == "--threads") setParallelThreads(static_cast<usize>(std::max(0, std::atoi(next().c_str()))));
        else if (arg == "--ai") settings.aiCompetitors = std::clamp(std::atoi(next().c_str()), 0, 3);
        else if (arg == "--inflation") settings.inflation = true;
        else if (arg == "--no-recessions") settings.recessions = false;
        else if (arg == "--land") {
            static constexpr const char* kKeys[] = {"continents", "mainland", "islands", "archipelago", "island"};
            const std::string v = next();
            const auto it = std::find(std::begin(kKeys), std::end(kKeys), v);
            if (it == std::end(kKeys)) {
                std::cerr << "--land takes continents, mainland, islands, archipelago or island\n";
                return EXIT_FAILURE;
            }
            settings.terrain.landmass = static_cast<Landmass>(it - std::begin(kKeys));
        } else if (arg == "--climate") {
            static constexpr const char* kKeys[] = {"temperate", "arid", "arctic", "tropical", "varied"};
            const std::string v = next();
            const auto it = std::find(std::begin(kKeys), std::end(kKeys), v);
            if (it == std::end(kKeys)) {
                std::cerr << "--climate takes temperate, arid, arctic, tropical or varied\n";
                return EXIT_FAILURE;
            }
            settings.terrain.climate = static_cast<Climate>(it - std::begin(kKeys));
        } else if (arg == "--mountains") settings.terrain.mountains = static_cast<u8>(std::clamp(std::atoi(next().c_str()), 0, kTerrainAmountMax));
        else if (arg == "--lakes") settings.terrain.lakes = static_cast<u8>(std::clamp(std::atoi(next().c_str()), 0, kTerrainAmountMax));
        else if (arg == "--rivers") settings.terrain.rivers = static_cast<u8>(std::clamp(std::atoi(next().c_str()), 0, kRiverAmountMax));
        else {
            std::cerr << "usage: transport-poly-server [--port N] [--seed N] [--load file] [--name text] [--password text] [--goal-value money] [--goal-years N] "
                         "[--autosave months] [--ai 0-3] [--inflation] [--no-recessions] [--threads N]\n"
                         "       [--land continents|mainland|islands|archipelago|island] [--climate temperate|arid|arctic|tropical|varied]\n"
                         "       [--mountains 0-4] [--lakes 0-4] [--rivers 0-3]\n"
                         "       transport-poly-server --load file --bench ticks [--bench-reroute ticks] [--bench-save file]\n";
            return EXIT_FAILURE;
        }
    }

    std::unique_ptr<World> world;
    if (!loadPath.empty()) {
        std::string err;
        CompanyId ignored;
        world = loadGame(loadPath, ignored, err);
        if (!world) {
            log::error("Cannot load {}: {}", loadPath, err);
            return EXIT_FAILURE;
        }
        log::info("Loaded {}", loadPath);
    } else {
        world = std::make_unique<World>(settings);
        // Explore the area around the origin so joining players find towns straight away.
        for (i32 y = -5; y <= 5; ++y)
            for (i32 x = -5; x <= 5; ++x) world->activateChunk({x, y});
        log::info("New world, seed {}, {} towns", settings.seed, world->towns.size());
    }
    if (benchTicks > 0) return runBenchmark(*world, benchTicks, benchSave, benchReroute);
    log::info("Simulation work runs on {} thread(s)", parallelThreads());

    net::NetServer server(std::move(world), options);
    std::string err;
    if (!server.start(err)) {
        log::error("{}", err);
        return EXIT_FAILURE;
    }
    std::thread console(consoleThread);
    console.detach();

    auto last = std::chrono::steady_clock::now();
    i32 lastMonth = server.server().world().clock.date().month;
    i32 monthsSinceSave = 0;
    while (!g_quit) {
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        server.update(dt);
        for (const std::string& e : server.takeEvents()) log::info("{}", e);
        for (const auto& c : server.takeChat()) log::info("<{}> {}", c.from, c.text);

        World& w = server.server().world();
        const i32 month = w.clock.date().month;
        if (month != lastMonth) {
            lastMonth = month;
            if (autosaveMonths > 0 && ++monthsSinceSave >= autosaveMonths) {
                monthsSinceSave = 0;
                std::string saveErr;
                if (saveGame(w, CompanyId{}, "saves/server-autosave.tpsave", saveErr))
                    log::info("Autosaved ({})", formatDate(w.clock.day()));
                else
                    log::warn("Autosave failed: {}", saveErr);
            }
        }

        std::queue<std::string> lines;
        {
            std::scoped_lock lock(g_consoleMutex);
            std::swap(lines, g_consoleLines);
        }
        while (!lines.empty()) {
            const std::string line = lines.front();
            lines.pop();
            if (line == "quit" || line == "exit") {
                g_quit = true;
            } else if (line == "status") {
                log::info("{}  tick {}  towns {}  people {}  vehicles {}  companies {}  clients {}",
                          formatDate(w.clock.day()), w.clock.tick, w.towns.size(), w.people.size(), w.vehicles.size(),
                          w.companies.size(), server.clients().size());
                for (const auto& c : server.clients()) log::info("  #{} {} (company {})", c.id, c.name, c.company);
            } else if (line.rfind("save ", 0) == 0) {
                std::string saveErr;
                const std::string path = line.substr(5);
                if (saveGame(w, CompanyId{}, path, saveErr)) log::info("Saved {}", path);
                else log::warn("Save failed: {}", saveErr);
            } else if (line.rfind("speed ", 0) == 0) {
                server.server().setSpeed(std::atoi(line.substr(6).c_str()));
                log::info("Speed {}", server.server().speed());
            } else if (line.rfind("say ", 0) == 0) {
                server.broadcastChat("Server", line.substr(4));
            } else if (line == "pause" || line == "unpause") {
                server.server().setSpeed(line == "pause" ? 0 : 1);
                if (line == "pause") log::info("Paused");
                else log::info("Running");
            } else if (line.rfind("kick ", 0) == 0) {
                // kick <player id or name> [reason]
                std::string who = line.substr(5), reason;
                if (const auto space = who.find(' '); space != std::string::npos) {
                    reason = who.substr(space + 1);
                    who = who.substr(0, space);
                }
                bool done = false;
                for (const auto& c : server.clients())
                    if (std::to_string(c.id) == who || c.name == who) done = server.kick(c.id, reason.empty() ? "Removed by the server" : reason);
                if (done) log::info("Removed {}", who);
                else log::info("No player {}", who);
            } else if (line == "companies") {
                for (const auto [id, c] : w.companies) {
                    std::string who;
                    for (const auto& p : server.clients())
                        if (p.company == id) who += (who.empty() ? "" : ", ") + p.name;
                    log::info("  #{} {}  rating {} ({})  value {}  cash {}{}{}", id.value, c.name, c.rating, league::title(c.rating),
                              formatMoney(league::companyValue(w, id)), formatMoney(c.money), server.companyLocked(id) ? "  [password]" : "",
                              who.empty() ? std::string() : "  players: " + who);
                }
            } else if (line == "password" || line.rfind("password ", 0) == 0) {
                const std::string password = line.size() > 9 ? line.substr(9) : std::string();
                server.setPassword(password);
                if (password.empty()) log::info("The server no longer needs a password");
                else log::info("New players need the password");
            } else if (line.rfind("unlock ", 0) == 0) {
                server.setCompanyPassword(CompanyId(static_cast<u8>(std::atoi(line.substr(7).c_str()))), {});
                log::info("Company {} unlocked", line.substr(7));
            } else if (!line.empty()) {
                log::info("Commands: status, companies, save <file>, speed <0-16>, pause, unpause, say <text>,");
                log::info("          kick <id or name> [reason], password [text], unlock <company id>, quit");
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    server.stop();
    log::info("Server stopped");
    return EXIT_SUCCESS;
}
