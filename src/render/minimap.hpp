#pragma once
// Top-down minimap of the explored world around a point, uploaded to a texture for the UI.

#include "render/gl_util.hpp"
#include "sim/world.hpp"

#include <vector>

namespace tp::gfx {

class Minimap {
public:
    static constexpr i32 kSize = 256; // pixels per side

    // Redraws the map centred on (centreX, centreZ) with `tilesPerPixel` tiles per pixel.
    void update(const World& w, double centreX, double centreZ, i32 tilesPerPixel);
    u32 textureId() const { return texture_.get(); }
    bool ready() const { return static_cast<bool>(texture_); }

    // World tile at a pixel of the last update, and the pixel of a world position.
    TilePos tileAt(double px, double py) const;
    double pixelX(double worldX) const { return (worldX - origin_.x) / scale_; }
    double pixelY(double worldZ) const { return (worldZ - origin_.y) / scale_; }

private:
    Texture texture_;
    std::vector<u32> pixels_;
    TilePos origin_{0, 0};
    i32 scale_ = 2;
};

} // namespace tp::gfx
