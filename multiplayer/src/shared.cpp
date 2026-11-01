/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The party's shared progress past the items, gold bolts and planets (shared.hpp).
 */
#include "shared.hpp"

#include <algorithm>

#include "progress.hpp"

namespace mp {

namespace {

constexpr std::uint8_t kDone = 0xFF;

/* The flags a player's own copy of a person or pickup sets as its scene ends (local_class in
 * world.hpp; the mission byte of the placed moby, in the record's second word or, for two, its
 * pvars; the same on both discs).
 *   level 1 flag 2: the plumber (774)
 *   level 3 flag 0: Al (909); 2: Helga (890); 3: the infobot (750)
 *   level 4 flag 1: the lieutenant (1190)
 *   level 6 flag 0: the infobots (750) and the shuttle's first ride (1109)
 *   level 7 flag 3: the infobot (750)
 *   level 8 flag 4: the plumber (1283)
 *   level 10 flags 2, 4: the infobots (750)
 *   level 11 flag 3: the resort owner (114)
 *   level 14 flag 2: the morph-o-ray (1354)
 *   level 16 flag 3: the help desk (1377)
 *   level 17 flag 3: the infobot (750) */
struct SceneFlag {
    int level;
    int flag;
};

constexpr SceneFlag kSceneFlags[] = {
    {1, 2}, {3, 0}, {3, 2}, {3, 3}, {4, 1}, {6, 0}, {7, 3}, {8, 4}, {10, 2}, {10, 4}, {11, 3}, {14, 2}, {16, 3}, {17, 3},
};

/* The nanotech upgrades (gSpecialItems 4 and 5: hero_max_health follows them). */
constexpr int kOwnSpecials[] = {4, 5};

template <std::size_t N>
void put_section(Writer& w, int tag, const std::array<std::uint8_t, N>& bytes) {
    w.u8(static_cast<std::uint8_t>(tag));
    const std::size_t length = (N + 7) / 8;
    w.u8(static_cast<std::uint8_t>(length >> 8)).u8(static_cast<std::uint8_t>(length));
    std::uint8_t bits = 0;

    for (std::size_t i = 0; i < N; ++i) {
        bits = static_cast<std::uint8_t>(bits | (bytes[i] != 0 ? 1u << (i % 8) : 0u));

        if (i % 8 == 7 || i + 1 == N) {
            w.u8(bits);
            bits = 0;
        }
    }
}

/* A section's bytes into `out`; false for a length that is not the section's. */
template <std::size_t N>
bool take_section(const Bytes& section, std::array<std::uint8_t, N>& out) {
    if (section.size() != (N + 7) / 8) {
        return false;
    }

    for (std::size_t i = 0; i < N; ++i) {
        out[i] = (section[i / 8] >> (i % 8)) & 1;
    }

    return true;
}

/* In `out`, what `bytes` has now, read through the game's call; false for none. */
template <std::size_t N>
bool read_section(Game& game, int tag, std::array<std::uint8_t, N>& out) {
    Bytes bytes;

    if (!game.shared_read(tag, bytes) || bytes.size() != N) {
        return false;
    }

    std::copy(bytes.begin(), bytes.end(), out.begin());
    return true;
}

template <std::size_t N>
std::size_t count_of(const std::array<std::uint8_t, N>& bytes) {
    return static_cast<std::size_t>(std::count_if(bytes.begin(), bytes.end(), [](std::uint8_t b) { return b != 0; }));
}

std::string count_text(const SharedBits& bits) {
    return std::to_string(count_of(bits.gold_weapons)) + " gold weapons, " + std::to_string(count_of(bits.skills))
           + " skill points, " + std::to_string(count_of(bits.flags)) + " flags, "
           + std::to_string(count_of(bits.specials)) + " special items";
}

}  // namespace

bool scene_flag(int level, int flag) {
    return std::any_of(std::begin(kSceneFlags), std::end(kSceneFlags), [&](const SceneFlag& f) {
        return f.level == level && f.flag == flag;
    });
}

bool own_special(int item) {
    return std::find(std::begin(kOwnSpecials), std::end(kOwnSpecials), item) != std::end(kOwnSpecials);
}

Bytes encode_shared(const SharedBits& bits) {
    Writer w;
    w.u8(kSharedKind);
    w.u8(4);
    put_section(w, kSharedGoldWeapons, bits.gold_weapons);
    put_section(w, kSharedSkillPoints, bits.skills);
    put_section(w, kSharedFlags, bits.flags);
    put_section(w, kSharedSpecials, bits.specials);
    return w.bytes();
}

bool decode_shared(const Bytes& bytes, SharedBits& out) {
    if (bytes.size() < 2 || bytes[0] != kSharedKind) {
        return false;
    }

    Reader r(bytes.data() + 2, bytes.size() - 2);
    SharedBits bits;

    for (std::uint8_t i = 0; i < bytes[1]; ++i) {
        const int tag = r.u8();
        const std::size_t high = r.u8();
        const std::size_t length = high << 8 | r.u8();

        if (!r.ok() || r.left() < length) {
            return false;
        }

        Bytes section(length);

        for (std::uint8_t& b : section) {
            b = r.u8();
        }

        // A wrong length is a build that should not be in the party: nothing of it is taken.
        if ((tag == kSharedGoldWeapons && !take_section(section, bits.gold_weapons))
            || (tag == kSharedSkillPoints && !take_section(section, bits.skills))
            || (tag == kSharedFlags && !take_section(section, bits.flags))
            || (tag == kSharedSpecials && !take_section(section, bits.specials))) {
            return false;
        }
    }

    if (!r.ok()) {
        return false;
    }

    out = bits;
    return true;
}

Bytes encode_mission(int level, const std::uint8_t* row) {
    Writer w;
    w.u8(kMissionKind);
    w.u8(static_cast<std::uint8_t>(level));

    for (std::size_t i = 0; i < kFlagsPerLevel; ++i) {
        w.u8(row[i]);
    }

    return w.bytes();
}

bool decode_mission(const Bytes& bytes, int& level, std::array<std::uint8_t, kFlagsPerLevel>& row) {
    if (bytes.size() != 2 + kFlagsPerLevel || bytes[0] != kMissionKind || bytes[1] >= kFlagLevels) {
        return false;
    }

    level = bytes[1];
    std::copy(bytes.begin() + 2, bytes.end(), row.begin());
    return true;
}

Bytes encode_shared_ask(int level) {
    Writer w;
    w.u8(kSharedAskKind);
    w.u8(static_cast<std::uint8_t>(level));
    return w.bytes();
}

bool decode_shared_ask(const Bytes& bytes, int& level) {
    if (bytes.size() != 2 || bytes[0] != kSharedAskKind || bytes[1] >= kFlagLevels) {
        return false;
    }

    level = bytes[1];
    return true;
}

Shared::Shared(Session& session, Game& game, Coop& coop) : session_(session), game_(game), coop_(coop) {}

void Shared::reset() {
    read_ = false;
    read_ms_ = 0;
    here_ = SharedBits{};
    once_ = SharedBits{};
    party_ = SharedBits{};
    heard_ = false;
    reports_.clear();
    asked_.clear();
    row_ = {};
    row_level_ = -1;
    asked_load_ = false;
    load_ = 0;
    sent_ = SharedBits{};
    sent_any_ = false;
    sent_ms_ = 0;
    members_ = 0;
}

void Shared::handle(std::int32_t sender, const Bytes& bytes) {
    if (!session_.in_party() || bytes.empty()) {
        return;
    }

    const bool from_host = sender == session_.host();

    if (bytes[0] == kSharedKind) {
        SharedBits bits;

        if (!decode_shared(bytes, bits)) {
            return;
        }

        // The host keeps a member's word until its next reading; a member takes the host's only.
        if (session_.is_host() && sender != session_.id()) {
            trace_line("received SHARED from " + std::to_string(sender) + ": " + count_text(bits));
            reports_[sender] = bits;
        } else if (!session_.is_host() && from_host) {
            trace_line("received SHARED from the host: " + count_text(bits));
            party_ = bits;
            heard_ = true;
            read_ms_ = 0;
        }
    } else if (bytes[0] == kMissionKind && !session_.is_host() && from_host) {
        int level = -1;
        std::array<std::uint8_t, kFlagsPerLevel> row{};

        if (decode_mission(bytes, level, row)) {
            trace_line("received MISSION of level " + std::to_string(level));

            for (std::size_t i = 0; i < kFlagsPerLevel; ++i) {
                std::uint8_t& flag = party_.flags[static_cast<std::size_t>(level) * kFlagsPerLevel + i];
                flag = static_cast<std::uint8_t>(flag | (row[i] != 0 ? 1 : 0));
            }

            heard_ = true;
            read_ms_ = 0;
        }
    } else if (bytes[0] == kSharedAskKind && session_.is_host()) {
        int level = -1;

        if (decode_shared_ask(bytes, level)) {
            asked_.insert({sender, level});
        }
    }
}

bool Shared::read() {
    SharedBits bits;

    if (!read_section(game_, kSharedGoldWeapons, bits.gold_weapons)
        || !read_section(game_, kSharedSkillPoints, bits.skills)
        || !read_section(game_, kSharedFlags, bits.flags)
        || !read_section(game_, kSharedSpecials, bits.specials)) {
        return false;
    }

    // The game's 0xFF and anything else non-zero read as one.
    const auto one = [](auto& section) {
        std::transform(section.begin(), section.end(), section.begin(), [](std::uint8_t b) {
            return static_cast<std::uint8_t>(b != 0 ? 1 : 0);
        });
    };
    one(bits.gold_weapons);
    one(bits.skills);
    one(bits.flags);
    one(bits.specials);
    here_ = bits;
    const auto keep = [](auto& all, const auto& now) {
        for (std::size_t i = 0; i < all.size(); ++i) {
            all[i] = static_cast<std::uint8_t>(all[i] | now[i]);
        }
    };
    keep(once_.gold_weapons, bits.gold_weapons);
    keep(once_.skills, bits.skills);
    keep(once_.flags, bits.flags);
    keep(once_.specials, bits.specials);
    return true;
}

bool Shared::merge(const SharedBits& want, int level, bool all_scenes, const std::string& from) {
    bool took = false;

    // Only what this game has never had; what is held back stays in `want` for a later reading.
    const auto fresh = [&](int tag, const auto& asked, const auto& never, auto& have, auto&& allowed) {
        Bytes bytes(asked.size(), 0);
        std::size_t count = 0;

        for (std::size_t i = 0; i < asked.size(); ++i) {
            if (asked[i] != 0 && never[i] == 0 && allowed(i)) {
                bytes[i] = 1;
                ++count;
            }
        }

        if (count == 0 || !game_.shared_give(tag, bytes)) {
            return;
        }

        for (std::size_t i = 0; i < asked.size(); ++i) {
            have[i] = static_cast<std::uint8_t>(have[i] | bytes[i]);
        }

        took = true;
        log("shared progress: section " + std::to_string(tag) + ", " + std::to_string(count) + " new (" + from + ")");
        trace_line("merged section " + std::to_string(tag) + ", " + std::to_string(count) + " new (" + from + ")");
    };
    const auto any = [](std::size_t) { return true; };

    fresh(kSharedGoldWeapons, want.gold_weapons, once_.gold_weapons, here_.gold_weapons, any);
    fresh(kSharedSkillPoints, want.skills, once_.skills, here_.skills, any);
    fresh(kSharedFlags, want.flags, once_.flags, here_.flags, [&](std::size_t i) {
        const int row = static_cast<int>(i / kFlagsPerLevel);
        const int flag = static_cast<int>(i % kFlagsPerLevel);
        return !scene_flag(row, flag) || (!all_scenes && row != level);
    });
    fresh(kSharedSpecials, want.specials, once_.specials, here_.specials, [](std::size_t i) {
        return !own_special(static_cast<int>(i));
    });

    return took;
}

void Shared::send(std::int32_t target, const std::string& to) {
    trace_line("sent SHARED to " + to + ": " + count_text(here_));
    session_.send_data(target, encode_shared(here_));
    sent_ = here_;
    sent_any_ = true;
}

void Shared::tick(std::uint64_t now) {
    // Only while the party plays its game.
    if (!session_.in_party() || !coop_.playing()) {
        if (read_ || heard_ || !reports_.empty()) {
            reset();
        }

        return;
    }

    if (read_ && now - read_ms_ < kProgressReadMs) {
        return;
    }

    if (!read()) {
        return;
    }

    read_ = true;
    read_ms_ = now;
    const GameStatus status = game_.status();
    const int level = in_level(status.phase) ? status.level : -1;

    if (session_.is_host()) {
        // A member's flags: never one of a scene the host's game is still to play.
        for (const auto& report : reports_) {
            merge(report.second, -1, true, "player " + std::to_string(report.first) + "'s");
        }

        reports_.clear();
        const std::size_t members = session_.members().size();

        if (!sent_any_ || here_ != sent_ || members != members_ || now - sent_ms_ >= kProgressResendMs) {
            members_ = members;
            sent_ms_ = now;
            send(kBroadcast, "all");
        }

        // The row of the host's level, at once at a change; and of the level each asker came to.
        if (level >= 0 && static_cast<std::size_t>(level) < kFlagLevels) {
            std::array<std::uint8_t, kFlagsPerLevel> row{};
            const std::size_t first = static_cast<std::size_t>(level) * kFlagsPerLevel;

            for (std::size_t i = 0; i < kFlagsPerLevel; ++i) {
                row[i] = here_.flags[first + i] != 0 ? kDone : 0;
            }

            if (level != row_level_ || row != row_) {
                row_level_ = level;
                row_ = row;
                session_.send_data(kBroadcast, encode_mission(level, row.data()));
                trace_line("sent MISSION of level " + std::to_string(level) + " to all");
            }
        }

        for (const auto& ask : asked_) {
            std::array<std::uint8_t, kFlagsPerLevel> row{};
            const std::size_t first = static_cast<std::size_t>(ask.second) * kFlagsPerLevel;

            for (std::size_t i = 0; i < kFlagsPerLevel; ++i) {
                row[i] = here_.flags[first + i] != 0 ? kDone : 0;
            }

            session_.send_data(ask.first, encode_mission(ask.second, row.data()));
            trace_line("sent MISSION of level " + std::to_string(ask.second) + " to " + std::to_string(ask.first));
        }

        asked_.clear();
        return;
    }

    // A member: what the host's has that this one lacks, a scene's flag of this level left for later.
    if (heard_) {
        merge(party_, level, false, "the host's");
    }

    // Arrived in a level: the host's word on it, now.
    if (level >= 0 && (!asked_load_ || status.loads != load_)) {
        asked_load_ = true;
        load_ = status.loads;
        session_.send_data(session_.host(), encode_shared_ask(level));
    }

    if (!sent_any_ || here_ != sent_ || now - sent_ms_ >= kProgressResendMs) {
        sent_ms_ = now;
        send(session_.host(), "the host");
    }
}

}  // namespace mp
