#pragma once
// A local player hosting a multiplayer game: the authoritative server runs in this process and
// remote clients connect to it.

#include "net/net_server.hpp"
#include "net/session.hpp"

#include <memory>
#include <string>

namespace tp::net {

class HostSession final : public GameSession {
public:
    // `world` must already contain the host's company.
    HostSession(std::unique_ptr<World> world, CompanyId company, NetServer::Options options);

    bool start(std::string& error) { return net_.start(error); }

    World& world() override { return net_.server().world(); }
    CompanyId localCompany() const override { return company_; }
    u32 submit(CommandPayload payload) override { return net_.submitLocal(company_, std::move(payload)); }
    void update(double dtSeconds) override { net_.update(dtSeconds); }
    double tickFraction() const override { return const_cast<NetServer&>(net_).server().tickFraction(); }
    int speed() const override { return const_cast<NetServer&>(net_).server().speed(); }
    void setSpeed(int speed) override { net_.server().setSpeed(speed); }
    bool isMultiplayer() const override { return true; }
    std::vector<CommandFeedback> takeFeedback() override;
    std::string status() const override;

    NetServer& net() { return net_; }
    // The host player joins another company (co-op) or watches (an invalid id).
    void switchCompany(CompanyId company);
    // The host player founds a new company and moves to it once it exists.
    void foundCompany(const std::string& name, u8 colour);

private:
    NetServer net_;
    CompanyId company_;
};

} // namespace tp::net
