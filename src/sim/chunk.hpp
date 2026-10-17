#pragma once
// A 32x32 tile piece of the active world. Corner heights are stored for the full 33x33 corner
// grid, so the corners on the east/south edges duplicate the neighbouring chunk's data; the
// Map keeps the copies consistent.

#include "core/types.hpp"
#include "sim/coords.hpp"
#include "sim/slope.hpp"
#include "sim/tile.hpp"

#include <array>

namespace tp {

struct Chunk {
    ChunkPos pos;
    std::array<u8, kChunkCorners * kChunkCorners> heights{};
    std::array<Tile, kChunkTiles> tiles{};
    // Incremented on every modification so renderers can rebuild cached meshes.
    // Not part of the game state (not serialised or hashed).
    u32 revision = 0;

    u8& height(i32 lx, i32 ly) { return heights[static_cast<usize>(ly * kChunkCorners + lx)]; }
    u8 height(i32 lx, i32 ly) const { return heights[static_cast<usize>(ly * kChunkCorners + lx)]; }
    Tile& tile(i32 lx, i32 ly) { return tiles[static_cast<usize>(ly * kChunkSize + lx)]; }
    const Tile& tile(i32 lx, i32 ly) const { return tiles[static_cast<usize>(ly * kChunkSize + lx)]; }

    TileCorners corners(i32 lx, i32 ly) const {
        return TileCorners{{height(lx, ly), height(lx + 1, ly), height(lx + 1, ly + 1), height(lx, ly + 1)}};
    }

    template <class Ar>
    void serialize(Ar& ar) {
        ar(pos, heights, tiles);
    }
};

} // namespace tp
