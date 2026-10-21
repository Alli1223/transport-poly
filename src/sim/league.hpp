#pragma once
// How companies compare: their value, a performance rating out of 1000 in the style of OpenTTD's
// detailed performance rating, a title for it, the monthly history behind the company graphs, and
// the competition a game can be played for (first to a company value, or the best rating by a year).

#include "core/money.hpp"
#include "core/types.hpp"
#include "sim/ids.hpp"

#include <array>
#include <string_view>

namespace tp {

class World;

namespace league {

enum class Measure : u8 {
    ProfitableVehicles, // vehicles that made a profit last year
    ServedStations,     // stations a vehicle called at in the last month
    LowestProfit,       // the least profit any of its vehicles made last year
    Income,             // income over the last twelve months
    OperatingProfit,    // income less running and upkeep costs over the last twelve months
    Passengers,         // passengers carried last year
    Cash,
    Loan,               // the less of it the better
    CompanyValue,
    Count
};
inline constexpr usize kMeasureCount = static_cast<usize>(Measure::Count);

struct MeasureInfo {
    std::string_view name;
    i64 target;    // the value that earns full points
    i32 maxPoints;
};
const MeasureInfo& measureInfo(Measure m);

struct Rating {
    std::array<i64, kMeasureCount> values{};
    std::array<i32, kMeasureCount> points{};
    i32 total = 0; // 0 - 1000
};

// Cash less the loan, plus what its vehicles are worth.
Money companyValue(const World& w, CompanyId company);
Rating rate(const World& w, CompanyId company);
// The title a rating earns, from Engineer to Tycoon.
std::string_view title(i32 rating);

// Start of a month: records each company's value and rating for the graphs, announces a new league
// leader, and decides the competition once someone has won it.
void monthly(World& w);

} // namespace league
} // namespace tp
