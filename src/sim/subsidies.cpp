#include "sim/subsidies.hpp"

#include "core/hash.hpp"
#include "core/math.hpp"
#include "core/rng.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>

namespace tp::subsidies {

namespace {

constexpr i64 kMinDistance = 16, kMaxDistance = 160; // tiles between the ends of a subsidised service

const Building* resolve(const World& w, const BuildingRef& r) {
    const Building* b = w.buildings.tryGet(r.id);
    return b && b->serial == r.serial ? b : nullptr;
}

std::string townName(const World& w, TownId id) {
    const Town* t = w.towns.tryGet(id);
    return t ? t->name : std::string("(gone)");
}

std::string place(const World& w, const BuildingRef& r) {
    const Building* b = resolve(w, r);
    if (!b) return "(gone)";
    TownId town = b->town;
    if (!w.towns.contains(town)) town = stations::nearestTown(w, b->pos);
    return std::format("{} near {}", b->info().name, townName(w, town));
}

std::string date(const World& w, i64 tick) { return formatDate(w.clock.startDay + static_cast<i32>(tick / kTicksPerDay)); }

bool sameRoute(const Subsidy& a, const Subsidy& b) {
    return a.cargo == b.cargo && a.fromTown == b.fromTown && a.toTown == b.toTown && a.source == b.source &&
           a.destination == b.destination;
}

// Passengers between two towns that public transport does not link yet.
std::optional<Subsidy> passengerOffer(World& w, Rng& rng) {
    std::vector<TownId> towns;
    for (const auto [id, t] : w.towns)
        if (t.population >= 150) towns.push_back(id);
    if (towns.size() < 2) return std::nullopt;
    const TownId from = towns[rng.below(static_cast<u32>(towns.size()))];
    const Town& a = w.towns[from];
    const auto& links = people::linkedTowns(w, from);
    std::vector<TownId> choices;
    for (const TownId to : towns) {
        const Town& b = w.towns[to];
        const i64 d = chebyshev(a.centre.x - b.centre.x, a.centre.y - b.centre.y);
        if (to != from && d >= kMinDistance && d <= kMaxDistance &&
            std::ranges::none_of(links, [&](const TownLink& l) { return l.town == to; }))
            choices.push_back(to);
    }
    if (choices.empty()) return std::nullopt;
    Subsidy s;
    s.fromTown = from;
    s.toTown = choices[rng.below(static_cast<u32>(choices.size()))];
    return s;
}

// An industry's produce, nobody carries it yet, to an industry or a town that takes it.
std::optional<Subsidy> cargoOffer(const World& w, Rng& rng) {
    std::vector<const Industry*> producers;
    for (const Industry& i : w.industries) {
        const Building* b = resolve(w, i.building);
        const IndustryRecipe* r = b ? recipeFor(b->type) : nullptr;
        if (r && i.producedLastMonth > 0 && i.exportedLastMonth == 0) producers.push_back(&i);
    }
    if (producers.empty()) return std::nullopt;
    const Industry& source = *producers[rng.below(static_cast<u32>(producers.size()))];
    const Building& from = *resolve(w, source.building);
    const CargoType type = recipeFor(from.type)->output;
    std::vector<Subsidy> choices;
    for (const Industry& i : w.industries) {
        const Building* to = resolve(w, i.building);
        if (!to || i.building == source.building || !cargo::accepts(w, *to, type)) continue;
        const i64 d = chebyshev(from.pos.x - to->pos.x, from.pos.y - to->pos.y);
        if (d < kMinDistance || d > kMaxDistance) continue;
        Subsidy s;
        s.cargo = type;
        s.source = source.building;
        const IndustryRecipe* r = recipeFor(to->type);
        if (r && (r->inputs[0] == type || r->inputs[1] == type)) s.destination = i.building;
        else if (w.towns.contains(to->town)) s.toTown = to->town;
        else continue;
        if (std::ranges::none_of(choices, [&](const Subsidy& c) { return sameRoute(c, s); })) choices.push_back(s);
    }
    if (choices.empty()) return std::nullopt;
    return choices[rng.below(static_cast<u32>(choices.size()))];
}

// Pays `pay` or a multiple of it on a subsidised route, awarding an offer to the first company.
template <class Matches>
Money apply(World& w, CompanyId company, Money pay, Matches matches) {
    if (!w.companies.contains(company)) return pay;
    for (Subsidy& s : w.subsidies) {
        if (!matches(s)) continue;
        if (!s.awarded()) {
            s.awardedTo = company;
            s.untilTick = w.clock.tick + kSubsidyAwardDays * kTicksPerDay;
            w.addNews(NewsKind::Finance,
                      std::format("Subsidy awarded to {}: carrying {} pays double until {}", w.companies[company].name,
                                  describe(w, s), date(w, s.untilTick)),
                      destinationTile(w, s));
        }
        if (s.awardedTo == company) return pay * kSubsidyMultiplier;
    }
    return pay;
}

} // namespace

void daily(World& w) {
    const i64 now = w.clock.tick;
    std::erase_if(w.subsidies, [&](const Subsidy& s) {
        const bool gone = s.cargo == CargoType::None
                              ? !w.towns.contains(s.fromTown) || !w.towns.contains(s.toTown)
                              : !resolve(w, s.source) || (s.destination.valid() ? !resolve(w, s.destination) : !w.towns.contains(s.toTown));
        if (gone) return true;
        if (now < s.untilTick) return false;
        if (s.awarded()) {
            if (const Company* c = w.companies.tryGet(s.awardedTo))
                w.addNews(NewsKind::Finance, std::format("{}'s subsidy has run out: {}", c->name, describe(w, s)));
        } else {
            w.addNews(NewsKind::Finance, std::format("Subsidy offer withdrawn: {}", describe(w, s)));
        }
        return true;
    });
    const usize offers = static_cast<usize>(std::ranges::count_if(w.subsidies, [](const Subsidy& s) { return !s.awarded(); }));
    if (offers >= kMaxSubsidyOffers) return;
    // About one new offer every twelve days. Drawn from the seed and the day, so the world's own
    // random numbers are left alone.
    Rng rng(hashCombine(w.settings.seed, static_cast<u64>(w.clock.day())), 0x5AB5);
    if (rng.below(12) != 0) return;
    for (int attempt = 0; attempt < 6; ++attempt) {
        std::optional<Subsidy> s = rng.below(2) == 0 ? passengerOffer(w, rng) : cargoOffer(w, rng);
        if (!s || std::ranges::any_of(w.subsidies, [&](const Subsidy& o) { return sameRoute(o, *s); })) continue;
        s->offeredTick = now;
        s->untilTick = now + kSubsidyOfferDays * kTicksPerDay;
        w.subsidies.push_back(*s);
        w.addNews(NewsKind::Finance,
                  std::format("Subsidy offered: the first service carrying {} earns double pay for a year. "
                              "Offer open until {}.",
                              describe(w, *s), date(w, s->untilTick)),
                  sourceTile(w, *s));
        return;
    }
}

Money passengerPay(World& w, CompanyId company, StationId from, StationId to, Money pay) {
    const Station* a = w.stations.tryGet(from);
    const Station* b = w.stations.tryGet(to);
    if (!a || !b || a->town == b->town) return pay;
    return apply(w, company, pay, [&](const Subsidy& s) {
        return s.cargo == CargoType::None && s.fromTown == a->town && s.toTown == b->town;
    });
}

Money cargoPay(World& w, CompanyId company, const CargoBatch& batch, const Building& receiver, Money pay) {
    return apply(w, company, pay, [&](const Subsidy& s) {
        return s.cargo == batch.type && s.source == batch.producer &&
               (s.destination.valid() ? s.destination == batch.receiver : receiver.town == s.toTown);
    });
}

std::string describe(const World& w, const Subsidy& s) {
    if (s.cargo == CargoType::None)
        return std::format("passengers from {} to {}", townName(w, s.fromTown), townName(w, s.toTown));
    std::string cargo(cargoName(s.cargo));
    if (!cargo.empty()) cargo[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(cargo[0])));
    return std::format("{} from {} to {}", cargo, place(w, s.source),
                       s.destination.valid() ? place(w, s.destination) : townName(w, s.toTown));
}

TilePos sourceTile(const World& w, const Subsidy& s) {
    if (s.cargo != CargoType::None)
        if (const Building* b = resolve(w, s.source)) return b->pos;
    const Town* t = w.towns.tryGet(s.fromTown);
    return t ? t->centre : TilePos{};
}

TilePos destinationTile(const World& w, const Subsidy& s) {
    if (s.destination.valid())
        if (const Building* b = resolve(w, s.destination)) return b->pos;
    const Town* t = w.towns.tryGet(s.toTown);
    return t ? t->centre : TilePos{};
}

} // namespace tp::subsidies
