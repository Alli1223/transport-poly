#include "net/net_server.hpp"

#include "core/log.hpp"
#include "core/version.hpp"
#include "sim/savegame.hpp"

#include <algorithm>
#include <format>

namespace tp::net {

NetServer::NetServer(std::unique_ptr<World> world) : NetServer(std::move(world), Options{}) {}

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
    ChatBroadcastMsg msg;
    msg.from = from;
    msg.text = text;
    deliverChat(msg, 0);
}

void NetServer::localChat(const std::string& text, ChatScope scope, u16 target) {
    if (text.empty()) return;
    ChatBroadcastMsg msg;
    msg.from = localHost_ ? localHost_->name : std::string("Host");
    msg.text = text.substr(0, 400);
    msg.scope = scope;
    msg.fromClient = kLocalClientId;
    msg.fromCompany = localHost_ ? localHost_->company : CompanyId{};
    deliverChat(msg, target);
}

// Sends a chat message to the players it is for: everyone, the sender's company (never spectators
// as a group) or one player; the sender always sees their own message.
void NetServer::deliverChat(const ChatBroadcastMsg& msg, u16 target) {
    const auto wants = [&](u16 client, CompanyId company) {
        if (client == msg.fromClient) return true;
        switch (msg.scope) {
        case ChatScope::All: return true;
        case ChatScope::Company: return msg.fromCompany.valid() && company == msg.fromCompany;
        case ChatScope::Private: return client == target;
        }
        return false;
    };
    const std::vector<u8> bytes = toBytes(msg);
    for (auto& c : clients_)
        if (c->welcomed && wants(c->id, c->company)) c->conn.sendRaw(PacketType::ChatBroadcast, bytes);
    if (localHost_ ? wants(kLocalClientId, localHost_->company) : msg.scope == ChatScope::All) chat_.push_back(msg);
}

void NetServer::setCompanyPassword(CompanyId company, const std::string& password) {
    if (password.empty()) companyPasswords_.erase(company.value);
    else companyPasswords_[company.value] = password.substr(0, 64);
    broadcastClientList();
}

bool NetServer::companyLocked(CompanyId company) const { return companyPasswords_.contains(company.value); }

std::vector<CompanyId> NetServer::lockedCompanies() const {
    std::vector<CompanyId> out;
    for (const auto& [company, password] : companyPasswords_)
        if (server_.world().companies.contains(CompanyId(company))) out.push_back(CompanyId(company));
    return out;
}

void NetServer::setLocalCursor(const std::optional<CursorInfo>& cursor) {
    localCursor_ = cursor;
    if (localCursor_) localCursor_->client = kLocalClientId;
}

std::vector<CursorInfo> NetServer::cursors() const {
    std::vector<CursorInfo> out;
    if (localHost_ && localCursor_) out.push_back(*localCursor_);
    for (const auto& c : clients_)
        if (c->welcomed && c->cursor) out.push_back(*c->cursor);
    return out;
}

bool NetServer::kick(u16 client, const std::string& reason) {
    for (auto& c : clients_)
        if (c->id == client) {
            c->conn.send(PacketType::Reject, RejectMsg{reason.empty() ? std::string("Removed by the host") : reason});
            c->conn.pump();
            c->conn.close("removed by the host");
            return true;
        }
    return false;
}

void NetServer::setLocalCompany(CompanyId company) {
    if (!localHost_) return;
    localHost_->company = company;
    broadcastClientList();
}

void NetServer::assign(Client& c, CompanyId company) {
    c.company = company;
    c.conn.send(PacketType::CompanyAssigned, CompanyAssignedMsg{c.company});
    broadcastClientList();
}

void NetServer::joinCompany(Client& c, const JoinCompanyMsg& msg) {
    const World& w = server_.world();
    if (msg.company == kJoinNewCompany) {
        std::string name = msg.name.substr(0, 64);
        if (name.empty()) name = std::format("{} Transport", c.name);
        for (const auto [id, co] : w.companies)
            if (co.name == name) {
                c.conn.send(PacketType::JoinRefused, JoinRefusedMsg{"A company already has that name"});
                return;
            }
        Command cmd;
        cmd.client = c.id;
        cmd.seq = 0;
        cmd.payload = CmdCreateCompany{name, msg.colour};
        server_.queueCommand(std::move(cmd));
    } else if (msg.company == kJoinSpectator) {
        assign(c, CompanyId{});
    } else if (const CompanyId id(msg.company);
               w.companies.contains(id) && !w.companies[id].aiControlled && !w.companies[id].bankrupt) {
        // Co-op: join a company others already play, giving its password if it has one.
        if (const auto it = companyPasswords_.find(id.value); it != companyPasswords_.end() && it->second != msg.password) {
            c.conn.send(PacketType::JoinRefused,
                        JoinRefusedMsg{msg.password.empty() ? "That company needs a password" : "Wrong company password"});
            return;
        }
        assign(c, id);
        events_.push_back(std::format("{} joined {}", c.name, w.companies[id].name));
    } else {
        c.conn.send(PacketType::JoinRefused, JoinRefusedMsg{"That company can't be joined"});
    }
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
    msg.locked = lockedCompanies();
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
        if (!options_.password.empty() && hello.password != options_.password) {
            c.conn.send(PacketType::Reject,
                        RejectMsg{hello.password.empty() ? "This server needs a password" : "Wrong server password"});
            c.conn.pump();
            c.conn.close("wrong password");
            return;
        }
        // Names are unique among the players, so chat and the player list are unambiguous.
        const std::string wanted = hello.name.empty() ? std::format("Player {}", c.id) : hello.name.substr(0, 28);
        const auto taken = [&](const std::string& n) {
            if (localHost_ && localHost_->name == n) return true;
            return std::ranges::any_of(clients_, [&](const auto& o) { return o.get() != &c && o->welcomed && o->name == n; });
        };
        c.name = wanted;
        for (int k = 2; taken(c.name) && k < 100; ++k) c.name = std::format("{} ({})", wanted, k);
        c.conn.send(PacketType::Welcome, WelcomeMsg{c.id, options_.name});
        sendMap(c);
        events_.push_back(std::format("{} joined", c.name));
        broadcastClientList();
        break;
    }
    case PacketType::JoinCompany: {
        if (!c.welcomed) return;
        joinCompany(c, p.decode<JoinCompanyMsg>());
        break;
    }
    case PacketType::SetCompanyPassword: {
        if (!c.welcomed || !c.company.valid()) return;
        setCompanyPassword(c.company, p.decode<SetCompanyPasswordMsg>().password);
        break;
    }
    case PacketType::Cursor: {
        if (!c.welcomed) return;
        const auto msg = p.decode<CursorMsg>();
        if (msg.visible) c.cursor = CursorInfo{c.id, msg.x, msg.y, msg.z};
        else c.cursor.reset();
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
        if (!c.welcomed) return;
        const auto in = p.decode<ChatMsg>();
        if (in.text.empty()) return;
        ChatBroadcastMsg msg;
        msg.from = c.name;
        msg.text = in.text.substr(0, 400);
        msg.scope = in.scope;
        msg.fromClient = c.id;
        msg.fromCompany = c.company;
        deliverChat(msg, in.target);
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
            if (std::holds_alternative<CmdCreateCompany>(r.command.payload)) {
                if (!r.result.ok) {
                    c->conn.send(PacketType::JoinRefused, JoinRefusedMsg{r.result.error});
                    continue;
                }
                assign(*c, CompanyId(static_cast<u8>(r.result.createdId)));
                events_.push_back(std::format("{} founded {}", c->name, server_.world().companies[c->company].name));
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
    // Pointers: ten times a second, and only when one has moved.
    cursorTimer_ += dtSeconds;
    if (cursorTimer_ >= 0.1) {
        cursorTimer_ = 0.0;
        std::vector<CursorInfo> now = cursors();
        const auto same = [](const CursorInfo& a, const CursorInfo& b) {
            return a.client == b.client && a.x == b.x && a.y == b.y && a.z == b.z;
        };
        if (!std::ranges::equal(now, lastCursors_, same)) {
            lastCursors_ = now;
            broadcastRaw(PacketType::Cursors, toBytes(CursorsMsg{std::move(now)}));
        }
    }
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
