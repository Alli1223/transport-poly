#pragma once
// Network client session: downloads the world from a server, then runs the same deterministic
// simulation locally, executing the server's command stream and never running ahead of the frames
// the server has authorised.

#include "net/protocol.hpp"
#include "net/session.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace tp::net {

class NetClientSession final : public GameSession {
public:
    enum class Phase { Connecting, Handshake, Downloading, Playing, Failed };

    NetClientSession(std::string host, u16 port, std::string playerName, u8 joinCompany = kJoinNewCompany);

    World& world() override { return *world_; }
    CompanyId localCompany() const override { return company_; }
    u32 submit(CommandPayload payload) override;
    void update(double dtSeconds) override;
    double tickFraction() const override;
    int speed() const override { return speed_; }
    void setSpeed(int) override {} // the server decides
    bool isMultiplayer() const override { return true; }
    bool ready() const override { return phase_ == Phase::Playing && world_ != nullptr; }
    std::vector<CommandFeedback> takeFeedback() override { return std::exchange(feedback_, {}); }
    std::string status() const override;

    Phase phase() const { return phase_; }
    const std::string& error() const { return error_; }
    float downloadProgress() const;
    u16 clientId() const { return clientId_; }
    i64 framesBehind() const { return frameMax_ - (world_ ? world_->clock.tick : 0); }
    const std::vector<ClientInfo>& clients() const { return clients_; }
    void sendChat(const std::string& text);
    std::vector<ChatBroadcastMsg> takeChat() { return std::exchange(chat_, {}); }

private:
    void handle(const Packet& p);
    void fail(const std::string& why);
    void runTick();

    std::string host_;
    u16 port_;
    std::string name_;
    u8 joinCompany_;
    Phase phase_ = Phase::Connecting;
    std::string error_;
    Connection conn_;
    std::optional<Socket> connecting_;
    double connectTimer_ = 0.0;

    u16 clientId_ = 0;
    std::string serverName_;
    std::vector<u8> mapData_;
    u64 mapSize_ = 0;
    std::unique_ptr<World> world_;
    CompanyId company_;
    u32 nextSeq_ = 1;
    int speed_ = 1;

    std::map<i64, std::vector<Command>> pending_; // tick -> commands executed at its start
    std::map<i64, u64> hashes_;                   // tick -> expected state hash after it
    i64 frameMax_ = 0;
    double sinceTick_ = 0.0;

    std::vector<CommandFeedback> feedback_;
    std::vector<ClientInfo> clients_;
    std::vector<ChatBroadcastMsg> chat_;
};

} // namespace tp::net
