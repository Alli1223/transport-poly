#include "client/app.hpp"
#include "render/vehicle_pose.hpp"
#include "sim/industries.hpp"
#include "core/hash.hpp"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

namespace tp {
void App::updateAudio(double now) {
    if (now < nextAudioUpdate_) return;
    nextAudioUpdate_ = now + .08;
    SoundScene scene;
    if (screen_ == Screen::Playing && session_ && session_->ready()) {
        const auto& w = world();
        scene.paused = session_->speed() == 0;
        const auto eye = camera_.eye(), right = camera_.rightXZ();
        struct Nearby { const Vehicle* vehicle; VehicleId id; double distance; };
        std::vector<Nearby> nearby;
        // Cull before calculating detailed poses; keep the 24 most audible sound sources.
        for (const auto [id, vehicle] : w.vehicles) {
            if (vehicle.state == VehicleState::InDepot) continue;
            const double dx = vehicle.tile.x + .5 - eye.x, dz = vehicle.tile.y + .5 - eye.z;
            const double d = dx * dx + dz * dz;
            const double reach = 150 + vehicle.cars.size() * 1.5;
            if (d < reach * reach) nearby.push_back({&vehicle, id, d});
        }
        std::sort(nearby.begin(), nearby.end(), [](const Nearby& a, const Nearby& b) { return a.distance < b.distance; });
        const auto offer = [&](SoundSource source) {
            if (source.gain < .001f) return;
            if (scene.count < scene.sources.size()) scene.sources[scene.count++] = source;
            else {
                auto quietest = std::min_element(scene.sources.begin(), scene.sources.end(), [](const auto& a, const auto& b) { return a.gain < b.gain; });
                if (source.gain > quietest->gain) *quietest = source;
            }
        };
        const auto at = [&](std::uint32_t id, SoundKind kind, glm::dvec3 position, float speed, float volume = 1.0f) {
            const auto delta = position - eye;
            const double distance = glm::length(delta);
            offer({id, kind, soundAttenuation(distance) * volume,
                static_cast<float>(std::clamp(glm::dot(delta, right) / std::max(10.0, distance), -1.0, 1.0)), speed});
        };
        std::vector<gfx::CarPose> cars;
        for (const auto& near : nearby) {
            const auto& v = *near.vehicle;
            const i32 top = v.info().type == VehicleType::Rail ? trains::maxSpeed(v) : std::max(1, speedUnits(v.info().maxSpeedKmh));
            const float speed = std::clamp(static_cast<float>(v.speed) / static_cast<float>(top), 0.0f, 1.0f);
            glm::dvec3 position;
            if (v.info().type == VehicleType::Rail) {
                cars.clear(); gfx::trainCarPoses(w, v, session_->tickFraction(), cars);
                for (const auto& car : cars) {
                    const auto& model = vehicleModel(car.model);
                    const auto key = mix32(v.serial * 31u ^ static_cast<u32>(car.index) * 0x9e3779b9u);
                    // Every bogie/body contributes rolling noise, including unpowered carriages.
                    if (speed > .001f) at(0x40000000u | (key & 0x1fffffffu), SoundKind::RailWheels, car.pose.pos,
                        std::clamp(v.speed / 128.0f, 0.0f, 1.0f), .68f);
                    if (model.powered) at(0x20000000u | (key & 0x1fffffffu), model.electric ? SoundKind::Electric :
                        model.visual == 0 ? SoundKind::Steam : SoundKind::Diesel, car.pose.pos, speed);
                }
                continue;
            } else {
                const auto pose = gfx::vehiclePose(w, v, session_->tickFraction());
                if (!pose) continue;
                position = pose->pos;
            }
            SoundKind kind = SoundKind::Road;
            switch (v.info().type) {
            case VehicleType::Rail: kind = v.info().electric ? SoundKind::Electric : v.info().visual == 0 ? SoundKind::Steam : SoundKind::Diesel; break;
            case VehicleType::Tram: kind = SoundKind::Tram; break;
            case VehicleType::Ship: kind = SoundKind::Ship; break;
            default: break;
            }
            at(near.id.value + 1, kind, position, speed);
        }
        const TilePos centre{static_cast<i32>(std::floor(camera_.target.x)), static_cast<i32>(std::floor(camera_.target.z))};
        for (const auto [id, person] : w.people) {
            if (person.state != PersonState::Driving || !person.carStarted || person.carIndex >= person.carPath.size()) continue;
            const auto tile = person.carPath[person.carIndex];
            if (std::hypot(tile.x - eye.x, tile.y - eye.z) > 150) continue;
            const auto pose = gfx::privateCarPose(w, person, session_->tickFraction());
            if (!pose) continue;
            const auto delta = pose->pos - eye;
            const double distance = glm::length(delta);
            SoundSource source{0x80000000u | (id.value + 1), SoundKind::Road, soundAttenuation(distance) * .45f,
                static_cast<float>(std::clamp(glm::dot(delta, right) / std::max(10.0, distance), -1.0, 1.0)),
                person.carStepStart + kDriveTicksPerTile > w.clock.tick ? .6f : 0.0f};
            offer(source);
        }
        int trees = 0, water = 0, town = 0;
        for (int y = -12; y <= 12; y += 3) for (int x = -12; x <= 12; x += 3) {
            const auto* tile = w.map.tile({centre.x + x, centre.y + y});
            if (!tile) continue;
            trees += tile->trees;
            water += tile->ground == Ground::Water;
            town += tile->type == TileType::Building || tile->type == TileType::Station;
        }
        const float zoom = static_cast<float>(std::clamp(1.0 - camera_.distance / 250.0, 0.0, 1.0));
        scene.countryside = std::clamp(trees / 90.0f, .12f, 1.0f) * zoom;
        scene.water = std::min(1.0f, water / 20.0f) * zoom;
        scene.town = std::min(1.0f, town / 25.0f) * zoom;
        for (const auto [id, b] : w.buildings) {
            if (!industries::isCountryside(b.type)) continue;
            const auto c = b.centre2();
            const double d = std::hypot(c.x * .5 - camera_.target.x, c.y * .5 - camera_.target.z);
            if (d > 30) continue;
            const float level = static_cast<float>(1.0 - d / 30) * zoom;
            if (b.type <= industries::kDairyFarm) scene.farm = std::max(scene.farm, level);
            else scene.industry = std::max(scene.industry, level);
        }
        const double minute = daylightOnly_ ? 720.0 : fixedTimeOfDay_.value_or(w.clock.exactMinuteOfDay(session_->tickFraction()));
        scene.night = minute < 360 || minute > 1200 ? 1.0f : 0.0f;
    }
    audio_.update(scene, !preferences_.muteUnfocused || glfwGetWindowAttrib(window_, GLFW_FOCUSED));
}
}
