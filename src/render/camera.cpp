#include "render/camera.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace tp::gfx {

void Camera::setViewport(int width, int height) {
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
}

void Camera::snapToGoals() {
    target = targetGoal;
    yaw = yawGoal;
    pitch = pitchGoal;
    distance = distanceGoal;
}

void Camera::update(double dt) {
    const double k = 1.0 - std::exp(-dt * 14.0);
    target += (targetGoal - target) * k;
    yaw += (yawGoal - yaw) * k;
    pitch += (pitchGoal - pitch) * k;
    // Zoom smoothing in log space feels uniform at every distance.
    distance = std::exp(std::log(distance) + (std::log(distanceGoal) - std::log(distance)) * k);
}

glm::dvec3 Camera::eye() const {
    const double cp = std::cos(pitch);
    return target + distance * glm::dvec3(cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw));
}

glm::dvec3 Camera::forwardXZ() const { return {-std::sin(yaw), 0.0, -std::cos(yaw)}; }
glm::dvec3 Camera::rightXZ() const { return {std::cos(yaw), 0.0, -std::sin(yaw)}; }

double Camera::nearPlane() const { return std::max(0.2, distance * 0.02); }
double Camera::farPlane() const { return distance * 4.0 + 900.0; }

glm::mat4 Camera::viewMatrix(const glm::dvec3& origin) const {
    const glm::vec3 e(eye() - origin);
    const glm::vec3 t(target - origin);
    return glm::lookAt(e, t, glm::vec3(0, 1, 0));
}

glm::mat4 Camera::projectionMatrix() const {
    return glm::perspective(static_cast<float>(fovY), static_cast<float>(aspect()), static_cast<float>(nearPlane()),
                            static_cast<float>(farPlane()));
}

glm::dmat4 Camera::viewProjectionD(const glm::dvec3& origin) const {
    const glm::dmat4 view = glm::lookAt(eye() - origin, target - origin, glm::dvec3(0, 1, 0));
    const glm::dmat4 proj = glm::perspective(fovY, aspect(), nearPlane(), farPlane());
    return proj * view;
}

Ray Camera::screenRay(double px, double py) const {
    const glm::dvec3 origin = target;
    const glm::dmat4 inv = glm::inverse(viewProjectionD(origin));
    const double nx = 2.0 * px / width_ - 1.0;
    const double ny = 1.0 - 2.0 * py / height_;
    glm::dvec4 a = inv * glm::dvec4(nx, ny, -1.0, 1.0);
    glm::dvec4 b = inv * glm::dvec4(nx, ny, 1.0, 1.0);
    a /= a.w;
    b /= b.w;
    Ray r;
    r.origin = glm::dvec3(a) + origin;
    r.dir = glm::normalize(glm::dvec3(b - a));
    return r;
}

void Camera::pan(double right, double forward) { targetGoal += rightXZ() * right + forwardXZ() * forward; }

void Camera::orbit(double dyaw, double dpitch) {
    yawGoal += dyaw;
    pitchGoal = std::clamp(pitchGoal + dpitch, kMinPitch, kMaxPitch);
}

void Camera::zoom(double factor) { distanceGoal = std::clamp(distanceGoal * factor, kMinDistance, kMaxDistance); }

void Camera::lookAt(const glm::dvec3& worldPos) { targetGoal = worldPos; }

} // namespace tp::gfx
