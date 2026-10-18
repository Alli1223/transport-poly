#include "render/person_pose.hpp"
#include "render/picking.hpp"
#include "render/vehicle_pose.hpp"
#include "sim/world.hpp"
#include <algorithm>
#include <cmath>

namespace tp::gfx {
std::optional<PersonPose> personPose(const World& w, const Person& p, double fraction) {
    const double now = static_cast<double>(w.clock.tick) + fraction;
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
            auto pose = vehiclePose(w, *v, fraction);
            u32 seats = v->info().capacity;
            if (v->info().type == VehicleType::Rail) {
                std::vector<CarPose> cars;
                trainCarPoses(w, *v, fraction, cars);
                usize index = 0;
                while (index < v->cars.size() && seat >= vehicleModel(v->cars[index]).capacity) {
                    seat -= vehicleModel(v->cars[index]).capacity; ++index;
                }
                pose.reset();
                for (const auto& car : cars) if (car.index == index) { pose = car.pose; seats = vehicleModel(car.model).capacity; break; }
            }
            if (pose) {
                const double length = std::max(0.3, v->info().length / 16.0);
                const usize rows = std::max<usize>(1, (seats + 3) / 4);
                const double x = (static_cast<double>(seat / 4) / rows - 0.45) * length * 0.75;
                const double z = (static_cast<int>(seat % 4) - 1.5) * 0.035;
                const double cs = std::cos(pose->yaw), sn = std::sin(pose->yaw);
                // Seated citizens remain visible through the window band and on ship decks.
                return PersonPose{pose->pos + glm::dvec3(cs*x + sn*z, 0.14, -sn*x + cs*z), pose->yaw, true};
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
