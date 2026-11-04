// SPDX-License-Identifier: GPL-3.0-or-later
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

struct Packet {
    int from, to;
    Bytes bytes;
};

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

    Network() {
        for (int id : members) {
            poses[id].level = 0;
            poses[id].health = 4;
            poses[id].position[0] = float(id);
            active[id] = true;
            available[id] = true;
        }
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

    void deliver() {
        int guard = 0;
        while (!packets.empty()) {
            CHECK(++guard < 1000);
            auto p = packets.front();
            packets.pop_front();
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

    void step(int ms) {
        now += ms;
        for (auto& [id, node] : nodes) {
            node->tick(now);
        }
        deliver();
    }

    void advance(int ms) {
        for (int n = 0; n < ms; n += 100) {
            step(100);
        }
    }

    void down(int id) {
        poses[id].health = 1;
        CHECK(nodes[id]->hit(now, 1, true));
        deliver();
        step(0);
    }
};

int main() {
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
    {
        Network n;
        n.down(1);
        n.nodes[2]->holding(true);
        n.advance(3500);
        CHECK(!n.nodes[1]->down());
        CHECK(n.expired[1] == 0);
    }
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
    {
        Network n;
        n.poses[2].health = 1;
        CHECK(!n.nodes[2]->hit(n.now, 1, false));
        CHECK(!n.nodes[2]->hit(n.now, -1, true));
        n.nodes[1]->configure({false, true, 10});
        n.step(1);
        CHECK(!n.nodes[2]->hit(n.now, 1, true));
    }
    {
        Network n;
        n.down(2);
        n.nodes[2]->give_up();
        CHECK(n.expired[2] == 1);
        n.nodes[2]->give_up();
        CHECK(n.expired[2] == 1);
    }
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
    {
        Network n;
        Bytes malformed = {kComfortKind, 1, 2};
        n.nodes[2]->handle(1, malformed, n.now);
        CHECK(n.nodes[2]->options().revive);
        for (int size = 0; size < 20; ++size) {
            auto packet = Bytes(size, 255);
            n.nodes[2]->handle(1, packet, n.now);
        }
        CHECK(!n.nodes[2]->rally().active);
    }
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
    {
        Network n;
        n.nodes[1]->configure({true, true, 10});
        n.step(1);
        n.down(2);
        n.advance(11000);
        CHECK(n.expired[2] == 1);
    }
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
