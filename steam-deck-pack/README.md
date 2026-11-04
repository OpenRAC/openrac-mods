# Steam Deck Pack (0.3.0, experimental)

A first native Steam Deck-oriented controller pack for OpenRAC's Ratchet & Clank 1 (PAL and
NTSC-U). It combines built-in Steam Deck button icons, an in-game controller settings page, Steam Input gyro/right-trackpad
mouse input, a 16:10 startup profile and a direct native start script for Steam's Game Mode.
It requires mod interface **1.9 or newer**. There are no game files in this pack.

## What works in this version

- **Button icons:** white A/B/X/Y and L1/L2/R1/R2 are part of this mod's own native library.
  No separate Xbox/controller icon mod is needed. Only recognized original game fonts are
  painted, once per font bank; unknown replacement fonts and ordinary text remain untouched.
  The font painter is shared source code linked statically into each mod, not a runtime plugin
  dependency. The original Xbox mod keeps its colored buttons and Xbox labels.
- **UI:** open **Pause → Options → Steam Deck**. The page contains only the gyro/trackpad
  switch, sensitivity (25–200%), vertical inversion and Reset. It follows all five PAL languages
  and uses the game's own button hints. There is no extra title-menu row, setup guide or display
  advice in the game. Steam Input setup remains in this README.
  On interface 1.11+, preferences live in the mod's own `controls.ini`
  (`<data home>/mod-data/steam-deck-pack`); on older interfaces they last for the session.
- **Gyro and right trackpad:** Steam Input turns either into mouse motion, and this mod maps it
  to the camera's right stick during play. Mouse movement stops affecting the camera on menus,
  loading screens, scenes, focus loss and OpenRAC's overlay. There is no residual turn on idle.
  A physical right-stick movement takes priority. Buttons and the left stick stay as they were.
- **Left trackpad:** configure it as a gamepad directional pad in Steam Input. The game already
  reads that pad in its menus; it works without an extra pointer layer.
- **16:10:** the offline setup tool creates independent balanced (1280×800 window target,
  100% render scale, 60 frame cap) and battery (75% scale, 40 cap, simpler graphics) profiles.
  Both select 16:10, borderless display and the centered HUD. Borderless uses the current display
  mode; set Steam's per-game resolution to 1280×800 on the Deck. Caps are starting settings,
  not measured performance or battery-life promises; set the matching refresh/cap in Steam too.
- **Direct Steam start:** the generated `start.sh` runs the native game with its existing data,
  levels, prepared mods and probes, plus the pack's separate settings and plugin list. No launcher
  process or Proton is required. The normal default memory cards are used.

This is mouse-to-stick aiming, subject to the game's stick turn-speed limit and native camera
invert settings. It is not raw sensor integration or unrestricted mouse camera aiming. Steam
Input combines gyro and trackpad before the mod sees them; tune their individual gains in Steam.
The pack does not implement flick stick, touch menus or enlarge every original game screen.

## Build

Use the SDK pinned by the repository and its normal build instructions. The top CMake project
builds this mod only when the SDK is at least 1.11. A **Linux x86-64** build is required on the
Deck; a macOS `.dylib` or Windows `.dll` cannot be used there.

```sh
python3 tools/fetch_sdk.py
cmake -S . -B build
cmake --build build --target steam_deck_pack steam_deck_logic_test steam_deck_load_test
ctest --test-dir build -R steam_deck --output-on-failure
```

The SDK packages the library, manifest, this README and `extras/tools/setup_deck.py` under
`build/package/steam_deck_pack`. The repository's existing release workflow builds the Linux
x86-64 version with the other native mods. A source checkout alone is not an installable mod.

## Set up on SteamOS

1. Use the OpenRAC launcher to install the **native Linux** RAC1 game and extract/check your own
   disc's levels once. Keep its game binary and data folder in permanent locations. If the
   launcher is an AppImage, extract it to a permanent AppDir and use the native game executable
   there; a path inside `/tmp/.mount_*` disappears when the AppImage exits and is refused.
2. Obtain/build the Linux x86-64 Steam Deck Pack package. Run its setup tool in Desktop Mode,
   adjusting the paths below. `--dry-run` validates without writing anything; omit it to create
   the new pack directory.

```sh
python3 extras/tools/setup_deck.py \
  --game '/path/to/openrac-rac1-pal' \
  --data '/home/deck/OpenRAC/active/rac1/data' \
  --package '/path/to/steam_deck_pack' \
  --output '/home/deck/Games/OpenRAC Deck' \
  --version pal --dry-run
```

3. In Steam, **Add a non-Steam game**, browse to the generated `start.sh` (show All Files), and
   name it **Ratchet & Clank 1**. Leave compatibility/Proton disabled. An optional `.desktop`
   shortcut is generated alongside it; no Steam configuration database is edited.
4. Open the shortcut's **Controller settings → Edit layout** and apply the mapping below.
   Steam Input requires this one-time setup; the pack does not import a profile automatically.
5. Start from Steam's Game Mode. Open **Pause → Options → Steam Deck** to tune the shared sensitivity.
   Switch to the battery profile using the shortcut's launch options:
   `OPENRAC_DECK_PRESET=battery %command%`. The default is `balanced`.

Existing launcher settings, saves and `plugins.json` are not changed. The tool copies preferences
and takes a snapshot of the mod list into the new pack, linking existing mod folders. After
changing the launcher installation/mod list, generate a new pack folder. Re-running onto an
existing pack is refused so custom changes cannot be overwritten. Removing only that generated
folder removes the shortcut/profile setup; existing game data and saves remain.

**Input conflict:** do not enable `keyboard-mouse` alongside this mod: both own the same mouse
stream. Setup refuses that combination unless `--disable-keyboard-mouse` is given; that flag
switches it off only in the pack's isolated list. The separate `xbox-icons` mod is switched off
in that list because its button painting is now included. Other mods, including Multiplayer, retain their enabled states.
The manifest declares both input and icon conflicts so the launcher can explain incompatible
combinations. When adding this mod by hand, disable `xbox-icons` and `keyboard-mouse` first.

## Steam Input layout

Start with **Gamepad with Mouse Trackpad** (or a gamepad template with the following edits).
Labels can vary with Steam client language/version.

| Control | Output |
|---|---|
| Gyro | Mouse / Gyro to Mouse; enable while right stick is touched |
| Right trackpad | Mouse; click → gamepad A |
| Left trackpad | Directional pad → gamepad D-pad |
| Sticks, ABXY, shoulders, triggers | Normal gamepad controls |
| L4 / R4 | Gamepad A (jump) / X (wrench) |
| L5 / R5 | Gamepad Y (Quick Select) / right bumper (crouch) |

`extras/profiles/steam-input.json` records this mapping for inspection; it is **not** a Steam-importable
VDF or an uploaded community layout. Valve documents keyboard/mouse/gamepad output in
[Legacy Mode Bindings](https://partner.steamgames.com/doc/features/steam_controller/legacy_mode)
and gyro/trackpad source modes in
[Input Source Modes](https://partner.steamgames.com/doc/features/steam_controller/input_source_modes).

## Validation and remaining device work

Tests cover the actual loaded native library (including PAL/NTSC generated-font painting without an Xbox mod), gamepad report boundaries, physical-stick priority,
UI editing/persistence, all five UI languages' byte limits, focus/pause/scene transitions, config
parsing, profile isolation, argument quoting and rejecting incompatible binaries. Hardware gyro
noise/feel, menu readability, Steam overlay/suspend behaviour and LCD/OLED performance need a
real Steam Deck playtest. No device performance or Steam Deck verification claim is made.

This pack and its translations are AI-assisted. GPL-3.0-or-later, as the repository's LICENSE.
