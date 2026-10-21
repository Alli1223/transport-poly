#include "sim/town.hpp"

#include "core/hash.hpp"
#include "core/math.hpp"
#include "core/names.hpp"
#include "sim/person.hpp"
#include "sim/road.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <format>

namespace tp {

std::string_view ratingName(i32 r) {
    if (r < -400) return "Appalling";
    if (r < -200) return "Very poor";
    if (r < 0) return "Poor";
    if (r < 200) return "Mediocre";
    if (r < 400) return "Good";
    if (r < 600) return "Very good";
    if (r < 800) return "Excellent";
    return "Outstanding";
}

std::string_view townClassName(TownClass c) {
    switch (c) {
    case TownClass::Village: return "Village";
    case TownClass::Town: return "Town";
    case TownClass::City: return "City";
    }
    return "?";
}

namespace {

constexpr u32 kSiteSalt = 0x70A7u;

bool siteSuitable(const TerrainGenerator& gen, TilePos c) {
    int lo = 255, hi = 0;
    for (i32 dy = -4; dy <= 4; dy += 2) {
        for (i32 dx = -4; dx <= 4; dx += 2) {
            if (gen.rawHeight(c.x + dx, c.y + dy) < kHeightOne) return false; // too close to the sea
            const int h = gen.cornerHeight(c.x + dx, c.y + dy);
            lo = std::min(lo, h);
            hi = std::max(hi, h);
        }
    }
    return hi < kSnowLine - 4 && hi - lo <= 3;
}

} // namespace

std::optional<TownSite> townSiteForCell(const TerrainGenerator& gen, i32 cx, i32 cy) {
    const u32 h = hashCoords(gen.seed(), cx, cy, kSiteSalt);
    if (h % 100 >= 72) return std::nullopt;
    for (u32 attempt = 0; attempt < 4; ++attempt) {
        const u32 a = mix32(h + attempt * 0x9E3779B9u);
        const TilePos c{cx * kTownCellSize + 20 + static_cast<i32>(a % 56),
                        cy * kTownCellSize + 20 + static_cast<i32>((a >> 8) % 56)};
        if (!siteSuitable(gen, c)) continue;
        TownSite s;
        s.centre = c;
        s.seed = mix32(a ^ 0xC17E5u);
        s.cellKey = ChunkPos{cx, cy}.key();
        const u32 r = (h >> 20) % 100;
        s.cls = r < 45 ? TownClass::Village : (r < 82 ? TownClass::Town : TownClass::City);
        switch (s.cls) {
        case TownClass::Village: s.targetPopulation = 60 + static_cast<i32>((a >> 4) % 90); break;
        case TownClass::Town: s.targetPopulation = 250 + static_cast<i32>((a >> 5) % 350); break;
        case TownClass::City: s.targetPopulation = 900 + static_cast<i32>((a >> 6) % 1600); break;
        }
        return s;
    }
    return std::nullopt;
}

namespace towns {

namespace {

bool footprintExplored(const Map& m, TilePos c) {
    const ChunkPos a = chunkOf({c.x - kTownFootprint, c.y - kTownFootprint});
    const ChunkPos b = chunkOf({c.x + kTownFootprint, c.y + kTownFootprint});
    for (i32 y = a.y; y <= b.y; ++y)
        for (i32 x = a.x; x <= b.x; ++x)
            if (!m.isActive({x, y})) return false;
    return true;
}

std::string uniqueName(const World& w, u32 seed, TownId self) {
    for (u32 k = 0; k < 24; ++k) {
        std::string name = generateTownName(seed + k * 7919u);
        bool clash = false;
        for (const auto [id, t] : w.towns)
            if (id != self && t.name == name) clash = true;
        if (!clash) return name;
    }
    return std::format("{} {}", generateTownName(seed), self.value);
}

// Connects a road from `a` towards `d`, recording newly created town road tiles.
bool townConnect(World& w, Town& t, TilePos a, Dir d) {
    const Tile* ta = w.map.tile(a);
    const Tile* tb = w.map.tile(neighbour(a, d));
    if (!ta || !tb) return false;
    const bool aNew = ta->type == TileType::Clear;
    const bool bNew = tb->type == TileType::Clear;
    if (!connectRoad(w.map, a, d, kOwnerTown)) return false;
    if (aNew) t.roads.push_back(a);
    if (bNew) t.roads.push_back(neighbour(a, d));
    return true;
}

u8 zoneOf(const Town& t, TilePos p) {
    const i64 d = chebyshev(p.x - t.centre.x, p.y - t.centre.y) * 100;
    const i64 r = std::max(t.radius, 4);
    if (d <= r * 40) return kZoneCore;
    if (d <= r * 75) return kZoneInner;
    return kZoneOuter;
}

u16 chooseBuildingType(Town& t, u8 zone, u8 maxSize) {
    const auto types = buildingTypes();
    const i64 pop = t.population + 10;
    const i64 jobs = t.jobs + 5;
    i64 total = 0;
    i64 weights[32] = {};
    for (usize i = 0; i < types.size(); ++i) {
        const BuildingType& bt = types[i];
        if (static_cast<u8>(bt.minClass) > static_cast<u8>(t.cls) || !(bt.zones & zone) || bt.size > maxSize) continue;
        i64 wgt = bt.weight * 10;
        if (bt.residents > 0 && bt.jobs == 0) {
            if (jobs * 10 > pop * 8) wgt *= 3;
            else if (jobs * 10 < pop * 4) wgt /= 2;
        } else if (bt.jobs > 0) {
            if (jobs * 10 < pop * 6) wgt *= 3;
            else if (jobs > pop) wgt /= 4;
        }
        if (bt.category == BuildingCategory::Retail) {
            if (t.retailCapacity * 10 < pop * 6 && t.cls != TownClass::Village) wgt *= 2;
            if (t.cls == TownClass::Village && !t.retail.empty()) wgt = 0;
        }
        if (bt.category == BuildingCategory::Civic && t.buildings.size() < 12) wgt = 0;
        weights[i] = std::max<i64>(wgt, 0);
        total += weights[i];
    }
    if (total <= 0) return 1; // house
    i64 pick = static_cast<i64>(t.rng.below(static_cast<u32>(total)));
    for (usize i = 0; i < types.size(); ++i) {
        if (pick < weights[i]) return static_cast<u16>(i);
        pick -= weights[i];
    }
    return 1;
}

bool extendRoad(World& w, TownId id) {
    Town& t = w.towns[id];
    if (t.roads.empty()) return false;
    const TilePos r = t.roads[t.rng.below(static_cast<u32>(t.roads.size()))];
    const Tile* rt = w.map.tile(r);
    if (!rt || rt->type != TileType::Road) return false;
    const Dir d = kDirs[t.rng.below(4)];
    if (hasRoadPiece(*rt, d)) return false;
    const TilePos n = neighbour(r, d);
    if (chebyshev(n.x - t.centre.x, n.y - t.centre.y) > t.radius + 3) return false;
    const Tile* nt = w.map.tile(n);
    if (!nt) return false;
    if (t.grid) {
        const i32 off = isAxisX(d) ? (r.y - t.centre.y) : (r.x - t.centre.x);
        if (floorMod(off, static_cast<i32>(t.gridSpacing)) != 0) return false;
    } else if (nt->type != TileType::Road) {
        // Organic layouts avoid running right alongside another road.
        const Tile* s1 = w.map.tile(neighbour(n, rotateCW(d)));
        const Tile* s2 = w.map.tile(neighbour(n, rotateCCW(d)));
        const bool crowded = (s1 && s1->type == TileType::Road) || (s2 && s2->type == TileType::Road);
        if (crowded && !t.rng.chance(1, 8)) return false;
    }
    return townConnect(w, t, r, d);
}

TilePos anchorFor2x2(TilePos p, Dir d, bool alt) {
    switch (d) {
    case Dir::N: return {alt ? p.x - 1 : p.x, p.y - 1};
    case Dir::S: return {alt ? p.x - 1 : p.x, p.y};
    case Dir::E: return {p.x, alt ? p.y - 1 : p.y};
    case Dir::W: return {p.x - 1, alt ? p.y - 1 : p.y};
    }
    return p;
}

bool buildBuilding(World& w, TownId id) {
    Town& t = w.towns[id];
    if (t.roads.empty()) return false;
    const TilePos r = t.roads[t.rng.below(static_cast<u32>(t.roads.size()))];
    const Tile* rt = w.map.tile(r);
    if (!rt || rt->type != TileType::Road) return false;
    const Dir d = kDirs[t.rng.below(4)];
    if (hasRoadPiece(*rt, d)) return false;
    const TilePos p = neighbour(r, d);
    const u8 zone = zoneOf(t, p);
    u16 type = chooseBuildingType(t, zone, 2);
    TilePos anchor = p;
    if (buildingType(type).size == 2) {
        anchor = anchorFor2x2(p, d, t.rng.chance(1, 2));
        if (!canPlaceBuilding(w, anchor, 2)) {
            type = chooseBuildingType(t, zone, 1);
            anchor = p;
        }
    }
    if (!canPlaceBuilding(w, anchor, buildingType(type).size)) return false;
    const u8 variant = static_cast<u8>(t.rng.below(std::max<u8>(1, buildingType(type).variants)));
    placeBuilding(w, id, type, anchor, opposite(d), variant);
    return true;
}

bool growStep(World& w, TownId id, bool founding) {
    Town& t = w.towns[id];
    if (t.rng.chance(founding ? 22 : 15, 100) && extendRoad(w, id)) return true;
    for (int i = 0; i < (founding ? 6 : 4); ++i)
        if (buildBuilding(w, id)) return true;
    for (int i = 0; i < (founding ? 10 : 4); ++i)
        if (extendRoad(w, id)) return true;
    return false;
}

void makeCentre(World& w, TownId id) {
    Town& t = w.towns[id];
    const int base = t.cls == TownClass::City ? 10 : (t.cls == TownClass::Town ? 7 : 4);
    for (const Dir d : kDirs) {
        TilePos cur = t.centre;
        const int len = base + static_cast<int>(t.rng.below(3));
        for (int i = 0; i < len; ++i) {
            if (!townConnect(w, t, cur, d)) break;
            cur = neighbour(cur, d);
        }
    }
}

// Growth points per day (100 points = one growth step). Good public transport makes towns grow
// faster: up to three times the base rate when everyone travels by transit, plus a bonus for
// each regularly served station.

// Population at which a town asks for its n-th need, and what each tier may ask for.
constexpr i32 kNeedPopulation[] = {0, 350, 1000, 2500, 5000};
constexpr std::array<CargoType, 3> kNeedTier1 = {CargoType::Food, CargoType::Planks, CargoType::Cheese};
constexpr std::array<CargoType, 5> kNeedTier2 = {CargoType::Bricks, CargoType::Cloth, CargoType::Food, CargoType::Cheese,
                                                 CargoType::Planks};
constexpr std::array<CargoType, 4> kNeedTier3 = {CargoType::Goods, CargoType::Furniture, CargoType::Bricks, CargoType::Cloth};
constexpr std::array<CargoType, 3> kNeedTier4 = {CargoType::Goods, CargoType::Furniture, CargoType::Steel};
constexpr std::array<CargoType, 4> kNeedTier5 = {CargoType::Furniture, CargoType::Goods, CargoType::Steel, CargoType::Cloth};

std::span<const CargoType> needPool(usize tier) {
    switch (tier) {
    case 0: return kNeedTier1;
    case 1: return kNeedTier2;
    case 2: return kNeedTier3;
    case 3: return kNeedTier4;
    default: return kNeedTier5;
    }
}

// Adds the needs a town's population calls for, picked at random from each tier's choices.
void addNeeds(World& w, Town& t) {
    while (t.needs.size() < std::size(kNeedPopulation) && t.population >= kNeedPopulation[t.needs.size()]) {
        std::vector<CargoType> choices;
        for (const CargoType c : needPool(t.needs.size()))
            if (std::ranges::find(t.needs, c) == t.needs.end()) choices.push_back(c);
        if (choices.empty()) break;
        const CargoType pick = choices[t.rng.below(static_cast<u32>(choices.size()))];
        t.needs.push_back(pick);
        if (t.needs.size() > 1)
            w.addNews(NewsKind::Town, std::format("{} now needs {} to keep growing", t.name, cargoName(pick)), t.centre);
    }
}

i32 growthRate(const Town& t) {
    i32 base = 8;
    switch (t.cls) {
    case TownClass::Village: base = 8; break;
    case TownClass::Town: base = 12; break;
    case TownClass::City: base = 18; break;
    }
    i32 rate = base;
    if (t.lastMonth.trips > 0) rate += base * 2 * t.lastMonth.transit / t.lastMonth.trips;
    rate += std::min(t.servedStations, 6) * 2;
    // Arrivals and deliveries benefit the receiving town, including visiting non-residents.
    const u32 population = static_cast<u32>(std::max(1, t.population));
    rate += static_cast<i32>(std::min<u64>(base * 2, static_cast<u64>(base) * t.arrivalsLastMonth / population));
    rate += static_cast<i32>(std::min<u64>(base * 2, static_cast<u64>(base) * t.goodsReceivedLastMonth / std::max(16u, population / 4)));
    // Every need met speeds growth up.
    for (const CargoType c : t.needs) rate += needMet(t, c) ? base : 0;
    return rate;
}

// A new village needs open, gently sloping land well away from other towns.
bool newTownSiteOk(const World& w, TilePos c) {
    for (const auto [id, t] : w.towns) {
        const i64 d = chebyshev(t.centre.x - c.x, t.centre.y - c.y);
        if (d < 40 || d < t.radius + 14) return false;
    }
    int lo = 255, hi = 0;
    for (i32 dy = -5; dy <= 5; ++dy) {
        for (i32 dx = -5; dx <= 5; ++dx) {
            const TilePos p{c.x + dx, c.y + dy};
            const Tile* tile = w.map.tile(p);
            if (!tile || tile->type != TileType::Clear) return false;
            const int h = w.map.cornerHeight(p.x, p.y);
            lo = std::min(lo, h);
            hi = std::max(hi, h);
        }
    }
    return lo >= 1 && hi - lo <= 2 && hi < kSnowLine - 4;
}

TownId findShopTown(const World& w, TownId self) {
    const Town& home = w.towns[self];
    TownId best;
    i64 bestD = 0;
    for (const auto [id, t] : w.towns) {
        if (t.cls != TownClass::City || t.retailCapacity <= 0) continue;
        const i64 d = chebyshev(t.centre.x - home.centre.x, t.centre.y - home.centre.y);
        if (d > 320) continue;
        if (!best.valid() || d < bestD) {
            best = id;
            bestD = d;
        }
    }
    if (best.valid()) return best;
    for (const auto [id, t] : w.towns) {
        if (t.retailCapacity <= 0) continue;
        const i64 d = chebyshev(t.centre.x - home.centre.x, t.centre.y - home.centre.y);
        if (d > 200) continue;
        if (!best.valid() || d < bestD) {
            best = id;
            bestD = d;
        }
    }
    return best;
}

} // namespace

u8 zoneAt(const Town& town, TilePos tile) { return zoneOf(town, tile); }

bool needs(const Town& t, CargoType type) { return std::ranges::find(t.needs, type) != t.needs.end(); }

u32 needDemand(const Town& t) { return static_cast<u32>(std::max(12, t.population / 12)); }

bool needMet(const Town& t, CargoType type) {
    return needs(t, type) && t.suppliedLastMonth[static_cast<usize>(type)] >= needDemand(t);
}

void updateNeeds(World& w, Town& t) { addNeeds(w, t); }

i32 nextNeedPopulation(const Town& t) {
    return t.needs.size() < std::size(kNeedPopulation) ? kNeedPopulation[t.needs.size()] : 0;
}

bool canPlaceBuilding(const World& w, TilePos pos, u8 size) {
    int level = -1;
    for (i32 dy = 0; dy < size; ++dy) {
        for (i32 dx = 0; dx < size; ++dx) {
            const TilePos p{pos.x + dx, pos.y + dy};
            const Tile* t = w.map.tile(p);
            if (!t || t->type != TileType::Clear) return false;
            if (w.trackTiles.contains(ChunkPos{p.x, p.y}.key())) return false; // under a bridge
            const Slope s = w.map.slope(p);
            const Surface surf = buildingSurface(s);
            if (surf == Surface::Invalid) return false;
            if (size > 1) {
                if (!s.isFlat()) return false;
                if (level >= 0 && level != s.base) return false;
                level = s.base;
            }
        }
    }
    return true;
}

BuildingId placeBuilding(World& w, TownId townId, u16 type, TilePos pos, Dir facing, u8 variant, u8 footprint) {
    const BuildingType& bt = buildingType(type);
    const BuildingId id = w.buildings.create();
    {
        Building& b = w.buildings[id];
        b.serial = ++w.nextSerial;
        b.type = type;
        b.town = townId;
        b.pos = pos;
        b.facing = facing;
        b.variant = variant;
        b.builtTick = w.clock.tick;
        b.footprint = footprint;
    }
    for (i32 dy = 0; dy < w.buildings[id].size(); ++dy) {
        for (i32 dx = 0; dx < w.buildings[id].size(); ++dx) {
            const TilePos p{pos.x + dx, pos.y + dy};
            Tile& t = *w.map.tile(p);
            t.type = TileType::Building;
            t.index = id.value;
            t.extra = static_cast<u8>(dx + dy * 2);
            t.owner = kOwnerTown;
            t.trees = 0;
            if (t.ground == Ground::Rock || t.ground == Ground::Rough) t.ground = Ground::Grass;
            w.map.touch(p);
        }
    }
    if (Town* town = w.towns.tryGet(townId)) {
        town->buildings.push_back(id);
        if (bt.jobs > 0) town->jobSites.push_back(id);
        if (bt.customers > 0) town->retail.push_back(id);
        town->jobs += bt.jobs;
        town->retailCapacity += bt.customers;
        // Fill new vacancies from the town's unemployed.
        if (bt.jobs > 0) {
            std::vector<PersonId> still;
            for (const PersonId pid : town->unemployed) {
                Person* p = w.people.tryGet(pid);
                if (!p || p->employed() || p->town != townId) continue;
                Building& b = w.buildings[id];
                if (b.workers < bt.jobs) {
                    ++b.workers;
                    ++town->workers;
                    p->work = b.ref(id);
                    people::reschedule(w, pid);
                } else {
                    still.push_back(pid);
                }
            }
            town->unemployed = std::move(still);
        }
    }
    // Move residents in (not always to full capacity).
    if (bt.residents > 0) {
        Town* town = w.towns.tryGet(townId);
        const u32 occupancy = town ? 75 + town->rng.below(26) : 100;
        const i32 count = std::max<i32>(1, static_cast<i32>(bt.residents * occupancy / 100));
        for (i32 i = 0; i < count; ++i) people::create(w, id);
        if (town) town->population += count;
    }
    return id;
}

void removeBuilding(World& w, BuildingId id) {
    const Building b = w.buildings[id];
    for (i32 dy = 0; dy < b.size(); ++dy) {
        for (i32 dx = 0; dx < b.size(); ++dx) {
            const TilePos p{b.pos.x + dx, b.pos.y + dy};
            Tile& t = *w.map.tile(p);
            t.type = TileType::Clear;
            t.index = 0;
            t.extra = 0;
            t.owner = kOwnerNone;
            w.map.touch(p);
        }
    }
    if (Town* town = w.towns.tryGet(b.town)) {
        std::erase(town->buildings, id);
        std::erase(town->jobSites, id);
        std::erase(town->retail, id);
    }
    w.buildings.destroy(id);
    recompute(w, b.town);
}

void recompute(World& w, TownId id) {
    Town* t = w.towns.tryGet(id);
    if (!t) return;
    t->population = 0;
    t->jobs = 0;
    t->workers = 0;
    t->retailCapacity = 0;
    i64 radius = 4;
    std::erase_if(t->buildings, [&](BuildingId b) { return !w.buildings.contains(b); });
    for (const BuildingId bid : t->buildings) {
        const Building& b = w.buildings[bid];
        const BuildingType& bt = b.info();
        t->population += b.residents;
        t->jobs += bt.jobs;
        t->workers += b.workers;
        t->retailCapacity += bt.customers;
        radius = std::max(radius, chebyshev(b.pos.x - t->centre.x, b.pos.y - t->centre.y) + b.size());
    }
    t->radius = static_cast<i32>(radius);
}

void onChunkActivated(World& w, ChunkPos chunk) {
    const TilePos o = chunkOrigin(chunk);
    const i32 x0 = floorDiv(o.x - kTownFootprint, kTownCellSize);
    const i32 x1 = floorDiv(o.x + kChunkSize + kTownFootprint, kTownCellSize);
    const i32 y0 = floorDiv(o.y - kTownFootprint, kTownCellSize);
    const i32 y1 = floorDiv(o.y + kChunkSize + kTownFootprint, kTownCellSize);
    for (i32 cy = y0; cy <= y1; ++cy) {
        for (i32 cx = x0; cx <= x1; ++cx) {
            const u64 key = ChunkPos{cx, cy}.key();
            if (w.processedTownCells.contains(key)) continue;
            const std::optional<TownSite> site = townSiteForCell(w.map.generator(), cx, cy);
            if (!site) {
                w.processedTownCells.insert(key);
                continue;
            }
            if (!footprintExplored(w.map, site->centre)) continue;
            w.processedTownCells.insert(key);
            foundTown(w, *site);
        }
    }
}

TownId foundTown(World& w, const TownSite& site) {
    const TownId id = w.towns.create();
    {
        Town& t = w.towns[id];
        t.centre = site.centre;
        t.cellKey = site.cellKey;
        t.cls = site.cls;
        t.rng.seed(site.seed, 0x707E);
        t.foundedTick = w.clock.tick;
        t.name = uniqueName(w, site.seed, id);
        t.grid = site.cls == TownClass::City ? t.rng.chance(7, 10)
                                             : (site.cls == TownClass::Town ? t.rng.chance(4, 10) : false);
        t.gridSpacing = 3;
        t.radius = site.cls == TownClass::City ? 12 : (site.cls == TownClass::Town ? 8 : 5);
        t.shopTown = id;
    }
    makeCentre(w, id);
    const i32 target = site.targetPopulation;
    for (i32 steps = 0; w.towns[id].population < target && steps < target * 2 + 300; ++steps) {
        growStep(w, id, true);
        // Let the town spread as it grows.
        Town& t = w.towns[id];
        if (steps % 16 == 0) {
            recompute(w, id);
            t.radius = std::max(t.radius, static_cast<i32>(isqrt(static_cast<u64>(t.population))) / 2 + 4);
        }
    }
    recompute(w, id);
    Town& t = w.towns[id];
    t.shopTown = findShopTown(w, id);
    if (w.settings.industries) addNeeds(w, t);
    return id;
}

TownId townAt(const World& w, TilePos tile) {
    TownId best;
    i64 bestD = 0;
    for (const auto [id, t] : w.towns) {
        const i64 d = chebyshev(t.centre.x - tile.x, t.centre.y - tile.y);
        if (d > t.radius + 3) continue;
        if (!best.valid() || d < bestD) {
            best = id;
            bestD = d;
        }
    }
    return best;
}

i32 rating(const Town& t, CompanyId company) {
    const auto it = t.ratings.find(company.value);
    return it == t.ratings.end() ? kRatingInitial : it->second;
}

void changeRating(World& w, TownId town, CompanyId company, i32 delta, i32 floor) {
    Town* t = w.towns.tryGet(town);
    if (!t || !w.companies.contains(company)) return;
    const i32 cur = rating(*t, company);
    i32 next = std::clamp(cur + delta, kRatingMin, kRatingMax);
    if (delta < 0) next = std::max(next, std::min(cur, floor));
    t->ratings[company.value] = static_cast<i16>(next);
}

std::string authorityRefusal(const World& w, CompanyId company, TilePos tile, i32 minRating) {
    if (!w.companies.contains(company)) return {};
    const TownId id = townAt(w, tile);
    const Town* t = w.towns.tryGet(id);
    if (!t || rating(*t, company) >= minRating) return {};
    return std::format("{} local authority refuses to allow this (rating: {})", t->name,
                       ratingName(rating(*t, company)));
}

void spawnNewTowns(World& w) {
    // Roughly one new village every four months.
    if (w.rng.below(4) != 0) return;
    const std::vector<ChunkPos>& chunks = w.map.activeChunks();
    if (chunks.size() < 16) return;
    std::vector<TilePos> remoteStations;
    for (const auto [id, st] : w.stations)
        if (!townAt(w, st.sign).valid()) remoteStations.push_back(st.sign);
    for (int attempt = 0; attempt < 24; ++attempt) {
        TilePos c;
        if (!remoteStations.empty() && attempt % 2 == 0) {
            // Settlers like a station nearby.
            const TilePos s = remoteStations[w.rng.below(static_cast<u32>(remoteStations.size()))];
            c = {s.x + static_cast<i32>(w.rng.below(13)) - 6, s.y + static_cast<i32>(w.rng.below(13)) - 6};
        } else {
            const TilePos o = chunkOrigin(chunks[w.rng.below(static_cast<u32>(chunks.size()))]);
            c = {o.x + static_cast<i32>(w.rng.below(kChunkSize)), o.y + static_cast<i32>(w.rng.below(kChunkSize))};
        }
        if (!newTownSiteOk(w, c)) continue;
        TownSite site;
        site.centre = c;
        site.cls = TownClass::Village;
        site.seed = w.rng.next();
        site.cellKey = 0;
        site.targetPopulation = 40 + static_cast<i32>(w.rng.below(60));
        const TownId id = foundTown(w, site);
        w.addNews(NewsKind::Town, std::format("New village of {} founded", w.towns[id].name), c);
        return;
    }
}

void growAll(World& w) {
    for (auto [id, t] : w.towns) {
        // A town with needs that are not all met stops growing when it reaches the size at which
        // it would ask for the next one.
        if (w.settings.industries && !t.needs.empty() && !std::ranges::all_of(t.needs, [&](CargoType c) { return needMet(t, c); })) {
            const i32 cap = t.needs.size() < std::size(kNeedPopulation) ? kNeedPopulation[t.needs.size()] : 8000;
            if (t.population >= cap) continue;
        }
        t.growthPoints += w.economy.recessionMonths > 0 ? std::max(1, growthRate(t) / 2) : growthRate(t);
        while (t.growthPoints >= 100) {
            t.growthPoints -= 100;
            const i32 before = static_cast<i32>(t.buildings.size());
            growStep(w, id, false);
            if (static_cast<i32>(t.buildings.size()) != before)
                t.radius = std::max(t.radius, static_cast<i32>(isqrt(static_cast<u64>(t.population))) / 2 + 4);
        }
    }
}

void monthly(World& w) {
    const i64 recent = w.clock.tick - 30 * kTicksPerDay;
    for (auto [id, t] : w.towns) {
        t.lastMonth = t.thisMonth;
        t.thisMonth = {};
        t.arrivalsLastMonth = t.arrivals;
        t.arrivals = 0;
        t.goodsReceivedLastMonth = t.goodsReceived;
        t.goodsReceived = 0;
        recompute(w, id);
        t.suppliedLastMonth = t.supplied;
        t.supplied = {};
        if (w.settings.industries) addNeeds(w, t);
        // Local authority: companies serving their stations here gain standing, neglected
        // stations cost it; without any presence ratings drift back towards neutral.
        std::map<u8, std::pair<i32, i32>> presence; // company -> (stations, served)
        for (const auto [sid, st] : w.stations) {
            if (chebyshev(st.sign.x - t.centre.x, st.sign.y - t.centre.y) > t.radius + 3) continue;
            auto& [total, served] = presence[st.owner.value];
            ++total;
            if (st.lastVisitTick >= recent) ++served;
        }
        t.servedStations = 0;
        for (const auto& [company, counts] : presence) {
            const auto [total, served] = counts;
            t.servedStations += served;
            const i32 delta = std::clamp(served * 15 - (total - served) * 10, -60, 60);
            changeRating(w, id, CompanyId(company), delta);
        }
        for (auto& [company, value] : t.ratings) {
            if (presence.contains(company)) continue;
            if (value > kRatingInitial) value = static_cast<i16>(std::max(kRatingInitial, value - 10));
            if (value < kRatingInitial) value = static_cast<i16>(std::min(kRatingInitial, value + 10));
        }
        if (t.cls == TownClass::Village && t.population >= 400) {
            t.cls = TownClass::Town;
            w.addNews(NewsKind::Town, std::format("{} has grown into a town", t.name), t.centre);
        } else if (t.cls == TownClass::Town && t.population >= 1500) {
            t.cls = TownClass::City;
            w.addNews(NewsKind::Town, std::format("{} has grown into a city", t.name), t.centre);
        }
        t.shopTown = findShopTown(w, id);
    }
}

} // namespace towns

} // namespace tp
