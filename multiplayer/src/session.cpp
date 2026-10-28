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

Session::Session(
    Transport& transport,
    std::string version,
    std::string name,
    std::function<std::uint64_t()> clock
)
    : transport_(transport),
      version_(std::move(version)),
      name_(std::move(name)),
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
    ready_.clear();
    started_ = false;
    waiting_ = false;
}

void Session::create() {
    // Only a greeted session out of any party, with nothing pending.
    if (!greeted() || in_party() || waiting_) {
        return;
    }

    waiting_ = true;
    transport_.send(party_create());
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
    transport_.send(data(
        host_, Writer().u8(static_cast<std::uint8_t>(LobbyKind::kReady)).u8(now ? 1 : 0).bytes()
    ));

    // Shown at once; the host's STATE confirms it.
    if (now) {
        ready_.insert(id_);
    } else {
        ready_.erase(id_);
    }
}

void Session::start() {
    // Only the host starts, once.
    if (!is_host() || started_) {
        return;
    }

    started_ = true;
    transport_.send(
        data(kBroadcast, Writer().u8(static_cast<std::uint8_t>(LobbyKind::kStart)).bytes())
    );
    notice_ = "Game starting (playing together comes in a later version)";
}

void Session::on_data(std::function<void(std::int32_t, const Bytes&)> callback) {
    on_data_ = std::move(callback);
}

void Session::send_data(std::int32_t target, const Bytes& bytes) {
    // Game data goes only inside a party, and never as one of the lobby's kinds.
    if (!in_party() || bytes.empty()
        || (bytes[0] >= static_cast<std::uint8_t>(LobbyKind::kReady)
            && bytes[0] <= static_cast<std::uint8_t>(LobbyKind::kStart))) {
        return;
    }

    transport_.send(data(target, bytes));
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
            ready_ = std::move(ready);
        }
    } else if (kind == LobbyKind::kStart && m.id == host_) {
        // The host started the game.
        started_ = true;
        notice_ = "Game starting (playing together comes in a later version)";
    } else if (kind != LobbyKind::kReady && kind != LobbyKind::kState && kind != LobbyKind::kStart
               && on_data_) {
        // Not the lobby's: the game's own data, as it came.
        on_data_(m.id, m.bytes);
    }
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
