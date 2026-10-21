// Industry windows and town needs: what an industry makes from what, how much of it reaches a
// station, and what each town needs delivered to keep growing.

#include "client/app.hpp"
#include "client/ui/widgets.hpp"

#include <imgui.h>

#include "sim/industries.hpp"

#include <algorithm>
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

} // namespace

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
    spec.accent = raw ? ui::col::kLeaf : ui::col::kOrange;
    spec.subtitle = sub.c_str();
    if (ui::beginPanel("###industryPanel", b->info().name.data(), ui::Icon::Briefcase, &open, spec)) {
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
            ui::FontScope f(ui::fonts().small);
            ui::mutedText(recipe->inputs[1] != CargoType::None ? "It works only while it has both." : "Deliver it from a producer nearby.");
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
