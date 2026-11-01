// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The play tests' shared support (play_test.cpp, play_*_test.cpp): CHECK, the recorders, the fake games
// and the players that talk through a router, and the simulated links.
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <algorithm>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "coop.hpp"
#include "cosave.hpp"
#include "menu.hpp"
#include "migrate.hpp"
#include "play.hpp"
#include "protocol.hpp"
#include "session.hpp"
#include "progress.hpp"
#include "camscripts.hpp"
#include "shared.hpp"
#include "world.hpp"

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c);                    \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

// The failures counted by CHECK, defined in play_test.cpp.
extern int g_failures;

namespace mpt {

using mp::Bytes;

inline mp::Pose pose_at(float x, float vx = 0.0f, int level = 3) {
    mp::Pose p;
    p.level = level;
    p.position[0] = x;
    p.velocity[0] = vx;
    return p;
}


inline mp::HeldPiece piece(int slot, int part, int item, int oclass, int joint) {
    mp::HeldPiece h;
    h.slot = slot;
    h.part = part;
    h.item = item;
    h.oclass = oclass;
    h.joint = joint;
    h.seq = 1;
    h.frame = 3;
    h.next_frame = 4;
    h.anim_time = 0.25f;
    h.anim_speed = 1.0f;
    return h;
}


// ---- The co-op, a host and a member ----

struct Recorder : mp::Transport {
    std::vector<Bytes> frames;
    /* With `udp` on, state stays out of the frames: the datagrams, for SimNet, and their targets. */
    bool udp = false;
    std::vector<std::pair<std::int32_t, Bytes>> datagrams;

    void send(Bytes frame) override { frames.push_back(std::move(frame)); }

    bool state_needs_tcp(std::size_t /*bytes*/) const override { return !udp; }

    void send_state(std::int32_t target, const Bytes& bytes, bool tcp) override {
        if (udp) {
            datagrams.emplace_back(target, bytes);
        } else if (tcp) {
            send(mp::data(target, bytes));
        }
    }
};


struct FakeGame : mp::Game {
    mp::GameStatus now{mp::kPhaseFrontEnd, -1, 1};
    std::vector<std::string> calls;
    Bytes started_save;
    Bytes live_save;
    int next_figure = 0;
    std::map<int, mp::Pose> figures;
    /* A grab: whether the game holds a hero, what the update last saw, what it did. */
    bool holds_ok = true;
    bool hero_held = false;
    mp::StandIn stood_as;
    mp::StandIn stand_did;
    float held_at[1] = {0};
    float held_yaw = 0;
    int hold_words = 0;
    mp::Pose hero_pose = [] {
        mp::Pose p = pose_at(1.0f);
        p.health = 8;
        return p;
    }();

    /* The mods' rules (party_mods.hpp): what this game's publish, which mods take the host's, and
     * what reached them, in order, with what the game had been asked to do by then. */
    std::vector<mp::PartyRules> published;
    std::set<std::string> takes;
    struct Delivery {
        std::string id;
        Bytes data;
        std::size_t calls;
    };
    std::vector<Delivery> delivered;

    /* A save refused (save_check.hpp); the level's sim digest, per level. */
    bool saves_usable = true;
    std::map<int, std::uint32_t> digests;

    bool save_usable(const Bytes&) override { return saves_usable; }

    /* A port that places the hero (`placeable`): the places asked, and, when `place_works`, the hero
     * there at once. */
    bool placeable = false;
    bool place_works = true;
    struct Placed {
        float at[3];
        float yaw;
        bool beside;
    };
    std::vector<Placed> placed;
    std::vector<bool> quiet;
    bool gifts_quiet = false;

    bool can_place() override { return placeable; }

    bool place(const float* position, float yaw, bool beside) override {
        if (!placeable) {
            return false;
        }

        placed.push_back({{position[0], position[1], position[2]}, yaw, beside});

        if (place_works) {
            std::copy(position, position + 3, hero_pose.position);
        }

        return true;
    }

    std::string level_name(int level) override { return "Planet" + std::to_string(level); }

    void quiet_gifts(bool on) override {
        quiet.push_back(on);
        gifts_quiet = on;
    }

    std::uint32_t sim_digest(int level) override {
        const auto it = digests.find(level);
        return it != digests.end() ? it->second : 0;
    }

    /* The card: `card_on` a port with the filter. What the filter holds, the co-op slots and their
     * notes, and the game's own card, where a save the game makes lands with the filter off: a
     * test's own_saves is a player's story saves. */
    bool card_on = false;
    int filter = mp::kCardPass;
    int hint = -1;
    bool write_refused = false;
    bool pass_refused = false;
    std::map<int, Bytes> slots;
    std::map<int, std::string> notes;
    Bytes own_saves;
    std::uint32_t saves = 0;
    std::int64_t wall = 1000;

    /* The game saves, as the port takes it: into the party's slot under the filter, else the card. */
    void game_saves(const Bytes& bytes) {
        if (filter == mp::kCardRedirect) {
            slots[hint] = bytes;
            ++saves;
        } else {
            own_saves = bytes;
        }
    }

    bool has_card_filter() override { return card_on; }

    bool card_filter(int mode) override {
        if (mode == mp::kCardPass && pass_refused) {
            return false;
        }

        filter = mode;
        return true;
    }

    void card_slot(int slot) override { hint = slot; }

    bool card_write(int slot, const Bytes& save) override {
        if (write_refused || filter != mp::kCardRedirect) {
            return false;
        }

        slots[slot] = save;
        return true;
    }

    Bytes card_read(int slot) override { return slots.count(slot) != 0 ? slots[slot] : Bytes{}; }

    std::uint32_t card_saves() override { return saves; }

    std::string card_note(int slot) override { return notes.count(slot) != 0 ? notes[slot] : std::string(); }

    void card_note_write(int slot, const std::string& text) override { notes[slot] = text; }

    std::int64_t wall_seconds() override { return wall; }

    // A camera script: the one running here, the calls of one shown here, and its last view. A
    // port's answer: refused (camera_taken: the game keeps the camera).
    bool script_on = false;
    mp::CameraView script_view;
    bool camera_taken = false;
    std::vector<int> camera_calls;
    int camera_shows = 0;
    float shown_eye[3] = {0, 0, 0};
    float shown_rotation[3] = {0, 0, 0};

    bool camera_view(mp::CameraView& out) override {
        out = script_view;
        return script_on;
    }

    bool camera_play(int id) override {
        camera_calls.push_back(id);
        return true;
    }

    bool camera_show(const float* eye, const float* rotation) override {
        if (camera_taken) {
            return false;
        }
        ++camera_shows;
        std::copy_n(eye, 3, shown_eye);
        std::copy_n(rotation, 3, shown_rotation);
        return true;
    }

    std::vector<mp::PartyRules> party_rules() override { return published; }

    bool deliver_rules(const std::string& id, const Bytes& data) override {
        if (takes.count(id) == 0) {
            return false;
        }

        delivered.push_back({id, data, calls.size()});
        return true;
    }

    mp::GameStatus status() override { return now; }

    bool start(int level, const Bytes& save) override {
        calls.push_back("start " + std::to_string(level) + " " + std::to_string(save.size()));
        started_save = save;
        return true;
    }

    bool travel(int level) override {
        calls.push_back("travel " + std::to_string(level));
        return true;
    }

    bool open_home() override {
        calls.push_back("home");
        return true;
    }

    Bytes capture() override { return live_save; }

    bool hero(mp::Pose& out) override {
        out = hero_pose;
        out.level = now.level;
        return mp::in_level(now.phase);
    }

    int figure_create() override {
        figures[next_figure] = mp::Pose{};
        return next_figure++;
    }

    bool figure_pose(int id, const mp::Pose& pose) override {
        if (figures.count(id) == 0) {
            return false;
        }
        figures[id] = pose;
        return true;
    }

    void figure_remove(int id) override { figures.erase(id); }

    // The hero's shots to give, and the figures' shots fired (figure id, shot).
    std::vector<mp::Shot> shots_out;
    std::vector<std::pair<int, mp::Shot>> shots_in;

    std::vector<mp::Shot> hero_shots() override {
        std::vector<mp::Shot> out;
        out.swap(shots_out);
        return out;
    }

    bool figure_shot(int id, const mp::Shot& shot) override {
        if (figures.count(id) == 0) {
            return false;
        }
        shots_in.emplace_back(id, shot);
        return true;
    }

    // What hold_death, revive and respawn were asked, and the mission row of this game's level.
    bool revivable = true;
    bool holding = false;
    int revives = 0;
    int respawns = 0;
    std::array<std::uint8_t, mp::kMissions> mission_row_bytes{};
    bool mission_row_known = true;

    bool can_revive() override { return revivable; }

    void hold_death(bool on) override { holding = on; }

    bool revive() override {
        ++revives;
        return true;
    }

    bool respawn() override {
        ++respawns;
        return true;
    }

    bool mission_row(int /*level*/, std::uint8_t* out) override {
        std::copy(mission_row_bytes.begin(), mission_row_bytes.end(), out);
        return mission_row_known;
    }

    // The world: a level's mobys by slot, the drives, kills, spawns, hits, stand-ins and bolts. The
    // hero's moby is slot 99, figure id N is slot 100 + N.
    std::vector<mp::MobyState> level;
    std::set<int> drives;
    std::vector<std::string> world_calls;
    std::vector<mp::Hit> hits;
    int stood = 0;
    int bolt_count = 0;

    std::vector<mp::MobyState> mobys() override {
        return listing_blank ? std::vector<mp::MobyState>{} : level;
    }

    int moby_slot(int key) override {
        for (const mp::MobyState& m : level) {
            if (m.key == key && (m.flags & mp::kMobyDead) == 0) {
                return m.slot;
            }
        }
        return -1;
    }

    bool moby_drive(int slot, bool on) override {
        if (on) {
            drives.insert(slot);
        } else {
            drives.erase(slot);
        }
        return true;
    }

    bool moby_pose(int slot, const mp::Pose& pose) override {
        for (mp::MobyState& m : level) {
            if (m.slot == slot) {
                m.pose = pose;
                return true;
            }
        }
        return false;
    }

    // A moby killed: a placed one stays listed, dead (as the port lists it), a spawned one goes.
    bool moby_kill(int slot) override {
        world_calls.push_back("kill " + std::to_string(slot));
        return die(slot);
    }

    bool die(int slot) {
        for (auto it = level.begin(); it != level.end(); ++it) {
            if (it->slot != slot || (it->flags & mp::kMobyDead) != 0) {
                continue;
            }
            if (it->key >= 0) {
                it->flags |= mp::kMobyDead;
            } else {
                level.erase(it);
            }
            return true;
        }
        return false;
    }

    // The game's listing as a menu with its own mobys (or a load) gives it: nothing.
    bool listing_blank = false;
    bool overlay = false;

    void overlay_pause(bool on) override { overlay = on; }

    void skip() override { calls.push_back("skip"); }

    // A scene asked for (the test plays it by setting `now`), and -1 a request dropped.
    bool play_scene(int scene) override {
        calls.push_back("scene " + std::to_string(scene));
        return mp::in_level(now.phase);
    }

    int moby_spawn(int oclass, const mp::Pose& pose) override {
        mp::MobyState m;
        m.slot = 500 + static_cast<int>(level.size());
        m.oclass = oclass;
        m.pose = pose;
        level.push_back(m);
        world_calls.push_back("spawn " + std::to_string(oclass));
        return m.slot;
    }

    int figure_slot(int id) override { return id < 0 ? 99 : 100 + id; }

    bool stand_in(const float* position, int figure) override {
        ++stood;
        world_calls.push_back("stand " + std::to_string(static_cast<int>(position[0])) + " " + std::to_string(figure));
        return true;
    }

    bool can_hold() override { return holds_ok; }

    bool stand_in_as(const mp::StandIn& as, int figure) override {
        ++stood;
        stood_as = as;
        world_calls.push_back("as " + std::to_string(as.state) + " " + std::to_string(figure));
        return true;
    }

    bool stand_in_read(mp::StandIn& out) override {
        out = stand_did;
        return true;
    }

    bool grab(int animation, std::uint32_t flags) override {
        calls.push_back("grab " + std::to_string(animation) + " " + std::to_string(flags));
        hero_held = true;
        return true;
    }

    bool hold_at(const float* position, float yaw) override {
        held_at[0] = position[0];
        held_yaw = yaw;
        ++hold_words;
        return true;
    }

    bool let_go() override {
        calls.push_back("let go");
        hero_held = false;
        return true;
    }

    bool hit(const mp::Hit& h) override {
        hits.push_back(h);
        return true;
    }

    int bolts(int add) override { return bolt_count += add; }

    // The eyes last set (x, y, z each).
    std::vector<float> eyes_at;

    void eyes(const std::vector<float>& xyz) override { eyes_at = xyz; }

    // The shared progress: what this game has (read in a level only), and what was given to it,
    // in order ("item 5", "gold 6", "planet 3").
    mp::ProgressBits owned;
    std::vector<std::string> given;

    bool progress(mp::ProgressBits& out) override {
        out = owned;
        return mp::in_level(now.phase);
    }

    bool give_item(int id) override {
        owned.items[static_cast<std::size_t>(id)] = 1;
        (gifts_quiet ? given_quietly : given).push_back("item " + std::to_string(id));
        return true;
    }

    bool give_gold_bolt(int index) override {
        owned.gold[static_cast<std::size_t>(index)] = 1;
        given.push_back("gold " + std::to_string(index));
        return true;
    }

    bool unlock_planet(int planet) override {
        owned.planets[static_cast<std::size_t>(planet)] = 1;
        (gifts_quiet ? given_quietly : given).push_back("planet " + std::to_string(planet));
        return true;
    }

    // The shared progress past those (shared.hpp): each section's bytes, as the port's are. The
    // items and planets given without a banner, in order ("item 5", "planet 3").
    mp::SharedBits sections;
    std::vector<std::string> given_quietly;
    bool has_shared = true;

    template <std::size_t N>
    static Bytes bytes_of(const std::array<std::uint8_t, N>& a) {
        return Bytes(a.begin(), a.end());
    }

    bool shared_read(int tag, Bytes& out) override {
        if (!has_shared || !mp::in_level(now.phase)) {
            return false;
        }

        switch (tag) {
        case mp::kSharedGoldWeapons:
            out = bytes_of(sections.gold_weapons);
            return true;
        case mp::kSharedSkillPoints:
            out = bytes_of(sections.skills);
            return true;
        case mp::kSharedFlags:
            out = bytes_of(sections.flags);
            return true;
        case mp::kSharedSpecials:
            out = bytes_of(sections.specials);
            return true;
        default:
            return false;
        }
    }

    bool shared_give(int tag, const Bytes& data) override {
        const auto merge = [&](auto& a, std::uint8_t value) {
            for (std::size_t i = 0; i < a.size() && i < data.size(); ++i) {
                if (data[i] != 0 && a[i] == 0) {
                    a[i] = value;
                }
            }
        };

        switch (tag) {
        case mp::kSharedGoldWeapons:
            merge(sections.gold_weapons, 1);
            return true;
        case mp::kSharedSkillPoints:
            merge(sections.skills, 1);
            return true;
        case mp::kSharedFlags:
            merge(sections.flags, 0xFF);
            return true;
        case mp::kSharedSpecials:
            merge(sections.specials, 1);
            return true;
        default:
            return false;
        }
    }

    // A moby hidden or shown (its listed flag), the deletions the game made (by slot), the camera.
    // A figure's (slot 100 + id) hidden, by id.
    std::set<int> hidden_figures;

    bool moby_hide(int slot, bool hidden) override {
        if (slot >= 100 && figures.count(slot - 100) != 0) {
            if (hidden) {
                hidden_figures.insert(slot - 100);
            } else {
                hidden_figures.erase(slot - 100);
            }
            world_calls.push_back((hidden ? "hide " : "show ") + std::to_string(slot));
            return true;
        }
        for (mp::MobyState& m : level) {
            if (m.slot == slot) {
                m.flags = hidden ? m.flags | mp::kMobyHidden : m.flags & ~mp::kMobyHidden;
                world_calls.push_back((hidden ? "hide " : "show ") + std::to_string(slot));
                return true;
            }
        }
        return false;
    }

    std::map<int, mp::MobyDeletion> deletions;

    bool moby_deletion(int slot, int& by, int& by_class, bool& by_mod) override {
        const auto it = deletions.find(slot);
        if (it == deletions.end()) {
            return false;
        }
        by = it->second.by;
        by_class = it->second.by_class;
        by_mod = it->second.by_mod;
        return true;
    }

    bool camera(float* xyz) override {
        xyz[0] = camera_x;
        xyz[1] = xyz[2] = 0.0f;
        return true;
    }

    float camera_x = 0.0f;

    // The run list (the verbose log's): off until a test sets `run_on`; each slot's run, else
    // `run`'s.
    bool moby_run(int slot, mp::MobyRun& out) override {
        if (!run_on) {
            return false;
        }
        const auto it = slot_runs.find(slot);
        out = it != slot_runs.end() ? it->second : run;
        return true;
    }

    bool run_on = false;
    mp::MobyRun run;
    std::map<int, mp::MobyRun> slot_runs;
};


inline mp::MobyState moby(int slot, int key, int oclass, std::uint32_t flags, float x) {
    mp::MobyState m;
    m.slot = slot;
    m.key = key;
    m.oclass = oclass;
    m.flags = flags;
    m.pose.position[0] = x;
    return m;
}


struct Player {
    std::int32_t id;
    Recorder link;
    std::uint64_t clock = 1000;
    mp::Session session;
    FakeGame game;
    mp::Coop coop;
    mp::World world;
    mp::Progress progress;
    mp::Shared shared;
    mp::CamScripts cam;
    mp::PartyMods mods;
    mp::CoSave cosave;
    mp::Migrate migrate;
    mp::Router router;

    explicit Player(std::int32_t id_)
        : id(id_),
          session(link, "test/1", "P" + std::to_string(id_), [this] { return clock; }),
          coop(session, game, [this] { return clock; }),
          world(session, game, coop, [this] { return clock; }),
          progress(session, game, coop),
          shared(session, game, coop),
          cam(session, game, coop, [this] { return clock; }),
          mods(session, game, {}, [this] { return clock; }),
          cosave(session, game, [this] { return clock; }),
          migrate(session, game, coop, world, progress, shared, mods, cosave, cam, [this] { return clock; }) {
        coop.set_save_hooks([this](std::int32_t target) { cosave.announce(target); },
                            [this](const Bytes& save) { cosave.starting(save); });
        coop.on_start_again([this] { cosave.start_again(); });
        router.route(mp::Owner::kSave, [this](std::int32_t sender, const Bytes& bytes) { cosave.handle(sender, bytes); });
        router.fallback([this](std::int32_t sender, const Bytes& bytes) { coop.handle(sender, bytes); });
        router.route(mp::Owner::kMods, [this](std::int32_t sender, const Bytes& bytes) { mods.handle(sender, bytes); });
        router.route(mp::Owner::kWorld, [this](std::int32_t sender, const Bytes& bytes) { world.handle(sender, bytes); });
        router.route(mp::Owner::kTether, [this](std::int32_t sender, const Bytes& bytes) { world.handle(sender, bytes); });
        router.route(mp::Owner::kProgress, [this](std::int32_t sender, const Bytes& bytes) { progress.handle(sender, bytes); });
        router.route(mp::Owner::kMigrate, [this](std::int32_t sender, const Bytes& bytes) { migrate.handle(sender, bytes); });
        router.route(mp::Owner::kShare, [this](std::int32_t sender, const Bytes& bytes) { shared.handle(sender, bytes); });
        router.route(mp::Owner::kCamera, [this](std::int32_t sender, const Bytes& bytes) { cam.handle(sender, bytes); });
        session.on_data([this](std::int32_t sender, const Bytes& bytes) { router.dispatch(sender, bytes); });
        mp::Message hello;
        hello.kind = mp::Message::Kind::kHello;
        hello.id = id;
        session.connected();
        session.handle(hello);
    }
};


inline mp::Message info(const std::vector<mp::Member>& members) {
    mp::Message m;
    m.kind = mp::Message::Kind::kPartyInfo;
    m.code = "ABCDEF";
    m.host = 1;
    m.members = members;
    m.description = "Kerwan";
    return m;
}


// What the server does with the players' DATA: a member's to the host, the host's to everyone
// else (target 0) or one. Frames other than DATA (hello, time, the lobby's own) are dropped.
inline void route(std::vector<Player*> players) {
    bool moved = true;

    while (moved) {
        moved = false;

        for (Player* from : players) {
            from->session.flush();
            std::vector<Bytes> frames;
            frames.swap(from->link.frames);

            for (const Bytes& frame : frames) {
                mp::FrameReader r;
                r.feed(frame.data(), frame.size());
                Bytes body;

                if (!r.next(body)) {
                    continue;
                }

                const mp::Request q = mp::decode_request(body);

                if (q.kind != mp::Request::Kind::kData) {
                    continue;
                }

                for (Player* to : players) {
                    const bool host_sends = from->session.is_host();
                    const bool reaches = to != from
                                         && (host_sends ? q.target == 0 || q.target == to->id
                                                        : to->session.is_host());

                    if (reaches && to->session.in_party()) {
                        mp::Message m;
                        m.kind = mp::Message::Kind::kData;
                        m.id = from->id;
                        m.bytes = q.bytes;
                        to->session.handle(m);
                        moved = true;
                    }
                }
            }
        }
    }
}


inline void tick(std::vector<Player*> players, std::uint64_t ms) {
    for (Player* p : players) {
        p->clock += ms;
        p->migrate.tick(p->clock);
        p->coop.tick(p->clock);
        p->world.tick(p->clock);
        p->progress.tick(p->clock);
        p->cosave.tick(p->clock, p->coop.playing());
        p->shared.tick(p->clock);
        p->cam.tick(p->clock);
        p->session.flush();
    }
    route(players);
}


// The host's Start, once every member has said Ready (as each member's READY reaching it would).
inline bool start_ready(Player& host) {
    for (const mp::Member& m : host.session.members()) {
        if (m.id != host.session.id() && !host.session.ready(m.id)) {
            mp::Message ready;
            ready.kind = mp::Message::Kind::kData;
            ready.id = m.id;
            ready.bytes = {static_cast<std::uint8_t>(mp::LobbyKind::kReady), 1};
            host.session.handle(ready);
        }
    }

    return host.coop.start();
}


// ---- The mods a party agrees on (party_mods.hpp) ----

inline mp::ModInfo difficulty_mod() {
    mp::ModInfo m{"difficulty", "Difficulty", "1.2.3", mp::kModGameplay, {}};
    const char* const keys[][2] = {{"enemy_count", "Enemy count"}, {"damage_taken", "Damage taken"}, {"bolts", "Bolt drops"}};

    for (const auto& k : keys) {
        m.settings.push_back({k[0], k[1], "1", "1", mp::kSettingShared, true});
    }

    m.settings.push_back({"chaos", "Chaos", "false", "off", mp::kSettingShared, true});
    m.settings.push_back({"seed", "Seed", "", "", mp::kSettingHost, true});
    m.settings.push_back({"trace", "Detailed log", "false", "off", mp::kSettingOwn, true});
    return m;
}


inline mp::ModList sample_list() {
    mp::ModList list;
    list.multiplayer = "0.9.0";
    list.openrac = "0.21.0";
    list.disc = "pal";
    list.family = "rac1-60-1";
    list.mods.push_back(difficulty_mod());
    list.mods.push_back({"item-randomizer", "Item Randomizer", "1.1.0", mp::kModGameplay, {}});
    list.mods.push_back({"keyboard-mouse", "Keyboard and Mouse", "1.2.5", mp::kModInput, {{"walk_speed", "Walk speed", "1", "1", 0, true}}});
    list.mods.push_back({"xbox-icons", "Xbox Icons", "1.0.2", mp::kModCosmetic, {}});
    return list;
}


inline mp::ModSettingInfo& setting_of(mp::ModList& list, const std::string& key) {
    for (mp::ModSettingInfo& s : list.mods[0].settings) {
        if (s.key == key) {
            return s;
        }
    }

    return list.mods[0].settings[0];
}


inline void tick_mods(std::vector<Player*> players, std::uint64_t ms) {
    for (Player* p : players) {
        p->mods.tick(p->clock + ms);
    }

    tick(players, ms);
}


/* A player with its list, gated as the mod gates it. */
inline void gate(Player& p, mp::ModList list) {
    p.coop.set_build("mp " + std::to_string(mp::mods_digest(list)));
    p.mods.set_own(std::move(list));
    p.session.set_ready_gate([&p](std::int32_t id) {
        return p.session.is_host() ? p.mods.may_play(id) : p.mods.heard_host() && p.mods.differences().empty();
    });
    p.coop.set_gate([&p](std::int32_t id) { return p.mods.may_play(id); });
}


inline mp::Message ready_of(std::int32_t id) {
    mp::Message m;
    m.kind = mp::Message::Kind::kData;
    m.id = id;
    m.bytes = {static_cast<std::uint8_t>(mp::LobbyKind::kReady), 1};
    return m;
}


// The slot in a game of the first live moby of a class (-1 none), and how many of it there are.
inline int slot_of_class(const FakeGame& game, int oclass) {
    for (const mp::MobyState& m : game.level) {
        if (m.oclass == oclass && (m.flags & mp::kMobyDead) == 0) {
            return m.slot;
        }
    }
    return -1;
}


inline int count_of_class(const FakeGame& game, int oclass) {
    return static_cast<int>(std::count_if(game.level.begin(), game.level.end(), [&](const mp::MobyState& m) {
        return m.oclass == oclass && (m.flags & mp::kMobyDead) == 0;
    }));
}


// Whether a game was given something (FakeGame's `given`), and whether it was asked for a scene.
inline bool was_given(const FakeGame& game, const std::string& what) {
    return std::find(game.given.begin(), game.given.end(), what) != game.given.end();
}


inline bool asked_scene(const FakeGame& game) {
    return std::any_of(game.calls.begin(), game.calls.end(),
                       [](const std::string& c) { return c.rfind("scene", 0) == 0 || c == "skip"; });
}




// A party in level 0, everyone in control, the logs kept.
struct SceneParty {
    Player host{1}, member{2};
    std::vector<std::string> host_log, member_log;

    SceneParty() {
        host.coop.on_log([this](const std::string& l) { host_log.push_back(l); });
        member.coop.on_log([this](const std::string& l) { member_log.push_back(l); });
        const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
        host.session.handle(info(both));
        member.session.handle(info(both));
        host.coop.choose({}, "New game");
        CHECK(start_ready(host));
        route({&host, &member});
        tick({&host, &member}, 16);
        host.game.now = member.game.now = {mp::kPhasePlaying, 0, 2};
        run(500);
        host.game.calls.clear();
        member.game.calls.clear();
    }

    void run(std::uint64_t ms) {
        for (std::uint64_t t = 0; t < ms; t += 50) {
            tick({&host, &member}, 50);
        }
    }

    static long count(const FakeGame& g, const std::string& call) {
        return static_cast<long>(std::count(g.calls.begin(), g.calls.end(), call));
    }

    static bool logged(const std::vector<std::string>& log, const std::string& start, std::size_t from = 0) {
        return std::any_of(log.begin() + static_cast<std::ptrdiff_t>(std::min(from, log.size())), log.end(),
                           [&](const std::string& l) { return l.rfind(start, 0) == 0; });
    }
};


inline mp::Bytes skip_bytes(std::int32_t origin, std::int32_t play, std::uint8_t what, std::uint16_t scene) {
    return mp::Writer()
        .u8(static_cast<std::uint8_t>(mp::GameKind::kSkip))
        .i32(origin)
        .u8(0)
        .u8(9)
        .i32(play)
        .u8(what)
        .u8(static_cast<std::uint8_t>(scene >> 8))
        .u8(static_cast<std::uint8_t>(scene))
        .bytes();
}


// Leaving (coop.hpp). Three players in the host's level 4: each plays the party's game.
struct Party {
    Player host{1}, a{2}, b{3};
    const std::vector<mp::Member> all = {{1, "P1"}, {2, "P2"}, {3, "P3"}};

    Party() {
        host.session.handle(info(all));
        a.session.handle(info(all));
        b.session.handle(info(all));
        host.coop.choose({}, "New game");
        CHECK(start_ready(host));
        route(players());
        tick(players(), 16);

        for (Player* p : players()) {
            p->game.now = {mp::kPhasePlaying, 4, 2};
            p->game.calls.clear();
        }

        tick(players(), 16);
    }

    std::vector<Player*> players() { return {&host, &a, &b}; }

    // The server's word that `id` left, to everyone still in the party.
    void left(std::int32_t id) {
        mp::Message m;
        m.kind = mp::Message::Kind::kPartyLeft;
        m.id = id;

        for (Player* p : players()) {
            if (p->id != id && p->session.in_party()) {
                p->session.handle(m);
            }
        }
    }
};


// What the server says of connections and slots (its RESUME), as Sessions read it.
inline mp::Message udp_offer_of(std::int64_t resume_token) {
    mp::Message m;
    m.kind = mp::Message::Kind::kUdp;
    m.features = mp::kFeatureKick | mp::kFeatureResume;
    m.resume_token = resume_token;
    return m;
}

inline void offer_resume(const std::vector<Player*>& players) {
    for (Player* p : players) {
        p->session.handle(udp_offer_of(1000 + p->id));
    }
}

inline void server_away(const std::vector<Player*>& others, std::int32_t id, bool back = false) {
    mp::Message m;
    m.kind = mp::Message::Kind::kPartyAway;
    m.id = id;
    m.back = back;

    for (Player* p : others) {
        p->session.handle(m);
    }
}

// The connection of `p` goes: the others are told, and nothing it sent reaches anyone.
inline void lose(Player& p, const std::vector<Player*>& others) {
    p.session.disconnected();
    p.link.frames.clear();
    server_away(others, p.id);
}

// The server gives the party to `host`.
inline void server_host(const std::vector<Player*>& players, std::int32_t host, std::int32_t epoch) {
    mp::Message m;
    m.kind = mp::Message::Kind::kPartyHostChanged;
    m.host = host;
    m.epoch = epoch;

    for (Player* p : players) {
        p->session.handle(m);
    }
}

// Its answer to RESUME on a new connection, the party as `party` says.
inline void come_back(Player& p, const std::vector<Player*>& others, mp::Message party) {
    p.session.connected();
    p.link.frames.clear();
    mp::Message hello;
    hello.kind = mp::Message::Kind::kHello;
    hello.id = 900 + p.id;  // the new connection's session, for a moment
    p.session.handle(hello);
    p.link.frames.clear();
    hello.id = p.id;
    p.session.handle(hello);
    p.session.handle(udp_offer_of(1000 + p.id));
    p.session.handle(party);
    server_away(others, p.id, true);
}


inline std::size_t count_of(const FakeGame& game, const std::string& call) {
    return static_cast<std::size_t>(std::count(game.calls.begin(), game.calls.end(), call));
}


// A player out of its level and into the title: Loading, then the title's main menu.
inline void reach_title(Player& p) {
    p.game.now = {mp::kPhaseLoading, -1, 3};
    tick({&p}, 16);
    p.game.now = {mp::kPhaseFrontEnd, -1, 3};
    tick({&p}, 16);
}


// ---- The network as it is: a seeded link and a model of the server ----

// What the server does with a player's packets (Server's README, Limits and UDP): DATA and state
// each 60 a second over a burst of 120, state over it dropped without a word, DATA over it dropped
// and a strike, 20 strikes (forgiven 1 a second) closing the connection; a member's traffic reaching
// the host only. The link on either side of it loses, delays, reorders and repeats datagrams, and
// delays TCP in order. All of it from one seed, on one clock.
struct SimLink {
    double loss = 0;       // of each datagram, each way
    double latency = 0;    // ms, each way
    double jitter = 0;     // ms more, at most
    double reorder = 0;    // of each datagram: late by a good deal more
    double duplicate = 0;  // of each datagram: arrives twice
    bool udp = true;       // false: UDP is blocked, state goes by TCP
};


struct SimNet {
    struct Packet {
        double at = 0;
        Player* to = nullptr;
        std::int32_t from = 0;
        Bytes bytes;
    };

    struct Limits {
        mp::Pacer data{120, 60};
        mp::Pacer state{120, 60};
        mp::Pacer strikes{20, 1};
        double tcp_last = 0;
    };

    SimLink link;
    std::mt19937 random;
    std::vector<Player*> players;
    std::vector<Packet> flying;
    std::map<std::int32_t, Limits> limits;
    std::uint64_t now = 1000;
    // The shared progress on this link too (off: the seeded runs keep the draws they were tuned on).
    bool shared = false;
    long data_drops = 0, state_drops = 0, kicked = 0, datagrams_lost = 0, state_sent = 0;
    /* What the host and the members sent, by kind (a message's first byte), and what each sent
     * up to the server by way of TCP (DATA; a BATCH is one). */
    struct Traffic {
        long messages = 0, bytes = 0;
    };
    std::map<int, Traffic> host_state, member_state, host_data, member_data;
    /* The host's state as the play stopped: what is left after it is what settling took. */
    std::map<int, Traffic> played_state;
    /* The longest gap, in ms, between two WORLDs reaching a member with a word of one moby, of the
     * ids `gaps_of` says to watch, while `gaps_on`. */
    std::function<bool(int)> gaps_of;
    bool gaps_on = false;
    std::map<std::pair<std::int32_t, int>, double> heard_at;
    double worst_gap = 0;

    SimNet(std::vector<Player*> who, SimLink l, unsigned seed) : link(l), random(seed), players(std::move(who)) {
        for (Player* p : players) {
            p->link.udp = link.udp;
        }
    }

    double uniform() { return std::uniform_real_distribution<double>(0.0, 1.0)(random); }

    double delay() { return link.latency + uniform() * link.jitter; }

    bool reaches(Player* from, std::int32_t target, Player* to) const {
        return to != from && (from->id == 1 ? target == 0 || target == to->id : to->id == 1);
    }

    void tcp(Player* from, std::int32_t target, const Bytes& bytes) {
        for (Player* to : players) {
            if (reaches(from, target, to)) {
                Limits& l = limits[to->id];
                l.tcp_last = std::max(l.tcp_last, static_cast<double>(now) + 2 * delay());
                flying.push_back({l.tcp_last, to, from->id, bytes});
            }
        }
    }

    void one_datagram(Player* from, Player* to, const Bytes& bytes) {
        for (int leg = 0; leg < 2; ++leg) {
            if (uniform() < link.loss) {
                ++datagrams_lost;
                return;
            }
        }

        double at = static_cast<double>(now) + 2 * delay();
        at += uniform() < link.reorder ? 60.0 + 3 * link.jitter : 0.0;
        flying.push_back({at, to, from->id, bytes});

        if (uniform() < link.duplicate) {
            flying.push_back({at + 5.0, to, from->id, bytes});
        }
    }

    void note_gaps(const Packet& f) {
        if (!gaps_on || !gaps_of || f.bytes.empty() || f.bytes[0] != static_cast<std::uint8_t>(mp::WorldKind::kWorld)) {
            return;
        }

        int level = -1;
        std::int64_t stamp = 0;
        std::vector<mp::WorldEntry> entries;

        if (!mp::decode_world(f.bytes, level, stamp, entries)) {
            return;
        }

        for (const mp::WorldEntry& e : entries) {
            if (!gaps_of(e.id)) {
                continue;
            }

            const auto seen = heard_at.find({f.to->id, e.id});

            if (seen != heard_at.end()) {
                worst_gap = std::max(worst_gap, f.at - seen->second);
                seen->second = f.at;
            } else {
                heard_at[{f.to->id, e.id}] = f.at;
            }
        }
    }

    void step(std::uint64_t ms) {
        now += ms;

        for (Player* p : players) {
            p->clock = now;
            Limits& l = limits[p->id];
            std::vector<Bytes> frames;
            frames.swap(p->link.frames);

            for (const Bytes& frame : frames) {
                mp::FrameReader r;
                r.feed(frame.data(), frame.size());
                Bytes body;

                if (!r.next(body)) {
                    continue;
                }

                const mp::Request q = mp::decode_request(body);

                if (q.kind != mp::Request::Kind::kData) {
                    continue;
                }

                if (!l.data.take(now)) {
                    ++data_drops;
                    kicked += l.strikes.take(now) ? 0 : 1;
                    continue;
                }

                Traffic& t = (p->id == 1 ? host_data : member_data)[q.bytes.empty() ? 0 : q.bytes[0]];
                ++t.messages;
                t.bytes += static_cast<long>(q.bytes.size());
                tcp(p, q.target, q.bytes);
            }

            std::vector<std::pair<std::int32_t, Bytes>> datagrams;
            datagrams.swap(p->link.datagrams);

            for (const auto& [target, bytes] : datagrams) {
                ++state_sent;
                Traffic& t = (p->id == 1 ? host_state : member_state)[bytes.empty() ? 0 : bytes[0]];
                ++t.messages;
                t.bytes += static_cast<long>(bytes.size());

                if (!l.state.take(now)) {
                    ++state_drops;
                    continue;
                }

                for (Player* to : players) {
                    if (reaches(p, target, to)) {
                        one_datagram(p, to, bytes);
                    }
                }
            }
        }

        std::vector<Packet> later;
        // Stable: a stream's messages due at the same moment arrive in the order they went (TCP's).
        std::stable_sort(flying.begin(), flying.end(), [](const Packet& a, const Packet& b) { return a.at < b.at; });

        for (Packet& f : flying) {
            if (f.at > static_cast<double>(now)) {
                later.push_back(std::move(f));
            } else if (f.to->session.in_party()) {
                mp::Message m;
                m.kind = mp::Message::Kind::kData;
                m.id = f.from;
                m.bytes = f.bytes;
                note_gaps(f);
                f.to->session.handle(m);
            }
        }

        flying.swap(later);

        for (Player* p : players) {
            p->migrate.tick(now);
            p->coop.tick(now);
            p->world.tick(now);
            p->progress.tick(now);

            if (shared) {
                p->shared.tick(now);
            }
            p->session.flush();
        }
    }
};


/* The games of a party in level 3, its mobys placed alike in each (each game's slots its own: one
 * spot on per player, so a key is the only name they share): `enemies` driven ones spread over the
 * level, `crates` crates among them. */
struct SimWorld {
    static constexpr std::uint32_t kRuns = mp::kMobyUpdates | mp::kMobyPlaced;
    int enemies = 0;
    int crates = 0;
    float spread = 8.0f;

    std::vector<mp::MobyState> made(int shift, const std::set<int>& skipped = {}) const {
        std::vector<mp::MobyState> out;
        int slot = shift;

        for (int key = 0; key < enemies + crates; ++key) {
            if (skipped.count(key) != 0) {
                continue;
            }

            const bool crate = key >= enemies;
            mp::MobyState m = moby(slot++, key, crate ? 500 : 300, kRuns | (crate ? 0 : mp::kMobyCreature), 0.0f);
            m.pose.position[0] = static_cast<float>(key % 40) * spread;
            m.pose.position[1] = static_cast<float>(key / 40) * spread;
            out.push_back(m);
        }

        return out;
    }
};


inline void join_party(std::vector<Player*> all, const std::vector<mp::Member>& members) {
    for (Player* p : all) {
        p->session.handle(info(members));
    }

    for (Player* p : all) {
        if (p->id != 1) {
            p->session.toggle_ready();
        }
    }

    route(all);
}


inline const mp::MobyState* by_key(const FakeGame& game, int key) {
    for (const mp::MobyState& m : game.level) {
        if (m.key == key) {
            return &m;
        }
    }

    return nullptr;
}


/* The game rebuilding its level as a death does: every moby new, the records `skipped` left out (so
 * the slots after them move), the spawned gone, nothing driven, the hits posted to the old ones gone
 * with them (the port writes a hit into its moby: a test's list of them is that, by slot); the same
 * level. */
inline void remake(Player& p, const SimWorld& w, const std::set<int>& skipped = {}, int shift = 0) {
    p.game.level = w.made(shift, skipped);
    p.game.drives.clear();
    p.game.hits.clear();
    ++p.game.now.loads;
}


/* The placed mobys' slots in a run, clear of the fake's figures and spawns. */
constexpr int kSimSlots = 1000;

struct SimResult {
    int wrong_place = 0, wrong_hidden = 0, wrong_crates = 0;
    long bolts_made = 0, bolts_credited = 0;
    /* From the end of the play: when everything stood as on the host, for good (-1: not by the end
     * of the settling), and the largest number of mobys wrong on the way. */
    long settled_ms = -1;
    int most_wrong = 0;
    /* The longest a moving moby went unheard by a member while the party played, ms. */
    double worst_gap_ms = 0;
};


/* What a run plays. The default is the run the sim tests have always had: all 120 enemies walking,
 * 30 crates, 10 seconds for the link to settle. */
struct SimPlan {
    int enemies = 120;
    int crates = 30;
    int walk_every = 1;       // every n-th enemy walks: a large share of a level only stands there
    bool fire = false;        // in the second half of the play the host and a member fire
    int settle_ms = 10000;
    float spread = 8.0f;      // between one moby and the next
};


/* How many of the host's mobys differ from each member's, as `r` counts them. */
inline void compare_sim(const std::vector<Player*>& all, SimResult& r) {
    r.wrong_place = r.wrong_hidden = r.wrong_crates = 0;
    const FakeGame& host = all[0]->game;

    for (std::size_t i = 1; i < all.size(); ++i) {
        std::map<int, const mp::MobyState*> theirs;

        for (const mp::MobyState& c : all[i]->game.level) {
            if (c.key >= 0) {
                theirs.emplace(c.key, &c);
            }
        }

        for (const mp::MobyState& h : host.level) {
            if (h.key < 0) {
                continue;
            }

            const auto it = theirs.find(h.key);
            const mp::MobyState* c = it != theirs.end() ? it->second : nullptr;
            const bool here_dead = (h.flags & mp::kMobyDead) != 0;
            const bool there_dead = c == nullptr || (c->flags & mp::kMobyDead) != 0;

            if (h.oclass == 500) {
                r.wrong_crates += here_dead != there_dead ? 1 : 0;
            } else if (c != nullptr) {
                const float dy = c->pose.position[1] - h.pose.position[1];
                r.wrong_place += std::fabs(dy) > 0.5f ? 1 : 0;
                r.wrong_hidden += ((c->flags ^ h.flags) & mp::kMobyHidden) != 0 ? 1 : 0;
            }
        }
    }
}


/* One run: a party of 1 + `members` plays `seconds`: the host's enemies move and hide, crates go
 * from every side, each break drops a bolt from the host that a member collects, and a member dies
 * half way; then everything stops and the link gets plan.settle_ms to settle. What differs then is a
 * desync no time will mend. */
inline SimResult run_sim(const SimLink& link, unsigned seed, int members, double seconds, SimNet** out = nullptr,
                         const SimPlan& plan = SimPlan{}) {
    static std::vector<std::unique_ptr<Player>> keep;
    keep.clear();
    keep.push_back(std::make_unique<Player>(1));
    std::vector<Player*> all = {keep[0].get()};
    std::vector<mp::Member> list = {{1, "P1"}};

    for (int i = 0; i < members; ++i) {
        keep.push_back(std::make_unique<Player>(2 + i));
        all.push_back(keep.back().get());
        list.push_back({2 + i, "P" + std::to_string(2 + i)});
    }

    join_party(all, list);
    Player& host = *all[0];
    host.coop.choose({}, "New game");
    CHECK(start_ready(host));
    route(all);
    SimWorld w{plan.enemies, plan.crates, plan.spread};
    const auto walks = [&](int key) { return key >= 0 && key < w.enemies && key % plan.walk_every == 0; };

    for (std::size_t i = 0; i < all.size(); ++i) {
        all[i]->game.now = {mp::kPhasePlaying, 3, 2};
        all[i]->game.level = w.made(kSimSlots + static_cast<int>(i) * 3);
        all[i]->game.hero_pose.position[0] = 40.0f * static_cast<float>(i);
    }

    static SimNet* net_kept = nullptr;
    static std::unique_ptr<SimNet> net;
    net = std::make_unique<SimNet>(all, link, seed);
    net_kept = net.get();
    net->gaps_of = walks;
    std::mt19937 rng(seed * 7 + 1);
    SimResult r;
    const auto ticks = static_cast<int>(seconds * 1000.0 / 16.0);
    std::uint16_t next_bolt = 0;
    bool died = false;
    std::set<int> broken;
    int last_wrong = ticks - 1;

    for (int t = 0; t < ticks + plan.settle_ms / 16; ++t) {
        const bool playing = t < ticks;
        // Once everyone is under way, up to the stop and not across the member's own death.
        net->gaps_on = playing && t * 16 >= 2000 && (t < ticks / 2 || t * 16 >= ticks * 8 + 2000);

        if (!net->gaps_on) {
            net->heard_at.clear();
        }

        // The host's enemies walk; some hide and show.
        if (playing) {
            for (mp::MobyState& m : host.game.level) {
                if (m.key >= 0 && m.key < w.enemies) {
                    if (walks(m.key)) {
                        m.pose.position[1] += 0.05f * (m.key % 2 == 0 ? 1.0f : -1.0f);
                    }

                    if (rng() % 2000 == 0) {
                        m.flags ^= mp::kMobyHidden;
                    }
                }
            }

            // Now and then a crate goes, here or there, unless it has.
            if (rng() % 40 == 0) {
                const int key = w.enemies + static_cast<int>(rng() % static_cast<unsigned>(w.crates));
                Player& who = *all[rng() % all.size()];
                const int slot = who.game.moby_slot(key);

                if (slot >= 0 && broken.count(key) == 0) {
                    broken.insert(key);
                    who.game.die(slot);
                }
            }

            // The first member dies half way.
            if (!died && t == ticks / 2 && members > 0) {
                died = true;
                remake(*all[1], w, {0, 1, 2}, kSimSlots);  // records skipped: slots mean other mobys now
            }

            // The party fires: a held trigger's ten shots a second, from two players at once.
            if (plan.fire && t >= ticks / 2 && t % 6 == 0) {
                for (std::size_t i = 0; i < std::min<std::size_t>(2, all.size()); ++i) {
                    mp::Shot shot;
                    shot.item = 2;
                    shot.position[0] = static_cast<float>(t);
                    all[i]->game.shots_out.push_back(shot);
                }
            }
        }

        // A replayed break: the crate's own update takes the hit.
        for (Player* p : all) {
            for (const mp::Hit& h : p->game.hits) {
                const auto it = std::find_if(p->game.level.begin(), p->game.level.end(), [&](const mp::MobyState& m) {
                    return m.slot == h.victim;
                });

                if (it != p->game.level.end() && it->oclass == 500 && it->key >= 0) {
                    const int key = it->key;
                    p->game.die(h.victim);

                    // The host's copy drops a bolt, landed by its next update.
                    if (p == &host) {
                        mp::MobyState bolt = moby(4000 + next_bolt++, -1, 13, mp::kMobyUpdates, 0.0f);
                        bolt.pose.position[0] = static_cast<float>(key);
                        bolt.state = mp::kBoltHovering;
                        host.game.level.push_back(bolt);
                        ++r.bolts_made;
                    }
                }
            }

            p->game.hits.clear();
        }

        // A bolt run by a member's game: it collects it (each once).
        for (std::size_t i = 1; i < all.size(); ++i) {
            Player& m = *all[i];
            std::vector<int> mine;

            for (const mp::MobyState& b : m.game.level) {
                if (b.key < 0 && mp::bolt_class(b.oclass) && m.game.drives.count(b.slot) == 0 && b.slot >= 500) {
                    mine.push_back(b.slot);
                }
            }

            for (const int slot : mine) {
                if (m.world.mobys() != 0 && m.world.collect(slot)) {
                    ++r.bolts_credited;  // its own: never one of the host's
                }

                m.game.die(slot);
            }
        }

        net->step(16);

        if (t + 1 == ticks) {
            net->played_state = net->host_state;
        }

        // Settling: when the last moby came right.
        if (!playing && t % 4 == 0) {
            compare_sim(all, r);
            const int wrong = r.wrong_place + r.wrong_hidden + r.wrong_crates;
            r.most_wrong = std::max(r.most_wrong, wrong);
            last_wrong = wrong != 0 ? t : last_wrong;
        }
    }

    // What differs now stays.
    compare_sim(all, r);
    r.settled_ms = r.wrong_place + r.wrong_hidden + r.wrong_crates != 0 ? -1 : (last_wrong + 1 - ticks) * 16L;
    r.worst_gap_ms = net->worst_gap;

    if (out != nullptr) {
        *out = net_kept;
    }

    return r;
}


// Each area's tests, run by main in play_test.cpp.
void run_poses_tests();
void run_coop_tests();
void run_world_tests();
void run_mods_tests();
void run_pause_tests();
void run_bolts_tests();
void run_scenes_tests();
void run_party_tests();
void run_saves_tests();
void run_sim_tests();
void run_sync_tests();
void run_save_tests();
void run_cross_tests();
void run_tags_tests();
void run_join_tests();
void run_shared_tests();
void run_lobby_tests();
void run_death_tests();
void run_camera_tests();
void run_hold_tests();
void run_migrate_tests();

}  // namespace mpt
