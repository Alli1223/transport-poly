// Handlers for free-form track (see track.hpp).

#include "sim/command_handlers.hpp"
#include "sim/prices.hpp"
#include "sim/track.hpp"
#include "sim/world.hpp"

#include <cstdlib>
#include <format>
#include <unordered_set>

namespace tp::cmd {

CommandResult handle(World& w, const Command& c, const CmdBuildTrack& x, bool exec) {
    if (!w.companies.contains(c.company)) return CommandResult::failure("Only companies can build railways");
    const TrackPlan plan = track::plan(w, c.company, x.from, x.to);
    if (!plan.ok) return CommandResult::failure(plan.error);
    CommandResult r = CommandResult::success(plan.cost, Expense::Construction);
    if (exec) {
        const std::vector<TrackEdgeId> edges = track::build(w, c.company, plan);
        if (!edges.empty()) r.createdId = edges.back().value;
    }
    return r;
}

CommandResult handle(World& w, const Command& c, const CmdRemoveTrack& x, bool exec) {
    const TrackEdge* e = w.trackEdges.tryGet(x.edge);
    if (!e) return CommandResult::failure("No track there");
    if (e->owner != c.company) return CommandResult::failure("Owned by another company");
    if (e->reservedBy.valid()) return CommandResult::failure("A train is using this track");
    if (e->kind == TrackKind::Depot) return CommandResult::failure("Demolish the depot to remove its track");
    const Money cost = static_cast<Money>(e->length) * prices::kTrackPerTile / kTrackUnits / 5;
    if (exec) track::remove(w, x.edge);
    return CommandResult::success(cost, Expense::Construction);
}

CommandResult handle(World& w, const Command& c, const CmdElectrifyTrack& x, bool exec) {
    const TrackEdge* first = w.trackEdges.tryGet(x.edge);
    if (!first) return CommandResult::failure("No track there");
    if (first->owner != c.company) return CommandResult::failure("Owned by another company");
    // The edge alone, or every edge of the company's joined to it.
    std::vector<TrackEdgeId> edges{x.edge};
    if (x.connected) {
        std::unordered_set<u32> seen{x.edge.value};
        for (usize i = 0; i < edges.size(); ++i) {
            const TrackEdge& e = w.trackEdges[edges[i]];
            for (const TrackNodeId nid : {e.a, e.b})
                if (const TrackNode* n = w.trackNodes.tryGet(nid))
                    for (const TrackEdgeId next : n->edges)
                        if (next.valid() && w.trackEdges.contains(next) && w.trackEdges[next].owner == c.company &&
                            seen.insert(next.value).second)
                            edges.push_back(next);
        }
    }
    Money cost = 0;
    usize changed = 0;
    for (const TrackEdgeId id : edges) {
        const TrackEdge& e = w.trackEdges[id];
        if (e.electrified != x.remove) continue;
        if (x.remove && e.reservedBy.valid() && w.vehicles.contains(e.reservedBy) && w.vehicles[e.reservedBy].needsWires())
            return CommandResult::failure("An electric train is using this track");
        cost += static_cast<Money>(e.length) * prices::kCatenaryPerTile / kTrackUnits / (x.remove ? 4 : 1);
        ++changed;
    }
    if (changed == 0) return CommandResult::failure(x.remove ? "No overhead wires there" : "Already electrified");
    if (exec) {
        for (const TrackEdgeId id : edges) w.trackEdges[id].electrified = !x.remove;
        ++w.trackRevision;
    }
    return CommandResult::success(cost, Expense::Construction);
}

CommandResult handle(World& w, const Command& c, const CmdBuildTrackStation& x, bool exec) {
    if (!w.companies.contains(c.company)) return CommandResult::failure("Only companies can build stations");
    if (x.join.valid()) {
        const auto* station = w.stations.tryGet(x.join);
        if (!station || station->owner != c.company) return CommandResult::failure("Not your station");
        bool nearby = false;
        for (const auto tile : station->tiles) if (distance(tileCentre(tile),x.from.pos) <= 8*kTrackUnits) nearby = true;
        if (!nearby) return CommandResult::failure("New platforms must be near the station");
    }
    if (x.platforms < 1 || x.platforms > kMaxStationPlatforms)
        return CommandResult::failure(std::format("Choose 1 to {} platforms", kMaxStationPlatforms));
    // Every platform must fit before any is built; they are planned again as they are built, as
    // each one changes the ground for the next.
    const TrackPlan plan = track::planPlatform(w, c.company, x.from, x.to);
    if (!plan.ok) return CommandResult::failure(plan.error);
    Money cost = 0;
    for (int k = 0; k < x.platforms; ++k) {
        const TrackPlan p = k == 0 ? plan : track::planParallelPlatform(w, c.company, plan, k);
        if (!p.ok) return CommandResult::failure(x.platforms > 1 ? std::format("Platform {}: {}", k + 1, p.error) : p.error);
        for (const TilePos t : p.tiles)
            if (const std::string why = towns::authorityRefusal(w, c.company, t, kRatingRefuseBuild); !why.empty())
                return CommandResult::failure(why);
        cost += p.cost;
    }
    CommandResult r = CommandResult::success(cost, Expense::Construction);
    if (exec) {
        const StationId sid = track::buildPlatform(w, c.company, plan, x.join, x.use);
        for (int k = 1; k < x.platforms && sid.valid(); ++k) {
            const TrackPlan p = track::planParallelPlatform(w, c.company, plan, k);
            if (p.ok) track::buildPlatform(w, c.company, p, sid);
        }
        if (sid.valid()) r.createdId = sid.value;
    }
    return r;
}

CommandResult handle(World& w, const Command& c, const CmdBuildTrackDepot& x, bool exec) {
    if (!w.companies.contains(c.company)) return CommandResult::failure("Only companies can build depots");
    const TrackPlan plan = track::planDepot(w, c.company, x.node);
    if (!plan.ok) return CommandResult::failure(plan.error);
    const TilePos site = tileAt(track::depotBuildingPos(plan));
    if (const std::string why = towns::authorityRefusal(w, c.company, site, kRatingRefuseBuild); !why.empty())
        return CommandResult::failure(why);
    CommandResult r = CommandResult::success(plan.cost, Expense::Construction);
    if (!exec) return r;
    const std::vector<TrackEdgeId> edges = track::build(w, c.company, plan);
    if (edges.empty()) return CommandResult::failure("Could not build the depot");
    const TDir face = track::depotFacing(plan);
    const DepotId id = w.depots.create();
    Depot& d = w.depots[id];
    d.owner = c.company;
    d.tile = site;
    d.kind = DepotKind::Rail;
    d.edge = edges.back();
    d.town = stations::nearestTown(w, site);
    d.facing = std::abs(face.x) >= std::abs(face.z) ? (face.x >= 0 ? Dir::E : Dir::W) : (face.z >= 0 ? Dir::S : Dir::N);
    w.trackEdges[d.edge].depot = id;
    Tile& t = *w.map.tile(site);
    t.type = TileType::Depot;
    t.index = id.value;
    t.owner = c.company.value;
    t.trees = 0;
    t.extra = static_cast<u8>(d.facing);
    t.extra2 = kFreeTrackDepot;
    t.road = 0;
    t.rail = 0;
    w.map.touch(site);
    r.createdId = id.value;
    return r;
}

CommandResult handle(World& w, const Command& c, const CmdTrackSignal& x, bool exec) {
    // Signals may change under a passing train: it already holds its way through.
    TrackSplit along;
    const TrackNode* n = nullptr;
    if (x.node.valid()) {
        n = w.trackNodes.tryGet(x.node);
        if (!n) return CommandResult::failure("No track there");
        for (const TrackEdgeId e : n->edges) {
            if (!e.valid()) continue;
            const TrackEdge& edge = w.trackEdges[e];
            if (edge.owner != c.company) return CommandResult::failure("Owned by another company");
            if (edge.kind == TrackKind::Depot) return CommandResult::failure("No signals at depots");
        }
        if (n->count(0) == 0 || n->count(1) == 0) return CommandResult::failure("Signals go where track continues on both sides");
    } else {
        const TrackEdge* e = w.trackEdges.tryGet(x.edge);
        if (!e) return CommandResult::failure("No track there");
        if (e->owner != c.company) return CommandResult::failure("Owned by another company");
        along = track::splitAt(w, x.edge, x.at);
        if (!along.ok) return CommandResult::failure(along.error);
    }
    static constexpr u8 kNext[4] = {3, 2, 0, 1}; // none -> both ways -> one way -> the other way -> none
    const u8 current = n ? n->signals & 3 : 0;
    const u8 next = x.signals < 0 ? kNext[current] : static_cast<u8>(x.signals & 3);
    if (next == current) return CommandResult::failure(next ? "The signal is already set that way" : "No signal there");
    const Money cost = current == 0 ? prices::kSignal : 0;
    CommandResult r = CommandResult::success(cost, Expense::Construction);
    if (exec) {
        const TrackNodeId node = n ? x.node : track::split(w, along);
        if (!node.valid()) return CommandResult::failure("No track there");
        w.trackNodes[node].signals = next;
        ++w.trackRevision;
        r.createdId = node.value;
    }
    return r;
}

} // namespace tp::cmd
