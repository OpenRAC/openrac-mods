# Mods for several games

One mod folder can serve Ratchet & Clank 1, 2, 3 and 4. A player installs the
same mod in each game it supports; each game loads the part written for it.
This note is the layout every multi-game mod here follows. Existing
single-game mods are unchanged: a mod for one game is the case of this layout
with one game in it.

## The manifest

`games` lists every game the mod supports, each with its discs (the
`versions` of OpenRAC's `games/<game>/game.json`):

```jsonc
"games": {
  "rac1": ["pal", "ntsc"],
  "rac2": ["ntsc", "pal", "ntsc2"]
}
```

- `hooks` lists the hook points of every game, **by permanent id**
  (`rac1.player.take_damage`, `rac2.player.take_damage`). An id names its game,
  so each game binds only its own and ignores the others'. Aliases
  (`Hero_TakeDamage`) are allowed only in a single-game mod, because two games
  may share a name.
- `settings` and `presets` are one list for all games. A setting a game cannot
  honour says `"games": ["rac1"]`: the launcher shows it only for those games,
  the port gives it its default elsewhere, and presets may still name it.
- `api` is the highest interface any of its games needs.
- Everything else (`id`, `version`, `multiplayer`, `requires`, `conflicts`,
  `load`) is the mod's, the same for every game. A party compares the mod by
  id, version and settings as before; players of different games never share a
  party.

## The folder

```
<mod>/
├── openrac-mod.json        one manifest, every game
├── CMakeLists.txt          openrac_add_mod() over every game's sources: one library
├── README.md               a section per game where they differ
├── listing.json
├── src/
│   ├── mod.c(pp)           openrac_mod_load: switches on api->game, calls <game>_load
│   ├── common/             what every game shares (settings, maths, presets, the log)
│   ├── rac1/               rac1's hooks, addresses and tables
│   └── rac2/               rac2's
└── tests/                  common tests plus per-game ones (tests/rac1, tests/rac2)
```

**One library per platform, all games in it.** `openrac_api.game` (and
`.version`) says which game loaded the mod; the entry point dispatches on it
and returns non-zero (with a log line) for a game it was not built for. No
per-game library, no change to `plugin`. A script mod does the same with
`openrac.game` in its first script (`if openrac.game == "rac2" then ...`).

Per-game code keeps that game's addresses, struct offsets and hook names in
its own folder; `common/` never holds an address. Each game's addresses come
from that game's decompilation and its SDK header, never guessed.

## Builds, releases and the index

Unchanged in form: the workflow builds each native mod once per platform, and
the release carries `<id>-<version>-<platform>.zip` (one library holding every
game), `<id>-<version>.zip` (all platforms) and `SHA256SUMS`. One release
serves every game. `tools/build_index.py` copies `games` into the index, so the
launcher lists the mod under each game it names and installs it into that
game's `plugins/`; the compatibility check is per game and disc, as before.

## The SDK's side, per game

Each game has its own hook table and header, named the same way:

| | rac1 | rac2 |
|---|---|---|
| hook rows | `port/game/rac1-pal/libraries.tsv` (and its other discs) | `port/game/rac2-ntsc/libraries.tsv` (the primary; `release_carry.py` writes `rac2-pal` and `rac2-ntsc2`) |
| ids and aliases | `sdk/bindings/rac1.json`, `rac1.ids.lock` | `sdk/bindings/rac2.json`, `rac2.ids.lock` |
| C header | `openrac/games/rac1.h` (`OPENRAC_RAC1_HOOK_<Name>`) | `openrac/games/rac2.h` (`OPENRAC_RAC2_HOOK_<Name>`) |
| ids | `rac1.<area>.<name>` | `rac2.<area>.<name>` |

A rac2 hook point that does the job of a rac1 one takes the same `<area>.<name>`
(`rac1.player.take_damage` and `rac2.player.take_damage`), so a mod's per-game
code reads alike. Signatures are each game's own. Common callbacks of
`openrac_api` (`on_tick`, `on_pad`, the HUD, menus, play) are the same calls in
every game, and answer `OPENRAC_HOOK_UNKNOWN` where a game lacks one.

**Adding rac2 hook points in parallel**: several branches add rows to the same
rac2 files. Append rows and table entries at the end of your area, rebase onto
develop before pushing, and regenerate (`sdk/tools/gen_binding.py`) after the
rebase rather than merging generated files by hand.
