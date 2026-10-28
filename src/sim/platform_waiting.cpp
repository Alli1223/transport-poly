#include "sim/passengers.hpp"
#include "core/hash.hpp"
#include "sim/world.hpp"
#include <algorithm>
#include <climits>
#include <map>
#include <tuple>

namespace tp::passengers {
namespace {
TrackEdgeId departurePlatform(const World& w, StationId station, StationId destination,
                              TrackEdgeId fallback) {
    TrackEdgeId best;
    auto score = std::tuple{INT_MAX, usize{SIZE_MAX}, UINT32_MAX};
    for (const auto [id, v] : w.vehicles) {
        if (v.info().type != VehicleType::Rail || !v.capacity() || v.stopped || v.sendToDepot ||
            v.goingForService)
            continue;
        if (!v.freeTrack && !(w.depots.contains(v.depot) && w.depots[v.depot].edge.valid())) continue;
        for (usize i = 0; i < v.orders.size(); ++i) {
            const auto& order = v.orders[i];
            if (order.kind != OrderKind::Station || order.station != station || order.passThrough() ||
                (order.flags & (kOrderNoLoading | kOrderUnloadOnly)))
                continue;
            StationId next;
            for (usize k = 1; k <= v.orders.size(); ++k) {
                const auto& stop = v.orders[(i + k) % v.orders.size()];
                if (stop.kind == OrderKind::Station && !stop.passThrough()) {
                    next = stop.station;
                    break;
                }
            }
            if (next != destination) continue;
            auto platform = tracktrains::passengerPlatform(w, id, order);
            if (!platform.valid()) platform = fallback;
            const bool current = i == v.orderIndex % v.orders.size();
            const int priority = current ? (v.state == VehicleState::Loading ? 0 : 1) : 2;
            const auto candidate = std::tuple{
                priority, (i + v.orders.size() - v.orderIndex % v.orders.size()) % v.orders.size(), id.value};
            if (candidate < score) {
                score = candidate;
                best = platform;
            }
        }
    }
    return best;
}
// Sample the whole platform in physical order, independent of edge creation order.
WalkPoint waitingPoint(const World& w, const std::vector<TrackEdgeId>& chain, u32 serial) {
    const auto ends = track::platformEnds(w, chain.front());
    if (ends.size() != 2) return {track::platformPoint(w, chain)};
    const auto& start = w.trackNodes[ends.front().node];
    const auto direction = -ends.front().outward;
    i64 length = 0;
    for (const auto id : chain) length += w.trackEdges[id].length;
    const i64 margin = std::min<i64>(kTrackUnits / 2, length / 4);
    const u32 hash = mix32(serial ^ mix32(ends.front().node.value + 0x9e3779b9u));
    const i64 along = margin + hash % std::max<i64>(1, length - 2 * margin);
    const auto centre = offset(start.pos, direction, along);
    // Stable per-person positions across both platform strips. New arrivals never move people
    // already waiting; keep a margin from the track and the outer platform edges.
    const u32 across = mix32(hash ^ 0x85ebca6bu);
    const i64 lateral = (108 + across % 121) * ((across & 0x80000000u) ? -1 : 1);
    return {offset(centre, {-direction.z, direction.x}, lateral), start.h};
}
} // namespace

void positionWaiting(World& w, StationId sid) {
    auto* station = w.stations.tryGet(sid);
    if (!station || station->waiting.empty()) return;
    const auto platforms = track::platformsOf(w, sid);
    if (platforms.empty()) return;
    std::map<usize, std::vector<PersonId>> groups;
    for (const auto& [destination, queue] : station->waiting) {
        const auto selected = departurePlatform(w, sid, destination, platforms.front().front());
        if (!selected.valid()) continue;
        usize platform = 0;
        for (usize i = 0; i < platforms.size(); ++i)
            if (std::ranges::find(platforms[i], selected) != platforms[i].end()) platform = i;
        for (const auto id : queue) {
            const auto* p = w.people.tryGet(id);
            if (p && p->state == PersonState::Waiting && p->station == sid) groups[platform].push_back(id);
        }
    }
    for (auto& [platform, people] : groups) {
        std::ranges::sort(people, {}, &PersonId::value);
        for (usize i = 0; i < people.size(); ++i) {
            auto& p = w.people[people[i]];
            const auto target = waitingPoint(w, platforms[platform], p.serial);
            // Finish an existing walk first, including transfers across the footbridge.
            if (p.walkEnd > w.clock.tick || p.transitEnd > w.clock.tick) continue;
            if (!p.walkPath.empty() && p.walkPath.back().p == target.p) continue;
            TPoint from =
                p.walkPath.empty() ? tileCentre(station->sign) : walking::position(p, w.clock.tick).p;
            if (p.walkPath.empty() && !station->entrances.empty())
                from = station->entrances.front().path.back();
            auto path = walking::withinStation(w, sid, from, target.p);
            walking::begin(w, people[i], std::move(path), w.clock.tick);
            // Review the service after the passenger reaches their assigned waiting area.
            if (p.nextEvent >= 0 && p.nextEvent <= p.walkEnd)
                people::schedule(w, people[i], p.walkEnd + 3 * kTicksPerHour);
        }
    }
}
void updateWaiting(World& w) {
    if (w.clock.tick % 30) return;
    for (const auto [sid, station] : w.stations)
        if (!station.waiting.empty()) positionWaiting(w, sid);
}
} // namespace tp::passengers
