/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The despawn log (despawn.hpp): each tick's listing against the last one's.
 */
#include "despawn.hpp"

#include <cmath>
#include <cstdio>

namespace mp {
namespace {

std::string hex(std::uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%02x", v);
    return b;
}

std::string metres(const float* a, const float* b) {
    float sum = 0;
    for (int i = 0; i < 3; ++i) {
        sum += (a[i] - b[i]) * (a[i] - b[i]);
    }
    char s[24];
    std::snprintf(s, sizeof s, "%.1f", std::sqrt(sum));
    return s;
}

bool dead(const MobyState& m) {
    return (m.flags & kMobyDead) != 0;
}

bool hidden(const MobyState& m) {
    return (m.flags & kMobyHidden) != 0;
}

}  // namespace

std::string despawn_cause(
    int slot,
    const std::string* sync_why,
    std::int32_t sender,
    const MobyDeletion* deleted,
    bool death,
    bool listed_dead
) {
    if (sync_why != nullptr) {
        return "by sync (" + *sync_why + (sender != 0 ? " from " + std::to_string(sender) : "")
               + ")";
    }

    const std::string recorded = death ? ", its death recorded" : "";

    if (deleted != nullptr && deleted->by_mod) {
        return "by mod (moby_kill, not the sync's" + recorded + ")";
    }

    if (deleted != nullptr && deleted->by == slot) {
        return "by game (its own update" + recorded + ")";
    }

    if (deleted != nullptr && deleted->by >= 0) {
        return "by game (the update of slot " + std::to_string(deleted->by) + " class "
               + std::to_string(deleted->by_class) + recorded + ")";
    }

    if (deleted != nullptr) {
        return "by game (outside any moby's update" + recorded + ")";
    }

    return std::string("vanished (") + (listed_dead ? "listed dead" : "left the listing")
           + ", no deletion seen" + recorded + ")";
}

void DespawnLog::note_sync(int slot, const std::string& why, std::int32_t sender) {
    if (slot >= 0) {
        sync_[slot] = {why, sender};
    }
}

void DespawnLog::note_death(int slot) {
    if (slot >= 0) {
        deaths_[slot] = true;
    }
}

void DespawnLog::reset() {
    last_.clear();
    roles_.clear();
    sync_.clear();
    deaths_.clear();
}

void DespawnLog::flush(std::uint64_t now) {
    // A new second: what the last one left out, in one line.
    if (now - second_ms_ < 1000) {
        return;
    }

    if (dropped_ > 0 && out) {
        out("despawn: " + std::to_string(dropped_) + " more events in the last second left out ("
            + std::to_string(dropped_gone_) + " gone, " + std::to_string(dropped_ - dropped_gone_)
            + " hidden or shown)");
    }

    second_ms_ = now;
    lines_ = 0;
    dropped_ = 0;
    dropped_gone_ = 0;
}

void DespawnLog::event(
    std::uint64_t now,
    const std::string& what,
    int slot,
    const MobyState& was,
    const std::string& who_why,
    const View& view
) {
    flush(now);

    if (lines_ >= kDespawnLinesPerSecond) {
        ++dropped_;
        ++dropped_total_;
        dropped_gone_ += what == "gone" ? 1 : 0;
        return;
    }

    ++lines_;
    ++written_;
    char at[64];
    std::snprintf(
        at,
        sizeof at,
        "%.1f %.1f %.1f",
        was.pose.position[0],
        was.pose.position[1],
        was.pose.position[2]
    );
    std::string line = "despawn: " + what + " class " + std::to_string(was.oclass) + " key "
                       + std::to_string(was.key) + " slot " + std::to_string(slot) + " "
                       + (roles_.count(slot) != 0 ? roles_[slot] : std::string("local")) + " "
                       + who_why + " at " + at;
    line += ", hero " + (view.hero ? metres(view.hero_at, was.pose.position) : std::string("?"));
    line +=
        ", camera " + (view.camera ? metres(view.camera_at, was.pose.position) : std::string("?"));
    line += ", state " + hex(was.state) + " flags " + hex(was.flags);

    if (host_seen) {
        const std::string seen = host_seen(slot, was);

        if (!seen.empty()) {
            line += ", " + seen;
        }
    }

    if (out) {
        out(line);
    }
}

void DespawnLog::tick(std::uint64_t now, const std::vector<MobyState>& listing, const View& view) {
    flush(now);

    // A tick the game lists nothing of (a load, a menu of its own) compares nothing.
    if (listing.empty()) {
        return;
    }

    std::map<int, MobyState> next;

    for (const MobyState& m : listing) {
        next[m.slot] = m;
    }

    for (const auto& [slot, was] : last_) {
        if (dead(was)) {
            continue;
        }

        const auto now_at = next.find(slot);
        const bool same = now_at != next.end() && now_at->second.oclass == was.oclass
                          && now_at->second.key == was.key;
        const auto note = sync_.find(slot);

        if (same && !dead(now_at->second)) {
            // Alive still: hidden, or shown again.
            if (hidden(was) != hidden(now_at->second)) {
                const std::string who =
                    note != sync_.end()
                        ? despawn_cause(
                              slot, &note->second.why, note->second.sender, nullptr, false, false
                          )
                        : "by game";
                event(
                    now,
                    hidden(now_at->second) ? "hidden" : "shown",
                    slot,
                    now_at->second,
                    who,
                    view
                );
            }

            continue;
        }

        // Gone: listed dead, not listed, or its slot another moby's now.
        MobyDeletion d;
        const bool deleted = deletion && deletion(slot, d);
        const bool death = deaths_.count(slot) != 0;
        const std::string who = despawn_cause(
            slot,
            note != sync_.end() ? &note->second.why : nullptr,
            note != sync_.end() ? note->second.sender : 0,
            deleted ? &d : nullptr,
            death,
            same && dead(now_at->second)
        );
        event(now, "gone", slot, was, who, view);
    }

    sync_.clear();
    deaths_.clear();
    roles_.clear();

    // How each one stands now (the World forgets a driven one as it kills it).
    for (const auto& [slot, m] : next) {
        if (!dead(m)) {
            roles_[slot] = role ? role(slot, m) : "local";
        }
    }

    last_ = std::move(next);
}

}  // namespace mp
