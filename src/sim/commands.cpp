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
    return std::visit([&](const auto& payload) { return cmd::handle(world, cmd, payload, exec); }, cmd.payload);
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
    return result;
}

std::string_view commandName(const CommandPayload& payload) {
    return std::visit(Overloaded{
                          [](const CmdActivateChunks&) { return std::string_view("Explore"); },
                          [](const CmdCreateCompany&) { return std::string_view("Create company"); },
                          [](const CmdSetLoan&) { return std::string_view("Change loan"); },
                          [](const CmdTerraform&) { return std::string_view("Terraform"); },
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
                          [](const CmdAddCarriage&) { return std::string_view("Add carriage"); },
                          [](const CmdRemoveCarriage&) { return std::string_view("Remove carriage"); },
                      },
                      payload);
}

namespace cmd {

Rect normalise(TilePos a, TilePos b) {
    return Rect{std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}

} // namespace cmd

} // namespace tp
