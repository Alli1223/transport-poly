// The curved road tool, and a curved road demo for automated screenshots.

#include "client/app.hpp"
#include "client/ui/widgets.hpp"

#include <imgui.h>

#include "core/log.hpp"
#include "core/serialize.hpp"
#include "render/picking.hpp"
#include "sim/road.hpp"
#include "sim/road_curves.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>

namespace tp {

namespace {

// The one road piece of a dead end, if the tile is one.
std::optional<Dir> deadEnd(const World& w, TilePos t) {
    const Tile* tile = w.map.tile(t);
    if (!tile || tile->type != TileType::Road || std::popcount(static_cast<unsigned>(roadPieces(*tile))) != 1) return std::nullopt;
    for (const Dir d : kDirs)
        if (roadPieces(*tile) == dirBit(d)) return d;
    return std::nullopt;
}

// The side a curve leaves its start tile through: straight on out of a dead end, otherwise the
// free side facing the pointer.
std::optional<Dir> startSide(const World& w, TilePos start, glm::dvec2 pointer) {
    if (const auto end = deadEnd(w, start)) return opposite(*end);
    const Tile* tile = w.map.tile(start);
    const u8 pieces = tile && tile->type == TileType::Road ? roadPieces(*tile) : 0;
    const double dx = pointer.x - (start.x + 0.5), dz = pointer.y - (start.y + 0.5);
    const Dir alongX = dx >= 0 ? Dir::E : Dir::W, alongZ = dz >= 0 ? Dir::S : Dir::N;
    const bool xFirst = std::abs(dx) >= std::abs(dz);
    for (const Dir d : {xFirst ? alongX : alongZ, xFirst ? alongZ : alongX})
        if (!(pieces & dirBit(d))) return d;
    return std::nullopt;
}

// The side the curve joins its end tile through: into a dead end there when it can, otherwise
// straight on or round a quarter turn, whichever the pointer suggests.
Dir endSide(const World& w, TilePos a, Dir sideA, TilePos b) {
    if (const auto end = deadEnd(w, b)) {
        RoadCurve c;
        c.a = a;
        c.sideA = sideA;
        c.b = b;
        c.sideB = opposite(*end);
        if (roadcurves::shape(c)) return c.sideB;
    }
    return roadcurves::naturalEndSide(a, sideA, b);
}

// A translucent ribbon over a curve's centre line, as wide as the road.
void addCurveRibbon(std::vector<gfx::OverlayQuad>& out, const World& w, const RoadCurve& c, u32 colour) {
    const std::vector<TrackSample>& sm = c.samples;
    if (sm.size() < 2) return;
    const usize step = std::max<usize>(1, static_cast<usize>(24) * (sm.size() - 1) / static_cast<usize>(std::max(1, c.length)));
    std::vector<usize> at;
    for (usize i = 0; i < sm.size(); i += step) at.push_back(i);
    if (at.back() != sm.size() - 1) at.push_back(sm.size() - 1);
    const auto point = [&](usize i, double side) {
        glm::dvec2 d;
        if (i == 0 || i + 1 == sm.size()) {
            const TilePos axis = dirOffset(i == 0 ? c.sideA : opposite(c.sideB)); // square to the grid at the ends
            d = glm::dvec2(axis.x, axis.y);
        } else {
            const TPoint before = sm[i >= 2 ? i - 2 : 0].p, after = sm[std::min(i + 2, sm.size() - 1)].p;
            d = glm::dvec2(static_cast<double>(after.x) - before.x, static_cast<double>(after.z) - before.z);
            d = glm::length(d) > 0.0 ? glm::normalize(d) : glm::dvec2(1.0, 0.0);
        }
        const double x = sm[i].p.x / static_cast<double>(kTrackUnits) + d.y * side;
        const double z = sm[i].p.z / static_cast<double>(kTrackUnits) - d.x * side;
        return glm::dvec3(x, gfx::terrainHeightAt(w.map, x, z) + 0.08, z);
    };
    for (usize k = 0; k + 1 < at.size(); ++k)
        out.push_back({{point(at[k], 0.4), point(at[k + 1], 0.4), point(at[k + 1], -0.4), point(at[k], -0.4)}, colour, true});
}

} // namespace

void App::handleRoadCurveTool(std::vector<gfx::OverlayQuad>& overlays) {
    World& w = world();
    constexpr u32 kStart = gfx::rgba(255, 210, 70, 150);
    constexpr u32 kOk = gfx::rgba(120, 240, 120, 120);
    constexpr u32 kBad = gfx::rgba(250, 90, 80, 130);
    if (!hover_.hit || mouseOverUi_) return;
    if (!curveStart_) {
        addTileOverlay(overlays, hover_.tile, kStart);
        ImGui::SetTooltip("Click a road end, or open land, to start a curved road");
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) curveStart_ = hover_.tile;
        return;
    }
    const TilePos a = *curveStart_, b = hover_.tile;
    addTileOverlay(overlays, a, kStart);
    const auto side = startSide(w, a, {hover_.point.x, hover_.point.z});
    if (a == b || !side) {
        ImGui::SetTooltip("Point to where the curved road should go\nRight-click to stop");
        return;
    }
    const Dir sideB = endSide(w, a, *side, b);
    const CmdBuildRoadCurve cmd{a, *side, b, sideB};
    Command c;
    c.company = session_->localCompany();
    c.payload = cmd;
    const CommandResult r = testCommand(w, c);
    const RoadCurvePlan plan = roadcurves::plan(w, a, *side, b, sideB, c.company.value);
    if (!plan.curve.samples.empty()) {
        addCurveRibbon(overlays, w, plan.curve, r.ok ? kOk : kBad);
        for (const TilePos t : plan.curve.verge) addTileOverlay(overlays, t, gfx::rgba(255, 255, 255, 26));
    }
    addTileOverlay(overlays, b, r.ok ? kOk : kBad);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * uiScale_, 7.0f * uiScale_));
    if (ImGui::BeginTooltip()) {
        if (r.ok)
            ui::iconText(ui::Icon::Coins, ui::col::kAccent, std::format("Cost {}", formatMoney(r.cost)).c_str());
        else
            ui::iconText(ui::Icon::Warning, ui::col::kRed, r.error.c_str(), ui::col::kRed);
        if (!plan.curve.samples.empty() && plan.curve.length > 0) {
            const double radius = plan.curve.minRadius / static_cast<double>(kTrackUnits);
            if (plan.curve.minRadius > 0)
                ImGui::TextDisabled("%.1f tiles long, %.1f tile radius", plan.curve.length / static_cast<double>(kTrackUnits), radius);
        }
        ImGui::TextDisabled("Right-click to stop");
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
    if (r.ok && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const Tile* end = w.map.tile(b);
        const bool joined = end && end->type == TileType::Road;
        session_->submit(cmd);
        // Carry on from the new end; joining a road finishes the curve.
        if (joined) curveStart_.reset();
        else curveStart_ = b;
    }
}

// Lays a straight road, a quarter turn, another straight, an S-curve and a last straight on open
// land near `centre`, with a bus stop at each end, a depot and two buses running between them.
// Returns the middle of the quarter turn (used for automated screenshots / smoke tests).
std::optional<TilePos> buildDemoRoadCurves(World& w, CompanyId company, TilePos centre) {
    w.companies[company].money = std::max<Money>(w.companies[company].money, 20'000'000);
    u16 model = 0;
    for (u16 m = 0; m < vehicleModels().size(); ++m) {
        const auto& info = vehicleModel(m);
        if (info.type == VehicleType::Road && info.capacity && !info.intercity && info.introYear <= w.clock.date().year) model = m;
    }
    for (i32 r = 0; r < 60; r += 3)
        for (i32 dy = -r; dy <= r; dy += 3)
            for (i32 dx = -r; dx <= r; dx += 3) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const TilePos o{centre.x + dx, centre.y + dy};
                bool open = true;
                for (i32 y = -2; y <= 27 && open; ++y)
                    for (i32 x = -2; x <= 17 && open; ++x) {
                        const Tile* tile = w.map.tile({o.x + x, o.y + y});
                        open = tile && tile->type == TileType::Clear && !w.map.slope({o.x + x, o.y + y}).isSteep();
                    }
                if (!open) continue;
                // Try it out on a copy first.
                World preview;
                fromBytes(toBytes(w, kSaveVersion), preview, kSaveVersion);
                std::vector<Command> plan;
                const auto run = [&](CommandPayload payload) {
                    Command c;
                    c.company = company;
                    c.payload = std::move(payload);
                    const CommandResult result = executeCommand(preview, c);
                    if (result.ok) plan.push_back(std::move(c));
                    return result;
                };
                const auto at = [&](i32 x, i32 y) { return TilePos{o.x + x, o.y + y}; };
                if (!run(CmdBuildRoad{at(0, 0), at(6, 0), false}).ok || !run(CmdBuildRoadCurve{at(6, 0), Dir::E, at(10, 4), Dir::N}).ok ||
                    !run(CmdBuildRoad{at(10, 4), at(10, 10), false}).ok ||
                    !run(CmdBuildRoadCurve{at(10, 10), Dir::S, at(12, 17), Dir::N}).ok ||
                    !run(CmdBuildRoad{at(12, 17), at(12, 22), false}).ok)
                    continue;
                StationId first, second;
                for (const TilePos t : {at(2, 0), at(3, 0), at(1, 0), at(4, 0)})
                    if (run(CmdBuildRoadStop{t, false}).ok) {
                        first = StationId(preview.map.tile(t)->index);
                        break;
                    }
                for (const TilePos t : {at(12, 20), at(12, 19), at(12, 21), at(12, 18)})
                    if (run(CmdBuildRoadStop{t, false}).ok) {
                        second = StationId(preview.map.tile(t)->index);
                        break;
                    }
                if (!first.valid() || !second.valid()) continue;
                DepotId depot;
                for (const auto& [tile, facing] : {std::pair{at(1, -1), Dir::S}, std::pair{at(1, 1), Dir::N}, std::pair{at(5, -1), Dir::S},
                                                   std::pair{at(5, 1), Dir::N}})
                    if (const CommandResult d = run(CmdBuildRoadDepot{tile, facing, false}); d.ok) {
                        depot = DepotId(d.createdId);
                        break;
                    }
                if (!depot.valid()) continue;
                Order oa, ob;
                oa.station = first;
                ob.station = second;
                for (int bus = 0; bus < 2; ++bus) {
                    const CommandResult bought = run(CmdBuyVehicle{depot, model});
                    if (!bought.ok) break;
                    const VehicleId vehicle(bought.createdId);
                    run(CmdSetOrders{vehicle, bus == 0 ? std::vector<Order>{oa, ob} : std::vector<Order>{ob, oa}});
                    run(CmdStartStopVehicle{vehicle});
                }
                for (const Command& c : plan)
                    if (const CommandResult result = executeCommand(w, c); !result.ok)
                        log::warn("Curved road demo: {} failed: {}", commandName(c.payload), result.error);
                return at(8, 2);
            }
    return std::nullopt;
}

} // namespace tp
