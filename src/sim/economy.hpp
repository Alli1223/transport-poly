#pragma once

#include "core/types.hpp"

namespace tp {
class World;
struct Economy {
    i32 priceIndex = 10'000; // basis points, construction / vehicles / upkeep / fares
    i32 recessionMonths = 0;
    i32 nextRecessionMonth = 0; // absolute calendar month
    template <class Ar> void serialize(Ar& ar) { ar(priceIndex, recessionMonths, nextRecessionMonth); }
};
namespace economy { void monthly(World& w); }
}
