// Main menu, in-game menu, connection screen, chat and autosave.

#include "client/app.hpp"
#include "client/terrain_options.hpp"
#include "client/ui/widgets.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>

#include "core/calendar.hpp"
#include "core/log.hpp"
#include "core/version.hpp"
#include "net/host_session.hpp"
#include "net/net_client.hpp"
#include "render/picking.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>

namespace tp {

namespace {

constexpr const char* kSaveDir = "saves";
constexpr int kAutosaveSlots = 5; // autosave-1 to autosave-5, the oldest overwritten next

// How long ago a save was written, for the load menu.
std::string savedAgo(i64 fileTime) {
    const i64 now = std::chrono::duration_cast<std::chrono::seconds>(std::filesystem::file_time_type::clock::now().time_since_epoch()).count();
    const i64 minutes = std::max<i64>(0, now - fileTime) / 60;
    if (minutes < 1) return "just now";
    if (minutes < 60) return std::format("{} min ago", minutes);
    if (minutes < 48 * 60) return std::format("{} h ago", minutes / 60);
    return std::format("{} days ago", minutes / (24 * 60));
}

u64 randomSeed() {
    return static_cast<u64>(std::chrono::system_clock::now().time_since_epoch().count() / 1000) % 1'000'000'000ULL;
}

ImVec2 screenCentre() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    return ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f);
}

// Darkens the whole screen behind a modal panel.
void dimBackground(float alpha) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::GetBackgroundDrawList()->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                                                  IM_COL32(8, 10, 14, static_cast<int>(alpha * 255.0f)));
}

// Full-width text input with a caption above it.
bool field(const char* caption, const char* id, char* buf, size_t size, const char* hint = nullptr) {
    ui::fieldLabel(caption);
    ImGui::SetNextItemWidth(-1.0f);
    return hint ? ImGui::InputTextWithHint(id, hint, buf, size) : ImGui::InputText(id, buf, size);
}

void portField(int& port) {
    ui::fieldLabel("Port");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputInt("##port", &port, 0, 0);
    port = std::clamp(port, 1, 65535);
}

// Large entry of the main menu: an icon, a title and a description.
bool menuItem(const char* id, ui::Icon icon, const char* title, const char* desc, bool selected) {
    const float s = ui::scale();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, 62.0f * s);
    const bool pressed = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const float h = ui::animate(ImGui::GetItemID(), hovered || selected ? 1.0f : 0.0f, 14.0f);
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(mn, mx, ui::withAlpha(ui::col::kCard, 0.35f + 0.6f * h), 12.0f * s);
    if (h > 0.01f)
        dl->AddRectFilled(ImVec2(mn.x, mn.y + 14.0f * s), ImVec2(mn.x + 3.0f * s, mx.y - 14.0f * s),
                          ui::withAlpha(ui::col::kAccent, h), 2.0f * s);
    const ImVec2 box(mn.x + 16.0f * s, mn.y + 11.0f * s);
    dl->AddRectFilled(box, ImVec2(box.x + 40.0f * s, box.y + 40.0f * s), ui::withAlpha(ui::col::kAccent, 0.10f + 0.12f * h),
                      10.0f * s);
    ui::drawIcon(dl, icon, ImVec2(box.x + 20.0f * s, box.y + 20.0f * s), 20.0f * s,
                 ui::mix(ui::col::kTextMuted, ui::col::kAccent, 0.4f + 0.6f * h));
    ImFont* hf = ui::fonts().heading;
    ImFont* sf = ui::fonts().small;
    const float tx = box.x + 54.0f * s + h * 4.0f * s;
    dl->AddText(hf, hf->FontSize, ImVec2(tx, mn.y + 10.0f * s), ui::col::kText, title);
    dl->AddText(sf, sf->FontSize, ImVec2(tx, mn.y + 14.0f * s + hf->FontSize), ui::col::kTextMuted, desc);
    return pressed;
}

// Terrain choices for a new world: a picture of the land around the start, the kind of land,
// and how many mountains, lakes and rivers.
void terrainOptions(ClientPreferences& prefs, gfx::TerrainPreview& preview, u64 seed, float s) {
    ui::sectionHeader("Terrain");
    preview.update(seed, terrainSettings(prefs));
    if (preview.ready()) {
        const float size = std::min(ImGui::GetContentRegionAvail().x, 190.0f * s);
        const ImVec2 at(ImGui::GetCursorScreenPos().x + (ImGui::GetContentRegionAvail().x - size) * 0.5f, ImGui::GetCursorScreenPos().y);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddImageRounded(static_cast<ImTextureID>(preview.textureId()), at, ImVec2(at.x + size, at.y + size), ImVec2(0, 0),
                            ImVec2(1, 1), IM_COL32_WHITE, 10.0f * s);
        dl->AddCircle(ImVec2(at.x + size * 0.5f, at.y + size * 0.5f), 4.0f * s, ui::col::kAccent, 0, 2.0f * s);
        const std::string water = std::format("{}% water", preview.waterPercent());
        ImFont* sf = ui::fonts().small;
        const ImVec2 ts = sf->CalcTextSizeA(sf->FontSize, FLT_MAX, 0.0f, water.c_str());
        dl->AddRectFilled(ImVec2(at.x + 6.0f * s, at.y + size - ts.y - 10.0f * s), ImVec2(at.x + ts.x + 18.0f * s, at.y + size - 6.0f * s),
                          ui::withAlpha(ui::col::kCard, 0.85f), 6.0f * s);
        dl->AddText(sf, sf->FontSize, ImVec2(at.x + 12.0f * s, at.y + size - ts.y - 8.0f * s), ui::col::kText, water.c_str());
        ImGui::Dummy(ImVec2(0, size + 6.0f * s));
    }
    ui::fieldLabel("Land");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##landmass", landmassInfo(static_cast<Landmass>(prefs.landmass)).name)) {
        for (int i = 0; i < kLandmassCount; ++i) {
            const LandmassInfo& info = landmassInfo(static_cast<Landmass>(i));
            if (ImGui::Selectable(info.name, prefs.landmass == i)) prefs.landmass = i;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", info.description);
        }
        ImGui::EndCombo();
    }
    {
        ui::FontScope f(ui::fonts().small);
        ui::mutedText(landmassInfo(static_cast<Landmass>(prefs.landmass)).description);
    }
    ui::fieldLabel("Climate");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##climate", climateInfo(static_cast<Climate>(prefs.climate)).name)) {
        for (int i = 0; i < kClimateCount; ++i) {
            const LandmassInfo& info = climateInfo(static_cast<Climate>(i));
            if (ImGui::Selectable(info.name, prefs.climate == i)) prefs.climate = i;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", info.description);
        }
        ImGui::EndCombo();
    }
    {
        ui::FontScope f(ui::fonts().small);
        ui::mutedText(climateInfo(static_cast<Climate>(prefs.climate)).description);
    }
    const auto amounts = [&](const char* label, const char* id, int& value, int count, const char* (*name)(int)) {
        ui::fieldLabel(label);
        std::array<ui::Segment, kTerrainAmountMax + 1> items{};
        for (int i = 0; i < count; ++i) items[static_cast<usize>(i)].label = name(i);
        if (const int clicked = ui::segmented(id, std::span(items.data(), static_cast<usize>(count)), value); clicked >= 0)
            value = clicked;
    };
    amounts("Mountains", "##mountains", prefs.mountains, kTerrainAmountMax + 1, mountainAmountName);
    amounts("Lakes", "##lakes", prefs.lakes, kTerrainAmountMax + 1, lakeAmountName);
    amounts("Rivers", "##rivers", prefs.rivers, kRiverAmountMax + 1, riverAmountName);
}

std::unique_ptr<World> newWorld(u64 seed) {
    WorldSettings s;
    s.seed = seed;
    return std::make_unique<World>(s);
}

} // namespace

void App::placeCameraAtStart() {
    World& w = world();
    // Start near the player's first town if possible, otherwise near the origin on dry land.
    TilePos start{0, 0};
    const TerrainGenerator& gen = w.map.generator();
    for (i32 r = 0; r < 4000; r += 8) {
        bool found = false;
        for (i32 i = 0; i < 16 && !found; ++i) {
            const double a = i * 6.2831853 / 16.0;
            const TilePos p{static_cast<i32>(std::cos(a) * r), static_cast<i32>(std::sin(a) * r)};
            const int h = gen.cornerHeight(p.x, p.y);
            if (h >= 2 && h <= 8) {
                start = p;
                found = true;
            }
        }
        if (found) break;
    }
    camera_.targetGoal = glm::dvec3(start.x + 0.5, gfx::terrainHeightAt(w.map, start.x + 0.5, start.y + 0.5),
                                    start.y + 0.5);
    camera_.distanceGoal = 70.0;
    camera_.snapToGoals();
    cameraPlaced_ = true;
}

void App::startSession(std::unique_ptr<GameSession> session) {
    returnToMenuRequested_ = false;
    session_ = std::move(session);
    renderer_.invalidateAll();
    explorePending_.clear();
    selDepot_ = {};
    selVehicle_ = {};
    selStation_ = {};
    selTown_ = {};
    followVehicle_ = {};
    selPerson_ = {};
    followPerson_ = false;
    citizenTown_ = {};
    fleetCompany_ = -2;
    tool_ = Tool::None;
    showGameMenu_ = false;
    cameraPlaced_ = false;
    autosaveClock_ = 0.0;
    autosaveTick_ = -1;
    chatLines_.clear();
    showClients_ = false;
    showChat_ = false;
    screen_ = Screen::Playing;
    if (session_->ready()) placeCameraAtStart();
}

void App::returnToMainMenu() {
    // UI is drawn before news, minimap and the world. Keep the session alive until
    // this frame finishes so those consumers cannot access a destroyed world.
    returnToMenuRequested_ = true;
}

void App::applyScreenTransition() {
    if (!returnToMenuRequested_) return;
    returnToMenuRequested_ = false;
    session_.reset();
    screen_ = Screen::MainMenu;
    menuPage_ = MenuPage::Main;
    renderer_.invalidateAll();
    showGameMenu_ = false;
    newsSession_ = nullptr;
    newsBanner_.reset();
    toasts_.clear();
    nextAudioUpdate_ = 0;
}

void App::updatePreview(double dt) {
    if (!preview_) {
        auto session = std::make_unique<LocalSession>(newWorld(20261017), CompanyId{});
        World& w = session->world();
        for (i32 y = -5; y <= 5; ++y)
            for (i32 x = -5; x <= 5; ++x) w.activateChunk({x, y});
        const Town* best = nullptr;
        for (const auto [id, t] : w.towns)
            if (!best || t.population > best->population) best = &t;
        const TilePos c = best ? best->centre : TilePos{0, 0};
        camera_.targetGoal = glm::dvec3(c.x + 0.5, gfx::terrainHeightAt(w.map, c.x + 0.5, c.y + 0.5), c.y + 0.5);
        camera_.distanceGoal = 55.0;
        camera_.pitchGoal = 0.62;
        camera_.snapToGoals();
        renderer_.invalidateAll();
        preview_ = std::move(session);
    }
    preview_->update(dt);
    camera_.yawGoal += dt * 0.05;
    camera_.update(dt);
}

void App::drawMainMenu() {
    const float s = uiScale_;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // Vignette over the slowly orbiting preview world: dark on the left behind the menu.
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    const ImVec2 p0 = vp->Pos, p1(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
    const float split = p0.x + vp->Size.x * 0.62f;
    bg->AddRectFilledMultiColor(p0, ImVec2(split, p1.y), IM_COL32(10, 12, 17, 225), IM_COL32(10, 12, 17, 0),
                                IM_COL32(10, 12, 17, 0), IM_COL32(10, 12, 17, 225));
    bg->AddRectFilledMultiColor(ImVec2(p0.x, p1.y - vp->Size.y * 0.3f), p1, IM_COL32(10, 12, 17, 0), IM_COL32(10, 12, 17, 0),
                                IM_COL32(10, 12, 17, 170), IM_COL32(10, 12, 17, 170));

    // --- Title and menu column -------------------------------------------------------------
    const float colW = 400.0f * s;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 72.0f * s, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.0f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(colW, 0.0f), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
    ImGui::Begin("##mainmenu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor(2);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImFont* tf = ui::fonts().title;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 a = tf->CalcTextSizeA(tf->FontSize, FLT_MAX, 0.0f, "transport");
        dl->AddText(tf, tf->FontSize, at, ui::col::kText, "transport");
        dl->AddText(tf, tf->FontSize, ImVec2(at.x + a.x, at.y), ui::col::kAccent, "-poly");
        ImGui::Dummy(ImVec2(colW - 32.0f * s, a.y));
        // A little low poly train rides under the title.
        const float t = static_cast<float>(ImGui::GetTime());
        const float trackY = at.y + a.y + 22.0f * s;
        dl->AddLine(ImVec2(at.x, trackY), ImVec2(at.x + colW - 32.0f * s, trackY), ui::col::kBorderStrong, 2.0f * s);
        const float x = at.x + std::fmod(t * 60.0f * s, colW + 60.0f * s) - 60.0f * s;
        dl->PushClipRect(ImVec2(at.x, trackY - 30.0f * s), ImVec2(at.x + colW - 32.0f * s, trackY + 4.0f * s), true);
        for (int car = 0; car < 3; ++car) {
            const float cx = x - car * 30.0f * s;
            dl->AddRectFilled(ImVec2(cx, trackY - 14.0f * s), ImVec2(cx + 26.0f * s, trackY - 3.0f * s),
                              car == 0 ? ui::col::kAccent : ui::col::kFrameActive, 3.0f * s);
            dl->AddCircleFilled(ImVec2(cx + 6.0f * s, trackY - 2.0f * s), 2.5f * s, ui::col::kTextMuted);
            dl->AddCircleFilled(ImVec2(cx + 20.0f * s, trackY - 2.0f * s), 2.5f * s, ui::col::kTextMuted);
        }
        dl->PopClipRect();
        ImGui::Dummy(ImVec2(0.0f, 30.0f * s));
        ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(ui::col::kTextMuted));
        ImGui::PushTextWrapPos(colW - 40.0f * s);
        ImGui::TextUnformatted("Build bus, tram, rail and ferry networks across an endless low poly world, "
                               "and get its citizens to work and to the shops.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0f, 18.0f * s));
    }
    const auto open = [&](MenuPage page) {
        menuPage_ = menuPage_ == page ? MenuPage::Main : page;
        menuError_.clear();
        if (page == MenuPage::NewGame || page == MenuPage::Host)
            std::snprintf(seedBuf_, sizeof(seedBuf_), "%llu", static_cast<unsigned long long>(randomSeed()));
        if (page == MenuPage::Load) saves_ = listSaves(kSaveDir);
    };
    if (menuItem("##new", ui::Icon::Spark, "New game", "Found a company in a brand new world", menuPage_ == MenuPage::NewGame))
        open(MenuPage::NewGame);
    if (menuItem("##load", ui::Icon::Folder, "Load game", "Carry on where you left off", menuPage_ == MenuPage::Load))
        open(MenuPage::Load);
    if (menuItem("##host", ui::Icon::Server, "Host a game", "Start a multiplayer world others can join",
                 menuPage_ == MenuPage::Host))
        open(MenuPage::Host);
    if (menuItem("##join", ui::Icon::Globe, "Join a game", "Connect to a friend's server", menuPage_ == MenuPage::Join))
        open(MenuPage::Join);
    if (menuItem("##settings", ui::Icon::Wrench, "Settings", "Graphics, controls and gameplay preferences", showSettings_))
        showSettings_ = !showSettings_;
    if (menuItem("##quit", ui::Icon::Power, "Quit", "Back to the desktop", false)) glfwSetWindowShouldClose(window_, 1);
    ImGui::End();

    // Version footer.
    {
        ImFont* f = ui::fonts().small;
        const std::string text = std::format("transport-poly {}   -   F12 saves a screenshot", kVersionString);
        bg->AddText(f, f->FontSize, ImVec2(vp->WorkPos.x + 72.0f * s, vp->WorkPos.y + vp->WorkSize.y - 34.0f * s),
                    ui::col::kTextDim, text.c_str());
    }

    // --- Page for the selected entry ---------------------------------------------------------
    if (menuPage_ == MenuPage::Main) return;
    if ((menuPage_ == MenuPage::NewGame || menuPage_ == MenuPage::Host) && seedBuf_[0] == 0)
        std::snprintf(seedBuf_, sizeof(seedBuf_), "%llu", static_cast<unsigned long long>(randomSeed()));
    ui::PanelSpec spec;
    spec.pos = ImVec2(vp->WorkPos.x + 72.0f * s + colW + 36.0f * s, vp->WorkPos.y + vp->WorkSize.y * 0.5f);
    spec.pivot = ImVec2(0.0f, 0.5f);
    spec.cond = ImGuiCond_Always;
    spec.width = 400.0f;
    spec.flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    const char* title = "";
    ui::Icon icon = ui::Icon::Spark;
    switch (menuPage_) {
    case MenuPage::NewGame:
        title = "New game";
        spec.subtitle = "Every seed makes a different world";
        break;
    case MenuPage::Load:
        title = "Load game";
        icon = ui::Icon::Folder;
        spec.subtitle = "Saved games";
        break;
    case MenuPage::Host:
        title = "Host a game";
        icon = ui::Icon::Server;
        spec.subtitle = "Friends join with your address and port";
        spec.accent = ui::col::kBlue;
        break;
    case MenuPage::Join:
        title = "Join a game";
        icon = ui::Icon::Globe;
        spec.subtitle = "Connect to a running server";
        spec.accent = ui::col::kBlue;
        break;
    case MenuPage::Main: break;
    }
    bool pageOpen = true;
    if (ui::beginPanel("##menupage", title, icon, &pageOpen, spec)) {
        if (!menuError_.empty()) {
            ui::iconText(ui::Icon::Warning, ui::col::kRed, menuError_.c_str(), ui::col::kRed);
            ImGui::Dummy(ImVec2(0, 2.0f * s));
        }
        switch (menuPage_) {
        case MenuPage::NewGame:
            ui::fieldLabel("World seed");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 8.0f * s);
            ImGui::InputText("##seed", seedBuf_, sizeof(seedBuf_), ImGuiInputTextFlags_CharsDecimal);
            ImGui::SameLine(0, 8.0f * s);
            if (ui::miniButton("##random", ui::Icon::Spark, ui::col::kAccent, "Random seed"))
                std::snprintf(seedBuf_, sizeof(seedBuf_), "%llu", static_cast<unsigned long long>(randomSeed()));
            field("Company name", "##company", companyName_, sizeof(companyName_), "Leave empty for a generated name");
            ui::fieldLabel("Company colour");
            colourPicker("##newcolour", companyColour_, CompanyId{});
            ImGui::SliderInt("Starting year", &preferences_.startYear, 1900, 2100);
            ImGui::SliderInt("Loan interest", &preferences_.interestPercent, 0, 20, "%d%%");
            competitionPicker();
            terrainOptions(preferences_, terrainPreview_, std::strtoull(seedBuf_, nullptr, 10), s);
            ImGui::Dummy(ImVec2(0, 6.0f * s));
            if (ui::button("Start", ui::Icon::Play, ui::Btn::Primary, ImVec2(-1.0f, 40.0f))) {
                WorldSettings ws;
                ws.seed = std::strtoull(seedBuf_, nullptr, 10);
                ws.terrain = terrainSettings(preferences_);
                ws.startYear = preferences_.startYear;
                ws.interestPercent = preferences_.interestPercent;
                ws.inflation = preferences_.inflation;
                ws.recessions = preferences_.recessions;
                ws.aiCompetitors = preferences_.aiCompetitors;
                applyCompetition(ws);
                startSession(std::make_unique<LocalSession>(ws, companyName_, companyColour_ < 0 ? 0xFF : static_cast<u8>(companyColour_)));
            }
            break;
        case MenuPage::Load:
            if (saves_.empty()) ui::emptyState(ui::Icon::Folder, "No saved games yet");
            for (const SaveInfo& info : saves_) {
                ImGui::PushID(info.path.c_str());
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float avail = ImGui::GetContentRegionAvail().x;
                const float h = 52.0f * s;
                const bool clicked = ImGui::InvisibleButton("##save", ImVec2(avail, h));
                const bool hovered = ImGui::IsItemHovered();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(p, ImVec2(p.x + avail, p.y + h), hovered ? ui::col::kCardHover : ui::col::kCard, 10.0f * s);
                ui::drawIcon(dl, isAutosave(info.path) ? ui::Icon::Clock : ui::Icon::Save, ImVec2(p.x + 22.0f * s, p.y + h * 0.5f),
                             18.0f * s, hovered ? ui::col::kAccent : ui::col::kTextMuted);
                const std::string name = std::filesystem::path(info.path).stem().string();
                const std::string detail =
                    std::format("{}  -  {}  -  saved {}", info.companyName, formatDate(info.day), savedAgo(info.fileTime));
                dl->AddText(ImVec2(p.x + 44.0f * s, p.y + 8.0f * s), ui::col::kText, name.c_str());
                ImFont* sf = ui::fonts().small;
                dl->AddText(sf, sf->FontSize, ImVec2(p.x + 44.0f * s, p.y + 29.0f * s), ui::col::kTextMuted, detail.c_str());
                if (clicked) {
                    std::string err;
                    CompanyId company;
                    auto loaded = loadGame(info.path, company, err);
                    if (loaded) startSession(std::make_unique<LocalSession>(std::move(loaded), company));
                    else menuError_ = "Could not load: " + err;
                }
                ImGui::PopID();
            }
            break;
        case MenuPage::Host:
            field("World seed", "##seed", seedBuf_, sizeof(seedBuf_));
            field("Your name", "##hostname", playerName_, sizeof(playerName_));
            field("Company name", "##company", companyName_, sizeof(companyName_), "Leave empty for a generated name");
            ui::fieldLabel("Company colour");
            colourPicker("##hostcolour", companyColour_, CompanyId{});
            portField(port_);
            field("Server password", "##hostpw", serverPassword_, sizeof(serverPassword_), "Optional: players must give it to join");
            competitionPicker();
            terrainOptions(preferences_, terrainPreview_, std::strtoull(seedBuf_, nullptr, 10), s);
            ImGui::Dummy(ImVec2(0, 6.0f * s));
            if (ui::button("Host new world", ui::Icon::Server, ui::Btn::Primary, ImVec2(-1.0f, 40.0f))) {
                WorldSettings ws;
                ws.seed = std::strtoull(seedBuf_, nullptr, 10);
                ws.terrain = terrainSettings(preferences_);
                ws.startYear = preferences_.startYear;
                ws.interestPercent = preferences_.interestPercent;
                ws.inflation = preferences_.inflation;
                ws.recessions = preferences_.recessions;
                ws.aiCompetitors = preferences_.aiCompetitors;
                applyCompetition(ws);
                auto hosted = std::make_unique<World>(ws);
                Command create;
                create.client = kLocalClientId;
                create.payload = CmdCreateCompany{companyName_, companyColour_ < 0 ? u8{0xFF} : static_cast<u8>(companyColour_)};
                const CommandResult r = executeCommand(*hosted, create);
                net::NetServer::Options opt;
                opt.port = static_cast<u16>(port_);
                opt.password = serverPassword_;
                auto host = std::make_unique<net::HostSession>(std::move(hosted), CompanyId(static_cast<u8>(r.createdId)), opt);
                host->net().setLocalHost(CompanyId(static_cast<u8>(r.createdId)), playerName_[0] ? playerName_ : "Host");
                std::string err;
                if (host->start(err)) startSession(std::move(host));
                else menuError_ = err;
            }
            break;
        case MenuPage::Join:
            field("Server address", "##address", address_, sizeof(address_));
            portField(port_);
            field("Your name", "##name", playerName_, sizeof(playerName_));
            field("Server password", "##joinpw", serverPassword_, sizeof(serverPassword_), "Only if the server has one");
            {
                ui::FontScope f(ui::fonts().small);
                ui::mutedText("Once the world has loaded you choose a company: start your own,");
                ui::mutedText("join a friend's to play together, or watch.");
            }
            ImGui::Dummy(ImVec2(0, 6.0f * s));
            if (ui::button("Join", ui::Icon::Globe, ui::Btn::Primary, ImVec2(-1.0f, 40.0f))) {
                menuError_.clear();
                startSession(std::make_unique<net::NetClientSession>(address_, static_cast<u16>(port_), playerName_, net::kJoinAsk,
                                                                     serverPassword_));
            }
            break;
        case MenuPage::Main: break;
        }
    }
    ui::endPanel();
    if (!pageOpen) menuPage_ = MenuPage::Main;
}

void App::drawConnecting() {
    auto* client = dynamic_cast<net::NetClientSession*>(session_.get());
    const float s = uiScale_;
    ui::PanelSpec spec;
    spec.pos = screenCentre();
    spec.pivot = ImVec2(0.5f, 0.5f);
    spec.cond = ImGuiCond_Always;
    spec.width = 400.0f;
    spec.accent = ui::col::kBlue;
    spec.flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    const bool failed = client && client->phase() == net::NetClientSession::Phase::Failed;
    bool back = false;
    if (ui::beginPanel("##connecting", failed ? "Connection failed" : "Connecting", failed ? ui::Icon::Warning : ui::Icon::Globe,
                       nullptr, spec)) {
        if (!failed) {
            ui::spinner(12.0f, ui::col::kBlue);
            ImGui::SameLine(0, 12.0f * s);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f * s);
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(session_->status().c_str());
        ImGui::PopTextWrapPos();
        if (client && client->phase() == net::NetClientSession::Phase::Downloading) {
            ImGui::Dummy(ImVec2(0, 2.0f * s));
            ui::meterRow("Downloading the world", std::format("{:.0f}%", client->downloadProgress() * 100.0f).c_str(),
                         client->downloadProgress(), ui::col::kBlue);
        }
        ImGui::Dummy(ImVec2(0, 4.0f * s));
        if (failed) {
            if (ui::button("Back to the main menu", ui::Icon::None, ui::Btn::Primary, ImVec2(-1.0f, 0.0f))) {
                menuError_ = client->error();
                back = true;
            }
        } else if (ui::button("Cancel", ui::Icon::Close, ui::Btn::Secondary, ImVec2(-1.0f, 0.0f))) {
            back = true;
        }
    }
    ui::endPanel();
    if (back) returnToMainMenu();
}

void App::drawGameMenu() {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !escConsumed_ && !ImGui::GetIO().WantTextInput) {
        showGameMenu_ = !showGameMenu_;
        escConsumed_ = true;
    }
    if (!showGameMenu_) return;
    const float s = uiScale_;
    dimBackground(0.45f);
    ui::PanelSpec spec;
    spec.pos = screenCentre();
    spec.pivot = ImVec2(0.5f, 0.5f);
    spec.cond = ImGuiCond_Always;
    spec.width = 360.0f;
    spec.flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    const std::string sub = formatDate(world().clock.day());
    spec.subtitle = sub.c_str();
    bool leave = false;
    if (ui::beginPanel("##gamemenu", "Game menu", ui::Icon::Menu, &showGameMenu_, spec)) {
        const bool canSave = !dynamic_cast<net::NetClientSession*>(session_.get());
        if (canSave) {
            field("Save as", "##savename", saveName_, sizeof(saveName_));
            if (ui::button("Save game", ui::Icon::Save, ui::Btn::Primary, ImVec2(-1.0f, 0.0f))) {
                std::string err;
                const std::string path = std::format("{}/{}.tpsave", kSaveDir, saveName_);
                if (saveGame(world(), session_->localCompany(), path, err))
                    toasts_.push_back({"Saved " + path, glfwGetTime() + 3.0, false});
                else
                    toasts_.push_back({"Save failed: " + err, glfwGetTime() + 5.0, true});
                showGameMenu_ = false;
            }
        } else {
            ui::iconText(ui::Icon::Info, ui::col::kBlue, "Only the host can save a multiplayer game.", ui::col::kTextMuted);
        }
        ImGui::Dummy(ImVec2(0, 4.0f * s));
        if (ui::button("Resume", ui::Icon::Play, ui::Btn::Secondary, ImVec2(-1.0f, 0.0f))) showGameMenu_ = false;
        if (ui::button("Settings", ui::Icon::Wrench, ui::Btn::Secondary, ImVec2(-1.0f, 0.0f))) {
            showSettings_ = true;
            showGameMenu_ = false;
        }
        if (ui::button("Exit to main menu", ui::Icon::Folder, ui::Btn::Secondary, ImVec2(-1.0f, 0.0f))) leave = true;
        if (ui::button("Quit game", ui::Icon::Power, ui::Btn::Danger, ImVec2(-1.0f, 0.0f))) glfwSetWindowShouldClose(window_, 1);
    }
    ui::endPanel();
    if (leave) returnToMainMenu();
}

void App::drawChat() {
    if (!session_ || !session_->isMultiplayer()) return;
    auto* client = dynamic_cast<net::NetClientSession*>(session_.get());
    auto* host = dynamic_cast<net::HostSession*>(session_.get());
    if (!ImGui::GetIO().WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Tab)) showClients_ = !showClients_;
    drawPlayersWindow();
    drawCompanyLobby();
    std::vector<net::ChatBroadcastMsg> incoming;
    if (client) incoming = client->takeChat();
    if (host) {
        incoming = host->net().takeChat();
        for (const std::string& e : host->net().takeEvents()) chatLines_.push_back("* " + e);
    }
    const std::vector<net::ClientInfo> everyone = players();
    const auto nameOf = [&](u16 id) {
        const auto it = std::ranges::find_if(everyone, [&](const net::ClientInfo& p) { return p.id == id; });
        return it == everyone.end() ? std::string("?") : it->name;
    };
    for (const auto& m : incoming) {
        if (m.scope == net::ChatScope::Company) chatLines_.push_back(std::format("[Company] <{}> {}", m.from, m.text));
        else if (m.scope == net::ChatScope::Private)
            chatLines_.push_back(m.fromClient == localClientId() ? std::format("[To {}] {}", nameOf(chatTarget_), m.text)
                                                                 : std::format("[From {}] {}", m.from, m.text));
        else chatLines_.push_back(std::format("<{}> {}", m.from, m.text));
    }
    while (chatLines_.size() > 50) chatLines_.pop_front();
    if (!ImGui::GetIO().WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Enter)) showChat_ = true;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float s = uiScale_;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 14.0f * s, vp->WorkPos.y + vp->WorkSize.y * 0.62f), ImGuiCond_Always,
                            ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(400.0f * s, 0), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::vec(ui::withAlpha(ui::col::kBar, showChat_ ? 0.95f : 0.55f)));
    ImGui::Begin("##chat", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::PopStyleColor();
    {
        ui::FontScope f(ui::fonts().small);
        ui::iconText(ui::Icon::Chat, ui::col::kBlue, session_->status().c_str(), ui::col::kTextMuted);
    }
    if (ImGui::SmallButton("Players (Tab)")) showClients_ = !showClients_;
    const usize first = chatLines_.size() > 6 ? chatLines_.size() - 6 : 0;
    for (usize i = first; i < chatLines_.size(); ++i) ImGui::TextWrapped("%s", chatLines_[i].c_str());
    if (!showChat_) {
        ui::FontScope f(ui::fonts().small);
        ImGui::TextDisabled("Press Enter to chat");
    }
    if (showChat_) {
        // Who to: everyone, your company (co-op partners), or the player picked in the players window.
        const bool inCompany = world().companies.contains(session_->localCompany());
        if (chatScope_ == static_cast<int>(net::ChatScope::Company) && !inCompany) chatScope_ = 0;
        if (chatScope_ == static_cast<int>(net::ChatScope::Private) &&
            std::ranges::none_of(everyone, [&](const net::ClientInfo& p) { return p.id == chatTarget_; }))
            chatScope_ = 0;
        {
            ui::FontScope f(ui::fonts().small);
            if (ImGui::RadioButton("Everyone", chatScope_ == 0)) chatScope_ = 0;
            if (inCompany) {
                ImGui::SameLine();
                if (ImGui::RadioButton("Company", chatScope_ == 1)) chatScope_ = 1;
            }
            if (chatScope_ == 2) {
                ImGui::SameLine();
                ImGui::RadioButton(std::format("To {}", nameOf(chatTarget_)).c_str(), true);
            }
        }
        ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputTextWithHint("##say", "Say something...", chatBuf_, sizeof(chatBuf_), ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (chatBuf_[0]) {
                const auto scope = static_cast<net::ChatScope>(chatScope_);
                if (client) client->sendChat(chatBuf_, scope, chatTarget_);
                if (host) host->net().localChat(chatBuf_, scope, chatTarget_);
            }
            chatBuf_[0] = 0;
            showChat_ = false;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) showChat_ = false;
    }
    ImGui::End();
}

void App::quicksave() {
    if (!session_ || !session_->ready()) return;
    if (dynamic_cast<net::NetClientSession*>(session_.get())) {
        toasts_.push_back({"Only the host can save a multiplayer game", glfwGetTime() + 3.0, true});
        return;
    }
    std::string err;
    if (saveGame(world(), session_->localCompany(), std::format("{}/quicksave.tpsave", kSaveDir), err))
        toasts_.push_back({"Quicksaved", glfwGetTime() + 3.0, false});
    else
        toasts_.push_back({"Quicksave failed: " + err, glfwGetTime() + 5.0, true});
}

void App::autosave() {
    if (!session_ || !session_->ready() || dynamic_cast<net::NetClientSession*>(session_.get())) return;
    if (!preferences_.autosave) return;
    autosaveClock_ = 0.0;
    autosaveTick_ = world().clock.tick;
    std::string err;
    const std::string path = nextAutosavePath(kSaveDir, kAutosaveSlots);
    if (saveGame(world(), session_->localCompany(), path, err))
        toasts_.push_back({"Autosaved", glfwGetTime() + 2.0, false});
    else
        log::warn("Autosave failed: {}", err);
}

void App::updateAutosave(double dt) {
    // Single player and multiplayer hosts save every five minutes of play; time paused doesn't count.
    if (!preferences_.autosave || !session_ || !session_->ready() || session_->speed() == 0 ||
        dynamic_cast<net::NetClientSession*>(session_.get()))
        return;
    autosaveClock_ += dt;
    if (autosaveClock_ >= autosaveInterval_ && world().clock.tick != autosaveTick_) autosave();
}

void App::applyDisplayMode() {
    if (!window_) return;
    GLFWmonitor* current = glfwGetWindowMonitor(window_);
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    if (!monitors || count == 0) return;
    preferences_.monitor = std::clamp(preferences_.monitor, 0, count - 1);
    if (preferences_.fullscreen) {
        GLFWmonitor* target = monitors[preferences_.monitor];
        if (current == target) return;
        const GLFWvidmode* mode = glfwGetVideoMode(target);
        if (!mode) { settingsError_ = "The selected display is unavailable"; return; }
        if (!current) {
            wasMaximized_ = glfwGetWindowAttrib(window_, GLFW_MAXIMIZED) == GLFW_TRUE;
            if (wasMaximized_) glfwRestoreWindow(window_);
            glfwGetWindowPos(window_, &windowedX_, &windowedY_);
            glfwGetWindowSize(window_, &windowedWidth_, &windowedHeight_);
        }
        // Match the desktop's current mode for smooth, borderless fullscreen switching.
        glfwSetWindowMonitor(window_, target, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else if (current) {
        // Keep the restored window reachable if its previous display was disconnected.
        bool visible = false;
        for (int i = 0; i < count; ++i) {
            int x, y, width, height;
            glfwGetMonitorWorkarea(monitors[i], &x, &y, &width, &height);
            if (windowedX_ + windowedWidth_ > x && windowedX_ < x + width &&
                windowedY_ >= y && windowedY_ < y + height - 32) visible = true;
        }
        if (!visible) {
            int width, height;
            glfwGetMonitorWorkarea(monitors[preferences_.monitor], &windowedX_, &windowedY_, &width, &height);
            windowedWidth_ = std::min(windowedWidth_, width);
            windowedHeight_ = std::min(windowedHeight_, height - 40);
        }
        glfwSetWindowMonitor(window_, nullptr, windowedX_, windowedY_, windowedWidth_, windowedHeight_, GLFW_DONT_CARE);
        if (wasMaximized_) glfwMaximizeWindow(window_);
    }
}

void App::applyPreferences() {
    preferences_.clamp();
    audio_.configure({preferences_.sounds, preferences_.masterVolume / 100.0f, preferences_.musicVolume / 100.0f,
        preferences_.vehicleVolume / 100.0f, preferences_.ambienceVolume / 100.0f, preferences_.interfaceVolume / 100.0f});
    renderer_.settings.shadows = preferences_.shadows;
    renderer_.settings.weather = preferences_.weather;
    renderer_.settings.maxViewDistance = static_cast<float>(preferences_.viewDistance);
    renderer_.settings.shadowMapSize = 512 << preferences_.shadowQuality;
    renderer_.settings.treeDistance = static_cast<float>(preferences_.treeDistance);
    renderer_.settings.groundDetail = preferences_.groundDetail;
    constexpr float detail[] = {0.65f, 1.0f, 1.5f};
    renderer_.settings.detailScale = detail[preferences_.detailQuality];
    renderer_.settings.brightness = preferences_.brightnessPercent / 100.0f;
    renderer_.settings.clouds = preferences_.clouds;
    renderer_.settings.cloudShadows = preferences_.cloudShadows;
    renderer_.settings.fogStrength = preferences_.fogPercent / 100.0f;
    renderer_.settings.nightFogStrength = preferences_.nightFogPercent / 100.0f;
    renderer_.settings.particles = preferences_.particles;
    renderer_.settings.lightHalos = preferences_.lightHalos;
    renderer_.settings.treeSway = preferences_.treeSway;
    renderer_.settings.waterAnimation = preferences_.waterAnimation;
    daylightOnly_ = preferences_.daylightOnly;
    applyDisplayMode();
    glfwSwapInterval(preferences_.vsync ? 1 : 0);
}

void App::drawSettings() {
    if (!showSettings_) return;
    ui::PanelSpec spec;
    spec.pos = screenCentre();
    spec.pivot = ImVec2(0.5f, 0.5f);
    const float scale = uiScale_;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    spec.width = std::min(660.0f, viewport->WorkSize.x / scale - 32.0f);
    spec.height = std::min(740.0f, viewport->WorkSize.y / scale - 40.0f);
    spec.subtitle = "Graphics, audio, controls and gameplay";
    bool changed = false;
    if (ui::beginPanel("###settings", "Settings", ui::Icon::Wrench, &showSettings_, spec)) {
        if (ImGui::BeginTabBar("##settingsTabs")) {
            if (ImGui::BeginTabItem("Graphics")) {
                if (ImGui::BeginChild("##graphicsOptions", ImVec2(0, -68.0f * scale))) {
                    ImGui::PushItemWidth(260.0f * scale);
                    ui::sectionHeader("Display");
                    changed |= ImGui::Checkbox("Fullscreen (borderless)", &preferences_.fullscreen);
                    ImGui::TextDisabled("Alt+Enter switches fullscreen on and off.");
                    int count = 0;
                    GLFWmonitor** monitors = glfwGetMonitors(&count);
                    const int selected = count > 0 ? std::clamp(preferences_.monitor, 0, count - 1) : 0;
                    if (ImGui::BeginCombo("Fullscreen monitor", count ? glfwGetMonitorName(monitors[selected]) : "Unavailable")) {
                        for (int i = 0; i < count; ++i) {
                            ImGui::PushID(i);
                            if (ImGui::Selectable(glfwGetMonitorName(monitors[i]), selected == i)) { preferences_.monitor = i; changed = true; }
                            ImGui::PopID();
                        }
                        ImGui::EndCombo();
                    }
                    changed |= ImGui::Checkbox("VSync", &preferences_.vsync);
                    changed |= ImGui::Combo("Frame rate limit", &preferences_.frameCap, "Unlimited\0" "30 FPS\0" "60 FPS\0" "90 FPS\0" "120 FPS\0" "144 FPS\0" "165 FPS\0" "240 FPS\0");
                    changed |= ImGui::Combo("Anti-aliasing", &preferences_.antiAliasing, "Off\0" "2x MSAA\0" "4x MSAA\0" "8x MSAA\0");
                    if (startupAntiAliasing_ != preferences_.antiAliasing) ImGui::TextWrapped("Save preferences and restart to apply anti-aliasing.");
                    changed |= ImGui::SliderInt("Brightness", &preferences_.brightnessPercent, 50, 200, "%d%%");
                    ui::sectionHeader("Quality");
                    for (int i = 0; i < 3; ++i) {
                        if (i) ImGui::SameLine();
                        if (ui::button(i == 0 ? "Low" : i == 1 ? "Balanced" : "High", ui::Icon::Chart, ui::Btn::Secondary)) {
                            preferences_.graphicsPreset(i); changed = true;
                        }
                    }
                    changed |= ImGui::Checkbox("Shadows", &preferences_.shadows);
                    ImGui::BeginDisabled(!preferences_.shadows);
                    changed |= ImGui::Combo("Shadow resolution", &preferences_.shadowQuality, "512 (Low)\0" "1024\0" "2048 (Balanced)\0" "4096 (High)\0");
                    ImGui::EndDisabled();
                    changed |= ImGui::Combo("Terrain detail", &preferences_.detailQuality, "Low\0" "Balanced\0" "High\0");
                    changed |= ImGui::SliderInt("View distance", &preferences_.viewDistance, 120, 900, "%d tiles");
                    changed |= ImGui::SliderInt("Tree draw distance", &preferences_.treeDistance, 60, 500, "%d tiles");
                    changed |= ImGui::Combo("Ground detail", &preferences_.groundDetail, "Off\0" "Some\0" "Lush\0");
                    ui::itemTooltip({"Ground detail", "Grass, flowers, ferns, reeds and stones on the ground when zoomed in"});
                    ui::sectionHeader("Atmosphere");
                    changed |= ImGui::Checkbox("Weather effects", &preferences_.weather);
                    ImGui::BeginDisabled(!preferences_.weather);
                    changed |= ImGui::Checkbox("Clouds", &preferences_.clouds);
                    ImGui::BeginDisabled(!preferences_.clouds);
                    changed |= ImGui::Checkbox("Cloud shadows", &preferences_.cloudShadows);
                    ImGui::EndDisabled();
                    changed |= ImGui::SliderInt("Mist strength", &preferences_.fogPercent, 0, 150, "%d%%");
                    changed |= ImGui::SliderInt("Night mist", &preferences_.nightFogPercent, 0, 100, "%d%% of daytime");
                    ImGui::EndDisabled();
                    changed |= ImGui::Checkbox("Always daylight", &preferences_.daylightOnly);
                    ImGui::TextWrapped("Always daylight changes lighting only; the game clock continues normally.");
                    ui::sectionHeader("Effects");
                    changed |= ImGui::Checkbox("Smoke and steam", &preferences_.particles);
                    changed |= ImGui::Checkbox("Light halos", &preferences_.lightHalos);
                    changed |= ImGui::Checkbox("Tree sway", &preferences_.treeSway);
                    changed |= ImGui::Checkbox("Animated water", &preferences_.waterAnimation);
                    ImGui::PopItemWidth();
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Controls")) {
                changed |= ImGui::SliderInt("Camera pan", &preferences_.panPercent, 25, 300, "%d%%");
                changed |= ImGui::SliderInt("Camera orbit", &preferences_.orbitPercent, 25, 300, "%d%%");
                changed |= ImGui::SliderInt("Mouse wheel zoom", &preferences_.zoomPercent, 25, 300, "%d%%");
                ImGui::TextWrapped("WASD / arrows: pan. Q/E: orbit. R/F: tilt. Page Up/Down or mouse wheel: zoom. Right drag: orbit. Middle drag: pan.");
                ImGui::TextWrapped("1-5: construction toolbars. Space: pause. F5: quicksave. F12: screenshot. Esc: close the current tool or window.");
                ImGui::TextWrapped("H: show or hide the elevation view (height colours and contour lines).");
                changed |= ImGui::Checkbox("Elevation view while laying track", &preferences_.trackContours);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Audio", nullptr, openAudioSettings_ ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
                openAudioSettings_ = false;
                changed |= ImGui::Checkbox("Enable audio", &preferences_.sounds);
                ImGui::BeginDisabled(!preferences_.sounds);
                changed |= ImGui::SliderInt("Master volume", &preferences_.masterVolume, 0, 100, "%d%%");
                changed |= ImGui::SliderInt("Music", &preferences_.musicVolume, 0, 100, "%d%%");
                ImGui::Text("Now playing: %s", audio_.musicTrack().data());
                if (ui::button("Next track", ui::Icon::Play, ui::Btn::Secondary)) audio_.nextTrack();
                changed |= ImGui::SliderInt("Vehicles", &preferences_.vehicleVolume, 0, 100, "%d%%");
                changed |= ImGui::SliderInt("Ambience", &preferences_.ambienceVolume, 0, 100, "%d%%");
                changed |= ImGui::SliderInt("Interface and notifications", &preferences_.interfaceVolume, 0, 100, "%d%%");
                changed |= ImGui::Checkbox("Mute when game is in background", &preferences_.muteUnfocused);
                if (ui::button("Preview sound", ui::Icon::Play, ui::Btn::Secondary)) audio_.play(SoundEvent::Buy);
                ImGui::EndDisabled();
                ImGui::TextWrapped("Original funk and jazz grooves play in menus and during play. Vehicle sounds follow the camera: zoom in or move closer to hear engines and wheels. Pausing quietens vehicles.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Gameplay")) {
                changed |= ImGui::Checkbox("Autosave every 5 minutes of play", &preferences_.autosave);
                ImGui::TextDisabled("Keeps the last 5, autosave-1 to autosave-5. Multiplayer hosts save for their game.");
                changed |= ImGui::SliderInt("New game year", &preferences_.startYear, 1900, 2100);
                changed |= ImGui::SliderInt("New game interest", &preferences_.interestPercent, 0, 20, "%d%%");
                changed |= ImGui::Checkbox("New game inflation (about 2% per year)", &preferences_.inflation);
                changed |= ImGui::Checkbox("New game recessions", &preferences_.recessions);
                changed |= ImGui::SliderInt("New game AI competitors", &preferences_.aiCompetitors, 0, 3);
                ImGui::TextWrapped("Year, interest and economy events apply when starting a new world.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Updates")) {
                drawUpdateSettings();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        if (changed) {
            applyPreferences();
            settingsError_ = startupAntiAliasing_ != preferences_.antiAliasing ?
                "Save preferences to keep changes. Anti-aliasing needs a restart." : "Changes applied. Save preferences to keep them.";
        }
        if (!settingsError_.empty()) ImGui::TextWrapped("%s", settingsError_.c_str());
        ImGui::Separator();
        if (ui::button("Save preferences", ui::Icon::Save, ui::Btn::Primary)) {
            applyPreferences();
            settingsError_ = preferences_.save("settings.cfg") ? "Preferences saved" : "Could not write settings.cfg";
        }
        ImGui::SameLine();
        if (ui::button("Defaults", ui::Icon::Wrench, ui::Btn::Secondary)) {
            preferences_ = ClientPreferences{};
            applyPreferences();
            settingsError_.clear();
        }
    }
    ui::endPanel();
}

} // namespace tp
