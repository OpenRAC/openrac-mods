// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Team tools' rescue and rally points (comfort.hpp): three players on a fake network, a downed
// player waiting for a held rescue, the host's rules and rally marker, and ignored bad messages.

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>

#include "comfort.hpp"
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%d: %s\n", __LINE__, #x);                                        \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)
using namespace mp;

// A message in flight: its sender, its target and its bytes.
struct Packet {
    int from, to;
    Bytes bytes;
};

// Three players on a fake network, each running a Comfort over hooks the test sets.
struct Network {
    std::uint64_t now = 1000;
    std::string room = "party";
    std::vector<int> members{1, 2, 3};
    std::map<int, Pose> poses;
    std::map<int, bool> active;
    std::map<int, bool> available;
    std::map<int, int> expired;
    std::map<int, std::unique_ptr<Comfort>> nodes;
    std::deque<Packet> packets;

    // Starts the three players in level 0 with four health, then ticks once.
    Network() {
        // Every member starts alive, active and observable.
        for (int id : members) {
            poses[id].level = 0;
            poses[id].health = 4;
            poses[id].position[0] = float(id);
            active[id] = true;
            available[id] = true;
        }

        // Each member gets its own hooks over the shared fake network.
        for (int id : members) {
            ComfortHooks h;
            h.self = [id] {
                return id;
            };
            h.host = [] {
                return 1;
            };
            h.room = [this] {
                return room;
            };
            h.members = [this] {
                return members;
            };
            h.pose = [this](int who, Pose& out, bool menus) {
                // The hook gives no pose for a hero the game cannot see, or for a paused one unless
                // the caller counts menus.
                if (!available[who] || !poses.count(who) || (!menus && !active[who])) {
                    return false;
                }

                out = poses[who];
                return true;
            };
            h.send = [this, id](int target, const Bytes& b) {
                packets.push_back({id, target, b});
            };
            h.expire = [this, id] {
                ++expired[id];
                poses[id].health = 0;
            };
            nodes[id] = std::make_unique<Comfort>(std::move(h));
        }

        step(0);
    }

    // Delivers queued messages until none are left, each to its target or to everyone for a
    // broadcast.
    void deliver() {
        int guard = 0;

        // Delivery can cause further sends, so it runs until the queue is empty; the guard catches
        // a message loop.
        while (!packets.empty()) {
            CHECK(++guard < 1000);
            auto p = packets.front();
            packets.pop_front();

            // A broadcast reaches everyone but the sender.
            if (p.to == kBroadcast) {
                for (auto& [id, node] : nodes) {
                    if (id != p.from) {
                        node->handle(p.from, p.bytes, now);
                    }
                }
            } else if (nodes.count(p.to)) {
                nodes[p.to]->handle(p.from, p.bytes, now);
            }
        }
    }

    // Advances the clock by `ms`, ticks every player once and delivers what they sent.
    void step(int ms) {
        now += ms;

        // Every player ticks before any message is delivered.
        for (auto& [id, node] : nodes) {
            node->tick(now);
        }

        deliver();
    }

    // Advances time by `ms` in steps of 100 ms, as game ticks would.
    void advance(int ms) {
        // Time passes in 100 ms steps.
        for (int n = 0; n < ms; n += 100) {
            step(100);
        }
    }

    // Knocks player `id` down with a lethal hit at one health and delivers the news.
    void down(int id) {
        poses[id].health = 1;
        CHECK(nodes[id]->hit(now, 1, true));
        deliver();
        step(0);
    }
};

int main() {
    // A teammate holding beside a fallen player revives it with one health.
    {
        Network n;
        n.down(2);
        CHECK(n.nodes[2]->down());
        CHECK(n.expired[2] == 0);
        n.nodes[1]->holding(true);
        n.advance(3500);
        CHECK(!n.nodes[2]->down());
        CHECK(n.poses[2].health == 1);
        CHECK(n.nodes[2]->hit(n.now, 1, true));
        n.advance(2100);
        CHECK(n.nodes[2]->hit(n.now, 0, true) == false);
    }
    // The host can be rescued by a member too.
    {
        Network n;
        n.down(1);
        n.nodes[2]->holding(true);
        n.advance(3500);
        CHECK(!n.nodes[1]->down());
        CHECK(n.expired[1] == 0);
    }
    // A hold that is let go starts over, so a rescue needs one unbroken hold.
    {
        Network n;
        n.down(2);
        n.nodes[1]->holding(true);
        n.advance(2000);
        n.nodes[1]->holding(false);
        n.advance(1000);
        n.nodes[1]->holding(true);
        n.advance(2000);
        CHECK(n.nodes[2]->down());
        n.advance(1500);
        CHECK(!n.nodes[2]->down());
    }
    // A rescuer far away or in another level does not count, so the fall expires after the wait
    // instead.
    {
        Network n;
        n.down(2);
        n.poses[1].position[0] = 100;
        n.poses[3].level = 1;
        n.nodes[1]->holding(true);
        n.nodes[3]->holding(true);
        n.advance(21000);
        CHECK(!n.nodes[2]->down());
        CHECK(n.expired[2] == 1);
    }
    // A rescuer whose game is paused does not rescue until it plays again.
    {
        Network n;
        n.down(2);
        n.nodes[1]->holding(true);
        n.active[1] = false;
        n.advance(4000);
        CHECK(n.nodes[2]->down());
        n.active[1] = true;
        n.advance(3500);
        CHECK(!n.nodes[2]->down());
    }
    // A hit is not consumed in a vehicle, with a negative damage, or once the host turns revive
    // off.
    {
        Network n;
        n.poses[2].health = 1;
        CHECK(!n.nodes[2]->hit(n.now, 1, false));
        CHECK(!n.nodes[2]->hit(n.now, -1, true));
        n.nodes[1]->configure({false, true, 10});
        n.step(1);
        CHECK(!n.nodes[2]->hit(n.now, 1, true));
    }
    // Giving up ends the wait and hands the game its hit once; a second call does nothing.
    {
        Network n;
        n.down(2);
        n.nodes[2]->give_up();
        CHECK(n.expired[2] == 1);
        n.nodes[2]->give_up();
        CHECK(n.expired[2] == 1);
    }
    // A player whose health reaches zero by other means stops waiting, and its hit is not handed
    // over.
    {
        Network n;
        n.down(2);
        n.nodes[2]->holding(true);
        n.advance(3500);
        CHECK(n.nodes[2]->down());
        n.poses[2].health = 0;
        n.step(1);
        CHECK(!n.nodes[2]->down());
        CHECK(n.expired[2] == 0);
    }
    // A hero pose missing for a while does not expire the wait; a later real death ends it.
    {
        Network n;
        n.down(2);
        n.available[2] = false;  // shared scene/load: the hero pose disappears temporarily
        n.advance(2000);
        CHECK(n.nodes[2]->down());
        CHECK(n.expired[2] == 0);
        n.available[2] = true;
        n.poses[2].health = 0;  // actual death once the hero becomes observable again
        n.step(1);
        CHECK(!n.nodes[2]->down());
        CHECK(n.expired[2] == 0);
    }
    // A member's rally request is placed at the host's position, and the host clears it.
    {
        Network n;
        n.nodes[2]->request_rally(n.now);
        n.deliver();
        n.step(1);
        CHECK(n.nodes[2]->rally().active);
        CHECK(n.nodes[2]->rally().position[0] == n.poses[1].position[0]);
        CHECK(n.nodes[2]->rally_distance() == 1);
        n.nodes[2]->clear_rally();
        CHECK(n.nodes[2]->rally().active);
        n.nodes[1]->clear_rally();
        n.step(1);
        CHECK(!n.nodes[2]->rally().active);
    }
    // A rally marker ends when the host changes level, and a later one lapses on its own.
    {
        Network n;
        n.nodes[1]->request_rally(n.now);
        n.step(1);
        n.poses[1].level = 1;
        n.step(1);
        CHECK(!n.nodes[2]->rally().active);
        n.nodes[1]->request_rally(n.now + 4000);
        n.step(4000);
        CHECK(n.nodes[1]->rally().active);
        n.advance(91000);
        CHECK(!n.nodes[1]->rally().active);
    }
    // Members follow the host's rules, so a member's own configure has no effect.
    {
        Network n;
        n.nodes[1]->configure({false, false, 30});
        n.step(1);
        CHECK(!n.nodes[2]->options().revive);
        CHECK(!n.nodes[2]->options().rally);
        n.nodes[2]->configure({true, true, 10});
        CHECK(!n.nodes[2]->options().revive);
        n.nodes[2]->request_rally(n.now);
        n.deliver();
        CHECK(!n.nodes[1]->rally().active);
    }
    // A grant from a member who is not the host is ignored, and leaving the party ends the wait.
    {
        Network n;
        n.down(2);
        auto fake = Writer().u8(kComfortKind).u8(1).str("party").u8(3).i32(2).i32(1).bytes();
        n.nodes[2]->handle(3, fake, n.now);
        CHECK(n.nodes[2]->down());
        fake.back() = 0;
        n.nodes[2]->handle(1, fake, n.now);
        CHECK(n.nodes[2]->down());
        n.room = "";
        n.step(1);
        CHECK(!n.nodes[2]->down());
        CHECK(!n.nodes[2]->rally().active);
    }
    // A member who leaves the party is dropped from the peers, and its wait does not expire.
    {
        Network n;
        n.down(2);
        n.nodes[1]->holding(true);
        n.advance(2000);
        n.members = {1, 3};
        n.advance(3500);
        CHECK(n.expired[2] == 0);
        CHECK(!n.nodes[1]->peers().count(2));
    }
    // Malformed messages are ignored.
    {
        Network n;
        Bytes malformed = {kComfortKind, 1, 2};
        n.nodes[2]->handle(1, malformed, n.now);
        CHECK(n.nodes[2]->options().revive);

        // Packets of every length from 0 to 19 bytes are ignored.
        for (int size = 0; size < 20; ++size) {
            auto packet = Bytes(size, 255);
            n.nodes[2]->handle(1, packet, n.now);
        }

        CHECK(!n.nodes[2]->rally().active);
    }
    // A player alone in the party cannot be held at one health.
    {
        Network n;
        n.members = {1};
        n.poses[1].health = 1;
        CHECK(!n.nodes[1]->hit(n.now, 1, true));
    }
    // Old-party packets, duplicate grants and absent heartbeats cannot rescue a new life.
    {
        Network n;
        n.down(2);
        auto old = Writer().u8(kComfortKind).u8(1).str("old-party").u8(3).i32(2).i32(1).bytes();
        n.nodes[2]->handle(1, old, n.now);
        CHECK(n.nodes[2]->down());
        auto grant = Writer().u8(kComfortKind).u8(1).str("party").u8(3).i32(2).i32(1).bytes();
        n.nodes[2]->handle(1, grant, n.now);
        CHECK(!n.nodes[2]->down());
        n.advance(2100);
        n.down(2);
        n.nodes[2]->handle(1, grant, n.now);
        CHECK(n.nodes[2]->down());
    }
    // A player cut off from the party stays down, since no grant reaches it.
    {
        Network n;
        n.down(2);
        auto absent = n.nodes.extract(2);
        n.advance(1500);
        n.nodes[1]->holding(true);
        n.advance(4000);
        CHECK(absent.mapped()->down());
        n.nodes.insert(std::move(absent));
    }
    // A rescue window of 10 seconds expires a fall after 10 seconds.
    {
        Network n;
        n.nodes[1]->configure({true, true, 10});
        n.step(1);
        n.down(2);
        n.advance(11000);
        CHECK(n.expired[2] == 1);
    }
    // A paused waiting player is not killed by world hits, and is rescued once it plays again.
    {
        Network n;
        n.down(2);
        n.active[2] = false;
        CHECK(
            n.nodes[2]->hit(n.now, 1, true)
        );  // paused waiting player is not killed by ordinary world hits
        n.nodes[1]->holding(true);
        n.advance(3500);
        CHECK(n.nodes[2]->down());
        n.active[2] = true;
        n.advance(3500);
        CHECK(!n.nodes[2]->down());
    }
    {
        Network n;
        n.down(2);
        n.nodes[1]->holding(true);
        n.advance(1500);
        n.step(4000);
        n.advance(1000);
        CHECK(n.nodes[2]->down());
        n.advance(2500);
        CHECK(!n.nodes[2]->down());
    }
}
