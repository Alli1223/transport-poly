#pragma once
// Strongly typed integer identifiers so that e.g. a TownId cannot be passed as a VehicleId.

#include "core/types.hpp"

#include <compare>
#include <format>
#include <functional>
#include <limits>

namespace tp {

template <class Tag, class Rep = u32>
struct StrongId {
    using RepType = Rep;
    static constexpr Rep kInvalidValue = std::numeric_limits<Rep>::max();

    Rep value = kInvalidValue;

    constexpr StrongId() = default;
    constexpr explicit StrongId(Rep v) : value(v) {}

    static constexpr StrongId invalid() { return StrongId{}; }
    constexpr bool valid() const { return value != kInvalidValue; }

    friend constexpr auto operator<=>(StrongId, StrongId) = default;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(value);
    }
};

} // namespace tp

template <class Tag, class Rep>
struct std::hash<tp::StrongId<Tag, Rep>> {
    std::size_t operator()(tp::StrongId<Tag, Rep> id) const noexcept { return std::hash<Rep>{}(id.value); }
};

template <class Tag, class Rep>
struct std::formatter<tp::StrongId<Tag, Rep>> : std::formatter<Rep> {
    auto format(tp::StrongId<Tag, Rep> id, std::format_context& ctx) const {
        if (!id.valid()) return std::format_to(ctx.out(), "invalid");
        return std::formatter<Rep>::format(id.value, ctx);
    }
};
