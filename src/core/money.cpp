#include "core/money.hpp"

#include <format>

namespace tp {

std::string formatMoney(Money amount) {
    const bool negative = amount < 0;
    u64 v = negative ? static_cast<u64>(-(amount + 1)) + 1 : static_cast<u64>(amount);
    std::string digits = std::to_string(v);
    std::string grouped;
    grouped.reserve(digits.size() + digits.size() / 3 + 2);
    const usize lead = digits.size() % 3 == 0 ? 3 : digits.size() % 3;
    for (usize i = 0; i < digits.size(); ++i) {
        if (i != 0 && (i - lead) % 3 == 0 && i >= lead) grouped.push_back(',');
        grouped.push_back(digits[i]);
    }
    return std::format("{}${}", negative ? "-" : "", grouped);
}

std::string formatMoneyShort(Money amount) {
    const bool negative = amount < 0;
    const double v = static_cast<double>(negative ? -amount : amount);
    const char* sign = negative ? "-" : "";
    if (v >= 1e9) return std::format("{}${:.1f}B", sign, v / 1e9);
    if (v >= 1e6) return std::format("{}${:.1f}M", sign, v / 1e6);
    if (v >= 1e4) return std::format("{}${:.0f}k", sign, v / 1e3);
    return std::format("{}${:.0f}", sign, v);
}

} // namespace tp
