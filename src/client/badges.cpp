#include "client/badges.hpp"

#include "client/ui/theme.hpp"

namespace tp {

Badge cargoBadge(CargoType type) {
    using ui::Icon;
    namespace col = ui::col;
    switch (type) {
    case CargoType::Grain: return {Icon::Wheat, col::kAccent};
    case CargoType::Food: return {Icon::Bread, IM_COL32(232, 172, 112, 255)};
    case CargoType::Goods: return {Icon::Factory, col::kBlue};
    case CargoType::Wool: return {Icon::Sheep, col::kLeaf};
    case CargoType::Milk: return {Icon::Milk, IM_COL32(214, 226, 240, 255)};
    case CargoType::Logs: return {Icon::Tree, col::kGreen};
    case CargoType::Stone: return {Icon::Stone, IM_COL32(206, 190, 160, 255)};
    case CargoType::IronOre: return {Icon::MineCart, col::kOrange};
    case CargoType::Coal: return {Icon::Pickaxe, col::kTextMuted};
    case CargoType::Planks: return {Icon::Saw, IM_COL32(222, 150, 90, 255)};
    case CargoType::Cheese: return {Icon::Cheese, IM_COL32(255, 214, 92, 255)};
    case CargoType::Cloth: return {Icon::Spool, col::kPurple};
    case CargoType::Bricks: return {Icon::Bricks, col::kRed};
    case CargoType::Steel: return {Icon::Anvil, IM_COL32(140, 180, 230, 255)};
    case CargoType::Furniture: return {Icon::Chair, IM_COL32(226, 130, 200, 255)};
    case CargoType::Fish: return {Icon::Fish, col::kTeal};
    default: return {Icon::Cart, col::kAccent};
    }
}

Badge industryBadge(u16 buildingType) {
    if (const IndustryRecipe* r = recipeFor(buildingType)) return cargoBadge(r->output);
    return {ui::Icon::Cart, ui::col::kAccent};
}

Badge townBadge(TownClass cls) {
    switch (cls) {
    case TownClass::City: return {ui::Icon::Town, ui::col::kAccent};
    case TownClass::Town: return {ui::Icon::Town, ui::col::kBlue};
    default: return {ui::Icon::Home, ui::col::kLeaf};
    }
}

} // namespace tp
