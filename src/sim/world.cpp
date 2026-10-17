#include "sim/world.hpp"

#include "core/hash.hpp"

namespace tp {

World::World(const WorldSettings& s) : settings(s), rng(s.seed, 0x5EED), map(s.seed) {
    clock.startDay = daysFromCivil(s.startYear, 1, 1);
    // Start on the first Monday of the year so the week structure is easy to follow.
    while (weekdayFromDays(clock.startDay) != Weekday::Monday) ++clock.startDay;
}

void World::tick() {
    ++clock.tick;
    if (clock.isNewDay()) onNewDay();
}

void World::onNewDay() {
    const CivilDate d = clock.date();
    if (d.day == 1) {
        onNewMonth();
        if (d.month == 1) onNewYear();
    }
}

void World::onNewMonth() {
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
    const i32 year = clock.date().year;
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
    return true;
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
    return h.value();
}

} // namespace tp
