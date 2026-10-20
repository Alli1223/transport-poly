#pragma once
// A client's view of a running game, independent of whether the game is local or remote.

#include "net/server.hpp"
#include "sim/commands.hpp"
#include "sim/world.hpp"

#include <memory>
#include <string>
#include <vector>

namespace tp {

struct CommandFeedback {
    u32 seq = 0;
    std::string name;
    CommandResult result;
};

class GameSession {
public:
    virtual ~GameSession() = default;

    virtual World& world() = 0;
    virtual CompanyId localCompany() const = 0;
    // Requests a command on behalf of the local company; returns its sequence number.
    virtual u32 submit(CommandPayload payload) = 0;
    virtual void update(double dtSeconds) = 0;
    // Fraction of the next tick already elapsed, for smooth rendering between ticks.
    virtual double tickFraction() const = 0;
    virtual int speed() const = 0;
    virtual void setSpeed(int speed) = 0;
    virtual bool canControlSpeed() const { return true; }
    virtual bool isMultiplayer() const { return false; }
    // False while a network client is still connecting / downloading the map.
    virtual bool ready() const { return true; }
    virtual std::vector<CommandFeedback> takeFeedback() = 0;
    virtual std::string status() const { return {}; }
};

// Single player: an in-process server without networking.
class LocalSession final : public GameSession {
public:
    // Starts a new game and founds the player's company.
    explicit LocalSession(const WorldSettings& settings, const std::string& companyName = {}, u8 colour = 0xFF);
    // Continues a loaded game as the given company.
    LocalSession(std::unique_ptr<World> world, CompanyId company);

    World& world() override { return server_.world(); }
    CompanyId localCompany() const override { return company_; }
    u32 submit(CommandPayload payload) override;
    void update(double dtSeconds) override { server_.update(dtSeconds); }
    double tickFraction() const override { return server_.tickFraction(); }
    int speed() const override { return server_.speed(); }
    void setSpeed(int speed) override { server_.setSpeed(speed); }
    std::vector<CommandFeedback> takeFeedback() override;

    Server& server() { return server_; }

private:
    Server server_;
    CompanyId company_;
    u32 nextSeq_ = 1;
};

} // namespace tp
