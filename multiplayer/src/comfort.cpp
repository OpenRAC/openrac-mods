// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The Team tools' rescue and rally logic: implements comfort.hpp. Each game keeps one Comfort. The
// host collects every member's presence report (life count, down, holding L3 + R3), decides who
// rescues whom from the poses it can see, and publishes a snapshot; a member sends its own report
// and follows the host's snapshot. The wire format is kept to README.md (Team tools and the DATA
// kinds table); the hooks keep it free of the game and the session, which is how
// tests/comfort_test.cpp runs three players against it.

#include "comfort.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mp {
namespace {
// The op byte that follows the party code in a COMFORT message (README: Team tools).
enum Op {
    Presence = 1,
    Snapshot = 2,
    Revive = 3,
    Rally = 4
};

// How long a presence report counts as fresh, and how long a rescuer must hold L3 + R3 beside a
// downed player, both in milliseconds (documented: README, Team tools).
constexpr std::uint64_t Fresh = 1000, Rescue = 3000;

// Returns whether a pose can be trusted: a live hero in one of the game's levels, at coordinates
// that are numbers.
bool finite_pose(const Pose& p) {
    // The game has 19 levels (documented), and a hero with no health left is neither rescued nor a
    // rescuer.
    if (p.level < 0 || p.level >= 19 || p.health <= 0) {
        return false;
    }

    // Every coordinate has to be usable before the pose is trusted.
    for (float x : p.position) {
        // A NaN, an infinity or a coordinate beyond 100000 units (assumed bound) marks a corrupt
        // report.
        if (!std::isfinite(x) || std::abs(x) > 100000) {
            return false;
        }
    }

    return true;
}

// Starts a COMFORT message: the kind, format version 1, the party code and the op, for the caller
// to add the op's fields.
Writer message(int op, const std::string& room) {
    Writer w;
    w.u8(kComfortKind).u8(1).str(room).u8(static_cast<std::uint8_t>(op));
    return w;
}
}  // namespace

float comfort_distance(const Pose& a, const Pose& b) {
    // Poses in different levels, or one that cannot be trusted, are infinitely far apart.
    if (a.level != b.level || !finite_pose(a) || !finite_pose(b)) {
        return std::numeric_limits<float>::infinity();
    }

    float d = 0;

    // The three axes add up to one straight-line distance.
    for (int i = 0; i < 3; ++i) {
        float x = a.position[i] - b.position[i];
        d += x * x;
    }

    return std::sqrt(d);
}

bool Comfort::member(int id) const {
    auto list = h_.members();
    return id > 0 && std::find(list.begin(), list.end(), id) != list.end();
}

bool Comfort::pose(int id, Pose& out, bool menus) const {
    return member(id) && h_.pose(id, out, menus) && finite_pose(out);
}

void Comfort::reset() {
    down_ = holding_ = false;
    life_ = 0;
    deadline_ = grace_ = 0;
    peers_.clear();
    attempts_.clear();
    granted_.clear();
    rally_ = {};
    presence_ms_ = snapshot_ms_ = rally_until_ = rally_request_ms_ = last_tick_ = 0;
    serial_ = 0;
    received_serial_ = -1;
    dirty_ = true;
}

void Comfort::configure(ComfortOptions options) {
    // Only the host sets the rules; a member's copy comes from the host's snapshot.
    if (h_.self() != h_.host()) {
        return;
    }

    // The page offers 10, 20 and 30 seconds: any other value rounds up to the next one.
    options.down_seconds = options.down_seconds <= 10 ? 10 : options.down_seconds <= 20 ? 20 : 30;
    options_ = local_options_ = options;

    // With rally points off, a marker already placed goes too.
    if (!options.rally) {
        rally_ = {};
    }

    dirty_ = true;
}

bool Comfort::hit(std::uint64_t now, int damage, bool on_foot) {
    // Only a normal hit taken on foot in a party of two or more can start a rescue wait.
    if (damage <= 0 || !on_foot || h_.room().empty() || h_.members().size() < 2) {
        return false;
    }

    Pose own;

    // Keep the deferred hit deferred under the pause menu too. Only a fresh normal
    // gameplay hit may start waiting for rescue; scripts/instant kills stay original.
    if (down_ || now < grace_) {
        return pose(h_.self(), own, true);
    }

    // Without a live pose (a scene or a load, say) the hit goes to the game as it is.
    if (!pose(h_.self(), own)) {
        return false;
    }

    // Only a hit at one health would kill, and only with rescue on does it wait for help.
    if (!options_.revive || own.health != 1) {
        return false;
    }

    down_ = true;
    down_level_ = own.level;
    deadline_ = now + options_.down_seconds * 1000u;  // Seconds to milliseconds
    ++life_;
    dirty_ = true;
    presence(now);
    return true;
}

int Comfort::seconds_left(std::uint64_t now) const {
    // The seconds round up, so the player reads 1 until the deadline has passed.
    return down_ && deadline_ > now ? static_cast<int>((deadline_ - now + 999) / 1000) : 0;
}

void Comfort::give_up() {
    // Only a player who waits has a deferred hit to hand to the game.
    if (down_) {
        down_ = false;
        holding_ = false;
        dirty_ = true;
        h_.expire();
    }
}

void Comfort::accept_presence(int sender, int life, bool down, bool holding, std::uint64_t now) {
    // Reports from outside the party, or with a negative life count, are ignored.
    if (!member(sender) || life < 0) {
        return;
    }

    auto old = peers_.find(sender);

    // A life count below the last one seen is an old report arriving late.
    if (old != peers_.end() && life < old->second.life) {
        return;
    }

    Pose p;

    // Without a live pose for the sender there is nothing to check its report against.
    if (!pose(sender, p, true)) {
        return;
    }

    // A one-health player may claim only their own downed state, never another's.
    if (down && p.health != 1) {
        return;
    }

    // The host already rescued this fall, so a late report is answered with the grant again.
    if (down && granted_.count(sender) && granted_[sender] == life) {
        grant(sender, life, now);
        return;
    }

    peers_[sender] = {down, holding, life, now};
    dirty_ = true;
}

void Comfort::presence(std::uint64_t now) {
    // Only a member of the party reports or is recorded.
    if (!member(h_.self())) {
        return;
    }

    // The host records its own report; a member sends its report to the host.
    if (h_.self() == h_.host()) {
        accept_presence(h_.self(), life_, down_, holding_, now);
    } else {
        h_.send(h_.host(), message(Presence, h_.room()).i32(life_).u8(down_).u8(holding_).bytes());
    }

    presence_ms_ = now;
    dirty_ = h_.self() == h_.host();
}

void Comfort::grant(int target, int life, std::uint64_t now) {
    // The host's own rescue takes effect here; any other player is told by message.
    if (target == h_.self()) {
        // Only the fall this grant was for ends, not a later one.
        if (down_ && life == life_) {
            down_ = false;
            grace_ = now + 2000;  // Two seconds of protection (documented)
            dirty_ = true;
        }
    } else {
        h_.send(target, message(Revive, h_.room()).i32(target).i32(life).bytes());
    }

    auto it = peers_.find(target);

    if (it != peers_.end()) {
        it->second.down = false;
    }

    granted_[target] = life;
}

void Comfort::rescue_tick(std::uint64_t now) {
    // Each fresh rescuer holding L3 + R3 looks for the nearest downed player in range.
    for (auto& [rescuer, p] : peers_) {
        Pose from;
        int target = 0;

        // Only a living rescuer who holds the buttons, with a fresh report, can rescue.
        if (!p.down && p.holding && now - p.seen <= Fresh && pose(rescuer, from)) {
            float best = 6.f;  // Range in game units (documented)

            // The nearest fresh downed player within range becomes the target.
            for (const auto& [id, to] : peers_) {
                Pose dest;

                // A target is another player who is down, with a fresh report and a live pose.
                if (id != rescuer && to.down && now - to.seen <= Fresh && pose(id, dest)) {
                    float distance = comfort_distance(from, dest);

                    // Closer than the best so far, which starts at the rescue range.
                    if (distance < best) {
                        best = distance;
                        target = id;
                    }
                }
            }
        }

        // Nobody in reach: the hold starts over the next time someone is.
        if (!target) {
            attempts_.erase(rescuer);
            continue;
        }

        auto& a = attempts_[rescuer];
        int life = peers_.at(target).life;

        // A new target or a new fall restarts the hold's clock.
        if (a.target != target || a.life != life) {
            a = {target, life, now};
        }

        // Held long enough: the host grants the rescue.
        if (now - a.since >= Rescue) {
            grant(target, life, now);
            attempts_.erase(rescuer);
            dirty_ = true;
        }
    }
}

void Comfort::snapshot(std::uint64_t now) {
    Writer w = message(Snapshot, h_.room());
    w.i64(++serial_)
        .u8(options_.revive)
        .u8(options_.rally)
        .u8(static_cast<std::uint8_t>(options_.down_seconds));
    w.u8(rally_.active);

    // The marker's level and place go only while one is set.
    if (rally_.active) {
        w.i32(rally_.level);

        // Coordinates go as whole hundredths of a unit; the reader divides them by 100.
        for (float x : rally_.position) {
            w.i32(static_cast<int>(std::lround(x * 100)));
        }
    }

    w.u8(static_cast<std::uint8_t>(peers_.size()));

    // Each member's life count and whether it is down; a stale report counts as up.
    for (const auto& [id, p] : peers_) {
        w.i32(id).i32(p.life).u8(p.down && now - p.seen <= Fresh);
    }

    h_.send(kBroadcast, w.bytes());
    snapshot_ms_ = now;
    dirty_ = false;
}

void Comfort::request_rally(std::uint64_t now) {
    // Rally points off, or no party, means no request.
    if (!options_.rally || h_.room().empty()) {
        return;
    }

    // A member asks the host, which owns the marker; no position is sent.
    if (h_.self() != h_.host()) {
        h_.send(h_.host(), message(Rally, h_.room()).bytes());
        return;
    }

    // The host takes one marker request in three seconds.
    if (rally_request_ms_ && now - rally_request_ms_ < 3000) {
        return;
    }

    Pose own;

    // The host's own pose is what the marker is placed at.
    if (!pose(h_.self(), own, true)) {
        return;
    }

    rally_.active = true;
    rally_.level = own.level;
    std::copy(own.position, own.position + 3, rally_.position);
    rally_until_ = now + 90000;  // The marker lasts 90 seconds (documented)
    rally_request_ms_ = now;
    dirty_ = true;
}

void Comfort::clear_rally() {
    // Only the host owns the marker.
    if (h_.self() == h_.host()) {
        rally_ = {};
        dirty_ = true;
    }
}

float Comfort::rally_distance() const {
    Pose own, dest;

    // No marker, or no live pose of this player, gives no distance.
    if (!rally_.active || !pose(h_.self(), own, true)) {
        return -1;
    }

    // The marker as a pose, so the same distance function measures it.
    dest = own;
    dest.level = rally_.level;
    std::copy(rally_.position, rally_.position + 3, dest.position);
    float d = comfort_distance(own, dest);
    // A marker in another level has no distance (infinite), which reads as none.
    return std::isfinite(d) ? d : -1;
}

bool Comfort::near_downed() const {
    Pose own;

    // Without a live pose of this player nothing is near.
    if (!pose(h_.self(), own)) {
        return false;
    }

    // Look for another player who is down and close enough to help.
    for (const auto& [id, p] : peers_) {
        Pose dest;

        // Down, with a live pose, and inside the rescue range of six units (documented).
        if (id != h_.self() && p.down && pose(id, dest) && comfort_distance(own, dest) < 6) {
            return true;
        }
    }

    return false;
}

void Comfort::tick(std::uint64_t now) {
    const auto room = h_.room();
    const int host = h_.host();

    // A new party, host or member id leaves the old state belonging to another game: start afresh.
    if (room != room_ || host != host_ || h_.self() != self_) {
        reset();
        room_ = room;
        host_ = host;
        self_ = h_.self();

        // The host keeps its own settings; a member's rescue stays off until the host's snapshot
        // arrives.
        if (h_.self() == host) {
            options_ = local_options_;
        } else {
            options_.revive = false;
        }
    }

    // Outside a party the tool is idle.
    if (room.empty() || !member(h_.self())) {
        // A wait that was on when the party ended is dropped.
        if (down_) {
            reset();
        }

        return;
    }

    // A suspended process or long frame cannot count as continuous rescue time.
    if (last_tick_ && now - last_tick_ > 500) {
        attempts_.clear();
    }

    last_tick_ = now;
    Pose own;
    // A scene/load can temporarily make the hero pose unavailable. Keep the deferred lethal hit
    // pending through that gap; clear it only when the game can report a changed or dead hero.
    const bool observed = member(h_.self()) && h_.pose(h_.self(), own, true);
    const bool alive = observed && finite_pose(own);

    // The hero died, changed level or healed past one health, so the wait is over.
    if (down_ && observed && (own.health <= 0 || own.level != down_level_ || own.health > 1)) {
        down_ = false;
        dirty_ = true;
    }

    // The wait also ends when rescue is turned off, the teammates are gone or the window ran out,
    // once play has resumed; the game then gets the deferred hit.
    if (down_ && (!options_.revive || h_.members().size() < 2 || now >= deadline_)
        && pose(h_.self(), own)) {
        give_up();
    }

    // The presence report goes out on a change, and at least every 200 ms.
    if (dirty_ || now - presence_ms_ >= 200) {
        presence(now);
    }

    // Only the host judges rescues and publishes the snapshot.
    if (h_.self() != host) {
        return;
    }

    // Players who left the party are dropped from the host's books.
    for (auto it = peers_.begin(); it != peers_.end();) {
        // A member that is gone leaves no hold or grant behind.
        if (!member(it->first)) {
            attempts_.erase(it->first);
            granted_.erase(it->first);
            it = peers_.erase(it);
            dirty_ = true;
        } else {
            ++it;
        }
    }

    // With rescue off, any hold in progress is dropped.
    if (options_.revive) {
        rescue_tick(now);
    } else {
        attempts_.clear();
    }

    // The marker ends at its time limit, when the host stops playing or changes level.
    if (rally_.active && (now >= rally_until_ || !alive || own.level != rally_.level)) {
        rally_ = {};
        dirty_ = true;
    }

    // The snapshot goes out on a change, and at least every 500 ms.
    if (dirty_ || now - snapshot_ms_ >= 500) {
        snapshot(now);
    }
}

void Comfort::handle(int sender, const Bytes& bytes, std::uint64_t now) {
    // Anything that is not a COMFORT message of a sane size from a member of this party is dropped.
    if (h_.room().empty() || bytes.size() < 3 || bytes.size() > 256 || bytes[0] != kComfortKind
        || !member(sender)) {
        return;
    }

    Reader r(bytes.data() + 1, bytes.size() - 1);

    // Only format version 1 is read.
    if (r.u8() != 1) {
        return;
    }

    // A message for another party code is ignored.
    if (r.str() != h_.room()) {
        return;
    }

    int op = r.u8();

    // The host reads only presence reports and rally requests from members.
    if (h_.self() == h_.host()) {
        // A presence report: the sender's life count, and whether it is down and holding.
        if (op == Presence) {
            int life = r.i32();
            int down = r.u8(), holding = r.u8();

            // The flags are 0 or 1 and nothing trails the fields.
            if (r.ok() && !r.left() && down <= 1 && holding <= 1) {
                accept_presence(sender, life, down, holding, now);
            }
        } else if (op == Rally && r.ok() && !r.left()) {
            // A rally request has no fields.
            request_rally(now);
        }

        return;
    }

    // A member takes orders from the host only.
    if (sender != h_.host()) {
        return;
    }

    // A grant of a rescue to one player for one of its falls.
    if (op == Revive) {
        int target = r.i32(), life = r.i32();
        Pose own;

        // The grant must be for this player's current fall, at the same level and one health.
        if (r.ok() && !r.left() && target == h_.self() && down_ && life == life_
            && pose(h_.self(), own, true) && own.level == down_level_ && own.health == 1) {
            down_ = false;
            grace_ = now + 2000;  // Two seconds of protection (documented)
            dirty_ = true;
        }

        return;
    }

    // Anything left is a snapshot; other ops are ignored.
    if (op != Snapshot) {
        return;
    }

    const auto serial = r.i64();
    int revive = r.u8(), rally_enabled = r.u8(), seconds = r.u8(), marked = r.u8();

    // A snapshot no newer than the last, or with a field out of range, is dropped.
    if (serial <= received_serial_ || revive > 1 || rally_enabled > 1 || marked > 1
        || (seconds != 10 && seconds != 20 && seconds != 30)) {
        return;
    }

    RallyPoint mark;

    // A marker comes with its level and its position.
    if (marked) {
        mark.active = true;
        mark.level = r.i32();

        // The level must be one of the game's 19 (documented).
        if (mark.level < 0 || mark.level >= 19) {
            return;
        }

        // Each coordinate is whole hundredths of a unit within a sane bound.
        for (float& x : mark.position) {
            auto n = r.i32();

            // A coordinate beyond 100000 units (10000000 hundredths, assumed bound) is refused.
            if (n < -10000000 || n > 10000000) {
                return;
            }

            x = n / 100.f;
        }
    }

    int count = r.u8();

    // A count above the party's size limit cannot be true.
    if (count > static_cast<int>(kMaxPartySize)) {
        return;
    }

    std::map<int, ComfortPeer> peers;

    // Each entry is a member's life count and whether it is down.
    for (int i = 0; i < count; ++i) {
        int id = r.i32(), life = r.i32(), down = r.u8();

        // An entry for a non-member, a repeat, a negative life or a bad flag voids the snapshot.
        if (!member(id) || peers.count(id) || life < 0 || down > 1) {
            return;
        }

        peers[id] = {down != 0, false, life, now};
    }

    // A truncated snapshot, or bytes left over, is rejected as a whole.
    if (!r.ok() || r.left()) {
        return;
    }

    received_serial_ = serial;
    options_ = {revive != 0, rally_enabled != 0, seconds};
    rally_ = mark;
    peers_ = std::move(peers);
}
}  // namespace mp
