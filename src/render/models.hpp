#pragma once
// Procedurally generated low poly model library and instanced drawing.

#include "core/types.hpp"
#include "render/gl_util.hpp"
#include "render/mesh.hpp"

#include <glm/glm.hpp>

#include <array>
#include <vector>

namespace tp::gfx {

// Models are identified by a small integer; ranges are reserved per family.
using ModelId = u16;

namespace models {
inline constexpr ModelId kTreeConifer0 = 0;
inline constexpr ModelId kTreeConifer1 = 1;
inline constexpr ModelId kTreeConifer2 = 2;
inline constexpr ModelId kTreeConiferSnow = 3;
inline constexpr ModelId kTreeDeciduous0 = 4;
inline constexpr ModelId kTreeDeciduous1 = 5;
inline constexpr ModelId kTreeDeciduous2 = 6;
inline constexpr ModelId kShipDepot = 7;
inline constexpr ModelId kBusStop = 8;
inline constexpr ModelId kRoadDepot = 9;
inline constexpr ModelId kTramDepot = 10;
inline constexpr ModelId kPerson = 11;
inline constexpr ModelId kRailDepot = 12;
inline constexpr ModelId kSignal = 13;
inline constexpr ModelId kPlatformCanopy = 14;
inline constexpr ModelId kDock = 15;

inline constexpr ModelId kBuildingBase = 16;
inline constexpr ModelId kBuildingVariants = 3;
inline constexpr ModelId kMaxBuildingTypes = 32;

inline constexpr ModelId kVehicleBase = kBuildingBase + kMaxBuildingTypes * kBuildingVariants;
inline constexpr ModelId kEffectBase = kVehicleBase + 48;
inline constexpr ModelId kSmoke = kEffectBase;
inline constexpr ModelId kStreetLamp = kEffectBase + 1;
inline constexpr ModelId kCount = kEffectBase + 8;

constexpr ModelId building(u16 type, u8 variant) {
    return static_cast<ModelId>(kBuildingBase + type * kBuildingVariants + variant % kBuildingVariants);
}
constexpr ModelId bus(int visual) { return static_cast<ModelId>(kVehicleBase + visual); }
constexpr ModelId tram(int visual) { return static_cast<ModelId>(kVehicleBase + 8 + visual); }
constexpr ModelId train(int visual) { return static_cast<ModelId>(kVehicleBase + 16 + visual); }
constexpr ModelId ship(int visual) { return static_cast<ModelId>(kVehicleBase + 32 + visual); }
} // namespace models

inline constexpr usize kModelCount = models::kCount;

// Per-instance data: a 3x4 affine transform (row major) and a tint colour.
struct ModelInstance {
    glm::vec4 row0;
    glm::vec4 row1;
    glm::vec4 row2;
    u32 tint;
};
static_assert(sizeof(ModelInstance) == 52);

ModelInstance makeInstance(const glm::vec3& pos, float yaw, float scale, u32 tint = rgba(255, 255, 255));
ModelInstance makeInstance(const glm::mat4& transform, u32 tint = rgba(255, 255, 255));

// Builds the mesh of a town building type / variant (footprint centred on the origin, entrance +Z).
void buildBuildingModel(MeshBuilder& mb, u16 type, u8 variant);
// Builds vehicle, stop, depot and person models (leaves `mb` empty for unknown ids).
void buildTransportModels(MeshBuilder& mb, ModelId id);

class ModelLibrary {
public:
    struct Range {
        GLint first = 0;
        GLsizei count = 0;
    };

    void build();
    Range range(ModelId id) const { return id < kModelCount ? ranges_[id] : Range{}; }

    // Draws `count` instances of a model whose instance data starts at `byteOffset` in `instanceBuffer`.
    void drawInstanced(ModelId id, GLuint instanceBuffer, usize byteOffset, GLsizei count) const;

private:
    VertexArray vao_;
    Buffer vbo_;
    std::array<Range, kModelCount> ranges_{};
};

// Collects per-frame dynamic instances grouped by model and draws them in one upload.
class InstanceBatch {
public:
    void clear();
    void add(ModelId id, const ModelInstance& inst) { perModel_[id].push_back(inst); }
    usize size() const;
    // Uploads all instances; must be called before draw() each frame.
    void upload();
    void draw(const ModelLibrary& lib) const;

private:
    std::array<std::vector<ModelInstance>, kModelCount> perModel_;
    std::array<usize, kModelCount> offsets_{};
    std::vector<ModelInstance> staging_;
    Buffer buffer_;
    usize capacity_ = 0;
};

// Static instances grouped by model in a single GPU buffer (used per chunk).
struct StaticInstances {
    struct Batch {
        ModelId model;
        u32 offset;
        u32 count;
    };
    Buffer buffer;
    std::vector<Batch> batches;
    usize total = 0;

    void upload(std::vector<std::pair<ModelId, ModelInstance>>& items);
    void draw(const ModelLibrary& lib) const;
};

} // namespace tp::gfx
