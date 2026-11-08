// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AsobiCodes
//
// Native OpenRAC core port of RAC-RS Arsenal Mastery.
// This milestone ports the progression loop that can be expressed with stable OpenRAC hooks:
// attacker attribution -> weapon XP -> V1-V5 power -> planet rebalance.
//
// RAC-RS's omni save/UI/struct helpers are deliberately not emulated here. README.md documents
// the remaining parity work.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

#include "openrac/mod.hpp"

namespace {

using openrac::gaddr;

struct Disc {
    const char* version;
    uint32_t hero;
    uint32_t item_in_hand;
    uint32_t hit_ring;
};

constexpr Disc kDiscs[] = {
    {"pal", 0x0013F450u, 0x00140508u, 0x00178200u},
    {"ntsc", 0x0013F350u, 0x00140408u, 0x00178100u},
};

struct GetHit {
    static constexpr const char* id = "rac1.combat.get_hit";
    static constexpr const char* name = "Moby_GetHitMessage";
    static constexpr const char* signature = "u32(u32,i32,i32)";
    using fn = uint32_t (*)(uint32_t, int32_t, int32_t);
};

struct NewGame {
    static constexpr const char* id = "rac1.save.new_game";
    static constexpr const char* name = "Save_NewGame";
    static constexpr const char* signature = "void()";
    using fn = void (*)();
};

struct NewGamePlus {
    static constexpr const char* id = "rac1.save.new_game_plus";
    static constexpr const char* name = "Save_NewGamePlus";
    static constexpr const char* signature = "void(i32)";
    using fn = void (*)(int32_t);
};

struct Weapon {
    const char* name;
    int item;
    double xp_scale;
};

// Item ids and XP scales from RAC-RS Arsenal Mastery.
constexpr std::array<Weapon, 16> kWeapons = {{
    {"wrench", 8, 1.0},
    {"suck_cannon", 9, 1.0},
    {"bomb_glove", 10, 1.0},
    {"devastator", 11, 0.75},
    {"visibomb", 13, 0.75},
    {"taunter", 14, 1.0},
    {"blaster", 15, 1.0},
    {"pyrocitor", 16, 1.0},
    {"mine_glove", 17, 0.75},
    {"walloper", 18, 0.75},
    {"tesla_claw", 19, 0.75},
    {"glove_of_doom", 20, 0.5},
    {"morph_o_ray", 21, 0.5},
    {"ryno", 23, 0.5},
    {"drone_device", 24, 0.5},
    {"decoy", 25, 0.5},
}};

struct Attacker {
    int moby_class;
    int weapon;
};

// Projectile / held-object attribution from RAC-RS power.luau.
// Duplicate classes intentionally resolve to the first entry, matching the original primary map.
constexpr std::array<Attacker, 31> kAttackers = {{
    {121, 2}, {192, 2},
    {74, 8}, {190, 8},
    {305, 6}, {168, 6},
    {457, 13}, {454, 13},
    {186, 11}, {230, 11}, {229, 11},
    {172, 4}, {163, 4},
    {153, 3}, {157, 3},
    {176, 7}, {177, 10}, {180, 9},
    {175, 5}, {185, 12}, {562, 15}, {849, 1},
    {483, 14}, {479, 14}, {203, 15}, {1900, 15},
    {71, 0},
    // Common held/projectile aliases used by the original map.
    {192, 2}, {163, 4}, {168, 6}, {454, 13}
}};

// Enemy classes explicitly treated as enemies by RAC-RS Arsenal Mastery.
constexpr std::array<int16_t, 76> kEnemyClasses = {{
    8, 28, 30, 31, 36, 44, 52, 63, 101, 111, 170, 217, 231, 233, 238, 252, 253,
    294, 326, 333, 340, 347, 388, 427, 455, 459, 491, 541, 563, 572, 573, 574,
    577, 578, 580, 612, 623, 625, 631, 638, 643, 668, 749, 827, 865, 866, 871,
    933, 1023, 1041, 1048, 1051, 1059, 1068, 1106, 1110, 1112, 1126, 1172,
    1196, 1202, 1219, 1229, 1231, 1246, 1261, 1262, 1269, 1356, 1382, 1422,
    1440, 1454, 1805, 1885, 1906
}};

constexpr std::array<double, 4> kXpThresholds = {{750.0, 2250.0, 5000.0, 10000.0}};
constexpr std::array<double, 5> kPower = {{1.0, 1.2, 1.5, 1.9, 2.5}};
constexpr std::array<double, 19> kRebalance = {{
    1.0, 1.0, 1.1, 1.2, 1.3, 1.45, 1.6, 1.75, 1.9, 2.0,
    2.1, 2.2, 2.3, 2.35, 2.4, 2.45, 2.5, 2.5, 2.5
}};

constexpr double kHardFactor = 1.3;
constexpr double kNgPlusFactor = 2.5;
constexpr double kXpPerDamage = 10.0;

constexpr uint32_t kHeroMoby = 0x2080u;
constexpr uint32_t kMobyClass = 0xA6u;
constexpr uint32_t kHitAttacker = 0x20u;
constexpr uint32_t kHitDamage = 0x2Cu;
constexpr uint32_t kHitSize = 0x40u;
constexpr int kHitCount = 64;

struct SeenHit {
    uint32_t victim = 0;
    uint32_t attacker = 0;
    float scaled_damage = 0.0f;
    uint64_t tick = 0;
};

openrac::Mod* sMod = nullptr;
const Disc* sDisc = nullptr;
std::array<double, kWeapons.size()> sXp{};
std::array<int, kWeapons.size()> sLastLevel{};
std::array<SeenHit, kHitCount> sSeen{};
uint64_t sTick = 0;
bool sNgPlus = false;
double sXpRate = 1.0;
double sWeaponDamage = 1.0;
bool sLevelPower = true;
bool sEnemyRebalance = true;
bool sHardRebalance = false;
bool sTrace = false;

template <class T>
T get(gaddr at) {
    return sMod->memory().read<T>(at);
}

template <class T>
void put(gaddr at, T value) {
    sMod->memory().write<T>(at, value);
}

gaddr exe(uint32_t address) {
    return sMod->relocate(address);
}

gaddr level0(uint32_t address) {
    return sMod->relocate_level(0, address);
}

gaddr hero() {
    return exe(sDisc->hero);
}

void clear_progress() {
    sXp.fill(0.0);
    sLastLevel.fill(1);
    sSeen.fill(SeenHit{});
    sNgPlus = false;
}

int weapon_from_item(int item) {
    for (std::size_t i = 0; i < kWeapons.size(); ++i) {
        if (kWeapons[i].item == item) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int weapon_from_attacker(uint32_t attacker) {
    if (attacker == 0) {
        return -1;
    }

    const uint32_t hero_moby = get<uint32_t>(hero() + kHeroMoby);
    if (attacker == hero_moby) {
        return weapon_from_item(get<int32_t>(exe(sDisc->item_in_hand)));
    }

    const int16_t cls = get<int16_t>(attacker + kMobyClass);
    for (const auto& entry : kAttackers) {
        if (entry.moby_class == cls) {
            return entry.weapon;
        }
    }
    return -1;
}

bool is_enemy(uint32_t moby) {
    if (moby == 0 || moby == get<uint32_t>(hero() + kHeroMoby)) {
        return false;
    }
    const int16_t cls = get<int16_t>(moby + kMobyClass);
    return std::find(kEnemyClasses.begin(), kEnemyClasses.end(), cls) != kEnemyClasses.end();
}

int weapon_level(int weapon) {
    const double xp = sXp[static_cast<std::size_t>(weapon)];
    const double scale = kWeapons[static_cast<std::size_t>(weapon)].xp_scale;
    int level = 1;
    for (std::size_t i = 0; i < kXpThresholds.size(); ++i) {
        if (xp >= kXpThresholds[i] * scale) {
            level = static_cast<int>(i) + 2;
        } else {
            break;
        }
    }
    return level;
}

double rebalance_factor() {
    if (!sEnemyRebalance) {
        return 1.0;
    }
    const int level = sMod->loaded_level();
    double factor = 1.0;
    if (level >= 0 && level < static_cast<int>(kRebalance.size())) {
        factor = kRebalance[static_cast<std::size_t>(level)];
    }
    if (sHardRebalance) {
        factor *= kHardFactor;
    }
    if (sNgPlus) {
        factor *= kNgPlusFactor;
    }
    return factor;
}

bool repeated_hit(uint32_t hit, uint32_t victim, uint32_t attacker, float current_damage) {
    const gaddr ring = level0(sDisc->hit_ring);
    if (hit < ring || (hit - ring) % kHitSize != 0) {
        return false;
    }
    const uint32_t index = (hit - ring) / kHitSize;
    if (index >= kHitCount) {
        return false;
    }

    const SeenHit& seen = sSeen[index];
    // Moby_GetHitMessage can expose the same record more than once. After this mod scales it, the
    // next read sees scaled_damage. Suppress that repeat for a small tick window. A later identical
    // attack is allowed once the ring record has genuinely cycled.
    return seen.victim == victim && seen.attacker == attacker
        && std::fabs(seen.scaled_damage - current_damage) < 0.0001f
        && sTick <= seen.tick + 2;
}

void remember_hit(uint32_t hit, uint32_t victim, uint32_t attacker, float scaled_damage) {
    const gaddr ring = level0(sDisc->hit_ring);
    if (hit < ring || (hit - ring) % kHitSize != 0) {
        return;
    }
    const uint32_t index = (hit - ring) / kHitSize;
    if (index < kHitCount) {
        sSeen[index] = {victim, attacker, scaled_damage, sTick};
    }
}

template <class Original>
uint32_t on_get_hit(Original original, uint32_t victim, int32_t mask, int32_t keep) {
    const uint32_t hit = original(victim, mask, keep);
    if (hit == 0 || !is_enemy(victim)) {
        return hit;
    }

    const uint32_t attacker = get<uint32_t>(hit + kHitAttacker);
    const int weapon = weapon_from_attacker(attacker);
    if (weapon < 0) {
        return hit;
    }

    const float raw_damage = get<float>(hit + kHitDamage);
    if (!(raw_damage > 0.0f) || repeated_hit(hit, victim, attacker, raw_damage)) {
        return hit;
    }

    const int before = weapon_level(weapon);
    const double power = sLevelPower ? kPower[static_cast<std::size_t>(before - 1)] : 1.0;
    const double balance = rebalance_factor();
    const double multiplier = power * sWeaponDamage / balance;
    const float scaled = static_cast<float>(raw_damage * multiplier);

    if (std::fabs(scaled - raw_damage) > 0.0001f) {
        put<float>(hit + kHitDamage, scaled);
    }
    remember_hit(hit, victim, attacker, scaled);

    // RAC-RS pays XP from health actually removed and adds a last-hit bonus. OpenRAC currently
    // exposes this earlier get-hit point, so the first port uses effective hit damage as the
    // progression signal. README.md calls out this intentional parity gap.
    const double gain = static_cast<double>(scaled) * kXpPerDamage * sXpRate;
    sXp[static_cast<std::size_t>(weapon)] += gain;

    const int after = weapon_level(weapon);
    if (after > sLastLevel[static_cast<std::size_t>(weapon)]) {
        sLastLevel[static_cast<std::size_t>(weapon)] = after;
        sMod->info(std::string(kWeapons[static_cast<std::size_t>(weapon)].name)
                   + " reached V" + std::to_string(after));
    }

    if (sTrace) {
        sMod->info(
            std::string("hit ") + kWeapons[static_cast<std::size_t>(weapon)].name
            + " xp+" + std::to_string(static_cast<int>(std::lround(gain)))
            + " total=" + std::to_string(static_cast<int>(std::lround(sXp[static_cast<std::size_t>(weapon)])))
            + " V" + std::to_string(after)
            + " damage=" + std::to_string(raw_damage)
            + "->" + std::to_string(scaled)
            + " T=" + std::to_string(balance)
        );
    }

    return hit;
}

template <class Original>
void on_new_game(Original original) {
    original();
    clear_progress();
    sMod->info("new game: Arsenal Mastery progression reset");
}

template <class Original>
void on_new_game_plus(Original original, int32_t slot) {
    original(slot);
    sNgPlus = true;
    sMod->info("New Game+: Arsenal Mastery session progression preserved");
}

void on_frame() {
    ++sTick;
}

void find_disc(openrac::Mod& mod) {
    if (mod.game() != "rac1") {
        throw openrac::Error("arsenal-mastery is for Ratchet & Clank (rac1)");
    }
    sDisc = nullptr;
    for (const auto& disc : kDiscs) {
        if (mod.version() == disc.version) {
            sDisc = &disc;
            break;
        }
    }
    if (sDisc == nullptr) {
        throw openrac::Error(
            "arsenal-mastery knows the PAL and NTSC-U discs, not " + std::string(mod.version())
        );
    }
}

void load(openrac::Mod& mod) {
    sMod = &mod;
    find_disc(mod);

    sXpRate = std::max(0.0, mod.setting<double>("xp_rate"));
    sWeaponDamage = std::max(0.0, mod.setting<double>("weapon_damage"));
    sLevelPower = mod.setting<bool>("level_power");
    sEnemyRebalance = mod.setting<bool>("enemy_rebalance");
    sHardRebalance = mod.setting<bool>("hard_rebalance");
    sTrace = mod.setting<bool>("trace");
    clear_progress();

    mod.hook<GetHit>([](auto original, uint32_t victim, int32_t mask, int32_t keep) {
        return on_get_hit(original, victim, mask, keep);
    });
    mod.hook<NewGame>([](auto original) {
        on_new_game(original);
    });
    mod.hook<NewGamePlus>([](auto original, int32_t slot) {
        on_new_game_plus(original, slot);
    });
    mod.on_frame(on_frame);

    mod.info(
        std::string("Arsenal Mastery core port loaded on ") + mod.version()
        + "; V1-V5 progression enabled"
        + (sEnemyRebalance ? "; enemy rebalance enabled" : "; enemy rebalance off")
        + (sHardRebalance ? " (hard)" : "")
    );
}

}  // namespace

OPENRAC_MOD_CPP("arsenal-mastery", "0.1.0", load)
