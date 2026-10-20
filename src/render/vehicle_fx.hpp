#pragma once
// Moving parts and exhaust of vehicles: the driving wheels and rods of steam engines turning with
// the distance run, and puffs of steam and smoke that rise, drift with the wind, grow and fade.
// Purely visual: kept by the renderer between frames, never part of the simulation.

#include "core/types.hpp"
#include "render/gl_util.hpp"
#include "render/models.hpp"
#include "render/shader.hpp"
#include "render/vehicle_pose.hpp"

#include <glm/glm.hpp>

#include <optional>
#include <unordered_map>
#include <vector>

namespace tp::gfx {

class VehicleFx {
public:
    bool init();
    // Starts a frame: advances every puff and forgets vehicles not seen for a while.
    void beginFrame(double timeSeconds);
    // A train's cars have been placed: turns the steam engines' wheels and rods, and puffs steam
    // and smoke from the engines' chimneys and exhausts.
    void addTrain(InstanceBatch& batch, VehicleId id, const std::vector<CarPose>& cars, const glm::dvec3& origin);
    // A chimney smoking at `top` (world position); `key` tells chimneys apart between frames.
    void addStack(const glm::dvec3& top, u32 key);
    // Soft billboards, back to front, after the opaque scene (depth tested, not written).
    void draw(const glm::mat4& view, const glm::dvec3& origin);

    usize puffCount() const { return puffs_.size(); }

private:
    struct Puff {
        glm::dvec3 pos;
        glm::vec3 vel;
        float age = 0.0f, life = 3.0f;
        float size0 = 0.05f, size1 = 0.35f;
        float opacity = 0.7f;
        glm::vec3 colour{0.9f};
    };
    // Per vehicle: where its head was last frame and how far it has run.
    struct Motion {
        glm::dvec3 last{0.0};
        double odometer = 0.0; // tiles along the head car's heading
        double speed = 0.0;    // tiles per second, smoothed
        double seen = -1.0;
        double nextIdle = 0.0; // time of the next wisp while standing
        i64 beat = 0;          // exhaust beats so far (four per wheel turn)
        bool valid = false;
    };

    void emit(const Puff& p);
    float random();

    std::vector<Puff> puffs_;
    std::unordered_map<u32, Motion> motion_;
    std::unordered_map<u32, float> stacks_; // chimney key -> puffs owed
    double now_ = 0.0;
    float dt_ = 0.0f;
    u32 seed_ = 0x9E3779B9u;
    std::optional<Shader> shader_;
    VertexArray vao_;
    Buffer vbo_;
    std::vector<float> scratch_;
};

} // namespace tp::gfx
