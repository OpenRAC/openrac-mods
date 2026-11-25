// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Team tools' rescue and rally points, free of the game and the session: everything it needs from
// them comes through ComfortHooks, so tests/comfort_test.cpp runs three players against it with no
// game. A normal lethal hit taken on foot waits at one health for a teammate to hold L3 + R3
// beside the player; the host judges that and tells everyone. The host can also mark its position
// for the group. The wire format is README.md's (Team tools, and the DATA kinds table).

#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "kinds.hpp"
#include "play.hpp"
#include "protocol.hpp"

namespace mp {
// Opaque DATA carried by the existing server and Direct connect; never pose/UDP state. Its value is
// the wire registry's (kinds.hpp: Owner::kComfort, 80 to 83).
constexpr std::uint8_t kComfortKind = kind::kComfort;

// The rules the host sets for Team tools, and the ones a member holds from the host's snapshot.
struct ComfortOptions {
    // Whether co-op rescue is on, and whether rally points are on.
    bool revive = true, rally = true;
    // How long a fallen player waits for rescue, in seconds: 10, 20 or 30.
    int down_seconds = 20;
};

// What the tool knows of one party member's rescue state.
struct ComfortPeer {
    // Whether the member waits for rescue, and whether it holds L3 + R3 now.
    bool down = false, holding = false;
    // The member's fall count: it goes up by one each time the member starts waiting for rescue.
    int life = 0;
    // When the host last heard from the member, in milliseconds.
    std::uint64_t seen = 0;
};

// The host's rally marker: a level and a position in the game's units, for the whole group.
struct RallyPoint {
    // Whether a marker is placed, the level it is in (-1 for none), and its position.
    bool active = false;
    int level = -1;
    float position[3] = {};
};

// What Comfort needs from the game and the session. All of them are called on the game's tick
// thread (the tool runs inside the mod's frame and pad hooks and the session's data callback).
struct ComfortHooks {
    // This player's member id, and the host's.
    std::function<int()> self, host;
    // The party code; empty outside a party.
    std::function<std::string()> room;
    // The ids of the party's members.
    std::function<std::vector<int>()> members;
    // A live pose. include_menu permits a pause page's marker/status, never rescue eligibility.
    std::function<bool(int, Pose&, bool include_menu)> pose;
    // Sends DATA to one member, or to all with kBroadcast.
    std::function<void(int, const Bytes&)> send;
    // Let the original damage handler take the deferred final hit; no health/position writes.
    std::function<void()> expire;
};

// The Team tools' state machine for one player: its own wait for rescue, the host's books of who
// is down and who is rescuing, and the rally marker. The host's copy judges and publishes; a
// member's copy reports to the host and follows its snapshots. Only the game's tick thread uses it.
class Comfort {
public:
    // Makes the tool from its hooks into the game and the session.
    explicit Comfort(ComfortHooks hooks) : h_(std::move(hooks)) {}

    // Advances the tool at time `now` (milliseconds): follows a change of party or host, sends this
    // player's presence report, and (for the host) judges rescues and publishes the snapshot.
    // Called once a game tick.
    void tick(std::uint64_t now);
    // Reads one COMFORT message from `sender`. A message that is malformed, from a non-member, for
    // another party, or from a member where only the host may speak is ignored.
    void handle(int sender, const Bytes& bytes, std::uint64_t now);
    // Returns true to consume a positive normal hit, so the player waits for rescue instead of
    // dying. `damage` is the hit's amount and `on_foot` is false in a vehicle. Instant-kill hazards
    // bypass this game hook, so they never come here.
    bool hit(std::uint64_t now, int damage, bool on_foot);

    // Sets whether this player holds L3 + R3 now; the pad hook calls it each report.
    void holding(bool value) { holding_ = value; }

    // Sets the rules. Only the host's call takes effect, and the rescue window rounds up to 10, 20
    // or 30 seconds.
    void configure(ComfortOptions options);
    // Marks the host's position for the group: the host places it, a member asks the host to. The
    // host takes one request in three seconds, and none with rally points off.
    void request_rally(std::uint64_t now);
    // Removes the rally marker. Only the host's call takes effect.
    void clear_rally();
    // Ends this player's wait for rescue and hands the game the deferred hit. Does nothing when not
    // waiting.
    void give_up();

    // Returns whether this player waits for rescue.
    bool down() const { return down_; }

    // Returns the whole seconds left of this player's wait (rounded up), 0 when not waiting.
    int seconds_left(std::uint64_t now) const;

    // Returns the rules in force: the host's own, or the last ones the host sent.
    const ComfortOptions& options() const { return options_; }

    // Returns the rally marker in force.
    const RallyPoint& rally() const { return rally_; }

    // Returns what is known of each member's rescue state by member id: the host's books, or the
    // host's last snapshot on a member.
    const std::map<int, ComfortPeer>& peers() const { return peers_; }

    // Returns the distance in game units from this player to the rally marker; -1 with no marker,
    // no live pose, or the marker in another level.
    float rally_distance() const;
    // Returns whether a downed teammate is within rescue range, so the page can tell the player to
    // hold L3 + R3.
    bool near_downed() const;

private:
    // A rescuer's hold in progress: the player it is for, which of that player's falls, and when
    // the hold began (milliseconds).
    struct Attempt {
        int target = 0, life = 0;
        std::uint64_t since = 0;
    };

    // Returns whether `id` is a member of the party.
    bool member(int id) const;
    // Reads the pose of member `id` into `out`; false when it is not a member, has no live pose or
    // the pose cannot be trusted. `menus` also accepts a player in a pause page.
    bool pose(int id, Pose& out, bool menus = false) const;
    // Sends this player's presence report to the host, or (the host) records it directly.
    void presence(std::uint64_t now);
    // Publishes the host's snapshot of the settings, the marker and each member's state to all.
    void snapshot(std::uint64_t now);
    // Rescues `target`'s fall number `life`: the host stands this player up or tells that member.
    void grant(int target, int life, std::uint64_t now);
    // Advances every rescuer's hold on the nearest downed player in range, and grants the rescues
    // that were held long enough. The host only.
    void rescue_tick(std::uint64_t now);
    // Records a presence report from `sender` after checking it against the sender's pose; a late
    // report for a fall already rescued is answered with the grant again.
    void accept_presence(int sender, int life, bool down, bool holding, std::uint64_t now);
    // Forgets everything, for a new party, host or member id.
    void reset();
    // The hooks into the game and the session.
    ComfortHooks h_;
    // The rules in force, and the ones this player set as host (kept for when it is host again).
    ComfortOptions options_, local_options_;
    // The rally marker in force.
    RallyPoint rally_;
    // Each member's last known state by id: the host's books, or the host's last snapshot.
    std::map<int, ComfortPeer> peers_;
    // The hold in progress for each rescuer by id. The host only.
    std::map<int, Attempt> attempts_;
    // The fall number last rescued for each member by id, so a late report is not counted again.
    std::map<int, int> granted_;
    // The party code last seen; a change starts the state afresh.
    std::string room_;
    // The host's and this player's ids last seen, this player's fall count, and the level it fell
    // in (-1 for none).
    int host_ = 0, self_ = 0, life_ = 0, down_level_ = -1;
    // Whether this player waits for rescue, whether it holds L3 + R3, and whether a change has not
    // been sent yet.
    bool down_ = false, holding_ = false, dirty_ = true;
    // Times in milliseconds: when the wait ends, when the protection after a rescue ends, when the
    // last presence report and snapshot went out, when the marker expires, when the last marker
    // request was taken, and when the tool last ticked.
    std::uint64_t deadline_ = 0, grace_ = 0, presence_ms_ = 0, snapshot_ms_ = 0, rally_until_ = 0,
                  rally_request_ms_ = 0, last_tick_ = 0;
    // The serial of the last snapshot sent (the host's), and of the last one received, which a
    // replayed snapshot cannot beat.
    std::int64_t serial_ = 0, received_serial_ = -1;
};

// Returns the straight-line distance in game units between two poses: infinity when they are in
// different levels or either cannot be trusted (no health left, a level the game lacks, or a
// coordinate that is not a number).
float comfort_distance(const Pose& a, const Pose& b);
}  // namespace mp
