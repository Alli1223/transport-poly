#include "render/mesh.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace tp::gfx {

u32 packNormal(const glm::vec3& n) {
    const auto pack = [](float v) -> u32 {
        const int i = static_cast<int>(std::lround(std::clamp(v, -1.0f, 1.0f) * 511.0f));
        return static_cast<u32>(i) & 0x3FFu;
    };
    return pack(n.x) | (pack(n.y) << 10) | (pack(n.z) << 20);
}

u32 rgbaf(const glm::vec3& c, float a) {
    const auto to8 = [](float v) { return static_cast<u8>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return rgba(to8(c.r), to8(c.g), to8(c.b), to8(a));
}

glm::vec3 unpackRgb(u32 c) {
    return glm::vec3(static_cast<float>(c & 0xFF), static_cast<float>((c >> 8) & 0xFF),
                     static_cast<float>((c >> 16) & 0xFF)) /
           255.0f;
}

u32 shade(u32 color, float factor) {
    const glm::vec3 c = unpackRgb(color) * factor;
    return (rgbaf(c) & 0x00FFFFFFu) | (color & 0xFF000000u);
}

void MeshBuilder::triangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, u32 color) {
    const glm::vec3 pa = apply(a);
    const glm::vec3 pb = apply(b);
    const glm::vec3 pc = apply(c);
    glm::vec3 n = glm::cross(pb - pa, pc - pa);
    const float len = glm::length(n);
    n = len > 1e-12f ? n / len : glm::vec3(0, 1, 0);
    const u32 pn = packNormal(n);
    vertices.push_back({pa, pn, color});
    vertices.push_back({pb, pn, color});
    vertices.push_back({pc, pn, color});
}

void MeshBuilder::triangleUp(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, u32 color) {
    const glm::vec3 n = glm::cross(b - a, c - a);
    if (n.y >= 0.0f)
        triangle(a, b, c, color);
    else
        triangle(a, c, b, color);
}

namespace {
// Emits a triangle whose (local space) normal faces `dir`.
void facing(MeshBuilder& mb, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, u32 color,
            const glm::vec3& dir) {
    if (glm::dot(glm::cross(b - a, c - a), dir) >= 0.0f)
        mb.triangle(a, b, c, color);
    else
        mb.triangle(a, c, b, color);
}
} // namespace

void MeshBuilder::quad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, u32 color) {
    triangle(a, b, c, color);
    triangle(a, c, d, color);
}

void MeshBuilder::quadFacing(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d,
                             u32 color, const glm::vec3& dir) {
    facing(*this, a, b, c, color, dir);
    facing(*this, a, c, d, color, dir);
}

void MeshBuilder::box(const glm::vec3& mn, const glm::vec3& mx, u32 color, u32 faces) {
    const u32 colors[6] = {color, color, color, color, color, color};
    if (faces == 0x3F) {
        box6(mn, mx, colors);
        return;
    }
    const float x0 = mn.x, y0 = mn.y, z0 = mn.z, x1 = mx.x, y1 = mx.y, z1 = mx.z;
    if (faces & 0x01) quad({x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, color);
    if (faces & 0x02) quad({x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, color);
    if (faces & 0x04) quad({x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, color);
    if (faces & 0x08) quad({x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, color);
    if (faces & 0x10) quad({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, color);
    if (faces & 0x20) quad({x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, color);
}

void MeshBuilder::box6(const glm::vec3& mn, const glm::vec3& mx, const u32 colors[6]) {
    const float x0 = mn.x, y0 = mn.y, z0 = mn.z, x1 = mx.x, y1 = mx.y, z1 = mx.z;
    quad({x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, colors[0]);
    quad({x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, colors[1]);
    quad({x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, colors[2]);
    quad({x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, colors[3]);
    quad({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, colors[4]);
    quad({x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, colors[5]);
}

void MeshBuilder::prism(const glm::vec3& base, float radius, float height, int sides, u32 color, bool caps,
                        float angleOffset) {
    const glm::vec3 up(0, height, 0);
    for (int i = 0; i < sides; ++i) {
        const float t0 = angleOffset + glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(sides);
        const float t1 = angleOffset + glm::two_pi<float>() * static_cast<float>(i + 1) / static_cast<float>(sides);
        const glm::vec3 p0 = base + glm::vec3(std::cos(t0) * radius, 0, std::sin(t0) * radius);
        const glm::vec3 p1 = base + glm::vec3(std::cos(t1) * radius, 0, std::sin(t1) * radius);
        const glm::vec3 out = (p0 + p1) * 0.5f - base;
        facing(*this, p0, p1, p1 + up, color, out);
        facing(*this, p0, p1 + up, p0 + up, color, out);
        if (caps) {
            facing(*this, base + up, p0 + up, p1 + up, color, {0, 1, 0});
            facing(*this, base, p0, p1, color, {0, -1, 0});
        }
    }
}

void MeshBuilder::cone(const glm::vec3& base, float radius, float height, int sides, u32 color, float angleOffset) {
    const glm::vec3 apex = base + glm::vec3(0, height, 0);
    for (int i = 0; i < sides; ++i) {
        const float t0 = angleOffset + glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(sides);
        const float t1 = angleOffset + glm::two_pi<float>() * static_cast<float>(i + 1) / static_cast<float>(sides);
        const glm::vec3 p0 = base + glm::vec3(std::cos(t0) * radius, 0, std::sin(t0) * radius);
        const glm::vec3 p1 = base + glm::vec3(std::cos(t1) * radius, 0, std::sin(t1) * radius);
        facing(*this, p0, p1, apex, color, (p0 + p1) * 0.5f - base + glm::vec3(0, radius * 0.5f, 0));
        facing(*this, base, p0, p1, color, {0, -1, 0});
    }
}

void MeshBuilder::frustum(const glm::vec3& base, float r0, float r1, float height, int sides, u32 color) {
    const glm::vec3 up(0, height, 0);
    for (int i = 0; i < sides; ++i) {
        const float t0 = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(sides);
        const float t1 = glm::two_pi<float>() * static_cast<float>(i + 1) / static_cast<float>(sides);
        const glm::vec3 d0(std::cos(t0), 0, std::sin(t0));
        const glm::vec3 d1(std::cos(t1), 0, std::sin(t1));
        const glm::vec3 a = base + d0 * r0, b = base + d1 * r0;
        const glm::vec3 c = base + up + d1 * r1, d = base + up + d0 * r1;
        const glm::vec3 out = (d0 + d1) * 0.5f;
        facing(*this, a, b, c, color, out);
        facing(*this, a, c, d, color, out);
        facing(*this, base + up, d, c, color, {0, 1, 0});
        facing(*this, base, a, b, color, {0, -1, 0});
    }
}

void MeshBuilder::icosphere(const glm::vec3& center, float radius, u32 color, int subdivisions, float squashY) {
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    std::array<glm::vec3, 12> v = {glm::vec3(-1, t, 0), glm::vec3(1, t, 0),   glm::vec3(-1, -t, 0), glm::vec3(1, -t, 0),
                                   glm::vec3(0, -1, t), glm::vec3(0, 1, t),   glm::vec3(0, -1, -t), glm::vec3(0, 1, -t),
                                   glm::vec3(t, 0, -1), glm::vec3(t, 0, 1),   glm::vec3(-t, 0, -1), glm::vec3(-t, 0, 1)};
    for (auto& p : v) p = glm::normalize(p);
    static constexpr int kFaces[20][3] = {{0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11},
                                          {1, 5, 9},  {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                                          {3, 9, 4},  {3, 4, 2},  {3, 2, 6},   {3, 6, 8},  {3, 8, 9},
                                          {4, 9, 5},  {2, 4, 11}, {6, 2, 10},  {8, 6, 7},  {9, 8, 1}};
    const auto emit = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
        const glm::vec3 scale(radius, radius * squashY, radius);
        const glm::vec3 centroid = (a + b + c) / 3.0f;
        facing(*this, center + a * scale, center + b * scale, center + c * scale, color, centroid);
    };
    for (const auto& f : kFaces) {
        const glm::vec3 a = v[static_cast<usize>(f[0])], b = v[static_cast<usize>(f[1])], c = v[static_cast<usize>(f[2])];
        if (subdivisions <= 0) {
            emit(a, b, c);
        } else {
            const glm::vec3 ab = glm::normalize(a + b), bc = glm::normalize(b + c), ca = glm::normalize(c + a);
            emit(a, ab, ca);
            emit(b, bc, ab);
            emit(c, ca, bc);
            emit(ab, bc, ca);
        }
    }
}

void MeshBuilder::gableRoofX(const glm::vec3& mn, const glm::vec3& mx, float y, float ridgeHeight, u32 roofColor,
                             u32 gableColor, float overhang) {
    const float x0 = mn.x - overhang, x1 = mx.x + overhang;
    const float z0 = mn.z - overhang, z1 = mx.z + overhang;
    const float zm = (mn.z + mx.z) * 0.5f;
    const float yr = y + ridgeHeight;
    const float yo = y - ridgeHeight * overhang / std::max(0.01f, (mx.z - mn.z) * 0.5f);
    facing(*this, {x0, yo, z0}, {x1, yo, z0}, {x1, yr, zm}, roofColor, {0, 1, -1});
    facing(*this, {x0, yo, z0}, {x1, yr, zm}, {x0, yr, zm}, roofColor, {0, 1, -1});
    facing(*this, {x0, yo, z1}, {x1, yr, zm}, {x1, yo, z1}, roofColor, {0, 1, 1});
    facing(*this, {x0, yo, z1}, {x0, yr, zm}, {x1, yr, zm}, roofColor, {0, 1, 1});
    facing(*this, {mn.x, y, mn.z}, {mn.x, y, mx.z}, {mn.x, yr, zm}, gableColor, {-1, 0, 0});
    facing(*this, {mx.x, y, mn.z}, {mx.x, y, mx.z}, {mx.x, yr, zm}, gableColor, {1, 0, 0});
}

void MeshBuilder::hipRoof(const glm::vec3& mn, const glm::vec3& mx, float y, float ridgeHeight, u32 roofColor) {
    const float dx = mx.x - mn.x, dz = mx.z - mn.z;
    const float yr = y + ridgeHeight;
    if (dx >= dz) {
        const float zm = (mn.z + mx.z) * 0.5f;
        const float ra = mn.x + dz * 0.5f, rb = mx.x - dz * 0.5f;
        const glm::vec3 r0(ra, yr, zm), r1(rb, yr, zm);
        facing(*this, {mn.x, y, mn.z}, {mx.x, y, mn.z}, r1, roofColor, {0, 1, -1});
        facing(*this, {mn.x, y, mn.z}, r1, r0, roofColor, {0, 1, -1});
        facing(*this, {mn.x, y, mx.z}, r1, {mx.x, y, mx.z}, roofColor, {0, 1, 1});
        facing(*this, {mn.x, y, mx.z}, r0, r1, roofColor, {0, 1, 1});
        facing(*this, {mn.x, y, mn.z}, r0, {mn.x, y, mx.z}, roofColor, {-1, 1, 0});
        facing(*this, {mx.x, y, mn.z}, {mx.x, y, mx.z}, r1, roofColor, {1, 1, 0});
    } else {
        const float xm = (mn.x + mx.x) * 0.5f;
        const float ra = mn.z + dx * 0.5f, rb = mx.z - dx * 0.5f;
        const glm::vec3 r0(xm, yr, ra), r1(xm, yr, rb);
        facing(*this, {mn.x, y, mn.z}, {mn.x, y, mx.z}, r1, roofColor, {-1, 1, 0});
        facing(*this, {mn.x, y, mn.z}, r1, r0, roofColor, {-1, 1, 0});
        facing(*this, {mx.x, y, mn.z}, r1, {mx.x, y, mx.z}, roofColor, {1, 1, 0});
        facing(*this, {mx.x, y, mn.z}, r0, r1, roofColor, {1, 1, 0});
        facing(*this, {mn.x, y, mn.z}, r0, {mx.x, y, mn.z}, roofColor, {0, 1, -1});
        facing(*this, {mn.x, y, mx.z}, {mx.x, y, mx.z}, r1, roofColor, {0, 1, 1});
    }
}

void MeshBuilder::append(const MeshBuilder& other) {
    vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
}

void setupVertexAttributes() {
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_INT_2_10_10_10_REV, GL_TRUE, sizeof(Vertex), reinterpret_cast<const void*>(12));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<const void*>(16));
}

void GpuMesh::upload(std::span<const Vertex> vertices) {
    if (!vao_) {
        vao_ = makeVertexArray();
        vbo_ = makeBuffer();
        glBindVertexArray(vao_.get());
        glBindBuffer(GL_ARRAY_BUFFER, vbo_.get());
        setupVertexAttributes();
    } else {
        glBindVertexArray(vao_.get());
        glBindBuffer(GL_ARRAY_BUFFER, vbo_.get());
    }
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size_bytes()), vertices.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    count_ = static_cast<GLsizei>(vertices.size());
}

void GpuMesh::draw() const {
    if (count_ == 0) return;
    glBindVertexArray(vao_.get());
    glDrawArrays(GL_TRIANGLES, 0, count_);
}

} // namespace tp::gfx
