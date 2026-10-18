#include "net/host_session.hpp"

#include <format>

namespace tp::net {

HostSession::HostSession(std::unique_ptr<World> world, CompanyId company, NetServer::Options options)
    : net_(std::move(world), std::move(options)), company_(company) {
    net_.setLocalHost(company);
}

std::vector<CommandFeedback> HostSession::takeFeedback() {
    std::vector<CommandFeedback> out;
    for (CommandRecord& r : net_.server().takeResults(kLocalClientId))
        out.push_back({r.command.seq, std::string(commandName(r.command.payload)), std::move(r.result)});
    return out;
}

std::string HostSession::status() const {
    return std::format("Hosting on port {} - {} player(s) connected", net_.options().port,
                       const_cast<NetServer&>(net_).clients().size());
}

} // namespace tp::net
