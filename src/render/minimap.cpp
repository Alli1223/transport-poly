#include "render/minimap.hpp"

#include "render/mesh.hpp"

#include <cmath>

namespace tp::gfx {

namespace {

constexpr u32 kUnexplored = rgba(36, 40, 48);
constexpr u32 kWater = rgba(48, 92, 156);
constexpr u32 kRoad = rgba(112, 112, 116);
constexpr u32 kRail = rgba(126, 96, 76);
constexpr u32 kHome = rgba(206, 146, 112);
constexpr u32 kShop = rgba(150, 164, 200);
constexpr u32 kWorks = rgba(176, 156, 120);

u32 groundColour(const Tile& t) {
    switch (t.ground) {
    case Ground::Rock: return rgba(132, 128, 122);
    case Ground::Sand: return rgba(214, 200, 142);
    case Ground::Snow: return rgba(234, 238, 246);
    case Ground::Rough: return rgba(118, 150, 76);
    default: return t.trees > 0 ? rgba(62, 118, 52) : rgba(98, 158, 72);
    }
}

u32 companyColour(const World& w, CompanyId id, float shadeBy = 1.0f) {
    const Company* c = w.companies.tryGet(id);
    const u32 rgb = companyColourRgb(c ? c->colour : 14);
    return rgba(static_cast<u8>(((rgb >> 16) & 0xFF) * shadeBy), static_cast<u8>(((rgb >> 8) & 0xFF) * shadeBy),
                static_cast<u8>((rgb & 0xFF) * shadeBy));
}

u32 tileColour(const World& w, const Tile& t) {
    switch (t.type) {
    case TileType::Clear: return groundColour(t);
    case TileType::Water: return kWater;
    case TileType::Road: return kRoad;
    case TileType::Rail: return kRail;
    case TileType::Building: {
        const Building* b = w.buildings.tryGet(BuildingId(t.index));
        if (!b) return kHome;
        switch (b->info().category) {
        case BuildingCategory::Residential: return kHome;
        case BuildingCategory::Industrial: return kWorks;
        default: return kShop;
        }
    }
    case TileType::Station: {
        const Station* s = w.stations.tryGet(StationId(t.index));
        return companyColour(w, s ? s->owner : CompanyId{});
    }
    case TileType::Depot: {
        const Depot* d = w.depots.tryGet(DepotId(t.index));
        return companyColour(w, d ? d->owner : CompanyId{}, 0.7f);
    }
    }
    return kUnexplored;
}

} // namespace

void Minimap::update(const World& w, double centreX, double centreZ, i32 tilesPerPixel) {
    scale_ = std::max(1, tilesPerPixel);
    origin_ = {static_cast<i32>(std::floor(centreX)) - kSize / 2 * scale_,
               static_cast<i32>(std::floor(centreZ)) - kSize / 2 * scale_};
    pixels_.assign(static_cast<usize>(kSize * kSize), kUnexplored);
    for (i32 py = 0; py < kSize; ++py) {
        for (i32 px = 0; px < kSize; ++px) {
            const TilePos t{origin_.x + px * scale_, origin_.y + py * scale_};
            if (const Tile* tile = w.map.tile(t)) pixels_[static_cast<usize>(py * kSize + px)] = tileColour(w, *tile);
        }
    }
    // Vehicles as bright dots in their owner's colour.
    for (const auto [id, v] : w.vehicles) {
        if (v.state == VehicleState::InDepot) continue;
        const i32 px = (v.tile.x - origin_.x) / scale_, py = (v.tile.y - origin_.y) / scale_;
        for (i32 dy = 0; dy <= (scale_ == 1 ? 1 : 0); ++dy)
            for (i32 dx = 0; dx <= (scale_ == 1 ? 1 : 0); ++dx)
                if (px + dx >= 0 && px + dx < kSize && py + dy >= 0 && py + dy < kSize)
                    pixels_[static_cast<usize>((py + dy) * kSize + px + dx)] = rgba(255, 244, 120);
    }
    if (!texture_) {
        texture_ = makeTexture();
        glBindTexture(GL_TEXTURE_2D, texture_.get());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_NEAREST));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_NEAREST));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     pixels_.data());
    } else {
        glBindTexture(GL_TEXTURE_2D, texture_.get());
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

TilePos Minimap::tileAt(double px, double py) const {
    return {origin_.x + static_cast<i32>(std::floor(px)) * scale_, origin_.y + static_cast<i32>(std::floor(py)) * scale_};
}

} // namespace tp::gfx
