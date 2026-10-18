#include "sim/world.hpp"

#include <format>
#include <bit>

#include "core/hash.hpp"
#include "core/serialize.hpp"
#include "sim/prices.hpp"

namespace tp {

World::World(const WorldSettings& s) : settings(s), rng(s.seed, 0x5EED), map(s.seed, s.waterways) {
    clock.startDay = daysFromCivil(s.startYear, 1, 1);
    // Start on the first Monday of the year so the week structure is easy to follow.
    while (weekdayFromDays(clock.startDay) != Weekday::Monday) ++clock.startDay;
    economy.nextRecessionMonth = s.startYear * 12 + 36 + static_cast<i32>(mix64(s.seed) % 37);
}

void World::tick() {
    ++clock.tick;
    if (clock.isNewDay()) onNewDay();
    passengers::updateRouting(*this);
    people::process(*this);
    vehicles::tickAll(*this);
}

void World::onNewDay() {
    const CivilDate d = clock.date();
    if (d.day == 1) {
        if (d.month == 1) onNewYear();
        onNewMonth();
    }
    towns::growAll(*this);
    vehicles::daily(*this);
}

void World::onNewMonth() {
    const CivilDate d = clock.date();
    for (auto [id, c] : companies) {
        if (c.thisMonth.month) c.monthlyHistory.push_back(c.thisMonth);
        if (c.monthlyHistory.size() > 60) c.monthlyHistory.erase(c.monthlyHistory.begin());
        c.thisMonth = MonthLedger{};
        c.thisMonth.year = d.year;
        c.thisMonth.month = static_cast<u8>(d.month);
    }
    economy::monthly(*this);
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
                        c->spend(Expense::PropertyMaintenance, inflated(amount));
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
    if (d.month % 3 == 1) {
        for (auto [id, c] : companies) {
            if (c.bankrupt) continue;
            c.quartersInDebt = c.money < 0 ? c.quartersInDebt + 1 : 0;
            if (c.quartersInDebt >= 4) liquidateCompany(*this, id);
            else if (c.quartersInDebt > 0)
                addNews(NewsKind::Finance, std::format("{} has negative cash for {} quarter(s). Bankruptcy follows four quarters in debt.", c.name, c.quartersInDebt));
        }
    }
    for (auto [id, c] : companies) if (c.money >= 0 && !c.bankrupt) c.quartersInDebt = 0;
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
    // Hash the canonical save representation so new simulation fields are covered
    // automatically. Render revisions and rebuildable lookup caches are excluded.
    const auto bytes = toBytes(*this, kSaveVersion);
    return hashString(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

} // namespace tp
