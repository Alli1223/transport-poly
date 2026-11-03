#pragma once
// The icon and colour standing for each kind of freight, shared by the places that make it and the
// places that need it: a quarry's sign, the stone chip on a brickworks' sign and the arrows from
// quarries to a brickworks all wear the same stone badge.

#include "client/ui/icons.hpp"
#include "sim/buildings.hpp"
#include "sim/cargo.hpp"

namespace tp {

struct Badge {
    ui::Icon icon = ui::Icon::None;
    ImU32 colour = 0;
};

// Farms green and gold, mines and quarries in the colours of their rock, mills and factories
// blue and purple, fish the colour of water.
Badge cargoBadge(CargoType type);
// An industry wears the badge of what it makes; shops taking deliveries get a cart.
Badge industryBadge(u16 buildingType);
// Cities amber, towns blue, villages green.
Badge townBadge(TownClass cls);

} // namespace tp
