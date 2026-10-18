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
        for (const auto [node, side] : {std::pair{e.a, e.sideA}, std::pair{e.b, e.sideB}}) {
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
std::optional<TPoint> platformApproach(const World& w, TilePos from, TrackEdgeId platform,
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
    const i64 steps = std::max<i64>(1, distance(centre, end) / 32);
    for (i64 n = 0; n <= steps; ++n) {
        const auto tile = tileAt({static_cast<i32>(centre.x + (i64(end.x) - centre.x) * n / steps),
                                  static_cast<i32>(centre.z + (i64(end.z) - centre.z) * n / steps)});
        if (tile == from && clear(w, tile, edge.station, company)) continue;
        const auto found = w.trackTiles.find(ChunkPos{tile.x, tile.y}.key());
        if (found == w.trackTiles.end() || std::ranges::find(found->second, platform) == found->second.end())
            return std::nullopt;
    }
    return end;
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
    const auto* road = w.map.tile(x.road);
    if (!road || (road->type != TileType::Road && road->type != TileType::Station) || !allRoadPieces(*road) ||
        road->span)
        return CommandResult::failure("Choose a surface road near the station");
    if (station.entrances.size() >= 8) return CommandResult::failure("Station already has eight entrances");
    for (const auto& e : station.entrances)
        if (e.road == x.road) return CommandResult::failure("An entrance already connects to this road");
    std::map<TilePos, TilePos> previous;
    std::queue<TilePos> queue;
    for (auto d : kDirs) {
        const auto p = neighbour(x.road, d);
        if (clear(w, p, x.station, c.company) &&
            std::abs(int(w.map.tileBaseHeight(p)) - int(w.map.tileBaseHeight(x.road))) <= 1) {
            previous[p] = p;
            queue.push(p);
        }
    }
    std::optional<TilePos> finish;
    TrackEdgeId platform;
    std::optional<TPoint> landing;
    while (!queue.empty() && previous.size() < 2048) {
        const auto p = queue.front();
        queue.pop();
        platform = track::platformNear(w, x.station, tileCentre(p), 2 * kTrackUnits + 32);
        if (platform.valid() && (landing = platformApproach(w, p, platform, c.company))) {
            finish = p;
            break;
        }
        for (auto d : kDirs) {
            const auto next = neighbour(p, d);
            if (previous.contains(next) || chebyshev(next.x - x.road.x, next.y - x.road.y) > 16 ||
                !clear(w, next, x.station, c.company))
                continue;
            if (std::abs(int(w.map.tileBaseHeight(p)) - int(w.map.tileBaseHeight(next))) > 1) continue;
            previous[next] = p;
            queue.push(next);
        }
    }
    if (!finish) return CommandResult::failure("No clear approach to a platform within 16 tiles");
    StationEntrance entry;
    entry.road = x.road;
    entry.platform = platform;
    for (auto p = *finish;; p = previous[p]) {
        entry.tiles.push_back(p);
        if (previous[p] == p) break;
    }
    std::reverse(entry.tiles.begin(), entry.tiles.end());
    const auto first = tileCentre(entry.tiles.front());
    // Connect to the nearest corner of the road's pavement, rather than its carriageway.
    TPoint curb{};
    i64 best = INT64_MAX;
    for (i32 z : {40, 216})
        for (i32 xx : {40, 216}) {
            const TPoint p{x.road.x * kTrackUnits + xx, x.road.y * kTrackUnits + z};
            if (distance(p, first) < best) {
                curb = p;
                best = distance(p, first);
            }
        }
    entry.path.push_back(curb);
    for (auto p : entry.tiles) entry.path.push_back(tileCentre(p));
    entry.path.push_back(*landing);
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
