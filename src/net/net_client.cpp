#include "net/net_client.hpp"

#include "core/log.hpp"
#include "core/version.hpp"
#include "sim/savegame.hpp"

#include <algorithm>
#include <chrono>
#include <format>

namespace tp::net {

NetClientSession::NetClientSession(std::string host, u16 port, std::string playerName, u8 joinCompany,
                                   std::string serverPassword)
    : host_(std::move(host)), port_(port), name_(std::move(playerName)), joinCompany_(joinCompany),
      password_(std::move(serverPassword)) {
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

void NetClientSession::sendChat(const std::string& text, ChatScope scope, u16 target) {
    if (phase_ == Phase::Playing) conn_.send(PacketType::Chat, ChatMsg{text, scope, target});
}

void NetClientSession::joinCompany(u8 company, const std::string& name, u8 colour, const std::string& password) {
    if (phase_ != Phase::Playing) return;
    JoinCompanyMsg msg;
    msg.company = company;
    msg.name = name;
    msg.colour = colour;
    msg.password = password;
    conn_.send(PacketType::JoinCompany, msg);
    joinPending_ = true;
}

void NetClientSession::setCompanyPassword(const std::string& password) {
    if (phase_ == Phase::Playing) conn_.send(PacketType::SetCompanyPassword, SetCompanyPasswordMsg{password});
}

void NetClientSession::setCursor(const std::optional<CursorInfo>& cursor) { cursor_ = cursor; }

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
        if (joinCompany_ == kJoinAsk) {
            choosing_ = true;
        } else {
            JoinCompanyMsg join;
            join.company = joinCompany_;
            conn_.send(PacketType::JoinCompany, join);
        }
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
    case PacketType::ClientList: {
        auto msg = p.decode<ClientListMsg>();
        clients_ = std::move(msg.clients);
        locked_ = std::move(msg.locked);
        break;
    }
    case PacketType::CompanyAssigned:
        company_ = p.decode<CompanyAssignedMsg>().company;
        choosing_ = false;
        joinPending_ = false;
        break;
    case PacketType::JoinRefused:
        notices_.push_back(p.decode<JoinRefusedMsg>().reason);
        joinPending_ = false;
        break;
    case PacketType::Cursors: cursors_ = p.decode<CursorsMsg>().cursors; break;
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
        conn_.send(PacketType::Hello, HelloMsg{kProtocolVersion, name_, password_});
        phase_ = Phase::Handshake;
    }
    // Read everything that arrived before acting on a closed connection: a server that refuses or
    // removes a player says why just before it hangs up.
    const bool alive = conn_.pump();
    try {
        while (auto p = conn_.next()) {
            handle(*p);
            if (phase_ == Phase::Failed) return;
        }
    } catch (const SerializeError& e) {
        fail(std::string("malformed data from server: ") + e.what());
        return;
    }
    if (!alive) {
        fail(conn_.error());
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
    // The pointer, at most ten times a second and only when it has moved.
    cursorTimer_ += dtSeconds;
    const auto same = [](const std::optional<CursorInfo>& a, const std::optional<CursorInfo>& b) {
        return a.has_value() == b.has_value() && (!a || (a->x == b->x && a->y == b->y && a->z == b->z));
    };
    if (cursorTimer_ >= 0.1 && !same(cursor_, sentCursor_)) {
        cursorTimer_ = 0.0;
        sentCursor_ = cursor_;
        CursorMsg msg;
        msg.visible = cursor_.has_value();
        if (cursor_) {
            msg.x = cursor_->x;
            msg.y = cursor_->y;
            msg.z = cursor_->z;
        }
        conn_.send(PacketType::Cursor, msg);
    }
    conn_.pump();
}

double NetClientSession::tickFraction() const {
    if (speed_ == 0) return 1.0;
    const double interval = 1.0 / (kTicksPerSecond * std::max(1, speed_));
    return std::clamp(sinceTick_ / interval, 0.0, 1.0);
}

} // namespace tp::net
