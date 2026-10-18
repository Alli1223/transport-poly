// Construction tools, map clicks and the build toolbar.

#include "client/app.hpp"
#include "client/ui/widgets.hpp"

#include <imgui.h>

#include "core/math.hpp"
#include "render/terrain_mesher.hpp"
#include "render/vehicle_pose.hpp"
#include "sim/rail.hpp"
#include "sim/road.hpp"
#include "sim/terraform.hpp"
#include "sim/water_path.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <unordered_set>

namespace tp {

namespace {

bool isDragTool(Tool t) {
    return t == Tool::Road || t == Tool::RemoveRoad || t == Tool::TramTrack || t == Tool::RemoveTram ||
           t == Tool::Clear || t == Tool::Trees || t == Tool::Canal || t == Tool::ReclaimWater ||
           t == Tool::RoadBridge || t == Tool::RailBridge || t == Tool::RoadTunnel || t == Tool::RailTunnel;
}

// Tools dragged along a straight line.
bool isRoadLikeTool(Tool t) {
    return t == Tool::Road || t == Tool::RemoveRoad || t == Tool::TramTrack || t == Tool::RemoveTram ||
           t == Tool::RailStation || t == Tool::RoadBridge || t == Tool::RailBridge ||
           t == Tool::RoadTunnel || t == Tool::RailTunnel;
}

// Tools dragged along a planned rail line (straight, then diagonal).
bool isRailLineTool(Tool) { return false; }

TilePos straighten(TilePos a, TilePos b) {
    return std::abs(b.x - a.x) >= std::abs(b.y - a.y) ? TilePos{b.x, a.y} : TilePos{a.x, b.y};
}

} // namespace

void App::addCatchmentOverlay(std::vector<gfx::OverlayQuad>& overlays, const std::vector<TilePos>& tiles, i32 radius,
                              u32 color) const {
    std::unordered_set<u64> done;
    for (const TilePos c : tiles) {
        for (i32 dy = -radius; dy <= radius; ++dy) {
            for (i32 dx = -radius; dx <= radius; ++dx) {
                const TilePos t{c.x + dx, c.y + dy};
                if (!done.insert(ChunkPos{t.x, t.y}.key()).second) continue;
                if (session_->world().map.tile(t)) addTileOverlay(overlays, t, color, 0.06);
            }
        }
        if (done.size() > 6000) break;
    }
}

void App::addTileOverlay(std::vector<gfx::OverlayQuad>& overlays, TilePos t, u32 color, double lift) const {
    const FineCorners f = session_->world().map.fineCorners(t);
    double h[4];
    for (int i = 0; i < 4; ++i) h[i] = f.h[i] * gfx::kHeightStep / kSubLevels;
    if (const Tile* tile = session_->world().map.tile(t); tile && (tile->span & 3) == kSpanBridge)
        for (double& height : h) height = tile->deckHeight * gfx::kHeightStep;
    const double x = t.x, z = t.y;
    overlays.push_back({{{x, h[0] + lift, z}, {x + 1, h[1] + lift, z}, {x + 1, h[2] + lift, z + 1}, {x, h[3] + lift, z + 1}},
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
    case Tool::RailTrack: return CmdBuildRail{a, b};
    case Tool::RemoveRail: return CmdRemoveRail{a, b};
    case Tool::RailStation: return CmdBuildRailStation{a, straighten(a, b)};
    case Tool::Canal: return CmdEditWater{a, b, true};
    case Tool::ReclaimWater: return CmdEditWater{a, b, false};
    case Tool::RoadBridge: return CmdBuildSpan{a, straighten(a, b), false, false};
    case Tool::RailBridge: return CmdBuildSpan{a, straighten(a, b), true, false};
    case Tool::RoadTunnel: return CmdBuildSpan{a, straighten(a, b), false, true};
    case Tool::RailTunnel: return CmdBuildSpan{a, straighten(a, b), true, true};
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
    case TileType::Track:
        if (const auto it = w.trackTiles.find(ChunkPos{hover_.tile.x, hover_.tile.y}.key()); it != w.trackTiles.end())
            for (const TrackEdgeId e : it->second)
                if (w.trackEdges[e].kind == TrackKind::Platform && w.stations.contains(w.trackEdges[e].station))
                    selStation_ = w.trackEdges[e].station;
        break;
    case TileType::Depot: selDepot_ = DepotId(t->index); break;
    case TileType::Station: selStation_ = StationId(t->index); break;
    case TileType::Building:
        if (const Building* b = w.buildings.tryGet(BuildingId(t->index))) selTown_ = b->town;
        break;
    default: break;
    }
}

void App::handleTools(std::vector<gfx::OverlayQuad>& overlays) {
    if (autoScreenshot_ && !previewHover_) return;
    const bool esc = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::GetIO().WantTextInput;
    const bool rightClick = !mouseOverUi_ && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    if (tool_ != Tool::RailTrack) trackStart_.reset();
    if (tool_ != Tool::RailStation) stationStart_.reset();
    if ((trackStart_ || stationStart_) && (esc || rightClick)) {
        // First stop laying the current track or platform, then leave the tool.
        trackStart_.reset();
        stationStart_.reset();
        if (esc) escConsumed_ = true;
    } else if ((esc && tool_ != Tool::None) || (rightClick && tool_ != Tool::None && !dragStart_)) {
        tool_ = Tool::None;
        dragStart_.reset();
        if (esc) escConsumed_ = true;
    }
    if (tool_ == Tool::None) {
        handleClick();
        return;
    }
    if (tool_ == Tool::RailTrack) {
        handleTrackTool(overlays);
        return;
    }
    if (tool_ == Tool::RemoveRail) {
        handleRemoveTrackTool(overlays);
        return;
    }
    if (tool_ == Tool::RailStation) {
        handleStationTool(overlays);
        return;
    }
    if (tool_ == Tool::RailDepot) {
        handleDepotTool(overlays);
        return;
    }
    if (tool_ == Tool::Signal) {
        handleSignalTool(overlays);
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
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * uiScale_, 7.0f * uiScale_));
        if (ImGui::BeginTooltip()) {
            if (r.ok && r.cost > 0)
                ui::iconText(ui::Icon::Coins, ui::col::kAccent, std::format("Cost {}", formatMoney(r.cost)).c_str());
            else if (r.ok)
                ui::iconText(ui::Icon::Check, ui::col::kGreen, r.cost < 0 ? std::format("Refund {}", formatMoney(-r.cost)).c_str()
                                                                          : "Free");
            else
                ui::iconText(ui::Icon::Warning, ui::col::kRed, r.error.c_str(), ui::col::kRed);
            ImGui::EndTooltip();
        }
        ImGui::PopStyleVar();
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
        // Line tools only make sense once dragged (a single click removes all track of a tile).
        const bool line = isRoadLikeTool(tool_) || isRailLineTool(tool_);
        const bool needsDrag = line && tool_ != Tool::RailStation && tool_ != Tool::RemoveRail;
        const CommandResult r = (dragStart_ || !line || tool_ == Tool::RailStation) ? test(*cmd) : CommandResult::success();
        const u32 color = r.ok ? kOk : kBad;
        if (tool_ == Tool::RailStation) {
            std::vector<TilePos> tiles;
            const TilePos e = straighten(a, b);
            for (i32 y = std::min(a.y, e.y); y <= std::max(a.y, e.y); ++y)
                for (i32 x = std::min(a.x, e.x); x <= std::max(a.x, e.x); ++x) tiles.push_back({x, y});
            addCatchmentOverlay(overlays, tiles, catchmentRadius(StopKind::Rail), gfx::rgba(90, 170, 255, 45));
        }
        if (isRailLineTool(tool_)) {
            addTileOverlay(overlays, a, color);
            for (const RailStep& st : planRailLine(a, b)) addTileOverlay(overlays, st.tile, color);
        } else {
            const i32 x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x), y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
            if ((x1 - x0 + 1) * (y1 - y0 + 1) <= 4096)
                for (i32 y = y0; y <= y1; ++y)
                    for (i32 x = x0; x <= x1; ++x) addTileOverlay(overlays, {x, y}, color);
        }
        if (dragStart_ || tool_ == Tool::RailStation) showResult(r);
        if (dragStart_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (!(needsDrag && a == b)) session_->submit(*cmd);
            dragStart_.reset();
        }
        return;
    }

    if (!hover_.hit || mouseOverUi_) return;
    std::optional<CommandPayload> cmd;
    switch (tool_) {
    case Tool::Raise:
    case Tool::Lower:
    case Tool::Level:
    case Tool::Smooth: {
        // Brush preview: tiles fade out towards the edge of the brush.
        const i32 r = brushRadius_;
        for (i32 dy = -r; dy < r; ++dy)
            for (i32 dx = -r; dx < r; ++dx) {
                const double ox = dx + 0.5, oy = dy + 0.5;
                const double k = 1.0 - (ox * ox + oy * oy) / (r * r);
                if (k <= 0.0) continue;
                addTileOverlay(overlays, {hover_.corner.x + dx, hover_.corner.y + dy},
                               gfx::rgba(255, 210, 70, static_cast<u8>(30 + 120 * k * k)), 0.05);
            }
        if (tool_ == Tool::Level && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            levelTarget_ = w.map.cornerFine(hover_.corner.x, hover_.corner.y);
        const BrushMode mode = tool_ == Tool::Raise   ? BrushMode::Raise
                               : tool_ == Tool::Lower ? BrushMode::Lower
                               : tool_ == Tool::Level ? BrushMode::Level
                                                      : BrushMode::Smooth;
        const u16 target = tool_ == Tool::Level && ImGui::IsMouseDown(ImGuiMouseButton_Left)
                               ? levelTarget_
                               : w.map.cornerFine(hover_.corner.x, hover_.corner.y);
        const CmdTerraformBrush brush{hover_.corner, static_cast<u8>(r), static_cast<u8>(mode), target};
        const CommandResult br = test(brush);
        showResult(br);
        // Holding the mouse keeps shaping the land.
        const double now = ImGui::GetTime();
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && br.ok && now - lastBrush_ > 0.12) {
            session_->submit(brush);
            lastBrush_ = now;
        }
        return;
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
    case Tool::Signal: cmd = CmdCycleSignal{hover_.tile}; break;
    case Tool::BlockSignal: cmd = CmdCycleSignal{hover_.tile, true}; break;
    case Tool::RemoveSpan: cmd = CmdRemoveSpan{hover_.tile}; break;
    case Tool::RailDepot: {
        // Face track on the side of the tile the cursor is closest to, or any adjacent track.
        Dir facing = hover_.edge;
        const auto railAt = [&](Dir d) {
            const Tile* n = w.map.tile(neighbour(hover_.tile, d));
            return n && n->type == TileType::Rail && (n->rail & tracksAtEdge(opposite(d)));
        };
        if (!railAt(facing))
            for (const Dir d : kDirs)
                if (railAt(d)) {
                    facing = d;
                    break;
                }
        cmd = CmdBuildRailDepot{hover_.tile, facing};
        break;
    }
    case Tool::Dock:
    case Tool::ShipDepot: {
        // Face the water on the side of the tile the cursor is closest to, or any adjacent water.
        Dir facing = hover_.edge;
        const auto waterAt = [&](Dir d) { return isWaterTile(w, neighbour(hover_.tile, d)); };
        if (!waterAt(facing))
            for (const Dir d : kDirs)
                if (waterAt(d)) {
                    facing = d;
                    break;
                }
        if (tool_ == Tool::Dock)
            cmd = CmdBuildDock{hover_.tile, facing};
        else
            cmd = CmdBuildShipDepot{hover_.tile, facing};
        break;
    }
    case Tool::PickOrder: {
        const Tile* t = w.map.tile(hover_.tile);
        Vehicle* v = w.vehicles.tryGet(selVehicle_);
        if (!v) {
            tool_ = Tool::None;
            return;
        }
        addTileOverlay(overlays, hover_.tile, t && (pickViaOrder_ || t->type == TileType::Station || t->type == TileType::Depot) ? kOk : kBad);
        if (pickViaOrder_ && t) {
            std::vector<Order> orders = v->orders;
            Order o;
            o.kind = OrderKind::Via;
            o.waypoint = hover_.tile;
            orders.push_back(o);
            cmd = CmdSetOrders{selVehicle_, orders};
        } else if (t && t->type == TileType::Station) {
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
            ImGui::SetTooltip("Click a station, depot, or enable Via waypoint to choose track / road / water");
            return;
        }
        break;
    }
    default: break;
    }
    if (!cmd) return;
    const CommandResult r = test(*cmd);
    if (tool_ == Tool::BusStop || tool_ == Tool::TramStop || tool_ == Tool::Dock) {
        const StopKind kind = tool_ == Tool::BusStop ? StopKind::Bus : tool_ == Tool::TramStop ? StopKind::Tram : StopKind::Dock;
        addCatchmentOverlay(overlays, {hover_.tile}, catchmentRadius(kind), gfx::rgba(90, 170, 255, 45));
    }
    if (tool_ != Tool::Raise && tool_ != Tool::Lower && tool_ != Tool::PickOrder)
        addTileOverlay(overlays, hover_.tile, r.ok ? kOk : kBad);
    showResult(r);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) session_->submit(*cmd);
}

namespace {

struct GroupInfo {
    ToolGroup group;
    ui::Icon icon;
    const char* name;
    const char* body;
    const char* key;
    ImU32 colour;
};

constexpr GroupInfo kGroups[] = {
    {ToolGroup::Land, ui::Icon::Landscape, "Landscape", "Raise and lower land, demolish and plant trees", "1", ui::col::kLand},
    {ToolGroup::Road, ui::Icon::Road, "Roads", "Roads, bus stops and bus depots", "2", ui::col::kRoad},
    {ToolGroup::Tram, ui::Icon::Tram, "Trams", "Tram track, tram stops and tram depots", "3", ui::col::kTram},
    {ToolGroup::Rail, ui::Icon::Train, "Railways", "Track, signals, stations and train depots", "4", ui::col::kRail},
    {ToolGroup::Water, ui::Icon::Ship, "Waterways", "Canals, water reclamation, docks and shipyards", "5", ui::col::kWater},
};

struct ToolInfo {
    Tool tool;
    ToolGroup group;
    ui::Icon icon;
    const char* label;
    const char* tip;
    bool remove;
};

constexpr ToolInfo kTools[] = {
    {Tool::Raise, ToolGroup::Land, ui::Icon::Raise, "Raise", "Hold the mouse to raise the land under the brush", false},
    {Tool::Lower, ToolGroup::Land, ui::Icon::Lower, "Lower", "Hold the mouse to lower the land under the brush", false},
    {Tool::Level, ToolGroup::Land, ui::Icon::Level, "Level",
     "Click to pick a height, then hold and move to flatten the land to it", false},
    {Tool::Smooth, ToolGroup::Land, ui::Icon::Smooth, "Smooth", "Hold the mouse to smooth out bumps and edges", false},
    {Tool::Clear, ToolGroup::Land, ui::Icon::Bulldoze, "Bulldoze", "Drag to demolish everything in an area", false},
    {Tool::Trees, ToolGroup::Land, ui::Icon::Tree, "Trees", "Drag to plant trees", false},
    {Tool::Road, ToolGroup::Road, ui::Icon::Road, "Road", "Drag to build a straight road", false},
    {Tool::RemoveRoad, ToolGroup::Road, ui::Icon::Road, "Remove", "Drag to remove road", true},
    {Tool::BusStop, ToolGroup::Road, ui::Icon::BusStop, "Bus stop", "Build a drive-through stop on a straight road",
     false},
    {Tool::RoadDepot, ToolGroup::Road, ui::Icon::Depot, "Depot", "Build a bus depot next to a road", false},
    {Tool::RoadBridge, ToolGroup::Road, ui::Icon::Road, "Bridge", "Drag between equal-height flat banks across a valley or water", false},
    {Tool::RoadTunnel, ToolGroup::Road, ui::Icon::Road, "Tunnel", "Drag between equal-height flat ends through a hill", false},
    {Tool::RemoveSpan, ToolGroup::Road, ui::Icon::Bulldoze, "Remove span", "Remove an entire owned bridge or tunnel", true},
    {Tool::TramTrack, ToolGroup::Tram, ui::Icon::Track, "Track", "Drag to lay tram track, on roads or on its own",
     false},
    {Tool::RemoveTram, ToolGroup::Tram, ui::Icon::Track, "Remove", "Drag to remove tram track", true},
    {Tool::TramStop, ToolGroup::Tram, ui::Icon::BusStop, "Tram stop", "Build a tram stop on straight track", false},
    {Tool::TramDepot, ToolGroup::Tram, ui::Icon::Depot, "Depot", "Build a tram depot next to tram track", false},
    {Tool::RailTrack, ToolGroup::Rail, ui::Icon::Track, "Track",
     "Click to start, click again to lay curved track; it carries on from there. Start on a track end to "
     "continue it, finish on one to join up",
     false},
    {Tool::RemoveRail, ToolGroup::Rail, ui::Icon::Track, "Remove", "Click a piece of track to remove it", true},
    {Tool::Signal, ToolGroup::Rail, ui::Icon::Signal, "Signal",
     "Click where track joins track to add path signals; click again for one-way and back to none", false},
    {Tool::RailStation, ToolGroup::Rail, ui::Icon::Station, "Station",
     "Click to start a platform (on a track end to line it up), click again to set its length. The ground is "
     "levelled for it",
     false},
    {Tool::RailDepot, ToolGroup::Rail, ui::Icon::Depot, "Depot", "Click the open end of a track to put a train depot there",
     false},
    {Tool::RemoveSpan, ToolGroup::Rail, ui::Icon::Bulldoze, "Remove span", "Remove an entire owned bridge or tunnel", true},
    {Tool::Dock, ToolGroup::Water, ui::Icon::Anchor, "Dock",
     "Build a dock on the shore; ships moor at the water tile in front of it", false},
    {Tool::ShipDepot, ToolGroup::Water, ui::Icon::Shipyard, "Shipyard",
     "Build a shipyard on the shore, facing the water", false},
    {Tool::Canal, ToolGroup::Water, ui::Icon::Ship, "Canal",
     "Drag to flood clear land at sea level. Lower and level the terrain first", false},
    {Tool::ReclaimWater, ToolGroup::Water, ui::Icon::Landscape, "Reclaim",
     "Drag over water to reclaim land. Ships and dock berths must be cleared first", true},
};

const GroupInfo* groupInfo(ToolGroup g) {
    for (const GroupInfo& gi : kGroups)
        if (gi.group == g) return &gi;
    return nullptr;
}

} // namespace

void App::drawToolbar() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float s = uiScale_;

    // --- The dock: tool groups and windows ---------------------------------------------
    ui::beginBar("##dock", ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y - 12.0f * s),
                 ImVec2(0.5f, 1.0f), ImVec2(9, 8), 20.0f);
    for (const GroupInfo& g : kGroups) {
        ImGui::PushID(static_cast<int>(g.group));
        if (ui::iconButton("##group", g.icon, 46, toolGroup_ == g.group, g.colour, {g.name, g.body, g.key})) {
            toolGroup_ = toolGroup_ == g.group ? ToolGroup::None : g.group;
            tool_ = Tool::None;
            dragStart_.reset();
        }
        ImGui::PopID();
        ImGui::SameLine(0, 4.0f * s);
    }
    {
        ImGui::SameLine(0, 8.0f * s);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(1.0f, 46.0f * s));
        ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, pos.y + 9.0f * s), ImVec2(pos.x, pos.y + 37.0f * s),
                                            ui::col::kBorderStrong);
        ImGui::SameLine(0, 12.0f * s);
    }
    struct WindowButton {
        const char* id;
        ui::Icon icon;
        bool* flag;
        ui::Tip tip;
    };
    const WindowButton windows[] = {
        {"##vehicles", ui::Icon::Bus, &showVehicleList_, {"Vehicles", "All of your buses, trams, trains and ships", "V"}},
        {"##finances", ui::Icon::Coins, &showFinances_, {"Finances", "Income, expenses and your loan", "F"}},
        {"##towns", ui::Icon::Town, &showTownList_, {"Towns", "Every town you have discovered", "T"}},
        {"##citizens", ui::Icon::People, &showCitizens_, {"Citizens", "Inspect a resident's schedule and follow their journey", "C"}},
        {"##news", ui::Icon::News, &showNews_, {"News", "What has been happening", "N"}},
        {"##map", ui::Icon::Map, &showMinimap_, {"Map", "Overview of the explored world", "M"}},
    };
    for (const WindowButton& b : windows) {
        if (ui::iconButton(b.id, b.icon, 46, *b.flag, ui::col::kAccent, b.tip)) *b.flag = !*b.flag;
        if (&b != &windows[std::size(windows) - 1]) ImGui::SameLine(0, 4.0f * s);
    }
    dockHeight_ = ImGui::GetWindowSize().y;
    ui::endBar();

    // --- Flyout with the tools of the open group -----------------------------------------
    const GroupInfo* g = groupInfo(toolGroup_);
    float hintBottom = vp->WorkPos.y + vp->WorkSize.y - dockHeight_ - 22.0f * s;
    if (g) {
        ui::beginBar("##flyout", ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, hintBottom), ImVec2(0.5f, 1.0f),
                     ImVec2(12, 10), 18.0f);
        {
            ui::FontScope f(ui::fonts().small);
            ui::iconInline(g->icon, g->colour);
            ImGui::SameLine(0, 6.0f * s);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(g->colour));
            ImGui::TextUnformatted(g->name);
            ImGui::PopStyleColor();
        }
        bool first = true;
        for (const ToolInfo& t : kTools) {
            if (t.group != toolGroup_) continue;
            if (!first) ImGui::SameLine(0, 6.0f * s);
            first = false;
            ImGui::PushID(static_cast<int>(t.tool));
            if (ui::toolTile("##tool", t.icon, t.label, tool_ == t.tool, g->colour, {t.label, t.tip}, t.remove)) {
                tool_ = tool_ == t.tool ? Tool::None : t.tool;
                dragStart_.reset();
            }
            ImGui::PopID();
        }
        if (toolGroup_ == ToolGroup::Land) {
            // Brush size for the terraform tools.
            ImGui::SameLine(0, 14.0f * s);
            ImGui::BeginGroup();
            ImGui::Dummy(ImVec2(0, 6.0f * s));
            ui::fieldLabel("Brush size");
            static constexpr ui::Segment kSizes[] = {{"S", ui::Icon::None, "1 tile"},
                                                     {"M", ui::Icon::None, "3 tiles"},
                                                     {"L", ui::Icon::None, "5 tiles"},
                                                     {"XL", ui::Icon::None, "8 tiles"}};
            static constexpr i32 kRadii[] = {1, 3, 5, 8};
            int current = -1;
            for (int i = 0; i < 4; ++i)
                if (brushRadius_ == kRadii[i]) current = i;
            if (const int clicked = ui::segmented("##brush", kSizes, current); clicked >= 0) brushRadius_ = kRadii[clicked];
            ImGui::EndGroup();
        }
        hintBottom = ImGui::GetWindowPos().y - 10.0f * s;
        ui::endBar();
    }

    // --- Hint for the active tool ---------------------------------------------------------
    const ToolInfo* active = nullptr;
    for (const ToolInfo& t : kTools)
        if (t.tool == tool_) active = &t;
    if (active && g) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImFont* f = ui::fonts().small;
        const std::string text = std::format("{}  -  right-click or Esc to stop", active->tip);
        const ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.0f, text.c_str());
        const float w = ts.x + 44.0f * s, h = ts.y + 14.0f * s;
        const ImVec2 mn(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f, hintBottom - h);
        const ImVec2 mx(mn.x + w, mn.y + h);
        dl->AddRectFilled(mn, mx, ui::withAlpha(ui::col::kBar, 0.92f), h * 0.5f);
        dl->AddRect(mn, mx, ui::withAlpha(g->colour, 0.5f), h * 0.5f);
        ui::drawIcon(dl, active->icon, ImVec2(mn.x + 18.0f * s, (mn.y + mx.y) * 0.5f), 14.0f * s, g->colour);
        dl->AddText(f, f->FontSize, ImVec2(mn.x + 32.0f * s, mn.y + 7.0f * s), ui::col::kText, text.c_str());
    }
}

} // namespace tp
