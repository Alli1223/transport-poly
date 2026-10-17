#include "core/calendar.hpp"

#include <array>
#include <format>

namespace tp {

namespace {
constexpr std::array<std::string_view, 7> kWeekdays = {"Monday", "Tuesday",  "Wednesday", "Thursday",
                                                       "Friday", "Saturday", "Sunday"};
constexpr std::array<std::string_view, 7> kWeekdaysShort = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
constexpr std::array<std::string_view, 12> kMonths = {"January", "February", "March",     "April",
                                                      "May",     "June",     "July",      "August",
                                                      "September", "October", "November", "December"};
constexpr std::array<std::string_view, 12> kMonthsShort = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
} // namespace

std::string_view weekdayName(Weekday d) { return kWeekdays[static_cast<usize>(d)]; }
std::string_view weekdayShortName(Weekday d) { return kWeekdaysShort[static_cast<usize>(d)]; }
std::string_view monthName(u32 month) { return kMonths[(month - 1) % 12]; }
std::string_view monthShortName(u32 month) { return kMonthsShort[(month - 1) % 12]; }

std::string formatDate(i32 dayNumber) {
    const CivilDate c = civilFromDays(dayNumber);
    return std::format("{} {} {} {}", weekdayShortName(weekdayFromDays(dayNumber)), c.day, monthShortName(c.month),
                       c.year);
}

std::string formatTimeOfDay(i32 minuteOfDay) {
    const i32 m = ((minuteOfDay % 1440) + 1440) % 1440;
    return std::format("{:02}:{:02}", m / 60, m % 60);
}

} // namespace tp
