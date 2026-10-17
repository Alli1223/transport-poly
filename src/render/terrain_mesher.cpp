#include "render/terrain_mesher.hpp"

#include "core/hash.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>

namespace tp::gfx {

bool tileSplitNESW(const TileCorners& c) { return c.h[1] == c.h[3] && c.h[0] != c.h[2]; }

float tileSurfaceLevel(const TileCorners& c, float fx, float fy) {
    const float nw = c.h[0], ne = c.h[1], se = c.h[2], sw = c.h[3];
    if (!tileSplitNESW(c)) {
        if (fx >= fy) return nw + (ne - nw) * fx + (se - ne) * fy;
        return nw + (se - sw) * fx + (sw - nw) * fy;
    }
    if (fx + fy <= 1.0f) return nw + (ne - nw) * fx + (sw - nw) * fy;
    return se + (sw - se) * (1.0f - fx) + (ne - se) * (1.0f - fy);
}

namespace {

constexpr u32 kGrass = rgba(104, 166, 76);
constexpr u32 kForestFloor = rgba(82, 138, 60);
constexpr u32 kRough = rgba(122, 158, 76);
constexpr u32 kRock = rgba(138, 132, 126);
constexpr u32 kSand = rgba(226, 210, 152);
constexpr u32 kSnow = rgba(238, 243, 250);
constexpr u32 kSeabed = rgba(178, 168, 122);

u32 groundColor(const Tile& t) {
    switch (t.ground) {
    case Ground::Grass: return t.trees >= 2 ? kForestFloor : kGrass;
    case Ground::Rough: return kRough;
    case Ground::Rock: return kRock;
    case Ground::Sand: return kSand;
    case Ground::Snow: return kSnow;
    case Ground::Water: return kSeabed;
    }
    return kGrass;
}

float seabedY(i64 raw) {
    const float levels = std::max(static_cast<float>(raw) / static_cast<float>(kHeightOne), -14.0f);
    return std::min(levels, 0.0f) * 0.22f - 0.3f;
}

float jitter(u32 h) { return 0.93f + static_cast<float>(h & 0xFF) / 255.0f * 0.12f; }

} // namespace

ChunkGeometry buildChunkGeometry(const Chunk& chunk, const TerrainGenerator& gen, const WaterLookup& waterOutside,
                                 int lod) {
    ChunkGeometry out;
    const TilePos origin = chunkOrigin(chunk.pos);
    const u64 seed = gen.seed();
    const i32 step = 1 << lod;
    const i32 cells = kChunkSize / step;

    // Water flags for the chunk plus a one tile border ring.
    constexpr i32 kRing = kChunkSize + 2;
    std::array<u8, kRing * kRing> water{};
    for (i32 ly = -1; ly <= kChunkSize; ++ly) {
        for (i32 lx = -1; lx <= kChunkSize; ++lx) {
            const bool inside = lx >= 0 && lx < kChunkSize && ly >= 0 && ly < kChunkSize;
            const bool w = inside ? chunk.tile(lx, ly).type == TileType::Water
                                  : waterOutside(TilePos{origin.x + lx, origin.y + ly});
            water[static_cast<usize>((ly + 1) * kRing + lx + 1)] = w ? 1 : 0;
        }
    }
    const auto isWater = [&](i32 lx, i32 ly) { return water[static_cast<usize>((ly + 1) * kRing + lx + 1)] != 0; };

    // Visual corner heights on the (possibly coarse) grid; sea floor corners are lowered.
    const i32 gridN = cells + 1;
    std::vector<float> cornerY(static_cast<usize>(gridN * gridN));
    out.minY = 1e9f;
    out.maxY = -1e9f;
    for (i32 j = 0; j < gridN; ++j) {
        for (i32 i = 0; i < gridN; ++i) {
            const i32 lx = i * step, ly = j * step;
            const u8 h = chunk.height(lx, ly);
            float y = static_cast<float>(h) * kHeightStep;
            if (h == 0 && isWater(lx - 1, ly - 1) && isWater(lx, ly - 1) && isWater(lx - 1, ly) && isWater(lx, ly))
                y = seabedY(gen.rawHeight(origin.x + lx, origin.y + ly));
            cornerY[static_cast<usize>(j * gridN + i)] = y;
            out.minY = std::min(out.minY, y);
            out.maxY = std::max(out.maxY, y);
        }
    }
    out.minY -= 2.5f; // skirts
    const auto cy = [&](i32 i, i32 j) { return cornerY[static_cast<usize>(j * gridN + i)]; };
    const float fs = static_cast<float>(step);

    MeshBuilder ground;
    ground.vertices.reserve(static_cast<usize>(cells * cells) * 6 + static_cast<usize>(cells) * 24);
    MeshBuilder waterMesh;
    std::vector<u32> cellColor(static_cast<usize>(cells * cells));
    for (i32 j = 0; j < cells; ++j) {
        for (i32 i = 0; i < cells; ++i) {
            const i32 lx = i * step, ly = j * step;
            const i32 sx = lx + step / 2, sy = ly + step / 2;
            const Tile& tile = chunk.tile(sx, sy);
            const TilePos wp{origin.x + lx, origin.y + ly};
            const float x0 = static_cast<float>(lx), x1 = x0 + fs;
            const float z0 = static_cast<float>(ly), z1 = z0 + fs;
            const glm::vec3 nw(x0, cy(i, j), z0), ne(x1, cy(i + 1, j), z0);
            const glm::vec3 se(x1, cy(i + 1, j + 1), z1), sw(x0, cy(i, j + 1), z1);

            u32 base = groundColor(tile);
            if (tile.type == TileType::Water) {
                const float depth = std::min((nw.y + ne.y + se.y + sw.y) * 0.25f, 0.0f);
                base = shade(base, std::max(0.45f, 1.0f + depth * 0.18f));
            }
            cellColor[static_cast<usize>(j * cells + i)] = base;
            const u32 h = hashCoords(seed, wp.x, wp.y, 0x7E44);
            const u32 c0 = shade(base, jitter(h));
            const u32 c1 = shade(base, jitter(h >> 8));
            const TileCorners coarse{{chunk.height(lx, ly), chunk.height(lx + step, ly),
                                      chunk.height(lx + step, ly + step), chunk.height(lx, ly + step)}};
            if (!tileSplitNESW(coarse)) {
                ground.triangleUp(nw, se, ne, c0);
                ground.triangleUp(nw, sw, se, c1);
            } else {
                ground.triangleUp(nw, sw, ne, c0);
                ground.triangleUp(ne, sw, se, c1);
            }

            if (tile.type == TileType::Water) {
                const u32 wc = rgba(255, 255, 255);
                waterMesh.triangleUp({x0, kWaterLevel, z0}, {x1, kWaterLevel, z1}, {x1, kWaterLevel, z0}, wc);
                waterMesh.triangleUp({x0, kWaterLevel, z0}, {x0, kWaterLevel, z1}, {x1, kWaterLevel, z1}, wc);
            }

            if (lod == 0 && tile.type == TileType::Clear && tile.trees > 0) {
                const TileCorners corners = chunk.corners(lx, ly);
                for (u32 t = 0; t < tile.trees; ++t) {
                    const u32 th = hashCoords(seed, wp.x, wp.y, 0x7EE0 + t);
                    const float r1 = static_cast<float>(th & 0xFF) / 255.0f;
                    const float r2 = static_cast<float>((th >> 8) & 0xFF) / 255.0f;
                    float fx, fz;
                    if (tile.trees == 1) {
                        fx = 0.25f + r1 * 0.5f;
                        fz = 0.25f + r2 * 0.5f;
                    } else {
                        fx = static_cast<float>(t % 2) * 0.5f + 0.1f + r1 * 0.3f;
                        fz = static_cast<float>(t / 2) * 0.5f + 0.1f + r2 * 0.3f;
                    }
                    TreePlacement tp;
                    tp.pos = glm::vec3(x0 + fx, tileSurfaceLevel(corners, fx, fz) * kHeightStep, z0 + fz);
                    tp.scale = 0.75f + static_cast<float>((th >> 16) & 0xFF) / 255.0f * 0.5f;
                    tp.yaw = static_cast<float>((th >> 24) & 0xFF) / 255.0f * glm::two_pi<float>();
                    tp.type = tile.treeType;
                    tp.variant = static_cast<u8>((th >> 4) % 3);
                    tp.snowy = tile.ground == Ground::Snow;
                    out.trees.push_back(tp);
                    out.maxY = std::max(out.maxY, tp.pos.y + 2.0f);
                }
            }
        }
    }

    // Skirts hide cracks between neighbouring chunks rendered at different levels of detail.
    constexpr float kSkirt = 2.5f;
    const float edge = static_cast<float>(kChunkSize);
    const glm::vec3 down(0, -kSkirt, 0);
    for (i32 k = 0; k < cells; ++k) {
        const float a = static_cast<float>(k * step), b = a + fs;
        const u32 cn = shade(cellColor[static_cast<usize>(k)], 0.8f);
        const u32 cs = shade(cellColor[static_cast<usize>((cells - 1) * cells + k)], 0.8f);
        const u32 cw = shade(cellColor[static_cast<usize>(k * cells)], 0.8f);
        const u32 ce = shade(cellColor[static_cast<usize>(k * cells + cells - 1)], 0.8f);
        const glm::vec3 n0(a, cy(k, 0), 0), n1(b, cy(k + 1, 0), 0);
        ground.quadFacing(n0, n1, n1 + down, n0 + down, cn, {0, 0, -1});
        const glm::vec3 s0(a, cy(k, cells), edge), s1(b, cy(k + 1, cells), edge);
        ground.quadFacing(s0, s1, s1 + down, s0 + down, cs, {0, 0, 1});
        const glm::vec3 w0(0, cy(0, k), a), w1(0, cy(0, k + 1), b);
        ground.quadFacing(w0, w1, w1 + down, w0 + down, cw, {-1, 0, 0});
        const glm::vec3 e0(edge, cy(cells, k), a), e1(edge, cy(cells, k + 1), b);
        ground.quadFacing(e0, e1, e1 + down, e0 + down, ce, {1, 0, 0});
    }

    out.ground = std::move(ground.vertices);
    out.water = std::move(waterMesh.vertices);
    return out;
}

} // namespace tp::gfx
