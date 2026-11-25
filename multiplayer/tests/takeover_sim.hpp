// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The party on a seeded network, its host gone at an awkward moment (migrate.hpp): what is wrong
// once those left have played on and the link has settled stays wrong.

#ifndef MULTIPLAYER_TAKEOVER_SIM_H
#define MULTIPLAYER_TAKEOVER_SIM_H

#include "test_support.hpp"

namespace mpt {

// The moment the host goes: nothing special; in a scene, its members with it; with the party flying
// to another planet; with a save half way across; with a member in an enemy's grip; or it leaves by
// choice; or it comes back within the wait.
enum class HostGone {
    kPlain,
    kScene,
    kTravel,
    kSave,
    kGrab,
    kLeaves,
    kBack
};

// Every way the host can go, in the order the enum lists them.
inline constexpr HostGone kHostGoneAll[] =
    {HostGone::kPlain,
     HostGone::kScene,
     HostGone::kTravel,
     HostGone::kSave,
     HostGone::kGrab,
     HostGone::kLeaves,
     HostGone::kBack};

// Returns a short name for a way the host goes.
inline const char* host_gone_name(HostGone how) {
    switch (how) {
        case HostGone::kPlain:
            return "plain";
        case HostGone::kScene:
            return "scene";
        case HostGone::kTravel:
            return "travel";
        case HostGone::kSave:
            return "save";
        case HostGone::kGrab:
            return "grab";
        case HostGone::kLeaves:
            return "leaves";
        case HostGone::kBack:
            return "back";
    }

    return "";
}

// What a run of the takeover found: every count that is not zero is a fault.
struct TakeoverResult {
    bool hosted = false;    // someone runs the world
    bool stuck = false;     // someone is still held, or has no world
    int wrong_crates = 0;   // a crate dead in one game, not in another
    int wrong_dead = 0;     // an enemy the party killed, alive somewhere
    int wrong_place = 0;    // an enemy far from where the host has it
    int torn_saves = 0;     // a slot holding something other than a whole save
    int spawned_apart = 0;  // a game with another number of the spawned moby
    int held_on = 0;        // a hero the end of the host left in an enemy's grip
    std::int32_t host = 0;

    // Returns true when the run found any fault.
    bool bad() const {
        return !hosted || stuck || wrong_crates != 0 || wrong_dead != 0 || wrong_place != 0
               || torn_saves != 0 || spawned_apart != 0 || held_on != 0;
    }
};

// Plays a party of 1 + `members` on a seeded link, drops the host as `how` says, plays on, and
// returns what still differs once the link has settled.
inline TakeoverResult run_takeover(const SimLink& link, unsigned seed, int members, HostGone how) {
    static constexpr int kKilled = 4;  // an enemy the party kills early on, one that stands
    // The class of the moby the host spawns, which has no key.
    static constexpr int kSpawnClass = 700;
    static std::vector<std::unique_ptr<Player>> keep;
    keep.clear();
    std::vector<Player*> all;
    std::vector<mp::Member> list;

    // One player per member plus the host, with ids from 1.
    for (int i = 0; i <= members; ++i) {
        keep.push_back(std::make_unique<Player>(1 + i));
        all.push_back(keep.back().get());
        list.push_back({1 + i, "P" + std::to_string(1 + i)});
    }

    // Each player hears the party's members, and its game is a port with the card filter.
    for (Player* p : all) {
        p->session.handle(info(list));
        p->game.card_on = true;
    }

    offer_resume(all);

    // Two rounds let the players' mod lists reach each other before anyone readies.
    for (int i = 0; i < 2; ++i) {
        for (Player* p : all) {
            p->mods.tick(p->clock);
        }

        route(all);
    }

    // Every member readies; the host starts once all have.
    for (std::size_t i = 1; i < all.size(); ++i) {
        all[i]->session.toggle_ready();
    }

    route(all);
    Player& first = *all[0];
    first.coop.choose({}, "New game");
    CHECK(start_ready(first));
    route(all);
    tick(all, 16);
    SimWorld w{40, 12, 8.0f};
    const Bytes old_save(3000, 1);

    // Every game starts in level 3, steady, with its mobys placed alike.
    for (std::size_t i = 0; i < all.size(); ++i) {
        all[i]->game.now = {mp::kPhasePlaying, 3, 2};
        all[i]->game.level = w.made(kSimSlots + static_cast<int>(i) * 3);
        all[i]->game.hero_pose.position[0] = 40.0f * static_cast<float>(i);
        all[i]->game.hero_pose.flags = mp::kPoseSteady;
        all[i]->game.holds_ok = true;
        all[i]->game.live_save = old_save;
    }

    static std::unique_ptr<SimNet> net;
    net = std::make_unique<SimNet>(all, link, seed);
    net->whole = true;
    std::mt19937 rng(seed * 11 + 3);
    const auto play_ticks = static_cast<int>(3500 / 16 + rng() % (3000 / 16));
    const int cut_ticks = how == HostGone::kBack     ? 5000 / 16
                          : how == HostGone::kLeaves ? 0
                                                     : 15500 / 16;
    const int after_ticks = 12000 / 16;
    std::vector<Player*> left(all.begin() + 1, all.end());
    Player* leader = &first;
    std::set<int> broken;
    bool saved = false;
    int level_of_party = 3;
    std::map<std::int32_t, int> landing;

    // Breaks one crate in the game of a player still in the party's level, once per crate.
    const auto break_one = [&]() {
        const int key = w.enemies + static_cast<int>(rng() % static_cast<unsigned>(w.crates));
        std::vector<Player*> alive;

        // Only players still connected and in the party's level can break a crate.
        for (Player* p : all) {
            if (net->cut.count(p->id) == 0 && mp::in_level(p->game.now.phase)
                && p->game.now.level == level_of_party) {
                alive.push_back(p);
            }
        }

        if (alive.empty()) {
            return;
        }

        Player& who = *alive[rng() % alive.size()];
        const int slot = who.game.moby_slot(key);

        // A crate breaks once, and only in a game that still has it.
        if (slot >= 0 && broken.count(key) == 0) {
            broken.insert(key);
            who.game.die(slot);
        }
    };

    // A ship leaving: each game asked to travel loads, and lands in the next planet's level.
    const auto follow_travel = [&](int t) {
        for (Player* p : all) {
            const bool asked = std::find(p->game.calls.begin(), p->game.calls.end(), "travel 4")
                               != p->game.calls.end();

            // A game asked to travel goes to Loading and lands 60 ticks later.
            if (asked && landing.count(p->id) == 0 && p->game.now.level == 3) {
                landing[p->id] = t + 60;
                p->game.now.phase = mp::kPhaseLoading;
            }

            const auto due = landing.find(p->id);

            // When the load is over the game is in the next planet's level with its mobys new.
            if (due != landing.end() && t >= due->second && p->game.now.level == 3) {
                p->game.now = {mp::kPhasePlaying, 4, p->game.now.loads + 1};
                p->game.level = w.made(kSimSlots + (p->id - 1) * 3);
            }
        }
    };

    int t = 0;
    bool moving = true;

    // Runs `ticks` steps of the party, with the leader's enemies walking and crates breaking while
    // `moving`.
    const auto run = [&](int ticks) {
        for (const int end = t + ticks; t < end; ++t) {
            for (mp::MobyState& m : leader->game.level) {
                // Every third enemy of the leader walks while the party plays.
                if (moving && m.key >= 0 && m.key < w.enemies && m.key % 3 == 0) {
                    m.pose.position[1] += 0.05f;
                }
            }

            // (Not on the first tick: a game has a tick in the level before anyone can break
            // anything.)
            if (moving && rng() % 60 == 0 && t > 0) {
                break_one();
            }

            // A replayed break: the crate's own update takes the hit.
            for (Player* p : all) {
                for (const mp::Hit& h : p->game.hits) {
                    const auto it = std::find_if(
                        p->game.level.begin(), p->game.level.end(), [&](const mp::MobyState& m) {
                            return m.slot == h.victim;
                        }
                    );

                    if (it != p->game.level.end() && it->oclass == 500 && it->key >= 0) {
                        p->game.die(it->slot);
                    }
                }

                p->game.hits.clear();
            }

            // A party flying to another planet follows its ships' loads.
            if (how == HostGone::kTravel) {
                follow_travel(t);
            }

            net->step(16);
        }
    };

    run(120);
    first.game.die(first.game.moby_slot(kKilled));
    first.game.level.push_back(
        moby(kSimSlots + 200, -1, kSpawnClass, mp::kMobyUpdates | mp::kMobyCreature, 3.0f)
    );
    run(play_ticks);

    // The awkward moment, set up a little ahead of the drop.
    if (how == HostGone::kScene) {
        // In a scene every game is in the same scene when the host goes.
        for (Player* p : all) {
            p->game.now.phase = mp::kPhaseScene;
            p->game.now.scene = 2;
        }

        run(30);
    } else if (how == HostGone::kTravel) {
        first.coop.ship_travel(4, 3);
        first.game.calls.push_back("travel 4");
        level_of_party = 4;
        run(40);
    } else if (how == HostGone::kSave) {
        // (What a player's game did to its slot by itself is its own.)
        for (Player* p : all) {
            if (p->cosave.slot() >= 0) {
                p->game.slots[p->cosave.slot()] = old_save;
            }
        }

        first.game.live_save = Bytes(40000, 2);
        saved = first.cosave.save_party();
        net->step(16);
    } else if (how == HostGone::kGrab) {
        mp::Writer hold;
        hold.u8(mp::kind::kHold).u8(0).u8(1).u8(0).u8(0xFF);

        for (int i = 0; i < 16; ++i) {
            hold.u8(0);
        }

        all[1]->world.handle(1, hold.bytes());
    }

    // Out it goes.
    if (how == HostGone::kLeaves) {
        first.session.leave();
        net->cut.insert(1);
    } else {
        net->cut.insert(1);
        first.session.disconnected();
        first.link.frames.clear();

        // Of what was still on its way, half makes it.
        if (how == HostGone::kSave) {
            net->flying.erase(
                std::remove_if(
                    net->flying.begin(),
                    net->flying.end(),
                    [&](const SimNet::Packet& f) { return f.from == 1 && rng() % 2 == 0; }
                ),
                net->flying.end()
            );
        }

        server_away(left, 1);
    }

    run(cut_ticks);

    // A host that comes back is reconnected; otherwise the server picks a new host.
    if (how == HostGone::kBack) {
        net->cut.erase(1);
        come_back(first, left, info(list));
    } else {
        // The server's turn: the first it was told of who does not decline.
        std::int32_t epoch = 0;
        const std::vector<std::int32_t> named = net->named;
        bool found = false;

        for (const std::int32_t id : named) {
            leader = all[static_cast<std::size_t>(id - 1)];
            server_host(left, id, ++epoch);
            run(5);

            // A player that declines to host passes the turn to the next one named.
            if (net->declined.count(id) == 0) {
                found = true;
                break;
            }
        }

        // If everyone declined the result stays empty, which counts as a fault: nobody hosts.
        if (!found) {
            return TakeoverResult{};
        }

        // (The port says what a class is; a copy here is made as the host's was.)
        for (mp::MobyState& m : leader->game.level) {
            if (m.oclass == kSpawnClass) {
                m.flags |= mp::kMobyCreature;
            }
        }
    }

    run(after_ticks);
    moving = false;
    run(after_ticks);

    const std::vector<Player*> who = how == HostGone::kBack ? all : left;
    TakeoverResult r;
    r.host = leader->id;
    r.hosted = leader->world.hosting() && leader->session.is_host();
    SimResult place;
    std::vector<Player*> order = {leader};

    // The leader's world is compared first, then each other player's.
    for (Player* p : who) {
        if (p != leader) {
            order.push_back(p);
        }
    }

    compare_sim(order, place);
    r.wrong_crates = place.wrong_crates;
    r.wrong_place = place.wrong_place;

    // Each remaining player is checked for being stuck, wrong or torn.
    for (Player* p : who) {
        // A player is stuck when input is held, the hosts are changing, the host is gone or its
        // world never synced.
        if (p->coop.hold_input() || p->coop.between_hosts() || p->migrate.host_gone()
            || (p != leader && !p->world.synced())) {
            r.stuck = true;
        }

        // The kill and the spawned moby are checked only in level 3, where they happened.
        if (level_of_party == 3) {
            r.wrong_dead += p->game.moby_slot(kKilled) >= 0 ? 1 : 0;
            r.spawned_apart +=
                count_of_class(p->game, kSpawnClass) != count_of_class(leader->game, kSpawnClass)
                    ? 1
                    : 0;
        }

        // Only a member that did not host checks its slot, and only if the party saved.
        if (saved && p != &first && p->cosave.slot() >= 0) {
            const Bytes kept = p->game.card_read(p->cosave.slot());
            r.torn_saves += kept != old_save && kept != Bytes(40000, 2) ? 1 : 0;
        }

        r.held_on +=
            how == HostGone::kGrab && p == all[1]
                    && std::count(p->game.calls.begin(), p->game.calls.end(), "let go") == 0
                ? 1
                : 0;
    }

    return r;
}

}  // namespace mpt

#endif
