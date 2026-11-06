// SPDX-License-Identifier: GPL-3.0-or-later
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

struct ComfortOptions {
    bool revive = true, rally = true;
    int down_seconds = 20;
};

struct ComfortPeer {
    bool down = false, holding = false;
    int life = 0;
    std::uint64_t seen = 0;
};

struct RallyPoint {
    bool active = false;
    int level = -1;
    float position[3] = {};
};

struct ComfortHooks {
    std::function<int()> self, host;
    std::function<std::string()> room;
    std::function<std::vector<int>()> members;
    // A live pose. include_menu permits a pause page's marker/status, never rescue eligibility.
    std::function<bool(int, Pose&, bool include_menu)> pose;
    std::function<void(int, const Bytes&)> send;
    // Let the original damage handler take the deferred final hit; no health/position writes.
    std::function<void()> expire;
};

class Comfort {
public:
    explicit Comfort(ComfortHooks hooks) : h_(std::move(hooks)) {}

    void tick(std::uint64_t now);
    void handle(int sender, const Bytes& bytes, std::uint64_t now);
    // true consumes a positive normal hit. Instant-kill hazards bypass this game hook.
    bool hit(std::uint64_t now, int damage, bool on_foot);

    void holding(bool value) { holding_ = value; }

    void configure(ComfortOptions options);
    void request_rally(std::uint64_t now);
    void clear_rally();
    void give_up();

    bool down() const { return down_; }

    int seconds_left(std::uint64_t now) const;

    const ComfortOptions& options() const { return options_; }

    const RallyPoint& rally() const { return rally_; }

    const std::map<int, ComfortPeer>& peers() const { return peers_; }

    float rally_distance() const;
    bool near_downed() const;

private:
    struct Attempt {
        int target = 0, life = 0;
        std::uint64_t since = 0;
    };

    bool member(int id) const;
    bool pose(int id, Pose& out, bool menus = false) const;
    void presence(std::uint64_t now);
    void snapshot(std::uint64_t now);
    void grant(int target, int life, std::uint64_t now);
    void rescue_tick(std::uint64_t now);
    void accept_presence(int sender, int life, bool down, bool holding, std::uint64_t now);
    void reset();
    ComfortHooks h_;
    ComfortOptions options_, local_options_;
    RallyPoint rally_;
    std::map<int, ComfortPeer> peers_;
    std::map<int, Attempt> attempts_;
    std::map<int, int> granted_;
    std::string room_;
    int host_ = 0, self_ = 0, life_ = 0, down_level_ = -1;
    bool down_ = false, holding_ = false, dirty_ = true;
    std::uint64_t deadline_ = 0, grace_ = 0, presence_ms_ = 0, snapshot_ms_ = 0, rally_until_ = 0,
                  rally_request_ms_ = 0, last_tick_ = 0;
    std::int64_t serial_ = 0, received_serial_ = -1;
};

float comfort_distance(const Pose& a, const Pose& b);
}  // namespace mp
