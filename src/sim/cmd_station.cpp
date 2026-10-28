#include "core/math.hpp"
#include "sim/command_handlers.hpp"
#include "sim/prices.hpp"
#include "sim/road.hpp"
#include "sim/world.hpp"
#include <algorithm>
#include <map>
#include <queue>

namespace tp::track {
std::vector<PlatformEnd> platformEnds(const World& w, TrackEdgeId platform) {
    const auto chain = platformChain(w, platform);
    std::vector<PlatformEnd> ends;
    for (const auto id : chain) {
        const auto& e = w.trackEdges[id];
        for (const auto& [node, side] : {std::pair{e.a, e.sideA}, std::pair{e.b, e.sideB}}) {
            const auto& n = w.trackNodes[node];
            bool interior = false;
            for (const auto other : n.edges)
                if (other != id && std::ranges::find(chain, other) != chain.end()) interior = true;
            if (!interior) ends.push_back({node, -n.side(side), id});
        }
    }
    std::ranges::sort(ends, [](const auto& a, const auto& b) { return a.node.value < b.node.value; });
    return ends;
}
} // namespace tp::track
namespace tp::cmd {
namespace {
constexpr i32 kEntranceReach = 64;
bool ours(const World& w, StationId id, CompanyId co) {
    const auto* s = w.stations.tryGet(id);
    return s && s->owner == co;
}
bool clear(const World& w, TilePos p, StationId id, CompanyId co) {
    const auto* t = w.map.tile(p);
    return t && !t->span && !w.map.slope(p).isSteep() &&
           ((t->type == TileType::Clear && (t->owner == kOwnerNone || t->owner == co.value)) ||
            (t->type == TileType::Footpath && t->index == id.value));
}
struct Approach {
    TPoint end;
    std::vector<TilePos> paving;
};
std::optional<Approach> platformApproach(const World& w, TilePos from, TrackEdgeId platform,
                                         CompanyId company) {
    const auto& edge = w.trackEdges[platform];
    const auto centre = tileCentre(from);
    const TrackSample* sample = &edge.samples.front();
    for (const auto& s : edge.samples)
        if (distance(s.p, centre) < distance(sample->p, centre)) sample = &s;
    if (std::abs(int(w.map.tileBaseHeight(from)) - sample->h / kSubLevels) > 1) return std::nullopt;
    const auto tangent = normalised(i64(w.trackNodes[edge.b].pos.x) - w.trackNodes[edge.a].pos.x,
                                    i64(w.trackNodes[edge.b].pos.z) - w.trackNodes[edge.a].pos.z);
    TDir normal{-tangent.z, tangent.x};
    if ((i64(centre.x) - sample->p.x) * normal.x + (i64(centre.z) - sample->p.z) * normal.z < 0)
        normal = -normal;
    const auto end = offset(sample->p, normal, 200);
    Approach approach{end, {}};
    const auto chain = track::platformChain(w, platform);
    const i64 steps = std::max<i64>(1, distance(centre, end) / 32);
    for (i64 n = 0; n <= steps; ++n) {
        const auto tile = tileAt({static_cast<i32>(centre.x + (i64(end.x) - centre.x) * n / steps),
                                  static_cast<i32>(centre.z + (i64(end.z) - centre.z) * n / steps)});
        if (clear(w, tile, edge.station, company)) {
            if (std::ranges::find(approach.paving, tile) == approach.paving.end())
                approach.paving.push_back(tile);
            continue;
        }
        const auto found = w.trackTiles.find(ChunkPos{tile.x, tile.y}.key());
        if (found == w.trackTiles.end() || std::ranges::none_of(found->second, [&](TrackEdgeId id) {
                return std::ranges::find(chain, id) != chain.end();
            }))
            return std::nullopt;
    }
    return approach;
}
} // namespace
CommandResult handle(World& w, const Command& c, const CmdStationEntrance& x, bool exec) {
    if (!ours(w, x.station, c.company)) return CommandResult::failure("Not your station");
    if (x.remove) {
        if (exec) stations::removeEntrances(w, x.station);
        return CommandResult::success(500);
    }
    auto& station = w.stations[x.station];
    if (!track::hasPlatform(w, x.station)) return CommandResult::failure("A rail platform is required");
    if (!station.takesPassengers()) return CommandResult::failure("Freight stations have no passenger entrance");
    const auto surfaceRoad = [&](TilePos p) {
        const auto* tile = w.map.tile(p);
        return tile && (tile->type == TileType::Road || tile->type == TileType::Station) &&
               allRoadPieces(*tile) && !tile->span;
    };
    TilePos road = x.road;
    std::optional<TilePos> plot;
    if (!surfaceRoad(road)) {
        if (!clear(w, x.road, x.station, c.company))
            return CommandResult::failure("Choose a road or a cleared building plot beside a road");
        bool found = false;
        for (const auto d : kDirs) {
            const auto adjacent = neighbour(x.road, d);
            if (surfaceRoad(adjacent)) {
                road = adjacent;
                plot = x.road;
                found = true;
                break;
            }
        }
        if (!found) return CommandResult::failure("The entrance plot must be directly beside a surface road");
    }
    if (station.entrances.size() >= 8) return CommandResult::failure("Station already has eight entrances");
    for (const auto& e : station.entrances)
        if (e.road == road) return CommandResult::failure("An entrance already connects to this road tile");
    std::map<TilePos, TilePos> previous;
    std::queue<TilePos> queue;
    for (auto d : kDirs) {
        const auto p = neighbour(road, d);
        if (plot && p != *plot) continue;
        if (clear(w, p, x.station, c.company) &&
            std::abs(int(w.map.tileBaseHeight(p)) - int(w.map.tileBaseHeight(road))) <= 1) {
            previous[p] = p;
            queue.push(p);
        }
    }
    std::optional<TilePos> finish;
    TrackEdgeId platform;
    if (queue.empty())
        return CommandResult::failure("Clear and level a plot beside this road for the entrance");
    std::optional<Approach> landing;
    while (!queue.empty() &&
           previous.size() < static_cast<usize>((2 * kEntranceReach + 1) * (2 * kEntranceReach + 1))) {
        const auto p = queue.front();
        queue.pop();
        platform = track::platformNear(w, x.station, tileCentre(p), 2 * kTrackUnits + 32);
        if (platform.valid() && (landing = platformApproach(w, p, platform, c.company))) {
            finish = p;
            break;
        }
        for (auto d : kDirs) {
            const auto next = neighbour(p, d);
            if (previous.contains(next) || chebyshev(next.x - road.x, next.y - road.y) > kEntranceReach ||
                !clear(w, next, x.station, c.company))
                continue;
            if (std::abs(int(w.map.tileBaseHeight(p)) - int(w.map.tileBaseHeight(next))) > 1) continue;
            previous[next] = p;
            queue.push(next);
        }
    }
    if (!finish)
        return CommandResult::failure("No walkable route to the platform within 64 tiles. Clear a continuous "
                                      "corridor and level steep ground");
    StationEntrance entry;
    entry.road = road;
    entry.platform = platform;
    for (auto p = *finish;; p = previous[p]) {
        entry.tiles.push_back(p);
        if (previous[p] == p) break;
    }
    std::reverse(entry.tiles.begin(), entry.tiles.end());
    const usize approachTiles = entry.tiles.size();
    for (const auto tile : landing->paving)
        if (std::ranges::find(entry.tiles, tile) == entry.tiles.end()) entry.tiles.push_back(tile);
    const auto first = tileCentre(entry.tiles.front());
    // Connect to the nearest corner of the road's pavement, rather than its carriageway.
    TPoint curb{};
    i64 best = INT64_MAX;
    for (i32 z : {40, 216})
        for (i32 xx : {40, 216}) {
            const TPoint p{road.x * kTrackUnits + xx, road.y * kTrackUnits + z};
            if (distance(p, first) < best) {
                curb = p;
                best = distance(p, first);
            }
        }
    entry.path.push_back(curb);
    for (usize n = 0; n < approachTiles; ++n) entry.path.push_back(tileCentre(entry.tiles[n]));
    entry.path.push_back(landing->end);
    Money cost = 5'000;
    for (auto p : entry.tiles) {
        cost += 500 + w.map.tile(p)->trees * prices::kRemoveTree;
        if (auto why = towns::authorityRefusal(w, c.company, p, kRatingRefuseBuild); !why.empty())
            return CommandResult::failure(why);
    }
    if (exec) {
        for (auto p : entry.tiles) {
            auto& t = *w.map.tile(p);
            t.type = TileType::Footpath;
            t.trees = 0;
            t.owner = c.company.value;
            t.index = x.station.value;
            w.map.touch(p);
        }
        station.entrances.push_back(std::move(entry));
        stations::rebuildIndex(w);
        passengers::markDirty(w);
    }
    return CommandResult::success(cost);
}
CommandResult handle(World& w, const Command& c, const CmdSetStationUse& x, bool exec) {
    if (!ours(w, x.station, c.company)) return CommandResult::failure("Not your station");
    Station& station = w.stations[x.station];
    if (!(station.facilities & kFacilityRail)) return CommandResult::failure("Only rail stations have a use");
    if (station.use == x.use) return CommandResult::failure("The station is used for that already");
    if (exec) {
        station.use = x.use;
        // Passengers find other ways to travel and freight left waiting is lost.
        if (!station.takesFreight()) station.cargoWaiting.clear();
        if (!station.takesPassengers()) stations::removeEntrances(w, x.station);
        stations::rebuildIndex(w);
        passengers::markDirty(w);
        ++w.trackRevision;
    }
    return CommandResult::success(0);
}
CommandResult handle(World& w, const Command& c, const CmdAddPlatform& x, bool exec) {
    if (!ours(w, x.station, c.company)) return CommandResult::failure("Not your station");
    const auto* e = w.trackEdges.tryGet(x.platform);
    if (!e || e->station != x.station || e->kind != TrackKind::Platform)
        return CommandResult::failure("Choose a platform");
    if ((x.side != 1 && x.side != -1) || x.length < 2 || x.length > 16)
        return CommandResult::failure("Invalid platform dimensions");
    const auto ends = track::platformEnds(w, x.platform);
    if (ends.size() != 2) return CommandResult::failure("The platform has junctions");
    const auto& n = w.trackNodes[ends[0].node];
    const auto direction = -ends[0].outward;
    const TDir normal{-direction.z, direction.x};
    TrackAnchor from;
    from.pos = offset(n.pos, normal, x.side * 2 * kTrackUnits);
    const auto to = offset(from.pos, direction, x.length * kTrackUnits);
    return handle(w, c, CmdBuildTrackStation{from, to, x.station}, exec);
}
CommandResult handle(World& w, const Command& c, const CmdExtendPlatform& x, bool exec) {
    if (!ours(w, x.station, c.company)) return CommandResult::failure("Not your station");
    if (x.length < 2 || x.length > 16) return CommandResult::failure("Choose 2 to 16 tiles");
    std::optional<track::PlatformEnd> end;
    for (const auto& chain : track::platformsOf(w, x.station)) {
        for (auto candidate : track::platformEnds(w, chain.front()))
            if (candidate.node == x.end) end = candidate;
    }
    if (!end) return CommandResult::failure("Choose a platform end");
    const auto& n = w.trackNodes[x.end];
    const auto& base = w.trackEdges[end->edge];
    const u8 inside = base.a == x.end ? base.sideA : base.sideB;
    const u8 outside = 1 - inside;
    for (const auto id : track::platformChain(w, end->edge))
        if (w.trackEdges[id].reservedBy.valid())
            return CommandResult::failure("A train is using the platform");
    if (n.count(outside) == 0) {
        TrackAnchor from;
        from.node = x.end;
        from.pos = n.pos;
        return handle(
            w, c, CmdBuildTrackStation{from, offset(n.pos, end->outward, x.length * kTrackUnits), x.station},
            exec);
    }
    if (n.count(outside) != 1)
        return CommandResult::failure("Remove the junction before extending the platform");
    const auto target = n.edges[outside * 2];
    auto& e = w.trackEdges[target];
    if (e.owner != c.company || e.kind != TrackKind::Plain || e.reservedBy.valid() || !e.spans.empty())
        return CommandResult::failure("The next track section is unavailable");
    if (w.trackNodes[e.a].h != w.trackNodes[e.b].h || e.minRadius != 0 ||
        std::ranges::any_of(e.samples, [&](const TrackSample& s) { return s.h != w.trackNodes[e.a].h; }))
        return CommandResult::failure("Platform extensions require straight, level track");
    const auto tiles = track::bedTiles(e.samples, kPlatformHalfWidth);
    Money cost = static_cast<Money>(e.length) * prices::kRailStationTile / kTrackUnits;
    for (auto p : tiles) {
        const auto* t = w.map.tile(p);
        if (!t || (t->type != TileType::Clear && t->type != TileType::Track) || !w.map.slope(p).isFlat())
            return CommandResult::failure("No room to widen the platform");
        if (t->type == TileType::Track && t->owner != c.company.value)
            return CommandResult::failure("Another company's track is in the way");
        if (auto why = towns::authorityRefusal(w, c.company, p, kRatingRefuseBuild); !why.empty())
            return CommandResult::failure(why);
        cost += t->trees * prices::kRemoveTree;
    }
    if (exec) {
        e.kind = TrackKind::Platform;
        e.station = x.station;
        for (auto p : tiles) {
            auto& ids = w.trackTiles[ChunkPos{p.x, p.y}.key()];
            if (std::ranges::find(ids, target) == ids.end()) {
                ids.push_back(target);
                auto& t = *w.map.tile(p);
                if (t.type == TileType::Clear) {
                    t.type = TileType::Track;
                    t.extra = 0;
                    t.owner = c.company.value;
                }
                if (t.extra < 255) ++t.extra;
                t.trees = 0;
            }
            if (std::ranges::find(w.stations[x.station].tiles, p) == w.stations[x.station].tiles.end())
                stations::addTile(w, x.station, p, StopKind::Rail);
            w.map.touch(p);
        }
        ++w.trackRevision;
        passengers::markDirty(w);
    }
    return CommandResult::success(cost);
}
} // namespace tp::cmd
