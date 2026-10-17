#pragma once
// Strongly typed ids for every pooled simulation object.

#include "core/id.hpp"
#include "core/types.hpp"

namespace tp {

struct CompanyTag {};
struct TownTag {};
struct BuildingTag {};
struct PersonTag {};
struct StationTag {};
struct DepotTag {};
struct VehicleTag {};

using CompanyId = StrongId<CompanyTag, u8>;
using TownId = StrongId<TownTag>;
using BuildingId = StrongId<BuildingTag>;
using PersonId = StrongId<PersonTag>;
using StationId = StrongId<StationTag>;
using DepotId = StrongId<DepotTag>;
using VehicleId = StrongId<VehicleTag>;

inline constexpr u8 kMaxCompanies = 15;

} // namespace tp
