#include "sim/industries.hpp"

#include "core/hash.hpp"
#include "core/math.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <optional>

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

// The kind of industry that suits a site's land (0 when none does, e.g. at sea).
u16 kindFor(const TerrainGenerator& gen, TilePos at, u32 hash) {
    i32 lo = 999, hi = -1;
    for (i32 dy = -6; dy <= 6; dy += 3)
        for (i32 dx = -6; dx <= 6; dx += 3) {
            const i32 h = gen.cornerHeight(at.x + dx, at.y + dy);
            lo = std::min(lo, h);
            hi = std::max(hi, h);
        }
    if (lo <= 0) return 0;
    const i32 level = gen.cornerHeight(at.x, at.y);
    const i32 slope = hi - lo;
    const bool forest = gen.forestDensity(at.x, at.y) > kHeightOne * 56 / 100;
    const Biome biome = gen.biomeAt(at.x, at.y);
    const bool dry = biome == Biome::Desert || biome == Biome::Steppe;
    const bool cold = biome == Biome::Tundra || biome == Biome::Taiga;
    const bool flat = slope <= 3;
    struct Option {
        u16 kind;
        u32 weight;
    };
    Option options[14];
    int n = 0;
    const auto add = [&](u16 kind, u32 weight) {
        if (weight) options[n++] = {kind, weight};
    };
    add(kLoggingCamp, forest && !dry ? 8 : 0);
    add(kQuarry, slope >= 4 || level >= 18 ? 5 : (biome == Biome::Desert ? 3 : 1));
    add(kIronMine, level >= 22 || slope >= 6 ? 4 : 1);
    add(kCoalMine, level >= 16 || slope >= 5 ? 4 : 1);
    add(kGrainFarm, flat && level <= 16 && !cold && biome != Biome::Desert && biome != Biome::Jungle ? 6 : 0);
    add(kSheepFarm, slope <= 5 && biome != Biome::Desert && biome != Biome::Jungle ? 4 : 0);
    add(kDairyFarm, flat && level <= 16 && !dry ? 4 : 0);
    if (flat) {
        add(kCheeseDairy, 2);
        add(kTextileMill, 2);
        add(kSawmill, 3);
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

// Open, gently sloping land for a footprint, with a margin of open land around it, clear of
// towns and of other industries.
bool fits(const World& w, TilePos pos, i32 size) {
    for (i32 dy = -1; dy <= size; ++dy)
        for (i32 dx = -1; dx <= size; ++dx) {
            const TilePos t{pos.x + dx, pos.y + dy};
            const Tile* tile = w.map.tile(t);
            if (!tile || tile->type != TileType::Clear || tile->span) return false;
            if (w.trackTiles.contains(ChunkPos{t.x, t.y}.key())) return false;
        }
    i32 lo = INT32_MAX, hi = INT32_MIN;
    for (i32 cy = pos.y; cy <= pos.y + size; ++cy)
        for (i32 cx = pos.x; cx <= pos.x + size; ++cx) {
            if (!w.map.cornerEditable(cx, cy)) return false;
            const i32 f = w.map.cornerFine(cx, cy);
            lo = std::min(lo, f);
            hi = std::max(hi, f);
        }
    if (hi - lo > 3 * kSubLevels || lo < kSubLevels) return false;
    for (const auto [id, t] : w.towns)
        if (chebyshev(t.centre.x - pos.x, t.centre.y - pos.y) < t.radius + 6 + size) return false;
    for (i32 dy = -5; dy < size + 5; ++dy)
        for (i32 dx = -5; dx < size + 5; ++dx) {
            const Tile* tile = w.map.tile({pos.x + dx, pos.y + dy});
            if (!tile || tile->type != TileType::Building) continue;
            if (const Building* b = w.buildings.tryGet(BuildingId(tile->index)); b && isCountryside(b->type)) return false;
        }
    return true;
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
    const i32 size = buildingType(type).size;
    for (i32 r = 0; r <= search; ++r)
        for (i32 dy = -r; dy <= r; ++dy)
            for (i32 dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const TilePos pos{near.x + dx - size / 2, near.y + dy - size / 2};
                if (!fits(w, pos, size)) continue;
                level(w, pos, size);
                TownId town;
                i64 best = kTownReach + 1;
                for (const auto [id, t] : w.towns) {
                    const i64 d = chebyshev(t.centre.x - pos.x, t.centre.y - pos.y);
                    if (d < best) {
                        best = d;
                        town = id;
                    }
                }
                const BuildingId id = towns::placeBuilding(w, town, type, pos, Dir::S, 0);
                Industry entry{w.buildings[id].ref(id)};
                entry.level = static_cast<u8>(1 + hashCoords(w.settings.seed, pos.x, pos.y, 0x1D57) % 4);
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
                if (const u16 kind = kindFor(w.map.generator(), site->at, site->hash)) found(w, kind, site->at, 12);
            }
}

} // namespace tp::industries
