# DualSense

Gives a PlayStation 5 DualSense what the PlayStation 2 never had, in OpenRAC's
native port of Ratchet & Clank (PAL and NTSC-U).

- **The controller's extra buttons become extra PlayStation 2 buttons.** The
  touchpad's click, the microphone button and the DualSense Edge's two paddles
  are buttons the port's own table leaves out. Each is bound to a PlayStation 2
  button of the player's choosing.
- **The light bar shows Ratchet's health**: green while he is well, red when he
  is nearly out, a white flash when a gold bolt is found.
- **The controller vibrates** when Ratchet is hit, hits something, breaks a
  crate, picks something up or finds a gold bolt.

The mod refuses to load when no DualSense is connected, and says so.

## Playing

1. Install the mod from the launcher's **Mods** tab, switch it on, choose its
   settings and start the game.

   **Install the archive, not this folder.** A native mod's library is named
   by its manifest, and only the built manifest names it: the source folder
   here has no `plugin` yet, because the library is not built until the
   packaging. The port refuses a manifest with no library
   ("it has no library: data mods are packs"). Use either of:

   - the archive, `dist/dualsense-1.0.0-linux-x86_64.zip`;
   - the built folder, `build/package/dualsense/`;
   - or **Add from a link** with `https://github.com/OpenRAC/rac1-mods`, which
     takes the release's archive.
2. Connect a DualSense (a DualSense Edge works too) **before** the game
   starts. The mod looks for the controller when it loads and refuses without
   one.
3. On Linux the controller's raw device needs to be readable. If the port
   cannot see the controller at all, add a rule for it and reload the udev
   rules:

   ```
   # /etc/udev/rules.d/70-dualsense.rules
   KERNEL=="hidraw*", ATTRS{idVendor}=="054c", ATTRS{idProduct}=="0ce6", MODE="0660", TAG+="uaccess"
   KERNEL=="hidraw*", ATTRS{idVendor}=="054c", ATTRS{idProduct}=="0df2", MODE="0660", TAG+="uaccess"
   ```

   ```
   sudo udevadm control --reload-rules && sudo udevadm trigger
   ```

## The settings

| Setting | Default | |
|---|---|---|
| Touchpad click | `circle` | What the touchpad's click does. **Circle** by default: the gadget fires, as it does with the button |
| Left paddle | `triangle` | What the DualSense Edge's left paddle does. **Triangle** by default: the Quick Select opens without lifting the thumb off the stick |
| Right paddle | `square` | What the DualSense Edge's right paddle does. **Square** by default: the wrench swings, and the hoverboard boosts |
| Microphone button | `none` | What the microphone button does. **Nothing** by default: L1 and R1 are read by the game as part of the free-look and crouch chords, so they are left alone |
| Vibration | on | Whether the controller vibrates |
| Vibration strength | `100` | How hard, in percent |
| Light bar | on | Whether the light bar shows the health |

Every button a setting can name: `none`, `cross`, `square`, `circle`,
`triangle`, `l1`, `r1`, `l2`, `r2`, `select`, `start`.

A binding **adds** to the button it names; it does not replace it. The
touchpad is a second Circle, not the only one.

## The weapons' feels

Each weapon has its own feel, and the mod gives it through the controller's
motors. The game itself asks for no vibration at all — the retail program links
no motor command — so all of this is the mod's own.

| Weapon | The feel |
|---|---|
| **Walloper, Devastator** | a heavy click: a stiff wall that snaps, then the shot |
| **Bomb Glove, Visibomb Gun, Mine Glove, Glove of Doom, Decoy Glove** | a lighter click |
| **Blaster** | a light rumble while it fires, and a kick with every shot |
| **R.Y.N.O.** | a heavy buzz through the whole seven-missile salvo, and a full rumble with every shot |
| **Pyrocitor, Tesla Claw, Morph-o-Ray** | a hum while it fires |
| **Suck Cannon, Taunter, Drone Device** | a steady low rumble while it fires |

The moments are the game's own, not the button's. The mod reads the hero's
fire-animation flag — set and cleared by nothing but the weapon code
(`games/rac1/src/overlays/shared/help_0020CDF0.c:718` and `:757`) — so the
buzz runs exactly while the weapon is firing, and a kick lands on each shot
rather than on each press. The two weapons whose bodies are still assembly on
the PAL disc, the Visibomb Gun and the Taunter, are seen through the fire
button instead, and the Walloper through its own hero state.

**The controller's right trigger is not used.** Ratchet & Clank 1 fires with
Circle, so the right trigger is free, and the mod leaves it alone: a pull
there would disturb nothing the game reads.

## Why the vibration is the mod's own

Ratchet & Clank 1 asks for no vibration at all. The retail program links no
motor command: `sceVibGetProfile` is there, but the motor command beside it was
dead-stripped by the linker
(`games/rac1/src/core/00125218.c`: *"sceVibSetActParam was dead-stripped"*).
The game keeps a waveform mixer that would have driven it
(`games/rac1/src/game/actuator.c`), but nothing calls it, and the in-game debug
menu still has an "Actuator" row that switches nothing on. The feature was
shelved before the game shipped.

So the port has nothing to restore, and the vibration here is made out of what
the game does:

| What happened | Vibration |
|---|---|
| Ratchet was hit | a crack from the light motor, then a heavier thump under it |
| Ratchet hit something | a short thump |
| A crate broke, or a moby died | a soft thump |
| Ratchet picked something up | a short blip |
| Ratchet found a gold bolt | two blips, the second stronger |

## How it is put together

| File | |
|---|---|
| [src/dualsense.cpp](src/dualsense.cpp) | The mod: it finds the controller, takes the hooks, and writes the light bar and the vibration |
| [src/ps2_pad.cpp](src/ps2_pad.cpp), [src/ps2_pad.hpp](src/ps2_pad.hpp) | The PlayStation 2's buttons and the report the game reads |
| [src/rumble.cpp](src/rumble.cpp), [src/rumble.hpp](src/rumble.hpp) | The vibration: the envelope and the strengths the motors take |
| [src/weapons.cpp](src/weapons.cpp), [src/weapons.hpp](src/weapons.hpp) | The weapons' feels, and what the game is doing with them |

The controller is reached through **SDL3**, which the port already uses for
the pad. The mod links its own copy of SDL3, so it works with a port that has
SDL3 built in (the `standalone` preset, what the launcher ships) and with one
that uses the system's.

The extra buttons are written into the report the game reads, through the
port's pad report hook (`rac1.pad.process_report`). The report is **active
low** — a button that is held reads 0 — so the mod clears the bit of the
button it stands for.

## Building

The mod is built with the rest of this repository:

```sh
python3 tools/fetch_sdk.py          # the SDK sdk.lock pins, into .sdk/
cmake -S . -B build -G Ninja
cmake --build build && ctest --test-dir build
```

It brings its own copy of **SDL3, built static**, so that it works with a port
that has SDL3 built in (the `standalone` preset, what the launcher ships) and
with one that uses the system's. SDL3 is fetched the first time, which takes a
few minutes; after that the build is quick.

For a quicker build, at the cost of the mod needing `libSDL3` on the player's
machine:

```sh
cmake -S . -B build -G Ninja -DOPENRAC_DUALSENSE_SHARED_SDL=ON
```

## Tests

```sh
ctest --test-dir build -R dualsense
```

- **logic**: the vibration's envelope and the strengths it gives the motors, the
  buttons and the report they are written into;
- **weapons**: the weapons' feels, and the rule that says whether a weapon is
  firing and whether a shot just went off;
- **load**: the library as the port loads it, against a host table of the
  test's own: it reports its id and interface, refuses another game and
  another disc, takes the hooks, and refuses with a reason when no DualSense
  is connected.

Two more, for a machine that has a controller attached:

```sh
./build/dualsense/dualsense_probe        # what the controller reports
./build/dualsense/dualsense_watch       # the extra buttons, as they are pressed
./build/dualsense/dualsense_two_instances <the library>   # two copies of SDL3, one controller
```

## What was checked on a controller

On a **DualSense Wireless Controller** (USB, `054c:0ce6`, firmware 1584):

| | |
|---|---|
| SDL3 sees it | type **PS5**, vendor `054c`, product `0ce6` |
| The touchpad's click | there, and SDL reads it |
| The microphone button | there, and SDL reads it |
| The Edge's paddles | not on this controller, as expected: it is not an Edge. The binding then never fires, and nothing else changes |
| The light bar | the controller takes the colour (`SDL_SetGamepadLED` succeeds) |
| The motors | the controller takes the vibration (`SDL_RumbleGamepad` succeeds) |
| Two copies of SDL3 | the mod's own copy finds the controller while the port's copy holds it open, and the port's copy still works afterwards |

The button *presses* were not exercised: the machine the mod was written on had
no one at the controller. The wiring that carries them — the report the mod
writes into, and the bit each button is read from — is covered by the logic
test.

## What it does not do

- **No gyroscope.** Ratchet & Clank 1 has no use for one, and the
  `keyboard-mouse` mod already turns the mouse into the camera.
- **No adaptive triggers.** They would restore nothing the PlayStation 2 had:
  Ratchet & Clank 1's only analog button input is the pressure of Cross and
  Square while Ratchet is underwater, and a DualSense's face buttons are
  digital. The trigger's feel would have to be written by hand into the
  controller's output report, which SDL does not expose. The weapons' feels
  are given through the motors instead, which is what the PlayStation 2 had.
  See [docs/port/CONTROLLERS.md](https://github.com/OpenRAC/OpenRAC/blob/master/docs/port/CONTROLLERS.md).
- **No HD haptics.** Those are a separate USB audio stream, not a HID report,
  and SDL has no API for them.
- **No landing thump.** It would need the hero's state, and the game asks for
  no vibration to begin with; the events above carry it.
