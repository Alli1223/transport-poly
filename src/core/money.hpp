#pragma once

#include "core/types.hpp"

#include <string>

namespace tp {

// Money is a plain signed 64-bit amount in whole currency units.
using Money = i64;

// "$1,234,567" / "-$12,000"
std::string formatMoney(Money amount);
// Compact form for tight UI spots: "$1.2M", "$350k", "$90".
std::string formatMoneyShort(Money amount);

} // namespace tp
