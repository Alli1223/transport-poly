#pragma once
// RTS style orbit camera. The camera works in double precision world coordinates
// (x = tile x, y = height, z = tile y) and produces float matrices relative to a render origin
// so that precision does not degrade far from the world origin.

#include <glm/glm.hpp>

namespace tp::gfx {

struct Ray {
    glm::dvec3 origin{0.0};
    glm::dvec3 dir{0.0, -1.0, 0.0};
};

class Camera {
public:
    // Current (smoothed) state.
    glm::dvec3 target{0.0};
    double yaw = 0.6;
    double pitch = 0.85;
    double distance = 70.0;
    double fovY = 0.70;

    // Goals the smoothed state moves towards.
    glm::dvec3 targetGoal{0.0};
    double yawGoal = 0.6;
    double pitchGoal = 0.85;
    double distanceGoal = 70.0;

    static constexpr double kMinDistance = 6.0;
    static constexpr double kMaxDistance = 420.0;
    static constexpr double kMinPitch = 0.25;
    static constexpr double kMaxPitch = 1.45;

    void setViewport(int width, int height);
    int viewportWidth() const { return width_; }
    int viewportHeight() const { return height_; }
    double aspect() const { return static_cast<double>(width_) / static_cast<double>(height_ > 0 ? height_ : 1); }

    void snapToGoals();
    void update(double dt);

    glm::dvec3 eye() const;
    glm::dvec3 forwardXZ() const;
    glm::dvec3 rightXZ() const;
    double nearPlane() const;
    double farPlane() const;

    glm::mat4 viewMatrix(const glm::dvec3& origin) const;
    glm::mat4 projectionMatrix() const;
    glm::dmat4 viewProjectionD(const glm::dvec3& origin) const;
    // World space ray through a window pixel (top-left origin).
    Ray screenRay(double px, double py) const;

    // Control helpers acting on the goals.
    void pan(double right, double forward); // in world units
    void orbit(double dyaw, double dpitch);
    void zoom(double factor);
    void lookAt(const glm::dvec3& worldPos);

private:
    int width_ = 1;
    int height_ = 1;
};

} // namespace tp::gfx
