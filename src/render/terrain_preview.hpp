#pragma once
// A small top-down picture of the land a seed and terrain settings would make around the start,
// for the new game screen.

#include "render/gl_util.hpp"
#include "sim/terrain_gen.hpp"

#include <vector>

namespace tp::gfx {

class TerrainPreview {
public:
    static constexpr i32 kSize = 192;        // pixels per side
    static constexpr i32 kTilesPerPixel = 8; // so the picture spans 1536 tiles

    // Redraws the picture when the seed or settings changed since the last call.
    void update(u64 seed, const TerrainSettings& terrain);
    u32 textureId() const { return texture_.get(); }
    bool ready() const { return static_cast<bool>(texture_); }
    // Share of the picture that is water, in percent.
    int waterPercent() const { return waterPercent_; }

private:
    Texture texture_;
    std::vector<u32> pixels_;
    u64 seed_ = 0;
    TerrainSettings terrain_;
    bool drawn_ = false;
    int waterPercent_ = 0;
};

} // namespace tp::gfx
