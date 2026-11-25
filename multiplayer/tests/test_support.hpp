// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The play tests' shared support (play_test.cpp, play_*_test.cpp): CHECK, the recorders, the fake
// games and the players that talk through a router, and the simulated links.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "camscripts.hpp"
#include "coop.hpp"
#include "cosave.hpp"
#include "menu.hpp"
#include "migrate.hpp"
#include "play.hpp"
#include "progress.hpp"
#include "protocol.hpp"
#include "session.hpp"
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

// Returns a pose in `level` at x with x velocity `vx` and everything else zero.
inline mp::Pose pose_at(float x, float vx = 0.0f, int level = 3) {
    mp::Pose p;
    p.level = level;
    p.position[0] = x;
    p.velocity[0] = vx;
    return p;
}

// Returns a held piece with the given slot, part, item, class and joint and fixed animation values.
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

// A transport that records the frames and datagrams a session sends, in place of a socket.
struct Recorder : mp::Transport {
    std::vector<Bytes> frames;
    // With `udp` on, state stays out of the frames: the datagrams, for SimNet, and their targets.
    bool udp = false;
    std::vector<std::pair<std::int32_t, Bytes>> datagrams;

    // Records a frame sent over TCP.
    void send(Bytes frame) override { frames.push_back(std::move(frame)); }

    // Returns true while datagrams are off, so state travels in TCP frames.
    bool state_needs_tcp(std::size_t /*bytes*/) const override { return !udp; }

    // Records a state datagram when `udp` is on, else sends it as a DATA frame if `tcp` is set.
    void send_state(std::int32_t target, const Bytes& bytes, bool tcp) override {
        // With UDP on, state leaves as a datagram, as it does over the real link.
        if (udp) {
            datagrams.emplace_back(target, bytes);
        } else if (tcp) {
            send(mp::data(target, bytes));
        }
    }
};

// A game that records what the mod asks of it and answers from fields a test sets.
struct FakeGame : mp::Game {
    mp::GameStatus now{mp::kPhaseFrontEnd, -1, 1};
    std::vector<std::string> calls;
    Bytes started_save;
    Bytes live_save;
    int next_figure = 0;
    std::map<int, mp::Pose> figures;
    // A grab: whether the game holds a hero, what the update last saw, what it did.
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

    // The mods' rules (party_mods.hpp): what this game's publish, which mods take the host's, and
    // what reached them, in order, with what the game had been asked to do by then.
    std::vector<mp::PartyRules> published;
    std::set<std::string> takes;

    // One delivery of a mod's rules: the mod id, the bytes and how many game calls had been made by
    // then.
    struct Delivery {
        std::string id;
        Bytes data;
        std::size_t calls;
    };

    std::vector<Delivery> delivered;
    // The mods told that this game hosts the party now (hand_over_rules).
    std::vector<std::string> handed_over;

    // A save refused (save_check.hpp); the level's sim digest, per level.
    bool saves_usable = true;
    std::map<int, std::uint32_t> digests;

    // Returns `saves_usable` whatever the save holds.
    bool save_usable(const Bytes&) override { return saves_usable; }

    // A port that places the hero (`placeable`): the places asked, and, when `place_works`, the
    // hero there at once.
    bool placeable = false;
    bool place_works = true;

    // One request to place the hero: the spot, the yaw and whether to stand beside it.
    struct Placed {
        float at[3];
        float yaw;
        bool beside;
    };

    std::vector<Placed> placed;
    std::vector<bool> quiet;
    bool gifts_quiet = false;

    // Returns whether this game can place the hero.
    bool can_place() override { return placeable; }

    // Records a placement and, when `place_works`, moves the hero there; returns false if placing
    // is off.
    bool place(const float* position, float yaw, bool beside) override {
        // A port that cannot place the hero refuses the request.
        if (!placeable) {
            return false;
        }

        placed.push_back({{position[0], position[1], position[2]}, yaw, beside});

        // A port whose placing fails still sees the request but leaves the hero where it was.
        if (place_works) {
            std::copy(position, position + 3, hero_pose.position);
        }

        return true;
    }

    // Returns a name made from the level number.
    std::string level_name(int level) override { return "Planet" + std::to_string(level); }

    // Records whether gifts are given without a banner.
    void quiet_gifts(bool on) override {
        quiet.push_back(on);
        gifts_quiet = on;
    }

    // Returns the digest set for the level, or 0 when none is set.
    std::uint32_t sim_digest(int level) override {
        const auto it = digests.find(level);
        return it != digests.end() ? it->second : 0;
    }

    // The card: `card_on` a port with the filter. What the filter holds, the co-op slots and their
    // notes, and the game's own card, where a save the game makes lands with the filter off: a
    // test's own_saves is a player's story saves.
    bool card_on = false;
    int filter = mp::kCardPass;
    int hint = -1;
    bool write_refused = false;
    bool pass_refused = false;
    bool redirect_refused = false;
    std::map<int, Bytes> slots;
    std::map<int, std::string> notes;
    Bytes own_saves;
    std::uint32_t saves = 0;
    std::int64_t wall = 1000;

    // The game saves, as the port takes it: into the party's slot under the filter, else the card.
    void game_saves(const Bytes& bytes) {
        if (filter == mp::kCardRedirect) {
            slots[hint] = bytes;
            ++saves;
        } else {
            own_saves = bytes;
        }
    }

    // Returns whether this game has a card filter.
    bool has_card_filter() override { return card_on; }

    // Sets the filter mode, unless the test made the port refuse that mode.
    bool card_filter(int mode) override {
        // A refused mode leaves the filter as it was.
        if (mode == mp::kCardPass && pass_refused) {
            return false;
        }

        // The redirect mode can be refused on its own.
        if (mode == mp::kCardRedirect && redirect_refused) {
            return false;
        }

        filter = mode;
        return true;
    }

    // Records the slot a save made now belongs to.
    void card_slot(int slot) override { hint = slot; }

    // Stores a save in a slot, only while the filter redirects and writes are not refused.
    bool card_write(int slot, const Bytes& save) override {
        // A save outside the redirect filter would land on the game's own card, not in a slot.
        if (write_refused || filter != mp::kCardRedirect) {
            return false;
        }

        slots[slot] = save;
        return true;
    }

    // Returns the slot's save, or nothing for an empty slot.
    Bytes card_read(int slot) override { return slots.count(slot) != 0 ? slots[slot] : Bytes{}; }

    // Returns how many saves the game has made into the party's slot.
    std::uint32_t card_saves() override { return saves; }

    // Returns the slot's note, or an empty string.
    std::string card_note(int slot) override {
        return notes.count(slot) != 0 ? notes[slot] : std::string();
    }

    // Stores a slot's note.
    void card_note_write(int slot, const std::string& text) override { notes[slot] = text; }

    // Returns the wall clock a test set, in seconds.
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

    // Copies the running script's view out and returns whether a script is running.
    bool camera_view(mp::CameraView& out) override {
        out = script_view;
        return script_on;
    }

    // Records the script id asked for.
    bool camera_play(int id) override {
        camera_calls.push_back(id);
        return true;
    }

    // Takes the camera view and counts it, unless the game keeps the camera.
    bool camera_show(const float* eye, const float* rotation) override {
        // A game that keeps the camera refuses the mod's view.
        if (camera_taken) {
            return false;
        }

        ++camera_shows;
        std::copy_n(eye, 3, shown_eye);
        std::copy_n(rotation, 3, shown_rotation);
        return true;
    }

    // Returns the rules this game publishes.
    std::vector<mp::PartyRules> party_rules() override { return published; }

    // Records the rules for a mod in `takes` and refuses any other mod.
    bool deliver_rules(const std::string& id, const Bytes& data) override {
        // A mod the game does not take gets no rules.
        if (takes.count(id) == 0) {
            return false;
        }

        delivered.push_back({id, data, calls.size()});
        return true;
    }

    // Records a mod in `takes` told that this game hosts the party now, and refuses any other.
    bool hand_over_rules(const std::string& id) override {
        // A mod the game does not take was given no rules to keep.
        if (takes.count(id) == 0) {
            return false;
        }

        handed_over.push_back(id);
        return true;
    }

    // Returns the status a test set.
    mp::GameStatus status() override { return now; }

    // Records a start request and keeps the save it was given.
    bool start(int level, const Bytes& save) override {
        calls.push_back("start " + std::to_string(level) + " " + std::to_string(save.size()));
        started_save = save;
        return true;
    }

    // Records a travel request.
    bool travel(int level) override {
        calls.push_back("travel " + std::to_string(level));
        return true;
    }

    // Records a request to open home.
    bool open_home() override {
        calls.push_back("home");
        return true;
    }

    // Returns the live save and copies the mission row as it stands at capture.
    Bytes capture() override {
        captured_row = mission_row_bytes;
        return live_save;
    }

    // Copies out the hero's pose and returns true only while a level is playing.
    bool hero(mp::Pose& out) override {
        out = hero_pose;
        out.level = now.level;
        return mp::in_level(now.phase);
    }

    // Adds a figure and returns its id.
    int figure_create() override {
        figures[next_figure] = mp::Pose{};
        return next_figure++;
    }

    // Stores a pose for a known figure and returns false for an unknown id.
    bool figure_pose(int id, const mp::Pose& pose) override {
        // Only a figure that exists takes a pose.
        if (figures.count(id) == 0) {
            return false;
        }

        figures[id] = pose;
        return true;
    }

    // Removes a figure.
    void figure_remove(int id) override { figures.erase(id); }

    // The hero's shots to give, and the figures' shots fired (figure id, shot).
    std::vector<mp::Shot> shots_out;
    std::vector<std::pair<int, mp::Shot>> shots_in;

    // Returns the shots a test queued and clears the queue.
    std::vector<mp::Shot> hero_shots() override {
        std::vector<mp::Shot> out;
        out.swap(shots_out);
        return out;
    }

    // Records a shot fired by a known figure and returns false for an unknown id.
    bool figure_shot(int id, const mp::Shot& shot) override {
        // Only a figure that exists can fire.
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
    std::array<std::uint8_t, mp::kMissions> captured_row{};
    bool mission_row_known = true;

    // Returns whether the hero can be revived.
    bool can_revive() override { return revivable; }

    // Records whether death is held.
    void hold_death(bool on) override { holding = on; }

    // Counts a revive request.
    bool revive() override {
        ++revives;
        return true;
    }

    // Counts a respawn request.
    bool respawn() override {
        ++respawns;
        return true;
    }

    bool takeoff_works = false;
    std::vector<int> takeoffs;

    // Records the level asked for and returns `takeoff_works`.
    bool ship_takeoff(int level) override {
        takeoffs.push_back(level);
        return takeoff_works;
    }

    // Copies the mission row out and returns whether the row is known.
    bool mission_row(int /*level*/, std::uint8_t* out) override {
        std::copy(mission_row_bytes.begin(), mission_row_bytes.end(), out);
        return mission_row_known;
    }

    // Zeroes each mission byte the flags name.
    bool mission_clear(int /*level*/, const std::uint8_t* flags) override {
        // Only the bytes the flags name change.
        for (std::size_t i = 0; i < mission_row_bytes.size(); ++i) {
            mission_row_bytes[i] = flags[i] != 0 ? 0 : mission_row_bytes[i];
        }

        return true;
    }

    // Sets each mission byte the flags name to 0xFF.
    bool mission_set(int /*level*/, const std::uint8_t* flags) override {
        // Only the bytes the flags name change.
        for (std::size_t i = 0; i < mission_row_bytes.size(); ++i) {
            mission_row_bytes[i] = flags[i] != 0 ? 0xFF : mission_row_bytes[i];
        }

        return true;
    }

    // The world: a level's mobys by slot, the drives, kills, spawns, hits, stand-ins and bolts. The
    // hero's moby is slot 99, figure id N is slot 100 + N.
    std::vector<mp::MobyState> level;
    std::set<int> drives;
    std::vector<std::string> world_calls;
    std::vector<mp::Hit> hits;
    int stood = 0;
    int bolt_count = 0;

    // Returns the level's mobys, or none while `listing_blank`.
    std::vector<mp::MobyState> mobys() override {
        return listing_blank ? std::vector<mp::MobyState>{} : level;
    }

    // Returns the slot of the live moby with a key, or -1.
    int moby_slot(int key) override {
        // A killed placed moby stays listed, dead, so dead ones are passed over.
        for (const mp::MobyState& m : level) {
            if (m.key == key && (m.flags & mp::kMobyDead) == 0) {
                return m.slot;
            }
        }

        return -1;
    }

    // Adds a slot to the driven set or removes it.
    bool moby_drive(int slot, bool on) override {
        // A slot is either taken over by the mod (driven) or left to the game.
        if (on) {
            drives.insert(slot);
        } else {
            drives.erase(slot);
        }

        return true;
    }

    // Sets the pose of the moby in a slot and returns false when none is there.
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

    // Kills the live moby in a slot as the port does: a placed one stays listed as dead, a spawned
    // one goes.
    bool die(int slot) {
        for (auto it = level.begin(); it != level.end(); ++it) {
            // A slot that is absent or already dead cannot be killed again.
            if (it->slot != slot || (it->flags & mp::kMobyDead) != 0) {
                continue;
            }

            // A placed moby has a key of 0 or more; a spawned one has none.
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

    // Records whether the overlay pauses the game.
    void overlay_pause(bool on) override { overlay = on; }

    // Records a skip request.
    void skip() override { calls.push_back("skip"); }

    // A scene asked for (the test plays it by setting `now`), and -1 a request dropped.
    // Records a scene request and returns whether a level is playing.
    bool play_scene(int scene) override {
        calls.push_back("scene " + std::to_string(scene));
        return mp::in_level(now.phase);
    }

    // Adds a moby of a class and returns its slot, from 500 up.
    int moby_spawn(int oclass, const mp::Pose& pose) override {
        mp::MobyState m;
        m.slot = 500 + static_cast<int>(level.size());
        m.oclass = oclass;
        m.pose = pose;
        level.push_back(m);
        world_calls.push_back("spawn " + std::to_string(oclass));
        return m.slot;
    }

    // Returns the slot of a figure: 99 for the hero (a negative id), else 100 plus the id.
    int figure_slot(int id) override { return id < 0 ? 99 : 100 + id; }

    // Records a stand-in at a position for a figure.
    bool stand_in(const float* position, int figure) override {
        ++stood;
        world_calls.push_back(
            "stand " + std::to_string(static_cast<int>(position[0])) + " " + std::to_string(figure)
        );
        return true;
    }

    // Returns whether the game can hold a hero.
    bool can_hold() override { return holds_ok; }

    // Records the stand-in the mod asked for and keeps it as `stood_as`.
    bool stand_in_as(const mp::StandIn& as, int figure) override {
        ++stood;
        stood_as = as;
        world_calls.push_back("as " + std::to_string(as.state) + " " + std::to_string(figure));
        return true;
    }

    // Copies out the stand-in the game last did.
    bool stand_in_read(mp::StandIn& out) override {
        out = stand_did;
        return true;
    }

    // Records a grab with its animation and flags, and holds the hero.
    bool grab(int animation, std::uint32_t flags) override {
        calls.push_back("grab " + std::to_string(animation) + " " + std::to_string(flags));
        hero_held = true;
        return true;
    }

    // Records a hold at a position and yaw.
    bool hold_at(const float* position, float yaw) override {
        held_at[0] = position[0];
        held_yaw = yaw;
        ++hold_words;
        return true;
    }

    // Records a let-go and releases the hero.
    bool let_go() override {
        calls.push_back("let go");
        hero_held = false;
        return true;
    }

    // Records a hit posted to the game.
    bool hit(const mp::Hit& h) override {
        hits.push_back(h);
        return true;
    }

    // Adds to the bolt count and returns the total.
    int bolts(int add) override { return bolt_count += add; }

    // The eyes last set (x, y, z each).
    std::vector<float> eyes_at;

    // Keeps the eye positions last set.
    void eyes(const std::vector<float>& xyz) override { eyes_at = xyz; }

    // The shared progress: what this game has (read in a level only), and what was given to it,
    // in order ("item 5", "gold 6", "planet 3").
    mp::ProgressBits owned;
    std::vector<std::string> given;

    // Copies the owned progress out and returns true only while a level is playing.
    bool progress(mp::ProgressBits& out) override {
        out = owned;
        return mp::in_level(now.phase);
    }

    // Marks an item owned and records the gift, in the quiet list when gifts are quiet.
    bool give_item(int id) override {
        owned.items[static_cast<std::size_t>(id)] = 1;
        (gifts_quiet ? given_quietly : given).push_back("item " + std::to_string(id));
        return true;
    }

    // Marks a gold bolt owned and records the gift.
    bool give_gold_bolt(int index) override {
        owned.gold[static_cast<std::size_t>(index)] = 1;
        given.push_back("gold " + std::to_string(index));
        return true;
    }

    // Marks a planet unlocked and records the gift, in the quiet list when gifts are quiet.
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

    // Returns an array's bytes as a vector.
    template <std::size_t N>
    static Bytes bytes_of(const std::array<std::uint8_t, N>& a) {
        return Bytes(a.begin(), a.end());
    }

    // Copies a section's bytes out; fails outside a level, without shared progress or for an
    // unknown tag.
    bool shared_read(int tag, Bytes& out) override {
        // The port reads these sections only in a level, and only when it has them.
        if (!has_shared || !mp::in_level(now.phase)) {
            return false;
        }

        // Each tag names one shared section.
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

    // Merges a section into the game's and returns false for an unknown tag. Each byte that is set
    // and not yet owned becomes `value`.
    bool shared_give(int tag, const Bytes& data) override {
        const auto merge = [&](auto& a, std::uint8_t value) {
            // Merging stops at the shorter of the two arrays.
            for (std::size_t i = 0; i < a.size() && i < data.size(); ++i) {
                // A byte the game already has keeps its value.
                if (data[i] != 0 && a[i] == 0) {
                    a[i] = value;
                }
            }
        };

        // Each tag names one shared section, and `value` is what a new byte becomes there.
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

    // Hides or shows a figure (slot 100 and up) or a listed moby; returns false for an unknown
    // slot.
    bool moby_hide(int slot, bool hidden) override {
        // Slots from 100 are figures, whose hidden state is kept by figure id.
        if (slot >= 100 && figures.count(slot - 100) != 0) {
            if (hidden) {
                hidden_figures.insert(slot - 100);
            } else {
                hidden_figures.erase(slot - 100);
            }

            world_calls.push_back((hidden ? "hide " : "show ") + std::to_string(slot));
            return true;
        }

        // Any other slot is a listed moby, hidden by its flag.
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

    // Copies out who deleted the moby in a slot, when the game recorded it.
    bool moby_deletion(int slot, int& by, int& by_class, bool& by_mod) override {
        const auto it = deletions.find(slot);

        // The game recorded no deletion for this slot.
        if (it == deletions.end()) {
            return false;
        }

        by = it->second.by;
        by_class = it->second.by_class;
        by_mod = it->second.by_mod;
        return true;
    }

    // Returns the camera x a test set, with y and z zero.
    bool camera(float* xyz) override {
        xyz[0] = camera_x;
        xyz[1] = xyz[2] = 0.0f;
        return true;
    }

    float camera_x = 0.0f;

    // The level between the camera and a point: every line blocked while `level_wall`, and how
    // many were asked.
    bool level_line(const float*, const float* to, bool& blocked) override {
        ++lines_asked;
        blocked = level_wall && (wall_top_z <= 0.0f || to[2] < wall_top_z);
        return line_known;
    }

    bool level_wall = false;
    float wall_top_z = 0.0f;  // above this a line goes over the wall (0: no top)
    bool line_known = true;
    int lines_asked = 0;

    // The run list (the verbose log's): off until a test sets `run_on`; each slot's run, else
    // `run`'s.
    bool moby_run(int slot, mp::MobyRun& out) override {
        // The list is off until a test turns it on.
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

// Returns a moby with a slot, key, class and flags, placed at x on the x axis.
inline mp::MobyState moby(int slot, int key, int oclass, std::uint32_t flags, float x) {
    mp::MobyState m;
    m.slot = slot;
    m.key = key;
    m.oclass = oclass;
    m.flags = flags;
    m.pose.position[0] = x;
    return m;
}

// One player: a session, a fake game and every part of the mod wired to them as the mod wires them.
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

    // Builds the player, routes each message owner to its part, and says hello as the server would.
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
          migrate(session, game, coop, world, progress, shared, mods, cosave, cam, [this] {
              return clock;
          }) {
        coop.set_save_hooks(
            [this](std::int32_t target) { cosave.announce(target); },
            [this](const Bytes& save) { return cosave.starting(save); }
        );
        coop.on_start_again([this] { cosave.start_again(); });
        router.route(mp::Owner::kSave, [this](std::int32_t sender, const Bytes& bytes) {
            cosave.handle(sender, bytes);
        });
        router.fallback([this](std::int32_t sender, const Bytes& bytes) {
            coop.handle(sender, bytes);
        });
        router.route(mp::Owner::kMods, [this](std::int32_t sender, const Bytes& bytes) {
            mods.handle(sender, bytes);
        });
        router.route(mp::Owner::kWorld, [this](std::int32_t sender, const Bytes& bytes) {
            world.handle(sender, bytes);
        });
        router.route(mp::Owner::kTether, [this](std::int32_t sender, const Bytes& bytes) {
            world.handle(sender, bytes);
        });
        router.route(mp::Owner::kProgress, [this](std::int32_t sender, const Bytes& bytes) {
            progress.handle(sender, bytes);
        });
        router.route(mp::Owner::kMigrate, [this](std::int32_t sender, const Bytes& bytes) {
            migrate.handle(sender, bytes);
        });
        router.route(mp::Owner::kShare, [this](std::int32_t sender, const Bytes& bytes) {
            shared.handle(sender, bytes);
        });
        router.route(mp::Owner::kCamera, [this](std::int32_t sender, const Bytes& bytes) {
            cam.handle(sender, bytes);
        });
        session.on_data([this](std::int32_t sender, const Bytes& bytes) {
            router.dispatch(sender, bytes);
        });
        mp::Message hello;
        hello.kind = mp::Message::Kind::kHello;
        hello.id = id;
        session.connected();
        session.handle(hello);
    }
};

// Returns the server's party info for `members`, hosted by player 1.
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

    // A delivery can make a player send more, so routing goes on until nothing moves.
    while (moved) {
        moved = false;

        // Each player's queued frames are what the server receives from it.
        for (Player* from : players) {
            from->session.flush();
            std::vector<Bytes> frames;
            frames.swap(from->link.frames);

            for (const Bytes& frame : frames) {
                mp::FrameReader r;
                r.feed(frame.data(), frame.size());
                Bytes body;

                // A frame that is not whole is dropped.
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

                    // Only a player in the party receives data.
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

// Advances each player's clock and parts by `ms` and routes what they sent.
inline void tick(std::vector<Player*> players, std::uint64_t ms) {
    // Each player's parts run on that player's own clock.
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
    // Every other member must have said Ready before the host can start.
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

// Returns a gameplay mod with shared, host-only and own settings.
inline mp::ModInfo difficulty_mod() {
    mp::ModInfo m{"difficulty", "Difficulty", "1.2.3", mp::kModGameplay, {}};
    const char* const keys[][2] =
        {{"enemy_count", "Enemy count"}, {"damage_taken", "Damage taken"}, {"bolts", "Bolt drops"}};

    // These three settings differ only in key and label.
    for (const auto& k : keys) {
        m.settings.push_back({k[0], k[1], "1", "1", mp::kSettingShared, true});
    }

    m.settings.push_back({"chaos", "Chaos", "false", "off", mp::kSettingShared, true});
    m.settings.push_back({"seed", "Seed", "", "", mp::kSettingHost, true});
    m.settings.push_back({"trace", "Detailed log", "false", "off", mp::kSettingOwn, true});
    return m;
}

// Returns a list of four mods as a player might have installed them.
inline mp::ModList sample_list() {
    mp::ModList list;
    list.multiplayer = "0.9.0";
    list.openrac = "0.21.0";
    list.disc = "pal";
    list.family = "rac1-60-1";
    list.mods.push_back(difficulty_mod());
    list.mods.push_back({"item-randomizer", "Item Randomizer", "1.1.0", mp::kModGameplay, {}});
    list.mods.push_back(
        {"keyboard-mouse",
         "Keyboard and Mouse",
         "1.2.5",
         mp::kModInput,
         {{"walk_speed", "Walk speed", "1", "1", 0, true}}}
    );
    list.mods.push_back({"xbox-icons", "Xbox Icons", "1.0.2", mp::kModCosmetic, {}});
    return list;
}

// Returns the setting with a key in the first mod, or its first setting when none matches.
inline mp::ModSettingInfo& setting_of(mp::ModList& list, const std::string& key) {
    // Settings are searched by key; an unknown key falls back to the first setting.
    for (mp::ModSettingInfo& s : list.mods[0].settings) {
        if (s.key == key) {
            return s;
        }
    }

    return list.mods[0].settings[0];
}

// Ticks every player's mods at the time the next step reaches, then the party by `ms`.
inline void tick_mods(std::vector<Player*> players, std::uint64_t ms) {
    // Each player's mod list ticks at the time the next step reaches.
    for (Player* p : players) {
        p->mods.tick(p->clock + ms);
    }

    tick(players, ms);
}

// A player with its list, gated as the mod gates it.
inline void gate(Player& p, mp::ModList list) {
    p.coop.set_build("mp " + std::to_string(mp::mods_digest(list)));
    p.mods.set_own(std::move(list));
    p.session.set_ready_gate([&p](std::int32_t id) {
        return p.session.is_host() ? p.mods.may_play(id)
                                   : p.mods.heard_host() && p.mods.differences().empty();
    });
    p.coop.set_gate([&p](std::int32_t id) { return p.mods.may_play(id); });
}

// Returns the READY message a member sends.
inline mp::Message ready_of(std::int32_t id) {
    mp::Message m;
    m.kind = mp::Message::Kind::kData;
    m.id = id;
    m.bytes = {static_cast<std::uint8_t>(mp::LobbyKind::kReady), 1};
    return m;
}

// The slot in a game of the first live moby of a class (-1 none), and how many of it there are.
// Returns the slot of the first live moby of a class in a game, or -1.
inline int slot_of_class(const FakeGame& game, int oclass) {
    // Dead mobys stay listed, so only a live one counts.
    for (const mp::MobyState& m : game.level) {
        if (m.oclass == oclass && (m.flags & mp::kMobyDead) == 0) {
            return m.slot;
        }
    }

    return -1;
}

// Returns how many live mobys of a class a game has.
inline int count_of_class(const FakeGame& game, int oclass) {
    return static_cast<int>(
        std::count_if(game.level.begin(), game.level.end(), [&](const mp::MobyState& m) {
            return m.oclass == oclass && (m.flags & mp::kMobyDead) == 0;
        })
    );
}

// Whether a game was given something (FakeGame's `given`), and whether it was asked for a scene.
inline bool was_given(const FakeGame& game, const std::string& what) {
    return std::find(game.given.begin(), game.given.end(), what) != game.given.end();
}

inline bool asked_scene(const FakeGame& game) {
    return std::any_of(game.calls.begin(), game.calls.end(), [](const std::string& c) {
        return c.rfind("scene", 0) == 0 || c == "skip";
    });
}

// A party in level 0, everyone in control, the logs kept.
struct SceneParty {
    Player host{1}, member{2};
    std::vector<std::string> host_log, member_log;

    // Joins a host and a member, starts a new game, puts both in level 0 and clears their calls.
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

    // Ticks both players for `ms` in 50 ms steps.
    void run(std::uint64_t ms) {
        // Steps of 50 ms let timers and messages interleave as they do in play.
        for (std::uint64_t t = 0; t < ms; t += 50) {
            tick({&host, &member}, 50);
        }
    }

    // Returns how many times a game was asked for a call.
    static long count(const FakeGame& g, const std::string& call) {
        return static_cast<long>(std::count(g.calls.begin(), g.calls.end(), call));
    }

    // Returns whether a log has a line starting with `start`, from index `from` on.
    static bool logged(
        const std::vector<std::string>& log, const std::string& start, std::size_t from = 0
    ) {
        return std::any_of(
            log.begin() + static_cast<std::ptrdiff_t>(std::min(from, log.size())),
            log.end(),
            [&](const std::string& l) { return l.rfind(start, 0) == 0; }
        );
    }
};

// Returns the bytes of a skip message from `origin` for a play, a kind and a scene.
inline mp::Bytes skip_bytes(
    std::int32_t origin, std::int32_t play, std::uint8_t what, std::uint16_t scene
) {
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

    // Joins three players, starts a new game and puts them in level 4.
    Party() {
        host.session.handle(info(all));
        a.session.handle(info(all));
        b.session.handle(info(all));
        host.coop.choose({}, "New game");
        CHECK(start_ready(host));
        route(players());
        tick(players(), 16);

        // Every player starts in the host's level 4.
        for (Player* p : players()) {
            p->game.now = {mp::kPhasePlaying, 4, 2};
            p->game.calls.clear();
        }

        tick(players(), 16);
    }

    // Returns the three players.
    std::vector<Player*> players() { return {&host, &a, &b}; }

    // The server's word that `id` left, to everyone still in the party.
    void left(std::int32_t id) {
        mp::Message m;
        m.kind = mp::Message::Kind::kPartyLeft;
        m.id = id;

        for (Player* p : players()) {
            // The leaver hears nothing about its own leaving.
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

// Gives every player the server's resume offer.
inline void offer_resume(const std::vector<Player*>& players) {
    // Each player gets its own resume token.
    for (Player* p : players) {
        p->session.handle(udp_offer_of(1000 + p->id));
    }
}

// Sends every player in `others` the server's word that `id` is away, or back when `back` is set.
inline void server_away(const std::vector<Player*>& others, std::int32_t id, bool back = false) {
    mp::Message m;
    m.kind = mp::Message::Kind::kPartyAway;
    m.id = id;
    m.back = back;

    // Every other player hears from the server that `id` is away.
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
inline void server_host(
    const std::vector<Player*>& players, std::int32_t host, std::int32_t epoch
) {
    mp::Message m;
    m.kind = mp::Message::Kind::kPartyHostChanged;
    m.host = host;
    m.epoch = epoch;

    // Every player hears the host change at once.
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

// Returns how many times a game was asked for a call.
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
// and a strike, 20 strikes (forgiven 1 a second) closing the connection; a member's traffic
// reaching the host only. The link on either side of it loses, delays, reorders and repeats
// datagrams, and delays TCP in order. All of it from one seed, on one clock.
struct SimLink {
    double loss = 0;       // of each datagram, each way
    double latency = 0;    // ms, each way
    double jitter = 0;     // ms more, at most
    double reorder = 0;    // of each datagram: late by a good deal more
    double duplicate = 0;  // of each datagram: arrives twice
    bool udp = true;       // false: UDP is blocked, state goes by TCP
};

// The server and the network between players: it takes what players send, applies the server's
// limits and the link's faults, and delivers it later.
struct SimNet {
    // A message in flight: when it arrives, to whom, from whom, and its bytes.
    struct Packet {
        double at = 0;
        Player* to = nullptr;
        std::int32_t from = 0;
        Bytes bytes;
    };

    // A player's rate limits at the server, and when its last TCP message arrives.
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
    // The shared progress on this link too (off: the seeded runs keep the draws they were tuned
    // on).
    bool shared = false;
    long data_drops = 0, state_drops = 0, kicked = 0, datagrams_lost = 0, state_sent = 0;

    // What the host and the members sent, by kind (a message's first byte), and what each sent
    // up to the server by way of TCP (DATA; a BATCH is one).
    // A count of messages and bytes.
    struct Traffic {
        long messages = 0, bytes = 0;
    };

    std::map<int, Traffic> host_state, member_state, host_data, member_data;
    // The host's state as the play stopped: what is left after it is what settling took.
    std::map<int, Traffic> played_state;
    // The longest gap, in ms, between two WORLDs reaching a member with a word of one moby, of the
    // ids `gaps_of` says to watch, while `gaps_on`.
    std::function<bool(int)> gaps_of;
    bool gaps_on = false;
    // The players whose connection is gone: nothing they say goes, nothing reaches them. Whether
    // the rest of a game ticks as the mod ticks it; who the host last named to follow it
    // (HOST_CHANGED).
    std::set<std::int32_t> cut;
    bool whole = false;
    std::vector<std::int32_t> named;
    std::set<std::int32_t> declined;
    std::map<std::pair<std::int32_t, int>, double> heard_at;
    double worst_gap = 0;

    // Starts a net over `who` with a link and a seed and sets each player's transport to match the
    // link's UDP.
    SimNet(std::vector<Player*> who, SimLink l, unsigned seed)
        : link(l),
          random(seed),
          players(std::move(who)) {
        // Each player's transport follows the link, so state goes by datagram or by TCP.
        for (Player* p : players) {
            p->link.udp = link.udp;
        }
    }

    // Returns a random number from 0 up to 1 from the seed.
    double uniform() { return std::uniform_real_distribution<double>(0.0, 1.0)(random); }

    // Returns the delay of one leg in ms: the latency plus up to the jitter.
    double delay() { return link.latency + uniform() * link.jitter; }

    // Returns whether a message gets to `to`. Neither side may be cut; a host's goes to everyone or
    // the target, a member's to the host.
    bool reaches(Player* from, std::int32_t target, Player* to) const {
        return to != from && cut.count(from->id) == 0 && cut.count(to->id) == 0
               && (from->session.is_host() ? target == 0 || target == to->id
                                           : to->session.is_host());
    }

    // Queues a TCP message to every player it reaches, in order after the last one.
    void tcp(Player* from, std::int32_t target, const Bytes& bytes) {
        // Every player the message reaches gets its own copy.
        for (Player* to : players) {
            if (reaches(from, target, to)) {
                Limits& l = limits[to->id];
                l.tcp_last = std::max(l.tcp_last, static_cast<double>(now) + 2 * delay());
                flying.push_back({l.tcp_last, to, from->id, bytes});
            }
        }
    }

    // Queues a datagram to one player, unless the link loses it; it may arrive late or twice.
    void one_datagram(Player* from, Player* to, const Bytes& bytes) {
        // A datagram crosses two legs, to the server and on to the player, and either can lose it.
        for (int leg = 0; leg < 2; ++leg) {
            if (uniform() < link.loss) {
                ++datagrams_lost;
                return;
            }
        }

        double at = static_cast<double>(now) + 2 * delay();
        at += uniform() < link.reorder ? 60.0 + 3 * link.jitter : 0.0;
        flying.push_back({at, to, from->id, bytes});

        // A duplicate arrives a little after the original.
        if (uniform() < link.duplicate) {
            flying.push_back({at + 5.0, to, from->id, bytes});
        }
    }

    // Records, for a delivered WORLD message, the longest gap between words of a watched moby.
    void note_gaps(const Packet& f) {
        // Only WORLD messages of a watched play are measured.
        if (!gaps_on || !gaps_of || f.bytes.empty()
            || f.bytes[0] != static_cast<std::uint8_t>(mp::WorldKind::kWorld)) {
            return;
        }

        int level = -1;
        std::int64_t stamp = 0;
        std::vector<mp::WorldEntry> entries;

        // A message that does not decode is left unmeasured.
        if (!mp::decode_world(f.bytes, level, stamp, entries)) {
            return;
        }

        for (const mp::WorldEntry& e : entries) {
            // Only the mobys the test watches count.
            if (!gaps_of(e.id)) {
                continue;
            }

            const auto seen = heard_at.find({f.to->id, e.id});

            // The gap is the time since the last WORLD that named this moby for this player.
            if (seen != heard_at.end()) {
                worst_gap = std::max(worst_gap, f.at - seen->second);
                seen->second = f.at;
            } else {
                heard_at[{f.to->id, e.id}] = f.at;
            }
        }
    }

    // Advances the net by `ms`. Takes what players sent through the server's limits, delivers what
    // is due, then ticks every player.
    void step(std::uint64_t ms) {
        now += ms;

        // Each player's outgoing traffic passes the server's limits before it is delivered.
        for (Player* p : players) {
            p->clock = now;
            Limits& l = limits[p->id];
            std::vector<Bytes> frames;
            frames.swap(p->link.frames);

            for (const Bytes& frame : frames) {
                mp::FrameReader r;
                r.feed(frame.data(), frame.size());
                Bytes body;

                // A frame that is not whole is dropped.
                if (!r.next(body)) {
                    continue;
                }

                // HOST_CHANGED: the host's list, or a new host saying it cannot.
                if (body.size() >= 4 && body[0] == static_cast<std::uint8_t>(mp::PacketId::kParty)
                    && body[1] == static_cast<std::uint8_t>(mp::PartyOp::kHostChanged)
                    && cut.count(p->id) == 0) {
                    // A non-zero byte means the new host says it cannot host.
                    if (body[2] != 0) {
                        declined.insert(p->id);
                    } else {
                        mp::Reader ids(body.data() + 4, body.size() - 4);
                        named.clear();

                        // The ids follow a count: the players the host named to follow it.
                        for (int i = 0; i < body[3]; ++i) {
                            named.push_back(ids.i32());
                        }
                    }

                    continue;
                }

                const mp::Request q = mp::decode_request(body);

                // Only DATA is routed here.
                if (q.kind != mp::Request::Kind::kData) {
                    continue;
                }

                // Over the DATA rate the server drops the message and counts a strike.
                if (!l.data.take(now)) {
                    ++data_drops;
                    kicked += l.strikes.take(now) ? 0 : 1;
                    continue;
                }

                Traffic& t =
                    (p->id == 1 ? host_data : member_data)[q.bytes.empty() ? 0 : q.bytes[0]];
                ++t.messages;
                t.bytes += static_cast<long>(q.bytes.size());
                tcp(p, q.target, q.bytes);
            }

            std::vector<std::pair<std::int32_t, Bytes>> datagrams;
            datagrams.swap(p->link.datagrams);

            // State datagrams are counted in the traffic whether or not the server then drops them.
            for (const auto& [target, bytes] : datagrams) {
                ++state_sent;
                Traffic& t = (p->id == 1 ? host_state : member_state)[bytes.empty() ? 0 : bytes[0]];
                ++t.messages;
                t.bytes += static_cast<long>(bytes.size());

                // Over the state rate the server drops the datagram without a word.
                if (!l.state.take(now)) {
                    ++state_drops;
                    continue;
                }

                // A datagram goes through the link once for each player it reaches.
                for (Player* to : players) {
                    if (reaches(p, target, to)) {
                        one_datagram(p, to, bytes);
                    }
                }
            }
        }

        std::vector<Packet> later;
        // Stable: a stream's messages due at the same moment arrive in the order they went (TCP's).
        std::stable_sort(flying.begin(), flying.end(), [](const Packet& a, const Packet& b) {
            return a.at < b.at;
        });

        // Packets that are due are delivered and the rest stay in flight.
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

        // Every player's parts tick after the deliveries.
        for (Player* p : players) {
            p->migrate.tick(now);
            p->coop.tick(now);
            p->world.tick(now);
            p->progress.tick(now);

            // Shared progress runs only for tests that turn it on.
            if (shared) {
                p->shared.tick(now);
            }

            // The rest of the game's parts run only for tests that need the whole mod.
            if (whole) {
                p->mods.tick(now);
                p->cosave.tick(now, p->coop.playing());
                p->cam.tick(now);
            }

            p->session.flush();
        }
    }
};

// The games of a party in level 3, its mobys placed alike in each (each game's slots its own: one
// spot on per player, so a key is the only name they share): `enemies` driven ones spread over the
// level, `crates` crates among them.
struct SimWorld {
    static constexpr std::uint32_t kRuns = mp::kMobyUpdates | mp::kMobyPlaced;
    int enemies = 0;
    int crates = 0;
    float spread = 8.0f;

    // Returns the world's mobys with slots counted from `shift`, leaving out the keys in `skipped`.
    std::vector<mp::MobyState> made(int shift, const std::set<int>& skipped = {}) const {
        std::vector<mp::MobyState> out;
        int slot = shift;

        for (int key = 0; key < enemies + crates; ++key) {
            // A skipped key stands for a record the game left out.
            if (skipped.count(key) != 0) {
                continue;
            }

            const bool crate = key >= enemies;
            mp::MobyState m =
                moby(slot++, key, crate ? 500 : 300, kRuns | (crate ? 0 : mp::kMobyCreature), 0.0f);
            m.pose.position[0] = static_cast<float>(key % 40) * spread;
            m.pose.position[1] = static_cast<float>(key / 40) * spread;
            out.push_back(m);
        }

        return out;
    }
};

// Gives every player the party info and readies the members, then routes.
inline void join_party(std::vector<Player*> all, const std::vector<mp::Member>& members) {
    for (Player* p : all) {
        p->session.handle(info(members));
    }

    // Members ready themselves; the host does not.
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

// The game rebuilding its level as a death does: every moby new, the records `skipped` left out (so
// the slots after them move), the spawned gone, nothing driven, the hits posted to the old ones
// gone with them (the port writes a hit into its moby: a test's list of them is that, by slot); the
// same level.
inline void remake(Player& p, const SimWorld& w, const std::set<int>& skipped = {}, int shift = 0) {
    p.game.level = w.made(shift, skipped);
    p.game.drives.clear();
    p.game.hits.clear();
    ++p.game.now.loads;
}

// The placed mobys' slots in a run, clear of the fake's figures and spawns.
constexpr int kSimSlots = 1000;

struct SimResult {
    // HITs and DAMAGEs refused in every game: all of them the game's own, so none should be.
    int refused = 0;
    int wrong_place = 0, wrong_hidden = 0, wrong_crates = 0;
    long bolts_made = 0, bolts_credited = 0;
    // From the end of the play: when everything stood as on the host, for good (-1: not by the end
    // of the settling), and the largest number of mobys wrong on the way.
    long settled_ms = -1;
    int most_wrong = 0;
    // The longest a moving moby went unheard by a member while the party played, ms.
    double worst_gap_ms = 0;
};

// What a run plays. The default is the run the sim tests have always had: all 120 enemies walking,
// 30 crates, 10 seconds for the link to settle.
struct SimPlan {
    int enemies = 120;
    int crates = 30;
    int walk_every = 1;  // every n-th enemy walks: a large share of a level only stands there
    bool fire = false;   // in the second half of the play the host and a member fire
    int settle_ms = 10000;
    float spread = 8.0f;  // between one moby and the next
};

// How many of the host's mobys differ from each member's, as `r` counts them.
inline void compare_sim(const std::vector<Player*>& all, SimResult& r) {
    r.wrong_place = r.wrong_hidden = r.wrong_crates = 0;
    const FakeGame& host = all[0]->game;

    // Each member's game is compared with the host's.
    for (std::size_t i = 1; i < all.size(); ++i) {
        std::map<int, const mp::MobyState*> theirs;

        for (const mp::MobyState& c : all[i]->game.level) {
            // Spawned mobys have no key, so only placed ones match across games.
            if (c.key >= 0) {
                theirs.emplace(c.key, &c);
            }
        }

        for (const mp::MobyState& h : host.level) {
            // Only placed mobys can be matched across games.
            if (h.key < 0) {
                continue;
            }

            const auto it = theirs.find(h.key);
            const mp::MobyState* c = it != theirs.end() ? it->second : nullptr;
            const bool here_dead = (h.flags & mp::kMobyDead) != 0;
            const bool there_dead = c == nullptr || (c->flags & mp::kMobyDead) != 0;

            // Class 500 is the crate: it is wrong when only one side broke it; other mobys are
            // compared by place and hidden flag.
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

// One run: a party of 1 + `members` plays `seconds`: the host's enemies move and hide, crates go
// from every side, each break drops a bolt from the host that a member collects, and a member dies
// half way; then everything stops and the link gets plan.settle_ms to settle. What differs then is
// a desync no time will mend.
inline SimResult run_sim(
    const SimLink& link,
    unsigned seed,
    int members,
    double seconds,
    SimNet** out = nullptr,
    const SimPlan& plan = SimPlan{}
) {
    static std::vector<std::unique_ptr<Player>> keep;
    keep.clear();
    keep.push_back(std::make_unique<Player>(1));
    std::vector<Player*> all = {keep[0].get()};
    std::vector<mp::Member> list = {{1, "P1"}};

    // Each extra member joins as player 2 and up.
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
    const auto walks = [&](int key) {
        return key >= 0 && key < w.enemies && key % plan.walk_every == 0;
    };

    // Every game starts in level 3 with its mobys placed alike and the heroes spread along x.
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

    // Each step is 16 ms: the play first, then the time for the link to settle.
    for (int t = 0; t < ticks + plan.settle_ms / 16; ++t) {
        const bool playing = t < ticks;
        // Once everyone is under way, up to the stop and not across the member's own death.
        net->gaps_on = playing && t * 16 >= 2000 && (t < ticks / 2 || t * 16 >= ticks * 8 + 2000);

        // Outside the measured window the gaps heard so far are forgotten.
        if (!net->gaps_on) {
            net->heard_at.clear();
        }

        // The host's enemies walk; some hide and show.
        if (playing) {
            for (mp::MobyState& m : host.game.level) {
                // Only the host's enemies move; crates and spawned mobys stay put.
                if (m.key >= 0 && m.key < w.enemies) {
                    if (walks(m.key)) {
                        m.pose.position[1] += 0.05f * (m.key % 2 == 0 ? 1.0f : -1.0f);
                    }

                    // Now and then an enemy hides or shows again.
                    if (rng() % 2000 == 0) {
                        m.flags ^= mp::kMobyHidden;
                    }
                }
            }

            // Now and then a crate goes, here or there, unless it has. Not on the first tick: a
            // game has a tick in the level before anyone can break anything in it.
            if (rng() % 40 == 0) {
                const int key =
                    w.enemies + static_cast<int>(rng() % static_cast<unsigned>(w.crates));
                Player& who = *all[rng() % all.size()];
                const int slot = who.game.moby_slot(key);

                if (t > 0 && slot >= 0 && broken.count(key) == 0) {
                    broken.insert(key);
                    who.game.die(slot);
                }
            }

            // The first member dies half way.
            if (!died && t == ticks / 2 && members > 0) {
                died = true;
                remake(
                    *all[1], w, {0, 1, 2}, kSimSlots
                );  // records skipped: slots mean other mobys now
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
                const auto it = std::find_if(
                    p->game.level.begin(), p->game.level.end(), [&](const mp::MobyState& m) {
                        return m.slot == h.victim;
                    }
                );

                if (it != p->game.level.end() && it->oclass == 500 && it->key >= 0) {
                    const int key = it->key;
                    p->game.die(h.victim);

                    // The host's copy drops a bolt, landed by its next update.
                    if (p == &host) {
                        mp::MobyState bolt =
                            moby(4000 + next_bolt++, -1, 13, mp::kMobyUpdates, 0.0f);
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
                // Only bolts the member's game spawned itself count, not the ones driven from the
                // host's copy.
                if (b.key < 0 && mp::bolt_class(b.oclass) && m.game.drives.count(b.slot) == 0
                    && b.slot >= 500) {
                    mine.push_back(b.slot);
                }
            }

            // Each of the member's own bolts is collected once.
            for (const int slot : mine) {
                // Credit counts only bolts the world layer accepts as the member's own.
                if (m.world.mobys() != 0 && m.world.collect(slot)) {
                    ++r.bolts_credited;  // its own: never one of the host's
                }

                m.game.die(slot);
            }
        }

        net->step(16);

        // The host's state traffic is kept as it stood when the play stopped.
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
    r.settled_ms =
        r.wrong_place + r.wrong_hidden + r.wrong_crates != 0 ? -1 : (last_wrong + 1 - ticks) * 16L;
    r.worst_gap_ms = net->worst_gap;

    // Refused hits add up over all players.
    for (Player* p : all) {
        r.refused += p->world.hits_refused();
    }

    // The net is handed back only to a caller that asks for it.
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
void run_rac2_tests();

}  // namespace mpt
