/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The player's session and party (session.hpp), from the server's packets: INFO gives the whole
 * party, JOINED and LEFT change it, END means the host left and the party is gone, ERROR refuses
 * the last action.
 */
#include "session.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <utility>

namespace mp {
namespace {

/* The launcher's name limits (launcher/core/src/multiplayer.rs). */
constexpr std::size_t kMinName = 3;
constexpr std::size_t kMaxName = 16;

}  // namespace

double Pacer::level(std::uint64_t now) {
    if (now > last_) {
        tokens_ = std::min(capacity_, tokens_ + static_cast<double>(now - last_) * per_ms_);
        last_ = now;
    }

    return tokens_;
}

bool Pacer::take(std::uint64_t now) {
    if (level(now) < 1.0) {
        return false;
    }

    tokens_ -= 1.0;
    return true;
}

void Pacer::spend(std::uint64_t now) {
    level(now);
    tokens_ -= 1.0;
}

Session::Session(
    Transport& transport,
    std::string version,
    std::string name,
    std::function<std::uint64_t()> clock
)
    : transport_(transport),
      version_(std::move(version)),
      name_(std::move(name)),
      data_bucket_(kDataBurst, kDataRate),
      state_bucket_(kStateBurst, kStateRate),
      clock_(std::move(clock)) {
    // Without a clock of the caller's, the monotonic one.
    if (!clock_) {
        clock_ = now_ms;
    }
}

void Session::connected() {
    id_ = 0;
    reset_party();
    transport_.send(hello(version_, name_));
}

void Session::disconnected() {
    id_ = 0;
    reset_party();
}

void Session::reset_party() {
    code_.clear();
    host_ = 0;
    members_.clear();
    description_.clear();
    ready_.clear();
    links_.clear();
    started_ = false;
    waiting_ = false;
    queue_.clear();
}

void Session::create(const std::string& description) {
    // Only a greeted session out of any party, with nothing pending.
    if (!greeted() || in_party() || waiting_) {
        return;
    }

    waiting_ = true;
    transport_.send(party_create(description));
}

void Session::list() {
    // Only a greeted session; the list is of the parties it could join.
    if (!greeted() || in_party()) {
        return;
    }

    transport_.send(party_list());
}

void Session::find() {
    // As create: greeted, out of a party, nothing pending.
    if (!greeted() || in_party() || waiting_) {
        return;
    }

    waiting_ = true;
    transport_.send(party_find());
}

void Session::sync_clock() {
    // The server answers only a greeted session.
    if (!greeted()) {
        return;
    }

    transport_.send(time(static_cast<std::int64_t>(clock_())));
}

std::int64_t Session::server_ms() const {
    return static_cast<std::int64_t>(clock_()) + static_cast<std::int64_t>(offset_ms_);
}

void Session::join(const std::string& code) {
    // As create: greeted, out of a party, nothing pending; and a code of the right length.
    if (!greeted() || in_party() || waiting_ || code.size() != kCodeLength) {
        return;
    }

    waiting_ = true;
    transport_.send(party_join(code));
}

void Session::leave() {
    // Out of a party there is nothing to leave.
    if (!in_party()) {
        return;
    }

    transport_.send(party_leave());
    reset_party();
}

void Session::toggle_ready() {
    // Only a member says it is ready; the host starts instead.
    if (!in_party() || is_host() || started_) {
        return;
    }

    const bool now = !ready(id_);
    send_lobby(host_, Writer().u8(static_cast<std::uint8_t>(LobbyKind::kReady)).u8(now ? 1 : 0).bytes());

    // Shown at once; the host's STATE confirms it.
    if (now) {
        ready_.insert(id_);
    } else {
        ready_.erase(id_);
    }
}

bool Session::all_ready() const {
    // Out of a party nobody is; in one, each member but the host.
    return in_party() && std::all_of(members_.begin(), members_.end(), [&](const Member& m) {
               return m.id == host_ || ready(m.id);
           });
}

bool Session::start() {
    // Only the host starts, once, and only with everyone ready.
    if (!is_host() || started_ || !all_ready()) {
        return false;
    }

    started_ = true;
    send_lobby(kBroadcast, Writer().u8(static_cast<std::uint8_t>(LobbyKind::kStart)).bytes());
    notice_ = "Game starting";
    return true;
}

void Session::on_data(std::function<void(std::int32_t, const Bytes&)> callback) {
    on_data_ = std::move(callback);
}

/* Whether a DATA's first byte is one of the lobby's kinds. */
static bool lobby_kind(std::uint8_t kind) {
    return (kind >= static_cast<std::uint8_t>(LobbyKind::kReady)
            && kind <= static_cast<std::uint8_t>(LobbyKind::kStart))
           || kind == static_cast<std::uint8_t>(LobbyKind::kLink)
           || kind == static_cast<std::uint8_t>(LobbyKind::kLinks)
           || kind == static_cast<std::uint8_t>(LobbyKind::kBatch);
}

bool Session::game_bytes(const Bytes& bytes) const {
    // Game data goes only inside a party, and never as one of the lobby's kinds.
    return in_party() && !bytes.empty() && !lobby_kind(bytes[0]);
}

void Session::report_link(std::int64_t ping_ms, bool udp) {
    if (!in_party()) {
        return;
    }

    const Link own{static_cast<std::int32_t>(std::clamp<std::int64_t>(ping_ms, -1, 99999)), udp};
    links_[id_] = own;

    // The host tells everyone; a member tells the host, who passes it on.
    if (is_host()) {
        send_links();
    } else {
        send_lobby(
            host_,
            Writer()
                .u8(static_cast<std::uint8_t>(LobbyKind::kLink))
                .i32(own.ping_ms)
                .u8(own.udp ? 1 : 0)
                .bytes()
        );
    }
}

Link Session::link(std::int32_t member) const {
    const auto it = links_.find(member);
    return it != links_.end() ? it->second : Link{};
}

void Session::send_links() {
    Writer w;
    w.u8(static_cast<std::uint8_t>(LobbyKind::kLinks))
        .u8(static_cast<std::uint8_t>(members_.size()));

    // Every member, heard of or not (-1).
    for (const Member& m : members_) {
        const Link l = link(m.id);
        w.i32(m.id).i32(l.ping_ms).u8(l.udp ? 1 : 0);
    }

    send_lobby(kBroadcast, w.bytes());
}

void Session::send_lobby(std::int32_t target, const Bytes& bytes) {
    data_bucket_.spend(clock_());
    ++counts_.data_frames;
    transport_.send(data(target, bytes));
}

void Session::send_data(std::int32_t target, const Bytes& bytes) {
    if (game_bytes(bytes)) {
        NetCounts::Kind& k = counts_.events_out[bytes[0]];
        ++k.messages;
        k.bytes += bytes.size();
        queue_.push_back({target, bytes});
        counts_.queue_most = std::max(counts_.queue_most, queue_.size());
    }
}

void Session::flush() {
    const std::uint64_t now = clock_();

    while (!queue_.empty()) {
        if (!data_bucket_.take(now)) {
            ++counts_.waited;
            return;
        }

        // Those for one target in a row go as one DATA; one alone as it is.
        const std::int32_t target = queue_.front().target;
        Bytes one = std::move(queue_.front().bytes);
        queue_.pop_front();
        ++counts_.data_frames;

        if (queue_.empty() || queue_.front().target != target
            || one.size() + 2 + queue_.front().bytes.size() + 3 > kBatchBytes) {
            transport_.send(data(target, one));
            continue;
        }

        Writer w;
        w.u8(static_cast<std::uint8_t>(LobbyKind::kBatch));
        const auto put = [&w](const Bytes& b) {
            w.u8(static_cast<std::uint8_t>(b.size() >> 8)).u8(static_cast<std::uint8_t>(b.size())).raw(b);
        };
        put(one);

        while (!queue_.empty() && queue_.front().target == target
               && w.bytes().size() + 2 + queue_.front().bytes.size() <= kBatchBytes) {
            put(queue_.front().bytes);
            queue_.pop_front();
        }

        transport_.send(data(target, w.bytes()));
    }
}

bool Session::send_state(std::int32_t target, const Bytes& bytes) {
    if (!game_bytes(bytes)) {
        return false;
    }

    const std::uint64_t now = clock_();

    if (!state_bucket_.take(now)) {
        ++counts_.state_held;
        return false;
    }

    // Until UDP is known to work, the state goes as DATA too, from what events leave.
    bool tcp = false;

    if (transport_.state_needs_tcp(bytes.size())) {
        tcp = data_bucket_.level(now) > kStateReserve && data_bucket_.take(now);
        counts_.state_tcp += tcp ? 1 : 0;
        counts_.data_frames += tcp ? 1 : 0;
        counts_.state_tcp_held += tcp ? 0 : 1;
    }

    NetCounts::Kind& k = counts_.state_out[bytes[0]];
    ++k.messages;
    k.bytes += bytes.size();
    ++counts_.state_datagrams;
    transport_.send_state(target, bytes, tcp);
    return true;
}

void Session::send_state() {
    Writer w;
    w.u8(static_cast<std::uint8_t>(LobbyKind::kState))
        .u8(static_cast<std::uint8_t>(members_.size()));

    // Every member, ready or not.
    for (const Member& m : members_) {
        w.i32(m.id).u8(ready(m.id) ? 1 : 0);
    }

    transport_.send(data(kBroadcast, w.bytes()));
}

void Session::handle(const Message& m) {
    // Each kind of message changes its part of the state.
    switch (m.kind) {
        case Message::Kind::kHello:
            id_ = m.id;
            clock_samples_ = 0;
            sync_clock();
            break;
        case Message::Kind::kTime: {
            // The round trip from the echoed send time; the server's clock read halfway through it.
            const auto now = static_cast<std::int64_t>(clock_());
            rtt_ms_ = std::max<std::int64_t>(0, now - m.client_ms);
            const double sample = static_cast<double>(m.server_ms - (m.client_ms + rtt_ms_ / 2));
            offset_ms_ = clock_samples_ == 0
                             ? sample
                             : offset_ms_ + kOffsetSmoothing * (sample - offset_ms_);
            ++clock_samples_;
            break;
        }
        case Message::Kind::kPartyInfo: {
            waiting_ = false;
            code_ = m.code;
            host_ = m.host;
            members_ = m.members;
            description_ = m.description;
            // The host first, the rest as the server lists them.
            std::stable_partition(members_.begin(), members_.end(), [&](const Member& x) {
                return x.id == host_;
            });
            ready_.clear();
            break;
        }
        case Message::Kind::kPartyJoined:
            members_.push_back({m.id, m.text});

            // The newcomer learns who is ready from the host.
            if (is_host()) {
                send_state();
            }

            break;
        case Message::Kind::kPartyLeft:
            members_.erase(
                std::remove_if(
                    members_.begin(), members_.end(), [&](const Member& x) { return x.id == m.id; }
                ),
                members_.end()
            );
            ready_.erase(m.id);
            links_.erase(m.id);
            break;
        case Message::Kind::kPartyList:
            parties_ = m.parties;
            ++lists_;
            break;
        case Message::Kind::kPartyEnd:
            reset_party();
            notice_ = "The host left: the party is over";
            break;
        case Message::Kind::kError:
            waiting_ = false;
            notice_ = error_text(m.reason);
            break;
        case Message::Kind::kData:
            handle_data(m);
            break;
        case Message::Kind::kChat:
            // No chat on the page yet.
            break;
        case Message::Kind::kUdp:
            // The connection's own: it binds UDP with it.
            break;
    }
}

void Session::handle_data(const Message& m) {
    // Lobby messages only mean something inside a party, and an empty one is nothing.
    if (!in_party() || m.bytes.empty()) {
        return;
    }

    Reader r(m.bytes.data(), m.bytes.size());
    const auto kind = static_cast<LobbyKind>(r.u8());

    // A member's READY, at the host: kept and told to everyone.
    if (kind == LobbyKind::kReady && is_host()) {
        const bool now = r.u8() != 0;

        // A cut-short message is ignored.
        if (!r.ok()) {
            return;
        }

        // Kept as the member says.
        if (now) {
            ready_.insert(m.id);
        } else {
            ready_.erase(m.id);
        }

        send_state();
    } else if (kind == LobbyKind::kState && m.id == host_) {
        // The host's word on who is ready.
        const std::uint8_t count = r.u8();
        std::set<std::int32_t> ready;

        // Each member: its id and its flag.
        for (std::uint8_t i = 0; i < count && r.ok(); ++i) {
            const std::int32_t member = r.i32();

            // Only the ready ones are kept.
            if (r.u8() != 0) {
                ready.insert(member);
            }
        }

        // A cut-short message is ignored.
        if (r.ok()) {
            // This member's own word stands: the host only changes it on this member's READY,
            // and a STATE sent before the last toggle may arrive after it.
            if (ready_.count(id_) != 0) {
                ready.insert(id_);
            } else {
                ready.erase(id_);
            }

            ready_ = std::move(ready);
        }
    } else if (kind == LobbyKind::kStart && m.id == host_) {
        // The host started the game.
        started_ = true;
        notice_ = "Game starting";
    } else if (kind == LobbyKind::kLink && is_host()) {
        // A member's link, at the host: kept and told to everyone.
        const std::int32_t ping = r.i32();
        const bool udp = r.u8() != 0;

        // A cut-short message is ignored.
        if (r.ok()) {
            links_[m.id] = {ping, udp};
            send_links();
        }
    } else if (kind == LobbyKind::kLinks && m.id == host_) {
        // The host's word on everyone's link; the player's own stays as it reported it.
        const std::uint8_t count = r.u8();
        std::map<std::int32_t, Link> links;

        for (std::uint8_t i = 0; i < count && r.ok(); ++i) {
            const std::int32_t member = r.i32();
            const std::int32_t ping = r.i32();
            const bool udp = r.u8() != 0;

            // A member not heard of yet keeps none.
            if (ping >= 0) {
                links[member] = {ping, udp};
            }
        }

        // A cut-short message is ignored.
        if (r.ok()) {
            if (const auto own = links_.find(id_); own != links_.end()) {
                links[id_] = own->second;
            }

            links_ = std::move(links);
        }
    } else if (kind == LobbyKind::kBatch) {
        // Several of the game's messages in one DATA: each as if it came alone.
        while (r.left() >= 2) {
            const std::size_t size = static_cast<std::size_t>(r.u8()) << 8 | r.u8();

            if (size == 0 || size > r.left()) {
                return;
            }

            Bytes one(size);

            for (std::uint8_t& b : one) {
                b = r.u8();
            }

            note_in(one);

            if (on_data_ && !lobby_kind(one[0])) {
                on_data_(m.id, one);
            }
        }
    } else if (!lobby_kind(static_cast<std::uint8_t>(kind)) && on_data_) {
        // Not the lobby's: the game's own data, as it came.
        note_in(m.bytes);
        on_data_(m.id, m.bytes);
    }
}

void Session::note_in(const Bytes& bytes) {
    NetCounts::Kind& k = counts_.in[bytes[0]];
    ++k.messages;
    k.bytes += bytes.size();
}

std::uint64_t now_ms() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count()
    );
}

std::string Session::take_notice() {
    std::string out;
    out.swap(notice_);
    return out;
}

bool valid_name(const std::string& name) {
    return name.size() >= kMinName && name.size() <= kMaxName
           && std::all_of(name.begin(), name.end(), [](char c) {
                  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
              });
}

std::string choose_name(const std::string& setting, const std::string& launcher) {
    // The mod's own setting first, then the launcher's; an invalid one is passed over. The
    // placeholder default (an older launcher, or the raw default) is "not set".
    if (setting != "${player_name}" && valid_name(setting)) {
        return setting;
    }

    return valid_name(launcher) ? launcher : std::string();
}

const char* error_text(ErrorReason reason) {
    // Each reason its own words.
    switch (reason) {
        case ErrorReason::kPartyNotFound:
            return "No party has that code";
        case ErrorReason::kPartyFull:
            return "That party is full";
        case ErrorReason::kAlreadyInParty:
            return "You are in a party already";
        case ErrorReason::kNotInParty:
            return "You are not in a party";
    }

    return "The server refused";
}

}  // namespace mp
