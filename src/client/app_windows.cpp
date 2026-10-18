// Information windows: depots, vehicles, stations, towns, finances.

#include "client/app.hpp"

#include <imgui.h>

#include "core/calendar.hpp"
#include "render/terrain_mesher.hpp"
#include "render/vehicle_pose.hpp"

#include <algorithm>
#include <format>
#include <string>

namespace tp {

namespace {

i32 displayKmh(i32 units) { return units * 100 / 80; }

void moneyText(const char* label, Money m) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(170);
    ImGui::TextColored(m < 0 ? ImVec4(1.0f, 0.5f, 0.45f, 1.0f) : ImVec4(0.65f, 1.0f, 0.65f, 1.0f), "%s",
                       formatMoney(m).c_str());
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
    camera_.targetGoal = glm::dvec3(t.x + 0.5, gfx::terrainHeightAt(world().map, t.x + 0.5, t.y + 0.5), t.y + 0.5);
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
}

namespace {

ImVec4 newsColour(NewsKind k) {
    switch (k) {
    case NewsKind::Town: return {0.6f, 0.9f, 0.6f, 1.0f};
    case NewsKind::Company: return {1.0f, 0.85f, 0.45f, 1.0f};
    case NewsKind::Vehicle: return {0.6f, 0.8f, 1.0f, 1.0f};
    case NewsKind::Finance: return {1.0f, 0.6f, 0.5f, 1.0f};
    default: return {0.9f, 0.9f, 0.9f, 1.0f};
    }
}

} // namespace

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
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 10.0f, vp->WorkPos.y + vp->WorkSize.y - 10.0f), ImGuiCond_Always,
                            ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(260, 0), ImVec2(420, 400));
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::Begin("##newsbanner", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::TextDisabled("NEWS  %s", formatDate(w.clock.startDay + static_cast<i32>(newsBanner_->tick / kTicksPerDay)).c_str());
    ImGui::PushTextWrapPos(400.0f);
    ImGui::TextColored(newsColour(newsBanner_->kind), "%s", newsBanner_->text.c_str());
    ImGui::PopTextWrapPos();
    if (newsBanner_->location) {
        if (ImGui::SmallButton("Go to")) focusOn(*newsBanner_->location);
        ImGui::SameLine();
    }
    if (ImGui::SmallButton("History")) showNews_ = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("Dismiss")) newsBanner_.reset();
    ImGui::End();
}

void App::drawNewsWindow() {
    if (!showNews_) return;
    const World& w = world();
    ImGui::SetNextWindowSize(ImVec2(460, 360), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(20, 140), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("News", &showNews_)) {
        if (w.news.empty()) ImGui::TextDisabled("Nothing has happened yet");
        int n = 0;
        for (auto it = w.news.rbegin(); it != w.news.rend() && n < 100; ++it, ++n) {
            ImGui::PushID(n);
            ImGui::TextDisabled("%s", formatDate(w.clock.startDay + static_cast<i32>(it->tick / kTicksPerDay)).c_str());
            ImGui::SameLine();
            if (it->location) {
                if (ImGui::SmallButton("Go to")) focusOn(*it->location);
                ImGui::SameLine();
            }
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(newsColour(it->kind), "%s", it->text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopID();
        }
    }
    ImGui::End();
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
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float size = static_cast<float>(gfx::Minimap::kSize) * uiScale_;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 10.0f, vp->WorkPos.y + vp->WorkSize.y - 10.0f),
                            ImGuiCond_FirstUseEver, ImVec2(1.0f, 1.0f));
    if (ImGui::Begin("Map", &showMinimap_, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar)) {
        if (ImGui::SmallButton("-") && minimapScale_ < 8) {
            minimapScale_ *= 2;
            minimapUpdated_ = -1.0;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+") && minimapScale_ > 1) {
            minimapScale_ /= 2;
            minimapUpdated_ = -1.0;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%d tiles across - click to jump", gfx::Minimap::kSize * minimapScale_);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Image(static_cast<ImTextureID>(minimap_.textureId()), ImVec2(size, size));
        const float k = size / static_cast<float>(gfx::Minimap::kSize);
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 m = ImGui::GetMousePos();
            focusOn(minimap_.tileAt((m.x - at.x) / k, (m.y - at.y) / k));
        }
        // The camera position, and town names when zoomed out.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 c(at.x + static_cast<float>(minimap_.pixelX(cam.x)) * k,
                       at.y + static_cast<float>(minimap_.pixelY(cam.y)) * k);
        const float r = std::max(4.0f, static_cast<float>(camera_.distance / minimapScale_) * k * 0.6f);
        dl->PushClipRect(at, ImVec2(at.x + size, at.y + size), true);
        dl->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), IM_COL32(255, 255, 255, 220), 0.0f, 0, 1.5f);
        for (const auto [id, t] : w.towns) {
            const ImVec2 p(at.x + static_cast<float>(minimap_.pixelX(t.centre.x + 0.5)) * k,
                           at.y + static_cast<float>(minimap_.pixelY(t.centre.y + 0.5)) * k);
            if (p.x < at.x || p.y < at.y || p.x > at.x + size || p.y > at.y + size) continue;
            const ImVec2 ts = ImGui::CalcTextSize(t.name.c_str());
            dl->AddText(ImVec2(p.x - ts.x * 0.5f + 1, p.y - ts.y * 0.5f + 1), IM_COL32(0, 0, 0, 200), t.name.c_str());
            dl->AddText(ImVec2(p.x - ts.x * 0.5f, p.y - ts.y * 0.5f), IM_COL32(255, 255, 255, 235), t.name.c_str());
        }
        dl->PopClipRect();
    }
    ImGui::End();
}

void App::drawTownList() {
    if (!showTownList_) return;
    const World& w = world();
    ImGui::SetNextWindowSize(ImVec2(470, 380), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(500, 140), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Towns", &showTownList_)) {
        std::vector<TownId> ids;
        for (const auto [id, t] : w.towns) ids.push_back(id);
        std::ranges::sort(ids, [&](TownId a, TownId b) { return w.towns[a].population > w.towns[b].population; });
        i64 total = 0;
        for (const TownId id : ids) total += w.towns[id].population;
        ImGui::Text("%zu towns, %lld people", ids.size(), static_cast<long long>(total));
        if (ImGui::BeginTable("towns", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Town", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Population");
            ImGui::TableSetupColumn("Transit");
            ImGui::TableSetupColumn("Your rating");
            ImGui::TableHeadersRow();
            for (const TownId id : ids) {
                const Town& t = w.towns[id];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const std::string label = std::format("{} ({})##{}", t.name, townClassName(t.cls), id.value);
                if (ImGui::Selectable(label.c_str(), selTown_ == id)) {
                    selTown_ = id;
                    focusOn(t.centre);
                }
                ImGui::TableNextColumn();
                ImGui::Text("%d", t.population);
                ImGui::TableNextColumn();
                const TripStats& m = t.lastMonth.trips > 0 ? t.lastMonth : t.thisMonth;
                if (m.trips > 0)
                    ImGui::Text("%d%%", m.transit * 100 / m.trips);
                else
                    ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(ratingName(towns::rating(t, session_->localCompany())).data());
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void App::drawDepotWindow() {
    World& w = world();
    const Depot* d = w.depots.tryGet(selDepot_);
    if (!d) {
        selDepot_ = {};
        return;
    }
    bool open = true;
    const Town* town = w.towns.tryGet(d->town);
    const char* kind = d->kind == DepotKind::Tram   ? "Tram depot"
                       : d->kind == DepotKind::Rail ? "Train depot"
                       : d->kind == DepotKind::Ship ? "Shipyard"
                                                    : "Bus depot";
    const std::string title = std::format("{} - {}###depot", kind, town ? town->name : std::string("countryside"));
    const bool rail = d->kind == DepotKind::Rail;
    const i32 year = w.clock.date().year;
    const auto models = vehicleModels();
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(20, 200), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(title.c_str(), &open)) {
        const bool mine = d->owner == session_->localCompany();
        ImGui::SeparatorText("Vehicles inside");
        int inside = 0;
        for (const auto [id, v] : w.vehicles) {
            if (v.depot != selDepot_ || v.state != VehicleState::InDepot) continue;
            ++inside;
            ImGui::PushID(static_cast<int>(id.value));
            ImGui::Text("%s  (%s)", vehicleName(v).c_str(), v.info().name.data());
            if (mine) {
                ImGui::SameLine();
                if (ImGui::SmallButton(v.stopped ? "Start" : "Stop")) session_->submit(CmdStartStopVehicle{id});
                ImGui::SameLine();
                if (ImGui::SmallButton("Orders")) selVehicle_ = id;
                ImGui::SameLine();
                if (ImGui::SmallButton("Sell")) session_->submit(CmdSellVehicle{id});
            }
            if (rail) {
                // Consist: the engine followed by its carriages; carriages can be detached.
                ImGui::Indent();
                for (usize k = 0; k < v.cars.size(); ++k) {
                    const VehicleModel& cm = vehicleModel(v.cars[k]);
                    ImGui::PushID(static_cast<int>(k));
                    if (k > 0) ImGui::SameLine();
                    ImGui::TextColored(k == 0 ? ImVec4(1.0f, 0.85f, 0.5f, 1.0f) : ImVec4(0.8f, 0.85f, 0.9f, 1.0f), "[%s]",
                                       cm.name.data());
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%d seats, %d km/h", cm.capacity, cm.maxSpeedKmh);
                    if (mine && k > 0 && v.stopped) {
                        ImGui::SameLine(0, 2);
                        if (ImGui::SmallButton("x")) session_->submit(CmdRemoveCarriage{id, static_cast<u16>(k)});
                    }
                    ImGui::PopID();
                }
                ImGui::Text("%u seats", v.capacity());
                if (mine && v.stopped) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Add carriage")) ImGui::OpenPopup("addcar");
                    if (ImGui::BeginPopup("addcar")) {
                        for (u16 i = 0; i < models.size(); ++i) {
                            const VehicleModel& m = models[i];
                            if (m.type != VehicleType::Rail || m.powered || m.introYear > year) continue;
                            const std::string item = std::format("{}  ({} seats, {} km/h)  {}", m.name, m.capacity,
                                                                 m.maxSpeedKmh, formatMoney(m.price));
                            if (ImGui::Selectable(item.c_str())) session_->submit(CmdAddCarriage{id, i});
                        }
                        ImGui::EndPopup();
                    }
                } else if (mine) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(stop the train to change carriages)");
                }
                ImGui::Unindent();
            }
            ImGui::PopID();
        }
        if (inside == 0) ImGui::TextDisabled("No vehicles inside");
        if (mine) {
            ImGui::SeparatorText(rail ? "Buy a new engine" : "Buy a new vehicle");
            const VehicleType want = d->kind == DepotKind::Tram   ? VehicleType::Tram
                                     : d->kind == DepotKind::Rail ? VehicleType::Rail
                                     : d->kind == DepotKind::Ship ? VehicleType::Ship
                                                                  : VehicleType::Road;
            if (ImGui::BeginTable("models", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("Model");
                ImGui::TableSetupColumn("Seats");
                ImGui::TableSetupColumn("Speed");
                ImGui::TableSetupColumn("Price");
                ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();
                for (u16 i = 0; i < models.size(); ++i) {
                    const VehicleModel& m = models[i];
                    if (m.type != want || m.introYear > year || (rail && !m.powered)) continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(m.name.data());
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Running cost %s / year\nIntroduced %d, life %d years",
                                          formatMoney(m.runningCost).c_str(), m.introYear, m.lifeYears);
                    ImGui::TableNextColumn();
                    if (m.capacity > 0)
                        ImGui::Text("%d", m.capacity);
                    else
                        ImGui::TextDisabled("-");
                    ImGui::TableNextColumn();
                    ImGui::Text("%d km/h", m.maxSpeedKmh);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(formatMoney(m.price).c_str());
                    ImGui::TableNextColumn();
                    ImGui::PushID(i);
                    if (ImGui::SmallButton("Buy")) pendingBuySeq_ = session_->submit(CmdBuyVehicle{selDepot_, i});
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (rail) ImGui::TextDisabled("Carriages are added to a stopped train inside the depot.");
        }
    }
    ImGui::End();
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
    bool open = true;
    const std::string title = std::format("{}###vehicle", vehicleName(*v));
    // Off to the side, so that a followed vehicle in the middle of the screen stays visible.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 460.0f, vp->WorkPos.y + 60.0f),
                            ImGuiCond_FirstUseEver);
    if (ImGui::Begin(title.c_str(), &open)) {
        const VehicleModel& m = v->info();
        const bool mine = v->owner == session_->localCompany();
        ImGui::Text("%s", m.name.data());
        ImGui::TextColored(v->lost ? ImVec4(1, 0.5f, 0.4f, 1) : ImVec4(0.8f, 0.9f, 1.0f, 1), "%s",
                           vehicleStatus(*v).c_str());
        ImGui::Text("Speed %d km/h   Passengers %zu / %u", displayKmh(v->speed), v->passengers.size(), v->capacity());
        moneyText("Profit this year", v->profitThisYear);
        moneyText("Profit last year", v->profitLastYear);
        ImGui::Text("Passengers this year: %d   Value: %s", v->passengersThisYear, formatMoney(v->value).c_str());
        {
            const i32 age = vehicles::ageDays(w, *v);
            const bool old = age > m.lifeYears * 365;
            ImGui::TextColored(old ? ImVec4(1.0f, 0.55f, 0.45f, 1.0f) : ImVec4(1, 1, 1, 1), "Age: %d years (life %d)",
                               age / 365, m.lifeYears);
            ImGui::SameLine();
            const float rel = v->reliability / 100.0f;
            ImGui::TextColored(rel < 50.0f ? ImVec4(1.0f, 0.55f, 0.45f, 1.0f) : ImVec4(0.75f, 1.0f, 0.75f, 1.0f),
                               "  Reliability: %.0f%%", rel);
            ImGui::Text("Breakdowns: %d   Last serviced %s", v->breakdownsSinceService,
                        formatDate(w.clock.startDay + static_cast<i32>(v->lastServiceTick / kTicksPerDay)).c_str());
        }
        if (mine) {
            if (ImGui::Button(v->stopped ? "Start" : "Stop")) session_->submit(CmdStartStopVehicle{selVehicle_});
            ImGui::SameLine();
            if (ImGui::Button(v->sendToDepot ? "Cancel depot" : "Send to depot")) session_->submit(CmdSendToDepot{selVehicle_});
            ImGui::SameLine();
            if (ImGui::Button("Skip order")) session_->submit(CmdSkipOrder{selVehicle_});
            ImGui::SameLine();
        }
        const bool following = followVehicle_ == selVehicle_;
        if (ImGui::Button(following ? "Unfollow" : "Follow")) followVehicle_ = following ? VehicleId{} : selVehicle_;
        if (mine && v->state == VehicleState::InDepot && v->stopped) {
            ImGui::SameLine();
            if (ImGui::Button("Sell")) session_->submit(CmdSellVehicle{selVehicle_});
        }
        if (mine && w.depots.contains(v->depot)) {
            if (ImGui::Button("Clone")) pendingBuySeq_ = session_->submit(CmdCloneVehicle{v->depot, selVehicle_, false});
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Buy a copy in its depot with a copy of the orders");
            ImGui::SameLine();
            if (ImGui::Button("Clone with shared orders"))
                pendingBuySeq_ = session_->submit(CmdCloneVehicle{v->depot, selVehicle_, true});
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Buy a copy that shares this vehicle's order list");
        }

        ImGui::SeparatorText("Orders");
        if (v->orderGroup != 0) {
            int sharing = 0;
            for (const auto [oid, o] : w.vehicles)
                if (oid != selVehicle_ && o.orderGroup == v->orderGroup && o.owner == v->owner) ++sharing;
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Shared with %d other vehicle%s", sharing,
                               sharing == 1 ? "" : "s");
            if (mine) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Unshare")) session_->submit(CmdUnshareOrders{selVehicle_});
            }
        }
        std::vector<Order> orders = v->orders;
        bool changed = false;
        int remove = -1, moveUp = -1;
        for (usize i = 0; i < orders.size(); ++i) {
            Order& o = orders[i];
            ImGui::PushID(static_cast<int>(i));
            const bool current = i == v->orderIndex % std::max<usize>(1, orders.size());
            std::string label;
            if (o.kind == OrderKind::Station) {
                const Station* s = w.stations.tryGet(o.station);
                label = std::format("{}. Go to {}", i + 1, s ? s->name : std::string("(removed)"));
            } else {
                label = std::format("{}. Service at depot", i + 1);
            }
            if (o.flags & kOrderFullLoad) label += "  [full load]";
            ImGui::TextColored(current ? ImVec4(1.0f, 0.85f, 0.4f, 1.0f) : ImVec4(1, 1, 1, 1), "%s", label.c_str());
            if (ImGui::IsItemClicked() && o.kind == OrderKind::Station)
                if (const Station* s = w.stations.tryGet(o.station)) focusOn(s->sign);
            if (mine) {
                ImGui::SameLine();
                if (o.kind == OrderKind::Station && ImGui::SmallButton("Full load")) {
                    o.flags ^= kOrderFullLoad;
                    changed = true;
                }
                ImGui::SameLine();
                if (i > 0 && ImGui::SmallButton("Up")) moveUp = static_cast<int>(i);
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) remove = static_cast<int>(i);
            }
            ImGui::PopID();
        }
        if (orders.empty()) ImGui::TextDisabled("No orders - add some stations");
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
            if (ImGui::Button(picking ? "Click a station on the map... (Esc to finish)" : "Add stop / depot"))
                tool_ = picking ? Tool::None : Tool::PickOrder;
        }
    }
    ImGui::End();
    if (!open) {
        if (followVehicle_ == selVehicle_) followVehicle_ = {};
        selVehicle_ = {};
        if (tool_ == Tool::PickOrder) tool_ = Tool::None;
    }
}

void App::drawStationWindow() {
    World& w = world();
    const Station* s = w.stations.tryGet(selStation_);
    if (!s) {
        selStation_ = {};
        return;
    }
    bool open = true;
    const std::string title = std::format("{}###station", s->name);
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(880, 200), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(title.c_str(), &open)) {
        const Company* c = w.companies.tryGet(s->owner);
        ImGui::Text("Owner: %s", c ? c->name.c_str() : "?");
        std::string fac;
        if (s->facilities & kFacilityBus) fac += "bus ";
        if (s->facilities & kFacilityTram) fac += "tram ";
        if (s->facilities & kFacilityRail) fac += "rail ";
        if (s->facilities & kFacilityDock) fac += "dock ";
        ImGui::Text("Stops: %s(%zu tiles), catchment %d", fac.c_str(), s->tiles.size(), s->catchment);
        ImGui::Text("Rating: %d%%", s->rating * 100 / 255);
        ImGui::Text("Boarded this month: %d, last month: %d", s->boardedThisMonth, s->boardedLastMonth);
        ImGui::SeparatorText("Waiting passengers");
        if (s->waiting.empty()) ImGui::TextDisabled("Nobody is waiting");
        for (const auto& [to, list] : s->waiting) {
            const Station* dest = w.stations.tryGet(to);
            ImGui::Text("%zu  for %s", list.size(), dest ? dest->name.c_str() : "?");
        }
        int served = 0;
        for (const auto [id, v] : w.vehicles)
            for (const Order& o : v.orders)
                if (o.kind == OrderKind::Station && o.station == selStation_) {
                    ++served;
                    break;
                }
        ImGui::Text("Served by %d vehicle(s)", served);
        if (ImGui::Button("Centre view")) focusOn(s->sign);
    }
    ImGui::End();
    if (!open) selStation_ = {};
}

void App::drawTownWindow() {
    World& w = world();
    const Town* t = w.towns.tryGet(selTown_);
    if (!t) {
        selTown_ = {};
        return;
    }
    bool open = true;
    const std::string title = std::format("{}###town", t->name);
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(880, 420), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(title.c_str(), &open)) {
        ImGui::Text("%s, founded %s", townClassName(t->cls).data(),
                    formatDate(w.clock.startDay + static_cast<i32>(t->foundedTick / kTicksPerDay)).c_str());
        ImGui::Text("Population: %d", t->population);
        ImGui::Text("Jobs: %d (filled %d)   Buildings: %zu", t->jobs, t->workers, t->buildings.size());
        ImGui::Text("Shopping capacity: %d   Unemployed: %zu", t->retailCapacity, t->unemployed.size());
        if (const Town* st = w.towns.tryGet(t->shopTown); st && st != t)
            ImGui::Text("Residents shop in %s", st->name.c_str());
        const TripStats& m = t->lastMonth.trips > 0 ? t->lastMonth : t->thisMonth;
        ImGui::SeparatorText(t->lastMonth.trips > 0 ? "Trips last month" : "Trips this month");
        if (m.trips > 0) {
            const auto pct = [&](i32 n) { return n * 100.0f / static_cast<float>(m.trips); };
            ImGui::Text("Total %d", m.trips);
            ImGui::Text("Walked %.0f%%  Drove %.0f%%", pct(m.walked), pct(m.drove));
            ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "Public transport %.0f%%", pct(m.transit));
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.5f, 1.0f), "Needed a lift (no service) %.0f%%", pct(m.lifts));
        } else {
            ImGui::TextDisabled("No trips yet");
        }
        ImGui::SeparatorText("Local authority");
        if (t->servedStations > 0) ImGui::Text("Stations served last month: %d", t->servedStations);
        bool any = false;
        for (const auto [cid, c] : w.companies) {
            const i32 r = towns::rating(*t, cid);
            if (!t->ratings.contains(cid.value) && cid != session_->localCompany()) continue;
            any = true;
            const float k = static_cast<float>(r - kRatingMin) / static_cast<float>(kRatingMax - kRatingMin);
            ImGui::Text("%s:", c.name.c_str());
            ImGui::SameLine(170);
            ImGui::TextColored(ImVec4(1.0f - k * 0.6f, 0.4f + k * 0.6f, 0.4f, 1.0f), "%s (%d)", ratingName(r).data(), r);
        }
        if (!any) ImGui::TextDisabled("No companies rated yet");
        if (ImGui::Button("Centre view")) focusOn(t->centre);
    }
    ImGui::End();
    if (!open) selTown_ = {};
}

void App::drawFinanceWindow() {
    if (!showFinances_) return;
    World& w = world();
    Company* c = w.companies.tryGet(session_->localCompany());
    if (!c) return;
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(20, 420), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Finances", &showFinances_)) {
        ImGui::Text("%s", c->name.c_str());
        moneyText("Bank balance", c->money);
        moneyText("Loan", -c->loan);
        if (ImGui::Button("Borrow $100,000")) session_->submit(CmdSetLoan{100'000});
        ImGui::SameLine();
        if (ImGui::Button("Repay $100,000")) session_->submit(CmdSetLoan{-100'000});
        ImGui::SeparatorText(std::format("{}", c->thisYear.year).c_str());
        for (usize i = 0; i < kExpenseCount; ++i) {
            const Money m = c->thisYear.amounts[i];
            if (m == 0) continue;
            moneyText(std::string(expenseName(static_cast<Expense>(i))).c_str(), m);
        }
        moneyText("Total", c->thisYear.total());
        ImGui::Text("Passengers carried this year: %lld", static_cast<long long>(c->passengersThisYear));
        if (!c->history.empty()) {
            const YearLedger& last = c->history.back();
            ImGui::SeparatorText(std::format("{}", last.year).c_str());
            moneyText("Total", last.total());
        }
    }
    ImGui::End();
}

void App::drawVehicleList() {
    if (!showVehicleList_) return;
    World& w = world();
    ImGui::SetNextWindowSize(ImVec2(520, 320), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(20, 640), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Vehicles", &showVehicleList_)) {
        if (ImGui::BeginTable("vehicles", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Vehicle");
            ImGui::TableSetupColumn("Status");
            ImGui::TableSetupColumn("Profit this year");
            ImGui::TableSetupColumn("Last year");
            ImGui::TableHeadersRow();
            for (const auto [id, v] : w.vehicles) {
                if (v.owner != session_->localCompany()) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(id.value));
                if (ImGui::Selectable(vehicleName(v).c_str(), selVehicle_ == id, ImGuiSelectableFlags_SpanAllColumns))
                    selVehicle_ = id;
                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(vehicleStatus(v).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(formatMoney(v.profitThisYear).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(formatMoney(v.profitLastYear).c_str());
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

} // namespace tp
