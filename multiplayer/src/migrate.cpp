/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Losing a connection, and coming back (migrate.hpp).
 */
#include "migrate.hpp"

#include <algorithm>
#include <utility>

namespace mp {

Migrate::Migrate(Session& session, Coop& coop, World& world, Progress& progress, Shared& shared,
                 std::function<std::uint64_t()> clock)
    : session_(session),
      coop_(coop),
      world_(world),
      progress_(progress),
      shared_(shared),
      clock_(std::move(clock)) {
    if (!clock_) {
        clock_ = now_ms;
    }
}

void Migrate::handle(std::int32_t, const Bytes&) {}

std::string Migrate::name_of(std::int32_t member) const {
    for (const Member& m : session_.members()) {
        if (m.id == member) {
            return m.name;
        }
    }

    return {};
}

void Migrate::set_hold(bool on) {
    coop_.hold_for_host(on);
}

void Migrate::take(const PartyEvent& event, std::uint64_t now) {
    switch (event.kind) {
        case PartyEvent::Kind::kDropped:
            log("connection lost: keeping this game's place in the party");
            break;
        case PartyEvent::Kind::kResumed:
            log("back in the party");
            progress_.again();
            shared_.again();

            if (session_.role().host()) {
                world_.host_back();
            } else {
                coop_.rejoined(now);
                world_.rejoined();
            }

            break;
        case PartyEvent::Kind::kAway:
            log(name_of(event.id) + "'s connection dropped");

            if (session_.role().from_host(event.id)) {
                host_gone_ms_ = now;
                set_hold(true);
            } else if (session_.role().host()) {
                world_.member_away(event.id);
            }

            break;
        case PartyEvent::Kind::kBack:
            log(name_of(event.id) + " is back");

            if (session_.role().from_host(event.id)) {
                host_gone_ms_ = 0;
                set_hold(false);
            } else if (session_.role().host()) {
                coop_.member_back(event.id);
                world_.member_back(event.id);
                progress_.again();
                shared_.again();
            }

            break;
    }
}

void Migrate::tick(std::uint64_t now) {
    // A party gone took the wait with it.
    if (!session_.in_party() && host_gone_ms_ != 0) {
        host_gone_ms_ = 0;
        set_hold(false);
    }

    for (const PartyEvent& event : session_.take_events()) {
        take(event, now);
    }
}

std::string Migrate::banner(std::uint64_t now) const {
    if (session_.dropped()) {
        return kDroppedLine;
    }

    if (host_gone_ms_ != 0) {
        const std::uint64_t waited = now - host_gone_ms_;
        const std::uint64_t left = waited < kHostGraceMs ? (kHostGraceMs - waited + 999) / 1000 : 0;
        return "The host lost its connection: waiting for it (" + std::to_string(left) + ")";
    }

    return {};
}

}  // namespace mp
