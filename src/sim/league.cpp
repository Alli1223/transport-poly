#include "sim/league.hpp"

#include "sim/world.hpp"

#include <algorithm>
#include <format>

namespace tp::league {

namespace {

constexpr usize kHistoryMonths = 120;

constexpr std::array<MeasureInfo, kMeasureCount> kMeasures{{
    {"Vehicles making a profit", 120, 100},
    {"Stations served last month", 80, 100},
    {"Lowest vehicle profit", 10'000, 100},
    {"Income, last 12 months", 5'000'000, 150},
    {"Operating profit, last 12 months", 2'000'000, 150},
    {"Passengers carried last year", 100'000, 150},
    {"Cash", 10'000'000, 50},
    {"Loan", 0, 50},
    {"Company value", 20'000'000, 150},
}};

bool operating(Expense e) {
    return e != Expense::Construction && e != Expense::NewVehicles && e != Expense::LoanInterest;
}

} // namespace

const MeasureInfo& measureInfo(Measure m) { return kMeasures[static_cast<usize>(m)]; }

Money companyValue(const World& w, CompanyId company) {
    const Company* c = w.companies.tryGet(company);
    if (!c) return 0;
    Money value = c->money - c->loan;
    for (const auto [id, v] : w.vehicles)
        if (v.owner == company) value += v.value;
    return value;
}

Rating rate(const World& w, CompanyId company) {
    Rating r;
    const Company* c = w.companies.tryGet(company);
    if (!c) return r;
    i64 profitable = 0, lowest = 0, vehicles = 0;
    for (const auto [id, v] : w.vehicles) {
        if (v.owner != company) continue;
        if (v.profitLastYear > 0) ++profitable;
        lowest = vehicles == 0 ? v.profitLastYear : std::min<i64>(lowest, v.profitLastYear);
        ++vehicles;
    }
    i64 served = 0;
    const i64 month = 30 * kTicksPerDay;
    for (const auto [id, s] : w.stations)
        if (s.owner == company && s.lastVisitTick >= 0 && w.clock.tick - s.lastVisitTick <= month) ++served;
    Money income = 0, operatingProfit = 0;
    const usize first = c->monthlyHistory.size() > 12 ? c->monthlyHistory.size() - 12 : 0;
    for (usize i = first; i < c->monthlyHistory.size(); ++i)
        for (usize e = 0; e < kExpenseCount; ++e) {
            const Money amount = c->monthlyHistory[i].amounts[e];
            if (isIncome(static_cast<Expense>(e))) income += amount;
            if (operating(static_cast<Expense>(e))) operatingProfit += amount;
        }
    r.values = {profitable, served, lowest, income, operatingProfit, c->passengersLastYear, c->money, c->loan, companyValue(w, company)};
    for (usize m = 0; m < kMeasureCount; ++m) {
        const MeasureInfo& info = kMeasures[m];
        const i64 value = r.values[m];
        i64 points = 0;
        if (static_cast<Measure>(m) == Measure::Loan) {
            // Full points without a loan, none at the most a company may borrow.
            const Money most = std::max<Money>(1, w.settings.maxLoan);
            points = info.maxPoints * std::max<Money>(0, most - value) / most;
        } else if (static_cast<Measure>(m) == Measure::LowestProfit && vehicles == 0) {
            points = 0;
        } else {
            points = info.maxPoints * std::clamp<i64>(value, 0, info.target) / info.target;
        }
        r.points[m] = static_cast<i32>(std::clamp<i64>(points, 0, info.maxPoints));
        r.total += r.points[m];
    }
    return r;
}

std::string_view title(i32 rating) {
    if (rating >= 960) return "Tycoon";
    if (rating >= 896) return "President";
    if (rating >= 768) return "Chairman";
    if (rating >= 640) return "Chief Executive";
    if (rating >= 512) return "Director";
    if (rating >= 384) return "Route Supervisor";
    if (rating >= 256) return "Transport Coordinator";
    if (rating >= 128) return "Traffic Manager";
    return "Engineer";
}

void monthly(World& w) {
    // The league before this month's ratings, to tell when a new company takes the lead.
    const auto leader = [&](bool previous) {
        CompanyId best;
        i32 bestRating = -1;
        usize active = 0;
        for (const auto [id, c] : w.companies) {
            if (c.bankrupt) continue;
            ++active;
            const i32 rating = previous ? (c.ratingHistory.empty() ? -1 : c.ratingHistory.back()) : c.rating;
            if (rating > bestRating) {
                bestRating = rating;
                best = id;
            }
        }
        return active >= 2 ? best : CompanyId{};
    };
    const CompanyId before = leader(true);
    for (auto [id, c] : w.companies) {
        if (c.bankrupt) continue;
        c.rating = rate(w, id).total;
        c.ratingHistory.push_back(static_cast<i16>(c.rating));
        c.valueHistory.push_back(companyValue(w, id));
        if (c.ratingHistory.size() > kHistoryMonths) c.ratingHistory.erase(c.ratingHistory.begin());
        if (c.valueHistory.size() > kHistoryMonths) c.valueHistory.erase(c.valueHistory.begin());
    }
    const CompanyId now = leader(false);
    if (now.valid() && before.valid() && now != before)
        w.addNews(NewsKind::Company, std::format("{} now leads the league with a rating of {}", w.companies[now].name, w.companies[now].rating));

    // The competition: the first company to the target value, or the best rating when the year comes.
    if (w.competitionWinner.valid() || w.competitionOver) return;
    CompanyId winner;
    if (w.settings.goalValue > 0) {
        Money best = 0;
        for (const auto [id, c] : w.companies)
            if (!c.bankrupt && !c.valueHistory.empty() && c.valueHistory.back() >= w.settings.goalValue && c.valueHistory.back() > best) {
                best = c.valueHistory.back();
                winner = id;
            }
        if (winner.valid())
            w.addNews(NewsKind::Company, std::format("{} wins the race to a company value of {}!", w.companies[winner].name,
                                                     formatMoney(w.settings.goalValue)));
    }
    if (!winner.valid() && w.settings.goalYear > 0 && w.clock.date().year >= w.settings.goalYear) {
        i32 best = -1;
        for (const auto [id, c] : w.companies)
            if (!c.bankrupt && c.rating > best) {
                best = c.rating;
                winner = id;
            }
        if (winner.valid())
            w.addNews(NewsKind::Company, std::format("{} wins the competition as {} with a rating of {}!", w.companies[winner].name,
                                                     title(best), best));
        w.competitionOver = true;
    }
    if (winner.valid()) {
        w.competitionWinner = winner;
        w.competitionOver = true;
    }
}

} // namespace tp::league
