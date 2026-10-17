#pragma once
// Game time. One simulation tick is 15 in-game seconds (4 ticks per minute, 5760 per day; a day
// lasts 3.2 real minutes at normal speed). Dates use the proleptic Gregorian calendar, stored as a
// day number relative to 1970-01-01 (day 0, a Thursday).

#include "core/types.hpp"

#include <string>
#include <string_view>

namespace tp {

inline constexpr i64 kTicksPerMinute = 4;
inline constexpr i64 kTicksPerHour = 60 * kTicksPerMinute;
inline constexpr i64 kTicksPerDay = 24 * kTicksPerHour;
inline constexpr i64 kTicksPerWeek = 7 * kTicksPerDay;
// Real-time ticks per second at normal game speed.
inline constexpr int kTicksPerSecond = 30;

enum class Weekday : u8 { Monday, Tuesday, Wednesday, Thursday, Friday, Saturday, Sunday };

constexpr bool isWeekend(Weekday d) { return d == Weekday::Saturday || d == Weekday::Sunday; }

struct CivilDate {
    i32 year = 1970;
    u32 month = 1; // 1..12
    u32 day = 1;   // 1..31
    friend constexpr bool operator==(const CivilDate&, const CivilDate&) = default;
};

// Howard Hinnant's days_from_civil / civil_from_days algorithms.
constexpr i32 daysFromCivil(i32 y, u32 m, u32 d) {
    y -= m <= 2 ? 1 : 0;
    const i32 era = (y >= 0 ? y : y - 399) / 400;
    const u32 yoe = static_cast<u32>(y - era * 400);
    const u32 doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const u32 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<i32>(doe) - 719468;
}

constexpr CivilDate civilFromDays(i32 z) {
    z += 719468;
    const i32 era = (z >= 0 ? z : z - 146096) / 146097;
    const u32 doe = static_cast<u32>(z - era * 146097);
    const u32 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const i32 y = static_cast<i32>(yoe) + era * 400;
    const u32 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const u32 mp = (5 * doy + 2) / 153;
    const u32 d = doy - (153 * mp + 2) / 5 + 1;
    const u32 m = mp < 10 ? mp + 3 : mp - 9;
    return CivilDate{y + (m <= 2 ? 1 : 0), m, d};
}

constexpr Weekday weekdayFromDays(i32 z) {
    // 1970-01-01 was a Thursday (index 3 with Monday = 0).
    const i32 w = ((z % 7) + 7 + 3) % 7;
    return static_cast<Weekday>(w);
}

constexpr bool isLeapYear(i32 y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

constexpr u32 daysInMonth(i32 y, u32 m) {
    constexpr u32 kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (m == 2 && isLeapYear(y)) ? 29 : kDays[m - 1];
}

// Clock of a running game: the absolute tick counter plus the calendar day the game started on.
struct GameClock {
    i64 tick = 0;
    i32 startDay = daysFromCivil(1950, 1, 2); // a Monday

    constexpr i32 day() const { return startDay + static_cast<i32>(tick / kTicksPerDay); }
    constexpr i32 minuteOfDay() const { return static_cast<i32>((tick % kTicksPerDay) / kTicksPerMinute); }
    // Fractional minute of the day including progress towards the next tick (for rendering).
    constexpr double exactMinuteOfDay(double tickFraction) const {
        return (static_cast<double>(tick % kTicksPerDay) + tickFraction) / static_cast<double>(kTicksPerMinute);
    }
    constexpr Weekday weekday() const { return weekdayFromDays(day()); }
    constexpr CivilDate date() const { return civilFromDays(day()); }
    constexpr bool isNewDay() const { return tick % kTicksPerDay == 0; }
    // Tick at which the given calendar day starts.
    constexpr i64 tickOfDay(i32 dayNumber) const { return static_cast<i64>(dayNumber - startDay) * kTicksPerDay; }

    template <class Ar>
    void serialize(Ar& ar) {
        ar(tick, startDay);
    }
};

std::string_view weekdayName(Weekday d);
std::string_view weekdayShortName(Weekday d);
std::string_view monthName(u32 month);
std::string_view monthShortName(u32 month);
// e.g. "Mon 2 Jan 1950"
std::string formatDate(i32 dayNumber);
// e.g. "07:45"
std::string formatTimeOfDay(i32 minuteOfDay);

} // namespace tp
