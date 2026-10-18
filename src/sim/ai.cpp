#include "sim/ai.hpp"
#include "sim/world.hpp"
#include "sim/commands.hpp"
#include "sim/road.hpp"
#include "sim/road_path.hpp"
#include "core/math.hpp"
#include "core/serialize.hpp"
#include <algorithm>
#include <format>

namespace tp::ai {
namespace {
// Plan on a copy, so unsuccessful attempts leave no stranded stops or depots.
bool buildRoute(World& w, CompanyId company, const Town& town) {
    std::vector<TilePos> sites;
    for (const auto p : town.roads) {
        const auto* tile = w.map.tile(p);
        if (tile && tile->type == TileType::Road && !tile->span && !tile->rail &&
            isStraight(roadPieces(*tile)) && w.map.slope(p).isFlat()) sites.push_back(p);
        if (sites.size() >= 48) break;
    }
    i32 attempts = 0;
    for (const auto a : sites) for (const auto b : sites) {
        if (manhattan(a.x-b.x, a.y-b.y) < 9) continue;
        if (++attempts > 16) return false;
        if (!findRoadPath(w, a, Dir::N, {b}, VehicleType::Road, {}) ||
            !findRoadPath(w, b, Dir::N, {a}, VehicleType::Road, {})) continue;
        World preview;
        fromBytes(toBytes(w, kSaveVersion), preview, kSaveVersion);
        std::vector<Command> plan;
        const auto run = [&](CommandPayload payload) {
            Command c; c.company = company; c.payload = std::move(payload);
            const auto result = executeCommand(preview, c);
            if (result.ok) plan.push_back(std::move(c));
            return result;
        };
        const auto sa = run(CmdBuildRoadStop{a, false});
        const auto sb = run(CmdBuildRoadStop{b, false});
        if (!sa.ok || !sb.ok) continue;
        const StationId start(preview.map.tile(a)->index), end(preview.map.tile(b)->index);
        if (start == end) continue;
        DepotId depot;
        for (const auto r : sites) {
            for (const auto dir : kDirs) {
                const auto candidate = run(CmdBuildRoadDepot{neighbour(r, dir), opposite(dir), false});
                if (candidate.ok) { depot = DepotId(candidate.createdId); break; }
            }
            if (depot.valid()) break;
        }
        if (!depot.valid()) continue;
        const auto& d = preview.depots[depot];
        if (!findRoadPath(preview, d.tile, opposite(d.facing), {a}, VehicleType::Road, depot)) continue;
        u16 model = 0;
        for (u16 m = 0; m < vehicleModels().size(); ++m) {
            const auto& info = vehicleModel(m);
            if (info.type == VehicleType::Road && info.capacity && info.introYear <= w.clock.date().year) model = m;
        }
        if (vehicleModel(model).introYear > w.clock.date().year) return false;
        const auto bought = run(CmdBuyVehicle{depot, model});
        if (!bought.ok) return false;
        const VehicleId vehicle(bought.createdId);
        Order oa; oa.station = start; Order ob; ob.station = end;
        if (!run(CmdSetOrders{vehicle, {oa, ob}}).ok || !run(CmdStartStopVehicle{vehicle}).ok) continue;
        // Keep enough cash for running costs; command validation enforces all construction costs.
        if (preview.companies[company].money < 50'000) return false;
        for (const auto& c : plan) executeCommand(w, c);
        w.addNews(NewsKind::Company, std::format("{} opened a bus route in {}", w.companies[company].name, town.name), a);
        return true;
    }
    return false;
}
}
void daily(World& w) {
    if (w.settings.aiCompetitors == 0 || w.companies.empty()) return;
    i32 count = 0;
    for (const auto [id, co] : w.companies) count += co.aiControlled;
    while (count < std::clamp(w.settings.aiCompetitors, 0, 3) && w.companies.size() < kMaxCompanies) {
        Command c; c.payload = CmdCreateCompany{std::format("Rival {} Transport", count + 1), 0xFF};
        const auto result = executeCommand(w, c);
        if (!result.ok) break;
        w.companies[CompanyId(static_cast<u8>(result.createdId))].aiControlled = true;
        ++count;
    }
    std::vector<CompanyId> competitors;
    for (const auto [id, co] : w.companies) if (co.aiControlled && !co.bankrupt) competitors.push_back(id);
    for (const auto company : competitors) {
        usize fleet = 0;
        for (const auto [vid, v] : w.vehicles) if (v.owner == company) ++fleet;
        if (fleet >= 8 || w.companies[company].money < 180'000) continue;
        std::vector<TownId> towns;
        for (const auto [tid, town] : w.towns) towns.push_back(tid);
        if (towns.empty()) continue;
        const usize cursor = static_cast<usize>(w.clock.tick / kTicksPerDay + company.value) % towns.size();
        usize attemptedTowns = 0;
        for (usize offset = 0; offset < towns.size() && attemptedTowns < 2; ++offset) {
            const auto tid = towns[(cursor + offset) % towns.size()];
            bool served = false;
            for (const auto [sid, station] : w.stations) if (station.owner == company && station.town == tid) served = true;
            if (served) continue;
            ++attemptedTowns;
            if (buildRoute(w, company, w.towns[tid])) break;
        }
    }
}
}
