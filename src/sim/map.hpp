#pragma once
// The infinite tile map: a set of active (simulated) chunks plus the terrain generator that
// defines everything not yet activated.

#include "core/types.hpp"
#include "sim/chunk.hpp"
#include "sim/terrain_gen.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace tp {

class Map {
public:
    explicit Map(u64 seed = 1, bool waterways = true, bool smooth = false, TerrainSettings terrain = TerrainSettings::original())
        : gen_(seed, waterways, smooth, terrain) {}

    const TerrainGenerator& generator() const { return gen_; }

    bool isActive(ChunkPos p) const { return chunks_.contains(p.key()); }
    bool isTileActive(TilePos t) const { return isActive(chunkOf(t)); }
    Chunk* chunk(ChunkPos p);
    const Chunk* chunk(ChunkPos p) const;

    // Generates and activates a chunk. Returns false if it was already active.
    bool activate(ChunkPos p);
    // Active chunks in activation order (deterministic iteration order).
    const std::vector<ChunkPos>& activeChunks() const { return order_; }

    // nullptr when the tile lies in an inactive chunk.
    Tile* tile(TilePos t);
    const Tile* tile(TilePos t) const;

    // Corner heights work everywhere: active chunks return stored data, inactive areas the
    // generated terrain.
    u8 cornerHeight(i32 cx, i32 cy) const;
    u16 cornerFine(i32 cx, i32 cy) const;
    TileCorners corners(TilePos t) const;
    FineCorners fineCorners(TilePos t) const;
    Slope slope(TilePos t) const { return computeSlope(corners(t)); }
    // Height of the lowest corner of a tile.
    u8 tileBaseHeight(TilePos t) const { return corners(t).min(); }

    // A corner may only be changed when every chunk holding a copy of it is active, so that
    // inactive chunks always match the generator.
    bool cornerEditable(i32 cx, i32 cy) const;
    void setCornerHeight(i32 cx, i32 cy, u8 height) { setCornerFine(cx, cy, fineOf(height)); }
    void setCornerFine(i32 cx, i32 cy, u16 fine);

    // Marks the chunk containing the tile as modified (bumps its render revision, and that of the
    // chunks beside it for a tile on its edge). Tiles holding
    // a structure get their corners snapped to whole levels, which is what every structure
    // stands on.
    void touch(TilePos t);
    void snapToLevels(TilePos t);

    void clear() {
        chunks_.clear();
        order_.clear();
    }

    template <class Ar>
    void serialize(Ar& ar) {
        if constexpr (Ar::kReading) {
            clear();
            u64 count = 0;
            ar(count);
            for (u64 i = 0; i < count; ++i) {
                auto c = std::make_unique<Chunk>();
                ar(*c);
                order_.push_back(c->pos);
                chunks_[c->pos.key()] = std::move(c);
            }
        } else {
            u64 count = order_.size();
            ar(count);
            ar.items(order_.size(), [&](auto& out, usize i) { out(*chunks_.at(order_[i].key())); });
        }
    }

private:
    TerrainGenerator gen_;
    std::unordered_map<u64, std::unique_ptr<Chunk>> chunks_; // lookup only, never iterated
    std::vector<ChunkPos> order_;
};

} // namespace tp
