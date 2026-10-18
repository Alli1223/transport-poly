#pragma once
// Flat shaded low poly geometry: non-indexed triangle lists where every triangle carries its
// own face normal and colour.

#include "core/types.hpp"
#include "render/gl_util.hpp"

#include <glm/glm.hpp>

#include <span>
#include <vector>

namespace tp::gfx {

struct Vertex {
    glm::vec3 pos;
    u32 normal; // GL_INT_2_10_10_10_REV, normalised
    u32 color;  // RGBA8; alpha 0 = tintable, 254 = warm night emission, 255 = ordinary opaque
};
static_assert(sizeof(Vertex) == 20);

u32 packNormal(const glm::vec3& n);

constexpr u32 rgba(u8 r, u8 g, u8 b, u8 a = 255) {
    return static_cast<u32>(r) | (static_cast<u32>(g) << 8) | (static_cast<u32>(b) << 16) | (static_cast<u32>(a) << 24);
}
u32 rgbaf(const glm::vec3& c, float a = 1.0f);
glm::vec3 unpackRgb(u32 c);
// Multiplies the rgb channels (keeps alpha).
u32 shade(u32 color, float factor);
// Marks a colour as tintable: the instance colour replaces it, scaled by `brightness`.
constexpr u32 tintable(float brightness = 1.0f) {
    const u8 v = static_cast<u8>(brightness * 127.5f > 255.0f ? 255.0f : brightness * 127.5f);
    return rgba(v, v, v, 0);
}

class MeshBuilder {
public:
    std::vector<Vertex> vertices;
    glm::mat4 transform{1.0f};

    void clear() { vertices.clear(); }
    usize size() const { return vertices.size(); }

    // Counter-clockwise triangle (front face) in the current transform.
    void triangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, u32 color);
    // Triangle oriented so that its normal points up (+Y); for terrain-like surfaces.
    void triangleUp(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, u32 color);
    // Quad a-b-c-d counter-clockwise.
    void quad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, u32 color);
    // Quad a-b-c-d (any winding) oriented so that its normal faces `dir` (local space).
    void quadFacing(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, u32 color,
                    const glm::vec3& dir);
    // Axis aligned box in the current transform. `faces` bit mask: +X,-X,+Y,-Y,+Z,-Z.
    void box(const glm::vec3& min, const glm::vec3& max, u32 color, u32 faces = 0x3F);
    // Box with per-face colours (+X,-X,+Y,-Y,+Z,-Z).
    void box6(const glm::vec3& min, const glm::vec3& max, const u32 colors[6]);
    // Vertical prism with an n-gon cross section (cylinder for large n).
    void prism(const glm::vec3& baseCenter, float radius, float height, int sides, u32 color, bool caps = true,
               float angleOffset = 0.0f);
    // Cone / pyramid with apex above the base centre.
    void cone(const glm::vec3& baseCenter, float radius, float height, int sides, u32 color, float angleOffset = 0.0f);
    // Truncated cone (frustum).
    void frustum(const glm::vec3& baseCenter, float r0, float r1, float height, int sides, u32 color);
    // Low poly sphere (icosahedron, optionally subdivided once).
    void icosphere(const glm::vec3& center, float radius, u32 color, int subdivisions = 0, float squashY = 1.0f);
    // Gable roof over the rectangle [min.x,max.x] x [min.z,max.z] at height y, ridge along X.
    void gableRoofX(const glm::vec3& min, const glm::vec3& max, float y, float ridgeHeight, u32 roofColor,
                    u32 gableColor, float overhang = 0.05f);
    // Hip roof (pyramid-like with a ridge) over a rectangle.
    void hipRoof(const glm::vec3& min, const glm::vec3& max, float y, float ridgeHeight, u32 roofColor);

    void append(const MeshBuilder& other);

private:
    glm::vec3 apply(const glm::vec3& p) const { return glm::vec3(transform * glm::vec4(p, 1.0f)); }
};

// A static triangle list on the GPU with the standard vertex layout (locations 0-2).
class GpuMesh {
public:
    void upload(std::span<const Vertex> vertices);
    void draw() const;
    GLsizei vertexCount() const { return count_; }
    bool empty() const { return count_ == 0; }

private:
    VertexArray vao_;
    Buffer vbo_;
    GLsizei count_ = 0;
};

// Configures attribute locations 0-2 for the currently bound GL_ARRAY_BUFFER of Vertex.
void setupVertexAttributes();

} // namespace tp::gfx
