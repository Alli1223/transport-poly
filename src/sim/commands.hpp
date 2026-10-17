#pragma once
// Commands are the only way to change the game state from outside the simulation. Clients send
// command requests to the server, which stamps them with the sender's company and schedules
// them for a tick; every peer then executes them identically.

#include "core/money.hpp"
#include "core/types.hpp"
#include "sim/company.hpp"
#include "sim/coords.hpp"
#include "sim/ids.hpp"

#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace tp {

class World;

// --- Command payloads ----------------------------------------------------------------------

// Explore: make chunks part of the simulated world (spawns their towns).
struct CmdActivateChunks {
    std::vector<ChunkPos> chunks;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(chunks);
    }
};

// Found a new company. colour 0xFF picks the first unused colour.
struct CmdCreateCompany {
    std::string name;
    u8 colour = 0xFF;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(name, colour);
    }
};

// Borrow (positive) or repay (negative) in steps of prices::kLoanStep.
struct CmdSetLoan {
    Money delta = 0;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(delta);
    }
};

// Raise (+1) or lower (-1) a terrain corner.
struct CmdTerraform {
    TilePos corner;
    i8 delta = 1;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(corner, delta);
    }
};

// Bulldoze a rectangular area (inclusive corners).
struct CmdClearArea {
    TilePos a;
    TilePos b;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(a, b);
    }
};

struct CmdPlantTrees {
    TilePos a;
    TilePos b;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(a, b);
    }
};

using CommandPayload =
    std::variant<CmdActivateChunks, CmdCreateCompany, CmdSetLoan, CmdTerraform, CmdClearArea, CmdPlantTrees>;

struct Command {
    CompanyId company;  // executing company, stamped by the server (invalid = server / spectator)
    u16 client = 0;     // issuing client id (0 = server itself)
    u32 seq = 0;        // client side sequence number, echoed in results
    CommandPayload payload;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(company, client, seq, payload);
    }
};

struct CommandResult {
    bool ok = false;
    Money cost = 0;                          // positive = expense, negative = income
    Expense category = Expense::Construction; // ledger category the cost is booked to
    std::string error;
    u32 createdId = 0xFFFFFFFFu; // id of an object created by the command, if any

    static CommandResult success(Money cost = 0, Expense category = Expense::Construction) {
        CommandResult r;
        r.ok = true;
        r.cost = cost;
        r.category = category;
        return r;
    }
    static CommandResult failure(std::string message) {
        CommandResult r;
        r.error = std::move(message);
        return r;
    }
};

// Validates the command without side effects and returns its cost.
CommandResult testCommand(World& world, const Command& cmd);
// Validates, checks the company can pay, applies the command and books its cost.
CommandResult executeCommand(World& world, const Command& cmd);

std::string_view commandName(const CommandPayload& payload);

} // namespace tp
