// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++ for Going Commando (README.md): the mod's entry point. It checks the disc, makes the mod's
// state and takes the hook points, each handed to the part that owns it:
//
//   tiers.h        the rules: steps, prices, experience, damage, ammo, tints, the enemies' curve
//   save_book.h    the book of saves and the rules that tell which save is in play
//   save_tiers.h   the tiers in play, the clock's watch, tiers.txt, the party's levels
//   game_view.h    the game's memory, texts and text slots, per disc (discs.h)
//   weapons.h      which weapon, which row, which tier
//   text.h         every weapon text at its tier, behind rac2.text.by_id
//   vendor.h       the vendor's entries, prices, purchases and tints
//   experience.h   steps by experience, the level-up, the rows' ammo and bar, the notices
//   combat.h       the tiers' damage and the enemies across new games
//   bolt_rules.h   the drops' scaling, the platinum share and the split into coins
//   platinum_bolts.h
//                  platinum bolts and bolt scaling, behind the bolt hook points (OpenRAC 1.23)
//   weapon_visuals.h
//                  the tiers' colours on the weapons' guns, shots, bots and effects (OpenRAC 1.25)
//   perk_rules.h   0.5.0's rules: perks, the Ice Gun, armour tiers, elites, modifiers, overdrive
//   perks.h        the perks in the game: chains, fans, bursts, fields, the Ice Gun's freeze
//   elites.h       elite enemies and the challenge modes' modifiers
//   armour.h       the armour tiers: protection, look, thorns, last stand
//   armour_vendor.h  their row at the armour vendor (OpenRAC 1.34)
//   shots.h        OpenRAC 1.29's: extra shots, split rockets, mini bombs, shot looks, statuses
//   tuning.h       0.5.1's: the Tesla Claw's steady hits, the Zodiac on bosses, the decoy's health
//   ship_upgrades.h
//                  the ship's weapon tiers, sold at the Ship Shack
//   space_missions.h
//                  the space missions: more waves, tougher ships, elites, the ship's hits
//   ship_looks.h   the ship's glow
//   culling.h      OpenRAC 1.30's: the Zodiac and the Plasma Coil hit enemies off screen too
//
// Each tick: the save's clock is watched (at the title too) and a level left forgets its platinum
// coins; in a level, the steps by experience are taken, the last versions' rows written, the
// levels published to a party, and a tiered weapon taken in hand named.

#include <memory>
#include <optional>
#include <string>

#include "armour.h"
#include "armour_vendor.h"
#include "bolt_rules.h"
#include "collectors.h"
#include "combat.h"
#include "culling.h"
#include "discs.h"
#include "elites.h"
#include "experience.h"
#include "extra_enemies.h"
#include "game_view.h"
#include "hooks.h"
#include "moby_pool.h"
#include "openrac/games/rac2.hpp"
#include "openrac/mod.hpp"
#include "perk_rules.h"
#include "perks.h"
#include "platinum_bolts.h"
#include "rewards.h"
#include "save_tiers.h"
#include "ship_looks.h"
#include "ship_upgrades.h"
#include "shots.h"
#include "space_missions.h"
#include "test_setup.h"
#include "text.h"
#include "tuning.h"
#include "tuning_rules.h"
#include "vendor.h"
#include "weapon_visuals.h"
#include "weapons.h"

namespace ng_plus_plus {
namespace {

// The port's text slot the collectors' dialogs go into: the vendor's (text.cpp), as no vendor is
// open while a collector talks.
constexpr int kCollectorSlot = 1;

// The mod's whole state: the parts, in the order they depend on each other.
//
// Neither copyable nor movable: the parts hold references to each other and the hooks hold a
// pointer to it. Only the game's thread uses it.
class NgPlusPlus {
public:
    // Makes the state for `mod` on `disc`, with the settings the manifest declares.
    NgPlusPlus(openrac::Mod& mod, const Disc& disc)
        : m_mod(mod),
          m_view(mod, disc),
          m_tiers(mod),
          m_weapons(m_view, m_tiers),
          m_experience(m_tiers, m_weapons, mod.setting<bool>("notices")),
          m_vendor(m_weapons, m_experience),
          m_combat(m_weapons, enemy_settings(mod)),
          m_text(m_view, m_weapons, m_vendor, m_experience),
          m_bolts(mod, m_view, bolt_scaling_of(mod.setting<std::string>("bolt_scaling"))),
          m_collectors(mod, m_view, bolt_scaling_of(mod.setting<std::string>("bolt_scaling"))),
          m_rewards(mod, m_view, bolt_scaling_of(mod.setting<std::string>("bolt_scaling"))),
          m_visuals(mod, m_view, m_weapons),
          m_enemies(mod, m_view, m_tiers, mod.setting<bool>("extra_enemies")),
          m_elites(
              mod,
              m_view,
              mod.setting<bool>("elite_enemies"),
              mod.setting<bool>("challenge_modifiers")
          ),
          m_perks(mod, m_view, m_weapons, m_elites, mod.setting<bool>("weapon_perks")),
          m_armour(mod, m_view, m_tiers, m_perks),
          m_armour_vendor(mod, m_view, m_armour),
          m_shots(mod, m_view, m_weapons, m_perks),
          m_tuning(mod, m_view, m_weapons, m_combat, mod.setting<bool>("tesla_steady")),
          m_ship(mod, m_view, m_tiers, disc.progress, disc.vendor),
          m_space(mod, m_view, m_tiers, m_ship, space_settings(mod)),
          m_ship_looks(mod, m_view, m_ship, ship_look_of(mod.setting<std::string>("ship_look")))
#ifdef NGPP_TEST_SETUP
          ,
          m_test(mod, m_view, m_tiers, m_weapons)
#endif
    {
        m_perks.set_shots(&m_shots);
        m_combat.set_parts(&m_perks, &m_elites, &m_armour);
        m_enemies.set_elites(&m_elites);
        m_bolts.set_elites(&m_elites);
        m_combat.set_tuning(&m_tuning);
        m_perks.set_zodiac_credit(&m_tuning.zodiac_credit());
        m_perks.set_claw_share(mod.setting<bool>("tesla_steady") ? kTeslaShare : 1.0f);
        m_combat.set_space(&m_space);
        m_space.set_elites(&m_elites);
    }

    ~NgPlusPlus() = default;
    NgPlusPlus(const NgPlusPlus&) = delete;
    NgPlusPlus& operator=(const NgPlusPlus&) = delete;
    NgPlusPlus(NgPlusPlus&&) = delete;
    NgPlusPlus& operator=(NgPlusPlus&&) = delete;

    // Takes the hook points and the tick, and reads the book. Throws openrac::Error when a hook
    // point NG++ needs is missing.
    void start();

    // Takes a party host's rules (openrac_party_rules_fn): `user` is the state.
    static void on_party_rules(void* user, const void* data, std::uint32_t size);

private:
    // Returns the enemies' settings of `mod`.
    static EnemySettings enemy_settings(const openrac::Mod& mod);

    // Returns the space missions' settings of `mod`.
    static SpaceSettings space_settings(const openrac::Mod& mod);

    // Takes the hook point `HookPoint` for `callable`, refusing the load when the port lacks it.
    template <class HookPoint, class F>
    void take(F&& callable);

    // Takes the optional hook points: the vendor's tints, the text lookup and the bolts.
    void take_optional();

    // An item's colours (rac2.item.colors), for the quick select's and the weapons screen's icons
    // and names: the game's, then the tier's tint at the game address `tint`.
    void colors(
        openrac::Original<void(std::int32_t, std::uint32_t, std::uint32_t, std::uint32_t)> original,
        std::int32_t id,
        std::uint32_t first,
        std::uint32_t second,
        std::uint32_t tint
    );

    // The game's text lookup (rac2.text.by_id): a weapon text at its tier, else the game's.
    std::int32_t text_by_id(
        openrac::Original<std::int32_t(std::int32_t)> original, std::int32_t id
    );

    // Once a game tick.
    void tick();

    // The hero's health steps (rac2.player.nanotech_steps): the game's, then a step for every two
    // weapons at Omega added to the count at game address `count`.
    std::int32_t nanotech_steps(
        openrac::Original<std::int32_t(std::uint32_t, std::uint32_t, std::uint32_t)> original,
        std::uint32_t a0,
        std::uint32_t a1,
        std::uint32_t count
    );

    openrac::Mod& m_mod;           // The mod; the SDK keeps it for the program.
    GameView m_view;               // The game on this disc.
    SaveTiers m_tiers;             // The tiers in play and their book.
    Weapons m_weapons;             // Which weapon, row and tier.
    Experience m_experience;       // Steps by experience and the rows.
    Vendor m_vendor;               // The vendor's entries.
    Combat m_combat;               // Damage and the enemies.
    TextResolver m_text;           // Weapon texts at their tiers.
    PlatinumBolts m_bolts;         // Platinum bolts and the drops' scaling.
    Collectors m_collectors;       // Tabora's and Grelbin's collectors' pay.
    Rewards m_rewards;             // The races' and arenas' prizes and the sellers' prices.
    WeaponVisuals m_visuals;       // The tiers' colours on the weapons' mobys.
    ExtraEnemies m_enemies;        // More enemies each new game.
    Elites m_elites;               // Elite enemies and the run's modifier.
    Perks m_perks;                 // The weapons' perks and the Ice Gun.
    Armour m_armour;               // The armour tiers.
    ArmourVendor m_armour_vendor;  // Their row at the armour vendor.
    Shots m_shots;                 // The perks of OpenRAC 1.29's weapon calls.
    WeaponTuning m_tuning;         // 0.5.1's Tesla Claw, Zodiac and decoy.
    ShipUpgrades m_ship;           // The ship's weapon tiers.
    SpaceMissions m_space;         // The space missions.
    ShipLooks m_ship_looks;        // The ship's glow.
#ifdef NGPP_TEST_SETUP
    TestSetup m_test;  // Test runs only.
#endif
    std::uint32_t m_tick = 0;  // Game ticks since load, for the animated tints.
};

// The mod's state, which the hooks and the tick reach through the pointer they capture. Owned here:
// made by load, deleted by the library's next load (after the SDK has let go of the earlier load's
// hooks), never at exit, since the port may call a hook while the program ends (as the SDK's own
// Mod, mod.hpp's detail::instance). OPENRAC_MOD_CPP's load carries no user pointer to hold it.
NgPlusPlus* g_state = nullptr;

EnemySettings NgPlusPlus::enemy_settings(const openrac::Mod& mod) {
    EnemySettings settings;
    settings.scaling = mod.setting<bool>("enemy_scaling");
    settings.strength = mod.setting<float>("enemy_strength");
    settings.gentle_clank = mod.setting<bool>("gentle_clank");
    return settings;
}

SpaceSettings NgPlusPlus::space_settings(const openrac::Mod& mod) {
    SpaceSettings settings;
    settings.on = mod.setting<bool>("space_missions");
    settings.scaling = mod.setting<bool>("enemy_scaling");
    settings.strength = mod.setting<float>("enemy_strength");
    return settings;
}

template <class HookPoint, class F>
void NgPlusPlus::take(F&& callable) {
    // The port's refusal becomes the mod's, naming what it needs.
    try {
        m_mod.hook<HookPoint>(std::forward<F>(callable));
    } catch (const openrac::Error&) {
        throw openrac::Error(
            std::string("NG++ needs the hook point ") + HookPoint::id + " (OpenRAC with SDK 1.14.6)"
        );
    }
}

void NgPlusPlus::start() {
    NgPlusPlus* self = this;

    // Each hook goes to its part. The port calls them on the game's thread.
    take<openrac::rac2::Vendor_BuildList>([self](auto original, std::int32_t hide) {
        self->m_vendor.build_list(original, hide);
    });
    take<openrac::rac2::Vendor_Price>([self](auto original) {
        return self->m_vendor.price(original);
    });
    take<openrac::rac2::Item_Upgrade>([self](auto original, std::int32_t id) {
        return self->m_vendor.upgrade(original, id);
    });
    take<openrac::rac2::Item_Colors>([self](
                                         auto original,
                                         std::int32_t id,
                                         std::uint32_t first,
                                         std::uint32_t second,
                                         std::uint32_t tint
                                     ) { self->colors(original, id, first, second, tint); });
    take<openrac::rac2::Moby_PostHit>(
        [self](auto original, std::uint32_t victim, std::uint32_t request) {
            self->m_combat.post_hit(original, victim, request);
        }
    );
    take<openrac::rac2::Hero_TakeDamage>([self](auto original, std::int32_t amount) {
        self->m_combat.take_damage(original, amount);
    });
    take<openrac::rac2::Moby_RecordDeath>(
        [self](auto original, std::uint32_t moby, std::int32_t flags, std::int32_t arg) {
            self->m_bolts.death_begins();
            self->m_combat.record_death(original, moby, flags, arg);
            self->m_bolts.death_ends();
        }
    );
    take_optional();

    // More spare moby slots in the levels to come: the game's pool of spawned mobys is 200 to 400,
    // and the copies and a long streak's coins fill it (moby_pool.h). An older OpenRAC keeps the
    // game's pool, and the drops and copies keep inside it.
    const int granted = request_pool(m_mod);
    m_mod.info(
        granted > 0 ? "asked for " + std::to_string(granted) + " more spare moby slots"
                    : std::string(
                          "this OpenRAC has no more moby slots to give (mod interface 1.27): "
                          "the bolts and copies keep to the game's pool"
                      )
    );

    // Once a game tick, on the game's thread.
    try {
        m_mod.on_frame([self] { self->tick(); });
    } catch (const openrac::Error&) {
        throw openrac::Error("no tick callback in this OpenRAC");
    }

    // A party's rules: optional (a game with no party never calls it).
    m_mod.api().on_party_rules(m_mod.handle(), &NgPlusPlus::on_party_rules, this);
    m_tiers.load_book();
}

void NgPlusPlus::take_optional() {
    NgPlusPlus* self = this;

    // The tier's tint in the vendor's list: an OpenRAC without these keeps the game's gold.
    try {
        m_mod.hook<hooks::Vendor_DrawList>([self](auto original) {
            self->m_vendor.draw_list(original);
        });
        m_mod.hook<hooks::Item_VersionColor>([self](auto original, std::int32_t kind) {
            return self->m_vendor.version_color(original, kind, self->m_tick);
        });
    } catch (const openrac::Error& error) {
        m_mod.info(std::string("no tint in the vendor's list: ") + error.what());
    }

    // The armour tiers at the armour vendor: an OpenRAC before 1.34 has no such hook points, and
    // the tiers bought keep working.
    try {
        m_mod.hook<hooks::ArmorVendor_BuildList>([self](auto original) {
            self->m_armour_vendor.build_list(original);
        });
        m_mod.hook<hooks::ArmorVendor_Buy>(
            [self](auto original, std::uint32_t answer, std::uint32_t unused) {
                self->m_armour_vendor.buy(original, answer, unused);
            }
        );
    } catch (const openrac::Error& error) {
        m_mod.info(
            std::string(
                "the armour vendor sells no armour tiers in this build (OpenRAC with mod "
                "interface 1.34): "
            )
            + error.what()
        );
    }

    // The tiers' names in the game's texts: an OpenRAC without it shows the game's.
    try {
        m_mod.hook<hooks::Text_ById>([self](auto original, std::int32_t id) {
            return self->text_by_id(original, id);
        });
        m_experience.set_text_hooked(true);
    } catch (const openrac::Error& error) {
        m_mod.info(std::string("no tier names in the game's texts: ") + error.what());
    }

    // Platinum bolts and bolt scaling: an OpenRAC before 1.23 keeps the game's drops.
    m_bolts.start();

    // The arenas' prizes: an OpenRAC without the hook point keeps the game's.
    m_rewards.start();

    // The tiers' colours on the weapons' mobys: an OpenRAC before 1.25 shows fewer of them.
    m_visuals.start();

    // More enemies each new game: an OpenRAC before 1.25 makes none.
    m_enemies.start();

    // 0.5.0: elites, perks, armour.
    m_elites.start();
    m_perks.start();
    m_armour.start();
    m_shots.start();
    m_tuning.start();
    m_space.start();
    m_ship_looks.start();

    // 0.5.1: the Zodiac and the Plasma Coil hit off screen too (an OpenRAC before 1.30 keeps the
    // game's picks).
    start_culling(m_mod, m_mod.setting<bool>("offscreen_targets"));

    // The hero's health from Omega weapons: an OpenRAC without the hook point goes without.
    try {
        m_mod.hook<openrac::rac2::Hero_NanotechSteps>(
            [self](auto original, std::uint32_t a0, std::uint32_t a1, std::uint32_t count) {
                return self->nanotech_steps(original, a0, a1, count);
            }
        );
    } catch (const openrac::Error& error) {
        m_mod.info(std::string("no health from Omega weapons: ") + error.what());
    }

    // What this build lacks, for a player's log.
    if (m_view.upgrade_fx() == nullptr) {
        m_mod.info(
            "no in-hand level-up in this build: a step of the weapon in hand shows the popup only"
        );
    }

    // No popup or line: a step by experience falls back to the mod's line.
    if (m_view.banner() == nullptr || m_view.message() == nullptr) {
        m_mod.info("no level-up popup in this build: a step by experience shows the mod's notice");
    }
}

void NgPlusPlus::colors(
    openrac::Original<void(std::int32_t, std::uint32_t, std::uint32_t, std::uint32_t)> original,
    std::int32_t id,
    std::uint32_t first,
    std::uint32_t second,
    std::uint32_t tint
) {
    original(id, first, second, tint);

    // Only an item the tables know, and only when the caller asked for the tint.
    if (id < 0 || id >= kItems || tint == 0) {
        return;
    }

    const std::optional<std::uint32_t> rgb = m_weapons.tint(id, m_tiers.level(id), m_tick);

    // No tier: the game's colour stays.
    if (!rgb) {
        return;
    }

    // The tint in the game's alpha.
    const openrac::Memory memory = m_view.memory();
    const auto old = memory.read<std::uint32_t>(tint);
    memory.write(tint, (old & kColorAlpha) | *rgb);
}

std::int32_t NgPlusPlus::text_by_id(
    openrac::Original<std::int32_t(std::int32_t)> original, std::int32_t id
) {
    // The mod's own lookups (the game's names it builds a text from) get the game's texts.
    if (m_view.looking_up_own()) {
        return original(id);
    }

    // The collectors' dialogs, with the scaled total: in the vendor's slot, as no vendor is open
    // while a collector talks.
    if (const std::optional<std::string> trade = m_collectors.dialog(id)) {
        const std::optional<std::uint32_t> slot = m_view.write_slot(kCollectorSlot, *trade);

        // Written: the game prints it.
        if (slot) {
            return static_cast<std::int32_t>(*slot);
        }
    }

    // The armour vendor's row of the mod's: its description.
    if (const std::optional<std::uint32_t> about = m_armour_vendor.text(id)) {
        return static_cast<std::int32_t>(*about);
    }

    const std::optional<std::uint32_t> text = m_text.resolve(id);

    // A game address, as the lookup returns it (the port's slots are negative as an i32).
    return text ? static_cast<std::int32_t>(*text) : original(id);
}

void NgPlusPlus::tick() {
    ++m_tick;
    m_experience.count_down_notice();
    m_vendor.tick();

    // The save's clock: a load, a save or a new game; at the title too.
    m_tiers.watch(m_view);
    m_bolts.tick();
    m_collectors.tick();
    m_enemies.tick();
    m_rewards.tick();
    m_visuals.tick(m_tick);
    m_elites.tick();
    m_perks.tick(m_tick);
    m_shots.tick(m_tick);
    m_tuning.tick(m_tick);
    m_ship.tick(m_tick);
    m_space.tick(m_tick);
    m_ship_looks.tick(m_tick);
#ifdef NGPP_TEST_SETUP
    m_test.tick();
#endif
    m_armour.tick(m_tick, m_armour_vendor.previewed_tier());

    // The rest only in a level.
    if (m_view.loaded_level() < 0) {
        return;
    }

    m_experience.step_by_experience();
    m_experience.write_rows();
    m_tiers.publish();
    m_experience.name_weapon_in_hand();
}

std::int32_t NgPlusPlus::nanotech_steps(
    openrac::Original<std::int32_t(std::uint32_t, std::uint32_t, std::uint32_t)> original,
    std::uint32_t a0,
    std::uint32_t a1,
    std::uint32_t count
) {
    const std::int32_t result = original(a0, a1, count);
    int omega = 0;

    // The weapons at Omega now.
    for (int item = 0; item < kItems; ++item) {
        const WeaponView weapon = m_weapons.weapon(item);
        omega += weapon.has_tier() && tier_of(weapon.kind, weapon.level) >= kTiers ? 1 : 0;
    }

    const int extra = omega_health(omega);

    // Added to the count the game sums into.
    if (extra > 0 && GameView::is_memory(count, sizeof(std::int32_t))) {
        const openrac::Memory memory = m_view.memory();
        memory.write(count, memory.read<std::int32_t>(count) + extra);
    }

    return result;
}

void NgPlusPlus::on_party_rules(void* user, const void* data, std::uint32_t size) {
    static_cast<NgPlusPlus*>(user)->m_tiers.take_party_rules(data, size);
}

// Loads NG++ into `mod`: refuses another game or an unknown disc, then starts the mod's state.
void load(openrac::Mod& mod) {
    // NG++ is Going Commando's.
    if (mod.game() != "rac2") {
        throw openrac::Error("NG++ is for Going Commando (rac2)");
    }

    const Disc* disc = disc_of(mod.version());

    // A disc whose addresses the table does not have.
    if (disc == nullptr) {
        throw openrac::Error(
            "NG++ knows NTSC-U, PAL and NTSC-U v2.00, not " + std::string(mod.version())
        );
    }

    // A load after an earlier one of the library starts afresh: the earlier state goes at the end
    // of this load. The new one is in place before its hooks are taken, so a refused load still
    // leaves them a live state.
    // The Lava Gun's path, which the rules read everywhere (perk_rules.h).
    const bool meteor = mod.setting<std::string>("lava_gun") == "meteor";
    set_lava_path(meteor ? LavaPath::Fire : LavaPath::Ice);

    const std::unique_ptr<NgPlusPlus> earlier(g_state);
    g_state = std::make_unique<NgPlusPlus>(mod, *disc).release();
    g_state->start();
}

}  // namespace
}  // namespace ng_plus_plus

OPENRAC_MOD_CPP("ng-plus-plus", "0.5.1", ng_plus_plus::load)
