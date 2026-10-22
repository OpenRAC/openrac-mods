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

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "openrac/games/rac1.hpp"
#include "openrac/mod.hpp"

#include "game_state.hpp"
#include "ps2_pad.hpp"
#include "rumble.hpp"
#include "weapons.hpp"

namespace {

// The DualSense's own buttons, as SDL names them.
struct ExtraButton {
    const char* setting;                        // the manifest's key
    int sdl_button;                              // SDL_GAMEPAD_BUTTON_*
    dualsense::Ps2Button button;                 // the PlayStation 2 button it becomes
    bool enabled = true;
};

// The button the report carries, and the PlayStation 2 button it presses.
struct Binding {
    int sdl_button = 0;
    dualsense::Ps2Button button = dualsense::kSelect;
};

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

// The two discs the port builds: the hero's base plus the offsets in
// game_state.hpp, and the global maximum hit points.
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
    bool fire_pressed = false;   // the fire button went down this tick
    bool fire_held = false;      // the fire button is down
    std::uint16_t hold_heavy = 0;  // the steady part of the weapon's feel
    std::uint16_t hold_light = 0;

    std::uint32_t now = 0;
    std::uint32_t last_gold_bolts = 0;
    std::uint32_t last_hit = 0;
    bool said_no_rumble = false;
    bool said_no_light = false;
    bool said_no_effect = false;
};

State g_state;

// The light bar's colours.
constexpr std::uint8_t kHealthFull[3] = {0, 255, 0};
constexpr std::uint8_t kHealthLow[3] = {255, 0, 0};
constexpr std::uint8_t kGold[3] = {255, 200, 0};
constexpr std::uint8_t kFlash[3] = {255, 255, 255};

// The disc the mod is running on.
bool disc_for(const std::string& version, Disc* out) {
    if (version == "pal") {
        *out = kDiscs[0];
        return true;
    }
    if (version == "ntsc") {
        *out = kDiscs[1];
        return true;
    }
    return false;
}

// Finds the DualSense among the gamepads SDL knows. SDL's own table knows the controller's
// type; the vendor and product say which one it is when the type is not enough.
SDL_Gamepad* find_dualsense() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count; ++i) {
        SDL_Gamepad* pad = SDL_OpenGamepad(ids[i]);
        if (pad == nullptr) {
            continue;
        }
        if (SDL_GetGamepadType(pad) == SDL_GAMEPAD_TYPE_PS5) {
            return pad;
        }
        const std::uint16_t vendor = SDL_GetGamepadVendor(pad);
        const std::uint16_t product = SDL_GetGamepadProduct(pad);
        if (vendor == 0x054C && (product == 0x0CE6 || product == 0x0DF2)) {
            return pad;
        }
        SDL_CloseGamepad(pad);
    }
    return nullptr;
}

// A colour between two, by `t` in 0..1.
std::uint8_t mix(std::uint8_t a, std::uint8_t b, float t) {
    const float value = static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t;
    if (value < 0.0f) {
        return 0;
    }
    if (value > 255.0f) {
        return 255;
    }
    return static_cast<std::uint8_t>(value + 0.5f);
}

// Reads a word from the game's memory.
std::int32_t read_word(openrac::Mod& mod, openrac::gaddr address) {
    return mod.memory().read<std::int32_t>(address);
}

// Starts a vibration, if the mod is asked to.
void play(const char* kind, std::uint32_t fall_ms = 0) {
    if (!g_state.vibration) {
        return;
    }
    const std::vector<dualsense::RumbleStep> steps =
        dualsense::rumble_for(kind, g_state.strength, fall_ms);
    if (!steps.empty()) {
        g_state.rumble.play(steps);
    }
}

// The light bar: the hero's health, and a flash when something happens.
void update_light_bar(openrac::Mod& mod) {
    if (!g_state.light_bar) {
        g_state.red = g_state.green = g_state.blue = 0;
        return;
    }
    if (g_state.now < g_state.flash_until) {
        g_state.red = kFlash[0];
        g_state.green = kFlash[1];
        g_state.blue = kFlash[2];
        return;
    }
    int health = 0;
    int maximum = 3;
    if (g_state.have_disc) {
        health = read_word(mod, g_state.disc.health);
        const int value = read_word(mod, g_state.disc.max_health);
        if (value > 0) {
            maximum = value;
        }
    }
    if (maximum <= 0) {
        maximum = 3;
    }
    if (health < 0) {
        health = 0;
    }
    if (health > maximum) {
        health = maximum;
    }
    // Full health is green, the last life is red.
    const float t = maximum > 0 ? static_cast<float>(health) / static_cast<float>(maximum) : 0.0f;
    g_state.red = mix(kHealthLow[0], kHealthFull[0], t);
    g_state.green = mix(kHealthLow[1], kHealthFull[1], t);
    g_state.blue = mix(kHealthLow[2], kHealthFull[2], t);
}

// The light bar flashes for a while.
void flash(std::uint32_t ms) {
    g_state.flash_until = g_state.now + ms;
}

// A strength in the table's scale (1..8) as the motors take it.
std::uint16_t strength_of(std::uint8_t strength) {
    if (strength == 0) {
        return 0;
    }
    if (strength >= 8) {
        return dualsense::kFull;
    }
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(dualsense::kFull) *
                                       static_cast<std::uint32_t>(strength) / 8u);
}

// Reads what the game is doing with the weapon: the equipped weapon, the
// game's own flag that it is firing, and the weapon's own counters.
dualsense::WeaponState read_weapon(openrac::Mod& mod) {
    dualsense::WeaponState state;
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
    if (dualsense::in_ram(moby, dualsense::kMobyPvarsOffset + 4)) {
        const std::uint32_t pvars = mod.memory().read<std::uint32_t>(moby + dualsense::kMobyPvarsOffset);
        if (dualsense::in_ram(pvars, dualsense::kPvarShots + 4)) {
            state.counter = read_word(mod, pvars + dualsense::kPvarCounter);
            state.shots = mod.memory().read<std::uint8_t>(pvars + dualsense::kPvarShots);
        }
    }
    return state;
}

}  // namespace

namespace dualsense_mod {

// The mod's load: the controller, the hooks, and the settings.
void load(openrac::Mod& mod) {
    if (mod.game() != "rac1") {
        throw openrac::Error("this mod is for Ratchet & Clank");
    }
    g_state.have_disc = disc_for(std::string(mod.version()), &g_state.disc);
    if (!g_state.have_disc) {
        throw openrac::Error("this mod does not know the " + std::string(mod.version()) + " disc");
    }

    // SDL: the port has it, and the mod links its own copy of it, so the mod works with a
    // port that has SDL built in and with one that uses the system's.
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        throw openrac::Error(std::string("SDL would not start: ") + SDL_GetError());
    }
    g_state.pad = find_dualsense();
    if (g_state.pad == nullptr) {
        SDL_Quit();
        throw openrac::Error("no PlayStation 5 DualSense is connected");
    }
    mod.info(std::string("DualSense: ") + SDL_GetGamepadName(g_state.pad));

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
    for (const Extra& extra : extras) {
        const std::string name = mod.setting<std::string>(extra.setting);
        if (name == "none" || name.empty()) {
            continue;
        }
        bool found = false;
        const dualsense::Ps2Button button = dualsense::button_from_name(name.c_str(), &found);
        if (!found) {
            mod.warn(std::string("the ") + extra.setting + " setting names no button (" + name +
                     "); it does nothing");
            continue;
        }
        Binding binding;
        binding.sdl_button = extra.sdl_button;
        binding.button = button;
        g_state.bindings.push_back(binding);
        mod.info(std::string("the ") + extra.setting + " is the " + dualsense::button_name(button));
    }

    g_state.vibration = mod.setting<bool>("vibration");
    g_state.strength = static_cast<int>(mod.setting<long long>("strength"));
    if (g_state.strength < 0) {
        g_state.strength = 0;
    }
    if (g_state.strength > 100) {
        g_state.strength = 100;
    }
    g_state.light_bar = mod.setting<bool>("light_bar");

    // The pad report: the extra buttons are written into it before the game reads it.
    mod.on_pad([](int port, std::uint8_t* report, int size) {
        if (port != 0 || g_state.pad == nullptr || size < dualsense::kButtonBytes) {
            return;
        }
        for (const Binding& binding : g_state.bindings) {
            const bool down =
                SDL_GetGamepadButton(g_state.pad, static_cast<SDL_GamepadButton>(binding.sdl_button));
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
    });

    // The vibration and the light bar, once a tick.
    mod.on_frame([&mod]() {
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
        g_state.hold_heavy = 0;
        g_state.hold_light = 0;
        if (feel != nullptr) {
            if (feel->feel == dualsense::Feel::kSteady && moment.firing) {
                g_state.hold_heavy = strength_of(feel->steady);
            } else if (feel->feel == dualsense::Feel::kBuzz && moment.firing) {
                g_state.hold_light = strength_of(feel->steady);
            } else if (feel->feel == dualsense::Feel::kKick && moment.firing) {
                g_state.hold_heavy = strength_of(feel->steady);
            } else if (feel->feel == dualsense::Feel::kBuzzRumble && moment.firing) {
                g_state.hold_light = strength_of(feel->steady);
            }
            if (moment.shot) {
                if (feel->feel == dualsense::Feel::kBuzzRumble) {
                    // The R.Y.N.O.: every missile of the salvo is a full rumble.
                    g_state.rumble.play({{dualsense::kFull, dualsense::kFull, 90}});
                } else if (feel->feel == dualsense::Feel::kKick) {
                    // The Blaster: a kick with every shot.
                    g_state.rumble.play({{strength_of(feel->shot), 0, 60}});
                } else if (feel->feel == dualsense::Feel::kClick) {
                    // A stiff wall that snaps: the click, and then the shot.
                    g_state.rumble.play({{0, strength_of(feel->shot), 40},
                                          {static_cast<std::uint16_t>(strength_of(feel->shot) / 2),
                                           0, 90}});
                }
            }
        }

        // The motors: the weapon's steady part, and whatever the game just
        // made happen.
        std::uint16_t heavy = g_state.rumble.heavy();
        std::uint16_t light = g_state.rumble.light();
        if (g_state.hold_heavy > heavy) {
            heavy = g_state.hold_heavy;
        }
        if (g_state.hold_light > light) {
            light = g_state.hold_light;
        }
        if (g_state.vibration) {
            SDL_RumbleGamepad(g_state.pad, heavy, light, 60);
        }

        // The light bar, written when its colour changes.
        update_light_bar(mod);
        if (g_state.red != g_state.last_red || g_state.green != g_state.last_green ||
            g_state.blue != g_state.last_blue) {
            if (!SDL_SetGamepadLED(g_state.pad, g_state.red, g_state.green, g_state.blue) &&
                !g_state.said_no_light) {
                g_state.said_no_light = true;
                mod.error(std::string("the light bar would not light: ") + SDL_GetError());
            }
            g_state.last_red = g_state.red;
            g_state.last_green = g_state.green;
            g_state.last_blue = g_state.blue;
        }
    });

    // The game's events, made into vibrations.
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
            if (record != 0 && record != g_state.last_hit) {
                g_state.last_hit = record;
                play("hit");
            }
            return record;
        });
    mod.hook<openrac::rac1::Moby_RecordDeath>(
        [](auto original, std::uint32_t moby, std::int32_t flags, std::int32_t index) {
            play("break");
            return original(moby, flags, index);
        });
    mod.hook<openrac::rac1::Item_Give>([](auto original, std::int32_t id, std::int32_t equip) {
        play("pickup");
        return original(id, equip);
    });
    mod.hook<openrac::rac1::GoldBolts_CountCollected>([](auto original) {
        const std::int32_t count = original();
        if (count < 0 || static_cast<std::uint32_t>(count) != g_state.last_gold_bolts) {
            g_state.last_gold_bolts = static_cast<std::uint32_t>(count);
            play("gold");
            flash(400);
        }
        return count;
    });
}

}  // namespace dualsense_mod

OPENRAC_MOD_CPP("dualsense", "1.0.0", dualsense_mod::load)

extern "C" OPENRAC_MOD_EXPORT void openrac_mod_unload(void) {
    if (g_state.pad != nullptr) {
        SDL_RumbleGamepad(g_state.pad, 0, 0, 0);
        SDL_SetGamepadLED(g_state.pad, 0, 0, 0);
        SDL_CloseGamepad(g_state.pad);
        g_state.pad = nullptr;
    }
    SDL_Quit();
}
