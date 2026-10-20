// Inspecting the map: hovering and clicking vehicles and people, the card showing where a person is
// going and how they get there, and their route drawn on the map.

#include "client/app.hpp"
#include "client/map_draw.hpp"
#include "client/ui/widgets.hpp"

#include <imgui.h>

#include "core/calendar.hpp"
#include "core/names.hpp"
#include "render/person_pose.hpp"
#include "render/terrain_mesher.hpp"
#include "render/vehicle_pose.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <map>
#include <string>

namespace tp {

namespace {

using mapdraw::addRibbon;
using mapdraw::addRing;
using mapdraw::groundLeg;
using mapdraw::groundPoint;

// One rigid body of a vehicle: a train car, a tram section, a bus or a ship.
struct Body {
    gfx::VehiclePose pose;
    double half;   // half length, tiles
    double width;  // half width, tiles
    double height; // tiles
};

void vehicleBodies(const World& w, const Vehicle& v, double fraction, std::vector<Body>& out) {
    out.clear();
    const VehicleType type = v.info().type;
    if (type == VehicleType::Rail) {
        std::vector<gfx::CarPose> cars;
        gfx::trainCarPoses(w, v, fraction, cars);
        for (const gfx::CarPose& car : cars) out.push_back({car.pose, vehicleModel(car.model).length / 32.0, 0.1, 0.17});
    } else if (const auto pose = gfx::vehiclePose(w, v, fraction)) {
        const bool ship = type == VehicleType::Ship;
        out.push_back({*pose, v.info().length / 32.0, ship ? 0.2 : 0.08, ship ? 0.22 : 0.14});
    }
}

glm::dvec3 forwardOf(const gfx::VehiclePose& p) {
    return {std::cos(p.yaw) * std::cos(p.pitch), std::sin(p.pitch), -std::sin(p.yaw) * std::cos(p.pitch)};
}
glm::dvec3 sideOf(const gfx::VehiclePose& p) { return {std::sin(p.yaw), 0.0, std::cos(p.yaw)}; }

// Distance in pixels from `p` to the segment a-b.
float segmentDistance(ImVec2 p, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 q(p.x, p.y), ab = b - a;
    const float len2 = glm::dot(ab, ab);
    const float t = len2 > 1e-6f ? std::clamp(glm::dot(q - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
    return glm::length(q - (a + ab * t));
}

// A rectangular frame around a body, lying in its floor plane, and a faint fill inside.
void addFrame(std::vector<gfx::OverlayQuad>& overlays, const Body& b, u32 edge, u32 fill) {
    const glm::dvec3 f = forwardOf(b.pose), s = sideOf(b.pose);
    const glm::dvec3 c = b.pose.pos + glm::dvec3(0.0, 0.015, 0.0);
    const double l = b.half + 0.05, wd = b.width + 0.05, t = 0.022;
    const auto at = [&](double along, double across) { return c + f * along + s * across; };
    overlays.push_back({{at(-l, -wd), at(l, -wd), at(l, wd), at(-l, wd)}, fill, true});
    overlays.push_back({{at(-l, -wd), at(l, -wd), at(l, -wd + t), at(-l, -wd + t)}, edge, true});
    overlays.push_back({{at(-l, wd - t), at(l, wd - t), at(l, wd), at(-l, wd)}, edge, true});
    overlays.push_back({{at(-l, -wd), at(-l + t, -wd), at(-l + t, wd), at(-l, wd)}, edge, true});
    overlays.push_back({{at(l - t, -wd), at(l, -wd), at(l, wd), at(l - t, wd)}, edge, true});
}

const Building* resolveBuilding(const World& w, BuildingRef ref) {
    const Building* b = w.buildings.tryGet(ref.id);
    return b && b->serial == ref.serial ? b : nullptr;
}

ui::Icon vehicleIconFor(VehicleType t) {
    switch (t) {
    case VehicleType::Road: return ui::Icon::Bus;
    case VehicleType::Tram: return ui::Icon::Tram;
    case VehicleType::Rail: return ui::Icon::Train;
    case VehicleType::Ship: return ui::Icon::Ship;
    }
    return ui::Icon::Bus;
}

std::string personName(const Person& p) {
    return generatePersonFirstName(p.nameSeed) + " " + generatePersonSurname(p.nameSeed ^ 0x9E3779B9u);
}

// Where a person is going (or will go next) and why.
struct Trip {
    const Building* dest = nullptr;
    const char* purpose = "";  // "Commuting to work"
    const char* place = "";    // "work"
    ui::Icon icon = ui::Icon::Walk;
    ImU32 colour = ui::col::kText;
};

Trip tripOf(const World& w, const Person& p) {
    Trip t;
    // At work or out shopping, the next trip is always home.
    const bool out = p.state == PersonState::AtWork || p.state == PersonState::AtShop;
    switch (out ? TripPurpose::Home : p.purpose) {
    case TripPurpose::Work:
        t = {resolveBuilding(w, p.work), "Commuting to work", "work", ui::Icon::Briefcase, ui::col::kBlue};
        break;
    case TripPurpose::Shop:
        t = {resolveBuilding(w, p.shop), "Going shopping", "the shops", ui::Icon::Cart, ui::col::kAccent};
        break;
    case TripPurpose::Home:
        t = {resolveBuilding(w, p.home), "Going home", "home", ui::Icon::Home, ui::col::kLeaf};
        break;
    default: break;
    }
    return t;
}

bool travelling(PersonState s) {
    return s != PersonState::AtHome && s != PersonState::AtWork && s != PersonState::AtShop;
}

glm::dvec3 buildingPoint(const World& w, const Building& b, double lift) {
    const TilePos c = b.centre2();
    return groundPoint(w, c.x * 0.5, c.y * 0.5, lift);
}

glm::dvec3 stationPoint(const World& w, const Station& s, double lift) {
    return groundPoint(w, s.sign.x + 0.5, s.sign.y + 0.5, lift);
}

} // namespace

VehicleId App::pickVehicle(float* screenDistance) const {
    // Each car's centre line is projected to the screen and the cursor's distance to it measured,
    // less the car's projected half width, so trains are as easy to click zoomed out as close up.
    const World& w = session_->world();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const double fraction = session_->tickFraction();
    const double reach = camera_.distance * 3.0 + 60.0;
    const float margin = 9.0f * uiScale_;
    VehicleId best;
    float bestD = FLT_MAX;
    std::vector<Body> bodies;
    for (const auto [id, v] : w.vehicles) {
        if (v.state == VehicleState::InDepot) continue;
        const double dx = v.tile.x - camera_.target.x, dz = v.tile.y - camera_.target.z;
        if (dx * dx + dz * dz > reach * reach) continue;
        vehicleBodies(w, v, fraction, bodies);
        for (const Body& b : bodies) {
            const glm::dvec3 mid = b.pose.pos + glm::dvec3(0.0, b.height * 0.5, 0.0);
            const glm::dvec3 f = forwardOf(b.pose) * b.half;
            const auto front = worldToScreen(mid + f), back = worldToScreen(mid - f), centre = worldToScreen(mid);
            if (!front || !back || !centre) continue;
            const auto side = worldToScreen(mid + sideOf(b.pose) * b.width);
            const float thick = side ? glm::length(*side - *centre) : 0.0f;
            const float d = segmentDistance(mouse, *front, *back) - thick;
            if (d < margin && d < bestD) {
                bestD = d;
                best = id;
            }
        }
    }
    if (screenDistance) *screenDistance = bestD;
    return best;
}

PersonId App::pickPerson(float* screenDistance) const {
    const World& w = session_->world();
    if (screenDistance) *screenDistance = FLT_MAX;
    if (camera_.distance > 70.0) return {}; // too small to pick out
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const double fraction = session_->tickFraction();
    // Only walks starting or ending near the view can pass through it.
    const double reach = (camera_.distance * 1.5 + 60.0) * kTrackUnits;
    const auto nearView = [&](TPoint q) {
        return std::abs(q.x - camera_.target.x * kTrackUnits) < reach && std::abs(q.z - camera_.target.z * kTrackUnits) < reach;
    };
    const float margin = 10.0f * uiScale_;
    PersonId best;
    float bestD = FLT_MAX;
    for (const auto [id, p] : w.people) {
        if (p.state != PersonState::Walking && p.state != PersonState::ToStop && p.state != PersonState::FromStop &&
            p.state != PersonState::Waiting)
            continue;
        if (p.walkPath.empty() || (!nearView(p.walkPath.front().p) && !nearView(p.walkPath.back().p))) continue;
        const auto pose = gfx::personPose(w, p, fraction);
        if (!pose || !pose->visible) continue;
        const auto feet = worldToScreen(pose->pos), head = worldToScreen(pose->pos + glm::dvec3(0.0, 0.1, 0.0));
        if (!feet || !head) continue;
        const float d = segmentDistance(mouse, *feet, *head);
        if (d < margin && d < bestD) {
            bestD = d;
            best = id;
        }
    }
    if (screenDistance) *screenDistance = bestD;
    return best;
}

void App::updateHover() {
    hoverVehicle_ = {};
    hoverPerson_ = {};
    if (mouseOverUi_ || tool_ != Tool::None) return;
    float vehicleD = FLT_MAX, personD = FLT_MAX;
    const VehicleId v = pickVehicle(&vehicleD);
    const PersonId p = pickPerson(&personD);
    // People are small: one right under the cursor wins over a vehicle beside them.
    if (p.valid() && (!v.valid() || personD < std::max(vehicleD, 0.0f) + 3.0f * uiScale_)) hoverPerson_ = p;
    else hoverVehicle_ = v;
    if (!hoverPerson_.valid() && !hoverVehicle_.valid()) return;
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    const World& w = world();
    std::string title, body;
    if (const Vehicle* veh = w.vehicles.tryGet(hoverVehicle_)) {
        title = vehicleName(*veh);
        body = vehicleStatus(*veh);
        if (veh->capacity()) body += std::format("\n{} / {} passengers", veh->passengers.size(), veh->capacity());
        body += std::format("\n{} km/h", veh->speed * 100 / 80);
    } else if (const Person* person = w.people.tryGet(hoverPerson_)) {
        title = personName(*person);
        const Trip trip = tripOf(w, *person);
        body = std::string(people::stateName(person->state));
        if (trip.dest) {
            const Town* town = w.towns.tryGet(trip.dest->town);
            body += std::format("\n{} - {}{}{}", trip.purpose, trip.dest->info().name, town ? ", " : "", town ? town->name : "");
        }
    }
    body += "\nClick to inspect";
    ui::tooltip({title.c_str(), body.c_str()});
}

void App::selectPerson(PersonId id) {
    const Person* p = world().people.tryGet(id);
    if (!p) return;
    selPerson_ = id;
    selPersonSerial_ = p->serial;
    citizenDiary_.clear();
    showPersonCard_ = true;
}

void App::drawInspectOverlays(std::vector<gfx::OverlayQuad>& overlays) {
    const World& w = world();
    const double fraction = session_->tickFraction();
    const float pulse = 0.75f + 0.25f * static_cast<float>(std::sin(timeSeconds_ * 5.0));
    std::vector<Body> bodies;
    const auto frame = [&](VehicleId id, u32 edge, u32 fill) {
        const Vehicle* v = w.vehicles.tryGet(id);
        if (!v || v->state == VehicleState::InDepot) return;
        vehicleBodies(w, *v, fraction, bodies);
        for (const Body& b : bodies) addFrame(overlays, b, edge, fill);
    };
    frame(selVehicle_, gfx::rgba(255, 186, 66, static_cast<u8>(230 * pulse)), gfx::rgba(255, 186, 66, 40));
    if (tool_ != Tool::None) {
        hoverVehicle_ = {};
        hoverPerson_ = {};
    }
    if (hoverVehicle_ != selVehicle_) frame(hoverVehicle_, gfx::rgba(255, 255, 255, 200), gfx::rgba(255, 255, 255, 36));
    if (const Person* p = w.people.tryGet(hoverPerson_); p && hoverPerson_ != selPerson_)
        if (const auto pose = gfx::personPose(w, *p, fraction); pose && pose->visible)
            addRing(overlays, pose->pos + glm::dvec3(0.0, 0.01, 0.0), 0.07, gfx::rgba(255, 255, 255, 210));
    if (showPersonCard_) drawPersonRoute(overlays);
}

void App::drawPersonRoute(std::vector<gfx::OverlayQuad>& overlays) {
    const World& w = world();
    const Person* p = w.people.tryGet(selPerson_);
    if (!p || p->serial != selPersonSerial_) return;
    const double fraction = session_->tickFraction();
    const Trip trip = tripOf(w, *p);
    const u32 walkColour = gfx::rgba(132, 220, 110, 220);
    const u32 rideColour = gfx::rgba(90, 175, 255, 220);
    const u32 abstractColour = gfx::rgba(255, 186, 66, 170);
    constexpr double kLift = 0.06;
    std::optional<glm::dvec3> here;
    const auto pose = gfx::personPose(w, *p, fraction);
    if (pose && pose->visible) {
        here = pose->pos;
        addRing(overlays, pose->pos + glm::dvec3(0.0, 0.01, 0.0), 0.085, gfx::rgba(255, 186, 66, 235));
    } else if (const Vehicle* v = w.vehicles.tryGet(p->vehicle); v && p->state == PersonState::Riding) {
        if (const auto vp = gfx::vehiclePose(w, *v, fraction)) here = vp->pos;
    }
    // The rest of the current walk, along the path they actually take.
    const bool onFoot = p->state == PersonState::Walking || p->state == PersonState::ToStop ||
                        p->state == PersonState::FromStop;
    if (onFoot && p->walkPath.size() >= 2 && here) {
        std::vector<glm::dvec3> rest{*here + glm::dvec3(0.0, 0.01, 0.0)};
        const double total = static_cast<double>(walking::length(p->walkPath));
        const double span = static_cast<double>(std::max<i64>(1, p->walkEnd - p->walkBegin));
        const double done = total * std::clamp((w.clock.tick + fraction - p->walkBegin) / span, 0.0, 1.0);
        double run = 0.0;
        for (usize i = 1; i < p->walkPath.size(); ++i) {
            const WalkPoint& a = p->walkPath[i - 1];
            const WalkPoint& b = p->walkPath[i];
            run += std::hypot(static_cast<double>(b.p.x - a.p.x), static_cast<double>(b.p.z - a.p.z)) +
                   (a.height >= 0 && b.height >= 0 ? std::abs(b.height - a.height) * 8.0 : 0.0);
            if (run <= done) continue;
            const double x = b.p.x / static_cast<double>(kTrackUnits), z = b.p.z / static_cast<double>(kTrackUnits);
            rest.push_back(b.height >= 0 ? glm::dvec3(x, b.height / static_cast<double>(kSubLevels) * 0.5 + kLift, z)
                                         : groundPoint(w, x, z, kLift));
        }
        addRibbon(overlays, rest, 0.035, walkColour, 0.12);
        here = rest.back();
    }
    const Station* board = w.stations.tryGet(p->station);
    const Station* alight = w.stations.tryGet(p->targetStation);
    const Station* last = w.stations.tryGet(p->finalStation);
    std::optional<glm::dvec3> from = here;
    const auto ride = [&](const Station* to) {
        if (!from || !to) return;
        const glm::dvec3 end = stationPoint(w, *to, kLift + 0.06);
        addRibbon(overlays, groundLeg(w, *from, end, kLift + 0.06), 0.05, rideColour);
        addRing(overlays, end, 0.28, rideColour);
        from = end;
    };
    switch (p->state) {
    case PersonState::ToStop:
    case PersonState::Waiting:
    case PersonState::Riding:
        if (p->state != PersonState::Riding && board) from = stationPoint(w, *board, kLift + 0.06);
        ride(alight);
        if (last && last != alight) ride(last);
        break;
    case PersonState::Driving:
    case PersonState::Lift:
        // Off the map: a straight line from where the trip began.
        from = groundPoint(w, p->tripFrom2.x * 0.5, p->tripFrom2.y * 0.5, kLift);
        break;
    default: break;
    }
    if (trip.dest && !travelling(p->state)) {
        // Staying put: mark where they go next.
        addRing(overlays, buildingPoint(w, *trip.dest, kLift), trip.dest->info().size * 0.5 + 0.15, gfx::rgba(255, 186, 66, 110));
    } else if (trip.dest) {
        const glm::dvec3 end = buildingPoint(w, *trip.dest, kLift);
        if (from && !(onFoot && p->state != PersonState::ToStop)) {
            const bool drive = p->state == PersonState::Driving || p->state == PersonState::Lift;
            addRibbon(overlays, groundLeg(w, *from, end, kLift), drive ? 0.05 : 0.035, drive ? abstractColour : walkColour,
                      drive ? 0.0 : 0.12);
        }
        const double size = trip.dest->info().size * 0.5 + 0.15;
        addRing(overlays, end, size, gfx::rgba(255, 186, 66, 200));
    }
}

void App::drawPersonLabels() {
    if (!showPersonCard_) return;
    const World& w = world();
    const Person* p = w.people.tryGet(selPerson_);
    if (!p || p->serial != selPersonSerial_) return;
    const Trip trip = tripOf(w, *p);
    if (!trip.dest) return;
    const glm::dvec3 at = buildingPoint(w, *trip.dest, 0.6 + trip.dest->info().size * 0.15);
    if (const auto sp = worldToScreen(at)) {
        const std::string title = travelling(p->state) ? std::format("{}'s destination", generatePersonFirstName(p->nameSeed))
                                                       : std::string("Next trip");
        ui::worldLabel(ImGui::GetBackgroundDrawList(), ImVec2(sp->x, sp->y), title.c_str(), trip.dest->info().name.data(),
                       trip.colour);
    }
}

void App::drawPersonWindow() {
    if (!showPersonCard_) return;
    World& w = world();
    const Person* p = w.people.tryGet(selPerson_);
    if (!p || p->serial != selPersonSerial_) {
        showPersonCard_ = false;
        followPerson_ = false;
        return;
    }
    const float s = uiScale_;
    const Trip trip = tripOf(w, *p);
    const Town* homeTown = w.towns.tryGet(p->town);
    const std::string name = personName(*p);
    const std::string sub = homeTown ? std::format("Lives in {}", homeTown->name) : std::string("Citizen");
    bool open = true;
    ui::PanelSpec spec;
    spec.pos = ImVec2(ImGui::GetMainViewport()->WorkPos.x + (showCitizens_ ? 560.0f : 20.0f) * s,
                      ImGui::GetMainViewport()->WorkPos.y + 80.0f * s);
    spec.width = 380.0f;
    spec.accent = trip.colour == ui::col::kText ? ui::col::kAccent : trip.colour;
    spec.subtitle = sub.c_str();
    if (ui::beginPanel("###personCard", name.c_str(), ui::Icon::People, &open, spec)) {
        const bool moving = travelling(p->state);
        ui::badge(std::string(people::stateName(p->state)).c_str(), moving ? ui::col::kBlue : ui::col::kTextMuted);
        if (followPerson_) {
            ImGui::SameLine();
            ui::badge("Following", ui::col::kBlue);
        }
        ImGui::Dummy(ImVec2(0, 2.0f * s));
        // Where to, and why.
        if (trip.dest) {
            const Town* town = w.towns.tryGet(trip.dest->town);
            ui::sectionHeader(moving ? "Going to" : "Next trip");
            ui::iconText(trip.icon, trip.colour, std::format("{} - {}", trip.purpose, trip.dest->info().name).c_str());
            if (town) {
                ui::FontScope f(ui::fonts().small);
                const std::string where = trip.dest->town == p->town ? std::format("In {}, their home town", town->name)
                                                                     : std::format("In {}", town->name);
                ui::mutedText(where.c_str());
            }
            if (!moving && p->nextEvent >= 0) {
                const i32 minute = static_cast<i32>((p->nextEvent % kTicksPerDay) / kTicksPerMinute);
                const i32 day = w.clock.startDay + static_cast<i32>(p->nextEvent / kTicksPerDay);
                ui::statRow("Leaves", std::format("{} {}", formatDate(day), formatTimeOfDay(minute)).c_str());
            }
        } else {
            ui::sectionHeader("Going to");
            ui::mutedText("Nowhere planned yet");
        }
        // The journey, step by step.
        if (moving) {
            ui::sectionHeader("Journey");
            const Station* board = w.stations.tryGet(p->station);
            const Station* alight = w.stations.tryGet(p->targetStation);
            const Station* last = w.stations.tryGet(p->finalStation);
            const std::string dest = trip.dest ? std::string(trip.dest->info().name) : std::string(trip.place);
            enum { Done, Now, Later };
            const auto step = [&](int status, ui::Icon icon, const std::string& text) {
                const ImU32 c = status == Done ? ui::col::kTextMuted : status == Now ? ui::col::kAccent : ui::col::kText;
                ui::iconText(status == Done ? ui::Icon::Check : icon, c, text.c_str(), status == Later ? ui::col::kTextMuted : c);
            };
            const i64 waited = (w.clock.tick - p->boardTick) / kTicksPerMinute;
            switch (p->state) {
            case PersonState::Walking: step(Now, ui::Icon::Walk, std::format("Walking all the way to {}", dest)); break;
            case PersonState::Driving: step(Now, ui::Icon::Car, std::format("Driving their own car to {}", dest)); break;
            case PersonState::Lift:
                step(Now, ui::Icon::Car, std::format("Getting a lift to {}", dest));
                {
                    ui::FontScope f(ui::fonts().small);
                    ui::iconText(ui::Icon::Warning, ui::col::kOrange, "No car and no public transport for this trip", ui::col::kOrange);
                }
                break;
            case PersonState::ToStop:
            case PersonState::Waiting:
            case PersonState::Riding: {
                const int walkStatus = p->state == PersonState::ToStop ? Now : Done;
                step(walkStatus, ui::Icon::Walk, std::format("Walk to {}", board ? board->name : std::string("the station")));
                if (p->state == PersonState::Waiting)
                    step(Now, ui::Icon::Clock, std::format("Waiting at {} for {} min", board ? board->name : std::string("?"), std::max<i64>(0, waited)));
                const Vehicle* v = w.vehicles.tryGet(p->vehicle);
                const std::string by = p->state == PersonState::Riding && v ? std::format(" on {}", vehicleName(*v)) : std::string();
                step(p->state == PersonState::Riding ? Now : Later, ui::Icon::Train,
                     std::format("Ride to {}{}", alight ? alight->name : std::string("?"), by));
                if (last && last != alight) step(Later, ui::Icon::Link, std::format("Change, then on to {}", last->name));
                step(Later, ui::Icon::Walk, std::format("Walk to {}", dest));
                break;
            }
            case PersonState::FromStop:
                step(Done, ui::Icon::Train, std::format("Rode to {}", board ? board->name : std::string("the station")));
                step(Now, ui::Icon::Walk, std::format("Walk to {}", dest));
                break;
            default: break;
            }
            const i64 minutes = std::max<i64>(0, (w.clock.tick - p->tripStart) / kTicksPerMinute);
            ImGui::Dummy(ImVec2(0, 2.0f * s));
            ui::statRow("Under way for", std::format("{} min", minutes).c_str());
        }
        ui::sectionHeader("About");
        ui::statRow("Car owner", p->carOwner ? "Yes" : "No");
        ui::statRow("Usual departure", formatTimeOfDay(p->departMinute).c_str());
        ui::statRow("Last commute", std::format("{} min", p->commuteMinutes).c_str());
        const auto place = [&](const char* kind, BuildingRef ref, ui::Icon icon) {
            const Building* b = resolveBuilding(w, ref);
            if (!b) return;
            const std::string label = std::format("{}: {}##{}", kind, b->info().name, kind);
            if (ui::button(label.c_str(), icon, ui::Btn::Ghost, ImVec2(-1, 0))) focusOn(b->pos);
        };
        place("Home", p->home, ui::Icon::Home);
        place("Work", p->work, ui::Icon::Briefcase);
        ImGui::Dummy(ImVec2(0, 2.0f * s));
        const float half = ui::columnWidth(2);
        if (ui::button(followPerson_ ? "Stop following" : "Follow", ui::Icon::Eye, ui::Btn::Primary, ImVec2(half, 0))) {
            followPerson_ = !followPerson_;
            followVehicle_ = {};
        }
        ImGui::SameLine();
        if (w.vehicles.contains(p->vehicle) && p->state == PersonState::Riding) {
            if (ui::button("Their vehicle", vehicleIconFor(w.vehicles[p->vehicle].info().type), ui::Btn::Secondary, ImVec2(-1, 0)))
                selVehicle_ = p->vehicle;
        } else if (trip.dest) {
            if (ui::button("Destination", ui::Icon::Target, ui::Btn::Secondary, ImVec2(-1, 0))) focusOn(trip.dest->pos);
        }
    }
    ui::endPanel();
    if (!open) {
        showPersonCard_ = false;
        followPerson_ = false;
    }
}

void App::drawVehiclePassengers(const Vehicle& v) {
    if (v.passengers.empty()) return;
    const World& w = world();
    ui::sectionHeader("Who's aboard");
    // Where the passengers are headed, by town, then a few of them by name.
    std::map<std::string, usize> towns;
    for (const PersonId pid : v.passengers)
        if (const Person* p = w.people.tryGet(pid))
            if (const Trip trip = tripOf(w, *p); trip.dest)
                if (const Town* t = w.towns.tryGet(trip.dest->town)) ++towns[t->name];
    {
        ui::FontScope f(ui::fonts().small);
        std::string summary = "Bound for ";
        usize n = 0;
        for (const auto& [name, count] : towns) summary += std::format("{}{} ({})", n++ ? ", " : "", name, count);
        if (n) ImGui::TextWrapped("%s", summary.c_str());
    }
    if (ImGui::BeginChild("##riders", ImVec2(0, std::min<float>(6.0f, static_cast<float>(v.passengers.size())) * ImGui::GetTextLineHeightWithSpacing() + 6.0f * uiScale_),
                          ImGuiChildFlags_Borders)) {
        usize shown = 0;
        for (const PersonId pid : v.passengers) {
            const Person* p = w.people.tryGet(pid);
            if (!p) continue;
            const Trip trip = tripOf(w, *p);
            const std::string label = std::format("{} - {}{}##rider{}", personName(*p), trip.purpose,
                                                  trip.dest ? std::format(" ({})", trip.dest->info().name) : std::string(), pid.value);
            if (ImGui::Selectable(label.c_str(), showPersonCard_ && selPerson_ == pid)) selectPerson(pid);
            if (++shown >= 60) break;
        }
    }
    ImGui::EndChild();
}

} // namespace tp
