#pragma once
// Deterministic procedural names. The same seed always yields the same name.

#include "core/types.hpp"

#include <string>

namespace tp {

std::string generateTownName(u32 seed);
std::string generatePersonFirstName(u32 seed);
std::string generatePersonSurname(u32 seed);
std::string generateCompanyName(u32 seed);
// Suffix appended to a town name to distinguish several stations in the same town,
// e.g. "Central", "North", "Market". index 0 yields the empty string.
std::string stationNameSuffix(u32 index);

} // namespace tp
