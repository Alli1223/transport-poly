// Handlers for free-form track (see track.hpp).

#include "sim/command_handlers.hpp"
#include "sim/prices.hpp"
#include "sim/track.hpp"
#include "sim/world.hpp"

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
    const Money cost = static_cast<Money>(e->length) * prices::kTrackPerTile / kTrackUnits / 5;
    if (exec) track::remove(w, x.edge);
    return CommandResult::success(cost, Expense::Construction);
}

} // namespace tp::cmd
