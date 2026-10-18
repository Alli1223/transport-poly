#include "sim/company.hpp"
#include "sim/world.hpp"
#include <format>

namespace tp {

std::string_view expenseName(Expense e) {
    switch (e) {
    case Expense::Construction: return "Construction";
    case Expense::NewVehicles: return "New vehicles";
    case Expense::TrainRunning: return "Train running costs";
    case Expense::RoadVehicleRunning: return "Road vehicle running costs";
    case Expense::ShipRunning: return "Ship running costs";
    case Expense::PropertyMaintenance: return "Property maintenance";
    case Expense::TrainIncome: return "Train income";
    case Expense::RoadVehicleIncome: return "Road vehicle income";
    case Expense::ShipIncome: return "Ship income";
    case Expense::LoanInterest: return "Loan interest";
    case Expense::Other: return "Other";
    case Expense::Count: break;
    }
    return "?";
}

bool isIncome(Expense e) {
    return e == Expense::TrainIncome || e == Expense::RoadVehicleIncome || e == Expense::ShipIncome;
}

Money YearLedger::total() const {
    Money t = 0;
    for (Money m : amounts) t += m;
    return t;
}

namespace {
struct Colour {
    u32 rgb;
    std::string_view name;
};
constexpr Colour kColours[kCompanyColourCount] = {
    {0x1F4E9C, "Dark Blue"}, {0x8FD18A, "Pale Green"}, {0xE58FB8, "Pink"},      {0xF2C83B, "Yellow"},
    {0xD8382F, "Red"},       {0x6EC1E8, "Light Blue"}, {0x3FA34D, "Green"},     {0x1F6B3A, "Dark Green"},
    {0x2F6FE0, "Blue"},      {0xEADFB4, "Cream"},      {0xB57BA6, "Mauve"},     {0x7340A8, "Purple"},
    {0xF08A2A, "Orange"},    {0x8A5A33, "Brown"},      {0x9AA1A8, "Grey"},      {0xF4F4F0, "White"}};
} // namespace

u32 companyColourRgb(u8 index) { return kColours[index % kCompanyColourCount].rgb; }
std::string_view companyColourName(u8 index) { return kColours[index % kCompanyColourCount].name; }

void Company::spend(Expense category, Money amount) {
    money -= amount;
    thisYear.amounts[static_cast<usize>(category)] -= amount;
    thisMonth.amounts[static_cast<usize>(category)] -= amount;
}

void Company::earn(Expense category, Money amount) {
    money += amount;
    thisYear.amounts[static_cast<usize>(category)] += amount;
    thisMonth.amounts[static_cast<usize>(category)] += amount;
}

void liquidateCompany(World& w, CompanyId company) {
    Company* co = w.companies.tryGet(company);
    if (!co || co->bankrupt) return;
    std::vector<VehicleId> fleet;
    for (const auto [id, v] : w.vehicles) if (v.owner == company) fleet.push_back(id);
    for (const VehicleId id : fleet) vehicles::sell(w, id);
    for (const auto [id, p] : w.people)
        if (p.state == PersonState::Waiting)
            if (const Station* s = w.stations.tryGet(p.station); s && s->owner == company)
                passengers::giveUpWaiting(w, id);
    for (const ChunkPos cp : w.map.activeChunks()) {
        Chunk& chunk = *w.map.chunk(cp);
        for (Tile& tile : chunk.tiles) {
            if (tile.roadOwner == company.value) { tile.roadOwner = kOwnerTown; ++chunk.revision; }
            if (tile.owner != company.value) continue;
            if (tile.type == TileType::Station) {
                const auto kind = static_cast<StopKind>(tile.extra);
                tile.type = kind == StopKind::Rail ? TileType::Rail :
                            (kind == StopKind::Bus || kind == StopKind::Tram) ? TileType::Road : TileType::Clear;
                tile.extra = 0; tile.extra2 = 0; tile.index = 0;
            } else if (tile.type == TileType::Depot) {
                tile.type = TileType::Clear; tile.road = 0; tile.rail = 0;
                tile.extra = 0; tile.extra2 = 0; tile.index = 0;
            }
            tile.owner = kOwnerNone;
            ++chunk.revision;
        }
    }
    std::vector<StationId> stops;
    for (const auto [id, s] : w.stations) if (s.owner == company) stops.push_back(id);
    for (const StationId id : stops) { stations::removeEntrances(w,id); w.stations.destroy(id); }
    std::vector<DepotId> depots;
    for (const auto [id, d] : w.depots) if (d.owner == company) depots.push_back(id);
    for (const DepotId id : depots) w.depots.destroy(id);
    stations::rebuildIndex(w);
    w.rebuildSignalIndex();
    passengers::markDirty(w);
    vehicles::invalidatePaths(w);
    co->bankrupt = true;
    co->loan = 0;
    if (co->money < 0) co->earn(Expense::Other, -co->money);
    w.addNews(NewsKind::Company, std::format("{} is bankrupt. Its vehicles have been liquidated and its routes made public.", co->name));
}

} // namespace tp
