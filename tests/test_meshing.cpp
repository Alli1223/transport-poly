#include "test.hpp"
#ifdef TP_TEST_RENDERING
#include "render/terrain_mesher.hpp"
#include "render/models.hpp"
#include "sim/vehicle.hpp"
#include "sim/buildings.hpp"
#include <cstring>
#include <future>
using namespace tp;
TEST_CASE("meshing: immutable parallel terrain builds match serial geometry") {
    const TerrainGenerator gen(12345, true, true);
    Chunk chunk; gen.generateChunk({-2,3}, chunk);
    const auto build = [chunk,gen]() {
        return gfx::buildChunkGeometry(chunk, gen, [&](TilePos pos) {
            const i64 raw[] = {gen.rawHeight(pos.x,pos.y),gen.rawHeight(pos.x+1,pos.y),
                               gen.rawHeight(pos.x+1,pos.y+1),gen.rawHeight(pos.x,pos.y+1)};
            const u16 fine[] = {gen.cornerFine(pos.x,pos.y),gen.cornerFine(pos.x+1,pos.y),
                                gen.cornerFine(pos.x+1,pos.y+1),gen.cornerFine(pos.x,pos.y+1)};
            return gen.generateTile(pos,raw,fine).type == TileType::Water;
        });
    };
    auto worker = std::async(std::launch::async, build);
    const auto serial = build(), parallel = worker.get();
    CHECK_EQ(serial.ground.size(), parallel.ground.size());
    CHECK_EQ(serial.water.size(), parallel.water.size());
    CHECK_EQ(serial.trees.size(), parallel.trees.size());
    CHECK(std::memcmp(serial.ground.data(), parallel.ground.data(), serial.ground.size()*sizeof(gfx::Vertex)) == 0);
    if (!serial.water.empty())
        CHECK(std::memcmp(serial.water.data(), parallel.water.data(), serial.water.size()*sizeof(gfx::Vertex)) == 0);
}
TEST_CASE("models: every freight and passenger vehicle has geometry") {
    for (const auto& model : vehicleModels()) {
        const auto id = model.type == VehicleType::Road ? gfx::models::bus(model.visual) :
            model.type == VehicleType::Tram ? gfx::models::tram(model.visual) :
            model.type == VehicleType::Rail ? gfx::models::train(model.visual) : gfx::models::ship(model.visual);
        gfx::MeshBuilder mesh;
        gfx::buildTransportModels(mesh, id);
        CHECK(!mesh.vertices.empty());
    }
}
TEST_CASE("models: industry expansion adds geometry within its reserved plot") {
    for (u16 type = 15; type <= 28; ++type) {
        usize previous = 0;
        for (u8 stage = 0; stage < 3; ++stage) {
            gfx::MeshBuilder mesh;
            gfx::buildBuildingModel(mesh, type, stage);
            CHECK(mesh.vertices.size() > previous);
            previous = mesh.vertices.size();
            const float half = (buildingType(type).size + 2) * 0.5f;
            for (const auto& vertex : mesh.vertices) {
                CHECK(vertex.pos.x >= -half && vertex.pos.x <= half);
                CHECK(vertex.pos.z >= -half && vertex.pos.z <= half);
            }
        }
    }
}
#endif
