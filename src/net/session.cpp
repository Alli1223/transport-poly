#include "net/session.hpp"

namespace tp {

LocalSession::LocalSession(const WorldSettings& settings, const std::string& companyName, u8 colour)
    : server_(std::make_unique<World>(settings)) {
    Command create;
    create.client = kLocalClientId;
    create.payload = CmdCreateCompany{companyName, colour};
    const CommandResult r = server_.executeNow(create);
    company_ = CompanyId(static_cast<u8>(r.createdId));
    (void)server_.takeResults(kLocalClientId);
}

LocalSession::LocalSession(std::unique_ptr<World> world, CompanyId company)
    : server_(std::move(world)), company_(company) {}

u32 LocalSession::submit(CommandPayload payload) {
    Command cmd;
    cmd.company = company_;
    cmd.client = kLocalClientId;
    cmd.seq = nextSeq_++;
    cmd.payload = std::move(payload);
    server_.queueCommand(std::move(cmd));
    return nextSeq_ - 1;
}

std::vector<CommandFeedback> LocalSession::takeFeedback() {
    std::vector<CommandFeedback> out;
    for (CommandRecord& r : server_.takeResults(kLocalClientId))
        out.push_back({r.command.seq, std::string(commandName(r.command.payload)), std::move(r.result)});
    return out;
}

} // namespace tp
