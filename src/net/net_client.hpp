#pragma once
// Network client session: downloads the world from a server, then runs the same deterministic
// simulation locally, executing the server's command stream and never running ahead of the frames
// the server has authorised. Ticks are played back at a steady pace slightly behind the newest one.

#include "net/protocol.hpp"
#include "net/session.hpp"

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace tp::net {

class NetClientSession final : public GameSession {
public:
    enum class Phase { Connecting, Handshake, Downloading, Playing, Failed };

    // joinCompany: a company to join straight away, kJoinNewCompany, kJoinSpectator, or kJoinAsk to
    // let the player choose once the map has loaded (see joinCompany()).
    NetClientSession(std::string host, u16 port, std::string playerName, u8 joinCompany = kJoinNewCompany,
                     std::string serverPassword = {});

    World& world() override { return *world_; }
    CompanyId localCompany() const override { return company_; }
    u32 submit(CommandPayload payload) override;
    void update(double dtSeconds) override;
    double tickFraction() const override;
    int speed() const override { return speed_; }
    void setSpeed(int) override {} // the server decides
    bool canControlSpeed() const override { return false; }
    bool isMultiplayer() const override { return true; }
    bool ready() const override { return phase_ == Phase::Playing && world_ != nullptr; }
    std::vector<CommandFeedback> takeFeedback() override { return std::exchange(feedback_, {}); }
    std::string status() const override;

    Phase phase() const { return phase_; }
    const std::string& error() const { return error_; }
    float downloadProgress() const;
    u16 clientId() const { return clientId_; }
    i64 framesBehind() const { return frameMax_ - (world_ ? world_->clock.tick : 0); }
    // How far (seconds) playback stays behind the newest authorised tick; it grows while ticks
    // arrive unevenly and shrinks again once they are steady.
    double playbackDelay() const;
    const std::vector<ClientInfo>& clients() const { return clients_; }
    void sendChat(const std::string& text, ChatScope scope = ChatScope::All, u16 target = 0);
    std::vector<ChatBroadcastMsg> takeChat() { return std::exchange(chat_, {}); }

    // The company lobby: join a company (co-op), found a new one with a name and colour, or watch.
    void joinCompany(u8 company, const std::string& name = {}, u8 colour = 0xFF, const std::string& password = {});
    // Waiting for the player to choose a company (joinCompany() not called yet, or refused).
    bool choosingCompany() const { return ready() && choosing_; }
    bool joinPending() const { return joinPending_; }
    // Why the last join was refused, once.
    std::vector<std::string> takeNotices() { return std::exchange(notices_, {}); }
    void setCompanyPassword(const std::string& password);
    const std::vector<CompanyId>& lockedCompanies() const { return locked_; }
    // Where this player points (sent ten times a second when it moves), and every other player's pointer.
    void setCursor(const std::optional<CursorInfo>& cursor);
    const std::vector<CursorInfo>& cursors() const { return cursors_; }

private:
    void handle(const Packet& p);
    void fail(const std::string& why);
    void runTick();

    std::string host_;
    u16 port_;
    std::string name_;
    u8 joinCompany_;
    std::string password_;
    bool choosing_ = false;
    bool joinPending_ = false;
    std::vector<std::string> notices_;
    std::vector<CompanyId> locked_;
    std::vector<CursorInfo> cursors_;
    std::optional<CursorInfo> cursor_, sentCursor_;
    double cursorTimer_ = 0.0;
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

    // Playback: ticks arrive in bursts (the server's frames, network jitter, a busy host) but are
    // shown at a steady pace a little behind the newest one, so motion never stalls or jumps.
    void advancePlayback(double dtSeconds);
    double fraction_ = 0.0;   // how far into the next tick the picture is
    double sinceFrame_ = 0.0; // seconds since the server last authorised a new tick
    double peakGap_ = 0.0;    // longest recent wait for a new tick, forgotten slowly
    i64 lastFrameMax_ = 0;
    // Player list and company changes, held until playback reaches the tick they arrived after.
    using Deferred = std::variant<ClientListMsg, CompanyAssignedMsg>;
    void defer(Deferred msg);
    void applyDeferred();
    std::deque<std::pair<i64, Deferred>> deferred_;

    std::vector<CommandFeedback> feedback_;
    std::vector<ClientInfo> clients_;
    std::vector<ChatBroadcastMsg> chat_;
};

} // namespace tp::net
