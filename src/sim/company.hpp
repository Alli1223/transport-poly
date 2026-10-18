#pragma once
// Player companies and their finances.

#include "core/money.hpp"
#include "core/types.hpp"
#include "sim/ids.hpp"

#include <array>
#include <string>
#include <string_view>
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

    void spend(Expense category, Money amount); // amount > 0 is subtracted from money
    void earn(Expense category, Money amount);  // amount > 0 is added to money

    template <class Ar>
    void serialize(Ar& ar) {
        ar(name, colour, money, loan, foundedTick, quartersInDebt, thisYear, history, passengersThisYear,
           passengersLastYear);
        if (ar.version == 0 || ar.version >= 8) ar(thisMonth, monthlyHistory, bankrupt);
    }
};

class World;
void liquidateCompany(World& world, CompanyId company);

} // namespace tp
