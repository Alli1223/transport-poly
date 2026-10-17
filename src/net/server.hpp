#pragma once
// The authoritative game host. It owns the world, schedules incoming commands for the next tick
// and advances the simulation. Single player runs exactly this code without any sockets.

#include "sim/commands.hpp"
#include "sim/world.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace tp {

inline constexpr u16 kServerClientId = 0;
inline constexpr u16 kLocalClientId = 1;

struct CommandRecord {
    i64 tick = 0;
    Command command;
    CommandResult result;
};

class Server {
public:
    explicit Server(std::unique_ptr<World> world);

    World& world() { return *world_; }
    const World& world() const { return *world_; }

    // Queues a command; it executes at the start of the next tick.
    void queueCommand(Command cmd);
    // Executes a command outside the tick loop. Only valid before any remote client has joined
    // (e.g. creating the host's company when a game starts).
    CommandResult executeNow(const Command& cmd);

    // Advances game time according to the speed setting; returns the number of ticks run.
    int update(double dtSeconds);
    // Runs exactly one tick: scheduled commands first, then the simulation.
    void step();

    double tickFraction() const;
    int speed() const { return speed_; }
    void setSpeed(int speed);

    // Results of commands issued by `client` since the last call.
    std::vector<CommandRecord> takeResults(u16 client);

    // Networking hooks: called for every tick with the commands executed in it (before the
    // tick), and after the tick finished.
    std::function<void(i64 tick, const std::vector<Command>&)> onTick;
    std::function<void(i64 tick)> onTickDone;

private:
    std::unique_ptr<World> world_;
    std::vector<Command> queue_;
    std::vector<CommandRecord> results_;
    double accumulator_ = 0.0;
    int speed_ = 1;
};

} // namespace tp
