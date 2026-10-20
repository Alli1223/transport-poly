#include "sim/commands.hpp"

#include "sim/command_handlers.hpp"
#include "sim/world.hpp"

#include <algorithm>
#include <format>

namespace tp {

namespace {

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

CommandResult run(World& world, const Command& cmd, bool exec) {
    if (const Company* co = world.companies.tryGet(cmd.company); co && co->bankrupt &&
        !std::holds_alternative<CmdActivateChunks>(cmd.payload) && !std::holds_alternative<CmdCreateCompany>(cmd.payload))
        return CommandResult::failure("This company is bankrupt");
    auto result = std::visit([&](const auto& payload) { return cmd::handle(world, cmd, payload, exec); }, cmd.payload);
    if (result.ok && result.cost > 0 && (result.category == Expense::Construction || result.category == Expense::NewVehicles))
        result.cost = world.inflated(result.cost);
    return result;
}

} // namespace

CommandResult testCommand(World& world, const Command& cmd) { return run(world, cmd, false); }

CommandResult executeCommand(World& world, const Command& cmd) {
    CommandResult test = run(world, cmd, false);
    if (!test.ok) return test;
    Company* company = world.companies.tryGet(cmd.company);
    if (test.cost > 0 && company && company->money < test.cost)
        return CommandResult::failure(std::format("Not enough money - {} required", formatMoney(test.cost)));
    CommandResult result = run(world, cmd, true);
    if (result.ok && company && result.cost != 0) {
        // The company may have been created by this very command; re-fetch the pointer.
        if (Company* co = world.companies.tryGet(cmd.company)) {
            if (result.cost > 0)
                co->spend(result.category, result.cost);
            else
                co->earn(result.category, -result.cost);
        }
    }
    if (result.ok && result.cost > 0 && result.category == Expense::Construction) walking::invalidate(world);
    return result;
}

std::string_view commandName(const CommandPayload& payload) {
    return std::visit(Overloaded{
                          [](const CmdDemolishBuilding&) { return std::string_view("Demolish building"); },
                          [](const CmdActivateChunks&) { return std::string_view("Explore"); },
                          [](const CmdCreateCompany&) { return std::string_view("Create company"); },
                          [](const CmdSetLoan&) { return std::string_view("Change loan"); },
                          [](const CmdTerraform&) { return std::string_view("Terraform"); },
                          [](const CmdTerraformBrush&) { return std::string_view("Terraform"); },
                          [](const CmdBuildTrack&) { return std::string_view("Build track"); },
                          [](const CmdRemoveTrack&) { return std::string_view("Remove track"); },
                          [](const CmdElectrifyTrack& c) { return std::string_view(c.remove ? "Remove overhead wires" : "Electrify track"); },
                          [](const CmdBuildTrackStation&) { return std::string_view("Build station"); },
                          [](const CmdBuildTrackDepot&) { return std::string_view("Build train depot"); },
                          [](const CmdTrackSignal&) { return std::string_view("Signal"); },
                          [](const CmdClearArea&) { return std::string_view("Clear area"); },
                          [](const CmdPlantTrees&) { return std::string_view("Plant trees"); },
                          [](const CmdBuildRoad& c) { return std::string_view(c.tram ? "Build tram track" : "Build road"); },
                          [](const CmdRemoveRoad&) { return std::string_view("Remove road"); },
                          [](const CmdBuildRoadStop& c) { return std::string_view(c.tram ? "Build tram stop" : "Build bus stop"); },
                          [](const CmdBuildRoadDepot&) { return std::string_view("Build depot"); },
                          [](const CmdBuyVehicle&) { return std::string_view("Buy vehicle"); },
                          [](const CmdSellVehicle&) { return std::string_view("Sell vehicle"); },
                          [](const CmdStartStopVehicle&) { return std::string_view("Start/stop vehicle"); },
                          [](const CmdSendToDepot&) { return std::string_view("Send to depot"); },
                          [](const CmdSetOrders&) { return std::string_view("Change orders"); },
                          [](const CmdSkipOrder&) { return std::string_view("Skip order"); },
                          [](const CmdBuildRail&) { return std::string_view("Build railway"); },
                          [](const CmdRemoveRail&) { return std::string_view("Remove railway"); },
                          [](const CmdCycleSignal&) { return std::string_view("Signal"); },
                          [](const CmdBuildRailStation&) { return std::string_view("Build railway station"); },
                          [](const CmdBuildRailDepot&) { return std::string_view("Build train depot"); },
                          [](const CmdStationEntrance&) { return std::string_view("Station entrance"); },
                          [](const CmdAddPlatform&) { return std::string_view("Add platform"); },
                          [](const CmdExtendPlatform&) { return std::string_view("Extend platform"); },
                          [](const CmdReorderConsist&) { return std::string_view("Reorder train"); },
                          [](const CmdAddCarriage&) { return std::string_view("Add carriage"); },
                          [](const CmdRemoveCarriage&) { return std::string_view("Remove carriage"); },
                          [](const CmdBuildDock&) { return std::string_view("Build dock"); },
                          [](const CmdBuildShipDepot&) { return std::string_view("Build shipyard"); },
                          [](const CmdCloneVehicle&) { return std::string_view("Clone vehicle"); },
                          [](const CmdUnshareOrders&) { return std::string_view("Unshare orders"); },
                          [](const CmdEditWater& c) { return std::string_view(c.water ? "Build canal" : "Reclaim water"); },
                          [](const CmdBuildSpan& c) { return std::string_view(c.tunnel ? "Build tunnel" : "Build bridge"); },
                          [](const CmdRemoveSpan&) { return std::string_view("Remove bridge / tunnel"); },
                          [](const CmdUpdateCompany&) { return std::string_view("Company details"); },
                      },
                      payload);
}

namespace cmd {

Rect normalise(TilePos a, TilePos b) {
    return Rect{std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}

} // namespace cmd

} // namespace tp
