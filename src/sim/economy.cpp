#include "sim/economy.hpp"
#include "core/hash.hpp"
#include "sim/world.hpp"
#include <algorithm>
#include <format>

namespace tp::economy {
void monthly(World& w) {
    auto& e = w.economy;
    const auto d = w.clock.date();
    const i32 month = d.year * 12 + d.month - 1;
    if (w.settings.inflation)
        e.priceIndex = std::min(1'000'000, e.priceIndex + std::max(1, (e.priceIndex * 17 + 5000) / 10'000));
    if (!w.settings.recessions) { e.recessionMonths = 0; return; }
    const u64 h = mix64(w.settings.seed ^ static_cast<u64>(month) * 0x9E3779B97F4A7C15ULL);
    if (e.recessionMonths > 0) {
        if (--e.recessionMonths == 0) {
            e.nextRecessionMonth = month + 36 + static_cast<i32>(h % 37);
            w.addNews(NewsKind::Finance, "The economy has recovered. Fares and town growth return to normal.");
        }
    } else if (month >= e.nextRecessionMonth) {
        e.recessionMonths = 6 + static_cast<i32>(h % 7);
        w.addNews(NewsKind::Finance, std::format("A recession begins: fares fall 30% and town growth slows for {} months.", e.recessionMonths));
    }
}
}
