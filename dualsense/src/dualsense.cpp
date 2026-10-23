// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The DualSense support (README.md): the PlayStation 5 controller's own buttons as extra
// PlayStation 2 buttons, its light bar showing the game's state, and a vibration the mod
// makes out of what the game does.
//
// The controller is reached through SDL3, which the port already uses for the pad. The mod
// finds the DualSense among the gamepads, reads the buttons the port's own table leaves out
// (the touchpad, the mic, the DualSense Edge's paddles), writes them into the report the
// game reads, and drives the light bar and the motors.
//
// Ratchet & Clank 1 asks for no vibration of its own -- the retail program links no motor
// command at all -- so the vibration here is the mod's own.
//
// The settings (openrac-mod.json), all read once at load:
//   touchpad      what the touchpad's click presses (a PlayStation 2 button, or "none");
//                 Circle by default, so the gadget fires as it does with the button.
//   left_paddle   what the DualSense Edge's left paddle presses; Triangle by default.
//   right_paddle  what the DualSense Edge's right paddle presses; Square by default.
//   microphone    what the microphone button presses; nothing by default, because L1 and R1
//                 are part of the game's free-look and crouch chords.
//   vibration     whether the controller vibrates at all (on by default).
//   strength      how hard it vibrates, 0..100 percent (100 by default).
//   light_bar     whether the light bar shows the hero's health, green to red, and flashes
//                 white when a gold bolt is found (on by default).
//
// How it runs: load() finds the controller and installs a pad callback (the extra buttons
// and the fire button's edge), a frame callback (the weapon's feel, the motors and the light
// bar) and hooks on the game's events (damage, hits, breaking, pickups, gold bolts). The
// callbacks and hooks are assumed to all run on the game's own thread, so the state below
// has no lock.

#include <cstdint>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "game_state.hpp"
#include "openrac/games/rac1.hpp"
#include "openrac/mod.hpp"
#include "ps2_pad.hpp"
#include "rumble.hpp"
#include "weapons.hpp"

namespace {

// ---- Game addresses ----

// The addresses of the two discs the port builds. The hero's health is at the hero's base plus
// 0x22A8 (games/rac1/include/hero.h:176), and the maximum, the global gMaxNanotech, is 3, 5 or
// 8 (games/rac1/include/names.h:903).
//
// The addresses are the game's own, not relocated: the hero lives in the executable's data and
// every level reads it there, which is what the port's own host code does
// (port/game/rac1/host/front_end.c) and what the SDK's script binding does for the same fields
// (port/game/rac1/script_binding.c).
struct Disc {
    std::uint32_t health;
    std::uint32_t max_health;
    std::uint32_t weapon_moby;  // the equipped weapon's moby
    std::uint32_t item_id;      // the equipped weapon's id
    std::uint32_t fire_flag;    // the fire animation is running
    std::uint32_t fire_age;     // frames since it started
    std::uint32_t hero_state;   // the hero's state
};

// The two discs the port builds: the hero's base plus the offsets in game_state.hpp, and the
// global maximum hit points (seen in game code, see game_state.hpp).
constexpr Disc kDiscs[] = {
    // PAL
    {dualsense::kHeroBasePal + dualsense::kHeroHealthOffset,
     dualsense::kMaxHealthPal,
     dualsense::kHeroBasePal + dualsense::kHeroWeaponMobyOffset,
     dualsense::kHeroBasePal + dualsense::kHeroItemIdOffset,
     dualsense::kHeroBasePal + dualsense::kHeroFireFlagOffset,
     dualsense::kHeroBasePal + dualsense::kHeroFireAgeOffset,
     dualsense::kHeroBasePal + dualsense::kHeroStateOffset},
    // NTSC-U
    {dualsense::kHeroBaseNtsc + dualsense::kHeroHealthOffset,
     dualsense::kMaxHealthNtsc,
     dualsense::kHeroBaseNtsc + dualsense::kHeroWeaponMobyOffset,
     dualsense::kHeroBaseNtsc + dualsense::kHeroItemIdOffset,
     dualsense::kHeroBaseNtsc + dualsense::kHeroFireFlagOffset,
     dualsense::kHeroBaseNtsc + dualsense::kHeroFireAgeOffset,
     dualsense::kHeroBaseNtsc + dualsense::kHeroStateOffset},
};

// ---- Constants ----

// The vendor id of Sony, as USB reports it (assumed).
constexpr std::uint16_t kSonyVendorId = 0x054C;

// The product id of the DualSense (assumed).
constexpr std::uint16_t kDualSenseProductId = 0x0CE6;

// The product id of the DualSense Edge (assumed).
constexpr std::uint16_t kDualSenseEdgeProductId = 0x0DF2;

// The hit points of the hero before any nanotech upgrade; the light bar's scale when the game's
// maximum cannot be read (seen in game code: gMaxNanotech starts at 3, names.h:903).
constexpr int kDefaultMaxHealth = 3;

// The strength setting's range, in percent (documented: openrac-mod.json).
constexpr int kMinStrengthPercent = 0;
constexpr int kMaxStrengthPercent = 100;

// The top of the weapons' strength scale, 1..8 (documented: weapons.hpp).
constexpr std::uint8_t kFeelScaleTop = 8;

// How long each motor command lasts, in milliseconds; the mod sends a new one every frame, so
// the motors stop soon after the mod does (assumed).
constexpr std::uint32_t kMotorCommandMs = 60;

// How long the light bar flashes when a gold bolt is found, in milliseconds (assumed).
constexpr std::uint32_t kGoldFlashMs = 400;

// The lengths of the weapons' per-shot vibrations, in milliseconds (assumed: tuned by feel).
constexpr std::uint32_t kRynoShotMs = 90;
constexpr std::uint32_t kBlasterKickMs = 60;
constexpr std::uint32_t kClickLeadMs = 40;
constexpr std::uint32_t kClickTailMs = 90;

// The light bar's colours, as red, green and blue (assumed).
constexpr std::uint8_t kHealthFull[3] = {0, 255, 0};
constexpr std::uint8_t kHealthLow[3] = {255, 0, 0};
constexpr std::uint8_t kGold[3] = {255, 200, 0};
constexpr std::uint8_t kFlash[3] = {255, 255, 255};

// ---- Types ----

// The DualSense's own buttons, as SDL names them.
struct ExtraButton {
    const char* setting;          // the manifest's key
    int sdl_button;               // SDL_GAMEPAD_BUTTON_*
    dualsense::Ps2Button button;  // the PlayStation 2 button it becomes
    bool enabled = true;
};

// The button the report carries, and the PlayStation 2 button it presses.
struct Binding {
    int sdl_button = 0;
    dualsense::Ps2Button button = dualsense::kSelect;
};

// The state the mod keeps between the ticks.
struct State {
    SDL_Gamepad* pad = nullptr;
    bool owns_pad = false;

    std::vector<Binding> bindings;

    dualsense::Rumble rumble;
    bool vibration = true;
    int strength = 100;

    bool light_bar = true;
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t last_red = 0;
    std::uint8_t last_green = 0;
    std::uint8_t last_blue = 0;
    std::uint32_t flash_until = 0;

    Disc disc{};
    bool have_disc = false;

    // The weapon the game is using, and what it was doing before.
    dualsense::WeaponState weapon;
    dualsense::WeaponState weapon_before;
    bool fire_pressed = false;     // the fire button went down this tick
    bool fire_held = false;        // the fire button is down
    std::uint16_t hold_heavy = 0;  // the steady part of the weapon's feel
    std::uint16_t hold_light = 0;

    std::uint32_t now = 0;
    std::uint32_t last_gold_bolts = 0;
    std::uint32_t last_hit = 0;
    bool said_no_rumble = false;
    bool said_no_light = false;
    bool said_no_effect = false;
};

// ---- State ----

// Everything the mod remembers. The pad callback, the frame callback, the hooks and the unload
// all touch it; they are assumed to run on the game's own thread, one at a time.
State g_state;

// ---- Helpers ----

// The disc the mod is running on: fills `out` and returns true for "pal" and "ntsc", and
// returns false for any other version, which the mod has no addresses for.
bool disc_for(const std::string& version, Disc* out) {
    // The PAL disc.
    if (version == "pal") {
        *out = kDiscs[0];
        return true;
    }

    // The NTSC-U disc.
    if (version == "ntsc") {
        *out = kDiscs[1];
        return true;
    }

    return false;
}

// Finds the DualSense among the gamepads SDL knows. SDL's own table knows the controller's
// type; the vendor and product say which one it is when the type is not enough. Returns the
// opened gamepad, which the caller owns, or null when there is none.
SDL_Gamepad* find_dualsense() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);

    // Each gamepad SDL knows is opened and looked at in turn.
    for (int i = 0; i < count; ++i) {
        SDL_Gamepad* pad = SDL_OpenGamepad(ids[i]);

        // A gamepad that will not open is skipped, not an error: another one may be the one.
        if (pad == nullptr) {
            continue;
        }

        // SDL's own table says this is a PlayStation 5 controller.
        if (SDL_GetGamepadType(pad) == SDL_GAMEPAD_TYPE_PS5) {
            return pad;
        }

        const std::uint16_t vendor = SDL_GetGamepadVendor(pad);
        const std::uint16_t product = SDL_GetGamepadProduct(pad);

        // SDL's table did not say, but the vendor and product are a DualSense's or an Edge's.
        if (vendor == kSonyVendorId
            && (product == kDualSenseProductId || product == kDualSenseEdgeProductId)) {
            return pad;
        }

        SDL_CloseGamepad(pad);
    }

    return nullptr;
}

// A colour between two, by `t` in 0..1; the result is clamped to a byte and rounded.
std::uint8_t mix(std::uint8_t a, std::uint8_t b, float t) {
    const float value = static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t;

    // A value below the byte's range is the darkest colour.
    if (value < 0.0f) {
        return 0;
    }

    // A value above the byte's range is the brightest colour.
    if (value > 255.0f) {
        return 255;
    }

    return static_cast<std::uint8_t>(value + 0.5f);
}

// Reads a word from the game's memory.
std::int32_t read_word(openrac::Mod& mod, openrac::gaddr address) {
    return mod.memory().read<std::int32_t>(address);
}

// Starts a vibration of `kind` (see rumble_for), if the mod is asked to vibrate. `fall_ms` is
// only for "land".
void play(const char* kind, std::uint32_t fall_ms = 0) {
    // The player turned the vibration off, so nothing is played.
    if (!g_state.vibration) {
        return;
    }

    const std::vector<dualsense::RumbleStep> steps =
        dualsense::rumble_for(kind, g_state.strength, fall_ms);

    // An event the table does not know makes no steps; there is nothing to play then.
    if (!steps.empty()) {
        g_state.rumble.play(steps);
    }
}

// The light bar: sets g_state's colour to a flash while one runs, else to the hero's health.
// Reads the game's memory, so it is called from the frame callback only.
void update_light_bar(openrac::Mod& mod) {
    // The player turned the light bar off, so it stays dark.
    if (!g_state.light_bar) {
        g_state.red = g_state.green = g_state.blue = 0;
        return;
    }

    // A flash is running: it shows instead of the health until it ends.
    if (g_state.now < g_state.flash_until) {
        g_state.red = kFlash[0];
        g_state.green = kFlash[1];
        g_state.blue = kFlash[2];
        return;
    }

    int health = 0;
    int maximum = kDefaultMaxHealth;

    // The addresses are known for this disc, so the health and its maximum can be read.
    if (g_state.have_disc) {
        health = read_word(mod, g_state.disc.health);
        const int value = read_word(mod, g_state.disc.max_health);

        // A maximum that is not positive is not a real one, so the default stays.
        if (value > 0) {
            maximum = value;
        }
    }

    // Guards the division below.
    if (maximum <= 0) {
        maximum = kDefaultMaxHealth;
    }

    // A health below zero is shown as none.
    if (health < 0) {
        health = 0;
    }

    // A health above the maximum is shown as the maximum.
    if (health > maximum) {
        health = maximum;
    }

    // Full health is green, the last life is red.
    const float t = maximum > 0 ? static_cast<float>(health) / static_cast<float>(maximum) : 0.0f;
    g_state.red = mix(kHealthLow[0], kHealthFull[0], t);
    g_state.green = mix(kHealthLow[1], kHealthFull[1], t);
    g_state.blue = mix(kHealthLow[2], kHealthFull[2], t);
}

// The light bar flashes for `ms` milliseconds from now.
void flash(std::uint32_t ms) {
    g_state.flash_until = g_state.now + ms;
}

// A strength in the table's scale (1..8) as the motors take it (0..65535).
std::uint16_t strength_of(std::uint8_t strength) {
    // Nothing is nothing.
    if (strength == 0) {
        return 0;
    }

    // The top of the scale (and anything above it) is the full strength.
    if (strength >= kFeelScaleTop) {
        return dualsense::kFull;
    }

    return static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(dualsense::kFull) * static_cast<std::uint32_t>(strength)
        / static_cast<std::uint32_t>(kFeelScaleTop)
    );
}

// Reads what the game is doing with the weapon: the equipped weapon, the
// game's own flag that it is firing, and the weapon's own counters. Returns an
// empty state when the disc's addresses are not known.
dualsense::WeaponState read_weapon(openrac::Mod& mod) {
    dualsense::WeaponState state;

    // Without the disc's addresses there is nothing to read.
    if (!g_state.have_disc) {
        return state;
    }

    const Disc& disc = g_state.disc;
    state.item_id = read_word(mod, disc.item_id);
    state.flag = mod.memory().read<std::uint8_t>(disc.fire_flag) != 0;
    state.age = read_word(mod, disc.fire_age);
    state.hero_state = read_word(mod, disc.hero_state);
    state.pressed = g_state.fire_pressed;

    // The weapon's moby holds its own counters, in its pvars. The moby is
    // only there when a weapon is out: with none, the slot holds whatever was
    // in it, so the pointer is checked before it is followed.
    const std::uint32_t moby = mod.memory().read<std::uint32_t>(disc.weapon_moby);

    // The moby pointer lies in the game's RAM, so it can be followed.
    if (dualsense::in_ram(moby, dualsense::kMobyPvarsOffset + 4)) {
        const std::uint32_t pvars =
            mod.memory().read<std::uint32_t>(moby + dualsense::kMobyPvarsOffset);

        // The pvars pointer lies in the game's RAM too, so the counters can be read.
        if (dualsense::in_ram(pvars, dualsense::kPvarShots + 4)) {
            state.counter = read_word(mod, pvars + dualsense::kPvarCounter);
            state.shots = mod.memory().read<std::uint8_t>(pvars + dualsense::kPvarShots);
        }
    }

    return state;
}

// ---- Load: the settings ----

// Binds the extra buttons the manifest names: for each of the touchpad, the paddles and the
// microphone button, the PlayStation 2 button its setting says, if any.
void bind_extra_buttons(openrac::Mod& mod) {
    // The extra buttons: what the port's own table leaves out.
    struct Extra {
        const char* setting;
        int sdl_button;
    };

    const Extra extras[] = {
        {"touchpad", SDL_GAMEPAD_BUTTON_TOUCHPAD},
        {"left_paddle", SDL_GAMEPAD_BUTTON_LEFT_PADDLE1},
        {"right_paddle", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1},
        {"microphone", SDL_GAMEPAD_BUTTON_MISC1},
    };

    // Each extra button is looked at in turn.
    for (const Extra& extra : extras) {
        const std::string name = mod.setting<std::string>(extra.setting);

        // "none" (or nothing) means the player wants this button to do nothing.
        if (name == "none" || name.empty()) {
            continue;
        }

        bool found = false;
        const dualsense::Ps2Button button = dualsense::button_from_name(name.c_str(), &found);

        // A name that is no button is the player's mistake: it is said, and the button is left
        // unbound.
        if (!found) {
            mod.warn(
                std::string("the ") + extra.setting + " setting names no button (" + name
                + "); it does nothing"
            );
            continue;
        }

        Binding binding;
        binding.sdl_button = extra.sdl_button;
        binding.button = button;
        g_state.bindings.push_back(binding);
        mod.info(std::string("the ") + extra.setting + " is the " + dualsense::button_name(button));
    }
}

// Reads the vibration, its strength and the light bar's settings.
void read_settings(openrac::Mod& mod) {
    g_state.vibration = mod.setting<bool>("vibration");
    g_state.strength = static_cast<int>(mod.setting<long long>("strength"));

    // A strength below the setting's range is the lowest.
    if (g_state.strength < kMinStrengthPercent) {
        g_state.strength = kMinStrengthPercent;
    }

    // A strength above the setting's range is the highest.
    if (g_state.strength > kMaxStrengthPercent) {
        g_state.strength = kMaxStrengthPercent;
    }

    g_state.light_bar = mod.setting<bool>("light_bar");
}

// ---- Pad callback ----

// The pad report: the extra buttons are written into it before the game reads it, and the
// fire button's edge is noted for the weapons. Only the first port is the player's.
void on_pad_report(int port, std::uint8_t* report, int size) {
    // Another port, a closed pad or a report too short to hold the buttons is not ours.
    if (port != 0 || g_state.pad == nullptr || size < dualsense::kButtonBytes) {
        return;
    }

    // Each bound extra button that is held presses its PlayStation 2 button.
    for (const Binding& binding : g_state.bindings) {
        const bool down =
            SDL_GetGamepadButton(g_state.pad, static_cast<SDL_GamepadButton>(binding.sdl_button));

        // Only a held button changes the report; a released one leaves the game's own state.
        if (down) {
            dualsense::press_button(report, size, binding.button);
        }
    }

    // The fire button's pressed edge: every weapon but the wrench fires
    // with Circle (games/rac1/src/overlays/shared/help_0020CDF0.c:939-942),
    // and the two weapons whose bodies are still assembly read the pad
    // themselves.
    const std::uint16_t buttons = dualsense::read_buttons(report);
    const bool circle = (buttons & static_cast<std::uint16_t>(dualsense::kCircle)) == 0;
    g_state.fire_pressed = circle && !g_state.fire_held;
    g_state.fire_held = circle;
}

// ---- Frame callback ----

// Works out the steady part of the weapon's feel (g_state.hold_heavy and hold_light) while it
// fires, and plays its own part at the shot.
void update_weapon_feel(const dualsense::WeaponFeel* feel, const dualsense::WeaponMoment& moment) {
    g_state.hold_heavy = 0;
    g_state.hold_light = 0;

    // A weapon that is not in the table has no feel.
    if (feel == nullptr) {
        return;
    }

    // The steady part runs only while the weapon fires: heavy for the steady and the kick,
    // light for the buzz and the buzz-rumble.
    if (feel->feel == dualsense::Feel::kSteady && moment.firing) {
        g_state.hold_heavy = strength_of(feel->steady);
    } else if (feel->feel == dualsense::Feel::kBuzz && moment.firing) {
        g_state.hold_light = strength_of(feel->steady);
    } else if (feel->feel == dualsense::Feel::kKick && moment.firing) {
        g_state.hold_heavy = strength_of(feel->steady);
    } else if (feel->feel == dualsense::Feel::kBuzzRumble && moment.firing) {
        g_state.hold_light = strength_of(feel->steady);
    }

    // No shot went off this tick, so there is no per-shot part to play.
    if (!moment.shot) {
        return;
    }

    // Each feel has its own per-shot part.
    if (feel->feel == dualsense::Feel::kBuzzRumble) {
        // The R.Y.N.O.: every missile of the salvo is a full rumble.
        g_state.rumble.play({{dualsense::kFull, dualsense::kFull, kRynoShotMs}});
    } else if (feel->feel == dualsense::Feel::kKick) {
        // The Blaster: a kick with every shot.
        g_state.rumble.play({{strength_of(feel->shot), 0, kBlasterKickMs}});
    } else if (feel->feel == dualsense::Feel::kClick) {
        // A stiff wall that snaps: the click, and then the shot.
        g_state.rumble.play(
            {{0, strength_of(feel->shot), kClickLeadMs},
             {static_cast<std::uint16_t>(strength_of(feel->shot) / 2), 0, kClickTailMs}}
        );
    }
}

// Sends the motors the larger of the weapon's steady part and the vibration playing.
void drive_motors() {
    std::uint16_t heavy = g_state.rumble.heavy();
    std::uint16_t light = g_state.rumble.light();

    // The weapon's heavy part is stronger than the vibration's.
    if (g_state.hold_heavy > heavy) {
        heavy = g_state.hold_heavy;
    }

    // The weapon's light part is stronger than the vibration's.
    if (g_state.hold_light > light) {
        light = g_state.hold_light;
    }

    // With the vibration off the motors are left alone.
    if (g_state.vibration) {
        SDL_RumbleGamepad(g_state.pad, heavy, light, kMotorCommandMs);
    }
}

// Writes the light bar when its colour has changed since the last write, and says once if the
// controller refuses it.
void write_light_bar(openrac::Mod& mod) {
    update_light_bar(mod);

    // The colour is the one already on the controller, so there is nothing to send.
    if (g_state.red == g_state.last_red && g_state.green == g_state.last_green
        && g_state.blue == g_state.last_blue) {
        return;
    }

    // The controller refused the colour; this is said once, not on every frame.
    if (!SDL_SetGamepadLED(g_state.pad, g_state.red, g_state.green, g_state.blue)
        && !g_state.said_no_light) {
        g_state.said_no_light = true;
        mod.error(std::string("the light bar would not light: ") + SDL_GetError());
    }

    g_state.last_red = g_state.red;
    g_state.last_green = g_state.green;
    g_state.last_blue = g_state.blue;
}

// The vibration and the light bar, once a tick.
void on_frame(openrac::Mod& mod) {
    g_state.now = SDL_GetTicks();
    g_state.rumble.update(g_state.now);

    // What the game is doing with the weapon.
    g_state.weapon_before = g_state.weapon;
    g_state.weapon = read_weapon(mod);
    const dualsense::WeaponFeel* feel = dualsense::feel_for(g_state.weapon.item_id);
    const dualsense::WeaponMoment moment =
        dualsense::weapon_moment(g_state.weapon, g_state.weapon_before);

    // The weapon's feel: the steady part while it fires, and its own part
    // at the shot.
    update_weapon_feel(feel, moment);

    // The motors: the weapon's steady part, and whatever the game just
    // made happen.
    drive_motors();

    // The light bar, written when its colour changes.
    write_light_bar(mod);
}

// ---- Hooks ----

// The game's events, made into vibrations.
void install_hooks(openrac::Mod& mod) {
    mod.hook<openrac::rac1::Hero_TakeDamage>([](auto original, std::int32_t amount) {
        play("damage");
        return original(amount);
    });
    // The hit hook is called every time the game reads the hits that are pending, so the same hit
    // is seen again and again. The record it returns is the attacker's, so a change of it means
    // another hit.
    mod.hook<openrac::rac1::Moby_GetHitMessage>(
        [](auto original, std::uint32_t moby, std::int32_t mask, std::int32_t keep) {
            const std::uint32_t record = original(moby, mask, keep);

            // A new attacker's record is a new hit; the same one again is the hit already felt.
            if (record != 0 && record != g_state.last_hit) {
                g_state.last_hit = record;
                play("hit");
            }

            return record;
        }
    );
    mod.hook<openrac::rac1::Moby_RecordDeath>(
        [](auto original, std::uint32_t moby, std::int32_t flags, std::int32_t index) {
            play("break");
            return original(moby, flags, index);
        }
    );
    mod.hook<openrac::rac1::Item_Give>([](auto original, std::int32_t id, std::int32_t equip) {
        play("pickup");
        return original(id, equip);
    });
    mod.hook<openrac::rac1::GoldBolts_CountCollected>([](auto original) {
        const std::int32_t count = original();

        // The count changed (or is unreadable), so a gold bolt was found since the last look.
        if (count < 0 || static_cast<std::uint32_t>(count) != g_state.last_gold_bolts) {
            g_state.last_gold_bolts = static_cast<std::uint32_t>(count);
            play("gold");
            flash(kGoldFlashMs);
        }

        return count;
    });
}

}  // namespace

// ---- Entry points ----

namespace dualsense_mod {

// The mod's load: the controller, the settings, the callbacks and the hooks. Throws when the
// mod cannot run: another game, an unknown disc, SDL that will not start or no DualSense.
void load(openrac::Mod& mod) {
    // The mod's addresses are Ratchet & Clank 1's.
    if (mod.game() != "rac1") {
        throw openrac::Error("this mod is for Ratchet & Clank");
    }

    g_state.have_disc = disc_for(std::string(mod.version()), &g_state.disc);

    // Without the disc's addresses the mod cannot read the hero.
    if (!g_state.have_disc) {
        throw openrac::Error("this mod does not know the " + std::string(mod.version()) + " disc");
    }

    // SDL: the port has it, and the mod links its own copy of it, so the mod works with a
    // port that has SDL built in and with one that uses the system's.
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        throw openrac::Error(std::string("SDL would not start: ") + SDL_GetError());
    }

    g_state.pad = find_dualsense();

    // No DualSense: SDL is shut down again, since the mod will not load.
    if (g_state.pad == nullptr) {
        SDL_Quit();
        throw openrac::Error("no PlayStation 5 DualSense is connected");
    }

    mod.info(std::string("DualSense: ") + SDL_GetGamepadName(g_state.pad));

    bind_extra_buttons(mod);
    read_settings(mod);

    // The pad report: the extra buttons are written into it before the game reads it.
    mod.on_pad([](int port, std::uint8_t* report, int size) { on_pad_report(port, report, size); });

    // The vibration and the light bar, once a tick.
    mod.on_frame([&mod]() { on_frame(mod); });

    install_hooks(mod);
}

}  // namespace dualsense_mod

OPENRAC_MOD_CPP("dualsense", "1.0.0", dualsense_mod::load)

// The mod's unload: the motors and the light bar are switched off and the controller is
// released, so it does not keep vibrating or glowing after the mod is gone.
extern "C" OPENRAC_MOD_EXPORT void openrac_mod_unload(void) {
    // The controller was opened (load can fail before that), so it is put to rest and closed.
    if (g_state.pad != nullptr) {
        SDL_RumbleGamepad(g_state.pad, 0, 0, 0);
        SDL_SetGamepadLED(g_state.pad, 0, 0, 0);
        SDL_CloseGamepad(g_state.pad);
        g_state.pad = nullptr;
    }

    SDL_Quit();
}
