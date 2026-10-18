#include "net/net_server.hpp"

#include "core/log.hpp"
#include "core/version.hpp"
#include "sim/savegame.hpp"

#include <algorithm>
#include <format>

namespace tp::net {

NetServer::NetServer(std::unique_ptr<World> world, Options options)
    : server_(std::move(world)), options_(std::move(options)) {
    server_.onTick = [this](i64 tick, const std::vector<Command>& commands) {
        if (commands.empty()) return;
        broadcastRaw(PacketType::Commands, toBytes(CommandsMsg{tick, commands}));
    };
    server_.onTickDone = [this](i64 tick) {
        FrameMsg f;
        f.tick = tick;
        f.speed = static_cast<u8>(server_.speed());
        if (options_.hashInterval > 0 && tick % options_.hashInterval == 0) f.hash = server_.world().stateHash();
        broadcastRaw(PacketType::Frame, toBytes(f));
    };
}

NetServer::~NetServer() { stop(); }

bool NetServer::start(std::string& error) {
    listener_ = Socket::listen(options_.port, error);
    if (!listener_) return false;
    options_.port = listener_->localPort();
    events_.push_back(std::format("Listening on port {}", options_.port));
    log::info("Server listening on port {}", options_.port);
    return true;
}

void NetServer::stop() {
    for (auto& c : clients_) c->conn.close("server shutting down");
    clients_.clear();
    listener_.reset();
}

u32 NetServer::submitLocal(CompanyId company, CommandPayload payload) {
    Command cmd;
    cmd.company = company;
    cmd.client = kLocalClientId;
    cmd.seq = nextLocalSeq_++;
    cmd.payload = std::move(payload);
    server_.queueCommand(std::move(cmd));
    return nextLocalSeq_ - 1;
}

std::vector<ClientInfo> NetServer::clients() const {
    std::vector<ClientInfo> out;
    if (localHost_) out.push_back(*localHost_);
    for (const auto& c : clients_)
        if (c->welcomed) out.push_back({c->id, c->name, c->company});
    return out;
}

void NetServer::setLocalHost(CompanyId company, const std::string& name) {
    localHost_ = ClientInfo{kLocalClientId, name, company};
    broadcastClientList();
}

void NetServer::broadcastChat(const std::string& from, const std::string& text) {
    ChatBroadcastMsg msg{from, text};
    chat_.push_back(msg);
    broadcastRaw(PacketType::ChatBroadcast, toBytes(msg));
}

std::vector<std::string> NetServer::takeEvents() { return std::exchange(events_, {}); }
std::vector<ChatBroadcastMsg> NetServer::takeChat() { return std::exchange(chat_, {}); }

void NetServer::broadcastRaw(PacketType type, const std::vector<u8>& payload) {
    for (auto& c : clients_)
        if (c->welcomed) c->conn.sendRaw(type, payload);
}

void NetServer::broadcastClientList() {
    ClientListMsg msg;
    msg.clients = clients();
    broadcastRaw(PacketType::ClientList, toBytes(msg));
}

void NetServer::acceptClients() {
    if (!listener_) return;
    while (auto sock = listener_->accept()) {
        if (static_cast<i32>(clients_.size()) >= options_.maxClients) {
            Connection tmp(std::move(*sock));
            tmp.send(PacketType::Reject, RejectMsg{"Server full"});
            tmp.pump();
            continue;
        }
        auto c = std::make_unique<Client>();
        c->id = nextClientId_++;
        c->conn = Connection(std::move(*sock));
        events_.push_back(std::format("Connection from {}", c->conn.socket().peer()));
        clients_.push_back(std::move(c));
    }
}

void NetServer::sendMap(Client& c) {
    const std::vector<u8> snapshot = snapshotWorld(server_.world(), CompanyId{});
    c.conn.send(PacketType::MapBegin, MapBeginMsg{snapshot.size(), server_.world().clock.tick});
    for (usize pos = 0; pos < snapshot.size(); pos += kMapChunkSize) {
        const usize n = std::min(kMapChunkSize, snapshot.size() - pos);
        c.conn.sendRaw(PacketType::MapData, std::span<const u8>(snapshot.data() + pos, n));
    }
    c.conn.sendRaw(PacketType::MapEnd, {});
    c.conn.send(PacketType::Speed, SpeedMsg{static_cast<u8>(server_.speed())});
    c.welcomed = true; // from now on the client receives the command stream
}

void NetServer::handle(Client& c, const Packet& p) {
    switch (p.type) {
    case PacketType::Hello: {
        const auto hello = p.decode<HelloMsg>();
        if (hello.protocol != kProtocolVersion) {
            c.conn.send(PacketType::Reject, RejectMsg{std::format("Version mismatch: server uses protocol {}, you have {}",
                                                                  kProtocolVersion, hello.protocol)});
            c.conn.pump();
            c.conn.close("version mismatch");
            return;
        }
        c.name = hello.name.empty() ? std::format("Player {}", c.id) : hello.name.substr(0, 32);
        c.conn.send(PacketType::Welcome, WelcomeMsg{c.id, options_.name});
        sendMap(c);
        events_.push_back(std::format("{} joined", c.name));
        broadcastClientList();
        break;
    }
    case PacketType::JoinCompany: {
        if (!c.welcomed) return;
        const auto msg = p.decode<JoinCompanyMsg>();
        if (msg.company == kJoinNewCompany) {
            Command cmd;
            cmd.client = c.id;
            cmd.seq = 0;
            cmd.payload = CmdCreateCompany{std::format("{} Transport", c.name), 0xFF};
            server_.queueCommand(std::move(cmd));
        } else if (msg.company == kJoinSpectator) {
            c.company = CompanyId{};
            c.conn.send(PacketType::CompanyAssigned, CompanyAssignedMsg{c.company});
            broadcastClientList();
        } else if (server_.world().companies.contains(CompanyId(msg.company)) &&
                   !server_.world().companies[CompanyId(msg.company)].aiControlled) {
            c.company = CompanyId(msg.company);
            c.conn.send(PacketType::CompanyAssigned, CompanyAssignedMsg{c.company});
            broadcastClientList();
        }
        break;
    }
    case PacketType::CommandRequest: {
        if (!c.welcomed) return;
        auto req = p.decode<CommandRequestMsg>();
        if (std::holds_alternative<CmdCreateCompany>(req.payload)) return; // use JoinCompany instead
        if (std::holds_alternative<CmdActivateChunks>(req.payload)) {
            // Exploration is rate limited per client.
            if (c.exploreBudget < 1.0) return;
            c.exploreBudget -= 1.0;
        }
        Command cmd;
        cmd.company = c.company; // never trust the client with its company id
        cmd.client = c.id;
        cmd.seq = req.seq;
        cmd.payload = std::move(req.payload);
        server_.queueCommand(std::move(cmd));
        break;
    }
    case PacketType::Chat: {
        const auto msg = p.decode<ChatMsg>();
        if (!msg.text.empty()) broadcastChat(c.name, msg.text.substr(0, 400));
        break;
    }
    default:
        c.conn.close("unexpected packet");
        break;
    }
}

void NetServer::collectResults() {
    for (auto& c : clients_) {
        for (const CommandRecord& r : server_.takeResults(c->id)) {
            if (std::holds_alternative<CmdCreateCompany>(r.command.payload) && r.result.ok) {
                c->company = CompanyId(static_cast<u8>(r.result.createdId));
                c->conn.send(PacketType::CompanyAssigned, CompanyAssignedMsg{c->company});
                events_.push_back(std::format("{} founded a company", c->name));
                broadcastClientList();
            }
        }
    }
}

void NetServer::update(double dtSeconds) {
    acceptClients();
    for (auto& c : clients_) {
        c->exploreBudget = std::min(4.0, c->exploreBudget + dtSeconds * 5.0);
        if (!c->conn.pump()) continue;
        while (auto p = c->conn.next()) {
            try {
                handle(*c, *p);
            } catch (const SerializeError& e) {
                c->conn.close(std::string("malformed message: ") + e.what());
            }
            if (!c->conn.open()) break;
        }
    }
    if (lastBroadcastSpeed_ != server_.speed()) {
        lastBroadcastSpeed_ = server_.speed();
        broadcastRaw(PacketType::Speed, toBytes(SpeedMsg{static_cast<u8>(lastBroadcastSpeed_)}));
    }
    server_.update(dtSeconds);
    collectResults();
    bool removed = false;
    for (auto it = clients_.begin(); it != clients_.end();) {
        Client& c = **it;
        c.conn.pump();
        if (!c.conn.open()) {
            events_.push_back(std::format("{} left ({})", c.name.empty() ? c.conn.socket().peer() : c.name,
                                          c.conn.error()));
            it = clients_.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    if (removed) broadcastClientList();
}

} // namespace tp::net
