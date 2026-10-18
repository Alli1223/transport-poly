#pragma once
// Ships sail between water tile centres in eight directions. Diagonal steps are only allowed
// when both tiles beside the diagonal are water too, so ships never clip a coastline corner.

#include "core/types.hpp"
#include "sim/coords.hpp"

#include <optional>
#include <span>
#include <vector>

namespace tp {

class World;

// Headings: 0 = N, 1 = NE, 2 = E, 3 = SE, 4 = S, 5 = SW, 6 = W, 7 = NW.
inline constexpr i32 kHeadingDX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
inline constexpr i32 kHeadingDY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

constexpr TilePos headingStep(TilePos t, u8 h) { return {t.x + kHeadingDX[h & 7], t.y + kHeadingDY[h & 7]}; }
// Distance between tile centres for a step (1 tile = 256 units).
constexpr i32 headingLength(u8 h) { return (h & 1) ? 362 : 256; }
constexpr u8 headingOf(Dir d) { return static_cast<u8>(static_cast<u8>(d) * 2); }

bool isWaterTile(const World& w, TilePos t);
bool canSail(const World& w, TilePos from, u8 heading);

// Headings leading from `from` to the nearest goal tile, nullopt when unreachable (within the
// search budget). Empty when `from` is already a goal.
std::optional<std::vector<u8>> findWaterPath(const World& w, TilePos from, std::span<const TilePos> goals,
                                             i32 maxNodes = 80'000);

} // namespace tp
