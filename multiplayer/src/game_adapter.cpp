/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The multiplayer mod's game (game_adapter.hpp).
 */
#include "game_adapter.hpp"
#include "mod_state.hpp"
#include "save_check.hpp"

#include <algorithm>
#include <cstdint>

namespace mpmod {

/* Where each disc keeps the progress a party shares (progress.hpp; rac1-decomp's names, the US
 * addresses from OpenRAC's versionmap): item_available (40 bytes, a byte an item), the gold bolts
 * (gold_bolts_collected, 4 a level), level_available (the planets, 20), and the game's own GiveItem
 * (grant_inventory_item) and UnlockPlanet (unlock_planet). */
struct ProgressDisc {
    const char* version;
    openrac::gaddr items;
    openrac::gaddr gold;
    openrac::gaddr planets;
    const char* give_item;
    const char* unlock_planet;
};

constexpr ProgressDisc kProgressDiscs[] = {
    {"pal", 0x0013D5C8u, 0x0014BFC0u, 0x0013DE48u, "func_L00_002618D8", "func_L00_00261848"},
    {"ntsc", 0x0013D4C0u, 0x0014BEC0u, 0x0013DD40u, "FUN_L00_00260860", "FUN_L00_002607d0"},
};

mp::GameStatus ModGame::status() {
    const openrac_play_status s = mod_.play_status();
    return {s.phase, s.level, s.loads, mod_.scene_playing()};
}

bool ModGame::save_usable(const mp::Bytes& save) {
    const mp::SaveFault fault = mp::check_save(save);

    if (fault != mp::SaveFault::kNone) {
        mod_.warn(std::string("multiplayer: a save was refused: ") + mp::save_fault_name(fault));
    }

    return fault == mp::SaveFault::kNone;
}

std::uint32_t ModGame::sim_digest(int level) { return mod_.sim_digest(level); }

bool ModGame::start(int level, const mp::Bytes& save) {
    return mod_.play_start(level, save) == OPENRAC_HOOK_OK;
}

bool ModGame::travel(int level) { return mod_.play_travel(level) == OPENRAC_HOOK_OK; }

mp::Bytes ModGame::capture() { return mod_.save_capture(); }

bool ModGame::hero(mp::Pose& out) {
    openrac_pose p{};

    if (!mod_.hero_pose(p)) {
        return false;
    }

    out = from_sdk(p);
    openrac_held held{};

    // A port without the pieces: the pose wears nothing.
    if (mod_.hero_held(held)) {
        held_from_sdk(held, out);
    }

    return true;
}

int ModGame::figure_create() { return mod_.figure_create(); }

bool ModGame::figure_pose(int id, const mp::Pose& pose) {
    if (!mod_.figure_pose(id, to_sdk(pose))) {
        return false;
    }

    if (mod_.has_held()) {
        mod_.figure_held(id, held_to_sdk(pose));
    }

    return true;
}

std::vector<mp::Shot> ModGame::hero_shots() {
    std::vector<mp::Shot> out;

    if (mod_.has_shots2()) {
        for (const openrac_shot2& s : mod_.hero_shots2()) {
            mp::Shot shot;
            shot.item = s.item;
            std::copy(s.position, s.position + 3, shot.position);
            std::copy(s.velocity, s.velocity + 3, shot.velocity);
            std::copy(s.aim, s.aim + 4, shot.aim);
            shot.flags = s.flags;
            shot.target_key = s.target_key;
            std::copy(s.velocity2, s.velocity2 + 3, shot.velocity2);
            std::copy(s.aux, s.aux + 4, shot.aux);
            out.push_back(shot);
        }

        return out;
    }

    for (const openrac_shot& s : mod_.hero_shots()) {
        mp::Shot shot;
        shot.item = s.item;
        std::copy(s.position, s.position + 3, shot.position);
        std::copy(s.velocity, s.velocity + 3, shot.velocity);
        std::copy(s.aim, s.aim + 4, shot.aim);
        out.push_back(shot);
    }

    return out;
}

bool ModGame::figure_shot(int id, const mp::Shot& shot) {
    if (mod_.has_shots2()) {
        openrac_shot2 s{};
        s.size = sizeof s;
        s.item = shot.item;
        std::copy(shot.position, shot.position + 3, s.position);
        std::copy(shot.velocity, shot.velocity + 3, s.velocity);
        std::copy(shot.aim, shot.aim + 4, s.aim);
        s.flags = shot.flags;
        s.target_key = shot.target_key;
        std::copy(shot.velocity2, shot.velocity2 + 3, s.velocity2);
        std::copy(shot.aux, shot.aux + 4, s.aux);
        return mod_.figure_shot2(id, s);
    }

    openrac_shot s{};
    s.size = sizeof s;
    s.item = shot.item;
    std::copy(shot.position, shot.position + 3, s.position);
    std::copy(shot.velocity, shot.velocity + 3, s.velocity);
    std::copy(shot.aim, shot.aim + 4, s.aim);
    return mod_.figure_shot(id, s);
}

void ModGame::figure_remove(int id) { mod_.figure_remove(id); }

std::vector<mp::MobyState> ModGame::mobys() {
    std::vector<mp::MobyState> out;

    for (const openrac_moby& m : mod_.moby_list()) {
        mp::MobyState s;
        s.slot = m.slot;
        s.key = m.key;
        s.oclass = m.oclass;
        s.flags = m.flags;
        s.state = m.state;
        std::copy(m.position, m.position + 3, s.pose.position);
        std::copy(m.rotation, m.rotation + 3, s.pose.rotation);
        s.pose.scale = m.scale;
        s.pose.seq = m.seq;
        s.pose.frame = m.frame;
        s.pose.next_frame = m.next_frame;
        s.pose.anim_time = m.anim_time;
        s.pose.anim_speed = m.anim_speed;
        out.push_back(s);
    }

    return out;
}

int ModGame::moby_slot(int key) { return mod_.moby_slot(key); }

bool ModGame::moby_drive(int slot, bool on) { return mod_.moby_drive(slot, on); }

bool ModGame::moby_pose(int slot, const mp::Pose& pose) { return mod_.moby_pose(slot, moby_of(pose)); }

bool ModGame::moby_kill(int slot) { return mod_.moby_kill(slot); }

bool ModGame::moby_hide(int slot, bool hidden) { return mod_.moby_hide(slot, hidden); }

bool ModGame::moby_deletion(int slot, int& by, int& by_class, bool& by_mod) {
    openrac_moby_deletion d{};

    if (!mod_.moby_deletion(slot, d)) {
        return false;
    }

    by = d.by;
    by_class = d.by_class;
    by_mod = d.by_mod != 0;
    return true;
}

bool ModGame::camera(float* xyz) {
    float at[3] = {0, 0, 0};

    if (!mod_.camera_position(at)) {
        return false;
    }

    std::copy(at, at + 3, xyz);
    return true;
}

bool ModGame::moby_run(int slot, mp::MobyRun& out) {
    openrac_moby_run r{};

    if (!mod_.moby_run(slot, r)) {
        return false;
    }

    out.runs = r.runs;
    std::copy(r.camera, r.camera + 3, out.camera);
    out.menu_ticks = r.menu_ticks;
    out.menu = r.menu;
    std::copy(r.level_camera, r.level_camera + 3, out.level_camera);
    out.level_mode = r.level_mode;
    out.context = r.context;
    out.update_dist = r.update_dist;
    out.draw_dist = r.draw_dist;
    out.drawn = r.drawn;
    out.ran = r.ran;
    out.distance = r.distance;
    return true;
}

int ModGame::moby_spawn(int oclass, const mp::Pose& pose) {
    return mod_.moby_spawn(oclass, moby_of(pose));
}

int ModGame::figure_slot(int id) { return mod_.figure_slot(id); }

bool ModGame::stand_in(const float* position, int figure) {
    return mod_.hero_stand_in(position, figure);
}

bool ModGame::hit(const mp::Hit& h) {
    openrac_hit out{};
    out.victim = h.victim;
    out.attacker = h.attacker;
    out.mask = h.mask;
    out.damage = h.damage;
    std::copy(h.position, h.position + 3, out.position);
    std::copy(h.velocity, h.velocity + 3, out.velocity);
    return mod_.moby_hit(out);
}

void ModGame::eyes(const std::vector<float>& xyz) { mod_.play_eyes(xyz); }

int ModGame::bolts(int add) { return mod_.bolts(add); }

bool ModGame::progress(mp::ProgressBits& out) {
    const ProgressDisc* disc = progress_disc();

    if (disc == nullptr || !mp::in_level(mod_.play_status().phase)) {
        return false;
    }

    const openrac::Memory memory = mod_.memory();
    read_bytes(memory, mod_.relocate(disc->items), out.items.data(), out.items.size());
    read_bytes(memory, mod_.relocate(disc->gold), out.gold.data(), out.gold.size());
    read_bytes(memory, mod_.relocate(disc->planets), out.planets.data(), out.planets.size());
    return true;
}

bool ModGame::give_item(int id) {
    const ProgressDisc* disc = progress_disc();
    const auto give = disc != nullptr ? mod_.function<void (*)(std::int32_t, std::int32_t)>(disc->give_item) : nullptr;

    if (give == nullptr) {
        return false;
    }

    // Another player's item, as it is: no mod makes it something else here.
    mod_.party_replay(true);
    give(id, 0);
    mod_.party_replay(false);
    return true;
}

bool ModGame::give_gold_bolt(int index) {
    const ProgressDisc* disc = progress_disc();

    if (disc == nullptr || index < 0 || index >= static_cast<int>(mp::kProgressGoldBolts)) {
        return false;
    }

    mod_.memory().write<std::uint8_t>(mod_.relocate(disc->gold) + static_cast<openrac::gaddr>(index), 1);
    return true;
}

bool ModGame::unlock_planet(int planet) {
    const ProgressDisc* disc = progress_disc();
    const auto unlock = disc != nullptr ? mod_.function<void (*)(std::int32_t)>(disc->unlock_planet) : nullptr;

    if (unlock == nullptr) {
        return false;
    }

    mod_.party_replay(true);
    unlock(planet);
    mod_.party_replay(false);
    return true;
}

mp::ModList ModGame::mod_list() {
    mp::ModList list;
    list.multiplayer = kModVersion;
    list.openrac = std::string(mod_.openrac_version());
    list.disc = std::string(mod_.version());
    // A port that does not say: the disc itself, so that only the same disc plays together.
    list.family = mod_.world_family();

    if (list.family.empty()) {
        list.family = list.disc;
    }


    for (const openrac::ModEntry& e : mod_.mods()) {
        if (e.id == "multiplayer") {
            continue;
        }

        mp::ModInfo m{e.id, e.name, e.version, e.party, {}};

        // A player's own mod is compared by nothing: its name is enough.
        if (m.counts()) {
            for (const openrac::ModSetting& s : mod_.mod_settings(e.index)) {
                m.settings.push_back({s.key, s.name, s.value, s.shown, s.party, s.is_default});
            }
        }

        indices_[e.id] = e.index;
        list.mods.push_back(std::move(m));
    }

    return list;
}

std::vector<mp::PartyRules> ModGame::party_rules() {
    std::vector<mp::PartyRules> out;

    for (const auto& [id, index] : indices_) {
        mp::Bytes data;
        std::uint32_t version = 0;

        if (mod_.party_rules_read(index, data, version)) {
            out.push_back({id, std::move(data), mod_.party_rules_text(index)});
        }
    }

    return out;
}

bool ModGame::deliver_rules(const std::string& id, const mp::Bytes& data) {
    const auto it = indices_.find(id);
    return it != indices_.end() && mod_.party_rules_deliver(it->second, data);
}

void ModGame::overlay_pause(bool on) { mod_.play_overlay_pause(on); }

void ModGame::skip() { mod_.play_skip(); }

bool ModGame::play_scene(int scene) { return mod_.play_scene(scene); }

openrac_moby ModGame::moby_of(const mp::Pose& p) {
    openrac_moby out{};
    out.size = sizeof out;
    std::copy(p.position, p.position + 3, out.position);
    std::copy(p.rotation, p.rotation + 3, out.rotation);
    out.scale = p.scale;
    out.seq = p.seq;
    out.frame = p.frame;
    out.next_frame = p.next_frame;
    out.anim_time = p.anim_time;
    out.anim_speed = p.anim_speed;
    return out;
}

mp::Pose ModGame::from_sdk(const openrac_pose& p) {
    mp::Pose out;
    out.level = p.level;
    std::copy(p.position, p.position + 3, out.position);
    std::copy(p.rotation, p.rotation + 3, out.rotation);
    out.scale = p.scale;
    std::copy(p.velocity, p.velocity + 3, out.velocity);
    out.state = p.state;
    out.health = p.health;
    out.item = p.item;
    out.seq = p.seq;
    out.frame = p.frame;
    out.next_frame = p.next_frame;
    out.anim_time = p.anim_time;
    out.anim_speed = p.anim_speed;
    out.flags = p.flags;
    return out;
}

void ModGame::held_from_sdk(const openrac_held& h, mp::Pose& out) {
    const auto count = static_cast<std::size_t>(
        std::clamp<std::int32_t>(h.count, 0, static_cast<std::int32_t>(mp::kHeldMax))
    );
    out.held_count = static_cast<std::uint32_t>(count);

    for (std::size_t i = 0; i < count; ++i) {
        const openrac_held_piece& s = h.pieces[i];
        mp::HeldPiece& d = out.held[i];
        d.slot = s.slot;
        d.part = s.part;
        d.item = s.item;
        d.oclass = s.oclass;
        d.joint = s.joint;
        d.flags = s.flags;
        std::copy(s.position, s.position + 3, d.position);
        std::copy(s.rotation, s.rotation + 3, d.rotation);
        d.seq = s.seq;
        d.frame = s.frame;
        d.next_frame = s.next_frame;
        d.anim_time = s.anim_time;
        d.anim_speed = s.anim_speed;
    }
}

openrac_held ModGame::held_to_sdk(const mp::Pose& p) {
    openrac_held h{};
    h.size = sizeof h;
    const std::size_t count = std::min<std::size_t>(p.held_count, OPENRAC_HELD_MAX);
    h.count = static_cast<std::int32_t>(count);

    for (std::size_t i = 0; i < count; ++i) {
        const mp::HeldPiece& s = p.held[i];
        openrac_held_piece& d = h.pieces[i];
        d.slot = s.slot;
        d.part = s.part;
        d.item = s.item;
        d.oclass = s.oclass;
        d.joint = s.joint;
        d.flags = s.flags;
        std::copy(s.position, s.position + 3, d.position);
        std::copy(s.rotation, s.rotation + 3, d.rotation);
        d.seq = s.seq;
        d.frame = s.frame;
        d.next_frame = s.next_frame;
        d.anim_time = s.anim_time;
        d.anim_speed = s.anim_speed;
    }

    return h;
}

openrac_pose ModGame::to_sdk(const mp::Pose& p) {
    openrac_pose out{};
    out.size = sizeof out;
    out.level = p.level;
    std::copy(p.position, p.position + 3, out.position);
    std::copy(p.rotation, p.rotation + 3, out.rotation);
    out.scale = p.scale;
    std::copy(p.velocity, p.velocity + 3, out.velocity);
    out.state = p.state;
    out.health = p.health;
    out.item = p.item;
    out.seq = p.seq;
    out.frame = p.frame;
    out.next_frame = p.next_frame;
    out.anim_time = p.anim_time;
    out.anim_speed = p.anim_speed;
    out.flags = p.flags;
    return out;
}

bool ModGame::open_home() { return home != nullptr && home->open(); }

const ProgressDisc* ModGame::progress_disc() const {
    for (const ProgressDisc& d : kProgressDiscs) {
        if (mod_.version() == d.version) {
            return &d;
        }
    }

    return nullptr;
}

void ModGame::read_bytes(const openrac::Memory& memory, openrac::gaddr at, std::uint8_t* out, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = memory.read<std::uint8_t>(at + static_cast<openrac::gaddr>(i));
    }
}

}  // namespace mpmod
