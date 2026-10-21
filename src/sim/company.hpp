#pragma once
// Player companies and their finances.

#include "core/money.hpp"
#include "core/types.hpp"
#include "sim/feedback.hpp"
#include "sim/ids.hpp"

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tp {

enum class Expense : u8 {
    Construction,
    NewVehicles,
    TrainRunning,
    RoadVehicleRunning,
    ShipRunning,
    PropertyMaintenance,
    TrainIncome,
    RoadVehicleIncome,
    ShipIncome,
    LoanInterest,
    Other,
    Count
};
inline constexpr usize kExpenseCount = static_cast<usize>(Expense::Count);

std::string_view expenseName(Expense e);
bool isIncome(Expense e);

// One year of income / expense totals per category (expenses negative, income positive).
struct YearLedger {
    i32 year = 0;
    std::array<Money, kExpenseCount> amounts{};

    Money total() const;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(year, amounts);
    }
};

struct MonthLedger : YearLedger {
    u8 month = 0;
    template <class Ar> void serialize(Ar& ar) { ar(year, amounts, month); }
};

// Company livery colours (index into this palette is stored per company).
inline constexpr usize kCompanyColourCount = 16;
u32 companyColourRgb(u8 index); // 0xRRGGBB
std::string_view companyColourName(u8 index);

struct Company {
    std::string name;
    u8 colour = 0;
    Money money = 0;
    Money loan = 0;
    i64 foundedTick = 0;
    i32 quartersInDebt = 0;
    YearLedger thisYear;
    std::vector<YearLedger> history; // most recent last, capped
    // Simple performance stats.
    i64 passengersThisYear = 0;
    i64 passengersLastYear = 0;
    MonthLedger thisMonth;
    std::vector<MonthLedger> monthlyHistory;
    bool bankrupt = false;
    bool aiControlled = false;
    // The league (see league.hpp): this month's performance rating, and the rating and company
    // value at the start of each month, most recent last.
    i32 rating = 0;
    std::vector<i16> ratingHistory;
    std::vector<Money> valueHistory;
    // Vehicle replacement at depot visits: renew vehicles past their life span, and swap the
    // first model of each pair for the second. The last "can't afford it" news, -1 none.
    bool autorenew = false;
    std::vector<std::pair<u16, u16>> replacements;
    i64 replaceNewsTick = -1;
    // When passengers last commented on the company (tick + 1, 0 never), by kind (see feedback.hpp).
    std::array<i64, kFeedbackKinds> feedbackTicks{};

    void spend(Expense category, Money amount); // amount > 0 is subtracted from money
    void earn(Expense category, Money amount);  // amount > 0 is added to money

    template <class Ar>
    void serialize(Ar& ar) {
        ar(name, colour, money, loan, foundedTick, quartersInDebt, thisYear, history, passengersThisYear,
           passengersLastYear);
        if (ar.version == 0 || ar.version >= 8) ar(thisMonth, monthlyHistory, bankrupt);
        if (ar.version == 0 || ar.version >= 17) ar(aiControlled);
        if (ar.version == 0 || ar.version >= 26) ar(rating, ratingHistory, valueHistory);
        if (ar.version == 0 || ar.version >= 29) ar(autorenew, replacements, replaceNewsTick, feedbackTicks);
    }
};

class World;
void liquidateCompany(World& world, CompanyId company);

} // namespace tp
