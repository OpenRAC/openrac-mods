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
| [item-randomizer](item-randomizer/README.md) | native | Shuffles the gadgets, weapons, vendor stock, gold bolts and infobots, with logic so every seed can be finished; seed codes to share, a spoiler log | PAL, NTSC-U |

## Installing a mod

**From the launcher**: open **Mods**, choose **Add from a link**, and paste
`https://github.com/OpenRAC/rac1-mods`. The launcher lists the mods here with
what each is for and whether your game can load it; **Install** downloads a
native mod built for your computer, or a script mod, from the latest release
and checks it against the release's `SHA256SUMS`. It is listed for the game, on; choose its settings and
Play (settings count from the game's next start). The launcher offers updates
when a newer release comes out, unless you keep the version you have.

**By hand**: download the mod's `.zip` from this repository's
[Releases](https://github.com/OpenRAC/rac1-mods/releases) (one archive holds
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

## Building

Script mods need no build. For the native ones you need CMake 3.20 or newer, a C and C++17 compiler (Clang, GCC or MSVC), Ninja
(optional) and Python 3. No copy of OpenRAC: the mods build against the
released [mod SDK](https://github.com/OpenRAC/OpenRAC/blob/master/sdk/README.md)
(the headers, the CMake package and the packaging script), the version and
SHA-256 that [sdk.lock](sdk.lock) pins, which `tools/fetch_sdk.py` downloads
into `.sdk/`.

```sh
git clone https://github.com/OpenRAC/rac1-mods.git
cd rac1-mods
python3 tools/fetch_sdk.py                                    # the pinned SDK, checked, into .sdk/
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build                                        # each mod's tests
python3 .sdk/openrac-mod-sdk-1.0.3/tools/package_mod.py build/package/* -o dist   # dist/<id>-<version>-<platform>.zip
python3 .sdk/openrac-mod-sdk-1.0.3/tools/package_mod.py button-swap -o dist        # dist/button-swap-1.0.0.zip, a script mod
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

GPL-3.0-or-later, as OpenRAC ([LICENSE](LICENSE)).
