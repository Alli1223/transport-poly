#include "sim/walking.hpp"
#include "core/math.hpp"
#include "sim/road.hpp"
#include "sim/world.hpp"
#include <algorithm>
#include <bit>
#include <map>
#include <queue>
#include <tuple>

namespace tp::walking {
namespace {
using Node = std::tuple<i32, i32, u8>;
TilePos tile(Node n) { return {std::get<0>(n), std::get<1>(n)}; }
bool road(const Tile* t) {
    return t && (t->type == TileType::Road || t->type == TileType::Station) && allRoadPieces(*t) &&
           (t->span & 3) != kSpanTunnel;
}
bool pavement(const Tile* t) { return t && t->type == TileType::Footpath; }
TPoint point(Node n) {
    const auto t = tile(n);
    const auto c = std::get<2>(n);
    constexpr i32 x[] = {40, 216, 216, 40, 128}, z[] = {40, 40, 216, 216, 128};
    return {t.x * kTrackUnits + x[c], t.y * kTrackUnits + z[c]};
}
std::optional<Node> graphNode(const World& w, TPoint p) {
    const auto at = tileAt(p);
    const auto* surface = w.map.tile(at);
    if (!road(surface) && !pavement(surface)) return std::nullopt;
    for (u8 corner = pavement(surface) ? 4 : 0; corner < (pavement(surface) ? 5 : 4); ++corner) {
        const Node n{at.x, at.y, corner};
        if (point(n) == p) return n;
    }
    return std::nullopt;
}
i64 cost(TPoint a, TPoint b) { return distance(a, b); }
bool clearLine(const World& w, TPoint a, TPoint b) {
    const i64 steps = std::max<i64>(1, distance(a, b) / 32);
    for (i64 n = 0; n <= steps; ++n) {
        const auto* t = w.map.tile(tileAt({static_cast<i32>(a.x + (i64(b.x) - a.x) * n / steps),
                                           static_cast<i32>(a.z + (i64(b.z) - a.z) * n / steps)}));
        if (!t || t->type == TileType::Water || t->type == TileType::Building || t->type == TileType::Rail ||
            t->type == TileType::Track || t->type == TileType::Depot)
            return false;
    }
    return true;
}
std::vector<Node> attachments(const World& w, TPoint p) {
    std::vector<Node> out;
    i64 best = 3 * kTrackUnits;
    const auto at = tileAt(p);
    for (i32 y = -2; y <= 2; ++y)
        for (i32 x = -2; x <= 2; ++x) {
            const TilePos t{at.x + x, at.y + y};
            const auto* surface = w.map.tile(t);
            if (!road(surface) && !pavement(surface)) continue;
            for (u8 c = pavement(surface) ? 4 : 0; c < (pavement(surface) ? 5 : 4); ++c) {
                Node node{t.x, t.y, c};
                const i64 d = cost(p, point(node));
                if (d > best + 32 || !clearLine(w, p, point(node))) continue;
                if (d < best) {
                    best = d;
                    std::erase_if(out, [&](Node old) { return cost(p, point(old)) > best + 32; });
                }
                out.push_back(node);
            }
        }
    return out;
}
std::vector<Node> neighbours(const World& w, Node n) {
    const auto at = tile(n);
    const auto* here = w.map.tile(at);
    const u8 c = std::get<2>(n);
    std::vector<Node> out;
    if (road(here)) {
        const u8 bits = allRoadPieces(*here);
        const bool crossing = std::popcount(static_cast<unsigned>(bits)) != 2 || !isStraight(bits);
        for (u8 other = 0; other < 4; ++other) {
            const u8 edge = std::min(c, other);
            const bool adjacent = other == (c + 1) % 4 || c == (other + 1) % 4;
            const u8 side = (c == 0 && other == 3) || (c == 3 && other == 0) ? 3 : edge;
            const bool along = (bits == axisBits(Dir::E) && (side == 0 || side == 2)) ||
                               (bits == axisBits(Dir::N) && (side == 1 || side == 3));
            if (adjacent && (crossing || along)) out.emplace_back(at.x, at.y, other);
        }
    }
    for (const auto d : kDirs) {
        const auto next = neighbour(at, d);
        const auto* there = w.map.tile(next);
        if (!road(there) && !pavement(there)) continue;
        if (pavement(here) && pavement(there)) {
            out.emplace_back(next.x, next.y, u8{4});
            continue;
        }
        if (pavement(here)) {
            for (u8 k = 0; k < 4; ++k)
                if (cost(point(n), point({next.x, next.y, k})) < kTrackUnits)
                    out.emplace_back(next.x, next.y, k);
            continue;
        }
        const bool touches = (d == Dir::N && c < 2) || (d == Dir::E && (c == 1 || c == 2)) ||
                             (d == Dir::S && c >= 2) || (d == Dir::W && (c == 0 || c == 3));
        if (!touches) continue;
        if (pavement(there)) {
            out.emplace_back(next.x, next.y, u8{4});
            continue;
        }
        if (!(allRoadPieces(*here) & dirBit(d)) || !(allRoadPieces(*there) & dirBit(opposite(d)))) continue;
        const u8 target = d == Dir::N   ? (c == 0 ? 3 : 2)
                          : d == Dir::E ? (c == 1 ? 0 : 3)
                          : d == Dir::S ? (c == 2 ? 1 : 0)
                                        : (c == 0 ? 1 : 2);
        out.emplace_back(next.x, next.y, target);
    }
    return out;
}
void append(std::vector<WalkPoint>& path, WalkPoint p) {
    if (path.empty() || path.back() != p) path.push_back(p);
}
TPoint meeting(const World& w, const Station& s) {
    if (!s.entrances.empty()) return s.entrances.front().path.back();
    (void)w;
    return {s.sign.x * kTrackUnits + 40, s.sign.y * kTrackUnits + 40};
}
} // namespace
TPoint endpoint(const World& w, TilePos centre2) {
    TPoint p{centre2.x * kTrackUnits / 2, centre2.y * kTrackUnits / 2};
    const auto* t = w.map.tile(tileAt(p));
    if (t && t->type == TileType::Building)
        if (const auto* b = w.buildings.tryGet(BuildingId(t->index))) {
            p = {b->pos.x * kTrackUnits + b->info().size * kTrackUnits / 2,
                 b->pos.y * kTrackUnits + b->info().size * kTrackUnits / 2};
            if (b->facing == Dir::N) p.z = b->pos.y * kTrackUnits - 1;
            if (b->facing == Dir::S) p.z = (b->pos.y + b->info().size) * kTrackUnits + 1;
            if (b->facing == Dir::W) p.x = b->pos.x * kTrackUnits - 1;
            if (b->facing == Dir::E) p.x = (b->pos.x + b->info().size) * kTrackUnits + 1;
        }
    return p;
}
std::optional<std::vector<WalkPoint>> route(const World& w, TPoint from, TPoint to) {
    const auto starts = attachments(w, from), ends = attachments(w, to);
    if (starts.empty() || ends.empty()) return std::nullopt;
    struct Item {
        i64 f, g;
        Node n;
        bool operator>(const Item& b) const { return std::tie(f, g, n) > std::tie(b.f, b.g, b.n); }
    };
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    std::map<Node, i64> best;
    std::map<Node, Node> previous;
    for (auto n : starts) {
        const auto g = cost(from, point(n));
        best[n] = g;
        previous[n] = n;
        open.push({g + cost(point(n), to), g, n});
    }
    usize expanded = 0;
    while (!open.empty() && expanded++ < 8192) {
        const auto current = open.top();
        open.pop();
        if (best[current.n] != current.g) continue;
        if (std::ranges::find(ends, current.n) != ends.end()) {
            std::vector<Node> nodes;
            auto n = current.n;
            while (true) {
                nodes.push_back(n);
                if (previous[n] == n) break;
                n = previous[n];
            }
            std::reverse(nodes.begin(), nodes.end());
            std::vector<WalkPoint> path{{from}};
            for (auto node : nodes) append(path, {point(node)});
            append(path, {to});
            return path;
        }
        for (auto next : neighbours(w, current.n)) {
            const i64 g = current.g + cost(point(current.n), point(next));
            if (!best.contains(next) || g < best[next]) {
                best[next] = g;
                previous[next] = current.n;
                open.push({g + cost(point(next), to), g, next});
            }
        }
    }
    return std::nullopt;
}
i64 length(const std::vector<WalkPoint>& path) {
    i64 total = 0;
    for (usize n = 1; n < path.size(); ++n) {
        total += distance(path[n - 1].p, path[n].p);
        if (path[n - 1].height >= 0 && path[n].height >= 0)
            total += std::abs(path[n].height - path[n - 1].height) * 8;
    }
    return total;
}
bool begin(World& w, PersonId id, std::vector<WalkPoint> path, i64 start) {
    auto& p = w.people[id];
    if (path.empty()) return false;
    p.walkBegin = start;
    p.walkEnd = start + std::max<i64>(1, (length(path) * kWalkTicksPerTile + kTrackUnits - 1) / kTrackUnits);
    p.walkPath = std::move(path);
    return true;
}
WalkPoint position(const Person& p, i64 tick) {
    if (p.walkPath.empty()) return {};
    i64 remaining = length(p.walkPath) *
                    std::clamp(tick - p.walkBegin, i64{0}, std::max<i64>(1, p.walkEnd - p.walkBegin)) /
                    std::max<i64>(1, p.walkEnd - p.walkBegin);
    for (usize n = 1; n < p.walkPath.size(); ++n) {
        const auto a = p.walkPath[n - 1], b = p.walkPath[n];
        const i64 span =
            distance(a.p, b.p) + (a.height >= 0 && b.height >= 0 ? std::abs(b.height - a.height) * 8 : 0);
        if (remaining <= span && span > 0)
            return {{static_cast<i32>(a.p.x + (i64(b.p.x) - a.p.x) * remaining / span),
                     static_cast<i32>(a.p.z + (i64(b.p.z) - a.p.z) * remaining / span)},
                    a.height >= 0 && b.height >= 0
                        ? static_cast<i32>(a.height + (i64(b.height) - a.height) * remaining / span)
                        : -1};
        remaining -= span;
    }
    return p.walkPath.back();
}
std::optional<std::vector<WalkPoint>> toStation(const World& w, TPoint from, StationId sid) {
    const auto* s = w.stations.tryGet(sid);
    if (!s) return std::nullopt;
    std::optional<std::vector<WalkPoint>> best;
    if (!s->entrances.empty())
        for (const auto& entry : s->entrances) {
            const auto* roadTile = w.map.tile(entry.road);
            const auto* platform = w.trackEdges.tryGet(entry.platform);
            if (entry.path.empty() || !road(roadTile) || !platform || platform->station != sid ||
                platform->kind != TrackKind::Platform)
                continue;
            auto path = route(w, from, entry.path.front());
            if (!path) continue;
            for (const auto point : entry.path) append(*path, {point});
            if (!best || length(*path) < length(*best)) best = std::move(path);
        }
    else if (!(s->facilities & kFacilityRail) || !track::hasPlatform(w, sid))
        best = route(w, from, meeting(w, *s));
    return best;
}
std::optional<std::vector<WalkPoint>> fromStation(const World& w, StationId sid, TPoint to) {
    auto path = toStation(w, to, sid);
    if (path) std::reverse(path->begin(), path->end());
    return path;
}
std::vector<WalkPoint> withinStation(const World& w, StationId sid, TPoint from, TPoint to) {
    const auto a = track::platformNear(w, sid, from), b = track::platformNear(w, sid, to);
    if (!a.valid() || !b.valid()) return {{from}, {to}};
    const bool same = track::platformNumber(w, sid, from) == track::platformNumber(w, sid, to);
    const auto platforms = track::platformsOf(w, sid);
    const auto ends = track::platformEnds(w, platforms.front().front());
    if (ends.size() != 2) return {{from}, {to}};
    const auto& na = w.trackNodes[ends.front().node];
    const auto dir = -ends.front().outward;
    const auto origin = offset(na.pos, dir, kTrackUnits);
    const auto stairs = [&](TPoint p) {
        const i64 along = (i64(origin.x) - p.x) * dir.x / kDirOne + (i64(origin.z) - p.z) * dir.z / kDirOne;
        return offset(p, dir, along);
    };
    const auto platformSide = [&](TPoint p, TrackEdgeId id, TPoint side) {
        const auto& e = w.trackEdges[id];
        const TrackSample* near = &e.samples.front();
        for (const auto& sample : e.samples)
            if (distance(sample.p, p) < distance(near->p, p)) near = &sample;
        const auto tangent = normalised(i64(w.trackNodes[e.b].pos.x) - w.trackNodes[e.a].pos.x,
                                        i64(w.trackNodes[e.b].pos.z) - w.trackNodes[e.a].pos.z);
        const TDir normal{-tangent.z, tangent.x};
        const i64 lateral = ((i64(p.x) - near->p.x) * normal.x + (i64(p.z) - near->p.z) * normal.z) / kDirOne;
        const i64 facing = (i64(side.x) - near->p.x) * normal.x + (i64(side.z) - near->p.z) * normal.z;
        return offset(p, normal, (facing < 0 ? -200 : 200) - lateral);
    };
    const auto fromSide = platformSide(from, a, from), toSide = platformSide(to, b, same ? from : to);
    const auto sa = stairs(fromSide), sb = stairs(toSide);
    const i32 ha = w.trackNodes[w.trackEdges[a].a].h, hb = w.trackNodes[w.trackEdges[b].a].h;
    if (same) return {{from, ha}, {fromSide, ha}, {toSide, hb}, {to, hb}};
    i32 bridge = std::max(ha, hb);
    for (const auto& chain : platforms)
        bridge = std::max(bridge, w.trackNodes[w.trackEdges[chain.front()].a].h);
    bridge += 64;
    return {{from, ha},   {fromSide, ha}, {offset(sa, dir, kTrackUnits), ha},
            {sa, bridge}, {sb, bridge},   {offset(sb, dir, kTrackUnits), hb},
            {toSide, hb}, {to, hb}};
}
void migrateLegacy(World& w) {
    for (auto [id, p] : w.people) {
        if (p.state != PersonState::Walking && p.state != PersonState::ToStop &&
            p.state != PersonState::FromStop)
            continue;
        TPoint from = endpoint(w, p.tripFrom2), to = endpoint(w, p.tripTo2);
        if (p.state == PersonState::ToStop) {
            if (const auto* station = w.stations.tryGet(p.station)) to = tileCentre(station->sign);
        } else if (p.state == PersonState::FromStop)
            from = {p.transitTo2.x * kTrackUnits / 2, p.transitTo2.y * kTrackUnits / 2};
        const i64 duration = std::max<i64>(1, distance(from, to) * kWalkTicksPerTile / kTrackUnits);
        const i64 elapsed = std::clamp(duration - (p.nextEvent - w.clock.tick), i64{0}, duration);
        const TPoint current{static_cast<i32>(from.x + (i64(to.x) - from.x) * elapsed / duration),
                             static_cast<i32>(from.z + (i64(to.z) - from.z) * elapsed / duration)};
        auto path = p.state == PersonState::ToStop ? toStation(w, current, p.station) : route(w, current, to);
        if (path) {
            begin(w, id, std::move(*path), w.clock.tick);
            people::schedule(w, id, p.walkEnd);
        } else
            people::travelAbstract(w, id, {current.x * 2 / kTrackUnits, current.z * 2 / kTrackUnits},
                                   p.tripTo2, false);
    }
}
void invalidate(World& w) {
    for (auto [id, p] : w.people) {
        if (p.walkPath.empty() || p.walkEnd <= w.clock.tick || p.state == PersonState::Riding ||
            p.state == PersonState::Waiting)
            continue;
        bool damaged = false;
        for (usize n = 1; n + 1 < p.walkPath.size(); ++n) {
            if (p.walkPath[n].height >= 0) continue;
            const auto* tile = w.map.tile(tileAt(p.walkPath[n].p));
            if (!tile || (!road(tile) && !pavement(tile) && tile->type != TileType::Track &&
                          tile->type != TileType::Station))
                damaged = true;
            // A road can remain in place after losing the piece used by this route.
            const auto a = graphNode(w, p.walkPath[n - 1].p), b = graphNode(w, p.walkPath[n].p);
            if (a && b && *a != *b) {
                const auto adjacent = neighbours(w, *a);
                if (std::ranges::find(adjacent, *b) == adjacent.end()) damaged = true;
            }
        }
        if (!damaged) continue;
        const auto from = position(p, w.clock.tick).p;
        std::optional<std::vector<WalkPoint>> path;
        if (p.state == PersonState::ToStop)
            path = toStation(w, from, p.station);
        else
            path = route(w, from, endpoint(w, p.tripTo2));
        if (path) {
            begin(w, id, std::move(*path), w.clock.tick);
            people::schedule(w, id, p.walkEnd);
        } else
            people::travelAbstract(w, id, {from.x * 2 / kTrackUnits, from.z * 2 / kTrackUnits}, p.tripTo2,
                                   false);
    }
}
} // namespace tp::walking
