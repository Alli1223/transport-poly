#include "render/person_pose.hpp"
#include "render/picking.hpp"
#include "render/vehicle_pose.hpp"
#include "sim/world.hpp"
#include <algorithm>
#include <cmath>

namespace tp::gfx {
std::optional<PersonPose> personPose(const World& w, const Person& p, double fraction) {
    if (p.state == PersonState::Driving && !p.carPath.empty()) {
        if (auto car = privateCarPose(w, p, fraction))
            return PersonPose{car->pos + glm::dvec3(0, .055, 0), car->yaw, true};
        return std::nullopt;
    }
    const double now = static_cast<double>(w.clock.tick) + fraction;
    const bool onFoot = p.state == PersonState::Walking || p.state == PersonState::ToStop ||
        p.state == PersonState::FromStop || p.state == PersonState::Waiting ||
        (p.state == PersonState::Riding && p.transitMotion == 1 && now < p.transitEnd);
    if (onFoot && !p.walkPath.empty()) {
        const auto a = walking::position(p,w.clock.tick), b = walking::position(p,w.clock.tick+1);
        const glm::dvec2 pos = glm::mix(glm::dvec2(a.p.x,a.p.z),glm::dvec2(b.p.x,b.p.z),fraction)/double(kTrackUnits);
        const auto tile = tileAt(a.p);
        const double height = a.height >= 0 && b.height >= 0 ?
            glm::mix(double(a.height),double(b.height),fraction)/kSubLevels*0.5 :
            roadSurfaceY(w,tile,static_cast<float>(pos.x-tile.x),static_cast<float>(pos.y-tile.y));
        const float yaw = static_cast<float>(std::atan2(-double(b.p.z-a.p.z),double(b.p.x-a.p.x)));
        const double lane = p.state == PersonState::Waiting && now >= p.walkEnd
                                ? 0.0
                                : (static_cast<int>(p.serial % 5) - 2) * 0.008;
        return PersonPose{{pos.x+lane,height+0.055,pos.y+lane},yaw,true};
    }
    if (p.transitEnd > now && p.transitBegin >= 0 &&
        (p.state == PersonState::Riding || p.state == PersonState::Waiting || p.state == PersonState::FromStop)) {
        const glm::dvec2 from(p.transitFrom2.x * 0.5, p.transitFrom2.y * 0.5);
        const glm::dvec2 to(p.transitTo2.x * 0.5, p.transitTo2.y * 0.5);
        const double along = std::clamp((now - static_cast<double>(p.transitBegin)) /
            static_cast<double>(std::max<i64>(1, p.transitEnd - p.transitBegin)), 0.0, 1.0);
        const auto at = glm::mix(from, to, along);
        const auto height = [&](TilePos pos2) {
            const TilePos tile{static_cast<i32>(std::floor(pos2.x * 0.5)), static_cast<i32>(std::floor(pos2.y * 0.5))};
            return roadSurfaceY(w, tile, 0.5f, 0.5f);
        };
        const double y = glm::mix(static_cast<double>(height(p.transitFrom2)), static_cast<double>(height(p.transitTo2)), along);
        const double lane = (static_cast<int>(p.serial % 5) - 2) * 0.025;
        return PersonPose{{at.x + lane, y + 0.035, at.y + lane},
            static_cast<float>(std::atan2(-(to.y - from.y), to.x - from.x)),
            p.transitMotion == 1 || now >= p.transitBegin};
    }
    const auto buildingPos = [&](BuildingRef ref) -> std::optional<glm::dvec2> {
        const Building* b = w.buildings.tryGet(ref.id);
        if (!b || b->serial != ref.serial) return std::nullopt;
        const TilePos centre = b->centre2();
        return glm::dvec2(centre.x * 0.5, centre.y * 0.5);
    };
    if (p.state == PersonState::Riding) {
        if (const Vehicle* v = w.vehicles.tryGet(p.vehicle)) {
            usize seat = 0;
            for (const auto pid : v->passengers) {
                const auto* rider = w.people.tryGet(pid);
                if (rider && rider->serial == p.serial) break;
                ++seat;
            }
            if (seat >= v->passengers.size()) return std::nullopt;
            auto pose = vehiclePose(w, *v, fraction);
            seat = v->passengerSeat(seat);
            const VehicleModel* model = &v->info();
            u32 seats = v->info().capacity;
            double passengerLength = std::max(0.3, model->length / 16.0);
            if (v->info().type == VehicleType::Rail) {
                std::vector<CarPose> cars;
                trainCarPoses(w, *v, fraction, cars);
                usize index = 0;
                while (index < v->cars.size() && seat >= vehicleModel(v->cars[index]).capacity) {
                    seat -= vehicleModel(v->cars[index]).capacity; ++index;
                }
                pose.reset();
                for (const auto& car : cars)
                    if (car.index == index) {
                        pose = car.pose;
                        model = &vehicleModel(car.model);
                        seats = model->capacity;
                        break;
                    }
            }
            if (v->info().type == VehicleType::Tram && tramSectionCount(*v) > 1) {
                std::vector<VehiclePose> sections;
                tramSectionPoses(w, *v, fraction, sections);
                const usize count = tramSectionCount(*v), capacity = v->info().capacity;
                const usize section = std::min(count - 1, seat * count / std::max<usize>(1, capacity));
                const usize first = (capacity * section + count - 1) / count;
                seats = static_cast<u32>((capacity * (section + 1) + count - 1) / count - first);
                seat -= first;
                pose =
                    section < sections.size() ? std::optional<VehiclePose>(sections[section]) : std::nullopt;
                passengerLength = tramSectionLength(*v);
            } else
                passengerLength = std::max(0.3, model->length / 16.0);
            if (pose) {
                const double length = passengerLength;
                double y = model->type == VehicleType::Rail   ? 0.08
                           : model->type == VehicleType::Ship ? 0.14
                                                              : 0.06;
                if (model->type == VehicleType::Rail && model->visual == 15) {
                    const usize perDeck = (seats + 1) / 2;
                    if (seat >= perDeck) {
                        seat -= perDeck;
                        y = 0.26;
                    }
                    seats = static_cast<u32>(perDeck);
                }
                const usize rows = std::max<usize>(1, (seats + 3) / 4);
                const double x = ((static_cast<double>(seat / 4) + 0.5) / rows - 0.5) * length * 0.83;
                const double z = (static_cast<int>(seat % 4) - 1.5) * 0.044;
                const double cs = std::cos(pose->yaw), sn = std::sin(pose->yaw);
                const double cp = std::cos(pose->pitch), sp = std::sin(pose->pitch);
                return PersonPose{pose->pos + glm::dvec3(cs * (cp * x - sp * y) + sn * z, sp * x + cp * y,
                                                         -sn * (cp * x - sp * y) + cs * z),
                                  pose->yaw, true};
            }
        }
        return std::nullopt;
    }
    glm::dvec2 from(p.tripFrom2.x * 0.5, p.tripFrom2.y * 0.5);
    glm::dvec2 to(p.tripTo2.x * 0.5, p.tripTo2.y * 0.5);
    bool visible = false;
    bool moving = false;
    i64 speed = kWalkTicksPerTile, overhead = 0;
    if (p.state == PersonState::AtHome || p.state == PersonState::AtWork || p.state == PersonState::AtShop) {
        const auto at = buildingPos(p.state == PersonState::AtHome ? p.home : p.state == PersonState::AtWork ? p.work : p.shop);
        if (!at) return std::nullopt;
        from = to = *at;
    } else if (p.state == PersonState::Waiting || p.state == PersonState::ToStop || p.state == PersonState::FromStop) {
        if (const Station* s = w.stations.tryGet(p.station)) {
            const glm::dvec2 stop(s->sign.x + 0.5, s->sign.y + 0.5);
            if (p.state == PersonState::Waiting) {
                const double ox = (static_cast<int>(p.serial % 32) - 16) * 0.035;
                const double oy = (static_cast<int>((p.serial / 32) % 32) - 16) * 0.035;
                from = to = stop + glm::dvec2(ox, oy);
            } else if (p.state == PersonState::ToStop) to = stop;
            else from = p.transitMotion == 2 ? glm::dvec2(p.transitTo2.x * 0.5, p.transitTo2.y * 0.5) : stop;
        }
        visible = true;
        moving = p.state != PersonState::Waiting;
    } else if (p.state == PersonState::Walking || p.state == PersonState::Driving || p.state == PersonState::Lift) {
        moving = true;
        visible = p.state == PersonState::Walking;
        if (p.state == PersonState::Driving) { speed = kDriveTicksPerTile; overhead = kDriveOverheadTicks; }
        if (p.state == PersonState::Lift) { speed = kLiftTicksPerTile; overhead = kLiftOverheadTicks; }
    }
    double along = 0;
    if (moving && p.nextEvent >= 0) {
        const double tiles = std::floor(glm::length(to - from) + 0.5);
        const double duration = std::max(1.0, tiles) * static_cast<double>(speed) + static_cast<double>(overhead);
        along = std::clamp(1.0 - (static_cast<double>(p.nextEvent - w.clock.tick) - fraction) / duration, 0.0, 1.0);
    }
    const auto at = glm::mix(from, to, along);
    return PersonPose{{at.x, terrainHeightAt(w.map, at.x, at.y) + 0.035, at.y},
                      static_cast<float>(std::atan2(-(to.y - from.y), to.x - from.x)), visible};
}
}
