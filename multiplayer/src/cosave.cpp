/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The party's saves (cosave.hpp).
 */
#include "cosave.hpp"

#include <algorithm>
#include <sstream>

namespace mp {

std::string note_text(const CoNote& note) {
    std::string host = note.host;
    host.erase(std::remove_if(host.begin(), host.end(), [](char c) { return c == '\n' || c == '\r'; }),
               host.end());
    return "format=1\nkey=" + std::to_string(note.key) + "\nsaved=" + std::to_string(note.saved)
           + "\nhost=" + host + "\n";
}

bool parse_note(const std::string& text, CoNote& out) {
    std::istringstream in(text);
    std::string line;
    CoNote note;
    bool format = false;

    while (std::getline(in, line)) {
        const std::size_t equals = line.find('=');

        if (equals == std::string::npos) {
            continue;
        }

        const std::string name = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);

        try {
            if (name == "format") {
                format = value == "1";
            } else if (name == "key") {
                note.key = static_cast<std::uint32_t>(std::stoul(value));
            } else if (name == "saved") {
                note.saved = std::stoll(value);
            } else if (name == "host") {
                note.host = value;
            }
        } catch (const std::exception&) {
            return false;
        }
    }

    if (!format || note.key == 0) {
        return false;
    }

    out = note;
    return true;
}

int pick_slot(const std::vector<CoNote>& notes, std::uint32_t key) {
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (key != 0 && notes[i].key == key) {
            return static_cast<int>(i);
        }
    }

    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (notes[i].key == 0) {
            return static_cast<int>(i);
        }
    }

    const auto oldest = std::min_element(notes.begin(), notes.end(), [](const CoNote& a, const CoNote& b) {
        return a.saved < b.saved;
    });
    return oldest == notes.end() ? 0 : static_cast<int>(oldest - notes.begin());
}

std::string coop_row(int slot, const std::string& save, const CoNote& note, bool there) {
    const std::string name = "Co-op " + std::to_string(slot + 1) + ": ";

    if (!there) {
        return name + "empty";
    }

    return name + save + (note.host.empty() ? std::string() : ", " + note.host + "'s party");
}

CoSave::CoSave(Session& session, Game& game, std::function<std::uint64_t()> clock)
    : session_(session), game_(game), clock_(std::move(clock)) {}

void CoSave::choose_new() {
    continuing_ = -1;
    chosen_key_ = 0;
}

void CoSave::choose_slot(int slot) {
    continuing_ = slot;
    CoNote note;
    chosen_key_ = parse_note(game_.card_note(slot), note) ? note.key : 0;
}

std::string CoSave::host_name() const {
    for (const Member& m : session_.members()) {
        if (m.id == session_.host()) {
            return m.name;
        }
    }

    return {};
}

void CoSave::ensure_key() {
    // The co-op save's it goes on from, else a new one (never 0: no party).
    if (key_ == 0) {
        key_ = chosen_key_ != 0 ? chosen_key_
                                : static_cast<std::uint32_t>((clock_() * 2654435761u) ^ game_.wall_seconds()) | 1u;
    }
}

void CoSave::announce(std::int32_t target) {
    if (!game_.has_card_filter()) {
        return;
    }

    ensure_key();
    session_.send_data(
        target,
        Writer()
            .u8(kind::kCoParty)
            .i32(static_cast<std::int32_t>(key_))
            .bytes()
    );
}

void CoSave::engage() {
    if (refused_ || !game_.has_card_filter()) {
        return;
    }

    std::vector<CoNote> notes(kCoSlots);

    for (int slot = 0; slot < kCoSlots; ++slot) {
        CoNote note;

        if (parse_note(game_.card_note(slot), note)) {
            notes[static_cast<std::size_t>(slot)] = note;
        }
    }

    ensure_key();
    const int slot = session_.is_host() && continuing_ >= 0 ? continuing_ : pick_slot(notes, key_);

    if (!game_.card_filter(kCardRedirect)) {
        // No write of the party's goes anywhere it should not: there is no saving at all.
        refused_ = true;
        return;
    }

    game_.card_slot(slot);
    on_ = true;
    slot_ = slot;
    copied_ = false;
    last_ = false;
    saves_seen_ = game_.card_saves();
    host_ = host_name();
    write_note();
}

void CoSave::write_note() {
    CoNote note;
    note.key = key_;
    note.saved = game_.wall_seconds();
    note.host = host_;
    game_.card_note_write(slot_, note_text(note));
}

bool CoSave::write_copy(const Bytes& save) {
    if (save.empty() || !game_.save_usable(save) || !game_.card_write(slot_, save)) {
        return false;
    }

    copied_ = true;
    return true;
}

void CoSave::starting(const Bytes& save) {
    if (!on_) {
        engage();
    }

    if (on_) {
        copied_ = false;
        write_copy(save);
    }
}

void CoSave::broadcast(const Bytes& save) {
    if (save.empty() || !session_.in_party()) {
        return;
    }

    ++tag_;
    acked_.clear();
    sent_to_ = session_.members().empty() ? 0 : session_.members().size() - 1;
    const int total = static_cast<int>((save.size() + kChunkBytes - 1) / kChunkBytes);

    for (int index = 0; index < total; ++index) {
        const std::size_t at = static_cast<std::size_t>(index) * kChunkBytes;
        const std::size_t n = std::min(kChunkBytes, save.size() - at);
        session_.send_data(
            kBroadcast,
            Writer()
                .u8(kind::kCoSave)
                .u8(tag_)
                .i32(total)
                .i32(index)
                .raw(Bytes(save.begin() + static_cast<std::ptrdiff_t>(at),
                           save.begin() + static_cast<std::ptrdiff_t>(at + n)))
                .bytes()
        );
    }
}

void CoSave::saved(std::uint64_t now) {
    saved_until_ = now + kCoSavedMs;
    write_note();

    if (session_.is_host()) {
        broadcast(game_.card_read(slot_));
    }
}

bool CoSave::save_party() {
    const std::uint64_t now = clock_();

    if (!on_ || !session_.in_party() || (asked_ms_ != 0 && now - asked_ms_ < kCoSaveEveryMs)) {
        return false;
    }

    asked_ms_ = now;

    if (!session_.is_host()) {
        session_.send_data(session_.host(), Writer().u8(kind::kSaveNow).bytes());
        return true;
    }

    // The game's state now; in a phase with none, what the slot last had.
    const Bytes live = game_.capture();

    if (write_copy(live)) {
        saved_until_ = now + kCoSavedMs;
        write_note();
        broadcast(live);
        return true;
    }

    const Bytes kept = game_.card_read(slot_);

    if (kept.empty()) {
        return false;
    }

    saved_until_ = now + kCoSavedMs;
    broadcast(kept);
    return true;
}

void CoSave::leaving() {
    if (!on_ || last_) {
        return;
    }

    last_ = true;

    if (!in_level(game_.status().phase)) {
        return;
    }

    const Bytes live = game_.capture();

    if (write_copy(live)) {
        write_note();

        if (session_.is_host()) {
            broadcast(live);
        }
    }
}

void CoSave::take_piece(Reader& r) {
    const std::uint8_t tag = r.u8();
    const int total = r.i32();
    const int index = r.i32();
    const Bytes piece = r.rest();
    const std::size_t most = (kMaxSaveBytes + kChunkBytes - 1) / kChunkBytes;

    if (!r.ok() || total <= 0 || static_cast<std::size_t>(total) > most || index < 0 || index >= total
        || piece.empty() || piece.size() > kChunkBytes
        || (index + 1 < total && piece.size() != kChunkBytes)) {
        return;
    }

    // A new save takes the place of one that never came whole.
    if (!receiving_ || tag != in_tag_ || total != in_total_) {
        receiving_ = true;
        in_tag_ = tag;
        in_total_ = total;
        in_got_.assign(static_cast<std::size_t>(total), false);
        in_bytes_.assign(static_cast<std::size_t>(total) * kChunkBytes, 0);
        in_size_ = 0;
    }

    const std::size_t at = static_cast<std::size_t>(index) * kChunkBytes;

    if (in_got_[static_cast<std::size_t>(index)]) {
        return;
    }

    in_got_[static_cast<std::size_t>(index)] = true;
    std::copy(piece.begin(), piece.end(), in_bytes_.begin() + static_cast<std::ptrdiff_t>(at));
    in_size_ = std::max(in_size_, at + piece.size());

    if (std::find(in_got_.begin(), in_got_.end(), false) != in_got_.end()) {
        return;
    }

    receiving_ = false;
    in_bytes_.resize(in_size_);
    const bool ok = on_ && write_copy(in_bytes_);
    in_bytes_.clear();

    if (ok) {
        const std::uint64_t now = clock_();
        saved_until_ = now + kCoSavedMs;
        write_note();
    }

    session_.send_data(
        session_.host(), Writer().u8(kind::kCoSaveAck).u8(tag).u8(ok ? 1 : 0).bytes()
    );
}

void CoSave::handle(std::int32_t sender, const Bytes& bytes) {
    if (bytes.empty()) {
        return;
    }

    Reader r(bytes.data() + 1, bytes.size() - 1);

    switch (bytes[0]) {
        case kind::kCoSave:
            if (!session_.is_host() && sender == session_.host()) {
                take_piece(r);
            }

            break;
        case kind::kCoParty:
            if (!session_.is_host() && sender == session_.host()) {
                const std::uint32_t key = static_cast<std::uint32_t>(r.i32());

                if (r.ok() && key != 0 && !on_) {
                    key_ = key;
                }
            }

            break;
        case kind::kCoSaveAck:
            if (session_.is_host()) {
                const std::uint8_t tag = r.u8();
                const bool ok = r.u8() != 0;

                if (r.ok() && ok && tag == tag_
                    && std::find(acked_.begin(), acked_.end(), sender) == acked_.end()) {
                    acked_.push_back(sender);
                }
            }

            break;
        case kind::kSaveNow:
            if (session_.is_host()) {
                save_party();
            }

            break;
        default:
            break;
    }
}

void CoSave::tick(std::uint64_t now, bool playing) {
    if (!game_.has_card_filter()) {
        return;
    }

    const bool party = playing && session_.in_party();

    // A party's game with no start seen here (it was on when this game came back to it).
    if (party && !on_) {
        engage();
    }

    if (!on_) {
        return;
    }

    const int phase = game_.status().phase;

    if (party) {
        last_ = false;

        if (in_level(phase)) {
            // A new game's first copy; a member's save that has not come whole must not stop it.
            if (!copied_) {
                write_copy(game_.capture());
            }

            const std::uint32_t saves = game_.card_saves();

            if (saves != saves_seen_) {
                saves_seen_ = saves;
                saved(now);
            }
        }

        return;
    }

    // The party went (or its game did): the game's progress is still the party's until the title.
    if (in_level(phase)) {
        leaving();
    } else if ((phase == kPhaseFrontEnd || phase == kPhaseNone) && game_.card_filter(kCardPass)) {
        on_ = false;
        slot_ = -1;
        key_ = 0;
        receiving_ = false;
        in_bytes_.clear();
        acked_.clear();
        sent_to_ = 0;
        continuing_ = -1;
        chosen_key_ = 0;
    }
}

std::string CoSave::where() const {
    return on_ ? "Saving in Co-op " + std::to_string(slot_ + 1) : std::string();
}

std::string CoSave::banner(std::uint64_t now) const {
    return now < saved_until_ ? "Party saved in Co-op " + std::to_string(slot_ + 1) : std::string();
}

}  // namespace mp
