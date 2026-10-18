// Free-form track tools: lay curved track that snaps onto track ends, and remove it again.

#include "client/app.hpp"
#include "client/ui/widgets.hpp"

#include <imgui.h>

#include "render/track_renderer.hpp"

#include <cmath>
#include <format>
#include <unordered_map>
#include <unordered_set>

namespace tp {

namespace {

constexpr i32 kSnapRadius = 150; // units: how close the pointer must be to a node to snap to it

TPoint toTrack(const glm::dvec3& p) {
    return {static_cast<i32>(std::lround(p.x * kTrackUnits)), static_cast<i32>(std::lround(p.z * kTrackUnits))};
}

glm::dvec3 toWorld(TPoint p, i32 h, double lift) {
    return {p.x / static_cast<double>(kTrackUnits), gfx::trackRailY(h) + lift, p.z / static_cast<double>(kTrackUnits)};
}

// A translucent ribbon along sampled track (the build preview).
void addRibbon(std::vector<gfx::OverlayQuad>& out, const std::vector<TrackSample>& sm, double halfWidth, u32 color,
               double lift = 0.07) {
    if (sm.size() < 2) return;
    const auto left = [&](usize i) {
        const TrackSample& a = sm[i == 0 ? 0 : i - 1];
        const TrackSample& b = sm[std::min(i + 1, sm.size() - 1)];
        glm::dvec2 t(b.p.x - a.p.x, b.p.z - a.p.z);
        const double len = glm::length(t);
        t = len > 0.0 ? t / len : glm::dvec2(1, 0);
        return glm::dvec2(t.y, -t.x) * halfWidth;
    };
    for (usize i = 0; i + 1 < sm.size(); ++i) {
        const glm::dvec3 a = toWorld(sm[i].p, sm[i].h, lift), b = toWorld(sm[i + 1].p, sm[i + 1].h, lift);
        const glm::dvec2 la = left(i), lb = left(i + 1);
        out.push_back({{{a.x + la.x, a.y, a.z + la.y},
                        {b.x + lb.x, b.y, b.z + lb.y},
                        {b.x - lb.x, b.y, b.z - lb.y},
                        {a.x - la.x, a.y, a.z - la.y}},
                       color,
                       true});
    }
}

// The ground as a plan's earthworks leave it, tinted by how far it is dug away (cuttings,
// rust) or built up (embankments, sand). Drawn at the new heights, so it shows through hills.
void addEarthworks(std::vector<gfx::OverlayQuad>& out, const Map& map, const std::vector<FineChange>& changes) {
    std::unordered_map<u64, u16> planned;
    std::unordered_set<u64> tiles;
    const auto key = [](i32 x, i32 y) { return (static_cast<u64>(static_cast<u32>(x)) << 32) | static_cast<u32>(y); };
    for (const FineChange& c : changes) {
        if (c.from == c.to) continue;
        planned[key(c.cx, c.cy)] = c.to;
        for (i32 dy = -1; dy <= 0; ++dy)
            for (i32 dx = -1; dx <= 0; ++dx) tiles.insert(key(c.cx + dx, c.cy + dy));
    }
    static constexpr i32 kCorner[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (const u64 k : tiles) {
        const i32 tx = static_cast<i32>(static_cast<u32>(k >> 32)), ty = static_cast<i32>(static_cast<u32>(k));
        glm::dvec3 corner[4];
        i32 change = 0; // the largest move of a corner, in fine units, signed
        for (int i = 0; i < 4; ++i) {
            const i32 cx = tx + kCorner[i][0], cy = ty + kCorner[i][1];
            const u16 now = map.cornerFine(cx, cy);
            const auto it = planned.find(key(cx, cy));
            const u16 h = it != planned.end() ? it->second : now;
            if (std::abs(h - now) > std::abs(change)) change = h - now;
            corner[i] = {static_cast<double>(cx), h * gfx::kHeightStep / kSubLevels + 0.03, static_cast<double>(cy)};
        }
        const u8 alpha = static_cast<u8>(std::clamp(70 + std::abs(change) * 6, 70, 170));
        const u32 colour = change < 0 ? gfx::rgba(196, 112, 72, alpha) : gfx::rgba(232, 204, 128, alpha);
        out.push_back({{corner[0], corner[1], corner[2], corner[3]}, colour, true});
    }
}

// A ring of small quads around a point (snap markers).
void addRing(std::vector<gfx::OverlayQuad>& out, const glm::dvec3& c, double radius, u32 color) {
    constexpr int kSegments = 18;
    const double w = radius * 0.28;
    for (int i = 0; i < kSegments; ++i) {
        const double a0 = 6.2831853 * i / kSegments, a1 = 6.2831853 * (i + 1) / kSegments;
        const glm::dvec2 d0(std::cos(a0), std::sin(a0)), d1(std::cos(a1), std::sin(a1));
        out.push_back({{{c.x + d0.x * radius, c.y, c.z + d0.y * radius},
                        {c.x + d1.x * radius, c.y, c.z + d1.y * radius},
                        {c.x + d1.x * (radius - w), c.y, c.z + d1.y * (radius - w)},
                        {c.x + d0.x * (radius - w), c.y, c.z + d0.y * (radius - w)}},
                       color});
    }
}

// The side of `n` an edge heading towards `towards` would leave through (preferring a free end).
TDir sideTowards(const TrackNode& n, TPoint towards) {
    const TDir to = normalised(static_cast<i64>(towards.x) - n.pos.x, static_cast<i64>(towards.z) - n.pos.z);
    int best = -1;
    i64 bestScore = INT64_MIN;
    for (u8 s = 0; s < 2; ++s) {
        if (!n.hasRoom(s)) continue;
        const i64 score = dot(n.side(s), to) + (n.count(s) == 0 ? static_cast<i64>(kDirOne) * kDirOne / 2 : 0);
        if (score > bestScore) {
            bestScore = score;
            best = s;
        }
    }
    return best < 0 ? TDir{0, 0} : n.side(static_cast<u8>(best));
}

void planTooltip(const TrackPlan& p, float scale) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * scale, 7.0f * scale));
    if (ImGui::BeginTooltip()) {
        if (p.ok) {
            ui::iconText(ui::Icon::Coins, ui::col::kAccent, std::format("Cost {}", formatMoney(p.cost)).c_str());
            ui::FontScope f(ui::fonts().small);
            const std::string details =
                std::format("{:.1f} tiles   gradient {:.1f}%   {}", p.length / static_cast<double>(kTrackUnits),
                            p.gradePermille / 10.0,
                            p.minRadius ? std::format("radius {:.1f} tiles", p.minRadius / static_cast<double>(kTrackUnits))
                                        : std::string("straight"));
            ui::mutedText(details.c_str());
            if (p.earthworks > 0) ui::mutedText(std::format("including earthworks {}", formatMoney(p.earthworks)).c_str());
        } else {
            ui::iconText(ui::Icon::Warning, ui::col::kRed, p.error.c_str(), ui::col::kRed);
        }
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
}

} // namespace

void App::handleTrackTool(std::vector<gfx::OverlayQuad>& overlays) {
    World& w = world();
    const CompanyId company = session_->localCompany();
    constexpr u32 kEnd = gfx::rgba(255, 210, 70, 200);
    constexpr u32 kSnap = gfx::rgba(120, 240, 140, 230);

    // Mark open track ends nearby: these are where new track joins.
    for (const auto [id, n] : w.trackNodes) {
        if (n.count(0) != 0 && n.count(1) != 0) continue;
        const double dx = n.pos.x / double(kTrackUnits) - camera_.target.x, dz = n.pos.z / double(kTrackUnits) - camera_.target.z;
        if (dx * dx + dz * dz > 90.0 * 90.0) continue;
        addRing(overlays, toWorld(n.pos, n.h, 0.08), 0.32, kEnd);
    }
    if (!hover_.hit || mouseOverUi_) return;
    const TPoint cursor = toTrack(hover_.point);
    const TrackNodeId snap = track::nodeNear(w, cursor, kSnapRadius);
    if (snap.valid()) {
        const TrackNode& n = w.trackNodes[snap];
        addRing(overlays, toWorld(n.pos, n.h, 0.09), 0.45, kSnap);
    }

    if (!trackStart_) {
        if (snap.valid()) {
            ImGui::SetTooltip("Click to continue this track");
        } else {
            ImGui::SetTooltip("Click to start a track");
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            TrackAnchor a;
            if (snap.valid()) {
                a.node = snap;
                a.pos = w.trackNodes[snap].pos;
            } else {
                a.pos = cursor;
            }
            trackStart_ = a;
        }
        return;
    }

    // The start: a node chooses its side towards the pointer.
    TrackAnchor from = *trackStart_;
    if (from.node.valid()) {
        if (const TrackNode* n = w.trackNodes.tryGet(from.node)) from.into = sideTowards(*n, cursor);
        else from.node = {};
    }
    TrackAnchor to;
    bool joined = false;
    if (snap.valid() && snap != from.node) {
        const TrackNode& n = w.trackNodes[snap];
        const TDir side = sideTowards(n, from.pos);
        if (!side.isZero()) {
            to.node = snap;
            to.pos = n.pos;
            to.into = side;
            joined = true;
        }
    }
    if (!joined) to.pos = cursor;

    const TrackPlan plan = track::plan(w, company, from, to);
    if (plan.ok) addEarthworks(overlays, w.map, plan.terrain);
    if (!plan.samples.empty()) {
        addRibbon(overlays, plan.samples, 0.34, plan.ok ? gfx::rgba(110, 230, 140, 120) : gfx::rgba(250, 90, 80, 130));
    } else {
        // Nothing planned (too short, explored, ...): a thin line shows where the track would go.
        addRibbon(overlays, {TrackSample{from.pos, track::terrainAt(w, from.pos), 0}, TrackSample{to.pos, track::terrainAt(w, to.pos), 1}},
                  0.08, gfx::rgba(250, 90, 80, 130));
    }
    planTooltip(plan, uiScale_);

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && plan.ok) {
        session_->submit(CmdBuildTrack{from, to});
        if (joined) {
            trackStart_.reset(); // connected up: done
        } else {
            // Carry on from the new end, in the direction the track was heading.
            const TrackSample& a = plan.samples[plan.samples.size() - 2];
            const TrackSample& b = plan.samples.back();
            TrackAnchor next;
            next.pos = b.p;
            next.into = normalised(static_cast<i64>(b.p.x) - a.p.x, static_cast<i64>(b.p.z) - a.p.z);
            trackStart_ = next;
        }
    }
}

void App::handleRemoveTrackTool(std::vector<gfx::OverlayQuad>& overlays) {
    World& w = world();
    if (!hover_.hit || mouseOverUi_) return;
    const TrackEdgeId e = track::edgeNear(w, toTrack(hover_.point), 140);
    if (!e.valid()) {
        ImGui::SetTooltip("Point at track to remove it");
        return;
    }
    const TrackEdge& edge = w.trackEdges[e];
    addRibbon(overlays, edge.samples, 0.36, gfx::rgba(250, 90, 80, 140));
    Command c;
    c.company = session_->localCompany();
    c.payload = CmdRemoveTrack{e};
    const CommandResult r = testCommand(w, c);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * uiScale_, 7.0f * uiScale_));
    if (ImGui::BeginTooltip()) {
        if (r.ok) ui::iconText(ui::Icon::Trash, ui::col::kRed, std::format("Remove track ({})", formatMoney(r.cost)).c_str());
        else ui::iconText(ui::Icon::Warning, ui::col::kRed, r.error.c_str(), ui::col::kRed);
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && r.ok) session_->submit(CmdRemoveTrack{e});
}

void App::handleStationTool(std::vector<gfx::OverlayQuad>& overlays) {
    World& w = world();
    const CompanyId company = session_->localCompany();
    for (const auto [id, n] : w.trackNodes) {
        if (n.count(0) != 0 && n.count(1) != 0) continue;
        const double dx = n.pos.x / double(kTrackUnits) - camera_.target.x, dz = n.pos.z / double(kTrackUnits) - camera_.target.z;
        if (dx * dx + dz * dz > 90.0 * 90.0) continue;
        addRing(overlays, toWorld(n.pos, n.h, 0.08), 0.32, gfx::rgba(255, 210, 70, 200));
    }
    if (!hover_.hit || mouseOverUi_) return;
    const TPoint cursor = toTrack(hover_.point);
    if (!stationStart_) {
        const TrackNodeId snap = track::nodeNear(w, cursor, kSnapRadius);
        const bool end = snap.valid() && (w.trackNodes[snap].count(0) == 0 || w.trackNodes[snap].count(1) == 0);
        if (end) addRing(overlays, toWorld(w.trackNodes[snap].pos, w.trackNodes[snap].h, 0.09), 0.45, gfx::rgba(120, 240, 140, 230));
        else addRing(overlays, glm::dvec3(hover_.point.x, hover_.point.y + 0.08, hover_.point.z), 0.3, gfx::rgba(120, 200, 255, 220));
        ImGui::SetTooltip(end ? "Click to start a platform from this track end" : "Click where the platform starts");
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            TrackAnchor a;
            if (end) {
                a.node = snap;
                a.pos = w.trackNodes[snap].pos;
            } else {
                a.pos = cursor;
            }
            stationStart_ = a;
        }
        return;
    }
    const TrackPlan plan = track::planPlatform(w, company, *stationStart_, cursor);
    // The ground levelled for it, then the station itself: platforms either side of the track.
    if (plan.ok) addEarthworks(overlays, w.map, plan.terrain);
    if (!plan.samples.empty()) {
        addRibbon(overlays, plan.samples, 0.95, plan.ok ? gfx::rgba(120, 200, 255, 120) : gfx::rgba(250, 90, 80, 130));
        addRibbon(overlays, plan.samples, 0.33, plan.ok ? gfx::rgba(70, 90, 110, 150) : gfx::rgba(150, 50, 45, 150), 0.08);
        addRibbon(overlays, plan.samples, 0.08, plan.ok ? gfx::rgba(255, 255, 255, 200) : gfx::rgba(250, 90, 80, 200), 0.09);
    }
    addRing(overlays, toWorld(stationStart_->pos, plan.samples.empty() ? 0 : plan.samples.front().h, 0.09), 0.32,
            gfx::rgba(120, 240, 140, 230));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * uiScale_, 7.0f * uiScale_));
    if (ImGui::BeginTooltip()) {
        if (plan.ok) {
            ui::iconText(ui::Icon::Station, ui::col::kBlue,
                         std::format("Station platform, {} tiles", plan.length / kTrackUnits).c_str());
            ui::iconText(ui::Icon::Coins, ui::col::kAccent, std::format("Cost {}", formatMoney(plan.cost)).c_str());
            ui::FontScope f(ui::fonts().small);
            if (plan.earthworks > 0)
                ui::mutedText(std::format("including levelling the ground: {}", formatMoney(plan.earthworks)).c_str());
            else
                ui::mutedText("The ground here is already level");
            ui::mutedText("Click to build, right-click to cancel");
        } else {
            ui::iconText(ui::Icon::Warning, ui::col::kRed, plan.error.c_str(), ui::col::kRed);
        }
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && plan.ok) {
        session_->submit(CmdBuildTrackStation{*stationStart_, cursor});
        stationStart_.reset();
    }
}

void App::handleDepotTool(std::vector<gfx::OverlayQuad>& overlays) {
    World& w = world();
    for (const auto [id, n] : w.trackNodes) {
        if (n.count(0) != 0 && n.count(1) != 0) continue;
        const double dx = n.pos.x / double(kTrackUnits) - camera_.target.x, dz = n.pos.z / double(kTrackUnits) - camera_.target.z;
        if (dx * dx + dz * dz > 90.0 * 90.0) continue;
        addRing(overlays, toWorld(n.pos, n.h, 0.08), 0.32, gfx::rgba(255, 210, 70, 200));
    }
    if (!hover_.hit || mouseOverUi_) return;
    const TrackNodeId snap = track::nodeNear(w, toTrack(hover_.point), kSnapRadius);
    if (!snap.valid()) {
        ImGui::SetTooltip("Point at the open end of a track");
        return;
    }
    const TrackPlan plan = track::planDepot(w, session_->localCompany(), snap);
    addRing(overlays, toWorld(w.trackNodes[snap].pos, w.trackNodes[snap].h, 0.09), 0.45,
            plan.ok ? gfx::rgba(120, 240, 140, 230) : gfx::rgba(250, 90, 80, 230));
    if (!plan.samples.empty()) {
        addRibbon(overlays, plan.samples, 0.34, plan.ok ? gfx::rgba(110, 230, 140, 120) : gfx::rgba(250, 90, 80, 130));
        addTileOverlay(overlays, tileAt(track::depotBuildingPos(plan)),
                       plan.ok ? gfx::rgba(110, 230, 140, 110) : gfx::rgba(250, 90, 80, 120), 0.06);
    }
    planTooltip(plan, uiScale_);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && plan.ok) session_->submit(CmdBuildTrackDepot{snap});
}

void App::handleSignalTool(std::vector<gfx::OverlayQuad>& overlays) {
    World& w = world();
    if (!hover_.hit || mouseOverUi_) return;
    const TrackNodeId node = track::nodeNear(w, toTrack(hover_.point), kSnapRadius, true);
    if (!node.valid() || w.trackNodes[node].count(0) == 0 || w.trackNodes[node].count(1) == 0) {
        ImGui::SetTooltip("Point where one piece of track joins the next");
        return;
    }
    const TrackNode& n = w.trackNodes[node];
    addRing(overlays, toWorld(n.pos, n.h, 0.09), 0.45, gfx::rgba(120, 240, 140, 230));
    Command c;
    c.company = session_->localCompany();
    c.payload = CmdTrackSignal{node};
    const CommandResult r = testCommand(w, c);
    static constexpr const char* kStates[4] = {"No signals", "One-way signal", "One-way signal", "Two-way signals"};
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * uiScale_, 7.0f * uiScale_));
    if (ImGui::BeginTooltip()) {
        ui::iconText(ui::Icon::Signal, ui::col::kGreen, std::format("{} - click to change", kStates[n.signals & 3]).c_str());
        if (!r.ok) ui::iconText(ui::Icon::Warning, ui::col::kRed, r.error.c_str(), ui::col::kRed);
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && r.ok) session_->submit(CmdTrackSignal{node});
}

} // namespace tp
