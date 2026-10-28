/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The despawn log (despawn.cpp; the `despawn_log` setting): a line for each moby that leaves the
 * level in this game, and for each one hidden (or shown again) while it lives, with who removed it
 * and why. No socket and no game here: the World gives it each tick's listing and what it did
 * itself (the sync's removals), and asks the game how it deleted a moby (the port's
 * moby_deletion) as the moby goes.
 *
 * Who removed it:
 *   sync      the World, for a message (KILLED, DESPAWN, DEAD from the host; BROKE from a member;
 *             a replayed crate's deadline), with the sender;
 *   game      this game: the update of the moby itself or of another (by slot and class), or code
 *             outside any moby's update; a death the game recorded (killed in play) is said;
 *   mod       a mod's moby_kill that was not the sync's;
 *   vanished  gone from the listing, or listed dead, with no deletion seen.
 *
 * Each line: what (gone, hidden, shown), class, key (-1 spawned), slot, driven (this game poses it
 * from the host's), shared (the host's that members pose) or local, who and why, its last place,
 * the distance from the local hero and the camera, its state and flags then, and on a member when
 * the host last sent it. At most kDespawnLinesPerSecond lines a second; the rest are counted and
 * said in one line when the second is over.
 */
#ifndef MULTIPLAYER_DESPAWN_H
#define MULTIPLAYER_DESPAWN_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "coop.hpp"

namespace mp {

constexpr int kDespawnLinesPerSecond = 20;

/* How the game deleted a moby (the SDK's openrac_moby_deletion). */
struct MobyDeletion {
    int by = -1;
    int by_class = -1;
    bool by_mod = false;
    int oclass = 0;
};

class DespawnLog {
public:
    /* Where this tick's viewers are, and how the moby stands in the world's sharing. */
    struct View {
        bool hero = false;
        float hero_at[3] = {0, 0, 0};
        bool camera = false;
        float camera_at[3] = {0, 0, 0};
    };

    /* The World's answers about a slot: driven, shared or local; when the host last sent it (a
     * line's last part, empty for none). */
    std::function<std::string(int slot, const MobyState& moby)> role;
    std::function<std::string(int slot, const MobyState& moby)> host_seen;
    /* The game's deletion of a slot (false: none seen). */
    std::function<bool(int slot, MobyDeletion& out)> deletion;
    std::function<void(const std::string&)> out;

    /* The sync removed (or hid, or showed) the moby in `slot`: `why` (KILLED, DESPAWN...), from
     * `sender` (0: none). Said on the line when the moby goes. */
    void note_sync(int slot, const std::string& why, std::int32_t sender);
    /* The game recorded the death of the moby in `slot` (killed in play). */
    void note_death(int slot);

    /* This tick's whole listing (the dead placed ones too); an empty one changes nothing. */
    void tick(std::uint64_t now_ms, const std::vector<MobyState>& listing, const View& view);

    /* A level load: nothing of the last is compared. */
    void reset();

    /* For the tests: lines written and dropped so far. */
    int written() const { return written_; }

    int dropped() const { return dropped_total_; }

private:
    struct Note {
        std::string why;
        std::int32_t sender = 0;
    };

    void event(
        std::uint64_t now,
        const std::string& what,
        int slot,
        const MobyState& was,
        const std::string& who_why,
        const View& view
    );
    void flush(std::uint64_t now);

    std::map<int, MobyState> last_;
    std::map<int, std::string> roles_;
    std::map<int, Note> sync_;
    std::map<int, bool> deaths_;
    std::uint64_t second_ms_ = 0;
    int lines_ = 0;
    int dropped_ = 0;
    int dropped_gone_ = 0;
    int written_ = 0;
    int dropped_total_ = 0;
};

/* The who/why of a moby gone (despawn.cpp; for the tests too): the sync's note, else the game's
 * deletion (`deleted`), a death recorded, and how it left (listed dead or not listed). */
std::string despawn_cause(
    int slot,
    const std::string* sync_why,
    std::int32_t sender,
    const MobyDeletion* deleted,
    bool death,
    bool listed_dead
);

}  // namespace mp

#endif /* MULTIPLAYER_DESPAWN_H */
