#include "test.hpp"
#ifdef TP_TEST_RENDERING
#include "render/models.hpp"
#include "render/person_pose.hpp"
#include "render/vehicle_pose.hpp"
#include "sim/world.hpp"
#include <cmath>
#include <limits>
using namespace tp;

namespace {
// Cast a side-on ray to verify the pane has an opening behind it, rather than an opaque wall.
u32 firstSurface(const gfx::MeshBuilder& mesh, glm::vec3 origin, glm::vec3 direction) {
    float nearest = std::numeric_limits<float>::max();
    u32 material = std::numeric_limits<u32>::max();
    for (usize i = 0; i + 2 < mesh.vertices.size(); i += 3) {
        const auto a = mesh.vertices[i].pos, e1 = mesh.vertices[i+1].pos - a, e2 = mesh.vertices[i+2].pos - a;
        const auto p = glm::cross(direction, e2);
        const float det = glm::dot(e1, p);
        if (std::abs(det) < 0.000001f) continue;
        const auto t = origin - a;
        const float u = glm::dot(t, p) / det;
        if (u < 0 || u > 1) continue;
        const auto q = glm::cross(t, e1);
        const float v = glm::dot(direction, q) / det;
        if (v < 0 || u+v > 1) continue;
        const float distance = glm::dot(e2, q) / det;
        if (distance < 0 || distance >= nearest) continue;
        nearest = distance; material = mesh.vertices[i].color >> 24;
    }
    return material;
}
}

TEST_CASE("passenger windows: coaches and trams have glazed openings and solid roofs") {
    for (const auto id : {gfx::models::train(3), gfx::models::train(4), gfx::models::train(5),
        gfx::models::train(6), gfx::models::train(7), gfx::models::train(8), gfx::models::train(9),
        gfx::models::train(14), gfx::models::train(15), gfx::models::tram(0), gfx::models::tram(1), gfx::models::tram(2)}) {
        gfx::MeshBuilder mesh;
        gfx::buildTransportModels(mesh, id);
        REQUIRE(!mesh.vertices.empty());
        int windows = 0;
        for (int i = 0; i < 15; ++i) {
            const float x = -0.29f + i * 0.039f;
            windows += firstSurface(mesh, {x, 0.19f, 1}, {0, 0, -1}) == 253 &&
                firstSurface(mesh, {x, 0.19f, 0.099f}, {0, 0, -1}) == 253;
        }
        CHECK(windows >= 7);
        CHECK(firstSurface(mesh, {-0.20f, 1, 0}, {0, -1, 0}) != 253);
        CHECK(firstSurface(mesh, {-0.20f, 1, 0}, {0, -1, 0}) != std::numeric_limits<u32>::max());
    }
}

TEST_CASE("passenger windows: occupants follow their own carriage length and both decks") {
    World w;
    Vehicle train; train.model = 7; train.cars = {7, 32, 37}; train.tile = {30, 8};
    train.state = VehicleState::Loading; train.progress = 250;
    for (i32 x = 30; x > 0; --x) train.trail.push_back({{x, 8}, Dir::W, Dir::E});
    const auto id = w.vehicles.create(train);
    for (int i = 0; i < 152; ++i) {
        Person p; p.serial = i + 1; p.vehicle = id; p.state = PersonState::Riding;
        w.vehicles[id].passengers.push_back(w.people.create(p));
    }
    std::vector<gfx::CarPose> cars;
    gfx::trainCarPoses(w, w.vehicles[id], 0, cars);
    REQUIRE(cars.size() == 3);
    int upper = 0, lower = 0;
    for (usize i = 0; i < 152; ++i) {
        const auto pose = gfx::personPose(w, w.people[w.vehicles[id].passengers[i]]);
        REQUIRE(pose);
        const auto& car = cars[i < 56 ? 1 : 2];
        const auto delta = pose->pos - car.pose.pos;
        CHECK(std::abs(delta.x) < vehicleModel(car.model).length / 32.0);
        CHECK(std::abs(delta.z) < 0.09);
        if (i >= 56) { upper += delta.y > 0.20; lower += delta.y < 0.20; }
    }
    CHECK_EQ(upper, 48); CHECK_EQ(lower, 48);
}
#endif
