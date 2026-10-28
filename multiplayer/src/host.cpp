/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Direct connect's embedded host (host.hpp): the server's party routing for one party, in the
 * hosting game. Each piece follows its Java counterpart in the Server repository: the packet
 * checks and limits of ServerPacketHandler, the joins and leaves of PartyController, the routing
 * of Party.forward, and the datagrams of ServerDatagramHandler and Session.sendState.
 */
#include "host.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <random>
#include <utility>

#include "sockets.hpp"

namespace mp {

using namespace net;

namespace {

/* How long one poll waits, how much one read takes, the most bytes a slow client may have waiting
 * before it is closed, and how long stop() keeps trying to hand the last frames over. */
constexpr int kPollMs = 10;
constexpr std::size_t kReadChunkBytes = 16384;
constexpr std::size_t kMaxPendingBytes = 1u << 20;
constexpr int kFlushMs = 300;

std::int64_t steady_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

/* A token bucket (the server's RateLimit): a burst, then a steady rate. */
class Bucket {
public:
    Bucket() = default;

    explicit Bucket(Rate rate)
        : capacity_(rate.burst),
          per_ns_(rate.per_second / 1e9),
          tokens_(rate.burst),
          last_(steady_ns()) {}

    bool take() {
        const std::int64_t now = steady_ns();
        tokens_ = std::min(capacity_, tokens_ + static_cast<double>(now - last_) * per_ns_);
        last_ = now;

        // An empty bucket refuses until it has refilled.
        if (tokens_ < 1.0) {
            return false;
        }

        tokens_ -= 1.0;
        return true;
    }

private:
    double capacity_ = 0.0;
    double per_ns_ = 0.0;
    double tokens_ = 0.0;
    std::int64_t last_ = 0;
};

/* Java's String.strip, for the whitespace a name may carry. */
std::string strip(const std::string& text) {
    const char* space = " \t\n\v\f\r\x1c\x1d\x1e\x1f";
    const auto begin = text.find_first_not_of(space);

    // All whitespace: nothing is left.
    if (begin == std::string::npos) {
        return {};
    }

    return text.substr(begin, text.find_last_not_of(space) - begin + 1);
}

/* ServerPacketHandler.isValid: neither blank nor longer than `max` characters. */
bool valid(const std::string& text, std::size_t max) {
    return !strip(text).empty() && utf16_length(text) <= max;
}

/* A 64-bit token no one can guess from the outside. */
std::int64_t new_token() {
    std::random_device random;
    const std::uint64_t high = random();
    const std::uint64_t low = random();
    return static_cast<std::int64_t>((high << 32) ^ low);
}

/* The numeric address of a socket address, without its port. */
std::string address_text(const sockaddr* address, socklen_t length) {
    char host[NI_MAXHOST] = {};

    // Numeric only: no name lookups on the host's thread.
    if (getnameinfo(address, length, host, sizeof host, nullptr, 0, NI_NUMERICHOST) != 0) {
        return "?";
    }

    std::string text = host;

    // A dual-stack socket names IPv4 clients as mapped IPv6 addresses.
    if (text.rfind("::ffff:", 0) == 0 && text.find('.') != std::string::npos) {
        text = text.substr(7);
    }

    return text;
}

/* Opens a socket of `type` on `port`, IPv6 and IPv4 at once where the system allows, else IPv4. */
socket_t listen_on(int type, int port, int& error_kind) {
    error_kind = 0;

    // Dual-stack first, then IPv4 alone.
    for (const int family : {AF_INET6, AF_INET}) {
        const socket_t fd = socket(family, type, 0);

        // No IPv6 here: IPv4.
        if (fd == kNoSocket) {
            continue;
        }

#if defined(_WIN32)
        BOOL exclusive = TRUE;
        setsockopt(
            fd,
            SOL_SOCKET,
            SO_EXCLUSIVEADDRUSE,
            reinterpret_cast<const char*>(&exclusive),
            sizeof exclusive
        );
#else
        // A restarted host may take its port back at once (TCP's TIME_WAIT).
        if (type == SOCK_STREAM) {
            int on = 1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
        }
#endif

        sockaddr_storage address{};
        socklen_t length = 0;

        if (family == AF_INET6) {
            int off = 0;
            setsockopt(
                fd, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&off), sizeof off
            );
            auto* a = reinterpret_cast<sockaddr_in6*>(&address);
            a->sin6_family = AF_INET6;  // Zeroed: the any address, ::.
            a->sin6_port = htons(static_cast<std::uint16_t>(port));
            length = sizeof(sockaddr_in6);
        } else {
            auto* a = reinterpret_cast<sockaddr_in*>(&address);
            a->sin_family = AF_INET;
            a->sin_addr.s_addr = htonl(INADDR_ANY);
            a->sin_port = htons(static_cast<std::uint16_t>(port));
            length = sizeof(sockaddr_in);
        }

        if (bind(fd, reinterpret_cast<sockaddr*>(&address), length) == 0
            && (type != SOCK_STREAM || listen(fd, 16) == 0)) {
            set_blocking(fd, false);
            return fd;
        }

        // Taken is taken, whatever the family: no use trying IPv4 alone.
        const bool in_use = address_in_use();
        close_socket(fd);

        if (in_use) {
            error_kind = 1;
            return kNoSocket;
        }

        error_kind = 2;
    }

    return kNoSocket;
}

/* The port a socket is bound to. */
int bound_port(socket_t fd) {
    sockaddr_storage address{};
    socklen_t length = sizeof address;
    getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length);
    return address.ss_family == AF_INET6
               ? ntohs(reinterpret_cast<sockaddr_in6*>(&address)->sin6_port)
               : ntohs(reinterpret_cast<sockaddr_in*>(&address)->sin_port);
}

}  // namespace

/* One connection: a client's socket, or the local game (no socket). */
struct Host::Peer {
    socket_t fd = kNoSocket;
    bool local = false;
    /* The session id (0 until HELLO) and the name. */
    std::int32_t id = 0;
    std::string name;
    std::string address;
    std::int64_t opened_ns = 0;
    FrameReader in;
    Bytes out;
    Bucket chat, party, data, time, strikes, state;
    bool in_party = false;
    /* Greeted before the party existed: joins when it does. */
    bool waiting = false;
    /* Closed: no more frames; its socket is closed and it is forgotten on the thread's next pass. */
    bool dead = false;
    /* UDP: the token, and where its last valid datagram came from and when. */
    std::int64_t token = 0;
    sockaddr_storage udp{};
    socklen_t udp_length = 0;
    std::int64_t udp_heard_ns = 0;

    Member member() const { return {id, name}; }
};

Host::Host(HostConfig config) : config_(config) {
#if defined(_WIN32)
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
}

Host::~Host() {
    stop();
#if defined(_WIN32)
    WSACleanup();
#endif
}

bool Host::start(Sink sink) {
    // One start per host.
    if (running_.load() || thread_.joinable()) {
        return true;
    }

    int error_kind = 0;
    const socket_t listener = listen_on(SOCK_STREAM, config_.port, error_kind);

    // Nothing to host on.
    if (listener == kNoSocket) {
        error_ = error_kind == 1 ? "Port " + std::to_string(config_.port) + " is in use"
                                 : "Could not listen on port " + std::to_string(config_.port);
        return false;
    }

    port_ = bound_port(listener);
    listener_ = static_cast<std::intptr_t>(listener);

    // UDP on the same port, when it is free; without it, state goes by TCP.
    if (config_.udp) {
        const socket_t udp = listen_on(SOCK_DGRAM, port_, error_kind);
        udp_ok_ = udp != kNoSocket;
        udp_ = static_cast<std::intptr_t>(udp);
    }

    sink_ = std::move(sink);
    started_ns_ = steady_ns();
    error_.clear();
    ended_ = false;
    stop_ = false;

    // The local game, linked in-process: greeted by its own HELLO.
    auto local = std::make_unique<Peer>();
    local->local = true;
    local->opened_ns = started_ns_;
    local_ = local.get();
    peers_.push_back(std::move(local));

    running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
}

void Host::stop() {
    // Never started, or stopped already.
    if (!thread_.joinable()) {
        return;
    }

    stop_ = true;
    thread_.join();

    const std::lock_guard lock(mutex_);

    // The party goes with its host.
    if (local_ != nullptr && local_->in_party) {
        end_party();
    }

    close_all();
    running_ = false;
}

void Host::close_all() {
    const std::int64_t until = steady_ns() + std::int64_t{kFlushMs} * 1000000;

    // The last frames (END) out, for as long as the clients take them.
    for (auto& p : peers_) {
        while (!p->local && !p->dead && !p->out.empty() && steady_ns() < until) {
            flush(*p);

            if (!p->out.empty()) {
                pollfd_t fd{};
                fd.fd = p->fd;
                fd.events = POLLOUT;
                poll_sockets(&fd, 1, 10);
            }
        }

        if (p->fd != kNoSocket) {
            close_socket(p->fd);
            p->fd = kNoSocket;
        }
    }

    peers_.clear();
    party_.clear();
    local_ = nullptr;

    if (listener_ != -1) {
        close_socket(static_cast<socket_t>(listener_));
        listener_ = -1;
    }

    if (udp_ != -1) {
        close_socket(static_cast<socket_t>(udp_));
        udp_ = -1;
    }
}

std::int64_t Host::clock_ms() const {
    return (steady_ns() - started_ns_) / 1000000;
}

std::vector<Member> Host::members() const {
    const std::lock_guard lock(mutex_);
    std::vector<Member> out;

    for (const Peer* p : party_) {
        out.push_back(p->member());
    }

    return out;
}

void Host::run() {
    [[maybe_unused]] const Sockets sockets;
    std::vector<pollfd_t> fds;
    std::vector<Peer*> polled;

    // Until stopped: wait on every socket, then handle what is ready.
    while (!stop_.load()) {
        fds.clear();
        polled.clear();
        {
            const std::lock_guard lock(mutex_);
            pollfd_t fd{};
            fd.fd = static_cast<socket_t>(listener_);
            fd.events = POLLIN;
            fds.push_back(fd);

            // The UDP socket, when there is one.
            if (udp_ != -1) {
                fd.fd = static_cast<socket_t>(udp_);
                fds.push_back(fd);
            }

            // Each live client: to read, and to write while frames wait.
            for (auto& p : peers_) {
                if (p->local || p->dead) {
                    continue;
                }

                fd.fd = p->fd;
                fd.events = static_cast<short>(POLLIN | (p->out.empty() ? 0 : POLLOUT));
                fds.push_back(fd);
                polled.push_back(p.get());
            }
        }

        poll_sockets(fds.data(), fds.size(), kPollMs);

        const std::lock_guard lock(mutex_);
        const std::size_t first = udp_ != -1 ? 2 : 1;

        // A new connection waiting.
        if ((fds[0].revents & POLLIN) != 0) {
            accept_clients();
        }

        // Datagrams waiting.
        if (first == 2 && (fds[1].revents & POLLIN) != 0) {
            read_datagrams();
        }

        // Each client polled: what it sent, then what waits for it.
        for (std::size_t i = 0; i < polled.size(); ++i) {
            Peer& p = *polled[i];
            const short events = fds[first + i].revents;

            if (!p.dead && (events & (POLLIN | POLLHUP | POLLERR)) != 0) {
                read_peer(p);
            }

            if (!p.dead && !p.out.empty()) {
                flush(p);
            }
        }

        const std::int64_t now = steady_ns();

        // A connection that never says hello would hold a socket for nothing.
        for (auto& p : peers_) {
            if (!p->local && !p->dead && p->id == 0
                && now - p->opened_ns > std::int64_t{config_.hello_timeout_ms} * 1000000) {
                kill(*p);
            }
        }

        // The closed ones' sockets.
        for (auto& p : peers_) {
            if (p->dead && p->fd != kNoSocket) {
                close_socket(p->fd);
                p->fd = kNoSocket;
            }
        }

        peers_.erase(
            std::remove_if(
                peers_.begin(), peers_.end(), [](const std::unique_ptr<Peer>& p) { return p->dead; }
            ),
            peers_.end()
        );
    }
}

void Host::accept_clients() {
    // Every connection waiting.
    for (;;) {
        sockaddr_storage address{};
        socklen_t length = sizeof address;
        const socket_t fd = accept(
            static_cast<socket_t>(listener_), reinterpret_cast<sockaddr*>(&address), &length
        );

        // None left.
        if (fd == kNoSocket) {
            return;
        }

        const std::string from = address_text(reinterpret_cast<sockaddr*>(&address), length);
        std::size_t open = 0;
        std::size_t open_here = 0;

        for (const auto& p : peers_) {
            if (!p->local && !p->dead) {
                ++open;
                open_here += p->address == from ? 1 : 0;
            }
        }

        // Too many already, from anywhere or from this address (ConnectionLimit).
        if (open >= config_.max_connections || open_here >= config_.max_connections_per_address) {
            close_socket(fd);
            continue;
        }

        set_blocking(fd, false);
        set_nodelay(fd);
        no_sigpipe(fd);
        auto peer = std::make_unique<Peer>();
        peer->fd = fd;
        peer->address = from;
        peer->opened_ns = steady_ns();
        peer->chat = Bucket(config_.chat);
        peer->party = Bucket(config_.party);
        peer->data = Bucket(config_.data);
        peer->time = Bucket(config_.time);
        peer->strikes = Bucket(config_.strikes);
        peer->state = Bucket(config_.data);
        peer->token = new_token();
        peers_.push_back(std::move(peer));
    }
}

void Host::read_peer(Peer& peer) {
    std::uint8_t chunk[kReadChunkBytes];
    const auto n = recv(peer.fd, reinterpret_cast<char*>(chunk), static_cast<int>(sizeof chunk), 0);

    // Nothing after all.
    if (n < 0 && would_block()) {
        return;
    }

    // Closed by the client, or failed.
    if (n <= 0) {
        kill(peer);
        return;
    }

    peer.in.feed(chunk, static_cast<std::size_t>(n));
    Bytes body;

    // Every whole frame, until one closes the connection.
    while (!peer.dead && peer.in.next(body)) {
        handle(peer, body);
    }

    // Longer than any frame may be: the connection is closed (the server's frame decoder).
    if (peer.in.broken()) {
        kill(peer);
    }
}

void Host::flush(Peer& peer) {
    // As much as the socket takes now; the rest on its next turn.
    while (!peer.out.empty()) {
        const auto n = ::send(
            peer.fd,
            reinterpret_cast<const char*>(peer.out.data()),
            static_cast<int>(peer.out.size()),
            kSendFlags
        );

        if (n > 0) {
            peer.out.erase(peer.out.begin(), peer.out.begin() + n);
            continue;
        }

        // Its buffer is full: later.
        if (n < 0 && would_block()) {
            return;
        }

        kill(peer);
        return;
    }
}

void Host::send_to(Peer& peer, const Bytes& frame) {
    // The local game's frames go straight to it.
    if (peer.local) {
        if (sink_) {
            sink_(frame);
        }

        return;
    }

    if (peer.dead) {
        return;
    }

    peer.out.insert(peer.out.end(), frame.begin(), frame.end());

    // A client that reads nothing is not kept on forever.
    if (peer.out.size() > kMaxPendingBytes) {
        kill(peer);
        return;
    }

    flush(peer);
}

void Host::kill(Peer& peer) {
    // The local game is never closed: it stops the host instead.
    if (peer.dead || peer.local) {
        return;
    }

    peer.dead = true;
    peer.out.clear();

    // A dropped connection leaves its party (channelInactive).
    if (peer.in_party) {
        leave(peer, false);
    }
}

void Host::local_send(const Bytes& frame) {
    const std::lock_guard lock(mutex_);

    // Stopped: the frame has nowhere to go.
    if (!running_.load() || local_ == nullptr) {
        return;
    }

    local_->in.feed(frame.data(), frame.size());
    Bytes body;

    while (local_->in.next(body)) {
        handle(*local_, body);
    }

    // A frame the game should never write: start over.
    if (local_->in.broken()) {
        local_->in.reset();
    }
}

void Host::local_state(std::int32_t target, const Bytes& bytes) {
    const std::lock_guard lock(mutex_);

    // Out of the party, state has nowhere to go.
    if (!running_.load() || local_ == nullptr || !local_->in_party) {
        return;
    }

    forward(*local_, target, [&](Peer& to) { deliver_state(local_->id, to, bytes); });
}

void Host::handle(Peer& peer, const Bytes& body) {
    const Request q = decode_request(body);

    // Malformed, unknown or the server's own: closed (the local game's: dropped).
    if (q.kind == Request::Kind::kInvalid) {
        kill(peer);
        return;
    }

    // Everything but the greeting needs a session first.
    if (peer.id == 0 && q.kind != Request::Kind::kHello) {
        kill(peer);
        return;
    }

    // Over its rate, a client's packet is dropped and counts as a strike; a flood closes it.
    if (!peer.local) {
        bool within = true;

        switch (q.kind) {
            case Request::Kind::kChat:
                within = peer.chat.take();
                break;
            case Request::Kind::kCreate:
            case Request::Kind::kJoin:
            case Request::Kind::kFind:
            case Request::Kind::kLeave:
                within = peer.party.take();
                break;
            case Request::Kind::kData:
                within = peer.data.take();
                break;
            case Request::Kind::kTime:
                within = peer.time.take();
                break;
            default:
                break;
        }

        if (!within) {
            if (!peer.strikes.take()) {
                kill(peer);
            }

            return;
        }
    }

    switch (q.kind) {
        case Request::Kind::kHello:
            hello(peer, q);
            break;
        case Request::Kind::kCreate:
        case Request::Kind::kJoin:
        case Request::Kind::kFind:
            // One party, so every way in is the same; the code is not read.
            join(peer);
            break;
        case Request::Kind::kLeave:
            leave(peer, true);
            break;
        case Request::Kind::kChat: {
            // Blank or oversized messages only come from a broken client.
            if (!valid(q.text, kMaxChatLength)) {
                kill(peer);
                return;
            }

            // Chat only travels inside a party, to every member, the sender included.
            if (!peer.in_party) {
                send_to(peer, error_reply(ErrorReason::kNotInParty));
                return;
            }

            const Bytes frame = chat_from(peer.id, q.text);

            for (Peer* m : std::vector<Peer*>(party_)) {
                send_to(*m, frame);
            }

            break;
        }
        case Request::Kind::kData: {
            // Data sent outside a party has nowhere to go.
            if (!peer.in_party) {
                return;
            }

            const Bytes frame = data_from(peer.id, q.bytes);
            forward(peer, q.target, [&](Peer& to) { send_to(to, frame); });
            break;
        }
        case Request::Kind::kTime:
            send_to(peer, time_reply(q.client_ms, clock_ms()));
            break;
        case Request::Kind::kInvalid:
            break;
    }
}

void Host::hello(Peer& peer, const Request& q) {
    // One greeting per connection.
    if (peer.id != 0) {
        kill(peer);
        return;
    }

    std::string name = strip(q.text);

    // Blank or oversized fields only come from a broken client; the local game is the launcher's.
    if (!valid(q.version, kMaxNameLength) || !valid(name, kMaxNameLength)) {
        if (!peer.local) {
            kill(peer);
            return;
        }

        name = name.empty() ? "Host" : name.substr(0, kMaxNameLength);
    }

    peer.id = next_id_++;
    peer.name = name;
    send_to(peer, hello_reply(peer.id));

    // A client may add UDP for its state; the local game has no need.
    if (udp_ok_ && !peer.local) {
        send_to(peer, udp_offer(peer.token, port_));
    }

    // A client is put in the party at once, so its lobby opens with nothing typed.
    if (!peer.local) {
        join(peer);
        return;
    }

    // The local game hosts the one party, and those who came early join it.
    peer.in_party = true;
    party_.assign(1, &peer);
    send_to(peer, party_info(kDirectCode, peer.id, {peer.member()}));

    for (auto& p : peers_) {
        if (p->waiting && !p->dead) {
            p->waiting = false;
            join(*p);
        }
    }
}

void Host::join(Peer& peer) {
    // A session belongs to one party at a time.
    if (peer.in_party) {
        send_to(peer, error_reply(ErrorReason::kAlreadyInParty));
        return;
    }

    // The local game has not said hello yet: in as soon as it has.
    if (party_.empty()) {
        // A party that ended does not come back.
        if (ended_.load()) {
            send_to(peer, error_reply(ErrorReason::kPartyNotFound));
            return;
        }

        peer.waiting = true;
        return;
    }

    // Each game only links so many players.
    if (party_.size() >= config_.max_party_size) {
        send_to(peer, error_reply(ErrorReason::kPartyFull));
        return;
    }

    // The members hear of it first, then the newcomer gets the whole party (PartyController.add).
    const Bytes joined = party_joined(peer.member());

    for (Peer* m : std::vector<Peer*>(party_)) {
        send_to(*m, joined);
    }

    party_.push_back(&peer);
    peer.in_party = true;

    std::vector<Member> members;

    for (const Peer* m : party_) {
        members.push_back(m->member());
    }

    send_to(peer, party_info(kDirectCode, party_.front()->id, members));
}

void Host::leave(Peer& peer, bool asked) {
    peer.waiting = false;

    // Leaving nothing is worth telling the client about.
    if (!peer.in_party) {
        if (asked) {
            send_to(peer, error_reply(ErrorReason::kNotInParty));
        }

        return;
    }

    // Without the host there is no game left to link to.
    if (!party_.empty() && party_.front() == &peer) {
        end_party();
        return;
    }

    party_.erase(std::remove(party_.begin(), party_.end(), &peer), party_.end());
    peer.in_party = false;
    const Bytes left = party_left(peer.id);

    for (Peer* m : std::vector<Peer*>(party_)) {
        send_to(*m, left);
    }
}

void Host::end_party() {
    const std::vector<Peer*> members(party_);
    party_.clear();
    ended_ = true;
    const Bytes end = party_end();

    // Everyone but the host, who left, hears the party is over.
    for (Peer* m : members) {
        m->in_party = false;

        if (!m->local) {
            send_to(*m, end);
        }
    }
}

void Host::forward(Peer& sender, std::int32_t target, const std::function<void(Peer&)>& delivery) {
    // An empty party routes nothing.
    if (party_.empty()) {
        return;
    }

    Peer* host = party_.front();

    // A member's data only ever goes to the authoritative host.
    if (&sender != host) {
        delivery(*host);
        return;
    }

    // From the host: everyone else (kBroadcast), or the one member named.
    for (Peer* m : std::vector<Peer*>(party_)) {
        if (m != host && (target == kBroadcast || m->id == target)) {
            delivery(*m);
        }
    }
}

void Host::deliver_state(std::int32_t sender, Peer& to, const Bytes& bytes) {
    const std::int64_t now = steady_ns();
    const bool bound = to.udp_length != 0
                       && now - to.udp_heard_ns <= std::int64_t{config_.udp_timeout_ms} * 1000000;

    // Over UDP to a client that bound it lately and when it fits; else as TCP DATA (sendState).
    if (!to.local && bound && udp_ != -1
        && kServerStateHeader + bytes.size() <= kMaxDatagramBytes) {
        const Bytes datagram = datagram_state_from(sender, bytes);
        sendto(
            static_cast<socket_t>(udp_),
            reinterpret_cast<const char*>(datagram.data()),
            static_cast<int>(datagram.size()),
            0,
            reinterpret_cast<const sockaddr*>(&to.udp),
            to.udp_length
        );
        return;
    }

    send_to(to, data_from(sender, bytes));
}

void Host::read_datagrams() {
    // Larger than any valid datagram, so an oversized one shows as such.
    std::uint8_t buffer[kMaxDatagramBytes + 1];

    // Every datagram waiting (bounded: an error report is no datagram, and never spun on).
    for (int i = 0; i < 1024; ++i) {
        sockaddr_storage from{};
        socklen_t length = sizeof from;
        const auto n = recvfrom(
            static_cast<socket_t>(udp_),
            reinterpret_cast<char*>(buffer),
            static_cast<int>(sizeof buffer),
            0,
            reinterpret_cast<sockaddr*>(&from),
            &length
        );

        // None left (or a port-unreachable report from an earlier send, which is no datagram).
        if (n < 0) {
            if (would_block()) {
                return;
            }

            continue;
        }

        ClientDatagram d;

        // Oversized, cut short or of an unknown kind: dropped unanswered.
        if (static_cast<std::size_t>(n) > kMaxDatagramBytes
            || !decode_client_datagram(buffer, static_cast<std::size_t>(n), d)) {
            continue;
        }

        Peer* peer = nullptr;

        for (auto& p : peers_) {
            if (!p->local && !p->dead && p->id != 0 && p->id == d.session) {
                peer = p.get();
            }
        }

        // Only the session's own client was told its token.
        if (peer == nullptr || peer->token != d.token) {
            continue;
        }

        // Any valid datagram binds its sender (NAT may move the port) and keeps UDP in use.
        std::memcpy(&peer->udp, &from, sizeof from);
        peer->udp_length = length;
        peer->udp_heard_ns = steady_ns();

        // BIND asks for nothing more; STATE is routed as DATA, within the same rate, never struck.
        if (d.kind != DatagramKind::kState || !peer->state.take() || !peer->in_party) {
            continue;
        }

        const std::int32_t sender = peer->id;
        forward(*peer, d.target, [&](Peer& to) { deliver_state(sender, to, d.bytes); });
    }
}

std::vector<std::string> local_addresses() {
    [[maybe_unused]] const Sockets sockets;
    std::vector<std::string> out;
    // The address the system would send from to the internet: no datagram is sent, connect()
    // on UDP only picks the route.
    const socket_t fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (fd == kNoSocket) {
        return out;
    }

    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(53);
    inet_pton(AF_INET, "192.0.2.1", &remote.sin_addr);  // TEST-NET-1: routed, never reached.

    if (connect(fd, reinterpret_cast<sockaddr*>(&remote), sizeof remote) == 0) {
        sockaddr_in self{};
        socklen_t length = sizeof self;

        if (getsockname(fd, reinterpret_cast<sockaddr*>(&self), &length) == 0
            && self.sin_addr.s_addr != htonl(INADDR_ANY)
            && self.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
            char text[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &self.sin_addr, text, sizeof text);
            out.emplace_back(text);
        }
    }

    close_socket(fd);
    return out;
}

}  // namespace mp
