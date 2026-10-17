// Main menu, in-game menu, connection screen, chat and autosave.

#include "client/app.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>

#include "core/calendar.hpp"
#include "core/log.hpp"
#include "core/version.hpp"
#include "net/host_session.hpp"
#include "net/net_client.hpp"
#include "render/picking.hpp"

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

void centreNextWindow(float width) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(width, 0), ImGuiCond_Always);
}

bool bigButton(const char* label) { return ImGui::Button(label, ImVec2(-1.0f, ImGui::GetFrameHeight() * 1.6f)); }

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
    centreNextWindow(420.0f * uiScale_);
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::Begin("##mainmenu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowFontScale(1.8f);
    ImGui::TextUnformatted("transport-poly");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Low poly transport tycoon - version %s", kVersionString.data());
    ImGui::Separator();
    if (!menuError_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.45f, 1.0f), "%s", menuError_.c_str());
        ImGui::Separator();
    }
    switch (menuPage_) {
    case MenuPage::Main:
        if (bigButton("New game")) {
            menuPage_ = MenuPage::NewGame;
            std::snprintf(seedBuf_, sizeof(seedBuf_), "%llu", static_cast<unsigned long long>(randomSeed()));
            menuError_.clear();
        }
        if (bigButton("Load game")) {
            saves_ = listSaves(kSaveDir);
            menuPage_ = MenuPage::Load;
            menuError_.clear();
        }
        if (bigButton("Host multiplayer game")) {
            menuPage_ = MenuPage::Host;
            std::snprintf(seedBuf_, sizeof(seedBuf_), "%llu", static_cast<unsigned long long>(randomSeed()));
            menuError_.clear();
        }
        if (bigButton("Join multiplayer game")) {
            menuPage_ = MenuPage::Join;
            menuError_.clear();
        }
        if (bigButton("Quit")) glfwSetWindowShouldClose(window_, 1);
        break;
    case MenuPage::NewGame:
        ImGui::InputText("World seed", seedBuf_, sizeof(seedBuf_));
        ImGui::SameLine();
        if (ImGui::SmallButton("Random"))
            std::snprintf(seedBuf_, sizeof(seedBuf_), "%llu", static_cast<unsigned long long>(randomSeed()));
        ImGui::InputText("Company name", companyName_, sizeof(companyName_));
        ImGui::TextDisabled("Leave empty for a generated name.");
        if (bigButton("Start")) {
            WorldSettings s;
            s.seed = std::strtoull(seedBuf_, nullptr, 10);
            startSession(std::make_unique<LocalSession>(s, companyName_));
        }
        if (ImGui::Button("Back")) menuPage_ = MenuPage::Main;
        break;
    case MenuPage::Load:
        if (saves_.empty()) ImGui::TextDisabled("No saved games in '%s'", kSaveDir);
        for (const SaveInfo& info : saves_) {
            const std::string label = std::format("{}\n{} - {}", std::filesystem::path(info.path).stem().string(),
                                                  info.companyName, formatDate(info.day));
            if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(0, ImGui::GetTextLineHeight() * 2.2f))) {
                std::string err;
                CompanyId company;
                auto world = loadGame(info.path, company, err);
                if (world) startSession(std::make_unique<LocalSession>(std::move(world), company));
                else menuError_ = "Could not load: " + err;
            }
        }
        if (ImGui::Button("Back")) menuPage_ = MenuPage::Main;
        break;
    case MenuPage::Host:
        ImGui::InputText("World seed", seedBuf_, sizeof(seedBuf_));
        ImGui::InputText("Company name", companyName_, sizeof(companyName_));
        ImGui::InputInt("Port", &port_);
        if (bigButton("Host new world")) {
            WorldSettings s;
            s.seed = std::strtoull(seedBuf_, nullptr, 10);
            auto world = std::make_unique<World>(s);
            Command create;
            create.client = kLocalClientId;
            create.payload = CmdCreateCompany{companyName_, 0xFF};
            const CommandResult r = executeCommand(*world, create);
            net::NetServer::Options opt;
            opt.port = static_cast<u16>(port_);
            auto host = std::make_unique<net::HostSession>(std::move(world), CompanyId(static_cast<u8>(r.createdId)), opt);
            std::string err;
            if (host->start(err)) startSession(std::move(host));
            else menuError_ = err;
        }
        if (ImGui::Button("Back")) menuPage_ = MenuPage::Main;
        break;
    case MenuPage::Join:
        ImGui::InputText("Server address", address_, sizeof(address_));
        ImGui::InputInt("Port", &port_);
        ImGui::InputText("Your name", playerName_, sizeof(playerName_));
        if (bigButton("Join")) {
            menuError_.clear();
            startSession(std::make_unique<net::NetClientSession>(address_, static_cast<u16>(port_), playerName_));
        }
        if (ImGui::Button("Back")) menuPage_ = MenuPage::Main;
        break;
    }
    ImGui::End();
}

void App::drawConnecting() {
    auto* client = dynamic_cast<net::NetClientSession*>(session_.get());
    centreNextWindow(420.0f * uiScale_);
    ImGui::Begin("##connecting", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextUnformatted(session_->status().c_str());
    if (client && client->phase() == net::NetClientSession::Phase::Downloading)
        ImGui::ProgressBar(client->downloadProgress());
    if (client && client->phase() == net::NetClientSession::Phase::Failed) {
        if (ImGui::Button("Back to main menu")) {
            menuError_ = client->error();
            ImGui::End();
            returnToMainMenu();
            return;
        }
    } else if (ImGui::Button("Cancel")) {
        ImGui::End();
        returnToMainMenu();
        return;
    }
    ImGui::End();
}

void App::drawGameMenu() {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && tool_ == Tool::None && !selVehicle_.valid() && !selDepot_.valid() &&
        !selStation_.valid() && !selTown_.valid())
        showGameMenu_ = !showGameMenu_;
    if (!showGameMenu_) return;
    centreNextWindow(360.0f * uiScale_);
    ImGui::Begin("Game menu", &showGameMenu_, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
    const bool canSave = !dynamic_cast<net::NetClientSession*>(session_.get());
    if (canSave) {
        ImGui::InputText("Save name", saveName_, sizeof(saveName_));
        if (bigButton("Save game")) {
            std::string err;
            const std::string path = std::format("{}/{}.tpsave", kSaveDir, saveName_);
            if (saveGame(world(), session_->localCompany(), path, err))
                toasts_.push_back({"Saved " + path, glfwGetTime() + 3.0, false});
            else
                toasts_.push_back({"Save failed: " + err, glfwGetTime() + 5.0, true});
            showGameMenu_ = false;
        }
    } else {
        ImGui::TextDisabled("Only the host can save a multiplayer game.");
    }
    if (bigButton("Exit to main menu")) {
        ImGui::End();
        returnToMainMenu();
        return;
    }
    if (bigButton("Quit game")) glfwSetWindowShouldClose(window_, 1);
    ImGui::End();
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
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 10, vp->WorkPos.y + vp->WorkSize.y - 70), ImGuiCond_Always,
                            ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(440 * uiScale_, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(showChat_ ? 0.75f : 0.3f);
    ImGui::Begin("##chat", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextDisabled("%s", session_->status().c_str());
    const usize first = chatLines_.size() > 6 ? chatLines_.size() - 6 : 0;
    for (usize i = first; i < chatLines_.size(); ++i) ImGui::TextWrapped("%s", chatLines_[i].c_str());
    if (showChat_) {
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##say", chatBuf_, sizeof(chatBuf_), ImGuiInputTextFlags_EnterReturnsTrue)) {
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
