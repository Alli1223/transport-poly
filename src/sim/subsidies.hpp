#pragma once
// Subsidies: now and then the authorities offer double pay for a year to the first company to
// start a new service - passengers from one town to another, or one industry's produce to
// where it is wanted. The offer lapses if nobody takes it up in time.

#include "core/money.hpp"
#include "core/types.hpp"
#include "sim/buildings.hpp"
#include "sim/cargo.hpp"
#include "sim/ids.hpp"

#include <string>

namespace tp {

class World;

struct Subsidy {
    CargoType cargo = CargoType::None; // None: passengers
    TownId fromTown, toTown; // passengers: between these towns; cargo: toTown takes it when there is no industry
    BuildingRef source;      // cargo: the producer
    BuildingRef destination; // cargo: the industry that takes it (invalid: any building of toTown)
    i64 offeredTick = 0;
    i64 untilTick = 0; // the offer lapses then, or once awarded the double pay ends
    CompanyId awardedTo;

    bool awarded() const { return awardedTo.valid(); }
    template <class Ar>
    void serialize(Ar& ar) {
        ar(cargo, fromTown, toTown, source, destination, offeredTick, untilTick, awardedTo);
    }
};

inline constexpr i64 kSubsidyOfferDays = 180;
inline constexpr i64 kSubsidyAwardDays = 365;
inline constexpr usize kMaxSubsidyOffers = 4;
inline constexpr i64 kSubsidyMultiplier = 2;

namespace subsidies {

// Lapses old offers and awards, and sometimes offers a new subsidy.
void daily(World& w);
// The pay for passengers carried from one station to another, or for a delivered cargo batch:
// multiplied on a subsidised route, after awarding a subsidy still on offer to the company.
Money passengerPay(World& w, CompanyId company, StationId from, StationId to, Money pay);
Money cargoPay(World& w, CompanyId company, const CargoBatch& batch, const Building& receiver, Money pay);
// "Passengers from Ashby to Brookfield", "Cheese from Cheese dairy near Ashby to Brookfield".
std::string describe(const World& w, const Subsidy& s);
// Where a subsidised service starts and ends (for showing it on the map).
TilePos sourceTile(const World& w, const Subsidy& s);
TilePos destinationTile(const World& w, const Subsidy& s);

} // namespace subsidies

} // namespace tp
