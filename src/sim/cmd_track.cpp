// Handlers for free-form track (see track.hpp).

#include "sim/command_handlers.hpp"
#include "sim/prices.hpp"
#include "sim/track.hpp"
#include "sim/world.hpp"

#include <cstdlib>

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

CommandResult handle(World& w, const Command& c, const CmdBuildTrackStation& x, bool exec) {
    if (!w.companies.contains(c.company)) return CommandResult::failure("Only companies can build stations");
    const TrackPlan plan = track::planPlatform(w, c.company, x.from, x.to);
    if (!plan.ok) return CommandResult::failure(plan.error);
    for (const TilePos t : plan.tiles)
        if (const std::string why = towns::authorityRefusal(w, c.company, t, kRatingRefuseBuild); !why.empty())
            return CommandResult::failure(why);
    CommandResult r = CommandResult::success(plan.cost, Expense::Construction);
    if (exec) {
        const StationId sid = track::buildPlatform(w, c.company, plan);
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
    TrackNode* n = w.trackNodes.tryGet(x.node);
    if (!n) return CommandResult::failure("No track there");
    for (const TrackEdgeId e : n->edges) {
        if (!e.valid()) continue;
        const TrackEdge& edge = w.trackEdges[e];
        if (edge.owner != c.company) return CommandResult::failure("Owned by another company");
        if (edge.reservedBy.valid()) return CommandResult::failure("A train is using this track");
        if (edge.kind == TrackKind::Depot) return CommandResult::failure("No signals at depots");
    }
    if (n->count(0) == 0 || n->count(1) == 0) return CommandResult::failure("Signals go where track continues on both sides");
    static constexpr u8 kNext[4] = {3, 2, 0, 1}; // none -> both ways -> one way -> the other way -> none
    const Money cost = n->signals == 0 ? prices::kSignal : 0;
    if (exec) {
        n->signals = kNext[n->signals & 3];
        ++w.trackRevision;
    }
    return CommandResult::success(cost, Expense::Construction);
}

} // namespace tp::cmd
