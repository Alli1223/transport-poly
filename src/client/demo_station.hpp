#pragma once
#include "sim/world.hpp"

namespace tp {
// Deterministic scene for the --demo-station rendering smoke test.
StationId buildDemoStation(World& world, CompanyId company);
} // namespace tp
