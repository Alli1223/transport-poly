#include "sim/command_handlers.hpp"
#include "sim/rail.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <cstdlib>

namespace tp::cmd {

CommandResult handle(World& w, const Command& c, const CmdBuildSpan& x, bool exec) {
    if (!w.companies.contains(c.company)) return CommandResult::failure("Only companies can build bridges and tunnels");
    if (x.from.x != x.to.x && x.from.y != x.to.y) return CommandResult::failure("Drag a straight span");
    const i64 dx = std::abs(static_cast<i64>(x.to.x) - x.from.x);
    const i64 dy = std::abs(static_cast<i64>(x.to.y) - x.from.y);
    const i64 count = dx + dy + 1;
    if (count < 3 || count > 64) return CommandResult::failure("Span must be 3 to 64 tiles long");
    const bool axisX = dx != 0;
    const TilePos a{std::min(x.from.x, x.to.x), std::min(x.from.y, x.to.y)};
    const TilePos b{std::max(x.from.x, x.to.x), std::max(x.from.y, x.to.y)};
    if (!w.map.tile(a) || !w.map.tile(b)) return CommandResult::failure("Area not explored");
    const auto start = w.map.corners(a), finish = w.map.corners(b);
    if (start.min() != start.max() || finish.min() != finish.max() || start.min() != finish.min())
        return CommandResult::failure("Both ends must be flat and at the same height");
    const u8 level = start.min();
    const auto busy = occupiedTiles(w);
    std::vector<TilePos> tiles;
    Money cost = 0;
    bool obstacle = false;
    for (i32 i = 0; i < count; ++i) {
        const TilePos p{a.x + (axisX ? i : 0), a.y + (axisX ? 0 : i)};
        const Tile* tile = w.map.tile(p);
        if (!tile) return CommandResult::failure("Area not explored");
        if (tile->span || (tile->type != TileType::Clear && tile->type != TileType::Water))
            return CommandResult::failure("Clear the span before building");
        if (tile->owner != kOwnerNone && tile->owner != c.company.value)
            return CommandResult::failure("Owned by another company");
        if (busy.contains(ChunkPos{p.x, p.y}.key())) return CommandResult::failure("Vehicle in the way");
        const bool portal = i == 0 || i == count - 1;
        if (portal && tile->type == TileType::Water) return CommandResult::failure("Build the ends on dry land");
        const auto heights = w.map.corners(p);
        const bool abutment = i == 1 || i == count - 2;
        if (!portal && (x.tunnel ? (heights.min() < level || (!abutment && heights.min() == level))
                                : (heights.max() > level || (!abutment && heights.max() == level))))
            return CommandResult::failure(x.tunnel ? "A tunnel must be covered by higher ground" : "Bridge needs clearance above the ground");
        if (!portal) obstacle = obstacle || (x.tunnel ? heights.min() > level : heights.max() < level);
        if (!x.tunnel && tile->type == TileType::Water && level < 3)
            return CommandResult::failure("A bridge over water needs at least 3 levels of clearance");
        if (const auto why = towns::authorityRefusal(w, c.company, p, kRatingRefuseBuild); !why.empty())
            return CommandResult::failure(why);
        tiles.push_back(p);
        cost += (x.tunnel ? 2'000 : 1'500) + tile->trees * 50;
    }
    if (!obstacle) return CommandResult::failure(x.tunnel ? "No hill to tunnel through" : "No valley or water to bridge");
    if (exec) {
        for (usize i = 0; i < tiles.size(); ++i) {
            Tile& tile = *w.map.tile(tiles[i]);
            const Ground ground = tile.type == TileType::Water ? Ground::Water : tile.ground;
            const bool canal = tile.type == TileType::Water && tile.owner != kOwnerNone;
            tile = Tile{};
            tile.ground = ground;
            tile.type = x.rail ? TileType::Rail : TileType::Road;
            tile.owner = c.company.value;
            if (x.rail) tile.rail = axisX ? kTrackEW : kTrackNS;
            else { tile.road = axisBits(axisX ? Dir::E : Dir::S); tile.roadOwner = c.company.value; }
            tile.span = static_cast<u8>((x.tunnel ? kSpanTunnel : kSpanBridge) | (axisX ? kSpanAxisX : 0) | (canal ? kSpanCanal : 0));
            tile.deckHeight = level;
            tile.index = static_cast<u32>((tiles.size() << 16) | i);
            w.map.touch(tiles[i]);
        }
        vehicles::invalidatePaths(w);
    }
    return CommandResult::success(cost);
}

CommandResult handle(World& w, const Command& c, const CmdRemoveSpan& x, bool exec) {
    const Tile* picked = w.map.tile(x.tile);
    if (!picked || !picked->span) return CommandResult::failure("No bridge or tunnel here");
    if ((picked->owner != c.company.value && picked->owner != kOwnerNone) || !w.companies.contains(c.company))
        return CommandResult::failure("Owned by another company");
    const bool axisX = picked->span & kSpanAxisX;
    const i32 length = static_cast<i32>(picked->index >> 16);
    const i32 offset = static_cast<i32>(picked->index & 0xFFFF);
    if (length < 3 || length > 64 || offset >= length) return CommandResult::failure("Invalid span");
    const TilePos a{x.tile.x - (axisX ? offset : 0), x.tile.y - (axisX ? 0 : offset)};
    const auto busy = occupiedTiles(w);
    std::vector<TilePos> tiles;
    for (i32 i = 0; i < length; ++i) {
        const TilePos p{a.x + (axisX ? i : 0), a.y + (axisX ? 0 : i)};
        const Tile* tile = w.map.tile(p);
        if (!tile || (tile->span & 7) != (picked->span & 7) || tile->owner != picked->owner ||
            tile->index != (static_cast<u32>(length) << 16 | static_cast<u32>(i)))
            return CommandResult::failure("Invalid span");
        if (tile->extra2 || busy.contains(ChunkPos{p.x, p.y}.key()))
            return CommandResult::failure("Vehicle or reserved route in the way");
        for (const auto [id, v] : w.vehicles)
            if (v.state != VehicleState::InDepot && v.info().type != VehicleType::Ship &&
                neighbour(v.tile, v.entry) == p && v.progress < v.info().length * 16 + 4)
                return CommandResult::failure("Vehicle tail in the way");
        tiles.push_back(p);
    }
    if (exec) {
        for (const TilePos p : tiles) {
            Tile& tile = *w.map.tile(p);
            const Ground ground = tile.ground;
            const bool canal = tile.span & kSpanCanal;
            tile = Tile{};
            tile.ground = ground;
            tile.type = ground == Ground::Water ? TileType::Water : TileType::Clear;
            if (canal) tile.owner = c.company.value;
            w.map.touch(p);
        }
        vehicles::invalidatePaths(w);
    }
    return CommandResult::success(length * 300);
}

} // namespace tp::cmd
