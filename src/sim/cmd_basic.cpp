// Handlers for exploration, company management and landscaping commands.

#include "core/hash.hpp"
#include "core/names.hpp"
#include "sim/command_handlers.hpp"
#include "sim/prices.hpp"
#include "sim/terraform.hpp"
#include "sim/world.hpp"

#include <format>

namespace tp::cmd {

CommandResult handle(World& w, const Command&, const CmdActivateChunks& x, bool exec) {
    if (x.chunks.empty()) return CommandResult::failure("Nothing to explore");
    if (static_cast<i32>(x.chunks.size()) > w.settings.maxChunksPerCommand)
        return CommandResult::failure("Too many chunks requested at once");
    usize fresh = 0;
    for (const ChunkPos p : x.chunks) fresh += w.map.isActive(p) ? 0 : 1;
    if (fresh == 0) return CommandResult::failure("Already explored");
    if (static_cast<i64>(w.map.activeChunks().size() + fresh) > w.settings.maxActiveChunks)
        return CommandResult::failure("The world has reached its maximum explored size");
    if (exec)
        for (const ChunkPos p : x.chunks) w.activateChunk(p);
    return CommandResult::success();
}

CommandResult handle(World& w, const Command&, const CmdCreateCompany& x, bool exec) {
    if (w.companies.size() >= kMaxCompanies) return CommandResult::failure("Too many companies");
    if (x.name.size() > 64) return CommandResult::failure("Company name too long");
    if (!exec) return CommandResult::success();

    bool used[kCompanyColourCount] = {};
    for (const auto [id, co] : w.companies) used[co.colour % kCompanyColourCount] = true;
    u8 colour = x.colour;
    if (colour >= kCompanyColourCount || used[colour]) {
        colour = 0;
        while (colour < kCompanyColourCount - 1 && used[colour]) ++colour;
    }
    const CompanyId id = w.companies.create();
    Company& co = w.companies[id];
    co.name = x.name.empty()
                  ? generateCompanyName(static_cast<u32>(hashCombine(w.settings.seed, static_cast<u64>(w.clock.tick) * 31 +
                                                                                           id.value)))
                  : x.name;
    co.colour = colour;
    co.money = w.settings.initialLoan;
    co.loan = w.settings.initialLoan;
    co.foundedTick = w.clock.tick;
    co.thisYear.year = w.clock.date().year;
    co.thisMonth.year = w.clock.date().year;
    co.thisMonth.month = static_cast<u8>(w.clock.date().month);
    w.addNews(NewsKind::Company, std::format("{} has been founded", co.name));
    CommandResult r = CommandResult::success();
    r.createdId = id.value;
    return r;
}

CommandResult handle(World& w, const Command& c, const CmdSetLoan& x, bool exec) {
    Company* co = w.companies.tryGet(c.company);
    if (!co) return CommandResult::failure("No company");
    if (x.delta == 0 || x.delta % prices::kLoanStep != 0) return CommandResult::failure("Invalid loan amount");
    if (x.delta > 0 && co->loan + x.delta > w.settings.maxLoan)
        return CommandResult::failure(std::format("Maximum loan is {}", formatMoney(w.settings.maxLoan)));
    if (x.delta < 0) {
        if (co->loan + x.delta < 0) return CommandResult::failure("Cannot repay more than the loan");
        if (co->money < -x.delta) return CommandResult::failure("Not enough money to repay the loan");
    }
    if (exec) {
        co->loan += x.delta;
        co->money += x.delta;
    }
    return CommandResult::success();
}

CommandResult handle(World& w, const Command& c, const CmdUpdateCompany& x, bool exec) {
    Company* co = w.companies.tryGet(c.company);
    if (!co) return CommandResult::failure("No company");
    if (x.name.empty() || x.name.size() > 64) return CommandResult::failure("Company name must be 1 to 64 bytes");
    if (x.colour >= kCompanyColourCount) return CommandResult::failure("Invalid company colour");
    for (const auto [id, other] : w.companies)
        if (id != c.company && !other.bankrupt && other.colour == x.colour)
            return CommandResult::failure("Colour already used by another company");
    if (exec) { co->name = x.name; co->colour = x.colour; }
    return CommandResult::success();
}

CommandResult handle(World& w, const Command& c, const CmdTerraform& x, bool exec) {
    if (!w.companies.contains(c.company)) return CommandResult::failure("Only companies can terraform");
    const TerraformPlan plan = planTerraform(w.map, x.corner.x, x.corner.y, x.delta);
    if (!plan.ok) return CommandResult::failure(plan.error);
    const std::vector<TilePos> tiles = terraformAffectedTiles(plan);
    for (const TilePos t : tiles) {
        const Tile* tile = w.map.tile(t);
        if (!tile) return CommandResult::failure("Area not explored");
        if (tile->type == TileType::Water && tile->owner != kOwnerNone)
            return CommandResult::failure("Reclaim the canal before terraforming");
        if ((tile->type != TileType::Clear && tile->type != TileType::Water) || w.trackTiles.contains(ChunkPos{t.x, t.y}.key()))
            return CommandResult::failure("Something is in the way - demolish it first");
    }
    const Money cost = static_cast<Money>(plan.changes.size()) * prices::kTerraformPerCorner;
    if (exec) {
        applyTerraform(w.map, plan);
        for (const TilePos t : tiles) {
            Tile& tile = *w.map.tile(t);
            const TileCorners corners = w.map.corners(t);
            if (tile.type == TileType::Water && corners.max() > 0) {
                tile.type = TileType::Clear;
                tile.ground = Ground::Sand;
            }
            if (tile.type == TileType::Clear) {
                const Slope s = computeSlope(corners);
                if (s.isSteep() && tile.ground != Ground::Snow) tile.ground = Ground::Rock;
                else if (tile.ground == Ground::Rock && !s.isSteep()) tile.ground = Ground::Rough;
            }
            w.map.touch(t);
        }
    }
    return CommandResult::success(cost, Expense::Construction);
}

CommandResult handle(World& w, const Command& c, const CmdTerraformBrush& x, bool exec) {
    if (!w.companies.contains(c.company)) return CommandResult::failure("Only companies can terraform");
    if (x.mode > static_cast<u8>(BrushMode::Smooth)) return CommandResult::failure("Unknown terraform mode");
    if (x.radius < 1 || x.radius > 12) return CommandResult::failure("Brush too large");
    // A corner may move only if none of its four tiles holds anything (or owned water).
    const auto locked = [&](i32 cx, i32 cy) {
        for (const TilePos t : {TilePos{cx - 1, cy - 1}, TilePos{cx, cy - 1}, TilePos{cx - 1, cy}, TilePos{cx, cy}}) {
            const Tile* tile = w.map.tile(t);
            if (!tile) return true;
            if (tile->type == TileType::Water ? tile->owner != kOwnerNone : tile->type != TileType::Clear) return true;
            if (w.trackTiles.contains(ChunkPos{t.x, t.y}.key())) return true; // under a bridge or over a tunnel
        }
        return false;
    };
    const BrushPlan plan = planBrush(w.map, x.corner, x.radius, static_cast<BrushMode>(x.mode), x.target, locked);
    if (plan.changes.empty()) return CommandResult::failure("Nothing to change here");
    const Money cost = std::max<Money>(1, plan.volume * prices::kTerraformPerCorner / kSubLevels);
    if (exec) {
        applyBrush(w.map, plan);
        std::vector<TilePos> tiles;
        for (const FineChange& ch : plan.changes)
            for (const TilePos t : {TilePos{ch.cx - 1, ch.cy - 1}, TilePos{ch.cx, ch.cy - 1}, TilePos{ch.cx - 1, ch.cy},
                                    TilePos{ch.cx, ch.cy}})
                tiles.push_back(t);
        std::sort(tiles.begin(), tiles.end());
        tiles.erase(std::unique(tiles.begin(), tiles.end()), tiles.end());
        for (const TilePos t : tiles) {
            Tile* tile = w.map.tile(t);
            if (!tile) continue;
            const FineCorners fc = w.map.fineCorners(t);
            if (tile->type == TileType::Water && tile->owner == kOwnerNone && fc.max() > 0) {
                tile->type = TileType::Clear;
                tile->ground = Ground::Sand;
            }
            if (tile->type == TileType::Clear) {
                const bool steep = fc.max() - fc.min() >= 2 * kSubLevels + kSubLevels / 2;
                if (steep && tile->ground != Ground::Snow) tile->ground = Ground::Rock;
                else if (tile->ground == Ground::Rock && !steep) tile->ground = Ground::Rough;
            }
            w.map.touch(t);
        }
    }
    return CommandResult::success(cost, Expense::Construction);
}

CommandResult handle(World& w, const Command& c, const CmdClearArea& x, bool exec) {
    if (!w.companies.contains(c.company)) return CommandResult::failure("Only companies can clear land");
    const Rect r = normalise(x.a, x.b);
    if (r.area() > 128 * 128) return CommandResult::failure("Area too large");
    Money cost = 0;
    bool anything = false;
    std::string firstError;
    const std::unordered_set<u64> busy = occupiedTiles(w);
    for (i32 y = r.y0; y <= r.y1; ++y) {
        for (i32 xx = r.x0; xx <= r.x1; ++xx) {
            if (!w.map.tile({xx, y})) return CommandResult::failure("Area not explored");
            const CommandResult t = demolishTile(w, c.company, {xx, y}, exec, busy);
            if (t.ok) {
                anything = true;
                cost += t.cost;
            } else if (firstError.empty()) {
                firstError = t.error;
            }
        }
    }
    if (!anything) return CommandResult::failure(firstError.empty() ? "Nothing to clear" : firstError);
    return CommandResult::success(cost, Expense::Construction);
}

CommandResult handle(World& w, const Command& c, const CmdPlantTrees& x, bool exec) {
    if (!w.companies.contains(c.company)) return CommandResult::failure("Only companies can plant trees");
    const Rect r = normalise(x.a, x.b);
    if (r.area() > 64 * 64) return CommandResult::failure("Area too large");
    Money cost = 0;
    for (i32 y = r.y0; y <= r.y1; ++y) {
        for (i32 xx = r.x0; xx <= r.x1; ++xx) {
            Tile* tile = w.map.tile({xx, y});
            if (!tile) return CommandResult::failure("Area not explored");
            if (tile->type != TileType::Clear || tile->trees >= 4) continue;
            if (tile->ground != Ground::Grass && tile->ground != Ground::Rough && tile->ground != Ground::Snow) continue;
            cost += prices::kPlantTree;
            if (exec) {
                if (tile->trees == 0)
                    tile->treeType = w.map.tileBaseHeight({xx, y}) >= 14 ? TreeType::Conifer : TreeType::Deciduous;
                ++tile->trees;
                w.map.touch({xx, y});
            }
        }
    }
    if (cost == 0) return CommandResult::failure("No room for trees here");
    return CommandResult::success(cost, Expense::Construction);
}

} // namespace tp::cmd
