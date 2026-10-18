#include "client/app.hpp"
#include "client/terrain_options.hpp"
#include "client/demo_station.hpp"
#include "render/person_pose.hpp"
#include "client/ui/widgets.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "core/calendar.hpp"
#include "core/log.hpp"
#include "core/png.hpp"
#include "core/serialize.hpp"
#include "net/net_client.hpp"
#include "core/math.hpp"
#include "core/version.hpp"
#include "render/gl.hpp"
#include "render/vehicle_pose.hpp"
#include "sim/road.hpp"
#include "sim/water_path.hpp"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <format>
#include <string_view>
#include <vector>

namespace tp {

namespace {

void glfwErrorCallback(int code, const char* description) { log::error("GLFW error {}: {}", code, description); }

App* appFrom(GLFWwindow* w) { return static_cast<App*>(glfwGetWindowUserPointer(w)); }

// Finds a dry spot close to `start` for the camera.
TilePos findLandNear(const TerrainGenerator& gen, TilePos start) {
    for (i32 r = 0; r < 4000; r += 8) {
        for (i32 i = 0; i < 16; ++i) {
            const double a = i * 6.2831853 / 16.0;
            const TilePos p{start.x + static_cast<i32>(std::cos(a) * r), start.y + static_cast<i32>(std::sin(a) * r)};
            const int h = gen.cornerHeight(p.x, p.y);
            if (h >= 2 && h <= 8) return p;
        }
    }
    return start;
}

// High ground near `s0`, for automated screenshots of mountains.
std::optional<TilePos> findMountainNear(const TerrainGenerator& g, TilePos s0) {
    for (i32 r = 0; r < 30000; r += 32) {
        for (i32 i = 0; i < 64; ++i) {
            const double a = i * 6.2831853 / 64.0;
            const TilePos p{s0.x + static_cast<i32>(std::cos(a) * r), s0.y + static_cast<i32>(std::sin(a) * r)};
            if (g.cornerHeight(p.x, p.y) >= 40) return p;
        }
    }
    return std::nullopt;
}

std::optional<TilePos> findCoastNear(const TerrainGenerator& g, TilePos s0) {
    for (i32 r = 0; r < 20000; r += 16) {
        for (i32 i = 0; i < 64; ++i) {
            const double a = i * 6.2831853 / 64.0;
            const TilePos p{s0.x + static_cast<i32>(std::cos(a) * r), s0.y + static_cast<i32>(std::sin(a) * r)};
            if (g.rawHeight(p.x, p.y) >= -2 * kHeightOne) continue;
            for (i32 k = 0; k < r; k += 2) {
                const TilePos q{p.x - static_cast<i32>(std::cos(a) * k), p.y - static_cast<i32>(std::sin(a) * k)};
                if (g.rawHeight(q.x, q.y) > 0) return q;
            }
        }
    }
    return std::nullopt;
}

// Builds a small bus line in the biggest town (used for automated screenshots / smoke tests).
void buildDemoNetwork(World& w, CompanyId company) {
    const Town* town = nullptr;
    for (const auto [id, t] : w.towns)
        if (!town || t.population > town->population) town = &t;
    if (!town) return;
    const auto run = [&](CommandPayload p) {
        Command c;
        c.company = company;
        c.payload = std::move(p);
        return executeCommand(w, c);
    };
    std::vector<TilePos> stops;
    for (const TilePos r : town->roads) {
        const Tile* t = w.map.tile(r);
        if (!t || t->type != TileType::Road || !isStraight(roadPieces(*t)) || !w.map.slope(r).isFlat()) continue;
        bool farEnough = true;
        for (const TilePos s : stops) farEnough = farEnough && manhattan(s.x - r.x, s.y - r.y) >= 9;
        if (farEnough && run(CmdBuildRoadStop{r, false}).ok) stops.push_back(r);
        if (stops.size() >= 4) break;
    }
    if (stops.size() < 2) return;
    DepotId depot;
    for (const TilePos r : town->roads) {
        for (const Dir d : kDirs) {
            const CommandResult res = run(CmdBuildRoadDepot{neighbour(r, d), opposite(d), false});
            if (res.ok) {
                depot = DepotId(res.createdId);
                break;
            }
        }
        if (depot.valid()) break;
    }
    if (!depot.valid()) return;
    std::vector<Order> orders;
    for (const TilePos s : stops) {
        Order o;
        o.station = StationId(w.map.tile(s)->index);
        orders.push_back(o);
    }
    for (int i = 0; i < 4; ++i) {
        const CommandResult buy = run(CmdBuyVehicle{depot, 0});
        if (!buy.ok) break;
        const VehicleId v(buy.createdId);
        std::rotate(orders.begin(), orders.begin() + 1, orders.end());
        run(CmdSetOrders{v, orders});
        run(CmdStartStopVehicle{v});
    }
}

// Builds a short railway with two terminus stations near the biggest town and starts a train on
// it. Returns the first station's tile (used for automated screenshots / smoke tests).
std::optional<TilePos> buildDemoRailway(World& w, CompanyId company, bool parked = false) {
    if (parked) w.companies[company].money = 10'000'000;
    const Town* town = nullptr;
    for (const auto [id, t] : w.towns)
        if (!town || t.population > town->population) town = &t;
    if (!town) return std::nullopt;
    const auto cmd = [&](CommandPayload p) {
        Command c;
        c.company = company;
        c.payload = std::move(p);
        return c;
    };
    const auto run = [&](CommandPayload p) { return executeCommand(w, cmd(std::move(p))); };
    const auto test = [&](CommandPayload p) { return testCommand(w, cmd(std::move(p))).ok; };
    constexpr i32 kPlatform = 4;
    // Candidate east-west platforms: four clear, flat tiles at one level.
    std::vector<TilePos> sites;
    for (i32 dy = -36; dy <= 36; ++dy) {
        for (i32 dx = -40; dx <= 40; ++dx) {
            const TilePos p{town->centre.x + dx, town->centre.y + dy};
            if (test(CmdBuildRailStation{p, {p.x + kPlatform - 1, p.y}})) sites.push_back(p);
        }
    }
    std::ranges::sort(sites, {}, [&](TilePos p) {
        return std::abs(p.x - town->centre.x) + std::abs(p.y - town->centre.y);
    });
    for (const TilePos a : sites) {
        if (!test(CmdBuildRailDepot{{a.x - 2, a.y}, Dir::E})) continue;
        for (const TilePos b : sites) {
            const i32 gap = b.x - (a.x + kPlatform);
            if (b.y != a.y || gap < 16 || gap > 48) continue;
            const TilePos from{a.x + kPlatform, a.y}, to{b.x - 1, b.y};
            if (!test(CmdBuildRail{from, to})) continue;
            if (!run(CmdBuildRailStation{a, {a.x + kPlatform - 1, a.y}}).ok) return std::nullopt;
            run(CmdBuildRailStation{b, {b.x + kPlatform - 1, b.y}});
            run(CmdBuildRail{from, to});
            for (i32 x = from.x + 4; x < to.x - 2; x += 8) run(CmdCycleSignal{{x, a.y}});
            const CommandResult dep = run(CmdBuildRailDepot{{a.x - 2, a.y}, Dir::E});
            if (!dep.ok) return a;
            // Newest engine and coaches on sale.
            CommandResult buy = run(CmdBuyVehicle{DepotId(dep.createdId), 8});
            if (!buy.ok) buy = run(CmdBuyVehicle{DepotId(dep.createdId), 7});
            if (!buy.ok) return a;
            const VehicleId train(buy.createdId);
            for (int i = 0; i < 2; ++i)
                if (!run(CmdAddCarriage{train, 12}).ok) run(CmdAddCarriage{train, 11});
            Order oa, ob;
            oa.station = StationId(w.map.tile(a)->index);
            ob.station = StationId(w.map.tile(b)->index);
            run(CmdSetOrders{train, {ob, oa}});
            if (parked) {
                run(CmdAddCarriage{train,19});
                run(CmdAddCarriage{train,7});
                run(CmdReorderConsist{train,{0,1,3,2,4}});
            } else run(CmdStartStopVehicle{train});
            return a;
        }
    }
    return std::nullopt;
}

// Lays a free-form railway near `centre` the way the tools do: a station, a chain of curved
// track through the hills, a second station, a depot and a train running between the stations.
// Returns the start of the line (used for automated screenshots / smoke tests).
std::optional<TPoint> buildDemoTrack(World& w, CompanyId company, TilePos centre) {
    const auto run = [&](CommandPayload p) {
        Command c;
        c.company = company;
        c.payload = std::move(p);
        return executeCommand(w, c);
    };
    const auto offset = [](TPoint p, double dx, double dz) {
        return TPoint{p.x + static_cast<i32>(dx * kTrackUnits), p.z + static_cast<i32>(dz * kTrackUnits)};
    };
    // The open end of the track at `p`, continuing outwards.
    const auto endAt = [&](TPoint p) {
        TrackAnchor a;
        a.node = track::nodeNear(w, p, 40);
        a.pos = p;
        if (a.node.valid()) {
            const TrackNode& n = w.trackNodes[a.node];
            a.pos = n.pos;
            a.into = n.count(0) == 0 ? n.side(0) : n.side(1);
        }
        return a;
    };
    // Offsets (tiles) of the successive ends relative to the start: a lazy S through the hills.
    static constexpr double kPath[][2] = {{20, 0}, {32, 5}, {44, 14}, {54, 16}, {66, 10}};
    for (i32 r = 0; r < 40; r += 4) {
        for (i32 i = 0; i < 8; ++i) {
            const double ang = i * 6.2831853 / 8.0;
            const TPoint start = offset({}, centre.x - 30 + std::cos(ang) * r, centre.y - 8 + std::sin(ang) * r);
            TrackAnchor from;
            from.pos = start;
            const CommandResult stationA = run(CmdBuildTrackStation{from, offset(start, 6, 0)});
            if (!stationA.ok) continue;
            TPoint end = offset(start, 6, 0);
            for (const auto& step : kPath) {
                const TPoint target = offset(start, step[0], step[1]);
                TrackAnchor to;
                to.pos = target;
                if (const CommandResult res = run(CmdBuildTrack{endAt(end), to}); !res.ok) {
                    log::info("Demo track stops early: {}", res.error);
                    break;
                }
                end = target;
            }
            // The second station on level ground a little beyond the end, then track joining them.
            const TrackAnchor last = endAt(end);
            const auto ahead = [&](i64 tiles) {
                return TPoint{last.pos.x + static_cast<i32>(i64(last.into.x) * tiles * kTrackUnits / kDirOne),
                              last.pos.z + static_cast<i32>(i64(last.into.z) * tiles * kTrackUnits / kDirOne)};
            };
            const auto placeStation = [&](World& world, i64 gap) {
                TrackAnchor platformStart;
                platformStart.pos = ahead(gap);
                Command c;
                c.company = company;
                c.payload = CmdBuildTrackStation{platformStart, ahead(gap + 6)};
                CommandResult station = executeCommand(world, c);
                if (!station.ok) return station;
                TrackAnchor join;
                join.node = track::nodeNear(world, platformStart.pos, 40);
                join.pos = platformStart.pos;
                c.payload = CmdBuildTrack{last, join};
                if (const CommandResult res = executeCommand(world, c); !res.ok) return res;
                return station;
            };
            // Tried on a copy first: the gap needed to ease the line down (or up) to the station varies.
            CommandResult stationB = CommandResult::failure("No room for the second station");
            const std::vector<u8> saved = toBytes(w, kSaveVersion);
            for (i64 gap = 3; gap <= 24; gap += 3) {
                World trial;
                fromBytes(saved, trial, kSaveVersion);
                if (placeStation(trial, gap).ok) {
                    stationB = placeStation(w, gap);
                    break;
                }
            }
            const CommandResult dep = run(CmdBuildTrackDepot{track::nodeNear(w, start, 40)});
            if (!stationB.ok || !dep.ok) {
                log::info("Demo railway incomplete: {}", stationB.ok ? dep.error : stationB.error);
                return start;
            }
            CommandResult buy = run(CmdBuyVehicle{DepotId(dep.createdId), 8});
            if (!buy.ok) buy = run(CmdBuyVehicle{DepotId(dep.createdId), 7});
            if (!buy.ok) return start;
            const VehicleId train(buy.createdId);
            for (int k = 0; k < 2; ++k)
                if (!run(CmdAddCarriage{train, 12}).ok) run(CmdAddCarriage{train, 11});
            Order oa, ob;
            oa.station = StationId(stationA.createdId);
            ob.station = StationId(stationB.createdId);
            run(CmdSetOrders{train, {ob, oa}});
            run(CmdStartStopVehicle{train});
            return start;
        }
    }
    return std::nullopt;
}

// Finds a straight line near `centre` that needs the longest bridge (or tunnel) and builds it.
// Returns the middle of that structure (used for automated screenshots / smoke tests).
std::optional<TPoint> buildDemoStructure(World& w, CompanyId company, TilePos centre, TrackStructure kind) {
    TrackPlan best;
    TrackAnchor bestFrom, bestTo;
    i32 bestLength = 2 * kTrackUnits;
    for (i32 dy = -40; dy <= 40; dy += 5)
        for (i32 dx = -40; dx <= 40; dx += 5)
            for (int d = 0; d < 8; ++d) {
                const double ang = d * 6.2831853 / 8.0;
                TrackAnchor from, to;
                from.pos = {(centre.x + dx) * kTrackUnits + kTrackUnits / 2, (centre.y + dy) * kTrackUnits + kTrackUnits / 2};
                to.pos = {from.pos.x + static_cast<i32>(std::cos(ang) * 24 * kTrackUnits),
                          from.pos.z + static_cast<i32>(std::sin(ang) * 24 * kTrackUnits)};
                const TrackPlan plan = track::plan(w, company, from, to);
                const i32 length = kind == TrackStructure::Bridge ? plan.bridgeLength : plan.tunnelLength;
                if (plan.ok && length > bestLength) {
                    best = plan;
                    bestLength = length;
                    bestFrom = from;
                    bestTo = to;
                }
            }
    if (!best.ok) return std::nullopt;
    Command c;
    c.company = company;
    c.payload = CmdBuildTrack{bestFrom, bestTo};
    if (!executeCommand(w, c).ok) return std::nullopt;
    for (const TrackSpan& sp : best.spans)
        if (sp.kind == kind) {
            // A bridge from its middle; a tunnel from just outside its first portal.
            const i32 look = kind == TrackStructure::Bridge ? (sp.s0 + sp.s1) / 2 : std::max(0, sp.s0 - kTrackUnits);
            for (const TrackSample& sm : best.samples)
                if (sm.s >= look) return sm.p;
        }
    return bestFrom.pos;
}

// Lays a straight line across a straight road near `centre`, with a level crossing. Returns the
// crossing (used for automated screenshots / smoke tests).
std::optional<TPoint> buildDemoCrossing(World& w, CompanyId company, TilePos centre) {
    for (i32 r = 0; r < 70; ++r)
        for (i32 dy = -r; dy <= r; ++dy)
            for (i32 dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const TilePos t{centre.x + dx, centre.y + dy};
                const Tile* tile = w.map.tile(t);
                if (!tile || tile->type != TileType::Road) continue;
                const bool ns = allRoadPieces(*tile) == (dirBit(Dir::N) | dirBit(Dir::S));
                const bool ew = allRoadPieces(*tile) == (dirBit(Dir::E) | dirBit(Dir::W));
                if (!ns && !ew) continue;
                const TPoint mid{t.x * kTrackUnits + kTrackUnits / 2, t.y * kTrackUnits + kTrackUnits / 2};
                for (const i32 skew : {0, 2, -2}) {
                    TrackAnchor from, to;
                    const i32 run = 9 * kTrackUnits, side = skew * kTrackUnits;
                    from.pos = ns ? TPoint{mid.x - run, mid.z - side} : TPoint{mid.x - side, mid.z - run};
                    to.pos = ns ? TPoint{mid.x + run, mid.z + side} : TPoint{mid.x + side, mid.z + run};
                    const TrackPlan plan = track::plan(w, company, from, to);
                    if (!plan.ok || plan.levelCrossings == 0) continue;
                    Command c;
                    c.company = company;
                    c.payload = CmdBuildTrack{from, to};
                    if (executeCommand(w, c).ok) return mid;
                }
            }
    // No crossable road nearby: lay a country road first, then cross it.
    for (i32 r = 4; r < 40; r += 2)
        for (int k = 0; k < 16; ++k)
            for (const bool ns : {true, false}) {
                const TilePos a{centre.x + static_cast<i32>(std::cos(k * 0.3927) * r),
                                centre.y + static_cast<i32>(std::sin(k * 0.3927) * r)};
                const TilePos b = ns ? TilePos{a.x, a.y + 10} : TilePos{a.x + 10, a.y};
                Command c;
                c.company = company;
                c.payload = CmdBuildRoad{a, b, false};
                if (!executeCommand(w, c).ok) continue;
                const TPoint mid{(a.x + b.x) * kTrackUnits / 2 + kTrackUnits / 2, (a.y + b.y) * kTrackUnits / 2 + kTrackUnits / 2};
                TrackAnchor from, to;
                from.pos = ns ? TPoint{mid.x - 9 * kTrackUnits, mid.z - kTrackUnits} : TPoint{mid.x - kTrackUnits, mid.z - 9 * kTrackUnits};
                to.pos = ns ? TPoint{mid.x + 9 * kTrackUnits, mid.z + kTrackUnits} : TPoint{mid.x + kTrackUnits, mid.z + 9 * kTrackUnits};
                const TrackPlan plan = track::plan(w, company, from, to);
                if (plan.ok && plan.levelCrossings > 0) {
                    c.payload = CmdBuildTrack{from, to};
                    if (executeCommand(w, c).ok) return mid;
                }
                c.payload = CmdRemoveRoad{a, b, false};
                executeCommand(w, c);
            }
    return std::nullopt;
}

// Builds two docks on a coast near `centre`, a shipyard and a ferry between them. Returns the
// first dock's tile (used for automated screenshots / smoke tests).
std::optional<TilePos> buildDemoFerry(World& w, CompanyId company, TilePos centre) {
    const auto cmd = [&](CommandPayload p) {
        Command c;
        c.company = company;
        c.payload = std::move(p);
        return c;
    };
    const auto run = [&](CommandPayload p) { return executeCommand(w, cmd(std::move(p))); };
    struct Site {
        TilePos tile;
        Dir facing;
    };
    std::vector<Site> sites;
    for (i32 dy = -60; dy <= 60; ++dy) {
        for (i32 dx = -60; dx <= 60; ++dx) {
            const TilePos p{centre.x + dx, centre.y + dy};
            for (const Dir d : kDirs)
                if (testCommand(w, cmd(CmdBuildDock{p, d})).ok) {
                    sites.push_back({p, d});
                    break;
                }
        }
    }
    const auto dist = [&](TilePos p) { return std::abs(p.x - centre.x) + std::abs(p.y - centre.y); };
    std::ranges::sort(sites, {}, [&](const Site& s) { return dist(s.tile); });
    for (usize i = 0; i < sites.size() && i < 40; ++i) {
        const Site a = sites[i];
        for (const Site& b : sites) {
            const i32 d = std::abs(b.tile.x - a.tile.x) + std::abs(b.tile.y - a.tile.y);
            if (d < 14 || d > 50) continue;
            const TilePos goals[] = {neighbour(b.tile, b.facing)};
            const auto path = findWaterPath(w, neighbour(a.tile, a.facing), goals, 20'000);
            if (!path || path->size() < 10) continue;
            if (!run(CmdBuildDock{a.tile, a.facing}).ok || !run(CmdBuildDock{b.tile, b.facing}).ok) return std::nullopt;
            // A shipyard next to the first dock.
            CommandResult yard;
            for (const Site& s : sites) {
                if (std::abs(s.tile.x - a.tile.x) + std::abs(s.tile.y - a.tile.y) > 6 || s.tile == a.tile) continue;
                yard = run(CmdBuildShipDepot{s.tile, s.facing});
                if (yard.ok) break;
            }
            if (!yard.ok) return a.tile;
            u16 model = 0;
            for (u16 m = 0; m < vehicleModels().size(); ++m)
                if (vehicleModel(m).type == VehicleType::Ship && vehicleModel(m).introYear <= w.clock.date().year) model = m;
            const CommandResult buy = run(CmdBuyVehicle{DepotId(yard.createdId), model});
            if (!buy.ok) return a.tile;
            const VehicleId ship(buy.createdId);
            Order oa, ob;
            oa.station = StationId(w.map.tile(a.tile)->index);
            ob.station = StationId(w.map.tile(b.tile)->index);
            run(CmdSetOrders{ship, {ob, oa}});
            run(CmdStartStopVehicle{ship});
            return a.tile;
        }
    }
    return std::nullopt;
}

} // namespace

void App::onScroll(double dy) { scrollAccum_ += dy; }

void App::onFramebufferSize(int width, int height) { camera_.setViewport(width, height); }

bool App::parseArgs(int argc, char** argv) {
    if (argc > 0 && argv[0]) exeDir_ = std::filesystem::path(argv[0]).parent_path().string();
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg != "--no-debug" && arg != "--screenshot" && arg != "--frames" && arg != "--time" && arg != "--open" &&
            arg != "--land" && arg != "--climate" && arg != "--clouds" && arg != "--mist" && arg != "--mountains" && arg != "--lakes" && arg != "--rivers")
            directStart_ = true;
        const auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (arg == "--screenshot") {
            if (const char* v = next()) autoScreenshot_ = v;
        } else if (arg == "--profile") {
            if (const char* v = next()) profilePath_ = v;
        } else if (arg == "--frames") {
            if (const char* v = next()) autoFrames_ = std::max(1, std::atoi(v));
        } else if (arg == "--seed") {
            if (const char* v = next()) seed_ = std::strtoull(v, nullptr, 10);
        } else if (arg == "--time") {
            if (const char* v = next()) fixedTimeOfDay_ = std::atof(v) * 60.0;
        } else if (arg == "--camera") {
            // x,z,distance,yawDegrees[,pitchDegrees]
            if (const char* v = next()) {
                double x = 0, z = 0, d = 70, yaw = 35, pitch = -1;
                const int n = std::sscanf(v, "%lf,%lf,%lf,%lf,%lf", &x, &z, &d, &yaw, &pitch);
                if (n >= 2) startCamera_ = glm::dvec4(x, z, d, yaw);
                if (n >= 5) startPitchDeg_ = pitch;
            }
        } else if (arg == "--goto-town") {
            if (const char* v = next()) gotoTownDistance_ = std::max(6.0, std::atof(v));
        } else if (arg == "--land") {
            // New world terrain: --land continents|mainland|islands|archipelago|island,
            // --mountains 0-4, --lakes 0-4, --rivers 0-3.
            if (const char* v = next(); v && landmassByKey(v)) preferences_.landmass = static_cast<int>(*landmassByKey(v));
        } else if (arg == "--clouds" || arg == "--mist") {
            // Automation: fixed weather, 0-1 (--clouds is cloud cover).
            if (const char* v = next())
                (arg == "--clouds" ? renderer_.settings.forceCloudCover : renderer_.settings.forceMist) = static_cast<float>(std::atof(v));
        } else if (arg == "--climate") {
            if (const char* v = next(); v && climateByKey(v)) preferences_.climate = static_cast<int>(*climateByKey(v));
        } else if (arg == "--mountains" || arg == "--lakes" || arg == "--rivers") {
            if (const char* v = next()) {
                int& amount = arg == "--mountains" ? preferences_.mountains : arg == "--lakes" ? preferences_.lakes : preferences_.rivers;
                amount = std::atoi(v);
                preferences_.clamp();
            }
        } else if (arg == "--goto-water") {
            gotoWater_ = true;
        } else if (arg == "--goto-mountain") {
            gotoMountain_ = true;
        } else if (arg == "--open") {
            if (const char* v = next()) openWindows_ = v;
        } else if (arg == "--follow") {
            followFirst_ = true;
        } else if (arg == "--demo-network") {
            demoNetwork_ = true;
        } else if (arg == "--demo-rail") {
            demoRail_ = true;
        } else if (arg == "--demo-track") {
            demoTrack_ = true;
        } else if (arg == "--demo-station") {
            demoStation_ = true;
        } else if (arg == "--preview-consist" || arg == "--preview-consist-popup") {
            previewConsist_ = true;
            previewConsistPopup_ = arg == "--preview-consist-popup";
            demoRail_ = true;
            openWindows_ = "depot";
        } else if (arg == "--preview-station") {
            previewStation_ = true;
        } else if (arg == "--preview-orders") {
            previewOrders_ = true;
        } else if (arg == "--demo-bridge") {
            demoStructure_ = TrackStructure::Bridge;
        } else if (arg == "--demo-tunnel") {
            demoStructure_ = TrackStructure::Tunnel;
        } else if (arg == "--demo-crossing") {
            demoCrossing_ = true;
        } else if (arg == "--demo-ships") {
            demoShips_ = true;
        } else if (arg == "--join") {
            if (const char* v = next()) joinAddress_ = v;
        } else if (arg == "--no-debug") {
            showDebug_ = false;
        } else {
            log::warn("Unknown argument '{}'", arg);
        }
    }
    return true;
}

void App::captureScreenshot(const std::string& path) {
    const int w = camera_.viewportWidth();
    const int h = camera_.viewportHeight();
    std::vector<u8> pixels(static_cast<usize>(w) * static_cast<usize>(h) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    // OpenGL rows are bottom-up; PNG wants top-down.
    const usize row = static_cast<usize>(w) * 3;
    std::vector<u8> tmp(row);
    for (int y = 0; y < h / 2; ++y) {
        u8* a = pixels.data() + row * static_cast<usize>(y);
        u8* b = pixels.data() + row * static_cast<usize>(h - 1 - y);
        std::copy(a, a + row, tmp.data());
        std::copy(b, b + row, a);
        std::copy(tmp.data(), tmp.data() + row, b);
    }
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
    if (writePng(path, w, h, 3, pixels))
        log::info("Saved screenshot {}", path);
    else
        log::error("Failed to save screenshot {}", path);
}

bool App::init() {
    preferences_.load("settings.cfg");
    audio_.setEnabled(preferences_.sounds);
    renderer_.settings.shadows = preferences_.shadows;
    renderer_.settings.weather = preferences_.weather;
    renderer_.settings.maxViewDistance = static_cast<float>(preferences_.viewDistance);
    daylightOnly_ = preferences_.daylightOnly;
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    if (autoScreenshot_) {
        // Automated runs must not steal keyboard focus from whatever the user is doing.
        glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    }

    window_ = glfwCreateWindow(1600, 900, "transport-poly", nullptr, nullptr);
    if (!window_) return false;
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(preferences_.vsync ? 1 : 0);
    glfwSetWindowUserPointer(window_, this);
    glfwSetScrollCallback(window_, [](GLFWwindow* w, double, double dy) { appFrom(w)->onScroll(dy); });
    glfwSetFramebufferSizeCallback(
        window_, [](GLFWwindow* w, int width, int height) { appFrom(w)->onFramebufferSize(width, height); });

    if (!gl::load(reinterpret_cast<gl::LoadProc>(glfwGetProcAddress))) return false;
    log::info("OpenGL {} on {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
              reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    glEnable(GL_MULTISAMPLE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "imgui.ini";
    float xs = 1.0f, ys = 1.0f;
    glfwGetWindowContentScale(window_, &xs, &ys);
    uiScale_ = std::max(1.0f, xs);
    // The UI font ships next to the executable; fall back to the working directory.
    std::filesystem::path font = std::filesystem::path(exeDir_) / "fonts" / "Roboto-Medium.ttf";
    std::error_code ec;
    if (!std::filesystem::exists(font, ec)) font = std::filesystem::path("fonts") / "Roboto-Medium.ttf";
    ui::setup(font, uiScale_);
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    if (!renderer_.init()) {
        log::error("Renderer initialisation failed");
        return false;
    }
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window_, &fbw, &fbh);
    camera_.setViewport(fbw, fbh);
    if (!joinAddress_.empty()) {
        std::string host = joinAddress_;
        u16 port = net::kDefaultPort;
        if (const auto colon = host.rfind(':'); colon != std::string::npos) {
            port = static_cast<u16>(std::atoi(host.substr(colon + 1).c_str()));
            host = host.substr(0, colon);
        }
        startSession(std::make_unique<net::NetClientSession>(host, port, "Player"));
    } else if (directStart_) {
        newGame(seed_);
    }
    return true;
}

void App::newGame(u64 seed) {
    seed_ = seed;
    WorldSettings settings;
    settings.startYear = preferences_.startYear;
    settings.interestPercent = preferences_.interestPercent;
    settings.inflation = preferences_.inflation;
    settings.recessions = preferences_.recessions;
    settings.aiCompetitors = preferences_.aiCompetitors;
    settings.terrain = terrainSettings(preferences_);
    settings.seed = seed;
    startSession(std::make_unique<LocalSession>(settings));
    if (demoStation_) {
        selStation_ = buildDemoStation(world(),session_->localCompany());
        if (!startCamera_) startCamera_=glm::dvec4(25,24,26,25);
        renderer_.invalidateAll();
    }
}

void App::shutdown() {
    session_.reset();
    preview_.reset();
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    if (window_) glfwDestroyWindow(window_);
    glfwTerminate();
}

void App::updateCamera(double dt) {
    if (followPerson_) {
        const Person* p = world().people.tryGet(selPerson_);
        if (!p || p->serial != selPersonSerial_) followPerson_ = false;
        else if (const auto pose = gfx::personPose(world(), *p, session_->tickFraction())) camera_.targetGoal = pose->pos;
    }
    if (const Vehicle* v = world().vehicles.tryGet(followVehicle_)) {
        if (const auto pose = gfx::vehiclePose(world(), *v, session_->tickFraction())) camera_.targetGoal = pose->pos;
    } else {
        followVehicle_ = {};
    }
    if (autoScreenshot_) {
        // Ignore all input during automated screenshots, except a scripted cursor.
        hover_ = {};
        mouseOverUi_ = true;
        if (previewHover_) {
            hover_ = gfx::pickTerrain(world().map, gfx::Ray{{previewHover_->x, 400.0, previewHover_->z}, {0.0, -1.0, 0.0}});
            mouseOverUi_ = false;
        }
        camera_.targetGoal.y = gfx::terrainHeightAt(world().map, camera_.targetGoal.x, camera_.targetGoal.z);
        camera_.update(dt);
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(window_, &mx, &my);
    // Cursor coordinates are in window units; convert to framebuffer pixels for HiDPI.
    int ww = 1, wh = 1;
    glfwGetWindowSize(window_, &ww, &wh);
    const double sx = static_cast<double>(camera_.viewportWidth()) / std::max(ww, 1);
    const double sy = static_cast<double>(camera_.viewportHeight()) / std::max(wh, 1);
    if (!mouseInitialised_) {
        lastMouseX_ = mx;
        lastMouseY_ = my;
        mouseInitialised_ = true;
    }
    const double dx = mx - lastMouseX_;
    const double dy = my - lastMouseY_;
    lastMouseX_ = mx;
    lastMouseY_ = my;

    if (!io.WantCaptureKeyboard) {
        const double speed = camera_.distanceGoal * 1.1 * dt * preferences_.panPercent / 100.0;
        const auto down = [&](int key) { return glfwGetKey(window_, key) == GLFW_PRESS; };
        double right = 0.0, forward = 0.0;
        if (down(GLFW_KEY_W) || down(GLFW_KEY_UP)) forward += speed;
        if (down(GLFW_KEY_S) || down(GLFW_KEY_DOWN)) forward -= speed;
        if (down(GLFW_KEY_D) || down(GLFW_KEY_RIGHT)) right += speed;
        if (down(GLFW_KEY_A) || down(GLFW_KEY_LEFT)) right -= speed;
        if (right != 0.0 || forward != 0.0) {
            camera_.pan(right, forward);
            followVehicle_ = {};
            followPerson_ = false;
        }
        const double orbit = dt * preferences_.orbitPercent / 100.0;
        if (down(GLFW_KEY_Q)) camera_.orbit(-1.6 * orbit, 0.0);
        if (down(GLFW_KEY_E)) camera_.orbit(1.6 * orbit, 0.0);
        if (down(GLFW_KEY_R)) camera_.orbit(0.0, orbit);
        if (down(GLFW_KEY_F)) camera_.orbit(0.0, -orbit);
        if (down(GLFW_KEY_PAGE_UP)) camera_.zoom(std::pow(0.3, dt));
        if (down(GLFW_KEY_PAGE_DOWN)) camera_.zoom(std::pow(3.0, dt));
    }
    if (!io.WantCaptureMouse) {
        if (scrollAccum_ != 0.0) camera_.zoom(std::pow(0.85, scrollAccum_ * preferences_.zoomPercent / 100.0));
        if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
            camera_.orbit(dx * 0.006 * preferences_.orbitPercent / 100.0, dy * 0.004 * preferences_.orbitPercent / 100.0);
        if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
            const double k = camera_.distance * 0.0016 * preferences_.panPercent / 100.0;
            camera_.pan(-dx * k, dy * k);
            followPerson_ = false;
            followVehicle_ = {};
        }
        mouseRay_ = camera_.screenRay(mx * sx, my * sy);
        hover_ = gfx::pickTerrain(world().map, mouseRay_);
    } else {
        hover_ = {};
    }
    mouseOverUi_ = io.WantCaptureMouse;
    scrollAccum_ = 0.0;

    // Keep the orbit target on the ground.
    camera_.targetGoal.y = gfx::terrainHeightAt(world().map, camera_.targetGoal.x, camera_.targetGoal.z);
    camera_.update(dt);
}

void App::updateExploration(double now) {
    if (now - lastExplore_ < 0.15) return;
    lastExplore_ = now;
    const Map& map = world().map;
    std::erase_if(explorePending_, [&](const auto& kv) {
        return map.isActive(ChunkPos::fromKey(kv.first)) || now - kv.second > 3.0;
    });
    const double radius = std::clamp(camera_.distance * 1.5, 128.0, 260.0);
    const double cx = camera_.target.x, cz = camera_.target.z;
    struct Want {
        ChunkPos pos;
        double d2;
    };
    std::vector<Want> wanted;
    const i32 x0 = static_cast<i32>(std::floor((cx - radius) / kChunkSize));
    const i32 x1 = static_cast<i32>(std::floor((cx + radius) / kChunkSize));
    const i32 z0 = static_cast<i32>(std::floor((cz - radius) / kChunkSize));
    const i32 z1 = static_cast<i32>(std::floor((cz + radius) / kChunkSize));
    for (i32 z = z0; z <= z1; ++z) {
        for (i32 x = x0; x <= x1; ++x) {
            const ChunkPos p{x, z};
            if (map.isActive(p) || explorePending_.contains(p.key())) continue;
            const double dx = (x + 0.5) * kChunkSize - cx, dz = (z + 0.5) * kChunkSize - cz;
            const double d2 = dx * dx + dz * dz;
            if (d2 > radius * radius) continue;
            wanted.push_back({p, d2});
        }
    }
    if (wanted.empty()) return;
    std::sort(wanted.begin(), wanted.end(), [](const Want& a, const Want& b) { return a.d2 < b.d2; });
    CmdActivateChunks cmd;
    for (const Want& w : wanted) {
        if (cmd.chunks.size() >= 32) break;
        cmd.chunks.push_back(w.pos);
        explorePending_[w.pos.key()] = now;
    }
    session_->submit(std::move(cmd));
}

void App::processFeedback(double now) {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool mouseValid = mouse.x > -1000.0f && mouse.y > -1000.0f;
    for (const CommandFeedback& f : session_->takeFeedback()) {
        if (f.name != "Explore") audio_.play(f.result.ok);
        if (f.result.ok) {
            if (f.seq == pendingBuySeq_ && f.result.createdId != 0xFFFFFFFFu) {
                selVehicle_ = VehicleId(f.result.createdId);
                pendingBuySeq_ = 0;
            }
            if (f.result.cost != 0) {
                // Costs float up from the mouse pointer, where the action happened.
                Toast t{f.result.cost > 0 ? std::format("-{}", formatMoney(f.result.cost))
                                          : std::format("+{}", formatMoney(-f.result.cost)),
                        now + 1.6, false};
                t.born = now;
                t.x = mouseValid ? mouse.x : ImGui::GetIO().DisplaySize.x * 0.5f;
                t.y = mouseValid ? mouse.y : ImGui::GetIO().DisplaySize.y * 0.5f;
                toasts_.push_back(std::move(t));
            }
        } else if (f.name != "Explore") {
            toasts_.push_back({std::format("{}: {}", f.name, f.result.error), now + 4.0, true});
        }
    }
    while (toasts_.size() > 10) toasts_.pop_front();
}

void App::drawToasts(double now) {
    std::erase_if(toasts_, [&](const Toast& t) { return t.expires < now; });
    if (toasts_.empty()) return;
    const float s = uiScale_;
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float cardY = vp->WorkPos.y + vp->WorkSize.y - dockHeight_ - 24.0f * s;
    ImFont* small = ui::fonts().small;
    ImFont* body = ui::fonts().body;
    // Newest card at the bottom, older ones stacked above it.
    for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it) {
        Toast& t = *it;
        if (t.born == 0.0) t.born = now;
        const float age = static_cast<float>(now - t.born);
        const float life = static_cast<float>(t.expires - t.born);
        const float fadeIn = std::min(1.0f, age / 0.15f);
        const float fadeOut = std::min(1.0f, static_cast<float>(t.expires - now) / 0.35f);
        const float alpha = std::clamp(std::min(fadeIn, fadeOut), 0.0f, 1.0f);
        if (t.x >= 0.0f) {
            // Floating cost label.
            const float k = std::clamp(age / std::max(0.1f, life), 0.0f, 1.0f);
            const bool income = !t.text.empty() && t.text[0] == '+';
            const ImU32 c = income ? ui::col::kGreen : ui::col::kAccent;
            const ImVec2 ts = small->CalcTextSizeA(small->FontSize, FLT_MAX, 0.0f, t.text.c_str());
            const ImVec2 at(t.x + 14.0f * s, t.y - 18.0f * s - k * 46.0f * s);
            const ImVec2 mn(at.x - 8.0f * s, at.y - 3.0f * s), mx(at.x + ts.x + 8.0f * s, at.y + ts.y + 3.0f * s);
            fg->AddRectFilled(mn, mx, ui::withAlpha(ui::col::kBar, 0.9f * alpha), (mx.y - mn.y) * 0.5f);
            fg->AddRect(mn, mx, ui::withAlpha(c, 0.6f * alpha), (mx.y - mn.y) * 0.5f);
            fg->AddText(small, small->FontSize, at, ui::withAlpha(c, alpha), t.text.c_str());
            continue;
        }
        // Card: errors in red, notices in green.
        const ImU32 c = t.error ? ui::col::kRed : ui::col::kGreen;
        const float wrap = 460.0f * s;
        const ImVec2 ts = body->CalcTextSizeA(body->FontSize, FLT_MAX, wrap, t.text.c_str());
        const float w = ts.x + 54.0f * s, h = std::max(ts.y, 18.0f * s) + 20.0f * s;
        const float slide = (1.0f - fadeIn) * 12.0f * s;
        const ImVec2 mn(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f, cardY - h + slide);
        const ImVec2 mx(mn.x + w, mn.y + h);
        ui::dropShadow(fg, mn, mx, 10.0f * s, 12.0f, 0.3f * alpha);
        fg->AddRectFilled(mn, mx, ui::withAlpha(ui::col::kBar, 0.97f * alpha), 10.0f * s);
        fg->AddRectFilled(mn, ImVec2(mn.x + 4.0f * s, mx.y), ui::withAlpha(c, alpha), 10.0f * s,
                          ImDrawFlags_RoundCornersLeft);
        ui::drawIcon(fg, t.error ? ui::Icon::Warning : ui::Icon::Check, ImVec2(mn.x + 24.0f * s, (mn.y + mx.y) * 0.5f),
                     18.0f * s, ui::withAlpha(c, alpha));
        fg->AddText(body, body->FontSize, ImVec2(mn.x + 42.0f * s, mn.y + (h - ts.y) * 0.5f),
                    ui::withAlpha(ui::col::kText, alpha), t.text.c_str(), nullptr, wrap);
        cardY = mn.y - 8.0f * s;
    }
}

std::optional<glm::vec2> App::worldToScreen(const glm::dvec3& p) const {
    const glm::dvec3 origin = camera_.target;
    const glm::dvec4 clip = camera_.viewProjectionD(origin) * glm::dvec4(p - origin, 1.0);
    if (clip.w <= 0.01) return std::nullopt;
    const glm::dvec3 ndc = glm::dvec3(clip) / clip.w;
    if (ndc.z > 1.0 || ndc.x < -1.2 || ndc.x > 1.2 || ndc.y < -1.2 || ndc.y > 1.2) return std::nullopt;
    const ImGuiIO& io = ImGui::GetIO();
    return glm::vec2(static_cast<float>((ndc.x + 1.0) * 0.5 * io.DisplaySize.x),
                     static_cast<float>((1.0 - ndc.y) * 0.5 * io.DisplaySize.y));
}

void App::drawTownLabels() {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const World& w = world();
    const double maxDist = camera_.distance * 3.0 + 150.0;
    for (const auto [id, t] : w.towns) {
        const double dx = t.centre.x + 0.5 - camera_.target.x;
        const double dz = t.centre.y + 0.5 - camera_.target.z;
        if (dx * dx + dz * dz > maxDist * maxDist) continue;
        const double y = w.map.cornerHeight(t.centre.x, t.centre.y) * gfx::kHeightStep + 2.5;
        const auto sp = worldToScreen({t.centre.x + 0.5, y, t.centre.y + 0.5});
        if (!sp) continue;
        const ImU32 dot = t.cls == TownClass::City ? ui::col::kAccent
                          : t.cls == TownClass::Town ? ui::col::kBlue
                                                     : ui::col::kLeaf;
        const float scale = t.cls == TownClass::City ? 1.15f : (t.cls == TownClass::Town ? 1.05f : 0.95f);
        const std::string pop = std::format("{}", t.population);
        ui::worldLabel(dl, ImVec2(sp->x, sp->y), t.name.c_str(), pop.c_str(), dot, scale);
    }
    // Station signs when zoomed in.
    if (camera_.distance > 110.0) return;
    if (camera_.distance < 100) for (const auto& industry : w.industries) {
        const auto* b = w.buildings.tryGet(industry.building.id);
        if (!b || b->serial != industry.building.serial) continue;
        const double dx = b->pos.x-camera_.target.x, dz = b->pos.y-camera_.target.z;
        if (dx*dx + dz*dz > 10000) continue;
        const double y = gfx::terrainHeightAt(w.map, b->pos.x+0.5, b->pos.y+0.5)+1.5;
        const auto sp = worldToScreen({b->pos.x+0.5, y, b->pos.y+0.5});
        if (!sp) continue;
        const char* chain = b->type == 12 ? "Produces grain" : b->type == 11 ? "Grain -> food" :
                            b->type == 10 ? "Food -> goods" : "Accepts food / goods";
        const auto detail = std::format("{} | G:{} F:{} C:{}", chain, industry.stock[0], industry.stock[1], industry.stock[2]);
        ui::worldLabel(dl, {sp->x,sp->y}, b->info().name.data(), detail.c_str(), ui::col::kLeaf, 0.8f);
    }
    const double stationDist = camera_.distance * 2.0 + 40.0;
    for (const auto [id, st] : w.stations) {
        const double dx = st.sign.x + 0.5 - camera_.target.x;
        const double dz = st.sign.y + 0.5 - camera_.target.z;
        if (dx * dx + dz * dz > stationDist * stationDist) continue;
        const double y = gfx::terrainHeightAt(w.map, st.sign.x + 0.5, st.sign.y + 0.5) + 1.1;
        const auto sp = worldToScreen({st.sign.x + 0.5, y, st.sign.y + 0.5});
        if (!sp) continue;
        const Company* c = w.companies.tryGet(st.owner);
        const ImU32 dot = ui::rgb(companyColourRgb(c ? c->colour : 14));
        const usize waiting = st.waitingCount();
        const std::string detail = waiting > 0 ? std::format("{} waiting", waiting) : std::string();
        ui::worldLabel(dl, ImVec2(sp->x, sp->y), st.name.c_str(), waiting > 0 ? detail.c_str() : nullptr, dot, 0.85f);
    }
}

void App::drawHud() {
    World& w = world();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float s = uiScale_;
    const float rowH = 32.0f * s;
    ui::beginBar("##hud", ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 10.0f * s), ImVec2(0.5f, 0.0f),
                 ImVec2(8, 6), 18.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto separator = [&] {
        ImGui::SameLine(0, 8.0f * s);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(1.0f, rowH));
        dl->AddLine(ImVec2(pos.x, pos.y + 7.0f * s), ImVec2(pos.x, pos.y + rowH - 7.0f * s), ui::col::kBorderStrong);
        ImGui::SameLine(0, 8.0f * s);
    };
    // An icon and a text, vertically centred in the bar, with an optional tooltip.
    const auto item = [&](const char* id, ui::Icon icon, ImU32 iconCol, const char* text, ImU32 textCol,
                          const ui::Tip& tip) {
        ImFont* f = ui::fonts().body;
        const ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.0f, text);
        const float iconSz = 17.0f * s;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(iconSz + 7.0f * s + ts.x, rowH));
        ui::drawIcon(dl, icon, ImVec2(pos.x + iconSz * 0.5f, pos.y + rowH * 0.5f), iconSz, iconCol);
        dl->AddText(f, f->FontSize, ImVec2(pos.x + iconSz + 7.0f * s, pos.y + (rowH - ts.y) * 0.5f), textCol, text);
        ui::itemTooltip(tip);
        return clicked;
    };

    if (ui::iconButton("##menu", ui::Icon::Menu, 32, showGameMenu_, ui::col::kAccent,
                       {"Game menu", "Save the game, or leave it", "Esc"}))
        showGameMenu_ = !showGameMenu_;
    separator();
    const std::string date = formatDate(w.clock.day());
    item("##date", ui::Icon::Calendar, ui::col::kTextMuted, date.c_str(), ui::col::kText, {});
    ImGui::SameLine(0, 14.0f * s);
    const i32 minute = static_cast<i32>(w.clock.minuteOfDay());
    const bool daytime = daylightOnly_ || (minute >= 6 * 60 && minute < 19 * 60);
    const std::string time = formatTimeOfDay(w.clock.minuteOfDay());
    if (item("##time", daytime ? ui::Icon::Sun : ui::Icon::Moon, daytime ? ui::col::kAccent : ui::col::kBlue, time.c_str(),
         daylightOnly_ ? ui::col::kAccent : ui::col::kText,
         {daylightOnly_ ? "Always daylight" : "Day / night cycle",
          daylightOnly_ ? "Click to restore the day / night cycle. The game clock continues normally."
                        : "Click to keep the world in daylight. The game clock continues normally."}))
    {
        daylightOnly_ = !daylightOnly_;
        preferences_.daylightOnly = daylightOnly_;
        preferences_.save("settings.cfg");
    }
    if (const Company* co = w.companies.tryGet(session_->localCompany())) {
        separator();
        const std::string money = formatMoney(co->money);
        const std::string tipBody = std::format("Loan {}. Click to open the finances.", formatMoney(co->loan));
        item("##money", ui::Icon::Coins, co->money < 0 ? ui::col::kRed : ui::col::kGreen, money.c_str(),
             co->money < 0 ? ui::col::kRed : ui::col::kText, {"Bank balance", tipBody.c_str(), "F"});
        if (ImGui::IsItemClicked()) showFinances_ = !showFinances_;
        ImGui::SameLine(0, 14.0f * s);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImFont* f = ui::fonts().body;
        const ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.0f, co->name.c_str());
        ImGui::Dummy(ImVec2(16.0f * s + ts.x, rowH));
        dl->AddCircleFilled(ImVec2(pos.x + 6.0f * s, pos.y + rowH * 0.5f), 6.0f * s, ui::rgb(companyColourRgb(co->colour)));
        dl->AddText(f, f->FontSize, ImVec2(pos.x + 18.0f * s, pos.y + (rowH - ts.y) * 0.5f), ui::col::kTextMuted,
                    co->name.c_str());
    }
    separator();
    static constexpr int kSpeeds[] = {0, 1, 2, 4, 8, 16};
    static constexpr ui::Segment kSegments[] = {{nullptr, ui::Icon::Pause, "Pause (Space)"},
                                                {"1x", ui::Icon::None, "Normal speed"},
                                                {"2x", ui::Icon::None, "Double speed"},
                                                {"4x", ui::Icon::None, "Four times as fast"},
                                                {"8x", ui::Icon::None, "Eight times as fast"},
                                                {"16x", ui::Icon::None, "Sixteen times as fast"}};
    int current = -1;
    for (int i = 0; i < 6; ++i)
        if (session_->speed() == kSpeeds[i]) current = i;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f * s);
    ImGui::BeginDisabled(!session_->canControlSpeed());
    if (const int clicked = ui::segmented("##speed", kSegments, current, 28.0f); clicked >= 0)
        session_->setSpeed(kSpeeds[clicked]);
    ImGui::EndDisabled();
    if (!session_->canControlSpeed() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("The host controls pause and game speed. Commands wait until play resumes.");
    const ImVec2 barPos = ImGui::GetWindowPos(), barSize = ImGui::GetWindowSize();
    ui::endBar();

    if (session_->speed() == 0) {
        // Paused badge under the bar.
        ImDrawList* fg = ImGui::GetBackgroundDrawList();
        ImFont* f = ui::fonts().small;
        const char* text = "PAUSED";
        const ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.0f, text);
        const float pulse = 0.75f + 0.25f * std::sin(static_cast<float>(ImGui::GetTime()) * 4.0f);
        const ImVec2 mn(barPos.x + (barSize.x - ts.x) * 0.5f - 12.0f * s, barPos.y + barSize.y + 6.0f * s);
        const ImVec2 mx(mn.x + ts.x + 24.0f * s, mn.y + ts.y + 6.0f * s);
        fg->AddRectFilled(mn, mx, ui::withAlpha(ui::col::kAccent, 0.9f * pulse), (mx.y - mn.y) * 0.5f);
        fg->AddText(f, f->FontSize, ImVec2(mn.x + 12.0f * s, mn.y + 3.0f * s), ui::col::kTextOnAccent, text);
    }
}

void App::handleHotkeys() {
    if (autoScreenshot_) return;
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || io.KeyCtrl || io.KeyAlt) return;
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) quicksave();
    const auto toggleGroup = [&](ToolGroup g) {
        toolGroup_ = toolGroup_ == g ? ToolGroup::None : g;
        tool_ = Tool::None;
        dragStart_.reset();
    };
    if (ImGui::IsKeyPressed(ImGuiKey_1, false)) toggleGroup(ToolGroup::Land);
    if (ImGui::IsKeyPressed(ImGuiKey_2, false)) toggleGroup(ToolGroup::Road);
    if (ImGui::IsKeyPressed(ImGuiKey_3, false)) toggleGroup(ToolGroup::Tram);
    if (ImGui::IsKeyPressed(ImGuiKey_4, false)) toggleGroup(ToolGroup::Rail);
    if (ImGui::IsKeyPressed(ImGuiKey_5, false)) toggleGroup(ToolGroup::Water);
    if (ImGui::IsKeyPressed(ImGuiKey_V, false)) showVehicleList_ = !showVehicleList_;
    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) showFinances_ = !showFinances_;
    if (ImGui::IsKeyPressed(ImGuiKey_T, false)) showTownList_ = !showTownList_;
    if (ImGui::IsKeyPressed(ImGuiKey_N, false)) showNews_ = !showNews_;
    if (ImGui::IsKeyPressed(ImGuiKey_M, false)) showMinimap_ = !showMinimap_;
    if (ImGui::IsKeyPressed(ImGuiKey_C, false)) showCitizens_ = !showCitizens_;
    // Escape peels back one layer at a time: the active tool (handled by handleTools), the
    // tool flyout, open windows and finally the game menu.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !escConsumed_) {
        if (toolGroup_ != ToolGroup::None) {
            toolGroup_ = ToolGroup::None;
            escConsumed_ = true;
        } else if (selVehicle_.valid() || selDepot_.valid() || selStation_.valid() || selTown_.valid()) {
            if (followVehicle_ == selVehicle_) followVehicle_ = {};
            selVehicle_ = {};
            selDepot_ = {};
            selStation_ = {};
            selTown_ = {};
            escConsumed_ = true;
        }
    }
}

void App::drawDebugUi() {
    if (!showDebug_) return;
    const gfx::RenderStats& st = renderer_.stats();
    World& w = world();
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::Begin("Debug", &showDebug_, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("%s %s", kGameName.data(), kVersionString.data());
    ImGui::Text("%.1f FPS (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
    ImGui::Text("Camera %.1f, %.1f  dist %.0f", camera_.target.x, camera_.target.z, camera_.distance);
    ImGui::Text("Chunks: %d visible, %d cached, %d built", st.chunksVisible, st.chunksCached, st.meshesBuilt);
    ImGui::Text("Meshing: %.2f ms, worker wait %.2f ms", st.meshCpuMs, st.meshWaitMs);
    ImGui::Text("Triangles: %zu  Trees: %zu  Buildings: %zu", st.trianglesDrawn, st.treesDrawn, st.buildingsDrawn);
    ImGui::Text("Towns %zu  people %zu  buildings %zu", w.towns.size(), w.people.size(), w.buildings.size());
    ImGui::Text("Tick %lld  explored chunks %zu", static_cast<long long>(w.clock.tick), w.map.activeChunks().size());
    ImGui::Text("State hash %016llx", static_cast<unsigned long long>(w.stateHash()));
    ImGui::Separator();
    ImGui::Checkbox("Shadows", &renderer_.settings.shadows);
    ImGui::SameLine();
    ImGui::Checkbox("Wireframe", &renderer_.settings.wireframe);
    ImGui::SliderFloat("View distance", &renderer_.settings.maxViewDistance, 120.0f, 900.0f, "%.0f");
    static int seedInput = 20261017;
    ImGui::InputInt("Seed", &seedInput);
    ImGui::SameLine();
    if (ImGui::Button("New game")) newGame(static_cast<u64>(static_cast<u32>(seedInput)));
    ImGui::Separator();
    if (hover_.hit) {
        const TileCorners c = w.map.corners(hover_.tile);
        ImGui::Text("Tile (%d, %d)  corners %d %d %d %d", hover_.tile.x, hover_.tile.y, c.h[0], c.h[1], c.h[2], c.h[3]);
        if (const Tile* t = w.map.tile(hover_.tile)) {
            ImGui::Text("type %d ground %d trees %d road %x", static_cast<int>(t->type), static_cast<int>(t->ground),
                        t->trees, t->road);
            if (t->type == TileType::Building) {
                if (const Building* b = w.buildings.tryGet(BuildingId(t->index))) {
                    const Town* town = w.towns.tryGet(b->town);
                    ImGui::Text("%s (%s)", b->info().name.data(), town ? town->name.c_str() : "?");
                    ImGui::Text("residents %d  workers %d/%d  customers %d", b->residents, b->workers, b->info().jobs,
                                b->customers);
                }
            }
        } else {
            ImGui::TextUnformatted("Unexplored");
        }
    }
    ImGui::End();
}

int App::run(int argc, char** argv) {
    log::info("{} {} starting", kGameName, kVersionString);
    parseArgs(argc, argv);
    if (!init()) {
        shutdown();
        return EXIT_FAILURE;
    }
    if (gotoMountain_) {
        const TilePos s0{static_cast<i32>(camera_.target.x), static_cast<i32>(camera_.target.z)};
        if (const auto peak = findMountainNear(world().map.generator(), s0))
            startCamera_ = glm::dvec4(peak->x, peak->y, startCamera_ ? startCamera_->z : 90.0,
                                      startCamera_ ? startCamera_->w : 35.0);
    }
    if (gotoWater_) {
        const TilePos s0{static_cast<i32>(camera_.target.x), static_cast<i32>(camera_.target.z)};
        if (const auto coast = findCoastNear(world().map.generator(), s0))
            startCamera_ = glm::dvec4(coast->x, coast->y, startCamera_ ? startCamera_->z : 70.0,
                                      startCamera_ ? startCamera_->w : 35.0);
    }
    if (startCamera_) {
        const glm::dvec4 c = *startCamera_;
        camera_.targetGoal = glm::dvec3(c.x, gfx::terrainHeightAt(world().map, c.x, c.y), c.y);
        camera_.distanceGoal = c.z;
        camera_.yawGoal = glm::radians(c.w);
        if (startPitchDeg_ > 0.0) camera_.pitchGoal = glm::radians(startPitchDeg_);
        camera_.snapToGoals();
    }
    std::ofstream profile;
    if (!profilePath_.empty()) {
        profile.open(profilePath_);
        profile << "frame,simulation_ms,cpu_frame_ms,meshing_ms,worker_wait_ms,chunks_built,people_drawn\n";
    }
    int frameCount = 0;
    double last = glfwGetTime();
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        const double now = glfwGetTime();
        const double dt = std::min(now - last, 0.1);
        last = now;
        timeSeconds_ += dt;

        const auto cpuStart = std::chrono::steady_clock::now();
        double simulationMs = 0;
        if (session_) {
            session_->update(dt);
            simulationMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-cpuStart).count();
            if (session_->ready()) processFeedback(now);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        escConsumed_ = false;
        if (ImGui::IsKeyPressed(ImGuiKey_F12)) pendingScreenshot_ = true;

        if (screen_ == Screen::MainMenu || !session_) {
            if (!openWindows_.empty() && frameCount == 30) {
                // Automation: show a page of the main menu.
                if (openWindows_.find("newgame") != std::string::npos) menuPage_ = MenuPage::NewGame;
                if (openWindows_.find("load") != std::string::npos) menuPage_ = MenuPage::Load;
                if (openWindows_.find("join") != std::string::npos) menuPage_ = MenuPage::Join;
            }
            updatePreview(dt);
            drawMainMenu();
            if (preview_) renderer_.render(preview_->world(), camera_, 15.5 * 60.0, timeSeconds_, preview_->tickFraction(), {});
        } else if (!session_->ready()) {
            glViewport(0, 0, camera_.viewportWidth(), camera_.viewportHeight());
            glClearColor(0.12f, 0.14f, 0.18f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            drawConnecting();
        } else {
            if (!cameraPlaced_) placeCameraAtStart();
            if (preview_) {
                preview_.reset();
                renderer_.invalidateAll();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F3)) showDebug_ = !showDebug_;
            if (!ImGui::GetIO().WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Space) && session_->canControlSpeed())
                session_->setSpeed(session_->speed() == 0 ? 1 : 0);
            if (gotoTownDistance_ > 0.0 && frameCount == 90) {
                // Jump to the most populous town found so far (used for automated screenshots).
                const Town* best = nullptr;
                for (const auto [id, t] : world().towns)
                    if (!best || t.population > best->population) best = &t;
                if (best) {
                    camera_.targetGoal = glm::dvec3(best->centre.x + 0.5, 0.0, best->centre.y + 0.5);
                    camera_.targetGoal.y =
                        gfx::terrainHeightAt(world().map, camera_.targetGoal.x, camera_.targetGoal.z);
                    camera_.distanceGoal = gotoTownDistance_;
                    camera_.snapToGoals();
                }
            }
            if (demoNetwork_ && frameCount == 100) {
                if (auto* local = dynamic_cast<LocalSession*>(session_.get()))
                    buildDemoNetwork(local->world(), local->localCompany());
            }
            if (demoRail_ && frameCount >= 100 && frameCount % 20 == 0 && !world().towns.empty()) {
                // Retried until a town has been founded near the camera.
                demoRail_ = false;
                if (auto* local = dynamic_cast<LocalSession*>(session_.get())) {
                    if (const auto at = buildDemoRailway(local->world(), local->localCompany(), previewConsist_)) {
                        camera_.targetGoal = glm::dvec3(at->x + 2.0, 0.0, at->y + 0.5);
                        camera_.targetGoal.y =
                            gfx::terrainHeightAt(world().map, camera_.targetGoal.x, camera_.targetGoal.z);
                        camera_.distanceGoal = 14.0;
                        camera_.snapToGoals();
                    } else {
                        log::warn("No site found for the demo railway");
                    }
                }
            }
            if (demoTrack_ && frameCount >= 160 && frameCount % 20 == 0) {
                if (auto* local = dynamic_cast<LocalSession*>(session_.get())) {
                    const TilePos centre{static_cast<i32>(std::floor(camera_.target.x)),
                                         static_cast<i32>(std::floor(camera_.target.z))};
                    if (const auto at = buildDemoTrack(local->world(), local->localCompany(), centre)) {
                        demoTrack_ = false;
                        camera_.targetGoal = glm::dvec3(at->x / double(kTrackUnits) + 30.0, 0.0, at->z / double(kTrackUnits) + 8.0);
                        camera_.targetGoal.y = gfx::terrainHeightAt(world().map, camera_.targetGoal.x, camera_.targetGoal.z);
                        camera_.snapToGoals();
                        if (previewOrders_) {
                            // Show the order picker pointing at the first station's platform.
                            for (const auto [vid, v] : world().vehicles) {
                                selVehicle_ = vid;
                                break;
                            }
                            tool_ = Tool::PickOrder;
                            previewHover_ = glm::dvec3(at->x / double(kTrackUnits) + 3.0, 0.0, at->z / double(kTrackUnits) + 0.3);
                            camera_.targetGoal = glm::dvec3(previewHover_->x, 0.0, previewHover_->z);
                            camera_.targetGoal.y = gfx::terrainHeightAt(world().map, camera_.targetGoal.x, camera_.targetGoal.z);
                            camera_.distanceGoal = 14.0;
                            camera_.snapToGoals();
                        }
                        if (previewStation_) {
                            // Show the station tool part way through placing a platform beside the line.
                            const TPoint p{at->x, at->z + 7 * kTrackUnits};
                            tool_ = Tool::RailStation;
                            TrackAnchor a;
                            a.pos = p;
                            stationStart_ = a;
                            previewHover_ = glm::dvec3(p.x / double(kTrackUnits) + 7.0, 0.0, p.z / double(kTrackUnits) + 1.0);
                            camera_.targetGoal = glm::dvec3(p.x / double(kTrackUnits) + 3.5, 0.0, p.z / double(kTrackUnits));
                            camera_.targetGoal.y = gfx::terrainHeightAt(world().map, camera_.targetGoal.x, camera_.targetGoal.z);
                            camera_.distanceGoal = 16.0;
                            camera_.snapToGoals();
                        }
                    } else if (frameCount >= 600) {
                        demoTrack_ = false;
                        log::warn("No site found for the demo track");
                    }
                }
            }
            if (demoCrossing_ && frameCount >= 200) {
                if (auto* local = dynamic_cast<LocalSession*>(session_.get())) {
                    const TilePos centre{static_cast<i32>(std::floor(camera_.target.x)),
                                         static_cast<i32>(std::floor(camera_.target.z))};
                    if (const auto at = buildDemoCrossing(local->world(), local->localCompany(), centre)) {
                        camera_.targetGoal = glm::dvec3(at->x / double(kTrackUnits), 0.0, at->z / double(kTrackUnits));
                        camera_.targetGoal.y = gfx::terrainHeightAt(world().map, camera_.targetGoal.x, camera_.targetGoal.z);
                        camera_.distanceGoal = 7.0;
                        camera_.snapToGoals();
                    } else {
                        log::warn("No site found for the demo level crossing");
                    }
                    demoCrossing_ = false;
                }
            }
            if (demoStructure_ && frameCount >= 200) {
                if (auto* local = dynamic_cast<LocalSession*>(session_.get())) {
                    const TilePos centre{static_cast<i32>(std::floor(camera_.target.x)),
                                         static_cast<i32>(std::floor(camera_.target.z))};
                    if (const auto at = buildDemoStructure(local->world(), local->localCompany(), centre, *demoStructure_)) {
                        camera_.targetGoal = glm::dvec3(at->x / double(kTrackUnits), 0.0, at->z / double(kTrackUnits));
                        camera_.targetGoal.y = gfx::terrainHeightAt(world().map, camera_.targetGoal.x, camera_.targetGoal.z);
                        camera_.distanceGoal = *demoStructure_ == TrackStructure::Bridge ? 18.0 : 9.0;
                        camera_.snapToGoals();
                    } else {
                        log::warn("No site found for the demo bridge or tunnel");
                    }
                    demoStructure_.reset();
                }
            }
            if (demoShips_ && frameCount >= 160 && frameCount % 20 == 0) {
                // Retried for a while: the coast around the camera is explored over several frames.
                if (auto* local = dynamic_cast<LocalSession*>(session_.get())) {
                    const TilePos centre{static_cast<i32>(std::floor(camera_.target.x)),
                                         static_cast<i32>(std::floor(camera_.target.z))};
                    if (const auto at = buildDemoFerry(local->world(), local->localCompany(), centre)) {
                        demoShips_ = false;
                        camera_.targetGoal = glm::dvec3(at->x + 0.5, 0.0, at->y + 0.5);
                        camera_.targetGoal.y =
                            gfx::terrainHeightAt(world().map, camera_.targetGoal.x, camera_.targetGoal.z);
                        camera_.distanceGoal = 14.0;
                        camera_.snapToGoals();
                    } else if (frameCount >= 600) {
                        demoShips_ = false;
                        log::warn("No site found for the demo ferry");
                    }
                }
            }
            if (!openWindows_.empty() && frameCount >= 400) {
                // Automation: open windows for screenshots once the demo network exists.
                const auto wants = [&](std::string_view name) { return openWindows_.find(name) != std::string::npos; };
                World& w = world();
                if (wants("finances")) showFinances_ = true;
                if (wants("vehicles")) showVehicleList_ = true;
                if (wants("orders"))
                    for (const auto [id, v] : w.vehicles) { selVehicle_ = id; break; }
                if (wants("towns")) showTownList_ = true;
                if (wants("news")) showNews_ = true;
                if (wants("depot"))
                    for (const auto [id, d] : w.depots) {
                        selDepot_ = id;
                        break;
                    }
                if (wants("station"))
                    for (const auto [id, st] : w.stations) {
                        selStation_ = id;
                        break;
                    }
                if (wants("town")) {
                    const Town* best = nullptr;
                    for (const auto [id, t] : w.towns)
                        if (!best || t.population > best->population) {
                            best = &t;
                            selTown_ = id;
                        }
                }
                if (wants("rail")) toolGroup_ = ToolGroup::Rail;
                if (wants("water")) toolGroup_ = ToolGroup::Water;
                if (wants("settings")) showSettings_ = true;
                if (wants("citizens")) showCitizens_ = true;
                if (wants("zones")) overlayMode_ = 3;
                if (wants("flow")) overlayMode_ = 2;
                if (wants("catchments")) overlayMode_ = 1;
                if (wants("gamemenu")) showGameMenu_ = true;
                openWindows_.clear();
            }
            if (followFirst_ && frameCount >= 160) {
                // Follow the first vehicle out on the network as soon as there is one.
                for (const auto [id, v] : world().vehicles) {
                    if (v.state == VehicleState::InDepot) continue;
                    followVehicle_ = id;
                    selVehicle_ = id;
                    followFirst_ = false;
                    break;
                }
            }
            updateCamera(dt);
            updateExploration(now);
            autosave();

            std::vector<gfx::OverlayQuad> overlays;
            drawWorldOverlays(overlays);
            if (hover_.hit && tool_ == Tool::None)
                addTileOverlay(overlays, hover_.tile, gfx::rgba(255, 255, 255, 70), 0.04);
            // Catchment area of the station whose window is open.
            if (const Station* st = world().stations.tryGet(selStation_))
                addCatchmentOverlay(overlays, st->catchmentTiles(), st->catchment, gfx::rgba(90, 170, 255, 55));
            handleTools(overlays);
            handleHotkeys();
            drawTownLabels();

            drawHud();
            drawToolbar();
            drawWindows();
            drawDebugUi();
            drawChat();
            drawGameMenu();
            drawToasts(now);
            drawNews(now);
            drawMinimap(now);
            const double minute =
                daylightOnly_ ? 12.0 * 60.0 :
                (fixedTimeOfDay_ ? *fixedTimeOfDay_ : world().clock.exactMinuteOfDay(session_->tickFraction()));
            renderer_.render(world(), camera_, minute, timeSeconds_, session_->tickFraction(), overlays);
        }
        drawSettings();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (profile) {
            const auto& st = renderer_.stats();
            const auto cpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-cpuStart).count();
            profile << frameCount << ',' << simulationMs << ',' << cpuMs << ',' << st.meshCpuMs << ',' << st.meshWaitMs
                    << ',' << st.meshesBuilt << ',' << st.peopleDrawn << '\n';
        }
        ++frameCount;
        if (pendingScreenshot_) {
            pendingScreenshot_ = false;
            const auto wall = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
            captureScreenshot(std::format("screenshots/shot-{:%Y%m%d-%H%M%S}.png", wall));
        }
        if (autoScreenshot_ && frameCount >= autoFrames_) {
            captureScreenshot(*autoScreenshot_);
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
        glfwSwapBuffers(window_);
    }
    shutdown();
    return EXIT_SUCCESS;
}

} // namespace tp
