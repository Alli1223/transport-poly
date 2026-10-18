#include "render/terrain_preview.hpp"

#include "render/mesh.hpp"

#include <algorithm>

namespace tp::gfx {

namespace {

struct Stop {
    float level;
    float r, g, b;
};

// Colour by height in levels: deep and shallow sea, beach, lowland, hills, rock and snow.
glm::vec3 heightColour(float level) {
    static constexpr Stop kStops[] = {
        {-24.0f, 22, 54, 104}, {-2.0f, 46, 104, 168}, {-0.01f, 70, 136, 196}, {0.0f, 214, 202, 146},
        {1.0f, 120, 170, 84},  {12.0f, 88, 148, 66},  {30.0f, 120, 132, 82},  {48.0f, 138, 128, 116},
        {62.0f, 226, 230, 238}, {120.0f, 250, 252, 255},
    };
    if (level <= kStops[0].level) return {kStops[0].r, kStops[0].g, kStops[0].b};
    for (usize i = 1; i < std::size(kStops); ++i) {
        const Stop& a = kStops[i - 1];
        const Stop& b = kStops[i];
        if (level <= b.level) {
            const float t = (level - a.level) / std::max(0.001f, b.level - a.level);
            return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
        }
    }
    const Stop& last = kStops[std::size(kStops) - 1];
    return {last.r, last.g, last.b};
}

} // namespace

void TerrainPreview::update(u64 seed, const TerrainSettings& terrain) {
    if (drawn_ && seed == seed_ && terrain == terrain_) return;
    seed_ = seed;
    terrain_ = terrain;
    drawn_ = true;
    const TerrainGenerator gen(seed, true, true, terrain);
    // Heights one pixel beyond each edge too, for the hill shading.
    constexpr i32 kSpan = kSize + 2;
    std::vector<float> levels(static_cast<usize>(kSpan * kSpan));
    const i32 origin = -kSize / 2 * kTilesPerPixel;
    for (i32 py = 0; py < kSpan; ++py)
        for (i32 px = 0; px < kSpan; ++px) {
            const i64 raw = gen.rawHeight(origin + (px - 1) * kTilesPerPixel, origin + (py - 1) * kTilesPerPixel);
            levels[static_cast<usize>(py * kSpan + px)] = static_cast<float>(raw) / kHeightOne;
        }
    pixels_.assign(static_cast<usize>(kSize * kSize), 0);
    int water = 0;
    for (i32 py = 0; py < kSize; ++py)
        for (i32 px = 0; px < kSize; ++px) {
            const auto at = [&](i32 x, i32 y) { return levels[static_cast<usize>((y + 1) * kSpan + x + 1)]; };
            const float h = at(px, py);
            glm::vec3 c = heightColour(h);
            if (h < 0.0f) {
                ++water;
            } else {
                // Light from the north west.
                const float slope = (at(px - 1, py - 1) - at(px + 1, py + 1)) / static_cast<float>(kTilesPerPixel);
                c *= std::clamp(1.0f + slope * 0.9f, 0.6f, 1.35f);
            }
            pixels_[static_cast<usize>(py * kSize + px)] =
                rgba(static_cast<u8>(std::clamp(c.r, 0.0f, 255.0f)), static_cast<u8>(std::clamp(c.g, 0.0f, 255.0f)),
                     static_cast<u8>(std::clamp(c.b, 0.0f, 255.0f)));
        }
    waterPercent_ = water * 100 / (kSize * kSize);
    if (!texture_) {
        texture_ = makeTexture();
        glBindTexture(GL_TEXTURE_2D, texture_.get());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());
    } else {
        glBindTexture(GL_TEXTURE_2D, texture_.get());
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace tp::gfx
