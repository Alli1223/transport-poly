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
    std::array<u16, kChunkCorners * kChunkCorners> heights{}; // fine units (see kSubLevels)
    std::array<Tile, kChunkTiles> tiles{};
    // Incremented on every modification so renderers can rebuild cached meshes.
    // Not part of the game state (not serialised or hashed).
    u32 revision = 0;

    u16& fine(i32 lx, i32 ly) { return heights[static_cast<usize>(ly * kChunkCorners + lx)]; }
    u16 fine(i32 lx, i32 ly) const { return heights[static_cast<usize>(ly * kChunkCorners + lx)]; }
    // Height in whole levels (rounded).
    u8 height(i32 lx, i32 ly) const { return levelOf(fine(lx, ly)); }
    Tile& tile(i32 lx, i32 ly) { return tiles[static_cast<usize>(ly * kChunkSize + lx)]; }
    const Tile& tile(i32 lx, i32 ly) const { return tiles[static_cast<usize>(ly * kChunkSize + lx)]; }

    TileCorners corners(i32 lx, i32 ly) const {
        return TileCorners{{height(lx, ly), height(lx + 1, ly), height(lx + 1, ly + 1), height(lx, ly + 1)}};
    }
    FineCorners fineCorners(i32 lx, i32 ly) const {
        return FineCorners{{fine(lx, ly), fine(lx + 1, ly), fine(lx + 1, ly + 1), fine(lx, ly + 1)}};
    }

    template <class Ar>
    void serialize(Ar& ar) {
        if (ar.version == 0 || ar.version >= 12) {
            ar(pos, heights, tiles);
            return;
        }
        // Older saves stored whole levels.
        std::array<u8, kChunkCorners * kChunkCorners> levels{};
        if constexpr (!Ar::kReading)
            for (usize i = 0; i < levels.size(); ++i) levels[i] = levelOf(heights[i]);
        ar(pos, levels, tiles);
        if constexpr (Ar::kReading)
            for (usize i = 0; i < levels.size(); ++i) heights[i] = fineOf(levels[i]);
    }
};

} // namespace tp
