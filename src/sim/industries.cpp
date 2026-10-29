#include "sim/industries.hpp"

#include "core/hash.hpp"
#include "core/math.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <vector>

namespace tp::industries {

namespace {

constexpr i32 kCell = 96;
constexpr u32 kSiteSalt = 0x1D51;
constexpr i32 kTownReach = 48; // an industry hires from a town this close

struct Site {
    TilePos at;
    u32 hash;
};

std::optional<Site> siteFor(u64 seed, i32 cx, i32 cy, int index) {
    const u32 h = hashCoords(seed, cx * 2 + index, cy, kSiteSalt);
    if (index == 1 && h % 3 == 0) return std::nullopt; // a second site in two regions out of three
    const TilePos at{cx * kCell + 12 + static_cast<i32>((h >> 4) % (kCell - 24)),
                     cy * kCell + 12 + static_cast<i32>((h >> 12) % (kCell - 24))};
    return Site{at, h};
}

// Quarries and mines, cut into the hillside: their sites may slope more steeply.
constexpr bool extractive(u16 type) { return type == kQuarry || type == kIronMine || type == kCoalMine; }

// The kind of industry that suits a site's land (0 when none does, e.g. on a low shore).
u16 kindFor(const TerrainGenerator& gen, TilePos at, u32 hash) {
    // The ground within six tiles tells how high and steep the site is; a wider look tells
    // whether mountains rise nearby and whether there is water.
    i32 lo = 999, hi = -1, peak = -1;
    bool shore = false;
    for (i32 dy = -12; dy <= 12; dy += 3)
        for (i32 dx = -12; dx <= 12; dx += 3) {
            const i64 raw = gen.rawHeight(at.x + dx, at.y + dy);
            const i32 h = static_cast<i32>(std::max<i64>(raw, 0) / kHeightOne);
            peak = std::max(peak, h);
            if (std::abs(dx) > 6 || std::abs(dy) > 6) continue;
            lo = std::min(lo, h);
            hi = std::max(hi, h);
            shore = shore || raw < 0;
        }
    // In lakes and the sea, net pens near the shore (sites far out find none and stay empty).
    if (gen.rawHeight(at.x, at.y) < 0) return hash % 5 < 2 ? kFishFarm : 0;
    if (shore) return hash % 4 < 3 ? kFishFarm : 0;
    if (lo <= 0) return 0;
    const i32 level = gen.cornerHeight(at.x, at.y);
    const i32 slope = hi - lo;
    const bool mountains = level >= 18 || slope >= 4 || peak >= std::max(level + 8, 16);
    const bool highMountains = level >= 22 || slope >= 6 || peak >= 30;
    const bool hills = mountains || level >= 10 || slope >= 2;
    // Thick woods: the forest is dense here and all around.
    i64 forest = gen.forestDensity(at.x, at.y);
    const bool woods = forest > kHeightOne * 56 / 100;
    for (const TilePos d : {TilePos{-8, 0}, TilePos{8, 0}, TilePos{0, -8}, TilePos{0, 8}})
        forest = std::min(forest, gen.forestDensity(at.x + d.x, at.y + d.y));
    const bool thickWoods = forest > kHeightOne * 60 / 100;
    const Biome biome = gen.biomeAt(at.x, at.y);
    const bool dry = biome == Biome::Desert || biome == Biome::Steppe;
    const bool cold = biome == Biome::Tundra || biome == Biome::Taiga;
    const bool flat = slope <= 3;
    const bool open = flat && level <= 16 && !thickWoods;
    struct Option {
        u16 kind;
        u32 weight;
    };
    Option options[14];
    int n = 0;
    const auto add = [&](u16 kind, u32 weight) {
        if (weight) options[n++] = {kind, weight};
    };
    add(kLoggingCamp, dry ? 0 : thickWoods ? 14 : woods ? 3 : 0);
    add(kQuarry, mountains ? 9 : hills ? 2 : (biome == Biome::Desert ? 2 : 0));
    add(kIronMine, highMountains ? 6 : mountains ? 2 : 0);
    add(kCoalMine, mountains ? 5 : hills ? 1 : 0);
    add(kGrainFarm, open && !cold && biome != Biome::Desert && biome != Biome::Jungle ? 6 : 0);
    add(kSheepFarm, slope <= 5 && !thickWoods && biome != Biome::Desert && biome != Biome::Jungle ? 4 : 0);
    add(kDairyFarm, open && !dry ? 4 : 0);
    if (flat) {
        add(kCheeseDairy, 2);
        add(kTextileMill, 2);
        add(kSawmill, woods ? 4 : 2);
        add(kBrickworks, 2);
        add(kSteelMill, 2);
        add(kGoodsFactory, 1);
        add(kFurnitureFactory, 2);
    }
    u32 total = 0;
    for (int i = 0; i < n; ++i) total += options[i].weight;
    if (total == 0) return 0;
    u32 pick = (hash >> 20) % total;
    for (int i = 0; i < n; ++i) {
        if (pick < options[i].weight) return options[i].kind;
        pick -= options[i].weight;
    }
    return 0;
}

bool explored(const Map& m, TilePos c, i32 r) {
    const ChunkPos a = chunkOf({c.x - r, c.y - r});
    const ChunkPos b = chunkOf({c.x + r, c.y + r});
    for (i32 y = a.y; y <= b.y; ++y)
        for (i32 x = a.x; x <= b.x; ++x)
            if (!m.isActive({x, y})) return false;
    return true;
}

// Tiles from a footprint (NW corner `pos`, `size` across) to a point, in the longer direction.
i64 gapTo(TilePos pos, i32 size, TilePos p) {
    const i64 dx = p.x < pos.x ? pos.x - p.x : p.x >= pos.x + size ? p.x - (pos.x + size - 1) : 0;
    const i64 dy = p.y < pos.y ? pos.y - p.y : p.y >= pos.y + size ? p.y - (pos.y + size - 1) : 0;
    return std::max(dx, dy);
}

// A town still to be founded: where its centre will be, and how far around it the town will
// reach (its built-up radius when founded, and its roads and houses a little beyond).
struct FutureTown {
    TilePos centre;
    i32 reach = 0;
};

// The towns still to be founded within `range` tiles of `near`.
std::vector<FutureTown> futureTowns(const World& w, TilePos near, i32 range) {
    std::vector<FutureTown> out;
    for (i32 cy = floorDiv(near.y - range, kTownCellSize); cy <= floorDiv(near.y + range, kTownCellSize); ++cy)
        for (i32 cx = floorDiv(near.x - range, kTownCellSize); cx <= floorDiv(near.x + range, kTownCellSize); ++cx) {
            if (w.processedTownCells.contains(ChunkPos{cx, cy}.key())) continue; // founded (in w.towns) or empty
            const std::optional<TownSite> site = townSiteForCell(w.map.generator(), cx, cy);
            if (!site) continue;
            const i32 classRadius = site->cls == TownClass::City ? 12 : site->cls == TownClass::Town ? 8 : 5;
            const i32 radius = std::max(classRadius, static_cast<i32>(isqrt(static_cast<u64>(std::max(0, site->targetPopulation)))) / 2 + 4);
            out.push_back({site->centre, radius + 6});
        }
    return out;
}

// Clear of towns (see kMinTownDistance), of towns still to be founded and of other countryside
// industries.
bool apart(const World& w, TilePos pos, i32 size, const std::vector<FutureTown>& future) {
    for (const auto [id, t] : w.towns)
        if (gapTo(pos, size, t.centre) < t.radius + kMinTownDistance) return false;
    for (const FutureTown& t : future)
        if (gapTo(pos, size, t.centre) < t.reach + kMinTownDistance) return false;
    for (i32 dy = -kMinTownDistance; dy < size + kMinTownDistance; ++dy)
        for (i32 dx = -kMinTownDistance; dx < size + kMinTownDistance; ++dx) {
            const TilePos p{pos.x + dx, pos.y + dy};
            const Tile* tile = w.map.tile(p);
            if (!tile) continue;
            if (tile->type == TileType::Road && tile->roadOwner == kOwnerTown) return false;
            if (tile->type != TileType::Building) continue;
            const Building* b = w.buildings.tryGet(BuildingId(tile->index));
            if (!b) continue;
            if (!isCountryside(b->type) && b->town.valid()) return false;
            if (isCountryside(b->type) && gapTo(pos, size, p) <= 5) return false;
        }
    return true;
}

// Open land for a footprint, no more than `relief` levels from its lowest to its highest corner,
// with a margin of open land around it.
bool fits(const World& w, TilePos pos, i32 size, i32 relief, const std::vector<FutureTown>& future) {
    for (i32 dy = -1; dy <= size; ++dy)
        for (i32 dx = -1; dx <= size; ++dx) {
            const TilePos t{pos.x + dx, pos.y + dy};
            const Tile* tile = w.map.tile(t);
            if (!tile || tile->type != TileType::Clear || tile->span) return false;
            if (w.trackTiles.contains(ChunkPos{t.x, t.y}.key()) || roadcurves::onVerge(w, t)) return false;
        }
    i32 lo = INT32_MAX, hi = INT32_MIN;
    for (i32 cy = pos.y; cy <= pos.y + size; ++cy)
        for (i32 cx = pos.x; cx <= pos.x + size; ++cx) {
            if (!w.map.cornerEditable(cx, cy)) return false;
            const i32 f = w.map.cornerFine(cx, cy);
            lo = std::min(lo, f);
            hi = std::max(hi, f);
        }
    if (hi - lo > relief * kSubLevels || lo < kSubLevels) return false;
    return apart(w, pos, size, future);
}

// Open water for a fish farm's pens, with a margin of water all round and the shore close by:
// the edge of a lake or the coast, where a dock or a quay can reach it. Narrow water such as a
// river, with land close on both sides, stays free for shipping.
bool fitsWater(const World& w, TilePos pos, i32 size, const std::vector<FutureTown>& future) {
    for (i32 dy = -1; dy <= size; ++dy)
        for (i32 dx = -1; dx <= size; ++dx) {
            const Tile* tile = w.map.tile({pos.x + dx, pos.y + dy});
            if (!tile || tile->type != TileType::Water || tile->owner != kOwnerNone || tile->span) return false;
        }
    i32 land = 0, water = 0;
    for (i32 dy = -4; dy <= size + 3; ++dy)
        for (i32 dx = -4; dx <= size + 3; ++dx) {
            if (dx >= -1 && dx <= size && dy >= -1 && dy <= size) continue;
            const Tile* tile = w.map.tile({pos.x + dx, pos.y + dy});
            if (!tile) return false;
            ++(tile->type == TileType::Water ? water : land);
        }
    return land > 0 && water >= 2 * land && apart(w, pos, size, future);
}

// Levels the footprint to the whole height level nearest its average.
void level(World& w, TilePos pos, i32 size) {
    i64 sum = 0, count = 0;
    for (i32 cy = pos.y; cy <= pos.y + size; ++cy)
        for (i32 cx = pos.x; cx <= pos.x + size; ++cx) {
            sum += w.map.cornerFine(cx, cy);
            ++count;
        }
    const u16 target = static_cast<u16>((sum / count + kSubLevels / 2) / kSubLevels * kSubLevels);
    for (i32 cy = pos.y; cy <= pos.y + size; ++cy)
        for (i32 cx = pos.x; cx <= pos.x + size; ++cx) w.map.setCornerFine(cx, cy, target);
    for (i32 dy = -1; dy <= size; ++dy)
        for (i32 dx = -1; dx <= size; ++dx) w.map.touch({pos.x + dx, pos.y + dy});
}

} // namespace

BuildingId found(World& w, u16 type, TilePos near, i32 search) {
    if (!isCountryside(type)) return {};
    const u8 startLevel = static_cast<u8>(1 + hashCoords(w.settings.seed, near.x, near.y, 0x1D57) % 4);
    const u8 size = static_cast<u8>(buildingType(type).size + 2 + startLevel / 2);
    const std::vector<FutureTown> future = futureTowns(w, near, search + size + kMinTownDistance + 48);
    for (i32 r = 0; r <= search; ++r)
        for (i32 dy = -r; dy <= r; ++dy)
            for (i32 dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const TilePos pos{near.x + dx - size / 2, near.y + dy - size / 2};
                if (type == kFishFarm) {
                    if (!fitsWater(w, pos, size, future)) continue;
                } else {
                    if (!fits(w, pos, size, extractive(type) ? 5 : 3, future)) continue;
                    level(w, pos, size);
                }
                TownId town;
                i64 best = kTownReach + 1;
                for (const auto [id, t] : w.towns) {
                    const i64 d = chebyshev(t.centre.x - pos.x, t.centre.y - pos.y);
                    if (d < best) {
                        best = d;
                        town = id;
                    }
                }
                const BuildingId id = towns::placeBuilding(w, town, type, pos, Dir::S, static_cast<u8>((startLevel - 1) / 3), size);
                Industry entry{w.buildings[id].ref(id)};
                entry.level = startLevel;
                w.industries.push_back(entry);
                return id;
            }
    return {};
}

void onChunkActivated(World& w, ChunkPos chunk) {
    if (!w.settings.industries || !w.map.generator().smooth()) return;
    const TilePos o = chunkOrigin(chunk);
    const i32 x0 = floorDiv(o.x - 24, kCell), x1 = floorDiv(o.x + kChunkSize + 24, kCell);
    const i32 y0 = floorDiv(o.y - 24, kCell), y1 = floorDiv(o.y + kChunkSize + 24, kCell);
    for (i32 cy = y0; cy <= y1; ++cy)
        for (i32 cx = x0; cx <= x1; ++cx)
            for (int index = 0; index < 2; ++index) {
                const u64 key = ChunkPos{cx * 2 + index, cy}.key();
                if (w.processedIndustrySites.contains(key)) continue;
                const std::optional<Site> site = siteFor(w.settings.seed, cx, cy, index);
                if (!site) {
                    w.processedIndustrySites.insert(key);
                    continue;
                }
                // Wait until the land around the site has been explored.
                if (!explored(w.map, site->at, 18)) continue;
                w.processedIndustrySites.insert(key);
                if (const u16 kind = kindFor(w.map.generator(), site->at, site->hash))
                    found(w, kind, site->at, kind == kFishFarm || extractive(kind) ? 16 : 12);
            }
}

} // namespace tp::industries
