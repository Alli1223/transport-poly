#include "net/net_client.hpp"

#include "core/log.hpp"
#include "core/version.hpp"
#include "sim/savegame.hpp"

#include <algorithm>
#include <chrono>
#include <format>

namespace tp::net {

NetClientSession::NetClientSession(std::string host, u16 port, std::string playerName, u8 joinCompany)
    : host_(std::move(host)), port_(port), name_(std::move(playerName)), joinCompany_(joinCompany) {
    std::string err;
    connecting_ = Socket::beginConnect(host_, port_, err);
    if (!connecting_) fail(err);
}

void NetClientSession::fail(const std::string& why) {
    if (phase_ == Phase::Failed) return;
    phase_ = Phase::Failed;
    error_ = why;
    log::warn("Network: {}", why);
    conn_.close(why);
}

std::string NetClientSession::status() const {
    switch (phase_) {
    case Phase::Connecting: return std::format("Connecting to {}:{}...", host_, port_);
    case Phase::Handshake: return "Joining...";
    case Phase::Downloading: return std::format("Downloading map {:.0f}%", downloadProgress() * 100.0f);
    case Phase::Playing: return std::format("Connected to {}", serverName_);
    case Phase::Failed: return "Disconnected: " + error_;
    }
    return {};
}

float NetClientSession::downloadProgress() const {
    return mapSize_ ? static_cast<float>(mapData_.size()) / static_cast<float>(mapSize_) : 0.0f;
}

u32 NetClientSession::submit(CommandPayload payload) {
    if (phase_ != Phase::Playing) return 0;
    const u32 seq = nextSeq_++;
    conn_.send(PacketType::CommandRequest, CommandRequestMsg{seq, std::move(payload)});
    return seq;
}

void NetClientSession::sendChat(const std::string& text) {
    if (phase_ == Phase::Playing) conn_.send(PacketType::Chat, ChatMsg{text});
}

void NetClientSession::handle(const Packet& p) {
    switch (p.type) {
    case PacketType::Welcome: {
        const auto msg = p.decode<WelcomeMsg>();
        clientId_ = msg.clientId;
        serverName_ = msg.serverName;
        phase_ = Phase::Downloading;
        break;
    }
    case PacketType::Reject: fail(p.decode<RejectMsg>().reason); break;
    case PacketType::MapBegin: {
        const auto msg = p.decode<MapBeginMsg>();
        mapSize_ = msg.size;
        mapData_.clear();
        mapData_.reserve(static_cast<usize>(std::min<u64>(msg.size, 512u << 20)));
        break;
    }
    case PacketType::MapData: mapData_.insert(mapData_.end(), p.payload.begin(), p.payload.end()); break;
    case PacketType::MapEnd: {
        CompanyId ignored;
        world_ = restoreWorld(mapData_, ignored);
        mapData_.clear();
        mapData_.shrink_to_fit();
        frameMax_ = world_->clock.tick;
        phase_ = Phase::Playing;
        conn_.send(PacketType::JoinCompany, JoinCompanyMsg{joinCompany_});
        log::info("Joined {} at tick {}", serverName_, world_->clock.tick);
        break;
    }
    case PacketType::Commands: {
        auto msg = p.decode<CommandsMsg>();
        auto& list = pending_[msg.tick];
        for (Command& c : msg.commands) list.push_back(std::move(c));
        break;
    }
    case PacketType::Frame: {
        const auto msg = p.decode<FrameMsg>();
        frameMax_ = std::max(frameMax_, msg.tick);
        speed_ = msg.speed;
        if (msg.hash) hashes_[msg.tick] = msg.hash;
        break;
    }
    case PacketType::ChatBroadcast: chat_.push_back(p.decode<ChatBroadcastMsg>()); break;
    case PacketType::Speed: speed_ = p.decode<SpeedMsg>().speed; break;
    case PacketType::ClientList: clients_ = p.decode<ClientListMsg>().clients; break;
    case PacketType::CompanyAssigned: company_ = p.decode<CompanyAssignedMsg>().company; break;
    default: fail("unexpected packet from server"); break;
    }
}

void NetClientSession::runTick() {
    World& w = *world_;
    const i64 tick = w.clock.tick + 1;
    if (auto it = pending_.find(tick); it != pending_.end()) {
        for (const Command& cmd : it->second) {
            CommandResult r = executeCommand(w, cmd);
            if (cmd.client == clientId_ && cmd.seq != 0)
                feedback_.push_back({cmd.seq, std::string(commandName(cmd.payload)), std::move(r)});
        }
        pending_.erase(it);
    }
    w.tick();
    if (auto it = hashes_.find(tick); it != hashes_.end()) {
        const u64 mine = w.stateHash();
        if (mine != it->second) {
            fail(std::format("Desync detected at tick {} (server {:016x}, client {:016x})", tick, it->second, mine));
            return;
        }
        hashes_.erase(it);
    }
}

void NetClientSession::update(double dtSeconds) {
    if (phase_ == Phase::Failed) return;
    if (phase_ == Phase::Connecting) {
        connectTimer_ += dtSeconds;
        std::string err;
        const int r = connecting_->finishConnect(err);
        if (r < 0) {
            fail(err);
            return;
        }
        if (r == 0) {
            if (connectTimer_ > 10.0) fail("connection timed out");
            return;
        }
        conn_ = Connection(std::move(*connecting_));
        connecting_.reset();
        conn_.send(PacketType::Hello, HelloMsg{kProtocolVersion, name_});
        phase_ = Phase::Handshake;
    }
    if (!conn_.pump()) {
        fail(conn_.error());
        return;
    }
    try {
        while (auto p = conn_.next()) {
            handle(*p);
            if (phase_ == Phase::Failed) return;
        }
    } catch (const SerializeError& e) {
        fail(std::string("malformed data from server: ") + e.what());
        return;
    }
    if (phase_ != Phase::Playing) return;

    // Simulate every tick the server has authorised; when far behind (just joined) catch up within
    // a time budget per frame.
    const auto start = std::chrono::steady_clock::now();
    int ran = 0;
    while (world_->clock.tick < frameMax_ && phase_ == Phase::Playing) {
        runTick();
        ++ran;
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(25)) break;
    }
    if (ran > 0)
        sinceTick_ = 0.0;
    else
        sinceTick_ += dtSeconds;
    conn_.pump();
}

double NetClientSession::tickFraction() const {
    if (speed_ == 0) return 1.0;
    const double interval = 1.0 / (kTicksPerSecond * std::max(1, speed_));
    return std::clamp(sinceTick_ / interval, 0.0, 1.0);
}

} // namespace tp::net
