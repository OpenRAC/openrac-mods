// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The despawn lines of the verbose log (despawn.cpp, trace.hpp's `despawn`): a line for each moby
// that leaves the level in this game, and for each one hidden (or shown again) while it lives, with
// who removed it and why. No socket and no game here: the World gives it each tick's listing and
// what it did itself (the sync's removals), and asks the game how it deleted a moby (the port's
// moby_deletion) as the moby goes.
//
// Who removed it:
//   sync      the World, for a message (KILLED, DESPAWN, DEAD from the host; BROKE from a member;
//             a replayed crate's deadline), with the sender;
//   game      this game: the update of the moby itself or of another (by slot and class), or code
//             outside any moby's update; a death the game recorded (killed in play) is said;
//   mod       a mod's moby_kill that was not the sync's;
//   vanished  gone from the listing, or listed dead, with no deletion seen.
//
// Each line: what (gone, hidden, shown), class, key (-1 spawned), slot, driven (this game poses it
// from the host's), shared (the host's that members pose) or local, who and why, its last place,
// the distance from the local hero and the camera, its state and flags then, and on a member when
// the host last sent it. The verbose log caps them with its other lines (trace.hpp).

#ifndef MULTIPLAYER_DESPAWN_H
#define MULTIPLAYER_DESPAWN_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "coop.hpp"

namespace mp {

// How the game deleted a moby (the SDK's openrac_moby_deletion).
struct MobyDeletion {
    int by = -1;          // Slot of the moby whose update deleted it; -1 for none.
    int by_class = -1;    // Class of that moby; -1 for none.
    bool by_mod = false;  // True when a mod's moby_kill did it.
    int oclass = 0;       // Class of the deleted moby; the world's hook leaves it 0.
};

// The despawn lines of the verbose log. It compares each tick's listing with the last one's and
// writes a line for each moby that is gone, hidden or shown.
//
// The World owns it, sets the four hooks below and calls it on the game's thread.
class DespawnLog {
public:
    // Where this tick's viewers are: the local hero and the camera, when known.
    struct View {
        bool hero = false;               // True when `hero_at` is known.
        float hero_at[3] = {0, 0, 0};    // The local hero's place.
        bool camera = false;             // True when `camera_at` is known.
        float camera_at[3] = {0, 0, 0};  // The camera's place.
    };

    // The World's answers about a slot: driven, shared or local; when the host last sent it (a
    // line's last part, empty for none).
    std::function<std::string(int slot, const MobyState& moby)> role;
    // Returns when the host last sent the moby, as the last part of a line; empty for none.
    std::function<std::string(int slot, const MobyState& moby)> host_seen;
    // The game's deletion of a slot (false: none seen).
    std::function<bool(int slot, MobyDeletion& out)> deletion;
    // Where each line goes; with none, a line is dropped.
    std::function<void(const std::string&)> out;

    // The sync removed (or hid, or showed) the moby in `slot`: `why` (KILLED, DESPAWN...), from
    // `sender` (0: none). Said on the line when the moby goes.
    void note_sync(int slot, const std::string& why, std::int32_t sender);
    // The game recorded the death of the moby in `slot` (killed in play).
    void note_death(int slot);

    // This tick's whole listing (the dead placed ones too); an empty one changes nothing.
    void tick(std::uint64_t now_ms, const std::vector<MobyState>& listing, const View& view);

    // A level load: nothing of the last is compared.
    void reset();

    // For the tests: lines written so far.
    int written() const { return written_; }

private:
    // A sync's reason for removing a moby, and who sent the message.
    struct Note {
        std::string why;          // The message that caused it (KILLED, DESPAWN, ...).
        std::int32_t sender = 0;  // The sender's id; 0 for none.
    };

    // Writes one line for `what` (gone, hidden or shown) of the moby in `slot`. `was` is the moby
    // as last listed, `who_why` the cause and `view` where the hero and the camera are.
    void event(
        std::uint64_t now,
        const std::string& what,
        int slot,
        const MobyState& was,
        const std::string& who_why,
        const View& view
    );

    std::map<int, MobyState> last_;     // The last tick's listing, by slot.
    std::map<int, std::string> roles_;  // How each slot stood last tick (driven, shared or local).
    std::map<int, Note> sync_;          // The sync's removals since the last tick, by slot.
    std::map<int, bool> deaths_;        // Slots whose death the game recorded since the last tick.
    int written_ = 0;                   // Lines written so far.
};

// The who/why of a moby gone (despawn.cpp; for the tests too): the sync's note, else the game's
// deletion (`deleted`), a death recorded, and how it left (listed dead or not listed).
std::string despawn_cause(
    int slot,
    const std::string* sync_why,
    std::int32_t sender,
    const MobyDeletion* deleted,
    bool death,
    bool listed_dead
);

}  // namespace mp

#endif  // MULTIPLAYER_DESPAWN_H
