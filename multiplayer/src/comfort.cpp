// SPDX-License-Identifier: GPL-3.0-or-later
#include "comfort.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mp {
namespace {
enum Op {
    Presence = 1,
    Snapshot = 2,
    Revive = 3,
    Rally = 4
};

constexpr std::uint64_t Fresh = 1000, Rescue = 3000;

bool finite_pose(const Pose& p) {
    if (p.level < 0 || p.level >= 19 || p.health <= 0) {
        return false;
    }
    for (float x : p.position) {
        if (!std::isfinite(x) || std::abs(x) > 100000) {
            return false;
        }
    }
    return true;
}

Writer message(int op, const std::string& room) {
    Writer w;
    w.u8(kComfortKind).u8(1).str(room).u8(static_cast<std::uint8_t>(op));
    return w;
}
}  // namespace

float comfort_distance(const Pose& a, const Pose& b) {
    if (a.level != b.level || !finite_pose(a) || !finite_pose(b)) {
        return std::numeric_limits<float>::infinity();
    }
    float d = 0;
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
    if (h_.self() != h_.host()) {
        return;
    }
    options.down_seconds = options.down_seconds <= 10 ? 10 : options.down_seconds <= 20 ? 20 : 30;
    options_ = local_options_ = options;
    if (!options.rally) {
        rally_ = {};
    }
    dirty_ = true;
}

bool Comfort::hit(std::uint64_t now, int damage, bool on_foot) {
    if (damage <= 0 || !on_foot || h_.room().empty() || h_.members().size() < 2) {
        return false;
    }
    Pose own;
    // Keep the deferred hit deferred under the pause menu too. Only a fresh normal
    // gameplay hit may start waiting for rescue; scripts/instant kills stay original.
    if (down_ || now < grace_) {
        return pose(h_.self(), own, true);
    }
    if (!pose(h_.self(), own)) {
        return false;
    }
    if (!options_.revive || own.health != 1) {
        return false;
    }
    down_ = true;
    down_level_ = own.level;
    deadline_ = now + options_.down_seconds * 1000u;
    ++life_;
    dirty_ = true;
    presence(now);
    return true;
}

int Comfort::seconds_left(std::uint64_t now) const {
    return down_ && deadline_ > now ? static_cast<int>((deadline_ - now + 999) / 1000) : 0;
}

void Comfort::give_up() {
    if (down_) {
        down_ = false;
        holding_ = false;
        dirty_ = true;
        h_.expire();
    }
}

void Comfort::accept_presence(int sender, int life, bool down, bool holding, std::uint64_t now) {
    if (!member(sender) || life < 0) {
        return;
    }
    auto old = peers_.find(sender);
    if (old != peers_.end() && life < old->second.life) {
        return;
    }
    Pose p;
    if (!pose(sender, p, true)) {
        return;
    }
    // A one-health player may claim only their own downed state, never another's.
    if (down && p.health != 1) {
        return;
    }
    if (down && granted_.count(sender) && granted_[sender] == life) {
        grant(sender, life, now);
        return;
    }
    peers_[sender] = {down, holding, life, now};
    dirty_ = true;
}

void Comfort::presence(std::uint64_t now) {
    if (!member(h_.self())) {
        return;
    }
    if (h_.self() == h_.host()) {
        accept_presence(h_.self(), life_, down_, holding_, now);
    } else {
        h_.send(h_.host(), message(Presence, h_.room()).i32(life_).u8(down_).u8(holding_).bytes());
    }
    presence_ms_ = now;
    dirty_ = h_.self() == h_.host();
}

void Comfort::grant(int target, int life, std::uint64_t now) {
    if (target == h_.self()) {
        if (down_ && life == life_) {
            down_ = false;
            grace_ = now + 2000;
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
    for (auto& [rescuer, p] : peers_) {
        Pose from;
        int target = 0;
        if (!p.down && p.holding && now - p.seen <= Fresh && pose(rescuer, from)) {
            float best = 6.f;
            for (const auto& [id, to] : peers_) {
                Pose dest;
                if (id != rescuer && to.down && now - to.seen <= Fresh && pose(id, dest)) {
                    float distance = comfort_distance(from, dest);
                    if (distance < best) {
                        best = distance;
                        target = id;
                    }
                }
            }
        }
        if (!target) {
            attempts_.erase(rescuer);
            continue;
        }
        auto& a = attempts_[rescuer];
        int life = peers_.at(target).life;
        if (a.target != target || a.life != life) {
            a = {target, life, now};
        }
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
    if (rally_.active) {
        w.i32(rally_.level);
        for (float x : rally_.position) {
            w.i32(static_cast<int>(std::lround(x * 100)));
        }
    }
    w.u8(static_cast<std::uint8_t>(peers_.size()));
    for (const auto& [id, p] : peers_) {
        w.i32(id).i32(p.life).u8(p.down && now - p.seen <= Fresh);
    }
    h_.send(kBroadcast, w.bytes());
    snapshot_ms_ = now;
    dirty_ = false;
}

void Comfort::request_rally(std::uint64_t now) {
    if (!options_.rally || h_.room().empty()) {
        return;
    }
    if (h_.self() != h_.host()) {
        h_.send(h_.host(), message(Rally, h_.room()).bytes());
        return;
    }
    if (rally_request_ms_ && now - rally_request_ms_ < 3000) {
        return;
    }
    Pose own;
    if (!pose(h_.self(), own, true)) {
        return;
    }
    rally_.active = true;
    rally_.level = own.level;
    std::copy(own.position, own.position + 3, rally_.position);
    rally_until_ = now + 90000;
    rally_request_ms_ = now;
    dirty_ = true;
}

void Comfort::clear_rally() {
    if (h_.self() == h_.host()) {
        rally_ = {};
        dirty_ = true;
    }
}

float Comfort::rally_distance() const {
    Pose own, dest;
    if (!rally_.active || !pose(h_.self(), own, true)) {
        return -1;
    }
    dest = own;
    dest.level = rally_.level;
    std::copy(rally_.position, rally_.position + 3, dest.position);
    float d = comfort_distance(own, dest);
    return std::isfinite(d) ? d : -1;
}

bool Comfort::near_downed() const {
    Pose own;
    if (!pose(h_.self(), own)) {
        return false;
    }
    for (const auto& [id, p] : peers_) {
        Pose dest;
        if (id != h_.self() && p.down && pose(id, dest) && comfort_distance(own, dest) < 6) {
            return true;
        }
    }
    return false;
}

void Comfort::tick(std::uint64_t now) {
    const auto room = h_.room();
    const int host = h_.host();
    if (room != room_ || host != host_ || h_.self() != self_) {
        reset();
        room_ = room;
        host_ = host;
        self_ = h_.self();
        if (h_.self() == host) {
            options_ = local_options_;
        } else {
            options_.revive = false;
        }
    }
    if (room.empty() || !member(h_.self())) {
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
    bool alive = pose(h_.self(), own, true);
    if (down_ && (!alive || own.level != down_level_ || own.health > 1)) {
        down_ = false;
        dirty_ = true;
    }
    if (down_ && (!options_.revive || h_.members().size() < 2 || now >= deadline_)
        && pose(h_.self(), own)) {
        give_up();
    }
    if (dirty_ || now - presence_ms_ >= 200) {
        presence(now);
    }
    if (h_.self() != host) {
        return;
    }
    for (auto it = peers_.begin(); it != peers_.end();) {
        if (!member(it->first)) {
            attempts_.erase(it->first);
            granted_.erase(it->first);
            it = peers_.erase(it);
            dirty_ = true;
        } else {
            ++it;
        }
    }
    if (options_.revive) {
        rescue_tick(now);
    } else {
        attempts_.clear();
    }
    if (rally_.active && (now >= rally_until_ || !alive || own.level != rally_.level)) {
        rally_ = {};
        dirty_ = true;
    }
    if (dirty_ || now - snapshot_ms_ >= 500) {
        snapshot(now);
    }
}

void Comfort::handle(int sender, const Bytes& bytes, std::uint64_t now) {
    if (h_.room().empty() || bytes.size() < 3 || bytes.size() > 256 || bytes[0] != kComfortKind
        || !member(sender)) {
        return;
    }
    Reader r(bytes.data() + 1, bytes.size() - 1);
    if (r.u8() != 1) {
        return;
    }
    if (r.str() != h_.room()) {
        return;
    }
    int op = r.u8();
    if (h_.self() == h_.host()) {
        if (op == Presence) {
            int life = r.i32();
            int down = r.u8(), holding = r.u8();
            if (r.ok() && !r.left() && down <= 1 && holding <= 1) {
                accept_presence(sender, life, down, holding, now);
            }
        } else if (op == Rally && r.ok() && !r.left()) {
            request_rally(now);
        }
        return;
    }
    if (sender != h_.host()) {
        return;
    }
    if (op == Revive) {
        int target = r.i32(), life = r.i32();
        Pose own;
        if (r.ok() && !r.left() && target == h_.self() && down_ && life == life_
            && pose(h_.self(), own, true) && own.level == down_level_ && own.health == 1) {
            down_ = false;
            grace_ = now + 2000;
            dirty_ = true;
        }
        return;
    }
    if (op != Snapshot) {
        return;
    }
    const auto serial = r.i64();
    int revive = r.u8(), rally_enabled = r.u8(), seconds = r.u8(), marked = r.u8();
    if (serial <= received_serial_ || revive > 1 || rally_enabled > 1 || marked > 1
        || (seconds != 10 && seconds != 20 && seconds != 30)) {
        return;
    }
    RallyPoint mark;
    if (marked) {
        mark.active = true;
        mark.level = r.i32();
        if (mark.level < 0 || mark.level >= 19) {
            return;
        }
        for (float& x : mark.position) {
            auto n = r.i32();
            if (n < -10000000 || n > 10000000) {
                return;
            }
            x = n / 100.f;
        }
    }
    int count = r.u8();
    if (count > static_cast<int>(kMaxPartySize)) {
        return;
    }
    std::map<int, ComfortPeer> peers;
    for (int i = 0; i < count; ++i) {
        int id = r.i32(), life = r.i32(), down = r.u8();
        if (!member(id) || peers.count(id) || life < 0 || down > 1) {
            return;
        }
        peers[id] = {down != 0, false, life, now};
    }
    if (!r.ok() || r.left()) {
        return;
    }
    received_serial_ = serial;
    options_ = {revive != 0, rally_enabled != 0, seconds};
    rally_ = mark;
    peers_ = std::move(peers);
}
}  // namespace mp
