#include "net/server.hpp"

#include "core/calendar.hpp"

#include <algorithm>
#include <chrono>

namespace tp {

Server::Server(std::unique_ptr<World> world) : world_(std::move(world)) {}

void Server::queueCommand(Command cmd) { queue_.push_back(std::move(cmd)); }

CommandResult Server::executeNow(const Command& cmd) {
    CommandResult r = executeCommand(*world_, cmd);
    results_.push_back({world_->clock.tick, cmd, r});
    return r;
}

void Server::setSpeed(int speed) {
    speed_ = std::clamp(speed, 0, 16);
    if (speed_ == 0) accumulator_ = 0.0;
}

void Server::step() {
    const i64 tick = world_->clock.tick + 1;
    std::vector<Command> commands;
    commands.swap(queue_);
    if (onTick) onTick(tick, commands);
    for (const Command& cmd : commands) {
        CommandResult r = executeCommand(*world_, cmd);
        results_.push_back({tick, cmd, std::move(r)});
    }
    world_->tick();
    if (onTickDone) onTickDone(tick);
}

int Server::update(double dtSeconds) {
    if (speed_ == 0) return 0;
    const double interval = 1.0 / (kTicksPerSecond * speed_);
    accumulator_ += dtSeconds;
    // Do not try to catch up more than a few frames worth of ticks (avoids a spiral of death).
    accumulator_ = std::min(accumulator_, interval * 8.0 + 0.1);
    const auto start = std::chrono::steady_clock::now();
    int ticks = 0;
    while (accumulator_ >= interval) {
        accumulator_ -= interval;
        step();
        ++ticks;
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(40)) {
            accumulator_ = std::min(accumulator_, interval);
            break;
        }
    }
    return ticks;
}

double Server::tickFraction() const {
    if (speed_ == 0) return 1.0;
    const double interval = 1.0 / (kTicksPerSecond * speed_);
    return std::clamp(accumulator_ / interval, 0.0, 1.0);
}

std::vector<CommandRecord> Server::takeResults(u16 client) {
    std::vector<CommandRecord> out;
    auto it = std::stable_partition(results_.begin(), results_.end(),
                                    [&](const CommandRecord& r) { return r.command.client != client; });
    out.assign(std::make_move_iterator(it), std::make_move_iterator(results_.end()));
    results_.erase(it, results_.end());
    // Results for clients nobody collects must not pile up forever.
    if (results_.size() > 4096) results_.erase(results_.begin(), results_.begin() + 2048);
    return out;
}

} // namespace tp
