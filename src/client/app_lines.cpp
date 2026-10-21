// Lines: vehicles that share a route are grouped into numbered, coloured lines. The lines window
// lists each line's stops, vehicles, load and profit with a diagram of where its vehicles are, and
// the lines map layer draws every line along the track, road or water it runs on.

#include "client/app.hpp"
#include "client/map_draw.hpp"
#include "client/ui/widgets.hpp"

#include <imgui.h>

#include "render/terrain_mesher.hpp"
#include "render/vehicle_pose.hpp"
#include "sim/road_path.hpp"
#include "sim/track.hpp"

#include <algorithm>
#include <format>
#include <functional>
#include <map>
#include <queue>
#include <string>
#include <tuple>
#include <unordered_map>

namespace tp {

// Routes between consecutive stops, found on the network and kept for a few seconds.
struct LineCache {
    struct Route {
        std::vector<glm::dvec3> points; // empty: no route found (drawn straight)
        double built = -1.0;
    };
    std::map<std::tuple<u32, u32, u8>, Route> routes;
};

namespace {

constexpr ImU32 kLinePalette[12] = {
    IM_COL32(231, 76, 60, 255),  IM_COL32(52, 152, 219, 255),  IM_COL32(46, 204, 113, 255), IM_COL32(241, 196, 15, 255),
    IM_COL32(155, 89, 182, 255), IM_COL32(230, 126, 34, 255),  IM_COL32(26, 188, 156, 255), IM_COL32(236, 64, 122, 255),
    IM_COL32(141, 110, 99, 255), IM_COL32(0, 150, 136, 255),   IM_COL32(255, 112, 67, 255), IM_COL32(121, 134, 203, 255),
};

struct Line {
    u32 key = 0;
    int number = 0;
    VehicleType type = VehicleType::Road;
    CompanyId owner;
    std::vector<StationId> stops;
    std::vector<VehicleId> vehicles;
    u32 capacity = 0, aboard = 0;
    i64 passengersThisYear = 0;
    Money profitThisYear = 0;
    u16 firstUnit = 0xFFFF;
    ImU32 colour = 0;
    std::string name;
};

std::vector<StationId> stopsOf(const Vehicle& v) {
    std::vector<StationId> out;
    for (const Order& o : v.orders)
        if (o.kind == OrderKind::Station && !o.passThrough()) out.push_back(o.station);
    return out;
}

// Vehicles sharing an order list are one line; so are vehicles of a company and type calling at the
// same stops in the same order.
u32 lineKey(const Vehicle& v) {
    if (v.orderGroup) return (v.orderGroup * 2654435761u) | 1u;
    u32 h = 2166136261u ^ (static_cast<u32>(v.info().type) << 4) ^ (static_cast<u32>(v.owner.value) << 12);
    for (const StationId s : stopsOf(v)) h = (h ^ s.value) * 16777619u;
    return h & ~1u;
}

ui::Icon typeIcon(VehicleType t) {
    switch (t) {
    case VehicleType::Road: return ui::Icon::Bus;
    case VehicleType::Tram: return ui::Icon::Tram;
    case VehicleType::Rail: return ui::Icon::Train;
    case VehicleType::Ship: return ui::Icon::Ship;
    }
    return ui::Icon::Bus;
}

glm::dvec2 stationXZ(const Station& s) { return {s.sign.x + 0.5, s.sign.y + 0.5}; }

std::vector<Line> collectLines(const World& w) {
    std::unordered_map<u32, usize> index;
    std::vector<Line> lines;
    for (const auto [id, v] : w.vehicles) {
        std::vector<StationId> stops = stopsOf(v);
        if (stops.size() < 2) continue;
        const auto [it, fresh] = index.try_emplace(lineKey(v), lines.size());
        if (fresh) {
            Line l;
            l.key = it->first;
            l.type = v.info().type;
            l.owner = v.owner;
            l.stops = std::move(stops);
            lines.push_back(std::move(l));
        }
        Line& l = lines[it->second];
        l.vehicles.push_back(id);
        l.capacity += v.capacity();
        l.aboard += static_cast<u32>(v.passengers.size());
        l.passengersThisYear += v.passengersThisYear;
        l.profitThisYear += v.profitThisYear;
        l.firstUnit = std::min(l.firstUnit, v.unitNumber);
    }
    // Each company's lines are numbered in the order of their first vehicles.
    std::sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) {
        return std::tie(a.owner, a.type, a.firstUnit, a.key) < std::tie(b.owner, b.type, b.firstUnit, b.key);
    });
    std::map<u8, int> numbers;
    for (Line& l : lines) {
        l.number = ++numbers[l.owner.value];
        l.colour = kLinePalette[(l.number - 1) % 12];
        // Named after the first stop and the stop farthest from it.
        const Station* first = w.stations.tryGet(l.stops.front());
        const Station* far = nullptr;
        double best = -1.0;
        for (const StationId s : l.stops)
            if (const Station* st = w.stations.tryGet(s); st && first) {
                const double d = glm::length(stationXZ(*st) - stationXZ(*first));
                if (d > best) {
                    best = d;
                    far = st;
                }
            }
        l.name = first && far ? std::format("{} - {}", first->name, far->name) : std::string("(stops removed)");
    }
    return lines;
}

constexpr double kRouteLift = 0.07;

// The shortest way along the free-form track from a platform of `from` to a platform of `to`.
std::vector<glm::dvec3> railRoute(const World& w, StationId from, StationId to) {
    using Key = u64;
    const auto keyOf = [](EdgeRun r) { return (static_cast<u64>(r.edge.value) << 1) | (r.forward ? 1u : 0u); };
    struct Item {
        i64 cost;
        EdgeRun run;
        bool operator>(const Item& o) const { return cost > o.cost; }
    };
    std::priority_queue<Item, std::vector<Item>, std::greater<>> open;
    std::unordered_map<Key, std::pair<Key, i64>> came;
    for (const auto [eid, e] : w.trackEdges)
        if (e.kind == TrackKind::Platform && e.station == from)
            for (const bool forward : {true, false}) {
                const EdgeRun r{eid, forward};
                came[keyOf(r)] = {~0ull, 0};
                open.push({0, r});
            }
    for (int budget = 40000; !open.empty() && budget > 0; --budget) {
        const Item item = open.top();
        open.pop();
        const Key k = keyOf(item.run);
        if (came[k].second < item.cost) continue;
        const TrackEdge* e = w.trackEdges.tryGet(item.run.edge);
        if (!e) continue;
        if (e->kind == TrackKind::Platform && e->station == to) {
            std::vector<EdgeRun> runs;
            for (Key at = k; at != ~0ull; at = came[at].first)
                runs.push_back({TrackEdgeId(static_cast<u32>(at >> 1)), (at & 1) != 0});
            std::reverse(runs.begin(), runs.end());
            std::vector<glm::dvec3> points;
            for (const EdgeRun& r : runs) {
                const TrackEdge* edge = w.trackEdges.tryGet(r.edge);
                if (!edge || edge->samples.empty()) continue;
                const usize n = edge->samples.size();
                for (usize i = 0; i < n; ++i) {
                    const TrackSample& s = edge->samples[r.forward ? i : n - 1 - i];
                    points.push_back({s.p.x / static_cast<double>(kTrackUnits), s.h / static_cast<double>(kSubLevels) * 0.5 + kRouteLift,
                                      s.p.z / static_cast<double>(kTrackUnits)});
                }
            }
            return points;
        }
        for (const EdgeRun next : track::continuations(w, item.run)) {
            const TrackEdge* ne = w.trackEdges.tryGet(next.edge);
            if (!ne || ne->kind == TrackKind::Depot) continue;
            const i64 cost = item.cost + std::max(1, ne->length);
            const Key nk = keyOf(next);
            if (const auto f = came.find(nk); f != came.end() && f->second.second <= cost) continue;
            came[nk] = {k, cost};
            open.push({cost, next});
        }
    }
    return {};
}

// The shortest way along roads (or tram tracks) from one stop to another.
std::vector<glm::dvec3> roadRoute(const World& w, const Station& a, const Station& b, VehicleType type) {
    if (a.tiles.empty() || b.tiles.empty()) return {};
    std::optional<std::vector<u8>> best;
    const TilePos start = a.tiles.front();
    for (const Dir entry : kDirs)
        if (auto p = findRoadPath(w, start, entry, b.tiles, type, DepotId{}, 12000); p && (!best || p->size() < best->size()))
            best = std::move(p);
    if (!best) return {};
    std::vector<glm::dvec3> points;
    TilePos t = start;
    const auto at = [&](TilePos tile) {
        return glm::dvec3(tile.x + 0.5, gfx::roadSurfaceY(w, tile, 0.5f, 0.5f) + kRouteLift, tile.y + 0.5);
    };
    points.push_back(at(t));
    for (const u8 d : *best) {
        t = neighbour(t, static_cast<Dir>(d & 3));
        points.push_back(at(t));
    }
    return points;
}

} // namespace

void App::drawLineRoutes(std::vector<gfx::OverlayQuad>& overlays, bool all) {
    const World& w = world();
    if (!lineCache_) lineCache_ = std::make_shared<LineCache>();
    const std::vector<Line> lines = collectLines(w);
    const glm::dvec2 camera(camera_.target.x, camera_.target.z);
    int searches = 0; // new routes found per frame, so a big network fills in over a few frames
    for (const Line& l : lines) {
        const bool selected = l.key == selLine_ && showLines_;
        if (!all && !selected) continue;
        if (all && fleetCompany_ >= 0 && l.owner.value != fleetCompany_ && !selected) continue;
        const ImVec4 c = ImGui::ColorConvertU32ToFloat4(l.colour);
        const u32 colour = gfx::rgba(static_cast<u8>(c.x * 255), static_cast<u8>(c.y * 255), static_cast<u8>(c.z * 255),
                                     selected ? 235 : 190);
        // Wider when zoomed out, so the lines stay readable from high up.
        const double zoom = std::max(1.0, camera_.distance / 25.0);
        const double width = (selected ? 0.08 : 0.055) * zoom;
        const double offset = all ? ((l.number % 4) - 1.5) * 0.12 * zoom : 0.0;
        for (usize i = 0; i < l.stops.size(); ++i) {
            const Station* a = w.stations.tryGet(l.stops[i]);
            const Station* b = w.stations.tryGet(l.stops[(i + 1) % l.stops.size()]);
            if (!a || !b || a == b) continue;
            if (glm::length(stationXZ(*a) - camera) > 400.0 && glm::length(stationXZ(*b) - camera) > 400.0) continue;
            auto& route = lineCache_->routes[{l.stops[i].value, l.stops[(i + 1) % l.stops.size()].value, static_cast<u8>(l.type)}];
            if ((route.built < 0.0 || timeSeconds_ - route.built > 6.0) && searches < 4) {
                ++searches;
                route.built = timeSeconds_;
                route.points = l.type == VehicleType::Rail ? railRoute(w, l.stops[i], l.stops[(i + 1) % l.stops.size()])
                               : l.type == VehicleType::Ship ? std::vector<glm::dvec3>{}
                                                             : roadRoute(w, *a, *b, l.type);
            }
            if (route.points.size() >= 2) {
                mapdraw::addRibbon(overlays, route.points, width, colour, 0.0, offset);
            } else {
                const glm::dvec2 pa = stationXZ(*a), pb = stationXZ(*b);
                mapdraw::addRibbon(overlays, mapdraw::groundLeg(w, {pa.x, 0, pa.y}, {pb.x, 0, pb.y}, kRouteLift), width, colour,
                                   l.type == VehicleType::Ship ? 0.25 : 0.0, offset);
            }
        }
        for (const StationId sid : l.stops)
            if (const Station* st = w.stations.tryGet(sid)) {
                const glm::dvec2 p = stationXZ(*st);
                mapdraw::addRing(overlays, mapdraw::groundPoint(w, p.x, p.y, kRouteLift + 0.01), (selected ? 0.36 : 0.28) * zoom, colour);
            }
    }
}

void App::drawLinesWindow() {
    if (!showLines_) return;
    World& w = world();
    const float s = uiScale_;
    if (fleetCompany_ == -2) fleetCompany_ = session_->localCompany().valid() ? session_->localCompany().value : -1;
    std::vector<Line> lines = collectLines(w);
    std::erase_if(lines, [&](const Line& l) { return fleetCompany_ >= 0 && l.owner.value != fleetCompany_; });
    const std::string sub = std::format("{} line{}", lines.size(), lines.size() == 1 ? "" : "s");
    ui::PanelSpec spec;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    spec.pos = ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f - 300.0f * s, vp->WorkPos.y + 80.0f * s);
    spec.width = 600.0f;
    spec.accent = ui::col::kTeal;
    spec.subtitle = sub.c_str();
    if (ui::beginPanel("###linesPanel", "Lines", ui::Icon::Link, &showLines_, spec)) {
        if (lines.empty()) {
            ui::emptyState(ui::Icon::Link, "No lines yet");
            ui::FontScope f(ui::fonts().small);
            ui::mutedText("Give a vehicle orders with two or more stops; vehicles");
            ui::mutedText("sharing orders or calling at the same stops form a line.");
        } else {
            if (std::ranges::none_of(lines, [&](const Line& l) { return l.key == selLine_; })) selLine_ = lines.front().key;
            ImGui::BeginChild("##lineList", ImVec2(0, std::min(6.0f, static_cast<float>(lines.size())) * 30.0f * s + 8.0f * s),
                              ImGuiChildFlags_Borders);
            for (const Line& l : lines) {
                ImGui::PushID(static_cast<int>(l.key));
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float h = 28.0f * s;
                if (ImGui::Selectable("##line", l.key == selLine_, 0, ImVec2(0, h))) selLine_ = l.key;
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(ImVec2(p.x + 4.0f * s, p.y + 6.0f * s), ImVec2(p.x + 10.0f * s, p.y + h - 6.0f * s), l.colour, 3.0f * s);
                ui::drawIcon(dl, typeIcon(l.type), ImVec2(p.x + 24.0f * s, p.y + h * 0.5f), 16.0f * s, ui::col::kTextMuted);
                const std::string label = std::format("Line {}  {}", l.number, l.name);
                dl->AddText(ImVec2(p.x + 38.0f * s, p.y + h * 0.5f - ImGui::GetTextLineHeight() * 0.5f), ui::col::kText, label.c_str());
                const std::string right = std::format("{} vehicle{}  {}", l.vehicles.size(), l.vehicles.size() == 1 ? "" : "s",
                                                      formatMoney(l.profitThisYear));
                const float rw = ImGui::CalcTextSize(right.c_str()).x;
                dl->AddText(ImVec2(p.x + ImGui::GetContentRegionAvail().x - rw - 6.0f * s, p.y + h * 0.5f - ImGui::GetTextLineHeight() * 0.5f),
                            l.profitThisYear < 0 ? ui::col::kRed : ui::col::kTextMuted, right.c_str());
                ImGui::PopID();
            }
            ImGui::EndChild();

            const Line& l = *std::ranges::find_if(lines, [&](const Line& x) { return x.key == selLine_; });
            ui::sectionHeader(std::format("Line {}: {}", l.number, l.name).c_str());
            const float cw = ui::columnWidth(3);
            const std::string vehicles = std::format("{}", l.vehicles.size());
            const std::string carried = std::format("{}", l.passengersThisYear);
            const std::string profit = formatMoney(l.profitThisYear);
            ui::statCard(typeIcon(l.type), vehicles.c_str(), "Vehicles", l.colour, cw);
            ImGui::SameLine();
            ui::statCard(ui::Icon::People, carried.c_str(), "Passengers this year", ui::col::kTeal, cw);
            ImGui::SameLine();
            ui::statCard(ui::Icon::Coins, profit.c_str(), "Profit this year", l.profitThisYear < 0 ? ui::col::kRed : ui::col::kGreen, cw);
            ImGui::Dummy(ImVec2(0, 2.0f * s));
            const float load = l.capacity ? static_cast<float>(l.aboard) / static_cast<float>(l.capacity) : 0.0f;
            ui::meterRow("Aboard now", std::format("{} / {}", l.aboard, l.capacity).c_str(), load, ui::col::kTeal);

            // The line diagram: its stops in order, the way back drawn as the return to the first,
            // and each vehicle placed between the stop it left and the one it is heading for.
            ui::sectionHeader("Stops and vehicles");
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            const float y = origin.y + 26.0f * s;
            const usize n = l.stops.size();
            const float left = origin.x + 20.0f * s, right = origin.x + width - 20.0f * s;
            const auto stopX = [&](float i) { return left + (right - left) * i / static_cast<float>(n); };
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddLine(ImVec2(left, y), ImVec2(right, y), l.colour, 5.0f * s);
            for (usize i = 0; i <= n; ++i) {
                const float x = stopX(static_cast<float>(i));
                const Station* st = w.stations.tryGet(l.stops[i % n]);
                dl->AddCircleFilled(ImVec2(x, y), 7.0f * s, ui::col::kText);
                dl->AddCircle(ImVec2(x, y), 7.0f * s, l.colour, 0, 2.5f * s);
                if (!st) continue;
                ui::FontScope f(ui::fonts().small);
                const std::string name = i == n ? std::string("(back)") : st->name;
                const ImVec2 size = ImGui::CalcTextSize(name.c_str());
                const float ty = (i % 2) ? y + 12.0f * s : y - 12.0f * s - size.y;
                dl->AddText(ImVec2(std::clamp(x - size.x * 0.5f, origin.x, origin.x + width - size.x), ty), ui::col::kTextMuted, name.c_str());
            }
            for (const VehicleId vid : l.vehicles) {
                const Vehicle* v = w.vehicles.tryGet(vid);
                if (!v || v->orders.empty()) continue;
                // Index of the stop it is heading for among the line's stops.
                usize next = 0, seen = 0;
                for (usize o = 0; o < v->orders.size(); ++o) {
                    const Order& order = v->orders[o];
                    if (order.kind != OrderKind::Station || order.passThrough()) continue;
                    if (o >= v->orderIndex % v->orders.size()) {
                        next = seen;
                        break;
                    }
                    ++seen;
                    next = 0;
                }
                const Station* to = w.stations.tryGet(l.stops[next % n]);
                const Station* from = w.stations.tryGet(l.stops[(next + n - 1) % n]);
                float t = 1.0f;
                if (to && from && v->state != VehicleState::Loading) {
                    const glm::dvec2 at(v->tile.x + 0.5, v->tile.y + 0.5);
                    const double a = glm::length(at - stationXZ(*from)), b = glm::length(at - stationXZ(*to));
                    t = static_cast<float>(a / std::max(1e-6, a + b));
                }
                const float stop = next == 0 ? static_cast<float>(n) : static_cast<float>(next);
                const float x = stopX(stop - 1.0f + t);
                const ImU32 c = v->state == VehicleState::InDepot ? ui::col::kTextMuted : v->breakdownTicks > 0 ? ui::col::kRed : ui::col::kAccent;
                dl->AddCircleFilled(ImVec2(x, y), 10.0f * s, ui::col::kBar);
                ui::drawIcon(dl, typeIcon(l.type), ImVec2(x, y), 14.0f * s, c);
                if (ImGui::IsMouseHoveringRect(ImVec2(x - 10.0f * s, y - 10.0f * s), ImVec2(x + 10.0f * s, y + 10.0f * s))) {
                    ui::tooltip({vehicleName(*v).c_str(), vehicleStatus(*v).c_str()});
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) selVehicle_ = vid;
                }
            }
            ImGui::Dummy(ImVec2(width, 56.0f * s));

            ui::sectionHeader("Stops");
            for (const StationId sid : l.stops) {
                const Station* st = w.stations.tryGet(sid);
                if (!st) continue;
                const std::string waiting = std::format("{} waiting", st->waitingCount());
                ImGui::PushID(static_cast<int>(sid.value));
                if (ui::button(st->name.c_str(), ui::Icon::Station, ui::Btn::Ghost)) {
                    selStation_ = sid;
                    focusOn(st->sign);
                }
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(waiting.c_str()).x + ImGui::GetCursorPosX() - 4.0f * s);
                ImGui::AlignTextToFramePadding();
                ui::mutedText(waiting.c_str());
                ImGui::PopID();
            }
            ui::sectionHeader("Vehicles");
            if (ImGui::BeginChild("##lineVehicles", ImVec2(0, std::min(5.0f, static_cast<float>(l.vehicles.size())) * ImGui::GetTextLineHeightWithSpacing() + 8.0f * s),
                                  ImGuiChildFlags_Borders)) {
                for (const VehicleId vid : l.vehicles)
                    if (const Vehicle* v = w.vehicles.tryGet(vid)) {
                        const std::string label = std::format("{} - {} ({}/{})##lv{}", vehicleName(*v), vehicleStatus(*v), v->passengers.size(),
                                                              v->capacity(), vid.value);
                        if (ImGui::Selectable(label.c_str(), selVehicle_ == vid)) selVehicle_ = vid;
                    }
            }
            ImGui::EndChild();
            ImGui::Dummy(ImVec2(0, 2.0f * s));
            if (ui::button("Show on map", ui::Icon::Target, ui::Btn::Primary, ImVec2(-1.0f, 0.0f))) {
                // Frame the whole line.
                glm::dvec2 lo(1e18), hi(-1e18);
                for (const StationId sid : l.stops)
                    if (const Station* st = w.stations.tryGet(sid)) {
                        lo = glm::min(lo, stationXZ(*st));
                        hi = glm::max(hi, stationXZ(*st));
                    }
                if (lo.x <= hi.x) {
                    const glm::dvec2 mid = (lo + hi) * 0.5;
                    followVehicle_ = {};
                    followPerson_ = false;
                    camera_.targetGoal = mapdraw::groundPoint(w, mid.x, mid.y, 0.0);
                    camera_.distanceGoal = std::clamp(glm::length(hi - lo) * 1.1, 12.0, 400.0);
                }
            }
        }
    }
    ui::endPanel();
}

} // namespace tp
