#include "render/vehicle_fx.hpp"

#include "render/shaders.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace tp::gfx {

namespace {

// Driving wheels of the tank engine (car local, tiles): axle positions, wheel radius and the crank
// throw of the rods.
constexpr float kAxles[3] = {-0.15f, -0.02f, 0.11f};
constexpr float kWheelRadius = 0.052f;
constexpr float kCrank = 0.022f;
constexpr float kMainRod = 0.17f;
constexpr float kCrossheadY = kWheelRadius + 0.016f;
constexpr usize kMaxPuffs = 2400;
const glm::vec3 kWind(0.16f, 0.0f, 0.07f); // tiles per second

bool isSteam(const VehicleModel& m) { return m.type == VehicleType::Rail && m.powered && m.visual == 0; }

bool isElectric(const VehicleModel& m) {
    return m.name.find("Electric") != std::string_view::npos || m.name.find("EMU") != std::string_view::npos;
}

glm::mat4 carMatrix(const VehiclePose& pose, const glm::dvec3& origin) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(pose.pos - origin));
    m = glm::rotate(m, pose.yaw, glm::vec3(0, 1, 0));
    return glm::rotate(m, pose.pitch, glm::vec3(0, 0, 1));
}

glm::dvec3 worldPoint(const glm::mat4& m, const glm::dvec3& origin, glm::vec3 local) {
    return origin + glm::dvec3(glm::vec3(m * glm::vec4(local, 1.0f)));
}

glm::vec3 forwardOf(const VehiclePose& p) {
    return {std::cos(p.yaw) * std::cos(p.pitch), std::sin(p.pitch), -std::sin(p.yaw) * std::cos(p.pitch)};
}

} // namespace

bool VehicleFx::init() {
    shader_ = Shader::create("puff", shaders::puffVS(), shaders::puffFS());
    if (!shader_) return false;
    vao_ = makeVertexArray();
    vbo_ = makeBuffer();
    glBindVertexArray(vao_.get());
    glBindBuffer(GL_ARRAY_BUFFER, vbo_.get());
    // Position, offset across the puff (-1..1) and colour with opacity.
    constexpr GLsizei kStride = 9 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, kStride, reinterpret_cast<const void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, kStride, reinterpret_cast<const void*>(5 * sizeof(float)));
    glBindVertexArray(0);
    return true;
}

float VehicleFx::random() {
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return static_cast<float>(seed_ & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

void VehicleFx::emit(const Puff& p) {
    if (puffs_.size() < kMaxPuffs) puffs_.push_back(p);
}

void VehicleFx::beginFrame(double timeSeconds) {
    dt_ = now_ > 0.0 ? static_cast<float>(std::clamp(timeSeconds - now_, 0.0, 0.1)) : 0.0f;
    now_ = timeSeconds;
    for (Puff& p : puffs_) {
        p.age += dt_;
        // Rising slows as the puff cools and mixes; the wind takes over.
        const float drag = std::exp(-1.6f * dt_);
        p.vel = p.vel * drag + kWind * (1.0f - drag);
        p.vel.y = std::max(p.vel.y, 0.06f);
        p.pos += glm::dvec3(p.vel * dt_);
    }
    std::erase_if(puffs_, [](const Puff& p) { return p.age >= p.life; });
    if (static_cast<i64>(now_ * 2.0) % 20 == 0)
        std::erase_if(motion_, [&](const auto& kv) { return now_ - kv.second.seen > 10.0; });
}

void VehicleFx::addTrain(InstanceBatch& batch, VehicleId id, const std::vector<CarPose>& cars, const glm::dvec3& origin) {
    if (cars.empty()) return;
    // How far the head has run since the last frame, along its heading (negative when backing).
    Motion& m = motion_[id.value];
    const glm::dvec3 head = cars.front().pose.pos;
    const glm::vec3 heading = forwardOf(cars.front().pose);
    double moved = 0.0;
    if (m.valid && m.seen >= 0.0 && now_ - m.seen < 0.5) {
        const glm::dvec3 delta = head - m.last;
        if (glm::length(delta) < 2.0) moved = glm::dot(glm::vec3(delta), heading);
    }
    m.valid = true;
    m.last = head;
    m.seen = now_;
    m.odometer += moved;
    const double instant = dt_ > 0.0f ? std::abs(moved) / dt_ : m.speed;
    const double previous = m.speed;
    m.speed += (instant - m.speed) * std::min(1.0, dt_ * 4.0);
    const double accel = dt_ > 0.0f ? (m.speed - previous) / dt_ : 0.0;
    const bool working = accel > 0.02 || (m.speed > 0.05 && m.speed < 0.8); // pulling away
    const bool coasting = accel < -0.05;                                    // steam shut off to brake
    const glm::dvec3 velocity = glm::dvec3(heading) * (moved / std::max(1e-3f, dt_));

    // Exhaust beats: four per turn of the driving wheels.
    const double turn = m.odometer / kWheelRadius;
    const i64 beat = static_cast<i64>(std::floor(turn / (glm::pi<double>() * 0.5)));
    const i64 beats = std::clamp<i64>(std::abs(beat - m.beat), 0, 3);
    m.beat = beat;
    const bool idleWisp = now_ >= m.nextIdle;
    if (idleWisp) m.nextIdle = now_ + 0.5 + random() * 0.6;

    for (const CarPose& car : cars) {
        const VehicleModel& model = vehicleModel(car.model);
        if (!model.powered) continue;
        const glm::mat4 cm = carMatrix(car.pose, origin);
        const float facing = glm::dot(forwardOf(car.pose), heading) < 0.0f ? -1.0f : 1.0f;
        if (isSteam(model)) {
            // Driving wheels turned by the distance run, and the rods: the coupling rods on the crank
            // pins (the two sides a quarter turn apart) and the connecting rods to the crossheads.
            const float angle = -static_cast<float>(turn) * facing;
            for (const float x : kAxles) {
                const glm::mat4 wheel = glm::rotate(glm::translate(cm, glm::vec3(x, kWheelRadius, 0.0f)), angle, glm::vec3(0, 0, 1));
                batch.add(models::kSteamWheelset, makeInstance(wheel));
            }
            for (const float side : {1.0f, -1.0f}) {
                const float phase = angle + (side > 0.0f ? 0.0f : glm::half_pi<float>());
                const glm::vec2 pin(std::cos(phase) * kCrank, std::sin(phase) * kCrank);
                batch.add(models::kSideRod, makeInstance(glm::translate(cm, glm::vec3(pin.x, kWheelRadius + pin.y, side * 0.112f))));
                const glm::vec2 p(kAxles[1] + pin.x, kWheelRadius + pin.y);
                const float rise = kCrossheadY - p.y;
                const glm::vec2 c(p.x + std::sqrt(kMainRod * kMainRod - rise * rise), kCrossheadY);
                glm::mat4 rod = glm::translate(cm, glm::vec3(p.x, p.y, side * 0.121f));
                rod = glm::rotate(rod, std::atan2(c.y - p.y, c.x - p.x), glm::vec3(0, 0, 1));
                batch.add(models::kMainRod, makeInstance(glm::scale(rod, glm::vec3(kMainRod, 1.0f, 1.0f))));
            }
            // A chuff of steam and smoke from the chimney on every beat, darker when working hard;
            // standing, a lazy wisp now and then and a feather of steam from the safety valves.
            const glm::dvec3 chimney = worldPoint(cm, origin, {0.24f, 0.3f, 0.0f});
            for (i64 b = 0; b < beats; ++b) {
                if (coasting && random() > 0.12f) continue;
                const float shade = working ? 0.42f + random() * 0.2f : 0.78f + random() * 0.14f;
                emit({chimney + glm::dvec3(0.0, b * 0.02, 0.0),
                      glm::vec3(velocity * 0.35) + glm::vec3((random() - 0.5f) * 0.2f, 0.75f + random() * 0.35f, (random() - 0.5f) * 0.2f),
                      0.0f, 2.0f + random() * 1.1f, 0.05f, 0.28f + random() * 0.1f, working ? 0.85f : 0.65f,
                      glm::vec3(shade, shade, shade * 1.03f)});
                // Drain cocks open while pulling away: white jets low down at the cylinders.
                if (m.speed < 0.6 && m.speed > 0.02)
                    for (const float side : {1.0f, -1.0f}) {
                        const glm::vec3 out = glm::vec3(cm * glm::vec4(0.0f, 0.0f, side, 0.0f));
                        emit({worldPoint(cm, origin, {0.27f, 0.06f, side * 0.1f}), out * 0.7f + glm::vec3(0.0f, 0.15f, 0.0f),
                              0.0f, 1.0f + random() * 0.4f, 0.03f, 0.16f, 0.6f, glm::vec3(0.95f)});
                    }
            }
            if (m.speed < 0.05 && idleWisp) {
                emit({chimney, glm::vec3((random() - 0.5f) * 0.06f, 0.35f + random() * 0.15f, (random() - 0.5f) * 0.06f), 0.0f,
                      2.6f, 0.04f, 0.22f, 0.35f, glm::vec3(0.88f)});
                if (random() < 0.25f)
                    emit({worldPoint(cm, origin, {-0.035f, 0.26f, 0.0f}), glm::vec3(0.0f, 1.1f, 0.0f), 0.0f, 1.2f, 0.02f, 0.12f,
                          0.55f, glm::vec3(0.97f)});
            }
        } else if (!isElectric(model) && m.speed > 0.05 && (idleWisp || (working && beats > 0 && random() < 0.3f))) {
            // Diesel exhaust: a thin grey haze from the roof while running, thicker pulling away.
            const float hl = model.length / 32.0f;
            const glm::dvec3 exhaust = worldPoint(cm, origin, {hl * 0.25f * facing, 0.31f, 0.0f});
            const float shade = working ? 0.3f : 0.5f;
            emit({exhaust, glm::vec3(velocity * 0.3) + glm::vec3(0.0f, 0.5f + random() * 0.2f, 0.0f), 0.0f, 1.4f + random() * 0.6f,
                  0.03f, working ? 0.2f : 0.14f, working ? 0.5f : 0.28f, glm::vec3(shade)});
        }
    }
}

void VehicleFx::draw(const glm::mat4& view, const glm::dvec3& origin) {
    if (puffs_.empty() || !shader_) return;
    // Back to front, so the soft edges blend over the puffs behind.
    std::vector<std::pair<float, const Puff*>> order;
    order.reserve(puffs_.size());
    for (const Puff& p : puffs_) {
        const glm::vec4 eye = view * glm::vec4(glm::vec3(p.pos - origin), 1.0f);
        order.push_back({eye.z, &p});
    }
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const glm::vec3 right(view[0][0], view[1][0], view[2][0]), up(view[0][1], view[1][1], view[2][1]);
    static constexpr float kQuad[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
    scratch_.clear();
    scratch_.reserve(order.size() * 6 * 9);
    for (const auto& [depth, p] : order) {
        const float t = p->age / p->life;
        const float size = p->size0 + (p->size1 - p->size0) * (1.0f - (1.0f - t) * (1.0f - t));
        // Quick to appear, slow to fade.
        const float alpha = p->opacity * std::min(1.0f, t * 12.0f) * (1.0f - t) * (1.0f - t);
        const glm::vec3 centre(p->pos - origin);
        for (const auto& c : kQuad) {
            const glm::vec3 q = centre + (right * c[0] + up * c[1]) * size;
            scratch_.insert(scratch_.end(), {q.x, q.y, q.z, c[0], c[1], p->colour.r, p->colour.g, p->colour.b, alpha});
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, vbo_.get());
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(scratch_.size() * sizeof(float)), scratch_.data(), GL_STREAM_DRAW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    shader_->use();
    glBindVertexArray(vao_.get());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(order.size() * 6));
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
}

} // namespace tp::gfx
