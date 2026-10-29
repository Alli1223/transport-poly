#pragma once
// Meshes for free-form track: ballast, sleepers and rails following each curved edge, with
// bridges, tunnel portals and level crossings. Curved roads (road_curves.hpp) are drawn here too.

#include "render/mesh.hpp"
#include "render/shader.hpp"
#include "sim/world.hpp"

#include <glm/glm.hpp>

#include <unordered_map>

namespace tp::gfx {

// World height of a free-form track's rail top for a sample height (fine units).
float trackRailY(i32 fineHeight);

class TrackRenderer {
public:
    // Rebuilds meshes after track was built or removed.
    void sync(const World& world);
    // Draws every edge within `radius` of `focus` with the (already bound) terrain shader.
    usize draw(const Shader& shader, const glm::dvec3& origin, const glm::dvec3& focus, double radius, bool shadowPass) const;
    void clear() {
        meshes_.clear();
        concourses_.clear();
        roads_.clear();
        revision_ = ~0u;
        roadRevision_ = ~0u;
    }

private:
    struct EdgeMesh {
        GpuMesh mesh;
        u32 serial = 0; // edge ids are reused; the serial tells edges apart
        StationUse use = StationUse::Both; // a platform's look follows its station's use
        TilePos anchor;
        glm::dvec2 centre{0.0};
        double extent = 0.0;
    };
    std::unordered_map<u32, EdgeMesh> meshes_;
    std::unordered_map<u32, EdgeMesh> concourses_;
    u32 revision_ = ~0u;
    struct RoadMesh : EdgeMesh {
        TilePos a, b; // curve ids are reused: the ends tell curves apart
        Dir sideA = Dir::N, sideB = Dir::N;
    };
    std::unordered_map<u32, RoadMesh> roads_;
    u32 roadRevision_ = ~0u;
};

// Builds the mesh of one edge with vertices relative to tile `anchor` (the world gives the
// ground under bridges).
void buildTrackMesh(const World& world, const TrackEdge& edge, TilePos anchor, MeshBuilder& out);
void buildStationConcourseMesh(const World& world, StationId station, TilePos anchor, MeshBuilder& out);
// The road surface of a curved road: kerbs, asphalt and a dashed centre line.
void buildRoadCurveMesh(const World& world, const RoadCurve& curve, TilePos anchor, MeshBuilder& out);

} // namespace tp::gfx
