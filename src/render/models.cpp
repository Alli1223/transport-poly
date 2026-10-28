#include "render/models.hpp"

#include "sim/buildings.hpp"
#include "sim/industries.hpp"

#include <algorithm>
#include <cmath>

namespace tp::gfx {

ModelInstance makeInstance(const glm::vec3& pos, float yaw, float scale, u32 tint) {
    const float c = std::cos(yaw) * scale;
    const float s = std::sin(yaw) * scale;
    return ModelInstance{{c, 0.0f, s, pos.x}, {0.0f, scale, 0.0f, pos.y}, {-s, 0.0f, c, pos.z}, tint};
}

ModelInstance makeInstance(const glm::mat4& m, u32 tint) {
    return ModelInstance{{m[0][0], m[1][0], m[2][0], m[3][0]},
                         {m[0][1], m[1][1], m[2][1], m[3][1]},
                         {m[0][2], m[1][2], m[2][2], m[3][2]},
                         tint};
}

namespace {

constexpr u32 kTrunk = rgba(110, 78, 52);

void buildConifer(MeshBuilder& mb, int variant, bool snowy) {
    const u32 greens[3] = {rgba(46, 104, 58), rgba(56, 116, 62), rgba(40, 92, 64)};
    const u32 g = greens[variant % 3];
    const u32 top = snowy ? rgba(236, 242, 248) : shade(g, 1.08f);
    mb.prism({0, 0, 0}, 0.05f, 0.25f, 5, kTrunk, false);
    const float s = 1.0f + 0.1f * static_cast<float>(variant);
    mb.cone({0, 0.18f, 0}, 0.34f, 0.55f * s, 7, g, 0.3f);
    mb.cone({0, 0.46f * s, 0}, 0.26f, 0.48f * s, 7, shade(g, 1.05f), 0.8f);
    mb.cone({0, 0.74f * s, 0}, 0.17f, 0.4f * s, 6, top, 0.1f);
}

void buildDeciduous(MeshBuilder& mb, int variant) {
    const u32 greens[3] = {rgba(92, 152, 62), rgba(112, 164, 64), rgba(76, 136, 56)};
    const u32 g = greens[variant % 3];
    mb.prism({0, 0, 0}, 0.06f, 0.38f, 5, kTrunk, false);
    if (variant == 2) {
        mb.icosphere({0.08f, 0.62f, 0.02f}, 0.27f, g, 0, 0.85f);
        mb.icosphere({-0.1f, 0.52f, -0.06f}, 0.22f, shade(g, 0.93f), 0, 0.9f);
    } else {
        mb.icosphere({0, 0.62f, 0}, 0.32f + 0.03f * static_cast<float>(variant), g, 0, 0.9f);
    }
}

void buildPalm(MeshBuilder& mb, int variant) {
    // A leaning, segmented trunk under a crown of drooping fronds.
    const float lean = variant ? -0.06f : 0.07f;
    glm::vec3 at(0.0f);
    for (int i = 0; i < 4; ++i) {
        mb.frustum(at, 0.055f - i * 0.006f, 0.05f - i * 0.006f, 0.17f, 5, i % 2 ? rgba(128, 98, 66) : rgba(112, 86, 58));
        at += glm::vec3(lean * (0.4f + i * 0.25f), 0.17f, lean * 0.3f);
    }
    const u32 frond = variant ? rgba(64, 138, 58) : rgba(78, 150, 62);
    for (int k = 0; k < 7; ++k) {
        const float a = k * 6.2831853f / 7.0f + variant * 0.4f;
        const glm::vec3 dir(std::cos(a), 0.0f, std::sin(a));
        const glm::vec3 side(-dir.z, 0.0f, dir.x);
        const glm::vec3 mid = at + dir * 0.22f + glm::vec3(0, 0.05f, 0);
        const glm::vec3 tip = at + dir * 0.42f + glm::vec3(0, -0.12f, 0);
        mb.triangle(at, mid + side * 0.07f, mid - side * 0.07f, frond);
        mb.triangle(at, mid - side * 0.07f, mid + side * 0.07f, shade(frond, 0.85f));
        mb.triangle(mid - side * 0.07f, mid + side * 0.07f, tip, frond);
        mb.triangle(mid + side * 0.07f, mid - side * 0.07f, tip, shade(frond, 0.85f));
    }
    mb.icosphere(at + glm::vec3(0, -0.02f, 0), 0.05f, rgba(96, 72, 44));
}

void buildCactus(MeshBuilder& mb, int variant) {
    const u32 g = variant ? rgba(92, 140, 76) : rgba(80, 128, 70);
    const float h = variant ? 0.42f : 0.55f;
    mb.prism({0, 0, 0}, 0.06f, h, 6, g);
    mb.icosphere({0, h, 0}, 0.06f, g, 0, 0.7f);
    // Arms bending upwards.
    for (const float side : {-1.0f, 1.0f}) {
        if (variant && side > 0) continue;
        const float y = h * (side > 0 ? 0.45f : 0.3f);
        mb.box({side > 0 ? 0.0f : -0.15f, y - 0.03f, -0.03f}, {side > 0 ? 0.15f : 0.0f, y + 0.03f, 0.03f}, shade(g, 0.95f));
        mb.prism({side * 0.15f, y, 0}, 0.04f, h * 0.35f, 6, g);
        mb.icosphere({side * 0.15f, y + h * 0.35f, 0}, 0.04f, g, 0, 0.7f);
    }
}

void buildAcacia(MeshBuilder& mb, int variant) {
    // Thin forked trunk and a wide, flat crown.
    const u32 g = variant ? rgba(120, 146, 64) : rgba(104, 136, 58);
    mb.frustum({0, 0, 0}, 0.04f, 0.03f, 0.36f, 5, kTrunk);
    mb.frustum({0, 0.3f, 0}, 0.025f, 0.02f, 0.16f, 4, kTrunk);
    mb.icosphere({0.03f, 0.5f, 0.0f}, 0.38f + 0.05f * variant, g, 0, 0.22f);
    mb.icosphere({-0.12f, 0.46f, 0.08f}, 0.22f, shade(g, 0.92f), 0, 0.25f);
}

void buildShrub(MeshBuilder& mb, int variant) {
    const u32 g = variant ? rgba(132, 124, 78) : rgba(104, 112, 70);
    mb.icosphere({0, 0.08f, 0}, 0.14f, g, 0, 0.7f);
    mb.icosphere({0.1f, 0.06f, 0.05f}, 0.1f, shade(g, 0.9f), 0, 0.7f);
    mb.icosphere({-0.08f, 0.05f, -0.06f}, 0.09f, shade(g, 1.08f), 0, 0.7f);
}

void buildBirch(MeshBuilder& mb, int variant) {
    // A slender white trunk with dark bands under a light, narrow crown.
    const u32 bark = rgba(226, 222, 210), band = rgba(60, 58, 56);
    const u32 greens[3] = {rgba(150, 186, 84), rgba(132, 176, 78), rgba(170, 192, 92)};
    const u32 g = greens[variant % 3];
    const float h = 0.5f + 0.06f * static_cast<float>(variant);
    mb.prism({0, 0, 0}, 0.035f, h, 5, bark, false);
    for (const float y : {0.12f, 0.27f, 0.4f}) mb.prism({0, y, 0}, 0.037f, 0.025f, 5, band, false);
    mb.icosphere({0.02f, h + 0.06f, 0}, 0.2f, g, 0, 1.25f);
    mb.icosphere({-0.05f, h - 0.1f, 0.03f}, 0.17f, shade(g, 0.92f), 0, 1.1f);
    if (variant == 1) mb.icosphere({0.06f, h + 0.24f, -0.02f}, 0.12f, shade(g, 1.06f), 0, 1.2f);
}

void buildPine(MeshBuilder& mb, int variant) {
    // A tall bare trunk with flat clumps of needles at the top, like a Scots pine.
    const u32 bark = rgba(150, 90, 58);
    const u32 g = variant ? rgba(52, 96, 60) : rgba(60, 106, 58);
    const float h = variant ? 0.8f : 0.7f;
    mb.frustum({0, 0, 0}, 0.045f, 0.03f, h, 5, bark);
    mb.icosphere({0.0f, h + 0.04f, 0.0f}, 0.26f, g, 0, 0.45f);
    mb.icosphere({0.13f, h - 0.1f, 0.06f}, 0.17f, shade(g, 0.92f), 0, 0.5f);
    mb.icosphere({-0.12f, h - 0.04f, -0.08f}, 0.16f, shade(g, 1.06f), 0, 0.5f);
}

void buildWillow(MeshBuilder& mb, int variant) {
    // A short, stout trunk under a broad dome whose fronds hang almost to the ground.
    const u32 g = variant ? rgba(140, 170, 92) : rgba(124, 160, 84);
    mb.frustum({0, 0, 0}, 0.07f, 0.05f, 0.3f, 6, kTrunk);
    mb.icosphere({0, 0.48f, 0}, 0.34f, g, 0, 0.7f);
    mb.frustum({0, 0.08f, 0}, 0.4f, 0.3f, 0.36f, 9, shade(g, 0.88f));
}

void buildBush(MeshBuilder& mb, int variant) {
    // A green, leafy bush; the second flowers in spring colours.
    const u32 g = variant ? rgba(78, 134, 62) : rgba(92, 146, 66);
    mb.icosphere({0, 0.11f, 0}, 0.17f, g, 0, 0.75f);
    mb.icosphere({0.13f, 0.08f, 0.06f}, 0.12f, shade(g, 0.9f), 0, 0.8f);
    mb.icosphere({-0.1f, 0.08f, -0.09f}, 0.12f, shade(g, 1.08f), 0, 0.8f);
    if (variant) {
        const u32 bloom[3] = {rgba(244, 238, 240), rgba(236, 150, 188), rgba(246, 214, 92)};
        static constexpr float kAt[5][3] = {{0.05f, 0.22f, 0.04f}, {-0.09f, 0.17f, 0.08f}, {0.15f, 0.15f, -0.05f},
                                            {-0.12f, 0.14f, -0.1f}, {0.0f, 0.19f, -0.12f}};
        for (int i = 0; i < 5; ++i) mb.icosphere({kAt[i][0], kAt[i][1], kAt[i][2]}, 0.03f, bloom[i % 3]);
    }
}

// Ground cover: lit as if facing the sky, like the ground under it, so it blends in rather than
// showing every blade's face.
void coverVertex(MeshBuilder& mb, glm::vec3 p, u32 color) { mb.vertices.push_back({p, packNormal({0, 1, 0}), color}); }
// One face: ground cover is drawn without back face culling, as blades and petals are seen
// from either side.
void coverTriangle(MeshBuilder& mb, glm::vec3 a, glm::vec3 b, glm::vec3 c, u32 ca, u32 cb, u32 cc) {
    coverVertex(mb, a, ca);
    coverVertex(mb, b, cb);
    coverVertex(mb, c, cc);
}

// A thin blade from `base` leaning `lean` towards `yaw`, dark at its foot and light at its tip.
void blade(MeshBuilder& mb, glm::vec3 base, float yaw, float height, float width, float lean, u32 foot, u32 tip) {
    const glm::vec3 dir(std::cos(yaw), 0.0f, std::sin(yaw)), side(-dir.z, 0.0f, dir.x);
    const glm::vec3 top = base + dir * lean + glm::vec3(0, height, 0);
    coverTriangle(mb, base + side * width, base - side * width, top, foot, foot, tip);
}

// A deterministic scatter for model building (no simulation state).
float scatter(int i, int salt) {
    // Unsigned so the multiplications wrap: signed overflow is undefined and GCC turned the
    // callers' loops into infinite ones.
    const unsigned h = static_cast<unsigned>(i) * 374761393u + static_cast<unsigned>(salt) * 668265263u;
    return static_cast<float>(((h ^ (h >> 13)) * 1274126177u) >> 8 & 0xFFFF) / 65535.0f;
}

void buildGrass(MeshBuilder& mb, int variant) {
    const int blades = 7 + variant * 3;
    for (int i = 0; i < blades; ++i) {
        const float a = scatter(i, variant) * 6.2831853f, r = scatter(i, variant + 7) * 0.07f;
        const glm::vec3 base(std::cos(a) * r, 0.0f, std::sin(a) * r);
        const float h = 0.07f + scatter(i, variant + 13) * (0.05f + 0.03f * variant);
        blade(mb, base, a + 0.6f * (scatter(i, variant + 21) - 0.5f), h, 0.011f, h * 0.35f, tintable(0.78f), tintable(1.22f));
    }
}

void buildFlowers(MeshBuilder& mb, int variant) {
    static constexpr u32 kPetal[4] = {rgba(246, 244, 236), rgba(248, 210, 60), rgba(150, 110, 200), rgba(214, 52, 48)};
    static constexpr u32 kHeart[4] = {rgba(240, 190, 50), rgba(220, 150, 40), rgba(236, 220, 120), rgba(40, 30, 30)};
    for (int i = 0; i < 5; ++i) blade(mb, {0, 0, 0}, scatter(i, 40 + variant) * 6.28f, 0.08f, 0.012f, 0.04f, tintable(0.8f), tintable(1.1f));
    const int heads = 4 + (variant % 2);
    for (int i = 0; i < heads; ++i) {
        const float a = scatter(i, 50 + variant) * 6.2831853f, r = 0.02f + scatter(i, 60 + variant) * 0.07f;
        const glm::vec3 base(std::cos(a) * r, 0.0f, std::sin(a) * r);
        const float h = 0.1f + scatter(i, 70 + variant) * 0.08f;
        blade(mb, base, a, h, 0.007f, 0.012f, tintable(0.8f), tintable(1.05f));
        // Five petals around a heart, facing up.
        const glm::vec3 c = base + glm::vec3(0.0f, h, 0.0f);
        const float size = variant == 3 ? 0.05f : 0.04f;
        for (int k = 0; k < 5; ++k) {
            const float p0 = (k + 0.0f) * 1.2566371f + a, p1 = (k + 0.5f) * 1.2566371f + a, p2 = (k + 1.0f) * 1.2566371f + a;
            const glm::vec3 e0 = c + glm::vec3(std::cos(p0), 0.0f, std::sin(p0)) * size * 0.4f;
            const glm::vec3 tip = c + glm::vec3(std::cos(p1), 0.004f, std::sin(p1)) * size;
            const glm::vec3 e1 = c + glm::vec3(std::cos(p2), 0.0f, std::sin(p2)) * size * 0.4f;
            coverTriangle(mb, e0, tip, e1, kPetal[variant], kPetal[variant], kPetal[variant]);
        }
        coverTriangle(mb, c + glm::vec3(0.008f, 0.003f, 0), c + glm::vec3(-0.005f, 0.003f, 0.007f), c + glm::vec3(-0.005f, 0.003f, -0.007f),
                      kHeart[variant], kHeart[variant], kHeart[variant]);
    }
}

void buildFern(MeshBuilder& mb, int variant) {
    // Fronds arching out from the middle, each a long leaf of two faces.
    const int fronds = 6 + variant;
    for (int i = 0; i < fronds; ++i) {
        const float a = static_cast<float>(i) / fronds * 6.2831853f + scatter(i, 80 + variant) * 0.5f;
        const glm::vec3 dir(std::cos(a), 0.0f, std::sin(a)), side(-dir.z, 0.0f, dir.x);
        const float len = 0.13f + scatter(i, 90 + variant) * 0.05f;
        const glm::vec3 mid = dir * (len * 0.55f) + glm::vec3(0, 0.09f, 0), tip = dir * len + glm::vec3(0, 0.05f, 0);
        coverTriangle(mb, {0, 0.01f, 0}, mid + side * 0.03f, mid - side * 0.03f, tintable(0.75f), tintable(1.0f), tintable(1.0f));
        coverTriangle(mb, mid + side * 0.03f, tip, mid - side * 0.03f, tintable(1.0f), tintable(1.2f), tintable(1.0f));
    }
}

void buildReeds(MeshBuilder& mb, int variant) {
    const u32 foot = rgba(88, 112, 58), tip = rgba(160, 168, 96), head = rgba(110, 72, 44);
    const int stems = 8 + variant * 2;
    for (int i = 0; i < stems; ++i) {
        const float a = scatter(i, 100 + variant) * 6.2831853f, r = scatter(i, 110 + variant) * 0.08f;
        const glm::vec3 base(std::cos(a) * r, 0.0f, std::sin(a) * r);
        const float h = 0.18f + scatter(i, 120 + variant) * 0.12f;
        blade(mb, base, a, h, 0.008f, 0.02f, foot, tip);
        if (i % 3 == 0) {
            const glm::vec3 c = base + glm::vec3(std::cos(a) * 0.015f, h * 0.82f, std::sin(a) * 0.015f);
            mb.box(c - glm::vec3(0.008f, 0.0f, 0.008f), c + glm::vec3(0.008f, 0.04f, 0.008f), head);
        }
    }
}

void buildHeather(MeshBuilder& mb, int variant) {
    // A low mound of tiny flowers over a few green shoots.
    const u32 bloom[2] = {rgba(168, 96, 164), rgba(196, 128, 176)};
    for (int i = 0; i < 5; ++i) blade(mb, {0, 0, 0}, scatter(i, 130 + variant) * 6.28f, 0.05f, 0.012f, 0.04f, tintable(0.7f), tintable(0.95f));
    for (int i = 0; i < 9; ++i) {
        const float a = scatter(i, 140 + variant) * 6.2831853f, r = scatter(i, 150 + variant) * 0.07f;
        const glm::vec3 c(std::cos(a) * r, 0.025f + scatter(i, 160 + variant) * 0.035f, std::sin(a) * r);
        const u32 col = shade(bloom[variant], 0.85f + scatter(i, 170) * 0.3f);
        coverTriangle(mb, c + glm::vec3(0.02f, 0, 0), c + glm::vec3(-0.012f, 0.012f, 0.016f), c + glm::vec3(-0.012f, 0.004f, -0.016f), col, col, col);
    }
}

void buildPebbles(MeshBuilder& mb, int variant) {
    for (int i = 0; i < 3 + variant; ++i) {
        const float a = scatter(i, 180 + variant) * 6.2831853f, r = scatter(i, 190 + variant) * 0.1f;
        const float size = 0.018f + scatter(i, 200 + variant) * 0.025f;
        const u32 grey = shade(rgba(150, 146, 140), 0.8f + scatter(i, 210 + variant) * 0.35f);
        mb.cone({std::cos(a) * r, -0.004f, std::sin(a) * r}, size, size * 0.9f, 5, grey, a);
    }
}

void buildCloud(MeshBuilder& mb, int variant) {
    // A flat-bottomed heap of puffs, about one unit across.
    const u32 white = rgba(250, 251, 255);
    struct Puff {
        float x, y, z, r;
    };
    static constexpr Puff kPuffs[3][6] = {
        {{0, 0.1f, 0, 0.42f}, {0.38f, 0.05f, 0.08f, 0.3f}, {-0.36f, 0.04f, -0.05f, 0.32f}, {0.1f, 0.28f, 0.05f, 0.3f}, {-0.12f, 0.02f, 0.3f, 0.26f}, {0.2f, 0.02f, -0.3f, 0.24f}},
        {{0, 0.08f, 0, 0.36f}, {0.42f, 0.04f, 0, 0.34f}, {0.78f, 0.02f, 0.06f, 0.24f}, {-0.36f, 0.04f, 0.04f, 0.28f}, {0.22f, 0.24f, 0.02f, 0.26f}, {-0.05f, 0.02f, -0.26f, 0.22f}},
        {{0, 0.12f, 0, 0.46f}, {0.3f, 0.3f, 0.1f, 0.3f}, {-0.32f, 0.06f, 0.2f, 0.3f}, {0.4f, 0.04f, -0.22f, 0.28f}, {-0.4f, 0.02f, -0.25f, 0.26f}, {0.02f, 0.02f, 0.42f, 0.24f}},
    };
    for (const Puff& p : kPuffs[variant % 3]) mb.icosphere({p.x, p.y, p.z}, p.r, white, 1, 0.62f);
}

} // namespace

void ModelLibrary::build() {
    std::vector<Vertex> all;
    const auto add = [&](ModelId id, const MeshBuilder& mb) {
        for (const bool glass : {false, true}) {
            const auto begin = all.size();
            for (usize i = 0; i + 2 < mb.vertices.size(); i += 3) {
                if (((mb.vertices[i].color >> 24) == 253) != glass) continue;
                all.insert(all.end(), mb.vertices.begin() + i, mb.vertices.begin() + i + 3);
            }
            (glass ? glassRanges_ : ranges_)[id] = {static_cast<GLint>(begin),
                                                    static_cast<GLsizei>(all.size() - begin)};
        }
    };
    // Opaque vertices of trees take alpha 250, so the shaders sway them in the wind.
    const auto swaying = [](MeshBuilder& mb) {
        for (Vertex& vertex : mb.vertices)
            if ((vertex.color >> 24) == 255) vertex.color = (vertex.color & 0x00FFFFFFu) | (250u << 24);
        return mb;
    };
    for (int v = 0; v < 3; ++v) {
        MeshBuilder mb;
        buildConifer(mb, v, false);
        add(static_cast<ModelId>(models::kTreeConifer0 + v), swaying(mb));
    }
    {
        MeshBuilder mb;
        buildConifer(mb, 1, true);
        add(models::kTreeConiferSnow, swaying(mb));
    }
    for (int v = 0; v < 3; ++v) {
        MeshBuilder mb;
        buildDeciduous(mb, v);
        add(static_cast<ModelId>(models::kTreeDeciduous0 + v), swaying(mb));
    }
    for (int v = 0; v < 2; ++v) {
        const auto make = [&](ModelId base, void (*fn)(MeshBuilder&, int)) {
            MeshBuilder mb;
            fn(mb, v);
            add(static_cast<ModelId>(base + v), base == models::kTreeCactus0 ? mb : swaying(mb));
        };
        make(models::kTreePalm0, buildPalm);
        make(models::kTreeCactus0, buildCactus);
        make(models::kTreeAcacia0, buildAcacia);
        make(models::kShrub0, buildShrub);
        make(models::kTreePine0, buildPine);
        make(models::kTreeWillow0, buildWillow);
        make(models::kBush0, buildBush);
    }
    for (int v = 0; v < 3; ++v) {
        MeshBuilder mb;
        buildBirch(mb, v);
        add(static_cast<ModelId>(models::kTreeBirch0 + v), swaying(mb));
    }
    // Ground cover sways and shrinks away by its instances (see the model shader), not its vertices.
    for (int v = 0; v < 4; ++v) {
        const auto make = [&](ModelId base, int variants, void (*fn)(MeshBuilder&, int)) {
            if (v >= variants) return;
            MeshBuilder mb;
            fn(mb, v);
            add(static_cast<ModelId>(base + v), mb);
        };
        make(models::kGrass0, 3, buildGrass);
        make(models::kFlowers0, 4, buildFlowers);
        make(models::kFern0, 2, buildFern);
        make(models::kReeds0, 2, buildReeds);
        make(models::kHeather0, 2, buildHeather);
        make(models::kPebbles0, 2, buildPebbles);
    }
    for (int v = 0; v < 3; ++v) {
        MeshBuilder mb;
        buildCloud(mb, v);
        add(static_cast<ModelId>(models::kCloud0 + v), mb);
    }
    for (const ModelId id :
         {models::kBusStop, models::kRoadDepot, models::kTramDepot, models::kPerson, models::kRailDepot,
          models::kSignal, models::kPlatformCanopy, models::kDock, models::kShipDepot, models::kSmoke,
          models::kStreetLamp, models::kCrate, models::kGrain, models::kFoodLogo, models::kGoodsLogo,
          models::kStationEntrance, models::kFootbridge, models::kSeatedPerson, models::kStationApproach}) {
        MeshBuilder mb;
        buildTransportModels(mb, id);
        add(id, mb);
    }
    for (int v = 0; v < 8; ++v) {
        for (const ModelId id :
             {models::bus(v), models::tram(v), models::train(v), models::train(v + 8), models::ship(v)}) {
            MeshBuilder mb;
            buildTransportModels(mb, id);
            if (!mb.vertices.empty()) add(id, mb);
        }
    }
    for (ModelId id = models::kTramSectionBase; id <= models::kTramBellows; ++id) {
        MeshBuilder mb;
        buildTransportModels(mb, id);
        if (!mb.vertices.empty()) add(id, mb);
    }
    for (const ModelId id : {models::kSteamWheelset, models::kSideRod, models::kMainRod, models::kCatenaryMast,
                             models::kCatenaryWire, models::kPantograph}) {
        MeshBuilder mb;
        buildTransportModels(mb, id);
        add(id, mb);
    }
    for (ModelId id = models::kPrivateCarBase; id < models::kPrivateCarBase + 3; ++id) {
        MeshBuilder mb;
        buildTransportModels(mb, id);
        add(id, mb);
    }
    const auto types = buildingTypes();
    for (u16 t = 0; t < types.size() && t < models::kMaxBuildingTypes; ++t) {
        for (u8 v = 0; v < models::kBuildingVariants; ++v) {
            MeshBuilder mb;
            buildBuildingModel(mb, t, industries::isCountryside(t) ? v : static_cast<u8>(v % std::max<u8>(1, types[t].variants)));
            add(models::building(t, v), mb);
        }
    }

    vao_ = makeVertexArray();
    vbo_ = makeBuffer();
    glBindVertexArray(vao_.get());
    glBindBuffer(GL_ARRAY_BUFFER, vbo_.get());
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(all.size() * sizeof(Vertex)), all.data(), GL_STATIC_DRAW);
    setupVertexAttributes();
    for (GLuint loc = 3; loc <= 6; ++loc) {
        glEnableVertexAttribArray(loc);
        glVertexAttribDivisor(loc, 1);
    }
    glBindVertexArray(0);
}

void ModelLibrary::drawInstanced(ModelId id, GLuint instanceBuffer, usize byteOffset, GLsizei count,
                                 bool glass) const {
    const Range r = glass ? glassRanges_[id] : range(id);
    if (count <= 0 || r.count == 0) return;
    glBindVertexArray(vao_.get());
    glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);
    constexpr GLsizei stride = sizeof(ModelInstance);
    const auto at = [&](usize off) { return reinterpret_cast<const void*>(byteOffset + off); };
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, at(0));
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, at(16));
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, at(32));
    glVertexAttribPointer(6, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, at(48));
    glDrawArraysInstanced(GL_TRIANGLES, r.first, r.count, count);
}

void InstanceBatch::clear() {
    for (auto& v : perModel_) v.clear();
}

usize InstanceBatch::size() const {
    usize n = 0;
    for (const auto& v : perModel_) n += v.size();
    return n;
}

void InstanceBatch::upload() {
    staging_.clear();
    for (usize i = 0; i < kModelCount; ++i) {
        offsets_[i] = staging_.size();
        staging_.insert(staging_.end(), perModel_[i].begin(), perModel_[i].end());
    }
    if (staging_.empty()) return;
    if (!buffer_) buffer_ = makeBuffer();
    glBindBuffer(GL_ARRAY_BUFFER, buffer_.get());
    const usize bytes = staging_.size() * sizeof(ModelInstance);
    if (bytes > capacity_) capacity_ = bytes * 2;
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity_), nullptr, GL_STREAM_DRAW); // orphan
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), staging_.data());
}

void InstanceBatch::draw(const ModelLibrary& lib, bool glass) const {
    if (staging_.empty()) return;
    for (usize i = 0; i < kModelCount; ++i) {
        if (perModel_[i].empty()) continue;
        lib.drawInstanced(static_cast<ModelId>(i), buffer_.get(), offsets_[i] * sizeof(ModelInstance),
                          static_cast<GLsizei>(perModel_[i].size()), glass);
    }
}

void StaticInstances::upload(std::vector<std::pair<ModelId, ModelInstance>>& items) {
    batches.clear();
    total = items.size();
    if (items.empty()) return;
    std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<ModelInstance> data;
    data.reserve(items.size());
    for (usize i = 0; i < items.size(); ++i) {
        if (batches.empty() || batches.back().model != items[i].first)
            batches.push_back({items[i].first, static_cast<u32>(i), 0});
        ++batches.back().count;
        data.push_back(items[i].second);
    }
    if (!buffer) buffer = makeBuffer();
    glBindBuffer(GL_ARRAY_BUFFER, buffer.get());
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(ModelInstance)), data.data(),
                 GL_STATIC_DRAW);
}

void StaticInstances::draw(const ModelLibrary& lib) const {
    for (const Batch& b : batches)
        lib.drawInstanced(b.model, buffer.get(), b.offset * sizeof(ModelInstance), static_cast<GLsizei>(b.count));
}

} // namespace tp::gfx
