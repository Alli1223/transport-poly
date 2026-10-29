// Free-form track tools: lay curved track that snaps onto track ends, and remove it again.

#include "client/app.hpp"
#include "client/ui/widgets.hpp"
#include "core/hash.hpp"
#include "core/log.hpp"

#include <imgui.h>

#include "render/track_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <set>
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

// The build preview: the planned track, coloured by what carries it (ground, bridge, tunnel).
void addPlanRibbon(std::vector<gfx::OverlayQuad>& out, const TrackPlan& plan, double halfWidth) {
    if (plan.samples.size() < 2) return;
    if (!plan.ok) {
        addRibbon(out, plan.samples, halfWidth, gfx::rgba(250, 90, 80, 130));
        return;
    }
    const auto colour = [](TrackStructure k) {
        switch (k) {
        case TrackStructure::Bridge: return gfx::rgba(110, 175, 255, 140);
        case TrackStructure::Tunnel: return gfx::rgba(196, 140, 255, 140);
        default: return gfx::rgba(110, 230, 140, 120);
        }
    };
    const auto kindOf = [&](usize i) { return track::structureAt(plan.spans, (plan.samples[i].s + plan.samples[i + 1].s) / 2); };
    std::vector<TrackSample> run{plan.samples[0]};
    TrackStructure current = kindOf(0);
    for (usize i = 0; i + 1 < plan.samples.size(); ++i) {
        if (const TrackStructure k = kindOf(i); k != current) {
            addRibbon(out, run, halfWidth, colour(current));
            run = {plan.samples[i]};
            current = k;
        }
        run.push_back(plan.samples[i + 1]);
    }
    addRibbon(out, run, halfWidth, colour(current));
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

// A flat arrow on the track at p pointing along d, `length` tiles long (crossed through when
// `barred`: no way past).
void addArrow(std::vector<gfx::OverlayQuad>& out, TPoint p, i32 h, TDir d, double length, u32 color, double lift = 0.1,
              bool barred = false) {
    const glm::dvec2 f(d.x / double(kDirOne), d.z / double(kDirOne));
    const glm::dvec2 r(-f.y, f.x);
    const glm::dvec3 base = toWorld(p, h, lift);
    const auto at = [&](double along, double side) {
        return glm::dvec3(base.x + f.x * along + r.x * side, base.y, base.z + f.y * along + r.y * side);
    };
    const double start = 0.18, shaft = length * 0.62, half = 0.06, head = 0.2;
    out.push_back({{at(start, -half), at(shaft, -half), at(shaft, half), at(start, half)}, color, true});
    out.push_back({{at(shaft, -head), at(length, 0.0), at(length, 0.0), at(shaft, head)}, color, true});
    if (barred) {
        const double mid = (start + length) * 0.5;
        out.push_back({{at(mid - 0.05, -0.3), at(mid + 0.05, -0.3), at(mid + 0.05, 0.3), at(mid - 0.05, 0.3)}, color, true});
    }
}

// A highlighted signal: the block it guards, the other way through a two-way signal, and the
// block trains reach a one-way signal from.
constexpr u32 kGuardedColour = gfx::rgba(92, 230, 140, 240);
constexpr u32 kOtherWayColour = gfx::rgba(255, 186, 66, 240);
constexpr u32 kApproachColour = gfx::rgba(110, 170, 255, 240);

// Colours for telling signal blocks apart.
constexpr u8 kBlockColours = 8;
constexpr u32 kBlockPalette[kBlockColours] = {
    gfx::rgba(255, 186, 66), gfx::rgba(88, 166, 255), gfx::rgba(92, 214, 144), gfx::rgba(255, 108, 168),
    gfx::rgba(196, 140, 255), gfx::rgba(72, 206, 218), gfx::rgba(255, 140, 72), gfx::rgba(200, 230, 90)};

// The point along the nearest track within `radius` where it could be split (for a switch or
// a signal), if there is one.
TrackSplit splitNear(const World& w, TPoint p, i32 radius) {
    const TrackEdgeId e = track::edgeNear(w, p, radius);
    return e.valid() ? track::splitAt(w, e, p) : TrackSplit{};
}

// The node a split would add, for choosing its sides.
TrackNode switchAt(const TrackSplit& at) {
    TrackNode n;
    n.pos = at.pos;
    n.h = at.h;
    n.dir = at.dir;
    n.grade = at.grade;
    return n;
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

// Height profile of a plan: the ground along it and the track over it, so it shows where the
// track keeps to the lie of the land and where it needs cuttings, embankments, bridges or tunnels.
void planProfile(const World& w, const TrackPlan& p, float scale) {
    if (p.samples.size() < 2) return;
    std::vector<float> ground(p.samples.size());
    float lo = 1e9f, hi = -1e9f;
    for (usize i = 0; i < p.samples.size(); ++i) {
        ground[i] = static_cast<float>(track::terrainAt(w, p.samples[i].p)) / kSubLevels;
        const float rail = static_cast<float>(p.samples[i].h) / kSubLevels;
        lo = std::min({lo, ground[i], rail});
        hi = std::max({hi, ground[i], rail});
    }
    // At least four levels tall, so that gentle slopes look gentle.
    const float span = std::max(hi - lo, 4.0f) * 1.2f, bottom = (lo + hi - span) * 0.5f;
    const ImVec2 size{300.0f * scale, 70.0f * scale}, origin = ImGui::GetCursorScreenPos();
    const float pad = 4.0f * scale;
    ImGui::Dummy(size);
    const double length = std::max(1, p.samples.back().s);
    const auto at = [&](usize i, float h) {
        return ImVec2(origin.x + pad + static_cast<float>((size.x - 2 * pad) * p.samples[i].s / length),
                      origin.y + size.y - pad - (size.y - 2 * pad) * (h - bottom) / span);
    };
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(12, 22, 30, 255), 4.0f * scale);
    const float floor = origin.y + size.y - pad;
    // Side by side slices of ground, without anti-aliased edges that would show as seams.
    const ImDrawListFlags flags = draw->Flags;
    draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    for (usize i = 0; i + 1 < p.samples.size(); ++i) {
        const ImVec2 a = at(i, ground[i]), b = at(i + 1, ground[i + 1]);
        draw->AddQuadFilled(a, b, ImVec2(b.x, floor), ImVec2(a.x, floor), IM_COL32(112, 92, 64, 255));
    }
    draw->Flags = flags;
    for (usize i = 0; i + 1 < p.samples.size(); ++i)
        draw->AddLine(at(i, ground[i]), at(i + 1, ground[i + 1]), IM_COL32(150, 190, 96, 255), 1.5f * scale);
    for (usize i = 0; i + 1 < p.samples.size(); ++i) {
        const TrackStructure k = track::structureAt(p.spans, (p.samples[i].s + p.samples[i + 1].s) / 2);
        const ImU32 colour = !p.ok ? IM_COL32(250, 90, 80, 255)
                           : k == TrackStructure::Bridge ? IM_COL32(110, 175, 255, 255)
                           : k == TrackStructure::Tunnel ? IM_COL32(196, 140, 255, 255) : IM_COL32(240, 240, 232, 255);
        draw->AddLine(at(i, p.samples[i].h / static_cast<float>(kSubLevels)),
                      at(i + 1, p.samples[i + 1].h / static_cast<float>(kSubLevels)), colour, 2.0f * scale);
    }
    ui::mutedText(std::format("Height {:.1f} to {:.1f}: the track (light) over the ground (green)",
                              p.samples.front().h / static_cast<double>(kSubLevels), p.samples.back().h / static_cast<double>(kSubLevels))
                      .c_str());
}

void planTooltip(const World& w, const TrackPlan& p, float scale) {
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
            std::string structures;
            const auto add = [&](std::string part) { structures += (structures.empty() ? "" : "   ") + part; };
            if (p.bridgeLength) add(std::format("bridge {:.1f} tiles", p.bridgeLength / static_cast<double>(kTrackUnits)));
            if (p.tunnelLength) add(std::format("tunnel {:.1f} tiles", p.tunnelLength / static_cast<double>(kTrackUnits)));
            if (p.levelCrossings) add(std::format("{} level crossing{}", p.levelCrossings, p.levelCrossings == 1 ? "" : "s"));
            if (!structures.empty()) ui::mutedText(structures.c_str());
            if (p.earthworks > 0) ui::mutedText(std::format("including earthworks {}", formatMoney(p.earthworks)).c_str());
            planProfile(w, p, scale);
        } else {
            ui::iconText(ui::Icon::Warning, ui::col::kRed, p.error.c_str(), ui::col::kRed);
        }
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
}

} // namespace

bool App::elevationTool() const { return tool_ == Tool::RailTrack || tool_ == Tool::RailStation || tool_ == Tool::RailDepot; }

// The elevation view is on with the track tools (unless switched off) and whenever H turns it
// on. It marks out the height of the track being laid - follow the line to keep it level - or
// before that, the height of the ground under the pointer.
void App::updateElevationView(double dt) {
    const bool on = elevationTool() ? preferences_.trackContours : elevationView_;
    elevationStrength_ += ((on ? 1.0f : 0.0f) - elevationStrength_) * std::min(1.0f, static_cast<float>(dt) * 8.0f);
    renderer_.elevation.strength = elevationStrength_ < 0.01f ? 0.0f : elevationStrength_;
    const World& w = world();
    const std::optional<TrackAnchor>& start = trackStart_ ? trackStart_ : stationStart_;
    renderer_.elevation.markLevel = -1000.0f;
    if (start) {
        const TrackNode* node = start->node.valid() ? w.trackNodes.tryGet(start->node) : nullptr;
        renderer_.elevation.markLevel = static_cast<float>(node ? node->h : track::terrainAt(w, start->pos)) / kSubLevels;
    } else if (hover_.hit && !mouseOverUi_) {
        renderer_.elevation.markLevel = static_cast<float>(hover_.point.y / gfx::kHeightStep);
    }
}

void App::drawStationUpgradePreview(std::vector<gfx::OverlayQuad>& overlays) {
    if (!stationUpgradePreview_ || !world().stations.contains(selStation_)) return;
    World& w = world();
    const CompanyId company = session_->localCompany();
    Command command; command.company = company; command.payload = *stationUpgradePreview_;
    const auto result = testCommand(w, command);
    TrackPlan plan;
    std::vector<TrackEdgeId> source;
    TPoint arrowFrom{}, arrowTo{};
    bool conversion = false;
    if (const auto* add = std::get_if<CmdAddPlatform>(&command.payload)) {
        if (!w.trackEdges.contains(add->platform)) return;
        source = track::platformChain(w, add->platform);
        const auto ends = track::platformEnds(w, add->platform);
        if (ends.size() != 2) return;
        const auto& node = w.trackNodes[ends.front().node];
        const auto direction = -ends.front().outward;
        const TDir normal{-direction.z, direction.x};
        TrackAnchor from;
        // Match CmdAddPlatform: left/right are relative to the platform's own orientation.
        from.pos = offset(node.pos, normal, add->side * 2 * kTrackUnits);
        const auto to = offset(from.pos, direction, add->length * kTrackUnits);
        plan = track::planPlatform(w, company, from, to);
        if (plan.samples.empty()) plan.samples = {{from.pos, node.h, 0}, {to, node.h, add->length * kTrackUnits}};
        arrowTo = offset(from.pos, direction, add->length * kTrackUnits / 2);
        arrowFrom = offset(arrowTo, normal, -add->side * 2 * kTrackUnits);
    } else if (const auto* extend = std::get_if<CmdExtendPlatform>(&command.payload)) {
        std::optional<track::PlatformEnd> end;
        for (const auto& platform : track::platformsOf(w, extend->station))
            for (const auto& candidate : track::platformEnds(w, platform.front()))
                if (candidate.node == extend->end) end = candidate;
        if (!end) return;
        source = track::platformChain(w, end->edge);
        const auto& node = w.trackNodes[end->node];
        const auto& base = w.trackEdges[end->edge];
        const u8 outside = 1 - (base.a == end->node ? base.sideA : base.sideB);
        arrowFrom = node.pos;
        arrowTo = offset(node.pos, end->outward, extend->length * kTrackUnits);
        if (node.count(outside) == 1) {
            // Connected ends convert the entire next section, regardless of the length slider.
            const auto& target = w.trackEdges[node.edges[outside * 2]];
            plan.samples = target.samples;
            plan.length = target.length;
            arrowTo = w.trackNodes[target.a == end->node ? target.b : target.a].pos;
            conversion = true;
        } else {
            TrackAnchor from; from.node = end->node; from.pos = node.pos;
            plan = track::planPlatform(w, company, from, arrowTo);
        }
        if (plan.samples.empty()) plan.samples = {{arrowFrom, node.h, 0}, {arrowTo, node.h, extend->length * kTrackUnits}};
        addRing(overlays, toWorld(node.pos, node.h, 0.24), 0.5, gfx::rgba(255, 210, 70, 240));
    } else return;

    for (const auto edge : source)
        addRibbon(overlays, w.trackEdges[edge].samples, 0.95, gfx::rgba(255, 210, 70, 105), 0.18);
    if (result.ok) addEarthworks(overlays, w.map, plan.terrain);
    const u32 colour = result.ok ? gfx::rgba(100, 200, 255, 170) : gfx::rgba(250, 90, 80, 170);
    addRibbon(overlays, plan.samples, 0.95, colour, 0.19);
    addRibbon(overlays, plan.samples, 0.33, gfx::rgba(55, 75, 95, 180), 0.20);
    addRibbon(overlays, plan.samples, 0.08, gfx::rgba(255, 255, 255, 230), 0.21);

    // A world-space arrow remains aligned with the change as the camera rotates.
    const i32 height = plan.samples.front().h;
    const auto a = toWorld(arrowFrom, height, 0.27), b = toWorld(arrowTo, height, 0.27);
    glm::dvec3 direction = b - a;
    const double distance = glm::length(direction);
    if (distance > 0.01) {
        direction /= distance;
        const glm::dvec3 side{-direction.z, 0, direction.x};
        const auto head = b - direction * std::min(0.65, distance * 0.4);
        overlays.push_back({{a + side * 0.09, head + side * 0.09, head - side * 0.09, a - side * 0.09}, colour, true});
        overlays.push_back({{b, head + side * 0.35, head - side * 0.35, b}, colour, true});
    }
    const i32 dx = arrowTo.x - arrowFrom.x, dz = arrowTo.z - arrowFrom.z;
    const bool eastWest = std::abs(dx) * 2 > std::abs(dz);
    const bool northSouth = std::abs(dz) * 2 > std::abs(dx);
    std::string heading;
    if (northSouth) heading = dz > 0 ? "south" : "north";
    if (eastWest) heading += (heading.empty() ? "" : "-") + std::string(dx > 0 ? "east" : "west");
    const double length = (plan.length > 0 ? plan.length : plan.samples.back().s) / double(kTrackUnits);
    const std::string description = conversion ? std::format("Widen the next {:.1f} tiles into platform, towards {}", length, heading)
        : std::format("{} {:.1f} tiles, towards {}", std::holds_alternative<CmdAddPlatform>(command.payload) ? "New platform:" : "Extend by", length, heading);
    if (const auto screen = worldToScreen(b + glm::dvec3(0, 0.6, 0))) {
        auto* draw = ImGui::GetBackgroundDrawList();
        const std::string label = result.ok ? "Proposed platform" : "Blocked platform";
        const ImVec2 size = ImGui::CalcTextSize(label.c_str());
        const ImVec2 pos{screen->x - size.x / 2, screen->y - size.y};
        draw->AddRectFilled(ImVec2(pos.x - 5, pos.y - 3), ImVec2(pos.x + size.x + 5, pos.y + size.y + 3), IM_COL32(15, 25, 35, 220), 4);
        draw->AddText(pos, result.ok ? IM_COL32(140, 220, 255, 255) : IM_COL32(255, 130, 110, 255), label.c_str());
    }
    if (!ImGui::BeginTooltip()) return;
    ImGui::TextUnformatted(stationUpgradeLabel_.c_str());
    ImGui::TextUnformatted(description.c_str());
    // A north-up plan stays readable even if the station window covers the world preview.
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size{380.0f * uiScale_, 125.0f * uiScale_};
    ImGui::Dummy(size);
    auto* diagram = ImGui::GetWindowDrawList();
    diagram->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(12, 22, 30, 255), 4);
    TPoint lo = plan.samples.front().p, hi = lo;
    const auto include = [&](const std::vector<TrackSample>& samples) {
        for (const auto& sample : samples) {
            lo.x = std::min(lo.x, sample.p.x); lo.z = std::min(lo.z, sample.p.z);
            hi.x = std::max(hi.x, sample.p.x); hi.z = std::max(hi.z, sample.p.z);
        }
    };
    include(plan.samples);
    for (const auto edge : source) include(w.trackEdges[edge].samples);
    const double scale = std::min((size.x - 36 * uiScale_) / (hi.x - lo.x + 3.0 * kTrackUnits),
                                  (size.y - 20 * uiScale_) / (hi.z - lo.z + 3.0 * kTrackUnits));
    const auto project = [&](TPoint p) {
        return ImVec2(origin.x + size.x / 2 + static_cast<float>((p.x - (lo.x + hi.x) / 2.0) * scale),
                      origin.y + size.y / 2 + static_cast<float>((p.z - (lo.z + hi.z) / 2.0) * scale));
    };
    const auto line = [&](const std::vector<TrackSample>& samples, ImU32 tint) {
        for (usize i = 1; i < samples.size(); ++i) {
            const auto p = project(samples[i - 1].p), q = project(samples[i].p);
            diagram->AddLine(p, q, tint, static_cast<float>(1.9 * kTrackUnits * scale));
            diagram->AddLine(p, q, IM_COL32(35, 45, 55, 240), std::max(1.0f, static_cast<float>(0.6 * kTrackUnits * scale)));
        }
    };
    for (const auto edge : source) line(w.trackEdges[edge].samples, IM_COL32(255, 210, 70, 190));
    const ImU32 tint = result.ok ? IM_COL32(100, 200, 255, 255) : IM_COL32(250, 90, 80, 255);
    line(plan.samples, tint);
    const auto p = project(arrowFrom), q = project(arrowTo);
    const float arrowLength = std::hypot(q.x - p.x, q.y - p.y);
    if (arrowLength > 1.0f) {
        const float ux = (q.x - p.x) / arrowLength, uy = (q.y - p.y) / arrowLength;
        const float head = std::min(8.0f * uiScale_, arrowLength * 0.45f);
        diagram->AddLine(p, q, IM_COL32_WHITE, 2 * uiScale_);
        diagram->AddTriangleFilled(q, ImVec2(q.x - ux * head - uy * head * 0.5f, q.y - uy * head + ux * head * 0.5f),
                                      ImVec2(q.x - ux * head + uy * head * 0.5f, q.y - uy * head - ux * head * 0.5f), IM_COL32_WHITE);
    }
    diagram->AddText(ImVec2(origin.x + 6 * uiScale_, origin.y + 4 * uiScale_), IM_COL32(200, 210, 220, 255), "N ^");
    ui::mutedText("Gold: existing platform. Blue: proposed addition. Red: blocked.");
    if (result.ok) ImGui::Text("Cost %s - click the button to build", formatMoney(result.cost).c_str());
    else ui::iconText(ui::Icon::Warning, ui::col::kRed, result.error.c_str(), ui::col::kRed);
    ImGui::EndTooltip();
}

void App::handleTrackTool(std::vector<gfx::OverlayQuad>& overlays) {
    World& w = world();
    const CompanyId company = session_->localCompany();
    constexpr u32 kEnd = gfx::rgba(255, 210, 70, 200);
    constexpr u32 kSnap = gfx::rgba(120, 240, 140, 230);
    constexpr u32 kBranch = gfx::rgba(110, 215, 255, 230);

    if (preferences_.signalBlocks) drawSignalBlocks(overlays);
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
    // Away from the joints, anywhere along a piece of track: a switch goes in there.
    const TrackSplit along = snap.valid() ? TrackSplit{} : splitNear(w, cursor, kSnapRadius);
    if (snap.valid()) {
        const TrackNode& n = w.trackNodes[snap];
        addRing(overlays, toWorld(n.pos, n.h, 0.09), 0.45, kSnap);
    } else if (along.ok) {
        addRing(overlays, toWorld(along.pos, along.h, 0.09), 0.45, kBranch);
    }

    if (!trackStart_) {
        const char* contours = preferences_.trackContours ? "Contour lines join ground of equal height: track that follows one stays "
                                                            "level.\nThe gold line runs at the pointer's height. H: hide the elevation view"
                                                          : "H: show the elevation view (height colours and contour lines)";
        const char* what = snap.valid() ? "Click to continue this track"
                           : along.ok    ? "Click to branch off this track here (a switch is put in)"
                                         : "Click to start a track";
        ImGui::SetTooltip("%s\n%s", what, contours);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            TrackAnchor a;
            if (snap.valid()) {
                a.node = snap;
                a.pos = w.trackNodes[snap].pos;
            } else if (along.ok) {
                a.edge = along.edge;
                a.pos = along.pos;
            } else {
                a.pos = cursor;
            }
            trackStart_ = a;
        }
        return;
    }

    // The start: a node (or the switch to be put in part way along track) chooses its side
    // towards the pointer.
    TrackAnchor from = *trackStart_;
    if (from.node.valid()) {
        if (const TrackNode* n = w.trackNodes.tryGet(from.node)) from.into = sideTowards(*n, cursor);
        else from.node = {};
    } else if (from.edge.valid()) {
        // Pieces are renumbered when track is split, so find the one there now.
        const TrackSplit start = splitNear(w, from.pos, 16);
        from.edge = start.ok ? start.edge : TrackEdgeId{};
        if (start.ok) from.into = sideTowards(switchAt(start), cursor);
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
    } else if (along.ok) {
        to.edge = along.edge;
        to.pos = along.pos;
        to.into = sideTowards(switchAt(along), from.pos);
        joined = true;
    }
    if (!joined) to.pos = cursor;

    const TrackPlan plan = track::plan(w, company, from, to);
    if (plan.ok) addEarthworks(overlays, w.map, plan.terrain);
    if (!plan.samples.empty()) {
        addPlanRibbon(overlays, plan, 0.34);
    } else {
        // Nothing planned (too short, explored, ...): a thin line shows where the track would go.
        addRibbon(overlays, {TrackSample{from.pos, track::terrainAt(w, from.pos), 0}, TrackSample{to.pos, track::terrainAt(w, to.pos), 1}},
                  0.08, gfx::rgba(250, 90, 80, 130));
    }
    planTooltip(w, plan, uiScale_);

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
    // A fixed length runs that far from the start towards the pointer.
    TPoint to = cursor;
    if (stationFixedLength_ > 0) {
        TDir dir = normalised(static_cast<i64>(cursor.x) - stationStart_->pos.x, static_cast<i64>(cursor.z) - stationStart_->pos.z);
        if (const TrackNode* n = stationStart_->node.valid() ? w.trackNodes.tryGet(stationStart_->node) : nullptr)
            dir = n->side(n->count(0) == 0 ? 0 : 1);
        if (!dir.isZero()) to = offset(stationStart_->pos, dir, stationFixedLength_ * kTrackUnits);
    }
    const Station* editing = w.stations.tryGet(stationEditTarget_);
    const StationUse use = editing ? editing->use : stationUse_;
    const int platforms = editing ? 1 : stationPlatforms_;
    Command command;
    command.company = company;
    command.payload = CmdBuildTrackStation{*stationStart_, to, stationEditTarget_, use, static_cast<u8>(platforms)};
    const CommandResult result = testCommand(w, command);
    const TrackPlan plan = track::planPlatform(w, company, *stationStart_, to);
    std::vector<TrackPlan> plans{plan};
    for (int k = 1; k < platforms && plan.ok; ++k) plans.push_back(track::planParallelPlatform(w, company, plan, k));
    // The ground levelled for it, then the station itself: platforms either side of each track.
    const bool ok = result.ok && plan.ok;
    for (const TrackPlan& p : plans) {
        if (p.ok) addEarthworks(overlays, w.map, p.terrain);
        if (p.samples.empty()) continue;
        addRibbon(overlays, p.samples, 0.95, ok ? gfx::rgba(120, 200, 255, 120) : gfx::rgba(250, 90, 80, 130));
        addRibbon(overlays, p.samples, 0.33, ok ? gfx::rgba(70, 90, 110, 150) : gfx::rgba(150, 50, 45, 150), 0.08);
        addRibbon(overlays, p.samples, 0.08, ok ? gfx::rgba(255, 255, 255, 200) : gfx::rgba(250, 90, 80, 200), 0.09);
    }
    addRing(overlays, toWorld(stationStart_->pos, plan.samples.empty() ? 0 : plan.samples.front().h, 0.09), 0.32,
            gfx::rgba(120, 240, 140, 230));
    const i32 tiles = (plan.length + kTrackUnits / 2) / kTrackUnits;
    if (plan.samples.size() >= 2) {
        // A mark at every tile along the first platform, and its length beside the far end.
        const TrackSample& a = plan.samples.front();
        const TrackSample& b = plan.samples.back();
        const glm::dvec2 along = glm::normalize(glm::dvec2(b.p.x - a.p.x, b.p.z - a.p.z));
        const glm::dvec2 across(-along.y, along.x);
        for (i32 t = 0; t <= tiles; ++t) {
            const glm::dvec3 c = toWorld(a.p, a.h, 0.11) + glm::dvec3(along.x * t, 0.0, along.y * t);
            const double half = t % 4 == 0 ? 1.05 : 0.8, thick = t % 4 == 0 ? 0.05 : 0.03;
            overlays.push_back({{c + glm::dvec3(across.x * half - along.x * thick, 0.0, across.y * half - along.y * thick),
                                 c + glm::dvec3(across.x * half + along.x * thick, 0.0, across.y * half + along.y * thick),
                                 c + glm::dvec3(-across.x * half + along.x * thick, 0.0, -across.y * half + along.y * thick),
                                 c + glm::dvec3(-across.x * half - along.x * thick, 0.0, -across.y * half - along.y * thick)},
                                gfx::rgba(255, 255, 255, 220)});
        }
        if (const auto screen = worldToScreen(toWorld(b.p, b.h, 0.6) + glm::dvec3(along.x * 0.8, 0.0, along.y * 0.8))) {
            ui::FontScope f(ui::fonts().body);
            const std::string label = std::format("{} tiles", tiles);
            const ImVec2 size = ImGui::CalcTextSize(label.c_str());
            const ImVec2 pos{screen->x - size.x / 2, screen->y - size.y};
            auto* draw = ImGui::GetBackgroundDrawList();
            draw->AddRectFilled(ImVec2(pos.x - 7, pos.y - 4), ImVec2(pos.x + size.x + 7, pos.y + size.y + 4), IM_COL32(15, 25, 35, 225), 6);
            draw->AddText(pos, ok ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 130, 110, 255), label.c_str());
        }
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * uiScale_, 7.0f * uiScale_));
    if (ImGui::BeginTooltip()) {
        if (ok) {
            const char* kind = use == StationUse::Freight ? "Freight station" : use == StationUse::Passengers ? "Passenger station" : "Station";
            const std::string what = editing ? std::format("New platform for {}, {} tiles", editing->name, tiles)
                                   : platforms > 1 ? std::format("{}: {} platforms of {} tiles", kind, platforms, tiles)
                                                   : std::format("{}: a platform of {} tiles", kind, tiles);
            ui::iconText(ui::Icon::Station, ui::col::kBlue, what.c_str());
            ui::iconText(ui::Icon::Coins, ui::col::kAccent, std::format("Cost {}", formatMoney(result.cost)).c_str());
            ui::FontScope f(ui::fonts().small);
            ui::mutedText(std::format("Takes trains up to {} tiles long", tiles).c_str());
            Money earthworks = 0;
            for (const TrackPlan& p : plans) earthworks += p.earthworks;
            if (earthworks > 0)
                ui::mutedText(std::format("including levelling the ground: {}", formatMoney(earthworks)).c_str());
            else
                ui::mutedText("The ground here is already level");
            ui::mutedText(stationFixedLength_ > 0 ? "Fixed length: point the way the platform should run. Click to build"
                                                   : "Click to build, right-click to cancel");
        } else {
            ui::iconText(ui::Icon::Warning, ui::col::kRed, plan.ok ? result.error.c_str() : plan.error.c_str(), ui::col::kRed);
        }
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ok) {
        session_->submit(command.payload);
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
    planTooltip(w, plan, uiScale_);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && plan.ok) session_->submit(CmdBuildTrackDepot{snap});
}

const App::SignalBlocks& App::currentSignalBlocks() {
    const World& w = world();
    if (signalBlocks_.world == &w && signalBlocks_.revision == w.trackRevision) return signalBlocks_;
    signalBlocks_.world = &w;
    signalBlocks_.revision = w.trackRevision;
    signalBlocks_.block = track::signalBlocks(w);
    // Blocks meeting at a signal get different colours, so the boundary shows.
    std::map<u32, std::set<u32>> next;
    for (const auto [nid, n] : w.trackNodes) {
        if (!n.signals || !n.edges[0].valid() || !n.edges[2].valid()) continue;
        const u32 a = signalBlocks_.block.at(n.edges[0].value), b = signalBlocks_.block.at(n.edges[2].value);
        next[a].insert(b);
        next[b].insert(a);
    }
    std::set<u32> blocks;
    for (const auto& [edge, block] : signalBlocks_.block) blocks.insert(block);
    signalBlocks_.colour.clear();
    for (const u32 block : blocks) {
        u32 used = 0;
        for (const u32 other : next[block])
            if (const auto it = signalBlocks_.colour.find(other); it != signalBlocks_.colour.end()) used |= 1u << it->second;
        u8 pick = static_cast<u8>(mix32(block) % kBlockColours);
        for (u8 k = 0; k < kBlockColours && (used >> pick & 1u); ++k) pick = static_cast<u8>((pick + 1) % kBlockColours);
        signalBlocks_.colour[block] = pick;
    }
    return signalBlocks_;
}

void App::drawSignalBlocks(std::vector<gfx::OverlayQuad>& overlays, TrackNodeId focus) {
    const World& w = world();
    const SignalBlocks& blocks = currentSignalBlocks();
    std::unordered_set<u32> held;
    for (const auto [id, e] : w.trackEdges)
        if (e.reservedBy.valid()) held.insert(blocks.block.at(id.value));
    // The blocks either side of a highlighted signal are left to its highlight.
    std::unordered_set<u32> skip;
    if (focus.valid())
        for (const auto& side : signalView(focus).sides)
            if (side.valid) skip.insert(side.block);
    for (const auto [id, e] : w.trackEdges) {
        if (!nearCamera(e)) continue;
        const u32 block = blocks.block.at(id.value);
        if (skip.contains(block)) continue;
        const u32 colour = kBlockPalette[blocks.colour.at(block)];
        // Blocks a train holds part of are drawn stronger.
        addRibbon(overlays, e.samples, e.kind == TrackKind::Platform ? 0.22 : 0.3,
                  (colour & 0x00ffffffu) | (held.contains(block) ? 0xb4000000u : 0x78000000u), 0.075);
    }
    // Which way each signal faces.
    for (const auto [nid, n] : w.trackNodes) {
        if (!n.signals) continue;
        const double dx = n.pos.x / double(kTrackUnits) - camera_.target.x, dz = n.pos.z / double(kTrackUnits) - camera_.target.z;
        if (dx * dx + dz * dz > 90.0 * 90.0) continue;
        for (u8 side = 0; side < 2; ++side)
            if (n.signals & (1u << side)) addArrow(overlays, n.pos, n.h, n.side(side), 0.9, gfx::rgba(255, 255, 255, 210));
    }
}

bool App::nearCamera(const TrackEdge& e) const {
    if (e.samples.empty()) return false;
    for (const TrackSample* sm : {&e.samples.front(), &e.samples.back()}) {
        const double dx = sm->p.x / double(kTrackUnits) - camera_.target.x, dz = sm->p.z / double(kTrackUnits) - camera_.target.z;
        if (dx * dx + dz * dz < 90.0 * 90.0) return true;
    }
    return false;
}

App::SignalView App::signalView(TrackNodeId node) {
    const World& w = world();
    SignalView view;
    const TrackNode* n = w.trackNodes.tryGet(node);
    if (!n) return view;
    const SignalBlocks& blocks = currentSignalBlocks();
    for (u8 side = 0; side < 2; ++side) {
        const TrackEdgeId first = n->edges[side * 2u];
        if (!first.valid()) continue;
        SignalView::Side& out = view.sides[side];
        out.block = blocks.block.at(first.value);
        out.valid = true;
        for (const auto [id, e] : w.trackEdges) {
            if (blocks.block.at(id.value) != out.block) continue;
            out.length += e.length;
            if (e.reservedBy.valid() && !out.heldBy.valid()) out.heldBy = e.reservedBy;
        }
    }
    return view;
}

void App::highlightSignal(std::vector<gfx::OverlayQuad>& overlays, TrackNodeId node) {
    const World& w = world();
    const TrackNode* n = w.trackNodes.tryGet(node);
    if (!n || !n->signals) return;
    const SignalView view = signalView(node);
    const SignalBlocks& blocks = currentSignalBlocks();
    const bool oneWay = (n->signals & 3) != 3;
    for (u8 side = 0; side < 2; ++side) {
        if (!view.sides[side].valid) continue;
        // The block a signal guards the way into (green; amber for the other way through a
        // two-way signal), and for a one-way signal the block trains reach it from (blue).
        const bool guarded = n->signals & (1u << side);
        const u32 colour = !guarded ? kApproachColour : side == 0 || oneWay ? kGuardedColour : kOtherWayColour;
        for (const auto [id, e] : w.trackEdges)
            if (blocks.block.at(id.value) == view.sides[side].block && nearCamera(e))
                addRibbon(overlays, e.samples, e.kind == TrackKind::Platform ? 0.24 : 0.32, (colour & 0x00ffffffu) | 0x8c000000u, 0.08);
        if (guarded) addArrow(overlays, n->pos, n->h, n->side(side), 1.8, colour, 0.11);
        else addArrow(overlays, n->pos, n->h, n->side(side), 1.1, gfx::rgba(250, 90, 80, 240), 0.11, true);
    }
    addRing(overlays, toWorld(n->pos, n->h, 0.1), 0.5, gfx::rgba(255, 255, 255, 230));
}

std::string App::signalSummary(TrackNodeId node) {
    const World& w = world();
    const TrackNode& n = w.trackNodes[node];
    if ((n.signals & 3) == 3)
        return "Two-way signal: trains going either way wait here until their way through the block ahead is clear "
               "(green and amber: the two blocks it divides).";
    return "One-way signal: trains heading along the green arrow wait here until their way through the green block "
           "is clear. Trains can't pass it the other way (red arrow).";
}

void App::handleSignalTool(std::vector<gfx::OverlayQuad>& overlays) {
    World& w = world();
    const bool pointing = hover_.hit && !mouseOverUi_;
    const TPoint cursor = toTrack(hover_.point);
    TrackNodeId node = pointing ? track::nodeNear(w, cursor, kSnapRadius, true) : TrackNodeId{};
    if (node.valid() && !w.trackNodes[node].signals && (w.trackNodes[node].count(0) == 0 || w.trackNodes[node].count(1) == 0))
        node = {}; // an open end
    // The signal pointed at, or else the one open in the signal panel, shows what it does.
    const bool selected = selSignal_.valid() && w.trackNodes.contains(selSignal_) && w.trackNodes[selSignal_].signals;
    const TrackNodeId focus = node.valid() && w.trackNodes[node].signals ? node : selected ? selSignal_ : TrackNodeId{};
    if (preferences_.signalBlocks) drawSignalBlocks(overlays, focus);
    if (focus.valid()) highlightSignal(overlays, focus);
    if (!pointing) return;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * uiScale_, 7.0f * uiScale_));

    // An existing signal: what it does, and a click opens it to change or remove.
    if (node.valid() && w.trackNodes[node].signals) {
        if (ImGui::BeginTooltip()) {
            const bool twoWay = (w.trackNodes[node].signals & 3) == 3;
            ui::iconText(ui::Icon::Signal, ui::col::kGreen,
                         std::format("{} signal - click to change or remove it", twoWay ? "Two-way" : "One-way").c_str());
            ui::FontScope small(ui::fonts().small);
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
            ui::mutedText(signalSummary(node).c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        ImGui::PopStyleVar();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) selSignal_ = node;
        return;
    }

    // Somewhere new: at a joint, or anywhere along a piece of track.
    const TrackSplit along = node.valid() ? TrackSplit{} : splitNear(w, cursor, kSnapRadius);
    if (!node.valid() && !along.ok) {
        const bool onTrack = track::edgeNear(w, cursor, kSnapRadius).valid();
        ImGui::SetTooltip("%s", onTrack && !along.error.empty() ? along.error.c_str()
                                                                : "Point at track to put a signal there, or at a signal to change it");
        ImGui::PopStyleVar();
        return;
    }
    const TrackNode at = node.valid() ? w.trackNodes[node] : switchAt(along);
    // A one-way signal is for trains passing it on their right: the side of the track pointed at.
    const i64 rx = static_cast<i64>(cursor.x) - at.pos.x, rz = static_cast<i64>(cursor.z) - at.pos.z;
    const bool rightOfSide0 = rz * at.dir.x - rx * at.dir.z > 0;
    const u8 value = newSignalOneWay_ ? (rightOfSide0 ? 1 : 2) : 3;
    Command c;
    c.company = session_->localCompany();
    c.payload = CmdTrackSignal{node, along.edge, along.pos, value};
    const CommandResult r = testCommand(w, c);
    const u32 tint = r.ok ? gfx::rgba(92, 240, 140, 240) : gfx::rgba(250, 90, 80, 240);
    addRing(overlays, toWorld(at.pos, at.h, 0.09), 0.45, tint);
    for (u8 side = 0; side < 2; ++side)
        if (value & (1u << side)) addArrow(overlays, at.pos, at.h, at.side(side), 1.1, tint, 0.11);
    if (ImGui::BeginTooltip()) {
        if (r.ok) {
            ui::iconText(ui::Icon::Signal, ui::col::kGreen,
                         std::format("Click to put a {} signal here ({})", newSignalOneWay_ ? "one-way" : "two-way", formatMoney(r.cost))
                             .c_str());
            ui::FontScope small(ui::fonts().small);
            ui::mutedText(newSignalOneWay_ ? "For trains heading along the arrow: point at the other side of the track to turn it round"
                                           : "Trains going either way wait here until the block ahead is clear");
        } else {
            ui::iconText(ui::Icon::Warning, ui::col::kRed, r.error.c_str(), ui::col::kRed);
        }
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && r.ok) session_->submit(c.payload);
}

void App::previewSignalling(TPoint lineStart) {
    auto* local = dynamic_cast<LocalSession*>(session_.get());
    if (!local) return;
    World& w = local->world();
    const auto point = [&](double dx, double dz) {
        return TPoint{lineStart.x + static_cast<i32>(dx * kTrackUnits), lineStart.z + static_cast<i32>(dz * kTrackUnits)};
    };
    const auto run = [&](CommandPayload p) {
        Command c;
        c.company = local->localCompany();
        c.payload = std::move(p);
        return executeCommand(w, c);
    };
    // The demo line runs east from its start, then bends south-east through the hills.
    const auto signalAt = [&](TPoint p, i32 value) {
        const TrackSplit at = splitNear(w, p, 3 * kTrackUnits);
        if (!at.ok) return TrackNodeId{};
        const CommandResult r = run(CmdTrackSignal{{}, at.edge, at.pos, value});
        if (!r.ok) log::warn("Signal preview: {}", r.error);
        return r.ok ? TrackNodeId(r.createdId) : TrackNodeId{};
    };
    const TrackSplit fork = splitNear(w, point(12, 0), 2 * kTrackUnits);
    TrackAnchor from;
    from.edge = fork.edge;
    from.pos = fork.pos;
    from.into = TDir{kDirOne, 0};
    tool_ = previewBranch_ ? Tool::RailTrack : Tool::Signal;
    toolGroup_ = ToolGroup::Rail;
    if (previewBranch_) {
        // Part way through laying a siding off the straight beyond the first station.
        trackStart_ = from;
        previewHover_ = glm::dvec3(lineStart.x / double(kTrackUnits) + 25.0, 0.0, lineStart.z / double(kTrackUnits) - 7.0);
        camera_.targetGoal = glm::dvec3(lineStart.x / double(kTrackUnits) + 17.0, 0.0, lineStart.z / double(kTrackUnits) - 3.0);
        camera_.distanceGoal = 26.0;
    } else {
        TrackAnchor siding;
        siding.pos = point(25, -7);
        if (const CommandResult r = run(CmdBuildTrack{from, siding}); !r.ok)
            log::warn("Signal preview siding: {}", r.error);
        selSignal_ = signalAt(point(17, 0), 3);
        signalAt(point(38, 9.5), 1);
        signalAt(point(60, 13), 3);
        preferences_.signalBlocks = true;
        newSignalOneWay_ = true;
        const TrackSplit hover = splitNear(w, point(27, 2), 3 * kTrackUnits);
        const TPoint p = hover.ok ? hover.pos : point(27, 2);
        previewHover_ = glm::dvec3(p.x / double(kTrackUnits) + 0.25, 0.0, p.z / double(kTrackUnits) + 0.25);
        camera_.targetGoal = glm::dvec3(lineStart.x / double(kTrackUnits) + 20.0, 0.0, lineStart.z / double(kTrackUnits) - 1.0);
        camera_.distanceGoal = 30.0;
    }
    camera_.targetGoal.y = gfx::terrainHeightAt(w.map, camera_.targetGoal.x, camera_.targetGoal.z);
    camera_.snapToGoals();
    renderer_.invalidateAll();
}

void App::drawSignalWindow() {
    if (!selSignal_.valid()) return;
    World& w = world();
    const TrackNode* n = w.trackNodes.tryGet(selSignal_);
    if (tool_ != Tool::Signal || !n || !n->signals) {
        selSignal_ = {};
        return;
    }
    const float s = uiScale_;
    const TrackNodeId node = selSignal_;
    const bool twoWay = (n->signals & 3) == 3;
    const u8 signals = n->signals;
    ui::PanelSpec spec;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    spec.pos = ImVec2(vp->WorkPos.x + vp->WorkSize.x - 400.0f * s, vp->WorkPos.y + 80.0f * s);
    spec.width = 380.0f;
    spec.accent = ui::col::kRail;
    spec.subtitle = twoWay ? "Two-way" : "One-way";
    bool open = true;
    if (ui::beginPanel("###signalPanel", "Signal", ui::Icon::Signal, &open, spec)) {
        {
            ui::FontScope small(ui::fonts().small);
            ImGui::PushTextWrapPos(0.0f);
            ui::mutedText(signalSummary(node).c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::Dummy(ImVec2(0, 4.0f * s));
        const SignalView view = signalView(node);
        for (u8 side = 0; side < 2; ++side) {
            if (!(signals & (1u << side)) || !view.sides[side].valid) continue;
            const auto& block = view.sides[side];
            const std::string held = block.heldBy.valid() && w.vehicles.contains(block.heldBy)
                                         ? std::format("held by {}", vehicleName(w.vehicles[block.heldBy]))
                                         : std::string("clear");
            const char* label = twoWay ? (side == 0 ? "Block ahead (green)" : "Block ahead (amber)") : "Block ahead";
            ui::statRow(label, std::format("{:.1f} tiles, {}", block.length / double(kTrackUnits), held).c_str(),
                        block.heldBy.valid() ? ui::col::kRed : ui::col::kGreen);
        }
        ImGui::Dummy(ImVec2(0, 4.0f * s));
        const auto attempt = [&](u8 value) {
            Command c;
            c.company = session_->localCompany();
            c.payload = CmdTrackSignal{node, {}, {}, value};
            return std::pair{testCommand(w, c), c.payload};
        };
        ui::fieldLabel("Kind");
        static constexpr ui::Segment kKinds[] = {
            {"Two-way", ui::Icon::None, "Trains going either way wait here until the block ahead is clear"},
            {"One-way", ui::Icon::None, "Only trains heading one way may pass, once the block ahead is clear"}};
        if (const int clicked = ui::segmented("##signalKind", kKinds, twoWay ? 0 : 1); clicked >= 0 && (clicked == 0) != twoWay)
            session_->submit(CmdTrackSignal{node, {}, {}, clicked == 0 ? 3 : 1});
        ImGui::Dummy(ImVec2(0, 4.0f * s));
        if (!twoWay) {
            const auto [flip, payload] = attempt(static_cast<u8>(signals ^ 3));
            if (ui::button("Turn round") && flip.ok) session_->submit(payload);
            ui::itemTooltip({"Turn round", "Make it a one-way signal for trains going the other way"});
            ImGui::SameLine();
        }
        const auto [removal, removePayload] = attempt(0);
        if (ui::button("Remove", ui::Icon::Trash, ui::Btn::Danger) && removal.ok) {
            session_->submit(removePayload);
            open = false;
        }
        if (!removal.ok) ui::iconText(ui::Icon::Warning, ui::col::kRed, removal.error.c_str(), ui::col::kRed);
    }
    ui::endPanel();
    if (!open) selSignal_ = {};
}

// Order picking for trains on free-form track: a platform (that platform in particular), a
// depot, or with "via" a waypoint on the track. False when the pointer is not on such track.
bool App::pickTrackOrder(std::vector<gfx::OverlayQuad>& overlays) {
    World& w = world();
    std::vector<Order> orders;
    VehicleType type = VehicleType::Road;
    if (!pickTarget(orders, type) || type != VehicleType::Rail || !hover_.hit || mouseOverUi_) return false;
    const TPoint cursor = toTrack(hover_.point);
    const TrackEdgeId id = track::edgeNear(w, cursor, kPlatformHalfWidth + 16);
    if (!id.valid()) return false;
    const TrackEdge& e = w.trackEdges[id];
    Order o;
    std::string label;
    std::vector<TrackEdgeId> highlight{id};
    double halfWidth = 0.34;
    if (pickViaOrder_) {
        if (e.kind == TrackKind::Depot) return false;
        // The tile under the nearest point of the track.
        const TrackSample* best = &e.samples.front();
        for (const TrackSample& sm : e.samples)
            if (std::hypot(sm.p.x - cursor.x, sm.p.z - cursor.z) < std::hypot(best->p.x - cursor.x, best->p.z - cursor.z)) best = &sm;
        o.kind = OrderKind::Via;
        o.waypoint = track::edgeCovers(w, id, hover_.tile) ? hover_.tile : tileAt(best->p);
        label = "Via this track (a waypoint)";
    } else if (e.kind == TrackKind::Platform && w.stations.contains(e.station)) {
        highlight = track::platformChain(w, id);
        halfWidth = 0.93;
        o.station = e.station;
        o.flags = kOrderPlatform;
        o.platform = track::platformPoint(w, highlight);
        label = std::format("Stop at {}, platform {}", w.stations[e.station].name, track::platformNumber(w, e.station, o.platform));
    } else if (e.kind == TrackKind::Depot && w.depots.contains(e.depot)) {
        o.kind = OrderKind::Depot;
        o.depot = e.depot;
        label = "Go for service at this depot";
    } else {
        return false;
    }
    orders.push_back(o);
    const CommandResult r = testPickedOrders(orders);
    for (const TrackEdgeId h : highlight)
        addRibbon(overlays, w.trackEdges[h].samples, halfWidth, r.ok ? gfx::rgba(120, 240, 140, 130) : gfx::rgba(250, 90, 80, 130));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * uiScale_, 7.0f * uiScale_));
    if (ImGui::BeginTooltip()) {
        if (r.ok) {
            ui::iconText(o.kind == OrderKind::Depot ? ui::Icon::Depot : o.kind == OrderKind::Via ? ui::Icon::Target : ui::Icon::Station,
                         ui::col::kGreen, label.c_str());
            ui::FontScope f(ui::fonts().small);
            ui::mutedText(o.kind == OrderKind::Station ? "Click to add it to the orders (or change the platform there later)"
                                                       : "Click to add it to the orders");
        } else {
            ui::iconText(ui::Icon::Warning, ui::col::kRed, r.error.c_str(), ui::col::kRed);
        }
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
    if (r.ok && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) commitPickedOrders(orders);
    return true;
}

void App::handleElectrifyTool(std::vector<gfx::OverlayQuad>& overlays) {
    World& w = world();
    const bool remove = tool_ == Tool::RemoveWires;
    // The wires already up nearby, so gaps are easy to spot.
    for (const auto [id, e] : w.trackEdges) {
        if (!e.electrified || e.samples.empty()) continue;
        const TrackSample& mid = e.samples[e.samples.size() / 2];
        const double dx = mid.p.x / double(kTrackUnits) - camera_.target.x, dz = mid.p.z / double(kTrackUnits) - camera_.target.z;
        if (dx * dx + dz * dz > 150.0 * 150.0) continue;
        addRibbon(overlays, e.samples, 0.1, gfx::rgba(255, 214, 90, 120));
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) wiredThisDrag_.clear();
    if (!hover_.hit || mouseOverUi_) return;
    const TrackEdgeId e = track::edgeNear(w, toTrack(hover_.point), 140);
    if (!e.valid()) {
        ImGui::SetTooltip(remove ? "Point at electrified track to take its wires down" : "Point at track to electrify it");
        return;
    }
    const bool connected = ImGui::GetIO().KeyShift;
    // Highlight what the click would change: the edge, or all the company's track joined to it.
    std::vector<TrackEdgeId> edges{e};
    if (connected) {
        std::unordered_set<u32> seen{e.value};
        for (usize i = 0; i < edges.size() && edges.size() < 4000; ++i) {
            const TrackEdge& edge = w.trackEdges[edges[i]];
            for (const TrackNodeId nid : {edge.a, edge.b})
                if (const TrackNode* n = w.trackNodes.tryGet(nid))
                    for (const TrackEdgeId next : n->edges)
                        if (next.valid() && w.trackEdges.contains(next) && w.trackEdges[next].owner == edge.owner &&
                            seen.insert(next.value).second)
                            edges.push_back(next);
        }
    }
    const u32 colour = remove ? gfx::rgba(250, 90, 80, 150) : gfx::rgba(120, 220, 255, 160);
    for (const TrackEdgeId id : edges) addRibbon(overlays, w.trackEdges[id].samples, 0.3, colour);
    Command c;
    c.company = session_->localCompany();
    c.payload = CmdElectrifyTrack{e, connected, remove};
    const CommandResult r = testCommand(w, c);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * uiScale_, 7.0f * uiScale_));
    if (ImGui::BeginTooltip()) {
        if (r.ok)
            ui::iconText(ui::Icon::Spark, remove ? ui::col::kRed : ui::col::kAccent,
                         std::format("{} ({})", remove ? "Take down the wires" : connected ? "Electrify the whole line" : "Electrify",
                                     formatMoney(r.cost))
                             .c_str());
        else
            ui::iconText(ui::Icon::Warning, ui::col::kRed, r.error.c_str(), ui::col::kRed);
        if (!connected) {
            ui::FontScope f(ui::fonts().small);
            ui::mutedText("Shift: all the track joined to it");
        }
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
    // Dragging electrifies each piece of track the cursor passes over, once.
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && r.ok && std::ranges::find(wiredThisDrag_, e.value) == wiredThisDrag_.end()) {
        wiredThisDrag_.push_back(e.value);
        session_->submit(CmdElectrifyTrack{e, connected, remove});
    }
}

} // namespace tp
