#include "sim/world.hpp"

#include <format>
#include <bit>

#include "core/hash.hpp"
#include "sim/prices.hpp"

namespace tp {

World::World(const WorldSettings& s) : settings(s), rng(s.seed, 0x5EED), map(s.seed, s.waterways) {
    clock.startDay = daysFromCivil(s.startYear, 1, 1);
    // Start on the first Monday of the year so the week structure is easy to follow.
    while (weekdayFromDays(clock.startDay) != Weekday::Monday) ++clock.startDay;
}

void World::tick() {
    ++clock.tick;
    passengers::updateRouting(*this);
    people::process(*this);
    vehicles::tickAll(*this);
    if (clock.isNewDay()) onNewDay();
}

void World::onNewDay() {
    towns::growAll(*this);
    vehicles::daily(*this);
    const CivilDate d = clock.date();
    if (d.day == 1) {
        onNewMonth();
        if (d.month == 1) onNewYear();
    }
}

void World::onNewMonth() {
    towns::monthly(*this);
    towns::spawnNewTowns(*this);
    stations::monthly(*this);
    people::jobMarket(*this);
    // Road and track ownership are independent (town roads may carry company trams).
    // Charge each facility tile once; station/depot pools refer to these same tiles.
    for (const ChunkPos cp : map.activeChunks()) {
        for (const Tile& tile : map.chunk(cp)->tiles) {
            const auto charge = [&](u8 owner, Money amount) {
                if (amount > 0)
                    if (Company* c = companies.tryGet(CompanyId{owner}))
                        c->spend(Expense::PropertyMaintenance, amount);
            };
            charge(tile.roadOwner, std::popcount(static_cast<unsigned>(tile.road & 0x0F)) * prices::kRoadMaintenance);
            Money track = std::popcount(static_cast<unsigned>(tile.road >> 4)) * prices::kTramMaintenance +
                          std::popcount(static_cast<unsigned>(tile.rail & 0x3F)) * prices::kRailMaintenance;
            if (tile.type == TileType::Rail && tile.extra) track += prices::kSignalMaintenance;
            if (tile.type == TileType::Station) track += prices::kStationMaintenance;
            if (tile.type == TileType::Depot) track += prices::kDepotMaintenance;
            if (tile.type == TileType::Water) track += prices::kCanalMaintenance;
            if (tile.span) track += 20;
            charge(tile.owner, track);
        }
    }
    for (auto [id, c] : companies) {
        // Monthly loan interest.
        if (c.loan > 0) {
            const Money interest = (c.loan * settings.interestPercent + 1199) / 1200;
            c.spend(Expense::LoanInterest, interest);
        }
    }
    const CivilDate d = clock.date();
    if (d.month % 3 == 1) {
        // Quarterly bankruptcy tracking.
        for (auto [id, c] : companies) c.quartersInDebt = c.money < 0 ? c.quartersInDebt + 1 : 0;
    }
}

void World::onNewYear() {
    vehicles::yearly(*this);
    const i32 year = clock.date().year;
    for (const VehicleModel& m : vehicleModels())
        if (m.introYear == year)
            addNews(NewsKind::Vehicle, std::format("New {} available: {} ({} km/h{})", vehicleTypeName(m.type), m.name,
                                                   m.maxSpeedKmh,
                                                   m.capacity ? std::format(", {} seats", m.capacity) : std::string()));
    for (auto [id, c] : companies) {
        c.thisYear.year = year - 1;
        c.history.push_back(c.thisYear);
        if (c.history.size() > 20) c.history.erase(c.history.begin());
        c.thisYear = YearLedger{};
        c.thisYear.year = year;
        c.passengersLastYear = c.passengersThisYear;
        c.passengersThisYear = 0;
    }
}

bool World::activateChunk(ChunkPos pos) {
    if (!map.activate(pos)) return false;
    towns::onChunkActivated(*this, pos);
    return true;
}

void World::rebuildSignalIndex() {
    signalTiles.clear();
    for (const ChunkPos cp : map.activeChunks()) {
        const Chunk* c = map.chunk(cp);
        const TilePos o = chunkOrigin(cp);
        for (i32 ly = 0; ly < kChunkSize; ++ly)
            for (i32 lx = 0; lx < kChunkSize; ++lx) {
                const Tile& t = c->tile(lx, ly);
                if ((t.type == TileType::Rail && t.extra) || (t.type == TileType::Road && t.rail))
                    signalTiles.insert(ChunkPos{o.x + lx, o.y + ly}.key());
            }
    }
}

void World::addNews(NewsKind kind, std::string text, std::optional<TilePos> where) {
    news.push_back(NewsItem{clock.tick, kind, std::move(text), where});
    while (news.size() > 200) news.pop_front();
}

u64 World::stateHash() const {
    StateHasher h;
    h.add(clock.tick);
    h.add(rng.stateForHash());
    h.add(map.activeChunks().size());
    for (const auto [id, c] : companies) h.addAll(id.value, c.money, c.loan);
    h.addAll(towns.size(), buildings.size(), people.size(), nextSerial);
    for (const auto [id, t] : towns) h.addAll(id.value, t.population, t.jobs, t.workers, t.roads.size());
    u64 acc = 0;
    for (const auto [id, p] : people)
        acc += static_cast<u64>(p.nextEvent) * 1315423911ULL ^ (static_cast<u64>(p.state) << 3) ^ id.value;
    h.add(acc);
    h.addAll(stations.size(), depots.size(), vehicles.size());
    for (const auto [id, v] : vehicles)
        h.addAll(id.value, v.tile.x, v.tile.y, v.progress, v.speed, static_cast<u8>(v.state), v.passengers.size());
    return h.value();
}

} // namespace tp
