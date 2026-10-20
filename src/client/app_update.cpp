// Updates: checking for a new release when the game starts, offering it with its release notes,
// downloading and installing it, and the update settings.

#include "client/app.hpp"
#include "client/ui/widgets.hpp"

#include <imgui.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "core/version.hpp"

#include <format>
#include <string>

namespace tp {

namespace {

#ifdef TP_UPDATE_FEED
constexpr const char* kUpdateFeed = TP_UPDATE_FEED;
#else
constexpr const char* kUpdateFeed = "";
#endif

// Release notes are Markdown: headings, bullet lists (with wrapped continuation lines) and paragraphs.
void releaseNotes(const std::string& notes) {
    std::string paragraph;
    bool bullet = false;
    const auto flush = [&] {
        if (paragraph.empty()) return;
        if (bullet) {
            ImGui::Bullet();
            ImGui::SameLine();
        }
        ImGui::TextWrapped("%s", paragraph.c_str());
        paragraph.clear();
        bullet = false;
    };
    usize at = 0;
    while (at <= notes.size()) {
        usize end = notes.find('\n', at);
        if (end == std::string::npos) end = notes.size();
        std::string line = notes.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string_view trimmed = line;
        while (!trimmed.empty() && trimmed.front() == ' ') trimmed.remove_prefix(1);
        if (trimmed.empty()) {
            flush();
        } else if (trimmed.starts_with("#")) {
            flush();
            while (!trimmed.empty() && (trimmed.front() == '#' || trimmed.front() == ' ')) trimmed.remove_prefix(1);
            ui::sectionHeader(std::string(trimmed).c_str());
        } else if (trimmed.starts_with("- ") || trimmed.starts_with("* ")) {
            flush();
            bullet = true;
            paragraph = std::string(trimmed.substr(2));
        } else {
            if (!paragraph.empty()) paragraph += ' ';
            paragraph += trimmed;
        }
        if (end == notes.size()) break;
    }
    flush();
}

} // namespace

bool App::updatesConfigured() const { return !updateFeed_.empty(); }

void App::startUpdateCheck() {
    if (updateFeed_.empty()) updateFeed_ = kUpdateFeed;
    if (!updatesConfigured()) return;
    updater_.check(updateFeed_, std::string(kVersionString));
}

void App::drawUpdatePanel() {
    using State = update::Updater::State;
    const State state = updater_.state();
    if (state == State::Idle || state == State::Checking || state == State::UpToDate) return;
    const releases::Release release = updater_.release();
    if ((state == State::Available || state == State::Ready) &&
        (updateDismissed_ || release.version == preferences_.skippedVersion) && !autoUpdate_) return;
    if (state == State::Failed && !showUpdateErrors_) return;
    if (autoUpdate_) {
        // Automation: download and install straight away.
        if (state == State::Available) updater_.download();
        if (state == State::Ready && updater_.apply()) glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
    const float s = uiScale_;
    ui::PanelSpec spec;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    spec.pos = ImVec2(vp->WorkPos.x + vp->WorkSize.x - 24.0f * s, vp->WorkPos.y + 24.0f * s);
    spec.pivot = ImVec2(1.0f, 0.0f);
    spec.width = 470.0f;
    spec.accent = state == State::Failed ? ui::col::kRed : ui::col::kGreen;
    const std::string sub = std::format("You have {}", kVersionString);
    spec.subtitle = sub.c_str();
    bool open = true;
    const std::string title = state == State::Failed ? std::string("Update problem") : std::format("{} is available", release.title);
    if (ui::beginPanel("###update", title.c_str(), ui::Icon::Spark, &open, spec)) {
        if (state == State::Failed) {
            ImGui::PushTextWrapPos(0.0f);
            ui::iconText(ui::Icon::Warning, ui::col::kRed, updater_.error().c_str(), ui::col::kRed);
            ImGui::PopTextWrapPos();
        }
        if (!release.notes.empty()) {
            ui::sectionHeader("What's new");
            if (ImGui::BeginChild("##notes", ImVec2(0, 260.0f * s), ImGuiChildFlags_Borders)) releaseNotes(release.notes);
            ImGui::EndChild();
        }
        ImGui::Dummy(ImVec2(0, 4.0f * s));
        const float half = ui::columnWidth(2);
        if (state == State::Available || state == State::Failed) {
            const bool installer = update::installKind() == releases::Kind::WindowsInstaller;
            if (ui::button(state == State::Failed ? "Try again" : "Download update", ui::Icon::ArrowDown, ui::Btn::Primary, ImVec2(half, 0))) {
                showUpdateErrors_ = true;
                if (state == State::Failed && release.version.empty()) startUpdateCheck();
                else updater_.download();
            }
            ui::itemTooltip({installer ? "Downloads the installer and updates this installation, then starts the game again"
                                       : "Downloads the new version and unpacks it over this one when the game closes, then starts it again"});
            ImGui::SameLine();
            if (ui::button("Release page", ui::Icon::Globe, ui::Btn::Secondary, ImVec2(-1.0f, 0)) && !release.page.empty())
                update::openUrl(release.page);
            if (state == State::Available) {
                if (ui::button("Skip this version", ui::Icon::Skip, ui::Btn::Ghost, ImVec2(half, 0))) {
                    preferences_.skippedVersion = release.version;
                    preferences_.save("settings.cfg");
                }
                ImGui::SameLine();
                if (ui::button("Not now", ui::Icon::Close, ui::Btn::Ghost, ImVec2(-1.0f, 0))) updateDismissed_ = true;
            }
        } else if (state == State::Downloading) {
            const u64 got = updater_.downloaded(), total = updater_.total();
            const float fraction = total ? static_cast<float>(static_cast<double>(got) / static_cast<double>(total)) : 0.0f;
            ui::meterRow("Downloading", std::format("{:.1f} / {:.1f} MB", got / 1048576.0, total / 1048576.0).c_str(), fraction, ui::col::kGreen);
        } else if (state == State::Ready) {
            ui::iconText(ui::Icon::Check, ui::col::kGreen, "Downloaded and checked. The game closes to install it, then starts again.");
            if (ui::button("Install and restart", ui::Icon::Play, ui::Btn::Primary, ImVec2(-1.0f, 0))) {
                if (screen_ == Screen::Playing) autosave();
                if (updater_.apply()) glfwSetWindowShouldClose(window_, GLFW_TRUE);
            }
        }
    }
    ui::endPanel();
    if (!open) {
        updateDismissed_ = true;
        showUpdateErrors_ = false;
    }
}

void App::drawUpdateSettings() {
    using State = update::Updater::State;
    ImGui::Text("Version %.*s", static_cast<int>(kVersionString.size()), kVersionString.data());
    if (!updatesConfigured()) {
        ImGui::TextWrapped("This build does not check for updates (only release builds do).");
        return;
    }
    bool changed = ImGui::Checkbox("Check for updates when the game starts", &preferences_.checkUpdates);
    if (changed) preferences_.save("settings.cfg");
    const State state = updater_.state();
    const releases::Release release = updater_.release();
    switch (state) {
    case State::Idle: ImGui::TextDisabled("Not checked yet."); break;
    case State::Checking: ImGui::TextDisabled("Checking..."); break;
    case State::UpToDate: ui::iconText(ui::Icon::Check, ui::col::kGreen, "You have the latest version."); break;
    case State::Available:
        ui::iconText(ui::Icon::Spark, ui::col::kAccent, std::format("{} is available.", release.title).c_str());
        if (!preferences_.skippedVersion.empty() && preferences_.skippedVersion == release.version) ImGui::TextDisabled("You chose to skip it.");
        break;
    case State::Downloading: ImGui::TextDisabled("Downloading the update..."); break;
    case State::Ready: ui::iconText(ui::Icon::Check, ui::col::kGreen, "The update is ready to install."); break;
    case State::Failed: ImGui::TextWrapped("Update error: %s", updater_.error().c_str()); break;
    }
    ImGui::BeginDisabled(state == State::Checking || state == State::Downloading);
    if (ui::button("Check now", ui::Icon::Spark, ui::Btn::Secondary)) {
        updateDismissed_ = false;
        showUpdateErrors_ = true;
        preferences_.skippedVersion.clear();
        preferences_.save("settings.cfg");
        startUpdateCheck();
    }
    ImGui::EndDisabled();
    if (state == State::Available || state == State::Ready) {
        ImGui::SameLine();
        if (ui::button("Show update", ui::Icon::ArrowDown, ui::Btn::Primary)) {
            updateDismissed_ = false;
            if (preferences_.skippedVersion == release.version) preferences_.skippedVersion.clear();
            preferences_.save("settings.cfg");
        }
    }
}

} // namespace tp
