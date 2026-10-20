#include "test.hpp"
#ifdef TP_TEST_RENDERING
#include "core/serialize.hpp"
#include "render/models.hpp"
#include "render/person_pose.hpp"
#include "render/vehicle_pose.hpp"
#include "sim/road_path.hpp"
#include "sim/world.hpp"
#include <algorithm>
#include <cmath>
using namespace tp;

namespace {
glm::dvec3 forward(const gfx::VehiclePose& p) {
    return {std::cos(p.yaw) * std::cos(p.pitch), std::sin(p.pitch), -std::sin(p.yaw) * std::cos(p.pitch)};
}
Vehicle tram(Dir exit) {
    Vehicle v;
    v.model = 6;
    v.tile = {20, 20};
    v.entry = Dir::W;
    v.exit = exit;
    v.state = VehicleState::Driving;
    v.stopped = false;
    v.speed = 3;
    v.progress = pathLength(v.entry, v.exit) / 2;
    v.trail = {{{19, 20}, Dir::W, Dir::E}, {{18, 20}, Dir::W, Dir::E}};
    return v;
}
} // namespace

TEST_CASE("trams: bodies turn independently and their couplings stay attached on both corners") {
    World w;
    for (const auto exit : {Dir::N, Dir::S}) {
        auto v = tram(exit);
        for (double fraction : {0.0, 0.5, 1.0}) {
            std::vector<gfx::VehiclePose> parts;
            gfx::tramSectionPoses(w, v, fraction, parts);
            REQUIRE(parts.size() == 3);
            CHECK(std::abs(parts.front().yaw - parts.back().yaw) > 0.1f);
            for (usize i = 1; i < parts.size(); ++i) {
                const double half = gfx::tramSectionLength(v) * 0.5;
                const auto a = parts[i - 1].pos - forward(parts[i - 1]) * half;
                const auto b = parts[i].pos + forward(parts[i]) * half;
                CHECK(glm::length(a - b) < 0.00001);
            }
        }
    }
    auto v = tram(Dir::E);
    std::vector<gfx::VehiclePose> parts;
    gfx::tramSectionPoses(w, v, 0, parts);
    for (const auto& part : parts) CHECK(std::abs(part.yaw) < 0.0001f);
}

TEST_CASE("trams: articulation follows the previous corner continuously after changing tiles") {
    World w;
    auto before = tram(Dir::N);
    before.progress = pathLength(before.entry, before.exit);
    before.stopped = true;
    auto after = before;
    after.tile = {20, 19};
    after.entry = Dir::S;
    after.exit = Dir::N;
    after.progress = 0;
    after.trail.insert(after.trail.begin(), {before.tile, before.entry, before.exit});
    std::vector<gfx::VehiclePose> a, b;
    gfx::tramSectionPoses(w, before, 0, a);
    gfx::tramSectionPoses(w, after, 0, b);
    REQUIRE(a.size() == b.size());
    for (usize i = 0; i < a.size(); ++i) {
        CHECK(glm::length(a[i].pos - b[i].pos) < 0.0001);
        CHECK(std::abs(a[i].yaw - b[i].yaw) < 0.0001f);
    }
    Vehicle saved;
    fromBytes(toBytes(after, kSaveVersion), saved, kSaveVersion);
    CHECK(saved.trail == after.trail);
    b.clear();
    gfx::tramSectionPoses(w, saved, 0, b);
    for (usize i = 0; i < a.size(); ++i) CHECK(glm::length(a[i].pos - b[i].pos) < 0.0001);
}

TEST_CASE("trams: passengers stay inside their articulated sections with glazed solid-roof bodies") {
    World w;
    const auto id = w.vehicles.create(tram(Dir::N));
    auto& v = w.vehicles[id];
    for (u32 i = 0; i < v.capacity(); ++i) {
        Person p;
        p.serial = i + 1;
        p.vehicle = id;
        p.state = PersonState::Riding;
        v.passengers.push_back(w.people.create(p));
    }
    std::vector<gfx::VehiclePose> parts;
    gfx::tramSectionPoses(w, v, 0, parts);
    for (usize i = 0; i < v.passengers.size(); ++i) {
        const auto person = gfx::personPose(w, w.people[v.passengers[i]], 0);
        REQUIRE(person);
        const auto& part = parts[i * parts.size() / v.capacity()];
        const auto d = person->pos - part.pos;
        CHECK(std::abs(glm::dot(d, forward(part))) < gfx::tramSectionLength(v) * 0.5);
        CHECK(std::abs(d.x * std::sin(part.yaw) + d.z * std::cos(part.yaw)) < 0.09);
    }
    for (int section = 0; section < 3; ++section) {
        gfx::MeshBuilder mesh;
        gfx::buildTransportModels(mesh, gfx::models::tramSection(2, section));
        REQUIRE(!mesh.vertices.empty());
        CHECK(
            std::ranges::any_of(mesh.vertices, [](const gfx::Vertex& v) { return (v.color >> 24) == 253; }));
        CHECK(std::ranges::any_of(
            mesh.vertices, [](const gfx::Vertex& v) { return v.pos.y >= 0.27f && (v.color >> 24) != 253; }));
    }
}
#endif
