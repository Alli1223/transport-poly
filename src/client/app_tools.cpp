// Construction tools, map clicks and the build toolbar.

#include "client/app.hpp"

#include <imgui.h>

#include "core/math.hpp"
#include "render/terrain_mesher.hpp"
#include "render/vehicle_pose.hpp"
#include "sim/road.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace tp {

namespace {

bool isDragTool(Tool t) {
    return t == Tool::Road || t == Tool::RemoveRoad || t == Tool::TramTrack || t == Tool::RemoveTram ||
           t == Tool::Clear || t == Tool::Trees;
}

bool isRoadLikeTool(Tool t) {
    return t == Tool::Road || t == Tool::RemoveRoad || t == Tool::TramTrack || t == Tool::RemoveTram;
}

TilePos straighten(TilePos a, TilePos b) {
    return std::abs(b.x - a.x) >= std::abs(b.y - a.y) ? TilePos{b.x, a.y} : TilePos{a.x, b.y};
}

} // namespace

void App::addTileOverlay(std::vector<gfx::OverlayQuad>& overlays, TilePos t, u32 color, double lift) const {
    const TileCorners c = session_->world().map.corners(t);
    const double x = t.x, z = t.y;
    overlays.push_back({{{x, c.h[0] * gfx::kHeightStep + lift, z},
                         {x + 1, c.h[1] * gfx::kHeightStep + lift, z},
                         {x + 1, c.h[2] * gfx::kHeightStep + lift, z + 1},
                         {x, c.h[3] * gfx::kHeightStep + lift, z + 1}},
                        color});
}

std::optional<CommandPayload> App::dragCommand(TilePos a, TilePos b) const {
    switch (tool_) {
    case Tool::Road: return CmdBuildRoad{a, straighten(a, b), false};
    case Tool::TramTrack: return CmdBuildRoad{a, straighten(a, b), true};
    case Tool::RemoveRoad: return CmdRemoveRoad{a, straighten(a, b), false};
    case Tool::RemoveTram: return CmdRemoveRoad{a, straighten(a, b), true};
    case Tool::Clear: return CmdClearArea{a, b};
    case Tool::Trees: return CmdPlantTrees{a, b};
    default: return std::nullopt;
    }
}

VehicleId App::pickVehicle() const {
    const World& w = session_->world();
    VehicleId best;
    double bestT = 1e18;
    for (const auto [id, v] : w.vehicles) {
        if (v.state == VehicleState::InDepot) continue;
        const double dx = v.tile.x - camera_.target.x, dz = v.tile.y - camera_.target.z;
        if (dx * dx + dz * dz > 250.0 * 250.0) continue;
        const auto pose = gfx::vehiclePose(w, v, session_->tickFraction());
        if (!pose) continue;
        const glm::dvec3 c = pose->pos + glm::dvec3(0, 0.12, 0);
        const double t = glm::dot(c - mouseRay_.origin, mouseRay_.dir);
        if (t < 0) continue;
        const double dist = glm::length(mouseRay_.origin + mouseRay_.dir * t - c);
        if (dist < 0.35 && t < bestT) {
            bestT = t;
            best = id;
        }
    }
    return best;
}

void App::handleClick() {
    if (mouseOverUi_ || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    World& w = world();
    if (const VehicleId v = pickVehicle(); v.valid()) {
        selVehicle_ = v;
        return;
    }
    if (!hover_.hit) return;
    const Tile* t = w.map.tile(hover_.tile);
    if (!t) return;
    switch (t->type) {
    case TileType::Depot: selDepot_ = DepotId(t->index); break;
    case TileType::Station: selStation_ = StationId(t->index); break;
    case TileType::Building:
        if (const Building* b = w.buildings.tryGet(BuildingId(t->index))) selTown_ = b->town;
        break;
    default: break;
    }
}

void App::handleTools(std::vector<gfx::OverlayQuad>& overlays) {
    if (autoScreenshot_) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) || (!mouseOverUi_ && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
                                                 tool_ != Tool::None && !dragStart_)) {
        tool_ = Tool::None;
        dragStart_.reset();
    }
    if (tool_ == Tool::None) {
        handleClick();
        return;
    }
    World& w = world();
    const CompanyId company = session_->localCompany();
    const auto test = [&](const CommandPayload& p) {
        Command c;
        c.company = company;
        c.payload = p;
        return testCommand(w, c);
    };
    const auto showResult = [&](const CommandResult& r) {
        if (r.ok)
            ImGui::SetTooltip("%s", r.cost > 0 ? std::format("Cost: {}", formatMoney(r.cost)).c_str() : "OK");
        else
            ImGui::SetTooltip("%s", r.error.c_str());
    };
    constexpr u32 kOk = gfx::rgba(120, 240, 120, 110);
    constexpr u32 kBad = gfx::rgba(250, 90, 80, 120);

    if (isDragTool(tool_)) {
        if (dragStart_ && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            dragStart_.reset();
            return;
        }
        if (!hover_.hit) return;
        if (!mouseOverUi_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) dragStart_ = hover_.tile;
        const TilePos a = dragStart_.value_or(hover_.tile);
        const TilePos b = isRoadLikeTool(tool_) ? straighten(a, hover_.tile) : hover_.tile;
        const auto cmd = dragCommand(a, hover_.tile);
        if (!cmd) return;
        const CommandResult r = (dragStart_ || !isRoadLikeTool(tool_)) ? test(*cmd) : CommandResult::success();
        const u32 color = r.ok ? kOk : kBad;
        const i32 x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x), y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
        if ((x1 - x0 + 1) * (y1 - y0 + 1) <= 4096)
            for (i32 y = y0; y <= y1; ++y)
                for (i32 x = x0; x <= x1; ++x) addTileOverlay(overlays, {x, y}, color);
        if (dragStart_) showResult(r);
        if (dragStart_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (!(isRoadLikeTool(tool_) && a == b)) session_->submit(*cmd);
            dragStart_.reset();
        }
        return;
    }

    if (!hover_.hit || mouseOverUi_) return;
    std::optional<CommandPayload> cmd;
    switch (tool_) {
    case Tool::Raise:
    case Tool::Lower: {
        const double cx = hover_.corner.x, cz = hover_.corner.y;
        const double cyy = w.map.cornerHeight(hover_.corner.x, hover_.corner.y) * gfx::kHeightStep + 0.05;
        const double r = 0.18;
        overlays.push_back({{{cx - r, cyy, cz - r}, {cx + r, cyy, cz - r}, {cx + r, cyy, cz + r}, {cx - r, cyy, cz + r}},
                            gfx::rgba(255, 220, 60, 200)});
        cmd = CmdTerraform{hover_.corner, static_cast<i8>(tool_ == Tool::Raise ? 1 : -1)};
        break;
    }
    case Tool::BusStop: cmd = CmdBuildRoadStop{hover_.tile, false}; break;
    case Tool::TramStop: cmd = CmdBuildRoadStop{hover_.tile, true}; break;
    case Tool::RoadDepot:
    case Tool::TramDepot: {
        const bool tram = tool_ == Tool::TramDepot;
        // Face the road on the side of the tile the cursor is closest to, or any adjacent road.
        Dir facing = hover_.edge;
        const auto roadAt = [&](Dir d) {
            const Tile* n = w.map.tile(neighbour(hover_.tile, d));
            return n && n->type == TileType::Road;
        };
        if (!roadAt(facing))
            for (const Dir d : kDirs)
                if (roadAt(d)) {
                    facing = d;
                    break;
                }
        cmd = CmdBuildRoadDepot{hover_.tile, facing, tram};
        break;
    }
    case Tool::PickOrder: {
        const Tile* t = w.map.tile(hover_.tile);
        Vehicle* v = w.vehicles.tryGet(selVehicle_);
        if (!v) {
            tool_ = Tool::None;
            return;
        }
        addTileOverlay(overlays, hover_.tile, t && t->type == TileType::Station ? kOk : kBad);
        if (t && t->type == TileType::Station) {
            std::vector<Order> orders = v->orders;
            Order o;
            o.station = StationId(t->index);
            orders.push_back(o);
            cmd = CmdSetOrders{selVehicle_, orders};
        } else if (t && t->type == TileType::Depot) {
            std::vector<Order> orders = v->orders;
            Order o;
            o.kind = OrderKind::Depot;
            o.depot = DepotId(t->index);
            orders.push_back(o);
            cmd = CmdSetOrders{selVehicle_, orders};
        } else {
            ImGui::SetTooltip("Click a station (or depot) to add it to the orders");
            return;
        }
        break;
    }
    default: break;
    }
    if (!cmd) return;
    const CommandResult r = test(*cmd);
    if (tool_ != Tool::Raise && tool_ != Tool::Lower && tool_ != Tool::PickOrder)
        addTileOverlay(overlays, hover_.tile, r.ok ? kOk : kBad);
    showResult(r);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) session_->submit(*cmd);
}

void App::drawToolbar() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y - 8.0f),
                            ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.8f);
    ImGui::Begin("##tools", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    const auto button = [&](const char* label, Tool t, const char* tip) {
        const bool active = tool_ == t;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label)) {
            tool_ = active ? Tool::None : t;
            dragStart_.reset();
        }
        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::SameLine();
    };
    const auto separator = [] {
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    };
    button("Raise", Tool::Raise, "Raise a terrain corner");
    button("Lower", Tool::Lower, "Lower a terrain corner");
    button("Bulldoze", Tool::Clear, "Drag to demolish an area");
    button("Trees", Tool::Trees, "Drag to plant trees");
    separator();
    button("Road", Tool::Road, "Drag to build a straight road");
    button("X Road", Tool::RemoveRoad, "Drag to remove road");
    button("Bus stop", Tool::BusStop, "Build a drive-through bus stop on a straight road");
    button("Bus depot", Tool::RoadDepot, "Build a bus depot next to a road");
    separator();
    button("Tram track", Tool::TramTrack, "Drag to lay tram track (on roads or on its own)");
    button("X Tram", Tool::RemoveTram, "Drag to remove tram track");
    button("Tram stop", Tool::TramStop, "Build a tram stop on straight track");
    button("Tram depot", Tool::TramDepot, "Build a tram depot next to tram track");
    separator();
    if (ImGui::Button("Vehicles")) showVehicleList_ = !showVehicleList_;
    ImGui::SameLine();
    if (ImGui::Button("Finances")) showFinances_ = !showFinances_;
    ImGui::End();
}

} // namespace tp
