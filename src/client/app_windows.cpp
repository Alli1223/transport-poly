// Information windows: depots, vehicles, stations, towns, finances, lists, news and the map.

#include "client/app.hpp"
#include "client/ui/widgets.hpp"

#include <imgui.h>

#include "core/calendar.hpp"
#include "core/names.hpp"
#include "core/math.hpp"
#include "render/person_pose.hpp"
#include "render/terrain_mesher.hpp"
#include "render/vehicle_pose.hpp"

#include <algorithm>
#include <cfloat>
#include <format>
#include <string>
#include <cctype>

namespace tp {

namespace {

i32 displayKmh(i32 units) { return units * 100 / 80; }

ui::Icon vehicleIcon(VehicleType t) {
    switch (t) {
    case VehicleType::Road: return ui::Icon::Bus;
    case VehicleType::Tram: return ui::Icon::Tram;
    case VehicleType::Rail: return ui::Icon::Train;
    case VehicleType::Ship: return ui::Icon::Ship;
    }
    return ui::Icon::Bus;
}

ImU32 typeColour(VehicleType t) {
    switch (t) {
    case VehicleType::Road: return ui::col::kRoad;
    case VehicleType::Tram: return ui::col::kTram;
    case VehicleType::Rail: return ui::col::kRail;
    case VehicleType::Ship: return ui::col::kWater;
    }
    return ui::col::kAccent;
}

ImU32 depotColour(DepotKind k) {
    switch (k) {
    case DepotKind::Road: return ui::col::kRoad;
    case DepotKind::Tram: return ui::col::kTram;
    case DepotKind::Rail: return ui::col::kRail;
    case DepotKind::Ship: return ui::col::kWater;
    }
    return ui::col::kAccent;
}

ImU32 statusColour(const Vehicle& v) {
    if (v.breakdownTicks > 0 || v.lost) return ui::col::kRed;
    if (v.state == VehicleState::InDepot || v.stopped) return ui::col::kTextMuted;
    if (v.state == VehicleState::Loading) return ui::col::kGreen;
    if (v.sendToDepot || v.goingForService) return ui::col::kOrange;
    return ui::col::kBlue;
}

ImU32 newsColour(NewsKind k) {
    switch (k) {
    case NewsKind::Town: return ui::col::kLeaf;
    case NewsKind::Company: return ui::col::kAccent;
    case NewsKind::Vehicle: return ui::col::kBlue;
    case NewsKind::Finance: return ui::col::kRed;
    default: return ui::col::kTextMuted;
    }
}

ui::Icon newsIcon(NewsKind k) {
    switch (k) {
    case NewsKind::Town: return ui::Icon::Town;
    case NewsKind::Company: return ui::Icon::Star;
    case NewsKind::Vehicle: return ui::Icon::Bus;
    case NewsKind::Finance: return ui::Icon::Coins;
    default: return ui::Icon::Info;
    }
}

ImU32 townColour(TownClass c) {
    return c == TownClass::City ? ui::col::kAccent : c == TownClass::Town ? ui::col::kBlue : ui::col::kLeaf;
}

// Green when good, through amber, to red when bad (t = 0..1).
ImU32 qualityColour(float t) {
    return t < 0.5f ? ui::mix(ui::col::kRed, ui::col::kAccent, t * 2.0f) : ui::mix(ui::col::kAccent, ui::col::kGreen, (t - 0.5f) * 2.0f);
}

std::string dateOf(const World& w, i64 tick) { return formatDate(w.clock.startDay + static_cast<i32>(tick / kTicksPerDay)); }

ImVec2 viewportPoint(float fx, float fy, float dx = 0.0f, float dy = 0.0f) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float s = ui::scale();
    return ImVec2(vp->WorkPos.x + vp->WorkSize.x * fx + dx * s, vp->WorkPos.y + vp->WorkSize.y * fy + dy * s);
}

// A small text button with a tooltip.
bool actionButton(const char* label, ui::Icon icon, ui::Btn kind, const char* tip, float width = 0.0f) {
    const bool pressed = ui::button(label, icon, kind, ImVec2(width, 0.0f));
    if (tip) ui::itemTooltip({tip});
    return pressed;
}

} // namespace

std::string App::vehicleName(const Vehicle& v) const {
    return std::format("{} {}", vehicleTypeName(v.info().type), v.unitNumber);
}

std::string App::vehicleStatus(const Vehicle& v) const {
    const World& w = session_->world();
    const auto stationName = [&](StationId s) {
        const Station* st = w.stations.tryGet(s);
        return st ? st->name : std::string("?");
    };
    if (v.state == VehicleState::InDepot) return v.stopped ? "Stopped in depot" : "Leaving depot";
    if (v.stopped) return "Stopped";
    if (v.breakdownTicks > 0) return "Broken down!";
    if (v.state == VehicleState::Loading) {
        const Order* o = v.currentOrder();
        return o ? std::format("Loading at {}", stationName(o->station)) : "Loading";
    }
    if (v.sendToDepot) return "Heading for depot";
    if (v.goingForService) return "Heading for depot (service)";
    if (v.lost) return "Lost - can't find a route!";
    if (const Order* o = v.currentOrder()) {
        if (o->kind == OrderKind::Station) return std::format("Heading for {}", stationName(o->station));
        return "Heading for depot (service)";
    }
    return "No orders";
}

void App::focusOn(TilePos t) {
    followVehicle_ = {};
    followPerson_ = false;
    camera_.targetGoal = glm::dvec3(t.x + 0.5, gfx::terrainHeightAt(world().map, t.x + 0.5, t.y + 0.5), t.y + 0.5);
}

void App::drawCitizens() {
    if (!showCitizens_) { followPerson_ = false; return; }
    World& w = world();
    const auto nameOf = [](const Person& p) {
        return generatePersonFirstName(p.nameSeed) + " " + generatePersonSurname(p.nameSeed ^ 0x9E3779B9u);
    };
    ui::PanelSpec spec;
    spec.pos = viewportPoint(0.0f, 0.0f, 20.0f, 80.0f);
    spec.width = 520.0f;
    spec.height = 650.0f;
    spec.subtitle = "Choose a resident to inspect their day";
    if (ui::beginPanel("###citizens", "Citizens", ui::Icon::People, &showCitizens_, spec)) {
        const Town* town = w.towns.tryGet(citizenTown_);
        if (ImGui::BeginCombo("Town", town ? town->name.c_str() : "All towns")) {
            if (ImGui::Selectable("All towns", !citizenTown_.valid())) { citizenTown_ = {}; selPerson_ = {}; }
            for (const auto [id, t] : w.towns)
                if (ImGui::Selectable(t.name.c_str(), id == citizenTown_)) { citizenTown_ = id; selPerson_ = {}; }
            ImGui::EndCombo();
        }
        static char search[80] = {};
        ImGui::InputTextWithHint("##citizenSearch", "Name or citizen ID", search, sizeof(search));
        std::string query(search);
        std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::vector<PersonId> shown;
        usize total = 0;
        for (const auto [id, p] : w.people) {
            if (citizenTown_.valid() && p.town != citizenTown_) continue;
            if (!query.empty()) {
                std::string text = nameOf(p) + " " + std::to_string(id.value);
                std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (text.find(query) == std::string::npos) continue;
            }
            ++total;
            if (shown.size() < 500) shown.push_back(id);
        }
        const auto select = [&](PersonId id) {
            selPerson_ = id; selPersonSerial_ = w.people[id].serial;
            citizenDiary_.clear(); followPerson_ = false;
        };
        if (const Person* p = w.people.tryGet(selPerson_); !p || p->serial != selPersonSerial_) {
            selPerson_ = {}; followPerson_ = false;
            if (!shown.empty()) select(shown.front());
        }
        ImGui::TextDisabled("%zu matching citizens%s", total, total > 500 ? " (first 500 shown; refine your search)" : "");
        ImGui::BeginChild("##residents", ImVec2(0, 140.0f * uiScale_), ImGuiChildFlags_Borders);
        for (const PersonId id : shown) {
            const Person& p = w.people[id];
            const std::string label = std::format("{} - {}##{}", nameOf(p), people::stateName(p.state), id.value);
            if (ImGui::Selectable(label.c_str(), selPerson_ == id)) select(id);
        }
        ImGui::EndChild();
        if (const Person* p = w.people.tryGet(selPerson_)) {
            ui::sectionHeader(nameOf(*p).c_str());
            if (ui::button(followPerson_ ? "Stop following" : "Follow citizen", ui::Icon::Eye, ui::Btn::Primary)) {
                followPerson_ = !followPerson_; followVehicle_ = {};
                if (const auto pose = gfx::personPose(w, *p, session_->tickFraction())) camera_.targetGoal = pose->pos;
            }
            ui::statRow("Citizen ID", std::to_string(selPerson_.value).c_str());
            ui::statRow("Now", std::string(people::stateName(p->state)).c_str());
            ui::statRow("Car owner", p->carOwner ? "Yes" : "No");
            if (p->nextEvent >= 0) {
                const i32 day = w.clock.startDay + static_cast<i32>(p->nextEvent / kTicksPerDay);
                const i32 minute = static_cast<i32>((p->nextEvent % kTicksPerDay) / kTicksPerMinute);
                ui::statRow("Next activity", std::format("{} {}", formatDate(day), formatTimeOfDay(minute)).c_str());
            } else ui::statRow("Next activity", "On arrival");
            ui::statRow("Usual departure", formatTimeOfDay(p->departMinute).c_str());
            ui::statRow("Last commute", std::format("{} minutes", p->commuteMinutes).c_str());
            ImGui::TextWrapped("Monday-Friday: work. Saturday-Sunday: shopping. Home after work or shopping.");
            const auto place = [&](const char* kind, BuildingRef ref, ui::Icon icon) {
                const Building* b = w.buildings.tryGet(ref.id);
                if (!b || b->serial != ref.serial) { ui::statRow(kind, "Unassigned"); return; }
                const std::string label = std::format("{}: {} ({}, {})", kind, b->info().name, b->pos.x, b->pos.y);
                if (ui::button(label.c_str(), icon, ui::Btn::Secondary, ImVec2(-1, 0))) focusOn(b->pos);
            };
            place("Home", p->home, ui::Icon::Home);
            place("Work", p->work, ui::Icon::Briefcase);
            place("Shop", p->shop, ui::Icon::Cart);
            if (const Station* stop = w.stations.tryGet(p->station))
                if (ui::button(("Station: " + stop->name).c_str(), ui::Icon::Station, ui::Btn::Ghost)) selStation_ = p->station;
            if (w.vehicles.contains(p->vehicle))
                if (ui::button("Inspect vehicle", ui::Icon::Bus, ui::Btn::Ghost)) selVehicle_ = p->vehicle;
            const std::string state(people::stateName(p->state));
            if (citizenDiary_.empty() || citizenDiary_.back().second != state) {
                citizenDiary_.push_back({w.clock.tick, state});
                if (citizenDiary_.size() > 12) citizenDiary_.pop_front();
            }
            ui::sectionHeader("Observed activities");
            for (auto it = citizenDiary_.rbegin(); it != citizenDiary_.rend(); ++it)
                ImGui::TextWrapped("%s - %s", formatTimeOfDay(static_cast<i32>((it->first % kTicksPerDay) / kTicksPerMinute)).c_str(), it->second.c_str());
        }
    }
    ui::endPanel();
    if (!showCitizens_) followPerson_ = false;
}

void App::drawWindows() {
    drawDepotWindow();
    drawVehicleWindow();
    drawStationWindow();
    drawTownWindow();
    drawFinanceWindow();
    drawVehicleList();
    drawNewsWindow();
    drawTownList();
    drawCitizens();
}

void App::drawNews(double now) {
    const World& w = world();
    // A new session starts with its old news already read.
    if (newsSession_ != session_.get()) {
        newsSession_ = session_.get();
        lastNewsTick_ = w.news.empty() ? -1 : w.news.back().tick;
        newsBanner_.reset();
    }
    if (!w.news.empty() && w.news.back().tick > lastNewsTick_) {
        lastNewsTick_ = w.news.back().tick;
        newsBanner_ = w.news.back();
        newsBannerUntil_ = now + 10.0;
    }
    if (!newsBanner_ || now > newsBannerUntil_) return;
    const float s = uiScale_;
    // Slides in from the left and out again.
    const float shown = static_cast<float>(10.0 - (newsBannerUntil_ - now));
    const float in = std::min(1.0f, shown / 0.3f);
    const float out = std::min(1.0f, static_cast<float>(newsBannerUntil_ - now) / 0.3f);
    const float k = std::min(in, out);
    const float ease = 1.0f - (1.0f - k) * (1.0f - k);
    constexpr float kWidth = 380.0f;
    ui::PanelSpec spec;
    spec.pos = viewportPoint(0.0f, 1.0f, 14.0f - (1.0f - ease) * (kWidth + 30.0f), -14.0f);
    spec.pivot = ImVec2(0.0f, 1.0f);
    spec.cond = ImGuiCond_Always;
    spec.width = kWidth;
    spec.accent = newsColour(newsBanner_->kind);
    const std::string date = dateOf(w, newsBanner_->tick);
    spec.subtitle = date.c_str();
    spec.flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoMove;
    bool open = true;
    if (ui::beginPanel("##newsbanner", "Transport Times", ui::Icon::News, &open, spec)) {
        {
            ui::FontScope f(ui::fonts().heading);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(newsBanner_->text.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::Dummy(ImVec2(0, 2.0f * s));
        if (newsBanner_->location) {
            if (ui::button("Go there", ui::Icon::Target, ui::Btn::Primary)) focusOn(*newsBanner_->location);
            ImGui::SameLine();
        }
        if (ui::button("All news", ui::Icon::News, ui::Btn::Ghost)) showNews_ = true;
    }
    ui::endPanel();
    if (!open) newsBanner_.reset();
}

void App::drawNewsWindow() {
    if (!showNews_) return;
    const World& w = world();
    const float s = uiScale_;
    ui::PanelSpec spec;
    spec.pos = viewportPoint(0.0f, 0.0f, 20.0f, 80.0f);
    spec.width = 440.0f;
    spec.height = 420.0f;
    spec.subtitle = "Latest first";
    if (ui::beginPanel("###newsPanel", "News", ui::Icon::News, &showNews_, spec)) {
        ImGui::BeginChild("##list", ImVec2(0, 0), ImGuiChildFlags_None);
        if (w.news.empty()) ui::emptyState(ui::Icon::News, "Nothing has happened yet");
        int n = 0;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (auto it = w.news.rbegin(); it != w.news.rend() && n < 100; ++it, ++n) {
            ImGui::PushID(n);
            const ImU32 c = newsColour(it->kind);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float avail = ImGui::GetContentRegionAvail().x;
            // The card background goes underneath the text, which is laid out first.
            dl->ChannelsSplit(2);
            dl->ChannelsSetCurrent(1);
            ImGui::BeginGroup();
            ImGui::Indent(38.0f * s);
            ImGui::Dummy(ImVec2(0, 4.0f * s));
            {
                ui::FontScope f(ui::fonts().small);
                ui::mutedText(dateOf(w, it->tick).c_str());
            }
            ImGui::PushTextWrapPos(p.x - ImGui::GetWindowPos().x + avail - (it->location ? 34.0f * s : 8.0f * s));
            ImGui::TextUnformatted(it->text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0, 3.0f * s));
            ImGui::Unindent(38.0f * s);
            ImGui::EndGroup();
            const float h = ImGui::GetItemRectSize().y;
            dl->ChannelsSetCurrent(0);
            dl->AddRectFilled(p, ImVec2(p.x + avail, p.y + h), ui::col::kCard, 10.0f * s);
            dl->AddCircleFilled(ImVec2(p.x + 19.0f * s, p.y + 19.0f * s), 13.0f * s, ui::withAlpha(c, 0.16f));
            ui::drawIcon(dl, newsIcon(it->kind), ImVec2(p.x + 19.0f * s, p.y + 19.0f * s), 14.0f * s, c);
            dl->ChannelsMerge();
            if (it->location) {
                const ImVec2 back = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(ImVec2(p.x + avail - ImGui::GetFrameHeight() * 0.85f - 6.0f * s, p.y + 6.0f * s));
                if (ui::miniButton("##goto", ui::Icon::Target, ui::col::kTextMuted, "Go there")) focusOn(*it->location);
                ImGui::SetCursorScreenPos(back);
            }
            ImGui::Dummy(ImVec2(0, 1.0f * s));
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    ui::endPanel();
}

void App::drawMinimap(double now) {
    if (!showMinimap_) return;
    const World& w = world();
    const glm::dvec2 cam(camera_.target.x, camera_.target.z);
    const double drift = glm::length(cam - minimapCentre_) / minimapScale_;
    if (minimapUpdated_ < 0.0 || now - minimapUpdated_ > 0.5 || drift > 24.0) {
        minimap_.update(w, cam.x, cam.y, minimapScale_);
        minimapCentre_ = cam;
        minimapUpdated_ = now;
    }
    if (!minimap_.ready()) return;
    const float s = uiScale_;
    const float size = 256.0f * s;
    ui::PanelSpec spec;
    spec.pos = viewportPoint(1.0f, 1.0f, -14.0f, -14.0f);
    spec.pivot = ImVec2(1.0f, 1.0f);
    spec.width = 256.0f + 32.0f;
    spec.accent = ui::col::kTeal;
    const std::string sub = std::format("{} tiles across", gfx::Minimap::kSize * minimapScale_);
    spec.subtitle = sub.c_str();
    spec.flags = ImGuiWindowFlags_NoScrollbar;
    if (ui::beginPanel("###mapPanel", "Map", ui::Icon::Map, &showMinimap_, spec)) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##map", ImVec2(size, size));
        const bool hovered = ImGui::IsItemHovered();
        const float k = size / static_cast<float>(gfx::Minimap::kSize);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 m = ImGui::GetMousePos();
            focusOn(minimap_.tileAt((m.x - at.x) / k, (m.y - at.y) / k));
        }
        if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
            // Scroll over the map to zoom it.
            if (ImGui::GetIO().MouseWheel < 0.0f && minimapScale_ < 8) minimapScale_ *= 2;
            if (ImGui::GetIO().MouseWheel > 0.0f && minimapScale_ > 1) minimapScale_ /= 2;
            minimapUpdated_ = -1.0;
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 end(at.x + size, at.y + size);
        dl->AddImageRounded(static_cast<ImTextureID>(minimap_.textureId()), at, end, ImVec2(0, 0), ImVec2(1, 1),
                            IM_COL32_WHITE, 10.0f * s);
        dl->AddRect(at, end, ui::col::kBorderStrong, 10.0f * s);
        dl->PushClipRect(at, end, true);
        // Camera view and town names.
        const ImVec2 c(at.x + static_cast<float>(minimap_.pixelX(cam.x)) * k, at.y + static_cast<float>(minimap_.pixelY(cam.y)) * k);
        const float r = std::max(5.0f * s, static_cast<float>(camera_.distance / minimapScale_) * k * 0.6f);
        dl->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), IM_COL32(255, 255, 255, 30), 3.0f * s);
        dl->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), IM_COL32(255, 255, 255, 230), 3.0f * s, 0, 1.5f * s);
        ImFont* f = ui::fonts().small;
        for (const auto [id, t] : w.towns) {
            const ImVec2 p(at.x + static_cast<float>(minimap_.pixelX(t.centre.x + 0.5)) * k,
                           at.y + static_cast<float>(minimap_.pixelY(t.centre.y + 0.5)) * k);
            if (p.x < at.x || p.y < at.y || p.x > end.x || p.y > end.y) continue;
            const ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.0f, t.name.c_str());
            dl->AddCircleFilled(p, 2.5f * s, townColour(t.cls));
            dl->AddText(f, f->FontSize, ImVec2(p.x - ts.x * 0.5f + 1, p.y + 3.0f * s + 1), IM_COL32(0, 0, 0, 190), t.name.c_str());
            dl->AddText(f, f->FontSize, ImVec2(p.x - ts.x * 0.5f, p.y + 3.0f * s), ui::col::kText, t.name.c_str());
        }
        dl->PopClipRect();
        // Zoom buttons over the map's corner.
        const ImVec2 back = ImGui::GetCursorScreenPos();
        const float bs = ImGui::GetFrameHeight() * 0.85f;
        dl->AddRectFilled(ImVec2(end.x - bs * 2 - 10.0f * s, at.y + 6.0f * s), ImVec2(end.x - 6.0f * s, at.y + bs + 10.0f * s),
                          ui::withAlpha(ui::col::kBar, 0.85f), 8.0f * s);
        ImGui::SetCursorScreenPos(ImVec2(end.x - bs * 2 - 8.0f * s, at.y + 8.0f * s));
        if (ui::miniButton("##zoomout", ui::Icon::Minus, ui::col::kText, "Zoom out") && minimapScale_ < 8) {
            minimapScale_ *= 2;
            minimapUpdated_ = -1.0;
        }
        ImGui::SameLine(0, 0);
        if (ui::miniButton("##zoomin", ui::Icon::Plus, ui::col::kText, "Zoom in") && minimapScale_ > 1) {
            minimapScale_ /= 2;
            minimapUpdated_ = -1.0;
        }
        ImGui::SetCursorScreenPos(back);
        const char* modes[] = {"None", "Station catchments", "Passenger flow", "Town zones"};
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo("##overlay", &overlayMode_, modes, 4);
        if (overlayMode_ == 2) ImGui::TextWrapped("Waiting and riding passengers. Wider arrows show more demand.");
        if (overlayMode_ == 3) {
            ui::legendItem(ui::col::kPurple, "Core"); ImGui::SameLine();
            ui::legendItem(ui::col::kBlue, "Inner"); ImGui::SameLine();
            ui::legendItem(ui::col::kLeaf, "Outer");
        }
    }
    ui::endPanel();
}

void App::drawTownList() {
    if (!showTownList_) return;
    const World& w = world();
    const float s = uiScale_;
    std::vector<TownId> ids;
    for (const auto [id, t] : w.towns) ids.push_back(id);
    std::ranges::sort(ids, [&](TownId a, TownId b) { return w.towns[a].population > w.towns[b].population; });
    i64 total = 0;
    for (const TownId id : ids) total += w.towns[id].population;
    const std::string sub = std::format("{} towns, {} people", ids.size(), total);
    ui::PanelSpec spec;
    spec.pos = viewportPoint(0.5f, 0.0f, -250.0f, 80.0f);
    spec.width = 500.0f;
    spec.height = 440.0f;
    spec.accent = ui::col::kLeaf;
    spec.subtitle = sub.c_str();
    if (ui::beginPanel("###townsPanel", "Towns", ui::Icon::Town, &showTownList_, spec)) {
        if (ImGui::BeginTable("towns", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit |
                                              ImGuiTableFlags_PadOuterX)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Town", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("People", 0, 70.0f * s);
            ImGui::TableSetupColumn("Transit", 0, 90.0f * s);
            ImGui::TableSetupColumn("Your rating", 0, 96.0f * s);
            ImGui::TableHeadersRow();
            for (const TownId id : ids) {
                const Town& t = w.towns[id];
                ImGui::TableNextRow(0, 26.0f * s);
                ImGui::TableNextColumn();
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const std::string label = std::format("     {}##{}", t.name, id.value);
                if (ImGui::Selectable(label.c_str(), selTown_ == id, ImGuiSelectableFlags_SpanAllColumns)) {
                    selTown_ = id;
                    focusOn(t.centre);
                }
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 7.0f * s, p.y + ImGui::GetTextLineHeight() * 0.5f),
                                                            4.0f * s, townColour(t.cls));
                ImGui::TableNextColumn();
                ImGui::Text("%d", t.population);
                ImGui::TableNextColumn();
                const TripStats& m = t.lastMonth.trips > 0 ? t.lastMonth : t.thisMonth;
                if (m.trips > 0) {
                    const float share = static_cast<float>(m.transit) / static_cast<float>(m.trips);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetTextLineHeight() * 0.35f);
                    ui::meter(share, ui::col::kBlue, 6.0f);
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableNextColumn();
                const i32 r = towns::rating(t, session_->localCompany());
                ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(qualityColour(static_cast<float>(r - kRatingMin) / (kRatingMax - kRatingMin))));
                ImGui::TextUnformatted(ratingName(r).data());
                ImGui::PopStyleColor();
            }
            ImGui::EndTable();
        }
    }
    ui::endPanel();
}

void App::drawDepotWindow() {
    World& w = world();
    const Depot* d = w.depots.tryGet(selDepot_);
    if (!d) {
        selDepot_ = {};
        return;
    }
    const float s = uiScale_;
    const Town* town = w.towns.tryGet(d->town);
    const char* kind = d->kind == DepotKind::Tram   ? "Tram depot"
                       : d->kind == DepotKind::Rail ? "Train depot"
                       : d->kind == DepotKind::Ship ? "Shipyard"
                                                    : "Bus depot";
    const std::string sub = town ? std::format("Near {}", town->name) : std::string("In the countryside");
    const bool rail = d->kind == DepotKind::Rail;
    const i32 year = w.clock.date().year;
    const auto models = vehicleModels();
    bool open = true;
    ui::PanelSpec spec;
    spec.pos = viewportPoint(0.0f, 0.0f, 20.0f, 80.0f);
    spec.width = 440.0f;
    spec.accent = depotColour(d->kind);
    spec.subtitle = sub.c_str();
    if (ui::beginPanel("###depotPanel", kind, d->kind == DepotKind::Ship ? ui::Icon::Shipyard : ui::Icon::Depot, &open,
                       spec)) {
        const bool mine = d->owner == session_->localCompany();
        ui::sectionHeader("Inside");
        int inside = 0;
        for (const auto [id, v] : w.vehicles) {
            if (v.depot != selDepot_ || v.state != VehicleState::InDepot) continue;
            ++inside;
            ImGui::PushID(static_cast<int>(id.value));
            const float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            ui::iconText(vehicleIcon(v.info().type), typeColour(v.info().type), vehicleName(v).c_str());
            ImGui::SameLine();
            ui::badge(v.stopped ? "Stopped" : "Leaving", v.stopped ? ui::col::kTextMuted : ui::col::kGreen);
            if (mine) {
                ImGui::SameLine(rowRight - ImGui::GetFrameHeight() * 0.85f * 3 - 6.0f * s);
                if (ui::miniButton("##start", v.stopped ? ui::Icon::Play : ui::Icon::Pause, ui::col::kGreen,
                                   v.stopped ? "Start" : "Stop"))
                    session_->submit(CmdStartStopVehicle{id});
                ImGui::SameLine(0, 2.0f * s);
                if (ui::miniButton("##orders", ui::Icon::Info, ui::col::kTextMuted, "Details and orders")) selVehicle_ = id;
                ImGui::SameLine(0, 2.0f * s);
                if (ui::miniButton("##sell", ui::Icon::Trash, ui::col::kRed, "Sell")) session_->submit(CmdSellVehicle{id});
            }
            if (rail) {
                // Consist: the engine followed by its carriages, which can be detached while stopped.
                ImDrawList* dl = ImGui::GetWindowDrawList();
                for (usize k = 0; k < v.cars.size(); ++k) {
                    const VehicleModel& cm = vehicleModel(v.cars[k]);
                    ImGui::PushID(static_cast<int>(k));
                    if (k > 0) ImGui::SameLine(0, 4.0f * s);
                    ui::FontScope f(ui::fonts().small);
                    const ImVec2 ts = ImGui::CalcTextSize(cm.name.data());
                    const bool removable = mine && k > 0 && v.stopped;
                    const float w0 = ts.x + 16.0f * s + (removable ? 16.0f * s : 0.0f);
                    if (ImGui::GetContentRegionAvail().x < w0 && k > 0) ImGui::NewLine();
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    const float h = ts.y + 8.0f * s;
                    ImGui::Dummy(ImVec2(w0, h));
                    const ImU32 c = k == 0 ? ui::col::kRail : ui::col::kTextMuted;
                    dl->AddRectFilled(p, ImVec2(p.x + w0, p.y + h), ui::withAlpha(c, 0.15f), 6.0f * s);
                    dl->AddText(ImVec2(p.x + 8.0f * s, p.y + 4.0f * s), k == 0 ? ui::col::kRail : ui::col::kText, cm.name.data());
                    if (ImGui::IsItemHovered())
                        ui::tooltip({cm.name.data(), std::format("{} seats, {} km/h", cm.capacity, cm.maxSpeedKmh).c_str()});
                    if (removable) {
                        const ImVec2 back = ImGui::GetCursorScreenPos();
                        ImGui::SetCursorScreenPos(ImVec2(p.x + w0 - 18.0f * s, p.y + (h - 16.0f * s) * 0.5f));
                        if (ImGui::InvisibleButton("##rm", ImVec2(16.0f * s, 16.0f * s)))
                            session_->submit(CmdRemoveCarriage{id, static_cast<u16>(k)});
                        ui::drawIcon(dl, ui::Icon::Close, ImVec2(p.x + w0 - 10.0f * s, p.y + h * 0.5f), 9.0f * s,
                                     ImGui::IsItemHovered() ? ui::col::kRed : ui::col::kTextMuted);
                        ImGui::SetCursorScreenPos(back);
                    }
                    ImGui::PopID();
                }
                const std::string seats = std::format("{} seats", v.capacity());
                ui::mutedText(seats.c_str());
                if (mine && v.stopped) {
                    ImGui::SameLine();
                    if (ui::button("Add carriage", ui::Icon::Plus, ui::Btn::Ghost)) ImGui::OpenPopup("addcar");
                    if (ImGui::BeginPopup("addcar")) {
                        ui::FontScope f(ui::fonts().small);
                        ui::mutedText("CARRIAGES");
                        for (u16 i = 0; i < models.size(); ++i) {
                            const VehicleModel& m = models[i];
                            if (m.type != VehicleType::Rail || m.powered || m.introYear > year) continue;
                            const std::string item = std::format("{}   {} seats  {} km/h  {}", m.name, m.capacity,
                                                                 m.maxSpeedKmh, formatMoney(w.inflated(m.price)));
                            if (ImGui::Selectable(item.c_str())) session_->submit(CmdAddCarriage{id, i});
                        }
                        ImGui::EndPopup();
                    }
                } else if (mine) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(stop the train to change carriages)");
                }
            }
            ImGui::Dummy(ImVec2(0, 2.0f * s));
            ImGui::PopID();
        }
        if (inside == 0) ui::emptyState(vehicleIcon(rail ? VehicleType::Rail : VehicleType::Road), "No vehicles inside");
        if (mine) {
            ui::sectionHeader(rail ? "Buy an engine" : "Buy a vehicle");
            const VehicleType want = d->kind == DepotKind::Tram   ? VehicleType::Tram
                                     : d->kind == DepotKind::Rail ? VehicleType::Rail
                                     : d->kind == DepotKind::Ship ? VehicleType::Ship
                                                                  : VehicleType::Road;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            for (u16 i = 0; i < models.size(); ++i) {
                const VehicleModel& m = models[i];
                if (m.type != want || m.introYear > year || (rail && !m.powered)) continue;
                ImGui::PushID(i);
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float avail = ImGui::GetContentRegionAvail().x;
                const float h = 52.0f * s;
                dl->AddRectFilled(p, ImVec2(p.x + avail, p.y + h), ui::col::kCard, 10.0f * s);
                ui::drawIcon(dl, vehicleIcon(m.type), ImVec2(p.x + 22.0f * s, p.y + h * 0.5f), 22.0f * s, typeColour(m.type));
                dl->AddText(ImVec2(p.x + 44.0f * s, p.y + 8.0f * s), ui::col::kText, m.name.data());
                const std::string spec2 = m.capacity > 0
                                              ? std::format("{} seats  -  {} km/h  -  {}/yr", m.capacity, m.maxSpeedKmh,
                                                            formatMoney(w.inflated(m.runningCost)))
                                              : std::format("{} km/h  -  {}/yr", m.maxSpeedKmh, formatMoney(w.inflated(m.runningCost)));
                ImFont* sf = ui::fonts().small;
                dl->AddText(sf, sf->FontSize, ImVec2(p.x + 44.0f * s, p.y + 29.0f * s), ui::col::kTextMuted, spec2.c_str());
                ImGui::Dummy(ImVec2(avail, h));
                const std::string price = formatMoney(w.inflated(m.price));
                const float bw = 110.0f;
                ImGui::SetCursorScreenPos(ImVec2(p.x + avail - bw * s - 10.0f * s, p.y + (h - ImGui::GetFrameHeight() - 4.0f * s) * 0.5f));
                const std::string label = std::format("{}##buy", price);
                if (ui::button(label.c_str(), ui::Icon::Plus, ui::Btn::Primary, ImVec2(bw, 0)))
                    pendingBuySeq_ = session_->submit(CmdBuyVehicle{selDepot_, i});
                ui::itemTooltip({"Buy", std::format("Introduced {}, expected life {} years", m.introYear, m.lifeYears).c_str()});
                ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 6.0f * s));
                ImGui::Dummy(ImVec2(0, 0));
                ImGui::PopID();
            }
            if (rail) {
                ui::FontScope f(ui::fonts().small);
                ui::mutedText("Carriages are added to a stopped train inside the depot.");
            }
        }
    }
    ui::endPanel();
    if (!open) selDepot_ = {};
}

void App::drawVehicleWindow() {
    World& w = world();
    Vehicle* v = w.vehicles.tryGet(selVehicle_);
    if (!v) {
        selVehicle_ = {};
        if (tool_ == Tool::PickOrder) tool_ = Tool::None;
        return;
    }
    const float s = uiScale_;
    const VehicleModel& m = v->info();
    const bool mine = v->owner == session_->localCompany();
    const std::string title = vehicleName(*v);
    bool open = true;
    ui::PanelSpec spec;
    // Off to the side, so that a followed vehicle in the middle of the screen stays visible.
    spec.pos = viewportPoint(1.0f, 0.0f, -20.0f, 70.0f);
    spec.pivot = ImVec2(1.0f, 0.0f);
    spec.width = 410.0f;
    spec.accent = typeColour(m.type);
    spec.subtitle = m.name.data();
    if (ui::beginPanel("###vehiclePanel", title.c_str(), vehicleIcon(m.type), &open, spec)) {
        ui::badge(vehicleStatus(*v).c_str(), statusColour(*v));
        if (followVehicle_ == selVehicle_) {
            ImGui::SameLine();
            ui::badge("Following", ui::col::kBlue);
        }
        if (v->orderGroup != 0) {
            ImGui::SameLine();
            ui::badge("Shared orders", ui::col::kPurple);
        }
        ImGui::Dummy(ImVec2(0, 2.0f * s));
        const float cw = ui::columnWidth(3);
        const std::string speed = std::format("{} km/h", displayKmh(v->speed));
        const std::string load = std::format("{} / {}", v->passengers.size(), v->capacity());
        const std::string profit = formatMoney(v->profitThisYear);
        ui::statCard(ui::Icon::Gauge, speed.c_str(), "Speed", ui::col::kBlue, cw);
        ImGui::SameLine();
        ui::statCard(ui::Icon::People, load.c_str(), "Passengers", ui::col::kTeal, cw);
        ImGui::SameLine();
        ui::statCard(ui::Icon::Coins, profit.c_str(), "Profit this year", v->profitThisYear < 0 ? ui::col::kRed : ui::col::kGreen, cw);

        const float rel = static_cast<float>(v->reliability) / 10000.0f;
        const std::string relText = std::format("{:.0f}%", rel * 100.0f);
        ImGui::Dummy(ImVec2(0, 2.0f * s));
        ui::meterRow("Reliability", relText.c_str(), rel, qualityColour(std::clamp((rel - 0.3f) / 0.6f, 0.0f, 1.0f)));

        ui::sectionHeader("Details");
        const i32 age = vehicles::ageDays(w, *v);
        const bool old = age > m.lifeYears * 365;
        ui::statRow("Age", std::format("{} of {} years", age / 365, m.lifeYears).c_str(), old ? ui::col::kRed : ui::col::kText);
        ui::moneyRow("Profit last year", v->profitLastYear);
        ui::statRow("Passengers this year", std::format("{}", v->passengersThisYear).c_str());
        ui::statRow("Value", formatMoney(v->value).c_str());
        ui::statRow("Last serviced", dateOf(w, v->lastServiceTick).c_str());
        ui::statRow("Breakdowns since service", std::format("{}", v->breakdownsSinceService).c_str(),
                    v->breakdownsSinceService > 0 ? ui::col::kOrange : ui::col::kText);

        ImGui::Dummy(ImVec2(0, 4.0f * s));
        const float bw = ui::columnWidth(4) / s;
        if (mine) {
            if (actionButton(v->stopped ? "Start##ss" : "Stop##ss", v->stopped ? ui::Icon::Play : ui::Icon::Pause,
                             v->stopped ? ui::Btn::Primary : ui::Btn::Secondary, v->stopped ? "Start the vehicle" : "Stop the vehicle", bw))
                session_->submit(CmdStartStopVehicle{selVehicle_});
            ImGui::SameLine();
            if (actionButton(v->sendToDepot ? "Cancel##dep" : "Depot##dep", ui::Icon::Depot, ui::Btn::Secondary,
                             v->sendToDepot ? "Cancel the trip to the depot" : "Send to its depot and stop there", bw))
                session_->submit(CmdSendToDepot{selVehicle_});
            ImGui::SameLine();
            if (actionButton("Skip", ui::Icon::Skip, ui::Btn::Secondary, "Skip to the next order", bw))
                session_->submit(CmdSkipOrder{selVehicle_});
            ImGui::SameLine();
        }
        const bool following = followVehicle_ == selVehicle_;
        if (actionButton(following ? "Unfollow" : "Follow", ui::Icon::Eye, following ? ui::Btn::Primary : ui::Btn::Secondary,
                         "Keep the camera on this vehicle", mine ? bw : 0.0f)) {
            followVehicle_ = following ? VehicleId{} : selVehicle_;
            followPerson_ = false;
        }
        if (mine && w.depots.contains(v->depot)) {
            if (actionButton("Clone", ui::Icon::Clone, ui::Btn::Ghost, "Buy a copy in its depot, with a copy of the orders", bw))
                pendingBuySeq_ = session_->submit(CmdCloneVehicle{v->depot, selVehicle_, false});
            ImGui::SameLine();
            if (actionButton("Shared", ui::Icon::Link, ui::Btn::Ghost, "Buy a copy that shares this vehicle's order list", bw))
                pendingBuySeq_ = session_->submit(CmdCloneVehicle{v->depot, selVehicle_, true});
            if (v->state == VehicleState::InDepot && v->stopped) {
                ImGui::SameLine();
                if (actionButton("Sell", ui::Icon::Trash, ui::Btn::Danger, "Sell the vehicle", bw))
                    session_->submit(CmdSellVehicle{selVehicle_});
            }
        }

        ui::sectionHeader("Orders");
        if (v->orderGroup != 0) {
            int sharing = 0;
            for (const auto [oid, o] : w.vehicles)
                if (oid != selVehicle_ && o.orderGroup == v->orderGroup && o.owner == v->owner) ++sharing;
            const std::string shared = std::format("Shared with {} other vehicle{}", sharing, sharing == 1 ? "" : "s");
            ui::iconText(ui::Icon::Link, ui::col::kPurple, shared.c_str(), ui::col::kTextMuted);
            if (mine) {
                ImGui::SameLine();
                if (ui::button("Unshare", ui::Icon::None, ui::Btn::Ghost)) session_->submit(CmdUnshareOrders{selVehicle_});
            }
        }
        std::vector<Order> orders = v->orders;
        bool changed = false;
        int remove = -1, moveUp = -1;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (usize i = 0; i < orders.size(); ++i) {
            Order& o = orders[i];
            ImGui::PushID(static_cast<int>(i));
            const bool current = i == v->orderIndex % std::max<usize>(1, orders.size());
            const Station* st = o.kind == OrderKind::Station ? w.stations.tryGet(o.station) : nullptr;
            std::string name = o.kind == OrderKind::Station ? (st ? st->name : std::string("(removed)"))
                : o.kind == OrderKind::Via ? std::format("Via {}, {}", o.waypoint.x, o.waypoint.y) : "Service at depot";
            if (st && (o.flags & kOrderPlatform)) {
                const int number = track::platformNumber(w, o.station, o.platform);
                name += number > 0 ? std::format(" - platform {}", number) : std::string(" - platform removed");
            }
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float avail = ImGui::GetContentRegionAvail().x;
            const float h = 34.0f * s;
            const float buttons = mine ? ImGui::GetFrameHeight() * 0.85f * 3 + 12.0f * s : 0.0f;
            ImGui::InvisibleButton("##row", ImVec2(avail - buttons, h));
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked() && st) focusOn(st->sign);
            if (ImGui::IsItemClicked() && o.kind == OrderKind::Via) focusOn(o.waypoint);
            dl->AddRectFilled(p, ImVec2(p.x + avail, p.y + h),
                              current ? ui::withAlpha(spec.accent, 0.14f) : (hovered ? ui::col::kCardHover : ui::col::kCard), 9.0f * s);
            if (current) dl->AddRectFilled(p, ImVec2(p.x + 3.0f * s, p.y + h), spec.accent, 9.0f * s, ImDrawFlags_RoundCornersLeft);
            const ImVec2 numC(p.x + 20.0f * s, p.y + h * 0.5f);
            dl->AddCircleFilled(numC, 10.0f * s, current ? spec.accent : ui::col::kFrameHover);
            const std::string num = std::format("{}", i + 1);
            ImFont* sf = ui::fonts().small;
            const ImVec2 ns = sf->CalcTextSizeA(sf->FontSize, FLT_MAX, 0.0f, num.c_str());
            dl->AddText(sf, sf->FontSize, ImVec2(numC.x - ns.x * 0.5f, numC.y - ns.y * 0.5f),
                        current ? ui::col::kTextOnAccent : ui::col::kText, num.c_str());
            const ImVec2 ts = ImGui::CalcTextSize(name.c_str());
            float tx = p.x + 38.0f * s;
            if (o.kind == OrderKind::Depot) {
                ui::drawIcon(dl, ui::Icon::Wrench, ImVec2(tx + 7.0f * s, p.y + h * 0.5f), 13.0f * s, ui::col::kOrange);
                tx += 20.0f * s;
            }
            dl->AddText(ImVec2(tx, p.y + (h - ts.y) * 0.5f), ui::col::kText, name.c_str());
            if (o.flags & kOrderFullLoad) {
                const char* fl = "FULL LOAD";
                const ImVec2 fs = sf->CalcTextSizeA(sf->FontSize, FLT_MAX, 0.0f, fl);
                const ImVec2 b0(tx + ts.x + 8.0f * s, p.y + (h - fs.y) * 0.5f - 2.0f * s);
                dl->AddRectFilled(b0, ImVec2(b0.x + fs.x + 12.0f * s, b0.y + fs.y + 4.0f * s), ui::withAlpha(ui::col::kTeal, 0.18f), 8.0f * s);
                dl->AddText(sf, sf->FontSize, ImVec2(b0.x + 6.0f * s, b0.y + 2.0f * s), ui::col::kTeal, fl);
            }
            if (mine) {
                const ImVec2 back = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(ImVec2(p.x + avail - buttons + 4.0f * s, p.y + (h - ImGui::GetFrameHeight() * 0.85f) * 0.5f));
                if (o.kind == OrderKind::Station &&
                    ui::miniButton("##full", ui::Icon::People, (o.flags & kOrderFullLoad) ? ui::col::kTeal : ui::col::kTextMuted,
                                   "Wait for a full load")) {
                    o.flags = static_cast<u8>(((o.flags & kOrderFullLoad) ? 0 : kOrderFullLoad) | (o.flags & kOrderPlatform));
                    changed = true;
                }
                if (o.kind != OrderKind::Station) ImGui::Dummy(ImVec2(ImGui::GetFrameHeight() * 0.85f, 1));
                ImGui::SameLine(0, 2.0f * s);
                if (i > 0) {
                    if (ui::miniButton("##up", ui::Icon::ArrowUp, ui::col::kTextMuted, "Move up")) moveUp = static_cast<int>(i);
                } else {
                    ImGui::Dummy(ImVec2(ImGui::GetFrameHeight() * 0.85f, 1));
                }
                ImGui::SameLine(0, 2.0f * s);
                if (ui::miniButton("##rm", ui::Icon::Trash, ui::col::kRed, "Remove")) remove = static_cast<int>(i);
                ImGui::SetCursorScreenPos(back);
            }
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 5.0f * s));
            ImGui::Dummy(ImVec2(0, 0));
            if (o.kind == OrderKind::Station) {
                int mode = (o.flags & kOrderNonStop) ? 4 : (o.flags & kOrderUnloadOnly) ? 2
                    : (o.flags & kOrderNoLoading) ? 3 : (o.flags & kOrderFullLoad) ? 1 : 0;
                ImGui::BeginDisabled(!mine);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##mode", &mode, "Stop normally\0Full load\0Unload all, no loading\0No loading\0Pass through\0")) {
                    constexpr u8 flags[] = {0, kOrderFullLoad, kOrderUnloadOnly, kOrderNoLoading, kOrderNonStop};
                    o.flags = static_cast<u8>(flags[mode] | (o.flags & kOrderPlatform)); changed = true;
                }
                // Trains on free-form track can be sent to one platform in particular.
                if (const auto platforms = v->info().type == VehicleType::Rail ? track::platformsOf(w, o.station)
                                                                              : std::vector<std::vector<TrackEdgeId>>{};
                    platforms.size() > 1 || (o.flags & kOrderPlatform)) {
                    std::string items = "Any free platform";
                    items.push_back('\0');
                    for (usize k = 0; k < platforms.size(); ++k) {
                        items += std::format("Platform {}", k + 1);
                        items.push_back('\0');
                    }
                    int chosen = (o.flags & kOrderPlatform) ? track::platformNumber(w, o.station, o.platform) : 0;
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::Combo("##platform", &chosen, items.c_str())) {
                        if (chosen == 0) {
                            o.flags = static_cast<u8>(o.flags & ~kOrderPlatform);
                            o.platform = {};
                        } else {
                            o.flags = static_cast<u8>(o.flags | kOrderPlatform);
                            o.platform = track::platformPoint(w, platforms[static_cast<usize>(chosen - 1)]);
                        }
                        changed = true;
                    }
                }
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        if (orders.empty()) ui::emptyState(ui::Icon::Station, "No orders yet - add some stations");
        if (remove >= 0) {
            orders.erase(orders.begin() + remove);
            changed = true;
        }
        if (moveUp > 0) {
            std::swap(orders[static_cast<usize>(moveUp)], orders[static_cast<usize>(moveUp - 1)]);
            changed = true;
        }
        if (changed) session_->submit(CmdSetOrders{selVehicle_, orders});
        if (mine) {
            const bool picking = tool_ == Tool::PickOrder;
            ImGui::Checkbox("Via waypoint (road / track / water tile)", &pickViaOrder_);
            if (ui::button(picking ? "Click a platform, station or depot - Esc when done" : "Add stop or depot",
                           picking ? ui::Icon::Target : ui::Icon::Plus, picking ? ui::Btn::Primary : ui::Btn::Secondary,
                           ImVec2(-1.0f, 0.0f)))
                tool_ = picking ? Tool::None : Tool::PickOrder;
        }
    }
    ui::endPanel();
    if (!open) {
        if (followVehicle_ == selVehicle_) followVehicle_ = {};
        selVehicle_ = {};
        if (tool_ == Tool::PickOrder) tool_ = Tool::None;
    }
}

void App::drawStationWindow() {
    World& w = world();
    const Station* st = w.stations.tryGet(selStation_);
    if (!st) {
        selStation_ = {};
        return;
    }
    const float s = uiScale_;
    const Company* c = w.companies.tryGet(st->owner);
    const ImU32 owner = ui::rgb(companyColourRgb(c ? c->colour : 14));
    bool open = true;
    ui::PanelSpec spec;
    spec.pos = viewportPoint(1.0f, 0.0f, -450.0f, 70.0f);
    spec.pivot = ImVec2(1.0f, 0.0f);
    spec.width = 380.0f;
    spec.accent = owner;
    spec.subtitle = c ? c->name.c_str() : nullptr;
    if (ui::beginPanel("###stationPanel", st->name.c_str(), ui::Icon::Station, &open, spec)) {
        bool first = true;
        const auto facility = [&](u8 bit, const char* name, ImU32 colour) {
            if (!(st->facilities & bit)) return;
            if (!first) ImGui::SameLine();
            first = false;
            ui::badge(name, colour);
        };
        facility(kFacilityBus, "Bus", ui::col::kRoad);
        facility(kFacilityTram, "Tram", ui::col::kTram);
        facility(kFacilityRail, "Rail", ui::col::kRail);
        facility(kFacilityDock, "Dock", ui::col::kWater);
        ImGui::Dummy(ImVec2(0, 2.0f * s));
        const float cw = ui::columnWidth(3);
        const std::string waiting = std::format("{}", st->waitingCount());
        const std::string boarded = std::format("{}", st->boardedLastMonth > 0 ? st->boardedLastMonth : st->boardedThisMonth);
        int served = 0;
        for (const auto [id, v] : w.vehicles)
            for (const Order& o : v.orders)
                if (o.kind == OrderKind::Station && o.station == selStation_) {
                    ++served;
                    break;
                }
        const std::string vehicles = std::format("{}", served);
        ui::statCard(ui::Icon::People, waiting.c_str(), "Waiting", ui::col::kTeal, cw);
        ImGui::SameLine();
        ui::statCard(ui::Icon::Check, boarded.c_str(), st->boardedLastMonth > 0 ? "Boarded last month" : "Boarded", ui::col::kGreen, cw);
        ImGui::SameLine();
        ui::statCard(ui::Icon::Bus, vehicles.c_str(), "Vehicles calling", ui::col::kBlue, cw);
        const float rating = static_cast<float>(st->rating) / 255.0f;
        ImGui::Dummy(ImVec2(0, 2.0f * s));
        ui::meterRow("Service rating", std::format("{}%", st->rating * 100 / 255).c_str(), rating, qualityColour(rating));
        ui::statRow("Catchment radius", std::format("{} tiles", st->catchment).c_str());
        ui::statRow("Stop tiles", std::format("{}", st->tiles.size()).c_str());
        ui::statRow("Waiting capacity", std::format("{} / {}", st->waitingCount(), st->waitingCapacity()).c_str());
        ui::sectionHeader("Waiting for");
        if (st->waiting.empty()) ui::emptyState(ui::Icon::People, "Nobody is waiting");
        usize most = 1;
        for (const auto& [to, list] : st->waiting) most = std::max(most, list.size());
        for (const auto& [to, list] : st->waiting) {
            const Station* dest = w.stations.tryGet(to);
            ui::meterRow(dest ? dest->name.c_str() : "?", std::format("{}", list.size()).c_str(),
                         static_cast<float>(list.size()) / static_cast<float>(most), ui::col::kTeal);
        }
        ImGui::Dummy(ImVec2(0, 2.0f * s));
        if (ui::button("Centre view", ui::Icon::Target, ui::Btn::Secondary, ImVec2(-1.0f, 0.0f))) focusOn(st->sign);
    }
    ui::endPanel();
    if (!open) selStation_ = {};
}

void App::drawTownWindow() {
    World& w = world();
    const Town* t = w.towns.tryGet(selTown_);
    if (!t) {
        selTown_ = {};
        return;
    }
    const float s = uiScale_;
    bool open = true;
    const std::string sub = std::format("{}, founded {}", townClassName(t->cls), dateOf(w, t->foundedTick));
    ui::PanelSpec spec;
    spec.pos = viewportPoint(1.0f, 0.0f, -450.0f, 70.0f);
    spec.pivot = ImVec2(1.0f, 0.0f);
    spec.width = 390.0f;
    spec.accent = townColour(t->cls);
    spec.subtitle = sub.c_str();
    if (ui::beginPanel("###townPanel", t->name.c_str(), ui::Icon::Town, &open, spec)) {
        const float cw = ui::columnWidth(3);
        const std::string pop = std::format("{}", t->population);
        const std::string jobs = std::format("{} / {}", t->workers, t->jobs);
        const std::string buildings = std::format("{}", t->buildings.size());
        ui::statCard(ui::Icon::People, pop.c_str(), "Population", ui::col::kTeal, cw);
        ImGui::SameLine();
        ui::statCard(ui::Icon::Briefcase, jobs.c_str(), "Jobs filled", ui::col::kBlue, cw);
        ImGui::SameLine();
        ui::statCard(ui::Icon::Home, buildings.c_str(), "Buildings", ui::col::kAccent, cw);
        ImGui::Dummy(ImVec2(0, 2.0f * s));
        ui::statRow("Unemployed", std::format("{}", t->unemployed.size()).c_str());
        ui::statRow("Shopping capacity", std::format("{}", t->retailCapacity).c_str());
        i64 servedResidents = 0;
        i64 boardings = 0;
        std::vector<StationId> coverage;
        for (const BuildingId bid : t->buildings) {
            const Building* b = w.buildings.tryGet(bid);
            if (!b || !b->residents) continue;
            const TilePos centre = b->centre2();
            stations::stationsCovering(w, {floorDiv(centre.x, 2), floorDiv(centre.y, 2)}, coverage);
            for (const StationId sid : coverage)
                if (const Station* stop = w.stations.tryGet(sid); stop && stop->lastVisitTick >= 0 &&
                    w.clock.tick - stop->lastVisitTick <= 30 * kTicksPerDay) {
                    servedResidents += b->residents;
                    break;
                }
        }
        for (const auto [id, stop] : w.stations) if (stop.town == selTown_) boardings += stop.boardedLastMonth;
        ui::statRow("Boardings last month", std::format("{}", boardings).c_str());
        ui::statRow("Population served", std::format("{}%", t->population ? std::min<i64>(100, servedResidents * 100 / t->population) : 0).c_str());
        if (const Town* shop = w.towns.tryGet(t->shopTown); shop && shop != t) ui::statRow("Residents shop in", shop->name.c_str());

        const TripStats& m = t->lastMonth.trips > 0 ? t->lastMonth : t->thisMonth;
        ui::sectionHeader(t->lastMonth.trips > 0 ? "Trips last month" : "Trips this month");
        if (m.trips > 0) {
            const float total = static_cast<float>(m.trips);
            const float parts[] = {m.walked / total, m.drove / total, m.transit / total, m.lifts / total};
            const ImU32 colours[] = {ui::col::kLeaf, ui::col::kTextMuted, ui::col::kBlue, ui::col::kOrange};
            ui::statRow("Journeys", std::format("{}", m.trips).c_str());
            ui::stackedBar(parts, colours, 12.0f);
            const char* names[] = {"Walked", "Drove", "Transit", "No service"};
            for (int i = 0; i < 4; ++i) {
                if (i) ImGui::SameLine(0, 12.0f * s);
                const std::string label = std::format("{} {:.0f}%", names[i], parts[i] * 100.0f);
                ui::legendItem(colours[i], label.c_str());
            }
        } else {
            ui::emptyState(ui::Icon::Walk, "No journeys recorded yet");
        }

        ui::sectionHeader("Local authority");
        bool any = false;
        for (const auto [cid, c] : w.companies) {
            const i32 r = towns::rating(*t, cid);
            if (!t->ratings.contains(cid.value) && cid != session_->localCompany()) continue;
            any = true;
            const float k = static_cast<float>(r - kRatingMin) / static_cast<float>(kRatingMax - kRatingMin);
            ui::meterRow(c.name.c_str(), std::format("{} ({})", ratingName(r), r).c_str(), k, qualityColour(k));
        }
        if (!any) ui::emptyState(ui::Icon::Star, "No companies rated yet");
        if (t->servedStations > 0) {
            ui::FontScope f(ui::fonts().small);
            ui::mutedText(std::format("{} station{} served here last month", t->servedStations, t->servedStations == 1 ? "" : "s").c_str());
        }
        ImGui::Dummy(ImVec2(0, 2.0f * s));
        if (ui::button("Centre view", ui::Icon::Target, ui::Btn::Secondary, ImVec2(-1.0f, 0.0f))) focusOn(t->centre);
        if (ui::button("Inspect residents", ui::Icon::People, ui::Btn::Secondary, ImVec2(-1.0f, 0.0f))) {
            citizenTown_ = selTown_;
            showCitizens_ = true;
            selPerson_ = {};
        }
    }
    ui::endPanel();
    if (!open) selTown_ = {};
}

void App::drawFinanceWindow() {
    if (!showFinances_) return;
    World& w = world();
    Company* c = w.companies.tryGet(session_->localCompany());
    if (!c) return;
    const float s = uiScale_;
    ui::PanelSpec spec;
    spec.pos = viewportPoint(0.0f, 0.0f, 20.0f, 80.0f);
    spec.width = 420.0f;
    spec.height = 620.0f;
    spec.accent = ui::col::kGreen;
    spec.subtitle = c->name.c_str();
    if (ui::beginPanel("###financePanel", "Finances", ui::Icon::Coins, &showFinances_, spec)) {
        if (w.economy.recessionMonths > 0)
            ImGui::TextWrapped("Recession: %d months remaining. Fares -30%%; town growth halved.", w.economy.recessionMonths);
        ImGui::TextDisabled("Price index %.1f%%  |  Inflation %s", w.economy.priceIndex / 100.0f, w.settings.inflation ? "on" : "off");
        const float cw = ui::columnWidth(2);
        const std::string balance = formatMoney(c->money);
        const std::string loan = formatMoney(c->loan);
        ui::statCard(ui::Icon::Coins, balance.c_str(), "Bank balance", c->money < 0 ? ui::col::kRed : ui::col::kGreen, cw);
        ImGui::SameLine();
        ui::statCard(ui::Icon::Briefcase, loan.c_str(), "Loan", ui::col::kOrange, cw);
        ImGui::Dummy(ImVec2(0, 2.0f * s));
        const float bw = ui::columnWidth(2) / s;
        if (c->bankrupt) {
            ui::iconText(ui::Icon::Warning, ui::col::kRed, "This company is bankrupt");
            ImGui::TextWrapped("Vehicles were liquidated and infrastructure made public. You can continue observing the world or start a new game.");
        } else if (c->quartersInDebt > 0) {
            ImGui::TextWrapped("Negative cash for %d quarter(s). Restore a positive balance before four quarters to avoid bankruptcy.", c->quartersInDebt);
        }
        ImGui::BeginDisabled(c->bankrupt);
        if (actionButton("Borrow $100,000", ui::Icon::Plus, ui::Btn::Secondary, "Increase the loan", bw))
            session_->submit(CmdSetLoan{100'000});
        ImGui::SameLine();
        if (actionButton("Repay $100,000", ui::Icon::Minus, ui::Btn::Secondary, "Pay back part of the loan", bw))
            session_->submit(CmdSetLoan{-100'000});
        ImGui::EndDisabled();

        static int period = 0;
        static constexpr ui::Segment periods[] = {{"This year", ui::Icon::None, nullptr},
                                                  {"This month", ui::Icon::None, nullptr},
                                                  {"Last month", ui::Icon::None, nullptr}};
        if (const int p = ui::segmented("##ledgerPeriod", periods, period); p >= 0) period = p;
        const YearLedger* ledger = &c->thisYear;
        const YearLedger emptyLedger;
        if (period == 1) ledger = &c->thisMonth;
        if (period == 2) ledger = c->monthlyHistory.empty() ? &emptyLedger : &c->monthlyHistory.back();
        ui::sectionHeader(period == 0 ? std::format("{} so far", ledger->year).c_str() : "Monthly income and expenses");
        bool anyEntry = false;
        for (usize i = 0; i < kExpenseCount; ++i) {
            const Money m = ledger->amounts[i];
            if (m == 0) continue;
            anyEntry = true;
            ui::moneyRow(std::string(expenseName(static_cast<Expense>(i))).c_str(), m);
        }
        if (!anyEntry) ui::emptyState(ui::Icon::Chart, "No income or expenses in this period");
        ImGui::Separator();
        {
            ui::FontScope f(ui::fonts().heading);
            ui::moneyRow("Total", ledger->total());
        }
        ui::statRow("Passengers carried", std::format("{}", c->passengersThisYear).c_str());
        if (!c->history.empty()) {
            ui::sectionHeader("Yearly result");
            std::vector<float> values;
            for (const YearLedger& y : c->history) values.push_back(static_cast<float>(y.total()));
            values.push_back(static_cast<float>(c->thisYear.total()));
            ui::sparkline(values, ImVec2(0, 64.0f), ui::col::kGreen);
            const YearLedger& last = c->history.back();
            ui::moneyRow(std::format("{}", last.year).c_str(), last.total());
        }
        if (!c->monthlyHistory.empty()) {
            ui::sectionHeader("Monthly results");
            const usize first = c->monthlyHistory.size() > 12 ? c->monthlyHistory.size() - 12 : 0;
            for (usize i = first; i < c->monthlyHistory.size(); ++i) {
                const MonthLedger& m = c->monthlyHistory[i];
                ui::moneyRow(std::format("{}-{:02}", m.year, m.month).c_str(), m.total());
            }
        }
        if (ImGui::CollapsingHeader("Company details")) {
            static std::string source;
            static char name[65] = {};
            static int colour = 0;
            const std::string identity = c->name + std::to_string(c->colour);
            if (source != identity) {
                std::copy(c->name.begin(), c->name.end(), name); name[c->name.size()] = '\0';
                colour = c->colour; source = identity;
            }
            ImGui::BeginDisabled(c->bankrupt);
            ImGui::InputText("Name", name, sizeof(name));
            if (ImGui::BeginCombo("Livery", companyColourName(static_cast<u8>(colour)).data())) {
                for (int i = 0; i < static_cast<int>(kCompanyColourCount); ++i)
                    if (ImGui::Selectable(companyColourName(static_cast<u8>(i)).data(), i == colour)) colour = i;
                ImGui::EndCombo();
            }
            if (ui::button("Apply company details", ui::Icon::Check, ui::Btn::Primary))
                session_->submit(CmdUpdateCompany{name, static_cast<u8>(colour)});
            ImGui::EndDisabled();
        }
    }
    ui::endPanel();
}

void App::drawVehicleList() {
    if (!showVehicleList_) return;
    World& w = world();
    const float s = uiScale_;
    static int filter = 0; // 0 = all, else VehicleType + 1
    static int status = 0;
    if (fleetCompany_ == -2) fleetCompany_ = session_->localCompany().valid() ? session_->localCompany().value : -1;
    const auto matches = [&](const Vehicle& v) {
        return (fleetCompany_ < 0 || v.owner.value == fleetCompany_) &&
               (filter == 0 || static_cast<int>(v.info().type) + 1 == filter) &&
               (status == 0 || (status == 1 && !v.stopped && v.state != VehicleState::InDepot) ||
                (status == 2 && (v.stopped || v.state == VehicleState::InDepot)) || (status == 3 && v.lost));
    };
    int count = 0;
    Money profit = 0;
    for (const auto [id, v] : w.vehicles)
        if (matches(v)) {
            ++count;
            profit += v.profitThisYear;
        }
    const std::string sub = std::format("{} vehicles, {} profit this year", count, formatMoney(profit));
    ui::PanelSpec spec;
    spec.pos = viewportPoint(0.5f, 0.0f, -290.0f, 80.0f);
    spec.width = 580.0f;
    spec.height = 380.0f;
    spec.accent = ui::col::kBlue;
    spec.subtitle = sub.c_str();
    if (ui::beginPanel("###vehicleListPanel", "Vehicles", ui::Icon::Bus, &showVehicleList_, spec)) {
        const Company* selected = fleetCompany_ >= 0 ? w.companies.tryGet(CompanyId{static_cast<u8>(fleetCompany_)}) : nullptr;
        if (ImGui::BeginCombo("Company", selected ? selected->name.c_str() : "All companies")) {
            if (ImGui::Selectable("All companies", fleetCompany_ < 0)) fleetCompany_ = -1;
            for (const auto [id, co] : w.companies)
                if (ImGui::Selectable(co.name.c_str(), fleetCompany_ == id.value)) fleetCompany_ = id.value;
            ImGui::EndCombo();
        }
        const char* statuses[] = {"All vehicles", "Operating", "Stopped / depot", "Lost"};
        ImGui::Combo("Status", &status, statuses, 4);
        static constexpr ui::Segment kFilters[] = {{"All", ui::Icon::None, nullptr},
                                                   {"Buses", ui::Icon::Bus, nullptr},
                                                   {"Trams", ui::Icon::Tram, nullptr},
                                                   {"Trains", ui::Icon::Train, nullptr},
                                                   {"Ships", ui::Icon::Ship, nullptr}};
        if (const int clicked = ui::segmented("##filter", kFilters, filter); clicked >= 0) filter = clicked;
        ImGui::Dummy(ImVec2(0, 2.0f * s));
        if (ImGui::BeginTable("vehicles", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit |
                                                 ImGuiTableFlags_PadOuterX)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Vehicle", 0, 120.0f * s);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("This year", 0, 96.0f * s);
            ImGui::TableSetupColumn("Last year", 0, 96.0f * s);
            ImGui::TableHeadersRow();
            for (const auto [id, v] : w.vehicles) {
                if (!matches(v)) continue;
                ImGui::TableNextRow(0, 26.0f * s);
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(id.value));
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const std::string label = std::format("      {}", vehicleName(v));
                if (ImGui::Selectable(label.c_str(), selVehicle_ == id, ImGuiSelectableFlags_SpanAllColumns)) selVehicle_ = id;
                ui::drawIcon(ImGui::GetWindowDrawList(), vehicleIcon(v.info().type),
                             ImVec2(p.x + 9.0f * s, p.y + ImGui::GetTextLineHeight() * 0.5f), 15.0f * s, typeColour(v.info().type));
                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(statusColour(v)));
                ImGui::TextUnformatted(vehicleStatus(v).c_str());
                ImGui::PopStyleColor();
                const auto money = [](Money m) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(m < 0 ? ui::col::kRed : (m > 0 ? ui::col::kGreen : ui::col::kTextMuted)));
                    ImGui::TextUnformatted(formatMoney(m).c_str());
                    ImGui::PopStyleColor();
                };
                ImGui::TableNextColumn();
                money(v.profitThisYear);
                ImGui::TableNextColumn();
                money(v.profitLastYear);
            }
            ImGui::EndTable();
        }
        if (count == 0) ui::emptyState(ui::Icon::Bus, "No vehicles match these filters");
    }
    ui::endPanel();
}

} // namespace tp
