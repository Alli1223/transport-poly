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
    if (v.state == VehicleState::Loading) {
        const Order* o = v.currentOrder();
        return o ? std::format("Loading at {}", stationName(o->station)) : "Loading";
    }
    if (v.sendToDepot) return "Heading for depot";
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
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(460, 200), ImGuiCond_FirstUseEver);
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

        ImGui::SeparatorText("Orders");
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
