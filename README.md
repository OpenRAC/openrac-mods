# Mods for Ratchet & Clank on OpenRAC

Mods for [OpenRAC](https://github.com/OpenRAC/OpenRAC)'s native PC port of
Ratchet & Clank (PAL and NTSC-U), installed and switched on or off in the
OpenRAC launcher's **Mods** tab. A game with no mods on plays as the
PlayStation 2 game does. They come in two kinds
([docs/port/MODS.md](https://github.com/OpenRAC/OpenRAC/blob/master/docs/port/MODS.md),
section 11):

- **native mods**: a small library the game loads at start, built with
  OpenRAC's mod SDK for each system. It is a program and runs with the game's
  rights; the launcher says *Runs code on this computer*;
- **script mods**: Luau files the game runs in a sandbox, the same files on
  every system and nothing to build. They reach only what the game exposes to
  scripts; the launcher says *Runs scripts (sandboxed)*.

| Mod | Kind | What it does | Discs |
|---|---|---|---|
| [lock-strafe](lock-strafe/README.md) | native | Strafe as in the later games: Up Your Arsenal's lock-strafe with weapons that fire ahead, Going Commando's strafe while L2 or R2 is held | PAL, NTSC-U |
| [button-swap](button-swap/README.md) | script | Swaps two buttons: Cross and Square, Circle and Triangle, or L1 and R1 | PAL, NTSC-U |
| [keyboard-mouse](keyboard-mouse/README.md) | native | Keyboard and mouse: WASD, mouse camera, click to fire, right click held for the Quick Select, number keys and wheel for weapons; every action rebindable in the launcher, with presets; a gamepad keeps working | PAL, NTSC-U |
| [item-randomizer](item-randomizer/README.md) | native | Shuffles the gadgets, weapons, vendor stock, gold bolts and infobots, with logic so every seed can be finished; seed codes to share, a spoiler log | PAL, NTSC-U |
| [difficulty](difficulty-mod/README.md) | native | A slider for everything: enemy count, damage both ways, enemy and Ratchet speed, weapon and ammo prices, ammo per shot, max ammo, bolt drops, health; presets Easy, Retail, Hard, Brutal and a seeded Chaos | PAL, NTSC-U |
| [live-splits](live-splits/README.md) | native | Splits a LiveSplit timer as the player goes from planet to planet: the mod talks to LiveSplit's own server and splits at every planet, so a speedrunner does not touch the timer | PAL, NTSC-U |
| [dualsense](dualsense/README.md) | native | A PlayStation 5 DualSense's own buttons as extra PlayStation 2 buttons (the touchpad, the microphone button, the DualSense Edge's paddles), its light bar showing Ratchet's health, a vibration when Ratchet is hit, hits something, breaks a crate, picks something up or finds a gold bolt, and a feel of its own for each weapon in the motors | PAL, NTSC-U |
| [ammo-max](ammo-max/README.md) | native | At the vendor, buying ammo starts at the most you can buy (the rounds the weapon has room for, or as many as your bolts pay for) instead of 1; Down still lowers it | PAL, NTSC-U |
| [xbox-icons](xbox-icons/README.md) | native | The game's PlayStation 2 button icons as an Xbox controller's, in the game's own style: A, B, X, Y on its dark buttons, LB, RB, LT, RT | PAL, NTSC-U |
| [multiplayer](multiplayer/README.md) | native | Online multiplayer, first step: a Multiplayer item in the main menu, under Load Game, whose pages are the game's own menus: it connects to an OpenRAC server, finds or creates a party (or joins one by its code) and shows the lobby with your launcher name. Nothing of the game is played together yet. Needs OpenRAC's mod interface 1.7 | PAL, NTSC-U |
| [archipelago](archipelago/README.md) | native | Plays in an [Archipelago](https://archipelago.gg/) multiworld: the gadgets, weapons, vendor slots, gold bolts and infobots are locations, what they hold may be another game's item, and what others find for you comes to you; beat Drek to finish. With `rac1.apworld` from the release. Needs OpenRAC's mod interface 1.9 | PAL, NTSC-U |

## Installing a mod

**From the launcher**: open **Mods**, choose **Add from a link**, and paste
`https://github.com/OpenRAC/openrac-mods`. The launcher lists the mods here with
what each is for and whether your game can load it; **Install** downloads a
native mod built for your computer, or a script mod, from the latest release
and checks it against the release's `SHA256SUMS`. It is listed for the game, on; choose its settings and
Play (settings count from the game's next start). The launcher offers updates
when a newer release comes out, unless you keep the version you have.

**By hand**: download the mod's `.zip` from this repository's
[Releases](https://github.com/OpenRAC/openrac-mods/releases) (one archive holds
the mod for Windows, Linux and macOS), then in the launcher's **Mods**, **Add
a mod**, and choose it.

A mod that cannot load (another game, a disc it does not know, an OpenRAC
whose mod interface is older than the mod's) says why on its row, and the game
starts without it. **Purist mode** in the launcher's Settings starts the game
with no mods at all. If the game stops with mods on, its crash report names
them: try again without them before reporting it to OpenRAC.

A native mod is a program: it runs with the game's rights. Install native mods
from sources you trust. A script mod runs in the game's sandbox; the game stops
one that errs or runs too long, and says which.

## Making your own

The [modding guides](https://github.com/OpenRAC/OpenRAC/blob/master/sdk/docs/README.md)
cover everything: [getting started](https://github.com/OpenRAC/OpenRAC/blob/master/sdk/docs/getting-started.md)
from the SDK's template to a mod players install by pasting your repository's
link, script mods in Luau and native mods in C or C++, settings, hooks and
their stable ids, and releasing with a tag. They also ship in the SDK's
archive, under `docs/`.

## Building

Script mods need no build. For the native ones you need CMake 3.20 or newer, a C and C++17 compiler (Clang, GCC or MSVC), Ninja
(optional) and Python 3. No copy of OpenRAC: the mods build against the
released [mod SDK](https://github.com/OpenRAC/OpenRAC/blob/master/sdk/README.md)
(the headers, the CMake package and the packaging script), the version and
SHA-256 that [sdk.lock](sdk.lock) pins, which `tools/fetch_sdk.py` downloads
into `.sdk/`.

```sh
git clone https://github.com/OpenRAC/openrac-mods.git
cd openrac-mods
python3 tools/fetch_sdk.py                                    # the pinned SDK, checked, into .sdk/
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build                                        # each mod's tests
python3 .sdk/openrac-mod-sdk-1.3.0/tools/package_mod.py build/package/* -o dist   # dist/<id>-<version>-<platform>.zip
python3 .sdk/openrac-mod-sdk-1.3.0/tools/package_mod.py button-swap -o dist        # dist/button-swap-1.0.0.zip, a script mod
```

To build against another SDK (a newer one, or `sdk/` in a checkout of OpenRAC
while the interface changes), pass `-DOPENRAC_SDK=<its folder>`. A new SDK
release is taken by changing `sdk.lock`'s version, url and sha256 (the
release's notes give the hash).

On Windows, run these in a "x64 Native Tools Command Prompt" of Visual
Studio (or leave out `-G Ninja` and add `--config Release` to the build
and `-C Release` to ctest). The SDK's
[README](https://github.com/OpenRAC/OpenRAC/blob/master/sdk/README.md) has
the details for each system, and
[docs/port/MODS.md](https://github.com/OpenRAC/OpenRAC/blob/master/docs/port/MODS.md)
how mods work: the interface, the manifest, the hook points, how the launcher
and the game load them.

To try a build without packaging it, add the mod's folder
(`build/package/<target>`) in the launcher's Mods tab.

The [build workflow](.github/workflows/build.yml), the SDK template's file
for file, builds and tests every native mod on macOS, Linux and Windows for
each change, packages each platform's build, joins them into one archive per
mod, packages each script mod as it is, and lists every archive's SHA-256 in
`SHA256SUMS`. Releasing is a tag:
`v<version>` publishes the release the launcher installs from, with
`<id>-<version>-<platform>.zip`, `<id>-<version>.zip` and `SHA256SUMS`.

## Adding a mod

A folder of its own, with its `openrac-mod.json`, its sources and a
`README.md`. A native mod also has a `CMakeLists.txt` that calls
`openrac_add_mod()` (copy the SDK's
[template](https://github.com/OpenRAC/OpenRAC/tree/master/sdk/template)), and
an `add_subdirectory()` line in the top `CMakeLists.txt`; a script mod is its
manifest and `.luau` files (copy
[template-script](https://github.com/OpenRAC/OpenRAC/tree/master/sdk/template-script)),
which the workflow finds by itself. Commits follow
[Conventional Commits](https://www.conventionalcommits.org/), with the mod's
id as the scope: `feat(lock-strafe): ...`.

What a mod may use of the game is what the port offers it: the hook points
of the SDK's `openrac/games/rac1.h` (and, for scripts, `types/rac1.d.luau`). One it needs and the port lacks is a
change to OpenRAC first (a `hook` row in the disc's `libraries.tsv`).

Nothing from the game's discs (code, data, text, pictures) goes into this
repository.

## License

The mods here are GPL-3.0-or-later, as OpenRAC's port ([LICENSE](LICENSE)).
The OpenRAC mod SDK they are built with is MIT, so mods elsewhere may choose
their own license; `tools/fetch_sdk.py` and `.github/workflows/build.yml` are
the SDK's files and keep its MIT notice.

## List your mod

Every mod here, and external mods listed in [`index/`](index), make up the
mod index: `index.json` on this repository's `index` pre-release, which the
OpenRAC website's mod pages and the launcher read. It is rebuilt every hour
by [`index.yml`](.github/workflows/index.yml) and signed.

- **A mod in this repository** is listed by itself from its releases. An
  optional `<folder>/listing.json` adds a short description, tags, impact,
  `aiAssisted` (`true` or `false`: whether AI tools helped make it, stated mod
  by mod and shown on its page), links, mods it recommends or suggests, and
  media (`icon`, `banner`,
  `screenshots: [{src, caption}]`, `videos: [{youtube, caption}]`, paths inside
  the folder; videos are YouTube links only).
- **A mod in your own repository** is a pull request adding
  `index/<id>.json`: the same fields plus `repo` and `path` (the mod's folder,
  `""` for the top). Your releases need `<id>-v<version>` (or `v<version>`)
  tags with the archives and `SHA256SUMS` the SDK's workflow makes; new
  releases are picked up by themselves.

- **Without git**: the [Submit a mod](https://github.com/OpenRAC/openrac-mods/issues/new?template=submit-mod.yml)
  issue form (or the wizard at https://openrac.dev/mods/submit). A bot checks
  it, takes the pictures you drag in, and opens the pull request; *Update a
  listing* and *Remove my mod* work the same way. You need write access to the
  mod's repository.
- A new version of a native mod from another repository reaches players 24 h
  after its release, once a maintainer has approved it on its review issue
  (`index/approved.json`).
- The bot is **openrac-bot**, a GitHub App of the organization, installed on this
  repository only. Its App id and private key are the repository secrets
  `OPENRAC_BOT_APP_ID` and `OPENRAC_BOT_PRIVATE_KEY`; each job asks it for a
  one-hour token with only the rights that job needs.
- If publishing fails, the bot opens an `index-broken` issue (and closes it once a
  publish works again). With the repository secret `DISCORD_WEBHOOK_MAINTAINERS`
  (a Discord channel webhook), both are also posted there.

Tags and impact come from fixed lists, in [`index/schema.json`](index/schema.json).
Check before you open the pull request:

```sh
python3 tools/build_index.py --check                     # the whole index
python3 tools/build_index.py --check-entry index/<id>.json
```

Every listed mod follows the [content rules](CONTENT.md).
