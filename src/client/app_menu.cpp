// Main menu, in-game menu, connection screen, chat and autosave.

#include "client/app.hpp"
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
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>

namespace tp {

namespace {

constexpr const char* kSaveDir = "saves";

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
    session_ = std::move(session);
    renderer_.invalidateAll();
    explorePending_.clear();
    selDepot_ = {};
    selVehicle_ = {};
    selStation_ = {};
    selTown_ = {};
    followVehicle_ = {};
    tool_ = Tool::None;
    showGameMenu_ = false;
    cameraPlaced_ = false;
    lastAutosaveMonth_ = -1;
    chatLines_.clear();
    screen_ = Screen::Playing;
    if (session_->ready()) placeCameraAtStart();
}

void App::returnToMainMenu() {
    session_.reset();
    screen_ = Screen::MainMenu;
    menuPage_ = MenuPage::Main;
    renderer_.invalidateAll();
    showGameMenu_ = false;
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
            ImGui::Dummy(ImVec2(0, 6.0f * s));
            if (ui::button("Start", ui::Icon::Play, ui::Btn::Primary, ImVec2(-1.0f, 40.0f))) {
                WorldSettings ws;
                ws.seed = std::strtoull(seedBuf_, nullptr, 10);
                startSession(std::make_unique<LocalSession>(ws, companyName_));
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
                ui::drawIcon(dl, ui::Icon::Save, ImVec2(p.x + 22.0f * s, p.y + h * 0.5f), 18.0f * s,
                             hovered ? ui::col::kAccent : ui::col::kTextMuted);
                const std::string name = std::filesystem::path(info.path).stem().string();
                const std::string detail = std::format("{}  -  {}", info.companyName, formatDate(info.day));
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
            field("Company name", "##company", companyName_, sizeof(companyName_), "Leave empty for a generated name");
            portField(port_);
            ImGui::Dummy(ImVec2(0, 6.0f * s));
            if (ui::button("Host new world", ui::Icon::Server, ui::Btn::Primary, ImVec2(-1.0f, 40.0f))) {
                WorldSettings ws;
                ws.seed = std::strtoull(seedBuf_, nullptr, 10);
                auto hosted = std::make_unique<World>(ws);
                Command create;
                create.client = kLocalClientId;
                create.payload = CmdCreateCompany{companyName_, 0xFF};
                const CommandResult r = executeCommand(*hosted, create);
                net::NetServer::Options opt;
                opt.port = static_cast<u16>(port_);
                auto host = std::make_unique<net::HostSession>(std::move(hosted), CompanyId(static_cast<u8>(r.createdId)), opt);
                std::string err;
                if (host->start(err)) startSession(std::move(host));
                else menuError_ = err;
            }
            break;
        case MenuPage::Join:
            field("Server address", "##address", address_, sizeof(address_));
            portField(port_);
            field("Your name", "##name", playerName_, sizeof(playerName_));
            ImGui::Dummy(ImVec2(0, 6.0f * s));
            if (ui::button("Join", ui::Icon::Globe, ui::Btn::Primary, ImVec2(-1.0f, 40.0f))) {
                menuError_.clear();
                startSession(std::make_unique<net::NetClientSession>(address_, static_cast<u16>(port_), playerName_));
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
    std::vector<net::ChatBroadcastMsg> incoming;
    if (client) incoming = client->takeChat();
    if (host) {
        incoming = host->net().takeChat();
        for (const std::string& e : host->net().takeEvents()) chatLines_.push_back("* " + e);
    }
    for (const auto& m : incoming) chatLines_.push_back(std::format("<{}> {}", m.from, m.text));
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
    const usize first = chatLines_.size() > 6 ? chatLines_.size() - 6 : 0;
    for (usize i = first; i < chatLines_.size(); ++i) ImGui::TextWrapped("%s", chatLines_[i].c_str());
    if (!showChat_) {
        ui::FontScope f(ui::fonts().small);
        ImGui::TextDisabled("Press Enter to chat");
    }
    if (showChat_) {
        ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputTextWithHint("##say", "Say something...", chatBuf_, sizeof(chatBuf_), ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (chatBuf_[0]) {
                if (client) client->sendChat(chatBuf_);
                if (host) host->net().broadcastChat("Host", chatBuf_);
            }
            chatBuf_[0] = 0;
            showChat_ = false;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) showChat_ = false;
    }
    ImGui::End();
}

void App::autosave() {
    if (!session_ || !session_->ready() || dynamic_cast<net::NetClientSession*>(session_.get())) return;
    const i32 month = static_cast<i32>(world().clock.date().month);
    if (lastAutosaveMonth_ < 0) {
        lastAutosaveMonth_ = month;
        return;
    }
    if (month == lastAutosaveMonth_) return;
    lastAutosaveMonth_ = month;
    std::string err;
    if (saveGame(world(), session_->localCompany(), std::format("{}/autosave.tpsave", kSaveDir), err))
        toasts_.push_back({"Autosaved", glfwGetTime() + 2.0, false});
    else
        log::warn("Autosave failed: {}", err);
}

} // namespace tp
