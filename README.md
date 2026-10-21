# Mods for Ratchet & Clank on OpenRAC

Mods for [OpenRAC](https://github.com/OpenRAC/OpenRAC)'s native PC port of
Ratchet & Clank (PAL and NTSC-U). Each is a small library the game loads at
start, built with OpenRAC's mod SDK, and installed and switched on or off in
the OpenRAC launcher's **Mods** tab. A game with no mods on plays as the
PlayStation 2 game does.

| Mod | What it does | Discs |
|---|---|---|
| [lock-strafe](lock-strafe/README.md) | Strafe as in the later games: Up Your Arsenal's lock-strafe with weapons that fire ahead, Going Commando's strafe while L2 or R2 is held | PAL, NTSC-U |

## Installing a mod

1. Download the mod's `.zip` from this repository's
   [Releases](https://github.com/OpenRAC/rac1-mods/releases) (or, for the
   latest build, from a run of the **build** workflow under Actions,
   "Artifacts"). One archive holds the mod for Windows, Linux and macOS.
2. In the OpenRAC launcher, open **Mods**, then **Add a mod**, and choose the
   `.zip`. The mod is listed for the game, off.
3. Switch it on, choose its settings, and Play. The settings count from the
   game's next start.

A mod that cannot load (another game, a disc it does not know, an OpenRAC
whose mod interface is older than the mod's) says why on its row, and the game
starts without it. **Purist mode** in the launcher's Settings starts the game
with no mods at all. If the game stops with mods on, its crash report names
them: try again without them before reporting it to OpenRAC.

A native mod is a program: it runs with the game's rights. Install mods from
sources you trust.

## Building

You need CMake 3.20 or newer, a C compiler (Clang, GCC or MSVC), Ninja
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
python3 .sdk/openrac-mod-sdk-1.0.1/tools/package_mod.py build/package/* -o dist   # dist/<id>-<version>-<platform>.zip
```

To build against another SDK (a newer one, or `sdk/` in a checkout of OpenRAC
while the interface changes), pass `-DOPENRAC_SDK=<its folder>`. A new SDK
release is taken by changing `sdk.lock`'s version, url and sha256 (the
release's notes give the hash).

On Windows, run these in a "x64 Native Tools Command Prompt" of Visual
Studio 2022 (or leave out `-G Ninja` and add `--config Release` to the build
and `-C Release` to ctest). The SDK's
[README](https://github.com/OpenRAC/OpenRAC/blob/master/sdk/README.md) has
the details for each system, and
[docs/port/MODS.md](https://github.com/OpenRAC/OpenRAC/blob/master/docs/port/MODS.md)
how mods work: the interface, the manifest, the hook points, how the launcher
and the game load them.

To try a build without packaging it, add the mod's folder
(`build/package/<target>`) in the launcher's Mods tab.

The [build workflow](.github/workflows/build.yml) builds and tests every mod
on macOS, Linux and Windows for each change, packages each platform's build
and joins them into one archive per mod. A tag `v*` puts those archives on a
draft release.

## Adding a mod

A folder of its own, with its `openrac-mod.json`, its sources, a `README.md`
and a `CMakeLists.txt` that calls `openrac_add_mod()` (copy the SDK's
[template](https://github.com/OpenRAC/OpenRAC/tree/master/sdk/template)),
and an `add_subdirectory()` line in the top `CMakeLists.txt`. Commits follow
[Conventional Commits](https://www.conventionalcommits.org/), with the mod's
id as the scope: `feat(lock-strafe): ...`.

What a mod may use of the game is what the port offers it: the hook points
of the SDK's `openrac/games/rac1.h`. One it needs and the port lacks is a
change to OpenRAC first (a `hook` row in the disc's `libraries.tsv`).

Nothing from the game's discs (code, data, text, pictures) goes into this
repository.

## License

GPL-3.0-or-later, as OpenRAC ([LICENSE](LICENSE)).
