// Handlers for docks and shipyards (ship depots). Both stand on a coastal land tile facing a
// water tile, where ships moor.

#include "sim/command_handlers.hpp"
#include "sim/passengers.hpp"
#include "sim/prices.hpp"
#include "sim/water_path.hpp"
#include "sim/world.hpp"

#include <set>

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

CommandResult handle(World& w, const Command& c, const CmdEditWater& x, bool exec) {
    if (!w.companies.contains(c.company)) return CommandResult::failure("Only companies can edit waterways");
    const Rect r = normalise(x.a, x.b);
    // Validate extents before iterating, including requests received over the network.
    if (static_cast<i64>(r.x1) - r.x0 >= 64 || static_cast<i64>(r.y1) - r.y0 >= 64)
        return CommandResult::failure("Water editing area too large (max 64 x 64)");
    std::set<u64> busy;
    const auto key = [](TilePos p) { return ChunkPos{p.x, p.y}.key(); };
    for (const auto [id, v] : w.vehicles) {
        if (v.state == VehicleState::InDepot) continue;
        busy.insert(key(v.tile));
        if (v.info().type == VehicleType::Ship && v.moving) {
            const TilePos next = headingStep(v.tile, v.heading);
            busy.insert(key(next));
            if (v.heading & 1) {
                busy.insert(key({next.x, v.tile.y}));
                busy.insert(key({v.tile.x, next.y}));
            }
        }
    }
    std::vector<TilePos> changed;
    Money cost = 0;
    for (i64 yy = r.y0; yy <= r.y1; ++yy) {
        for (i64 xx = r.x0; xx <= r.x1; ++xx) {
            const TilePos p{static_cast<i32>(xx), static_cast<i32>(yy)};
            const Tile* tile = w.map.tile(p);
            if (!tile) return CommandResult::failure("Area not explored");
            if (x.water && tile->type == TileType::Water) continue;
            if (!x.water && tile->type == TileType::Clear) continue;
            if (tile->type != (x.water ? TileType::Clear : TileType::Water))
                return CommandResult::failure("Clear the site before editing water");
            if (tile->owner != kOwnerNone && tile->owner != c.company.value)
                return CommandResult::failure("Waterway owned by another company");
            if (w.map.corners(p).max() != 0)
                return CommandResult::failure("Lower and level the land to sea level first");
            if (busy.contains(key(p))) return CommandResult::failure("Vehicle in the way");
            if (!x.water) {
                for (const Dir d : kDirs) {
                    const Tile* other = w.map.tile(neighbour(p, d));
                    if (!other) continue;
                    const bool dock = other->type == TileType::Station &&
                                      static_cast<StopKind>(other->extra) == StopKind::Dock &&
                                      static_cast<Dir>(other->extra2) == opposite(d);
                    const bool yard = other->type == TileType::Depot &&
                                      w.depots.tryGet(DepotId{other->index}) &&
                                      w.depots[DepotId{other->index}].kind == DepotKind::Ship &&
                                      static_cast<Dir>(other->extra) == opposite(d);
                    if (dock || yard) return CommandResult::failure("Remove the dock or shipyard using this water first");
                }
            }
            if (const std::string why = towns::authorityRefusal(w, c.company, p, kRatingRefuseBuild); !why.empty())
                return CommandResult::failure(why);
            cost += x.water ? prices::kCanalTile + tile->trees * prices::kRemoveTree : prices::kReclaimWater;
            changed.push_back(p);
        }
    }
    if (changed.empty()) return CommandResult::failure(x.water ? "Already water" : "No water to reclaim");
    if (exec) {
        for (const TilePos p : changed) {
            Tile& tile = *w.map.tile(p);
            if (tile.trees)
                towns::changeRating(w, towns::townAt(w, p), c.company, kRatingRemoveTrees, kRatingRefuseBuild);
            tile = Tile{};
            tile.type = x.water ? TileType::Water : TileType::Clear;
            tile.ground = x.water ? Ground::Water : Ground::Sand;
            tile.owner = x.water ? c.company.value : kOwnerNone;
            w.map.touch(p);
            for (const Dir d : kDirs) w.map.touch(neighbour(p, d));
        }
        // Ships re-plan from their current position when a waterway is edited.
        vehicles::invalidatePaths(w);
    }
    return CommandResult::success(cost, Expense::Construction);
}

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
