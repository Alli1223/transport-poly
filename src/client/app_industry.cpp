// Industry windows and town needs: what an industry makes from what, how much of it reaches a
// station, where its nearest supplies are, and what each town needs delivered to keep growing.

#include "client/app.hpp"
#include "client/badges.hpp"
#include "client/ui/widgets.hpp"

#include <imgui.h>

#include "render/picking.hpp"
#include "sim/industries.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <format>
#include <string>

namespace tp {

namespace {

ImVec2 panelPoint(float fx, float fy, float dx, float dy) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float s = ui::scale();
    return ImVec2(vp->WorkPos.x + vp->WorkSize.x * fx + dx * s, vp->WorkPos.y + vp->WorkSize.y * fy + dy * s);
}

const char* classVehicles(CargoClass c) {
    switch (c) {
    case CargoClass::Bulk: return "hopper wagons, grain lorries or bulk coasters";
    case CargoClass::Refrigerated: return "refrigerated wagons, vans or coasters";
    case CargoClass::Piece: return "box wagons, goods lorries or container coasters";
    case CargoClass::Flatbed: return "flat wagons, timber lorries or deck cargo coasters";
    default: return "passenger services";
    }
}

glm::dvec2 footprintCentre(const Building& b) {
    const double half = b.size() * 0.5;
    return {b.pos.x + half, b.pos.y + half};
}

// Compass direction of an offset on the map (x runs east, y south).
const char* compass(glm::dvec2 d) {
    static constexpr const char* kNames[] = {"east", "north-east", "north", "north-west",
                                             "west", "south-west", "south", "south-east"};
    const double sector = std::round(std::atan2(-d.y, d.x) / (3.14159265358979 / 4.0));
    return kNames[(static_cast<int>(sector) + 8) % 8];
}

// What the countryside industry making `cargo` is called, in lower case ("logging camp").
std::string producerName(CargoType cargo) {
    std::string name(cargoName(cargo));
    for (const IndustryRecipe& r : industryRecipes())
        if (r.output == cargo && industries::isCountryside(r.building)) name = buildingType(r.building).name;
    for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return name;
}

double smoothstep(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace

std::vector<SupplyLink> nearestSupplies(const World& w, BuildingId industry, usize perCargo) {
    std::vector<SupplyLink> out;
    const Building* target = w.buildings.tryGet(industry);
    const IndustryRecipe* recipe = target ? recipeFor(target->type) : nullptr;
    if (!recipe) return out;
    const glm::dvec2 at = footprintCentre(*target);
    std::vector<SupplyLink> found;
    for (const CargoType in : recipe->inputs) {
        if (in == CargoType::None) continue;
        found.clear();
        for (const Industry& i : w.industries) {
            const Building* b = w.buildings.tryGet(i.building.id);
            if (i.building.id == industry || !b || b->serial != i.building.serial) continue;
            const IndustryRecipe* r = recipeFor(b->type);
            if (r && r->output == in) found.push_back({i.building.id, in, glm::length(footprintCentre(*b) - at)});
        }
        const auto n = static_cast<std::ptrdiff_t>(std::min(perCargo, found.size()));
        std::partial_sort(found.begin(), found.begin() + n, found.end(),
                          [](const SupplyLink& x, const SupplyLink& y) { return x.distance < y.distance; });
        out.insert(out.end(), found.begin(), found.begin() + n);
    }
    return out;
}

void App::drawSupplyArrows() {
    // Shown only for the industry whose window asked for them, until it closes.
    if (supplyArrows_ != selIndustry_) supplyArrows_ = {};
    const World& w = world();
    const Building* target = w.buildings.tryGet(supplyArrows_);
    if (!target) return;
    const std::vector<SupplyLink> links = nearestSupplies(w, supplyArrows_);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float s = uiScale_;
    const double shown = timeSeconds_ - supplyArrowsShown_;
    const ImVec2 display = ImGui::GetIO().DisplaySize;

    // Projection that keeps points off screen (unlike worldToScreen), so arcs run out of the view.
    constexpr double kNear = 0.3;
    const glm::dvec3 origin = camera_.target;
    const glm::dmat4 viewProj = camera_.viewProjectionD(origin);
    const auto clipOf = [&](const glm::dvec3& p) { return viewProj * glm::dvec4(p - origin, 1.0); };
    const auto screenOf = [&](const glm::dvec4& c) {
        return ImVec2(static_cast<float>((c.x / c.w + 1.0) * 0.5 * display.x),
                      static_cast<float>((1.0 - c.y / c.w) * 0.5 * display.y));
    };
    const auto onScreen = [&](ImVec2 p, float margin) {
        return p.x > -margin && p.y > -margin && p.x < display.x + margin && p.y < display.y + margin;
    };
    const auto project = [&](const glm::dvec3& p) -> std::optional<ImVec2> {
        const glm::dvec4 c = clipOf(p);
        if (c.w <= kNear) return std::nullopt;
        return screenOf(c);
    };
    // Pixels per tile around the camera's target, to keep the moving chevrons a readable distance apart.
    float tilePx = 1.0f;
    if (const auto o = project(origin))
        for (const glm::dvec3 step : {glm::dvec3(1, 0, 0), glm::dvec3(0, 0, 1)})
            if (const auto q = project(origin + step)) tilePx = std::max(tilePx, std::hypot(q->x - o->x, q->y - o->y));

    // Ripples on the ground around the industry the supplies are for.
    const Badge targetBadge = industryBadge(target->type);
    const glm::dvec2 to = footprintCentre(*target);
    const double toHalf = target->size() * 0.5;
    const double groundY = gfx::terrainHeightAt(w.map, to.x, to.y) + 0.15;
    constexpr int kRingPoints = 48;
    std::vector<ImVec2> ring;
    for (int pass = 0; pass < 2; ++pass) {
        const double cycle = std::fmod(timeSeconds_ * 0.6 + pass * 0.5, 1.0);
        const double radius = toHalf + 0.6 + cycle * 3.5;
        ring.clear();
        for (int k = 0; k < kRingPoints; ++k) {
            const double a = k * 6.283185307179586 / kRingPoints;
            const auto p = project({to.x + std::cos(a) * radius, groundY, to.y + std::sin(a) * radius});
            if (!p) break;
            ring.push_back(*p);
        }
        if (ring.size() != kRingPoints) continue;
        const float fade = static_cast<float>((1.0 - cycle) * std::min(1.0, shown / 0.4));
        dl->AddConvexPolyFilled(ring.data(), kRingPoints, ui::withAlpha(targetBadge.colour, 0.10f * fade));
        dl->AddPolyline(ring.data(), kRingPoints, ui::withAlpha(targetBadge.colour, 0.75f * fade), ImDrawFlags_Closed,
                        2.0f * s);
    }

    constexpr int kSamples = 72;
    std::vector<std::vector<ImVec2>> runs;
    for (usize i = 0; i < links.size(); ++i) {
        const SupplyLink& l = links[i];
        const Building* from = w.buildings.tryGet(l.from);
        if (!from || l.distance < 1.0) continue;
        // Arcs grow out of their sources one after another, easing to a stop at the target.
        const double delay = 0.07 * static_cast<double>(i);
        const double grow = std::clamp((shown - delay) / 0.8, 0.0, 1.0);
        if (grow <= 0.0) continue;
        const double reveal = 1.0 - std::pow(1.0 - grow, 3.0);
        const float fade = static_cast<float>(std::min(1.0, grow * 3.0));
        const Badge badge = cargoBadge(l.cargo);

        // From the edge of the source's footprint to the edge of the target's, arcing up by over a
        // quarter of the way so that arrows from different sources stand apart.
        const glm::dvec2 fc = footprintCentre(*from);
        const glm::dvec2 dir = (to - fc) / l.distance;
        const glm::dvec2 a = fc + dir * (from->size() * 0.5 + 0.4), b = to - dir * (toHalf + 0.4);
        const double ha = gfx::terrainHeightAt(w.map, a.x, a.y) + 0.8, hb = gfx::terrainHeightAt(w.map, b.x, b.y) + 0.8;
        const double chord = glm::length(b - a);
        const double lift = std::clamp(chord * 0.28, 3.0, 80.0);
        const auto arcAt = [&](double t) {
            const glm::dvec2 g = a + (b - a) * t;
            return glm::dvec3(g.x, ha + (hb - ha) * t + lift * 4.0 * t * (1.0 - t), g.y);
        };

        // The arc on screen, in runs broken where it passes behind the camera.
        runs.assign(1, {});
        glm::dvec4 prev(0.0);
        for (int k = 0; k <= kSamples; ++k) {
            const glm::dvec4 c = clipOf(arcAt(reveal * k / kSamples));
            const bool front = c.w > kNear;
            if (k > 0 && front != (prev.w > kNear)) {
                runs.back().push_back(screenOf(prev + (c - prev) * ((kNear - prev.w) / (c.w - prev.w))));
                if (!front) runs.emplace_back();
            }
            if (front) runs.back().push_back(screenOf(c));
            prev = c;
        }
        // A translucent tube: a dark edge and a soft glow, the cargo's colour and a light core.
        const ImU32 light = ui::mix(badge.colour, IM_COL32(255, 255, 255, 255), 0.5f);
        for (const auto& run : runs) {
            if (run.size() < 2) continue;
            const int n = static_cast<int>(run.size());
            dl->AddPolyline(run.data(), n, ui::withAlpha(badge.colour, 0.16f * fade), ImDrawFlags_None, 16.0f * s);
            dl->AddPolyline(run.data(), n, ui::withAlpha(ui::col::kBar, 0.32f * fade), ImDrawFlags_None, 6.5f * s);
            dl->AddPolyline(run.data(), n, ui::withAlpha(badge.colour, 0.68f * fade), ImDrawFlags_None, 3.8f * s);
            dl->AddPolyline(run.data(), n, ui::withAlpha(light, 0.5f * fade), ImDrawFlags_None, 1.3f * s);
        }

        // Chevrons streaming towards the target. They move between slots about three tiles apart,
        // using every `step`-th slot so that they stay some 30 pixels apart on screen.
        const double arcLength = chord + lift * 1.5;
        const int slots = std::clamp(static_cast<int>(arcLength / 3.0), 4, 240);
        int step = 1;
        while (step < 64 && arcLength / slots * tilePx * step < 30.0 * s) step *= 2;
        const double phase = std::fmod(timeSeconds_ * 1.4, static_cast<double>(step));
        const ImU32 chevron = light;
        const std::optional<ImVec2> head = project(arcAt(reveal));
        for (int k = 0; k * step + phase < slots; ++k) {
            const double t = (k * step + phase) / slots;
            if (t >= reveal) break;
            const auto p = project(arcAt(t)), q = project(arcAt(std::min(1.0, t + 0.002)));
            if (!p || !q || !onScreen(*p, 20.0f)) continue;
            if (head && std::hypot(head->x - p->x, head->y - p->y) < 26.0f * s) continue; // clear of the arrowhead
            const float dx = q->x - p->x, dy = q->y - p->y, len = std::hypot(dx, dy);
            if (len < 1e-4f) continue;
            const ImVec2 u(dx / len, dy / len), nrm(-u.y, u.x);
            const float alpha = fade * static_cast<float>(smoothstep(0.0, 0.06, t) * smoothstep(0.0, 0.05, reveal - t));
            const float sz = 6.0f * s;
            const ImVec2 pts[3] = {ImVec2(p->x - u.x * sz * 0.6f + nrm.x * sz, p->y - u.y * sz * 0.6f + nrm.y * sz),
                                   ImVec2(p->x + u.x * sz, p->y + u.y * sz),
                                   ImVec2(p->x - u.x * sz * 0.6f - nrm.x * sz, p->y - u.y * sz * 0.6f - nrm.y * sz)};
            dl->AddPolyline(pts, 3, ui::withAlpha(ui::col::kBar, 0.4f * alpha), ImDrawFlags_None, 4.6f * s);
            dl->AddPolyline(pts, 3, ui::withAlpha(chevron, 0.95f * alpha), ImDrawFlags_None, 2.6f * s);
        }

        // An arrowhead on the growing tip, pointing along the arc.
        const auto back = project(arcAt(std::max(0.0, reveal - 0.01)));
        if (head && back && onScreen(*head, 30.0f)) {
            const float dx = head->x - back->x, dy = head->y - back->y, len = std::hypot(dx, dy);
            if (len > 1e-4f) {
                const ImVec2 u(dx / len, dy / len), nrm(-u.y, u.x);
                const auto tri = [&](float length, float half, float lead, ImU32 colour) {
                    const ImVec2 tip(head->x + u.x * lead, head->y + u.y * lead);
                    const ImVec2 base(tip.x - u.x * length, tip.y - u.y * length);
                    dl->AddTriangleFilled(tip, ImVec2(base.x + nrm.x * half, base.y + nrm.y * half),
                                          ImVec2(base.x - nrm.x * half, base.y - nrm.y * half), colour);
                };
                tri(25.0f * s, 14.0f * s, 3.0f * s, ui::withAlpha(ui::col::kBar, 0.5f * fade));
                tri(20.0f * s, 10.5f * s, 0.0f, ui::withAlpha(badge.colour, 0.95f * fade));
                tri(11.0f * s, 4.5f * s, -4.0f * s, ui::withAlpha(light, 0.6f * fade));
            }
        }

        // Where it comes from: the cargo's badge at the start, and how far it is at the top of the arc.
        if (const auto p = project(arcAt(0.0)); p && onScreen(*p, 20.0f)) {
            const float r = 11.0f * s;
            dl->AddCircleFilled(*p, r + 2.0f * s, ui::withAlpha(ui::col::kBar, 0.8f * fade));
            dl->AddCircleFilled(*p, r, ui::withAlpha(badge.colour, 0.95f * fade));
            ui::drawIcon(dl, badge.icon, *p, r * 1.25f, ui::withAlpha(ui::col::kBar, fade));
        }
        const float labelFade = static_cast<float>(std::clamp((shown - delay - 0.7) / 0.3, 0.0, 1.0));
        if (const auto p = project(arcAt(0.5)); p && labelFade > 0.0f && onScreen(*p, 0.0f)) {
            ImFont* f = ui::fonts().small ? ui::fonts().small : ImGui::GetFont();
            const std::string text = std::format("{:.0f} tiles", l.distance);
            const ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.0f, text.c_str());
            const ImVec2 mn(p->x - ts.x * 0.5f - 7.0f * s, p->y - ts.y * 0.5f - 3.0f * s);
            const ImVec2 mx(p->x + ts.x * 0.5f + 7.0f * s, p->y + ts.y * 0.5f + 3.0f * s);
            const float r = (mx.y - mn.y) * 0.5f;
            dl->AddRectFilled(mn, mx, ui::withAlpha(ui::col::kBar, 0.86f * labelFade), r);
            dl->AddRect(mn, mx, ui::withAlpha(badge.colour, 0.7f * labelFade), r, 0, 1.2f * s);
            dl->AddText(f, f->FontSize, ImVec2(p->x - ts.x * 0.5f, p->y - ts.y * 0.5f),
                        ui::withAlpha(ui::col::kText, labelFade), text.c_str());
        }
    }
}

void App::drawIndustryWindow() {
    World& w = world();
    const Building* b = w.buildings.tryGet(selIndustry_);
    const IndustryRecipe* recipe = b ? recipeFor(b->type) : nullptr;
    const auto entry = b ? std::ranges::find_if(w.industries, [&](const Industry& i) { return i.building.id == selIndustry_; }) : w.industries.end();
    if (!recipe || entry == w.industries.end()) {
        selIndustry_ = {};
        return;
    }
    const Industry& ind = *entry;
    const float s = uiScale_;
    bool open = true;
    const bool raw = recipe->inputs[0] == CargoType::None;
    const std::string sub = std::format("{} industry - level {} of 8", ind.level == 1 ? "Small" : (ind.level <= 3 ? "Medium" : "Large"), ind.level);
    ui::PanelSpec spec;
    spec.pos = panelPoint(1.0f, 0.0f, -450.0f, 70.0f);
    spec.pivot = ImVec2(1.0f, 0.0f);
    spec.width = 380.0f;
    const Badge badge = industryBadge(b->type);
    spec.accent = badge.colour;
    spec.subtitle = sub.c_str();
    if (ui::beginPanel("###industryPanel", b->info().name.data(), badge.icon, &open, spec)) {
        const usize out = static_cast<usize>(recipe->output);
        const u32 daily = recipe->rate * ind.level / 2;
        const float cw = ui::columnWidth(3);
        const std::string made = std::format("{}", ind.producedLastMonth);
        const std::string shipped = ind.producedLastMonth ? std::format("{}%", std::min<u32>(100, ind.exportedLastMonth * 100 / ind.producedLastMonth)) : std::string("-");
        const std::string stock = std::format("{}", ind.stock[out]);
        ui::statCard(ui::Icon::Chart, made.c_str(), "Last month", ui::col::kLeaf, cw);
        ImGui::SameLine();
        ui::statCard(ui::Icon::Train, shipped.c_str(), "Transported", ui::col::kBlue, cw);
        ImGui::SameLine();
        ui::statCard(ui::Icon::Home, stock.c_str(), "In stock", ui::col::kAccent, cw);
        ImGui::Dummy(ImVec2(0, 2.0f * s));
        if (ind.level < 8) {
            const std::string progress = std::format("{} / 100", ind.growthProgress);
            ui::meterRow("Expansion", progress.c_str(), ind.growthProgress / 100.0f, ui::col::kGreen);
            ui::mutedText("Deliver at least half its monthly capacity to grow.");
        } else ui::mutedText("Maximum production level reached.");
        ui::sectionHeader("Produces");
        ui::statRow(cargoName(recipe->output).data(), raw ? std::format("{} a day", daily).c_str() : std::format("up to {} a day", daily).c_str());
        if (!raw) {
            ui::sectionHeader("Needs");
            for (const CargoType in : recipe->inputs) {
                if (in == CargoType::None) continue;
                ui::statRow(cargoName(in).data(), std::format("{} waiting", ind.stock[static_cast<usize>(in)]).c_str());
            }
            {
                ui::FontScope f(ui::fonts().small);
                ui::mutedText(recipe->inputs[1] != CargoType::None ? "It works only while it has both." : "Deliver it from a producer nearby.");
            }
            // Arrows over the map from the nearest producers of what it needs, listed here too.
            ImGui::Dummy(ImVec2(0, 2.0f * s));
            const bool showing = supplyArrows_ == selIndustry_;
            if (ui::button(showing ? "Hide nearest supplies" : "Show nearest supplies", ui::Icon::Eye,
                           showing ? ui::Btn::Primary : ui::Btn::Secondary, ImVec2(-1.0f, 0.0f))) {
                supplyArrows_ = showing ? BuildingId{} : selIndustry_;
                supplyArrowsShown_ = timeSeconds_;
            }
            ui::itemTooltip({"Nearest supplies", "Arrows over the map from the five nearest producers of each thing it needs."});
            if (showing) {
                const std::vector<SupplyLink> links = nearestSupplies(w, selIndustry_);
                const glm::dvec2 here = footprintCentre(*b);
                for (const CargoType in : recipe->inputs) {
                    if (in == CargoType::None) continue;
                    const Badge cb = cargoBadge(in);
                    bool found = false;
                    for (const SupplyLink& l : links) {
                        const Building* from = w.buildings.tryGet(l.from);
                        if (l.cargo != in || !from) continue;
                        found = true;
                        // A row with the cargo's chip, the producer and where it is; clicking looks at it.
                        const std::string where = std::format("{:.0f} tiles {}", l.distance, compass(footprintCentre(*from) - here));
                        ImGui::PushID(static_cast<int>(l.from.value));
                        const ImVec2 pos = ImGui::GetCursorScreenPos();
                        const float avail = ImGui::GetContentRegionAvail().x;
                        const float rowH = ImGui::GetTextLineHeight() + 8.0f * s;
                        if (ImGui::Selectable("##supply", false, ImGuiSelectableFlags_None, ImVec2(avail, rowH)))
                            focusOn(from->pos + TilePos{from->size() / 2, from->size() / 2});
                        ui::itemTooltip({"Look at it"});
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        const float r = rowH * 0.36f, cy = pos.y + rowH * 0.5f, textY = cy - ImGui::GetTextLineHeight() * 0.5f;
                        const ImVec2 chip(pos.x + r + 4.0f * s, cy);
                        dl->AddCircleFilled(chip, r, cb.colour);
                        ui::drawIcon(dl, cb.icon, chip, r * 1.3f, ui::col::kBar);
                        dl->AddText(ImVec2(pos.x + r * 2.0f + 12.0f * s, textY), ui::col::kText, from->info().name.data());
                        const float vw = ImGui::CalcTextSize(where.c_str()).x;
                        dl->AddText(ImVec2(pos.x + avail - vw - 6.0f * s, textY), ui::col::kTextMuted, where.c_str());
                        ImGui::PopID();
                    }
                    if (!found) {
                        ui::FontScope f(ui::fonts().small);
                        ui::iconText(cb.icon, cb.colour, std::format("No {} explored yet", producerName(in)).c_str(), ui::col::kTextMuted);
                    }
                }
            }
        }
        ui::statRow("Delivered here in all", std::format("{}", ind.delivered).c_str());
        // Stations within reach.
        ui::sectionHeader("Stations in reach");
        bool any = false;
        for (const auto [sid, st] : w.stations)
            if (cargo::covers(w, sid, *b)) {
                any = true;
                ui::statRow(st.name.c_str(), w.companies.contains(st.owner) ? w.companies[st.owner].name.c_str() : "");
            }
        if (!any) {
            ui::emptyState(ui::Icon::Station, "No station reaches it yet");
        }
        {
            ui::FontScope f(ui::fonts().small);
            ui::mutedText(std::format("Carry {} in {}.", cargoName(recipe->output), classVehicles(cargoClass(recipe->output))).c_str());
        }
        ImGui::Dummy(ImVec2(0, 2.0f * s));
        if (ui::button("Centre view", ui::Icon::Target, ui::Btn::Secondary, ImVec2(-1.0f, 0.0f))) focusOn(b->pos);
    }
    ui::endPanel();
    if (!open) selIndustry_ = {};
}

void App::drawTownNeeds(const Town& t) {
    const World& w = world();
    if (!w.settings.industries) return;
    ui::statRow("Passengers arrived last month", std::format("{}", t.arrivalsLastMonth).c_str());
    ui::statRow("Goods received last month", std::format("{}", t.goodsReceivedLastMonth).c_str());
    ui::sectionHeader("Needs");
    if (t.needs.empty()) {
        ui::emptyState(ui::Icon::Cart, "Nothing yet");
        return;
    }
    const u32 demand = towns::needDemand(t);
    bool allMet = true;
    for (const CargoType c : t.needs) {
        const u32 now = t.supplied[static_cast<usize>(c)];
        const bool met = towns::needMet(t, c);
        allMet = allMet && met;
        const std::string value = std::format("{} / {} this month{}", now, demand, met ? "  (met)" : "");
        ui::meterRow(cargoName(c).data(), value.c_str(), std::min(1.0f, static_cast<float>(now) / static_cast<float>(demand)),
                     met ? ui::col::kGreen : ui::col::kOrange);
    }
    ui::FontScope f(ui::fonts().small);
    if (const i32 next = towns::nextNeedPopulation(t); next > 0) {
        if (!allMet && t.population >= next) ui::iconText(ui::Icon::Warning, ui::col::kOrange, "Growth has stopped until every need is met", ui::col::kOrange);
        else ui::mutedText(std::format("Another need at {} people.", next).c_str());
    }
    ui::mutedText("Each need met speeds growth. Deliveries count at");
    ui::mutedText("the town's shops, offices and civic buildings.");
}

} // namespace tp
