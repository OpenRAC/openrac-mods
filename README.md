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
| [lock-strafe](lock-strafe/README.md) | native | Strafe as in Going Commando: L2 or R2 held, or one of them alone (or always, with a weapon that fires ahead, as Up Your Arsenal's lock-strafe), Ratchet facing the camera, his hips turning toward where he goes, and side and back flips on X | PAL, NTSC-U |
| [button-swap](button-swap/README.md) | script | Swaps two buttons: Cross and Square, Circle and Triangle, or L1 and R1 | PAL, NTSC-U |
| [keyboard-mouse](keyboard-mouse/README.md) | native | Keyboard and mouse: WASD, mouse camera, click to fire, right click held for the Quick Select, number keys and wheel for weapons; every action rebindable in the launcher, with presets; a gamepad keeps working | PAL, NTSC-U |
| [item-randomizer](item-randomizer/README.md) | native | Shuffles the gadgets, weapons, vendor stock, gold bolts and infobots, with logic so every seed can be finished; seed codes to share, a spoiler log | PAL, NTSC-U |
| [difficulty](difficulty-mod/README.md) | native | A slider for everything: enemy count, damage both ways, enemy and Ratchet speed, weapon and ammo prices, ammo per shot, max ammo, bolt drops, health; presets Easy, Retail, Hard, Brutal and a seeded Chaos | PAL, NTSC-U; Going Commando: NTSC-U, PAL, NTSC-U v2.00 (no enemy count, enemy speed or health pickups there) |
| [live-splits](live-splits/README.md) | native | Splits a LiveSplit timer as the player goes from planet to planet: the mod talks to LiveSplit's own server and splits at every planet, so a speedrunner does not touch the timer | PAL, NTSC-U |
| [dualsense](dualsense/README.md) | native | A PlayStation 5 DualSense's own buttons as extra PlayStation 2 buttons (the touchpad, the microphone button, the DualSense Edge's paddles), its light bar showing Ratchet's health, a vibration when Ratchet is hit, hits something, breaks a crate, picks something up or finds a gold bolt, and a feel of its own for each weapon in the motors | PAL, NTSC-U |
| [ammo-max](ammo-max/README.md) | native | Starts individual RAC1 ammo purchases at the maximum affordable amount; Square / Xbox X also refills ammunition across owned weapons in one action | RAC1 PAL, NTSC-U; RAC2 NTSC-U, PAL, NTSC-U v2.00 |
| [xbox-icons](xbox-icons/README.md) | native | The game's PlayStation 2 button icons as an Xbox controller's, in the game's own style: A, B, X, Y on its dark buttons, LB, RB, LT, RT; in Ratchet & Clank and Going Commando | PAL, NTSC-U; Going Commando: NTSC-U, PAL, NTSC-U v2.00 |
| [multiplayer](multiplayer/README.md) | native | Online co-op for up to 4 players, through an OpenRAC server or Direct connect: a Multiplayer item in the main menu, whose pages are the game's own menus. Everyone plays in the host's level and shares its world, progress and co-op saves; a dropped player keeps its place, and another player takes over from a host that goes. Players on different discs of the game play together. Team tools add co-op rescue and host rally points, and the pages speak the game's five PAL languages. Needs OpenRAC's mod interface 1.14 | PAL, NTSC-U; Going Commando: NTSC-U, PAL, NTSC-U v2.00 (a first co-op chosen in the settings: the host's level, the others seen) |
| [cheats](cheats/README.md) | native | Invincibility, infinite ammo, infinite oxygen under water and a bolt multiplier (x2 to x100), switched from Cheats in the pause menu's Options or from OpenRAC's own menu, with buttons that refill every weapon's ammo and give 10,000 bolts; a HUD line says what is on | PAL, NTSC-U |
| [archipelago](archipelago/README.md) | native | Plays in an [Archipelago](https://archipelago.gg/) multiworld: the gadgets, weapons, vendor slots, gold bolts and infobots are locations, what they hold may be another game's item, and what others find for you comes to you; beat Drek to finish. With `rac1.apworld` from the release. Needs OpenRAC's mod interface 1.9 | PAL, NTSC-U |
| [NG++](ng-plus-plus/README.md) | native | New Game++: Alpha to Omega: eight more tiers for every Going Commando weapon past its last version, bought then levelled with experience, each hitting harder, holding more ammo and with its own tint; an upgrade path for the RYNO II; enemies tougher with each challenge mode; bigger bolt drops, part of them as platinum bolts worth five times the gold ones | Going Commando: NTSC-U, PAL, NTSC-U v2.00 |

## Installing a mod

**From the launcher**: open **Mods**, choose **Add from a link**, and paste
`https://github.com/OpenRAC/openrac-mods`. The launcher lists the mods here with
what each is for and whether your game can load it; **Install** downloads a
native mod built for your computer, or a script mod, from the latest release
and checks its SHA-256 (GitHub's digest, and the release's `SHA256SUMS` if any). It is listed for the game, on; choose its settings and
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

A native mod's code follows OpenRAC's
[coding conventions](https://github.com/OpenRAC/OpenRAC/blob/develop/port/docs/CODING_CONVENTIONS.md)
(layout by the port's `.clang-format`, comments and documentation, names,
ownership, errors, file-local code in an anonymous namespace, state in
objects), adapted to a mod:

- C++17, through the SDK's `openrac/mod.hpp`, where the port's rules ask for
  C++20 (`std::span` and `std::format` are not there; use views, pointers and
  sizes, and `std::string` or `std::to_string`).
- The game is reached only through the SDK's C ABI (`openrac/openrac_mod.h`)
  and its hook points; no exception leaves a hook or a callback (the SDK's
  layer catches them).
- A mod keeps what it saves in its own data folder (`data_dir`), never in the
  memory card or the game's files.
- No game data in a mod: no code, text or bytes of the discs, in sources,
  tests or logs.

[ng-plus-plus](ng-plus-plus/README.md) is laid out this way.

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
  tags with the archives the SDK's workflow makes (`SHA256SUMS` is optional when
  GitHub gives each archive a digest); new
  releases are picked up by themselves.
- **A modpack** is `index/<id>.json` with `type: pack`, `name`, `author`,
  `description` and `versions: [{version, released, requires}]`: it installs
  the mods its `requires` names. A `requires` item may give that mod's settings
  values, by key: `{"id": "rac1-enhanced-movement", "settings": {"camera":
  "rac2"}}`. The index keeps only the keys the mod's manifest declares, with
  values it takes. The launcher sets them when the pack is installed or
  updated, never over a value the player chose. A change of mods or settings is
  a new pack version.

- **Without git**: the [Submit a mod](https://github.com/OpenRAC/openrac-mods/issues/new?template=submit-mod.yml)
  issue form. A bot checks it, takes the pictures you add, and opens the pull
  request; a maintainer merges it. *Remove my mod* works the same way. You need
  write access to the mod's repository. At most 3 open submissions per account;
  ids and names with "OpenRAC" are only for the mods of this repository.
- **Your listing lives in your repo**: `openrac-listing.json` next to your
  `openrac-mod.json` takes the same fields as `listing.json` (not `id` or
  `featured`). It is read every hour and wins over `index/<id>.json`.
  `openrac-revoked.json` (`[{"versions": ["1.2.0"], "reason": "..."}]`) pulls
  versions: launchers turn them off.
- **New versions publish by themselves**: every hour, 1 h after the release, once
  the checks pass. Nobody reviews each version.

### Labels

Every mod carries one label (`trust` in `index.json`), the first that applies:

| Label | When |
|---|---|
| OpenRAC | the mod's folder is in this repository (its code came in by reviewed PRs) |
| Verified | its id is in `index/verified.json` `mods`, or its GitHub owner in `owners`: a maintainer looked at it when it was added |
| Trusted author | its GitHub owner is in `index/trusted.json`: not checked by a person |
| Not checked | none of these |

The lists change only by a pull request a maintainer merges; removing a line
takes the label back. An owner may carry its numeric GitHub `id`, which keeps
the label when the account is renamed. A trusted or verified author's listing
pull request merges by itself once its checks pass.

### The scan

`tools/binary_scan.py` reads every new native version's libraries (import
tables and strings; nothing is run):

- **Labels** (`scan.labels` on the version, `scan.new` for what the version before
  could not do): Network, Starts programs, Loads code, Executable memory, Clipboard.
- **Held for review**: reads saved passwords or keys, reaches other processes,
  watches the keyboard or screen, changes system settings, telling strings
  (`cmd.exe`, PowerShell, browser password files, Discord or Telegram API,
  `.onion`, wallets, `.ssh`, mining pools), or a file that cannot be read. The
  version is not published, the mod gets `held` (the site and the launcher hide
  it; players keep the version they have) and the bot opens a `held` issue. A
  maintainer labels it `cleared` (the findings go into `index/cleared.json`;
  later versions with the same findings pass) or `revoke`.

`built` on the newest native version says `ci` when GitHub's build
attestation shows a workflow of the mod's repo built it from its tag, else
`hand`. It is checked every hour.

### What is published

On the `index` pre-release: `index.json` (signed), `details-<id>.json` (readme
and changelogs, each named with its SHA-256 in `index.json`), `picks.json`
(the weekly picks: three mods per game, Monday 00:00 UTC, OpenRAC, Verified or
Trusted only, at most one per author and one OpenRAC mod, none picked in the
last 4 weeks; `index/picks-pin.json` pins one until a date), `held.json` and,
once launchers read it, `community.json` (signed): new Not checked mods, which
older launchers never see.

`index/policy.json` turns each part on or off without a revert:

| Key | Default | |
|---|---|---|
| `review` | `"scan"` | `"approve"`: external native versions wait for `approved` on a review issue (`index/approved.json`) |
| `community` | `false` | new Not checked mods go to `community.json` |
| `selfMerge` | `true` | a trusted author's listing PR merges by itself |
| `slimIndex` | `false` | readme and changelogs only in `details-<id>.json` |
| `picks` | `true` | the weekly picks |
| `provenanceRequired` | `false` | an external native version must be built by CI |

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
