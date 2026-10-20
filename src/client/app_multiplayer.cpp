// Multiplayer: the company lobby (found a company with your own name and colour, join a friend's
// for co-op, or watch), the players window, other players' pointers in the world, and bot players
// for automated tests and screenshots.

#include "client/app.hpp"
#include "client/map_draw.hpp"
#include "client/ui/widgets.hpp"

#include <imgui.h>

#include "core/calendar.hpp"
#include "net/host_session.hpp"
#include "net/net_client.hpp"
#include "render/picking.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <string>

namespace tp {

namespace {

ImU32 colourOf(const World& w, CompanyId company) {
    const Company* c = w.companies.tryGet(company);
    return c ? ui::rgb(companyColourRgb(c->colour)) : ui::col::kTextMuted;
}

void colourChip(ImU32 colour, float size) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + (h - size) * 0.5f), ImVec2(p.x + size, p.y + (h + size) * 0.5f), colour,
                                              size * 0.25f);
    ImGui::Dummy(ImVec2(size, h));
}

} // namespace

net::NetClientSession* App::netClient() { return dynamic_cast<net::NetClientSession*>(session_.get()); }
net::HostSession* App::netHost() { return dynamic_cast<net::HostSession*>(session_.get()); }

std::vector<net::ClientInfo> App::players() {
    if (auto* c = netClient()) return c->clients();
    if (auto* h = netHost()) return h->net().clients();
    return {};
}

u16 App::localClientId() {
    if (auto* c = netClient()) return c->clientId();
    return kLocalClientId;
}

bool App::companyLocked(CompanyId company) {
    if (auto* c = netClient()) return std::ranges::find(c->lockedCompanies(), company) != c->lockedCompanies().end();
    if (auto* h = netHost()) return h->net().companyLocked(company);
    return false;
}

bool App::colourPicker(const char* id, int& colour, CompanyId own) {
    // Sixteen liveries; those other companies use are struck through and cannot be chosen.
    const World* w = session_ ? &world() : nullptr;
    bool taken[kCompanyColourCount] = {};
    if (w)
        for (const auto [cid, c] : w->companies)
            if (cid != own && !c.bankrupt) taken[c.colour % kCompanyColourCount] = true;
    const float s = uiScale_;
    const float size = 26.0f * s;
    bool changed = false;
    ImGui::PushID(id);
    for (int i = 0; i < static_cast<int>(kCompanyColourCount); ++i) {
        if (i % 8) ImGui::SameLine(0, 5.0f * s);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID(i);
        ImGui::BeginDisabled(taken[i]);
        if (ImGui::InvisibleButton("##c", ImVec2(size, size)) && !taken[i]) {
            colour = i;
            changed = true;
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::EndDisabled();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 c = ui::rgb(companyColourRgb(static_cast<u8>(i)));
        dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), taken[i] ? ui::withAlpha(c, 0.35f) : c, 6.0f * s);
        if (taken[i]) dl->AddLine(ImVec2(p.x + 4.0f * s, p.y + size - 4.0f * s), ImVec2(p.x + size - 4.0f * s, p.y + 4.0f * s), ui::col::kText, 2.0f * s);
        if (colour == i) dl->AddRect(ImVec2(p.x - 2.0f * s, p.y - 2.0f * s), ImVec2(p.x + size + 2.0f * s, p.y + size + 2.0f * s), ui::col::kText, 7.0f * s, 0, 2.5f * s);
        if (hovered) ui::tooltip({std::string(companyColourName(static_cast<u8>(i))).c_str(), taken[i] ? "Another company's colour" : nullptr});
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}

void App::drawCompanyLobby() {
    auto* client = netClient();
    auto* host = netHost();
    if (!client && !host) return;
    if (client) {
        for (std::string& n : client->takeNotices()) lobbyNotice_ = std::move(n);
        if (client->choosingCompany()) showLobby_ = true;
    }
    if (!showLobby_ || !session_->ready()) return;
    World& w = world();
    const float s = uiScale_;
    const bool forced = client && client->choosingCompany();
    ui::PanelSpec spec;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    spec.pos = ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f);
    spec.pivot = ImVec2(0.5f, 0.5f);
    spec.cond = ImGuiCond_Appearing;
    spec.width = 540.0f;
    spec.accent = ui::col::kBlue;
    spec.subtitle = "Play together in one company, start your own, or watch";
    bool open = true;
    if (ui::beginPanel("###lobby", "Choose your company", ui::Icon::People, forced ? nullptr : &open, spec)) {
        if (!lobbyNotice_.empty()) ui::iconText(ui::Icon::Warning, ui::col::kRed, lobbyNotice_.c_str(), ui::col::kRed);
        const std::vector<net::ClientInfo> all = players();
        // Companies already in the game: join one to play together.
        ui::sectionHeader("Companies");
        bool any = false;
        for (const auto [cid, c] : w.companies) {
            if (c.bankrupt) continue;
            any = true;
            ImGui::PushID(static_cast<int>(cid.value));
            colourChip(ui::rgb(companyColourRgb(c.colour)), 14.0f * s);
            ImGui::SameLine(0, 8.0f * s);
            ImGui::TextUnformatted(c.name.c_str());
            const bool locked = companyLocked(cid);
            if (c.aiControlled) {
                ImGui::SameLine();
                ui::badge("Computer", ui::col::kTextMuted);
            }
            if (locked) {
                ImGui::SameLine();
                ui::badge("Password", ui::col::kOrange);
            }
            std::string who;
            for (const auto& p : all)
                if (p.company == cid) who += (who.empty() ? "" : ", ") + p.name;
            const bool mine = cid == session_->localCompany();
            {
                ui::FontScope f(ui::fonts().small);
                ui::mutedText(std::format("Founded {}  |  {}  |  {}", civilFromDays(w.clock.startDay + static_cast<i32>(c.foundedTick / kTicksPerDay)).year,
                                          formatMoney(c.money), who.empty() ? std::string("nobody playing") : who).c_str());
            }
            if (!c.aiControlled && !mine) {
                if (locked && client && joinTarget_ == cid.value) {
                    ImGui::SetNextItemWidth(200.0f * s);
                    ImGui::InputTextWithHint("##pw", "Company password", joinPassword_, sizeof(joinPassword_), ImGuiInputTextFlags_Password);
                    ImGui::SameLine();
                }
                if (ui::button("Join", ui::Icon::Link, ui::Btn::Secondary)) {
                    if (locked && client && joinTarget_ != cid.value) {
                        joinTarget_ = cid.value; // ask for the password first
                    } else {
                        lobbyNotice_.clear();
                        if (client) client->joinCompany(cid.value, {}, 0xFF, joinPassword_);
                        if (host) {
                            host->switchCompany(cid);
                            showLobby_ = false;
                        }
                    }
                }
            } else if (mine) {
                ui::badge("Your company", ui::col::kGreen);
            }
            ImGui::PopID();
            ImGui::Dummy(ImVec2(0, 2.0f * s));
        }
        if (!any) ui::emptyState(ui::Icon::Briefcase, "No companies yet");
        // A new company with its own name and livery.
        ui::sectionHeader("Start a new company");
        ui::fieldLabel("Company name");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##newname", std::format("{} Transport", playerName_).c_str(), lobbyCompanyName_, sizeof(lobbyCompanyName_));
        ui::fieldLabel("Colour");
        colourPicker("##lobbycolour", companyColour_, CompanyId{});
        ImGui::Dummy(ImVec2(0, 4.0f * s));
        const u8 colour = companyColour_ < 0 ? 0xFF : static_cast<u8>(companyColour_);
        const bool pending = client && client->joinPending();
        ImGui::BeginDisabled(pending);
        if (ui::button(pending ? "Founding..." : "Found company", ui::Icon::Star, ui::Btn::Primary, ImVec2(ui::columnWidth(2), 0))) {
            lobbyNotice_.clear();
            if (client) client->joinCompany(net::kJoinNewCompany, lobbyCompanyName_, colour);
            if (host) {
                host->foundCompany(lobbyCompanyName_[0] ? lobbyCompanyName_ : std::format("{} Transport", playerName_), colour);
                showLobby_ = false;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ui::button("Watch as a spectator", ui::Icon::Eye, ui::Btn::Ghost, ImVec2(-1.0f, 0))) {
            if (client) client->joinCompany(net::kJoinSpectator);
            if (host) host->switchCompany({});
            showLobby_ = false;
        }
    }
    ui::endPanel();
    if (!open) showLobby_ = false;
    // Joined: close the lobby.
    if (client && !client->choosingCompany() && !client->joinPending() && forced) showLobby_ = false;
}

void App::drawPlayersWindow() {
    if (!showClients_) return;
    auto* client = netClient();
    auto* host = netHost();
    World& w = world();
    const float s = uiScale_;
    const std::vector<net::ClientInfo> all = players();
    const u16 me = localClientId();
    ui::PanelSpec spec;
    spec.pos = ImVec2(ImGui::GetMainViewport()->WorkPos.x + 20.0f * s, ImGui::GetMainViewport()->WorkPos.y + 80.0f * s);
    spec.width = 470.0f;
    spec.accent = ui::col::kBlue;
    const std::string sub = std::format("{} player{} connected{}", all.size(), all.size() == 1 ? "" : "s",
                                        host ? " - you are the host" : "");
    spec.subtitle = sub.c_str();
    if (ui::beginPanel("##clients", "Players", ui::Icon::People, &showClients_, spec)) {
        // Players grouped by company, then the spectators.
        std::vector<CompanyId> groups;
        for (const auto [cid, c] : w.companies)
            if (std::ranges::any_of(all, [&](const net::ClientInfo& p) { return p.company == cid; })) groups.push_back(cid);
        groups.push_back(CompanyId{});
        for (const CompanyId cid : groups) {
            std::vector<const net::ClientInfo*> members;
            for (const auto& p : all)
                if (p.company == cid) members.push_back(&p);
            if (members.empty()) continue;
            const Company* c = w.companies.tryGet(cid);
            colourChip(c ? ui::rgb(companyColourRgb(c->colour)) : ui::col::kTextMuted, 14.0f * s);
            ImGui::SameLine(0, 8.0f * s);
            ImGui::TextUnformatted(c ? c->name.c_str() : "Spectators");
            if (c && companyLocked(cid)) {
                ImGui::SameLine();
                ui::badge("Password", ui::col::kOrange);
            }
            if (members.size() > 1 && c) {
                ImGui::SameLine();
                ui::badge("Co-op", ui::col::kGreen);
            }
            for (const net::ClientInfo* p : members) {
                ImGui::PushID(p->id);
                ImGui::Indent(22.0f * s);
                const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
                const std::string label = std::format("{}{}{}", p->name, p->id == me ? "  (you)" : "", p->id == kLocalClientId ? "  [host]" : "");
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(label.c_str());
                if (p->id != me) {
                    // Message (and, for the host, Kick) at the right of the row.
                    const float message = 104.0f * s, kick = 76.0f * s, gap = ImGui::GetStyle().ItemSpacing.x;
                    ImGui::SameLine(right - message - (host ? gap + kick : 0.0f));
                    if (ui::button("Message", ui::Icon::Chat, ui::Btn::Ghost, ImVec2(message, 0))) {
                        chatScope_ = static_cast<int>(net::ChatScope::Private);
                        chatTarget_ = p->id;
                        showChat_ = true;
                    }
                    if (host) {
                        ImGui::SameLine();
                        if (ui::button("Kick", ui::Icon::Close, ui::Btn::Danger, ImVec2(kick, 0))) host->net().kick(p->id, "Removed by the host");
                    }
                }
                ImGui::Unindent(22.0f * s);
                ImGui::PopID();
            }
            ImGui::Dummy(ImVec2(0, 3.0f * s));
        }
        // Co-op: lock your company so only friends who know the password can join it.
        if (w.companies.contains(session_->localCompany())) {
            ui::sectionHeader("Co-op");
            const bool locked = companyLocked(session_->localCompany());
            ui::FontScope f(ui::fonts().small);
            ui::mutedText(locked ? "Your company needs a password to join." : "Anyone can join your company and play with you.");
            ImGui::SetNextItemWidth(220.0f * s);
            ImGui::InputTextWithHint("##companypw", "Company password", companyPassword_, sizeof(companyPassword_), ImGuiInputTextFlags_Password);
            ImGui::SameLine();
            if (ui::button(locked ? "Change" : "Lock", ui::Icon::Check, ui::Btn::Secondary) && companyPassword_[0]) {
                if (client) client->setCompanyPassword(companyPassword_);
                if (host) host->net().setCompanyPassword(session_->localCompany(), companyPassword_);
                companyPassword_[0] = 0;
            }
            if (locked) {
                ImGui::SameLine();
                if (ui::button("Unlock", ui::Icon::Close, ui::Btn::Ghost)) {
                    if (client) client->setCompanyPassword({});
                    if (host) host->net().setCompanyPassword(session_->localCompany(), {});
                }
            }
        }
        ImGui::Dummy(ImVec2(0, 4.0f * s));
        if (ui::button("Change company", ui::Icon::People, ui::Btn::Secondary, ImVec2(-1.0f, 0))) showLobby_ = true;
    }
    ui::endPanel();
}

void App::drawPlayerCursors(std::vector<gfx::OverlayQuad>& overlays) {
    auto* client = netClient();
    auto* host = netHost();
    if (!client && !host) return;
    // Share where this player points.
    std::optional<net::CursorInfo> mine;
    if (hover_.hit && !mouseOverUi_)
        mine = net::CursorInfo{0, static_cast<i32>(std::lround(hover_.point.x * net::kCursorScale)),
                               static_cast<i32>(std::lround(hover_.point.y * net::kCursorScale)),
                               static_cast<i32>(std::lround(hover_.point.z * net::kCursorScale))};
    if (client) client->setCursor(mine);
    if (host) host->net().setLocalCursor(mine);
    // Everyone else's, eased between the updates that arrive ten times a second.
    const std::vector<net::CursorInfo> others = client ? client->cursors() : host->net().cursors();
    const std::vector<net::ClientInfo> all = players();
    const u16 me = localClientId();
    const double ease = 1.0 - std::exp(-ImGui::GetIO().DeltaTime * 14.0);
    std::erase_if(cursorDisplay_, [&](const auto& kv) {
        return std::ranges::none_of(others, [&](const net::CursorInfo& c) { return c.client == kv.first; });
    });
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float s = uiScale_;
    for (const net::CursorInfo& c : others) {
        if (c.client == me) continue;
        const glm::dvec3 target(c.x / static_cast<double>(net::kCursorScale), c.y / static_cast<double>(net::kCursorScale),
                                c.z / static_cast<double>(net::kCursorScale));
        auto [it, fresh] = cursorDisplay_.try_emplace(c.client, target);
        if (!fresh) it->second += (target - it->second) * ease;
        const glm::dvec3 at = it->second;
        const auto who = std::ranges::find_if(all, [&](const net::ClientInfo& p) { return p.id == c.client; });
        if (who == all.end()) continue;
        const ImU32 colour = colourOf(world(), who->company);
        const ImVec4 cv = ImGui::ColorConvertU32ToFloat4(colour);
        mapdraw::addRing(overlays, at + glm::dvec3(0.0, 0.03, 0.0), 0.22 * std::max(1.0, camera_.distance / 30.0),
                         gfx::rgba(static_cast<u8>(cv.x * 255), static_cast<u8>(cv.y * 255), static_cast<u8>(cv.z * 255), 220));
        const auto sp = worldToScreen(at);
        if (!sp) continue;
        // A pointer in the player's company colour, with their name beside it.
        const ImVec2 p(sp->x, sp->y);
        const ImVec2 arrow[] = {p, ImVec2(p.x, p.y + 20.0f * s), ImVec2(p.x + 5.0f * s, p.y + 15.0f * s), ImVec2(p.x + 9.0f * s, p.y + 23.0f * s),
                                ImVec2(p.x + 12.0f * s, p.y + 21.5f * s), ImVec2(p.x + 8.5f * s, p.y + 13.5f * s), ImVec2(p.x + 15.0f * s, p.y + 13.5f * s)};
        dl->AddConcavePolyFilled(arrow, 7, colour);
        dl->AddPolyline(arrow, 7, IM_COL32(20, 22, 26, 230), ImDrawFlags_Closed, 1.6f * s);
        ImFont* font = ui::fonts().small ? ui::fonts().small : ImGui::GetFont();
        const ImVec2 size = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, who->name.c_str());
        const ImVec2 tag(p.x + 16.0f * s, p.y + 18.0f * s);
        dl->AddRectFilled(tag, ImVec2(tag.x + size.x + 12.0f * s, tag.y + size.y + 6.0f * s), colour, 6.0f * s);
        dl->AddText(font, font->FontSize, ImVec2(tag.x + 6.0f * s, tag.y + 3.0f * s), IM_COL32(15, 17, 20, 255), who->name.c_str());
    }
}

void App::updateBots(double dt) {
    if (bots_.empty()) return;
    auto* host = netHost();
    const double t = timeSeconds_;
    for (usize i = 0; i < bots_.size(); ++i) {
        net::NetClientSession& bot = *bots_[i];
        bot.update(dt);
        if (!bot.ready()) continue;
        // The first bot plays in the host's company (co-op); the others found their own.
        if (bot.choosingCompany() && !bot.joinPending()) {
            if (i == 0 && host && session_->localCompany().valid()) bot.joinCompany(session_->localCompany().value);
            else bot.joinCompany(net::kJoinNewCompany, std::format("{} Railways", i == 1 ? "Bluebell" : "Ridgeway"),
                                 static_cast<u8>(3 + i * 4));
        }
        // Wander around the view so their pointers show.
        const double a = t * (0.6 + 0.17 * static_cast<double>(i)) + static_cast<double>(i) * 2.1;
        const double r = 4.0 + 3.0 * static_cast<double>(i);
        const double x = camera_.target.x + std::cos(a) * r, z = camera_.target.z + std::sin(a * 1.3) * r * 0.7;
        bot.setCursor(net::CursorInfo{0, static_cast<i32>(x * net::kCursorScale),
                                      static_cast<i32>(gfx::terrainHeightAt(world().map, x, z) * net::kCursorScale),
                                      static_cast<i32>(z * net::kCursorScale)});
        if (!botsGreeted_ && bot.localCompany().valid() && i + 1 == bots_.size()) {
            botsGreeted_ = true;
            bots_[0]->sendChat("Hi! I've joined your company, let's build a railway.", net::ChatScope::Company);
            bot.sendChat("Good luck everyone, may the best company win!");
        }
    }
}

} // namespace tp
