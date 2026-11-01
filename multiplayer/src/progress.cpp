/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The party's shared progress (progress.hpp).
 */
#include "progress.hpp"

#include <algorithm>

namespace mp {

namespace {

template <std::size_t N>
void put_bytes(Writer& w, const std::array<std::uint8_t, N>& bytes) {
    w.u8(static_cast<std::uint8_t>(N));

    for (const std::uint8_t b : bytes) {
        w.u8(b != 0 ? 1 : 0);
    }
}

/* A count and its bytes into `out`: those past what `out` holds left out, those it holds beyond
 * the count left as they are (an older or newer mod's lists). */
template <std::size_t N>
void take_bytes(Reader& r, std::array<std::uint8_t, N>& out) {
    const std::uint8_t count = r.u8();

    for (std::size_t i = 0; i < count && r.ok(); ++i) {
        const std::uint8_t b = r.u8();

        if (i < N) {
            out[i] = b != 0 ? 1 : 0;
        }
    }
}

/* The ids `a` has and `b` has not, as text ("3 7 12"; "" for none). */
template <std::size_t N>
std::string more(const std::array<std::uint8_t, N>& a, const std::array<std::uint8_t, N>& b) {
    std::string text;

    for (std::size_t i = 0; i < N; ++i) {
        if (a[i] != 0 && b[i] == 0) {
            text += (text.empty() ? "" : " ") + std::to_string(i);
        }
    }

    return text;
}

std::string what_more(const ProgressBits& a, const ProgressBits& b) {
    std::string text;
    const std::string items = more(a.items, b.items);
    const std::string gold = more(a.gold, b.gold);
    const std::string planets = more(a.planets, b.planets);
    text += items.empty() ? "" : " items [" + items + "]";
    text += gold.empty() ? "" : " gold bolts [" + gold + "]";
    text += planets.empty() ? "" : " planets [" + planets + "]";
    return text.empty() ? " nothing new" : text;
}

}  // namespace

Bytes encode_progress(const ProgressBits& bits) {
    Writer w;
    w.u8(kProgressKind);
    put_bytes(w, bits.items);
    put_bytes(w, bits.gold);
    put_bytes(w, bits.planets);
    return w.bytes();
}

bool decode_progress(const Bytes& bytes, ProgressBits& out) {
    if (bytes.empty() || bytes[0] != kProgressKind) {
        return false;
    }

    Reader r(bytes.data() + 1, bytes.size() - 1);
    ProgressBits bits;
    take_bytes(r, bits.items);
    take_bytes(r, bits.gold);
    take_bytes(r, bits.planets);

    if (!r.ok()) {
        return false;
    }

    out = bits;
    return true;
}

Progress::Progress(Session& session, Game& game, Coop& coop) : session_(session), game_(game), coop_(coop) {}

void Progress::reset() {
    read_ = false;
    read_ms_ = 0;
    here_ = ProgressBits{};
    party_ = ProgressBits{};
    heard_ = false;
    reports_.clear();
    sent_ = ProgressBits{};
    sent_any_ = false;
    sent_ms_ = 0;
    members_ = 0;
}

void Progress::handle(std::int32_t sender, const Bytes& bytes) {
    ProgressBits bits;

    if (!session_.in_party() || !decode_progress(bytes, bits)) {
        return;
    }

    // The host keeps a member's word until its game can take it; a member takes the host's only.
    if (session_.is_host() && sender != session_.id()) {
        trace_line("received PROGRESS from " + std::to_string(sender) + ":" + what_more(bits, here_));
        reports_[sender] = bits;
    } else if (!session_.is_host() && sender == session_.host()) {
        trace_line("received PROGRESS from the host:" + what_more(bits, here_));
        party_ = bits;
        heard_ = true;
    }
}

bool Progress::give(const ProgressBits& want, const std::string& from) {
    bool gave = false;

    for (std::size_t i = 1; i < kProgressItems; ++i) {
        if (want.items[i] != 0 && here_.items[i] == 0 && game_.give_item(static_cast<int>(i))) {
            here_.items[i] = 1;
            gave = true;
            log("given item " + std::to_string(i) + " (" + from + ")");
            trace_line("given item " + std::to_string(i) + " (" + from + ")");
        }
    }

    for (std::size_t i = 0; i < kProgressGoldBolts; ++i) {
        if (want.gold[i] != 0 && here_.gold[i] == 0 && game_.give_gold_bolt(static_cast<int>(i))) {
            here_.gold[i] = 1;
            gave = true;
            log("given gold bolt " + std::to_string(i % 4) + " of level " + std::to_string(i / 4) + " (" + from + ")");
            trace_line("given gold bolt " + std::to_string(i) + " (" + from + ")");
        }
    }

    for (std::size_t i = 0; i < kProgressPlanets; ++i) {
        if (want.planets[i] != 0 && here_.planets[i] == 0 && game_.unlock_planet(static_cast<int>(i))) {
            here_.planets[i] = 1;
            gave = true;
            log("given planet " + std::to_string(i) + " (" + from + ")");
            trace_line("given planet " + std::to_string(i) + " (" + from + ")");
        }
    }

    return gave;
}

void Progress::send(std::int32_t target, const ProgressBits& bits, const std::string& to) {
    trace_line("sent PROGRESS to " + to + ":" + what_more(bits, sent_any_ ? sent_ : ProgressBits{}));
    session_.send_data(target, encode_progress(bits));
    sent_ = bits;
    sent_any_ = true;
}

void Progress::tick(std::uint64_t now) {
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

    ProgressBits bits;

    if (!game_.progress(bits)) {
        return;
    }

    read_ = true;
    read_ms_ = now;
    here_ = bits;

    // What another game has is given here only while the player is in control.
    const bool control = game_.status().phase == kPhasePlaying;

    if (session_.is_host()) {
        // The members' words: what they have that this game lacks.
        for (auto it = reports_.begin(); it != reports_.end() && control;) {
            give(it->second, "player " + std::to_string(it->first) + "'s");
            it = reports_.erase(it);
        }

        // The party's progress (this game's) to everyone: at each change, at each member that came,
        // and now and then.
        const std::size_t members = session_.members().size();

        if (!sent_any_ || here_ != sent_ || members != members_ || now - sent_ms_ >= kProgressResendMs) {
            members_ = members;
            sent_ms_ = now;
            send(kBroadcast, here_, "all");
        }

        return;
    }

    // A member: what the host's game has that this one lacks, then what this one has to the host.
    if (heard_ && (control || coop_.quiet_gifts())) {
        give(party_, coop_.quiet_gifts() ? "the host's, quietly" : "the host's");
    }

    if (!sent_any_ || here_ != sent_ || now - sent_ms_ >= kProgressResendMs) {
        sent_ms_ = now;
        send(session_.host(), here_, "the host");
    }
}

}  // namespace mp
