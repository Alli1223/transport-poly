#pragma once
// Procedurally generated low poly model library and instanced drawing.

#include "core/types.hpp"
#include "render/gl_util.hpp"
#include "render/mesh.hpp"

#include <glm/glm.hpp>

#include <array>
#include <vector>

namespace tp::gfx {

enum class ModelId : u16 {
    TreeConifer0,
    TreeConifer1,
    TreeConifer2,
    TreeConiferSnow,
    TreeDeciduous0,
    TreeDeciduous1,
    TreeDeciduous2,
    Count
};

inline constexpr usize kModelCount = static_cast<usize>(ModelId::Count);

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

class ModelLibrary {
public:
    struct Range {
        GLint first = 0;
        GLsizei count = 0;
    };

    void build();
    Range range(ModelId id) const { return ranges_[static_cast<usize>(id)]; }

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
    void add(ModelId id, const ModelInstance& inst) { perModel_[static_cast<usize>(id)].push_back(inst); }
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

} // namespace tp::gfx
