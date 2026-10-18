// Handlers for docks and shipyards (ship depots). Both stand on a coastal land tile facing a
// water tile, where ships moor.

#include "sim/command_handlers.hpp"
#include "sim/passengers.hpp"
#include "sim/prices.hpp"
#include "sim/water_path.hpp"
#include "sim/world.hpp"

namespace tp::cmd {

namespace {

// Common checks for a coastal site; returns an error message or an empty string.
std::string whyNotCoastal(const World& w, CompanyId company, TilePos t, Dir facing) {
    const Tile* tile = w.map.tile(t);
    if (!tile) return "Area not explored";
    switch (tile->type) {
    case TileType::Clear: break;
    case TileType::Water: return "Build on the shore, facing the water";
    case TileType::Building: return "Building in the way";
    default: return "Site must be clear";
    }
    if (w.map.slope(t).isSteep()) return "Land sloped in the wrong direction";
    if (!w.map.tile(neighbour(t, facing))) return "Area not explored";
    if (std::string why = towns::authorityRefusal(w, company, t, kRatingRefuseBuild); !why.empty()) return why;
    if (!isWaterTile(w, neighbour(t, facing))) return "Must face the water";
    return {};
}

} // namespace

CommandResult handle(World& w, const Command& c, const CmdBuildDock& x, bool exec) {
    if (!w.companies.contains(c.company)) return CommandResult::failure("Only companies can build docks");
    if (const std::string why = whyNotCoastal(w, c.company, x.tile, x.facing); !why.empty())
        return CommandResult::failure(why);
    const Money cost = prices::kDock + w.map.tile(x.tile)->trees * prices::kRemoveTree;
    if (exec) {
        StationId sid = stations::findJoinable(w, c.company, x.tile);
        Tile& t = *w.map.tile(x.tile);
        t.type = TileType::Station;
        t.extra = static_cast<u8>(StopKind::Dock);
        t.extra2 = static_cast<u8>(x.facing);
        t.trees = 0;
        t.road = 0;
        t.rail = 0;
        t.owner = c.company.value;
        if (sid.valid()) {
            t.index = sid.value;
            stations::addTile(w, sid, x.tile, StopKind::Dock);
        } else {
            sid = stations::create(w, c.company, x.tile, StopKind::Dock);
            w.map.tile(x.tile)->index = sid.value;
        }
        w.map.touch(x.tile);
        passengers::markDirty(w);
    }
    return CommandResult::success(cost, Expense::Construction);
}

CommandResult handle(World& w, const Command& c, const CmdBuildShipDepot& x, bool exec) {
    if (!w.companies.contains(c.company)) return CommandResult::failure("Only companies can build shipyards");
    if (const std::string why = whyNotCoastal(w, c.company, x.tile, x.facing); !why.empty())
        return CommandResult::failure(why);
    const Money cost = prices::kShipDepot + w.map.tile(x.tile)->trees * prices::kRemoveTree;
    if (!exec) return CommandResult::success(cost, Expense::Construction);
    const DepotId id = w.depots.create();
    Depot& d = w.depots[id];
    d.owner = c.company;
    d.tile = x.tile;
    d.facing = x.facing;
    d.kind = DepotKind::Ship;
    d.town = stations::nearestTown(w, x.tile);
    Tile& t = *w.map.tile(x.tile);
    t.type = TileType::Depot;
    t.extra = static_cast<u8>(x.facing);
    t.extra2 = 0;
    t.index = id.value;
    t.owner = c.company.value;
    t.trees = 0;
    t.road = 0;
    t.rail = 0;
    w.map.touch(x.tile);
    CommandResult r = CommandResult::success(cost, Expense::Construction);
    r.createdId = id.value;
    return r;
}

} // namespace tp::cmd
