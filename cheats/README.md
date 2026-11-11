# Cheats

Cheats for Ratchet & Clank on OpenRAC's native port (PAL and NTSC-U discs), switched on and off
while you play: invincibility, infinite ammo, infinite oxygen under water and a bolt multiplier,
with buttons that refill your ammo and give you bolts.

With the mod off, or every cheat off, the game is the PlayStation 2 game.

## Playing

Install it from the launcher's **Mods** tab (**Add from a link**:
`https://github.com/OpenRAC/openrac-mods`), switch it on and play. In a level, open the pause
menu, then **Options**, then **Cheats**:

| Row | What it does |
|---|---|
| Invincibility | Off / On: hits take no health |
| Infinite ammo | Off / On: shots use no ammo; switched on, every weapon you own is filled first |
| Infinite oxygen | Off / On: Ratchet never runs out of air under water, as with the O2 Mask |
| Bolt multiplier | x1, x2, x3, x5, x10, x100: each bolt you pick up counts that many times |
| Refill ammo | Fills every weapon you own to its most |
| Give 10000 bolts | Adds 10,000 bolts |

The page and the launcher's settings stay in step (below, "In step with the launcher"): what you
switch on the page is saved as the mod's settings, so the launcher shows it and the next start
begins with it, and a cheat changed in the launcher while the game runs is taken within a second.
The settings:

| Setting | Default | |
|---|---|---|
| Invincibility | off | On at the start |
| Infinite ammo | off | On at the start |
| Infinite oxygen | off | On at the start |
| Bolt multiplier | x1 | The multiplier at the start |
| Show the cheats in the HUD | on | A small line at the bottom of the screen names the cheats that are on |
| Detailed log | off | For bug reports: a line in the game's log for each cheat switched and each refill |

Invincibility stops hits, not deaths that are not hits: a fall out of the level or a crusher
still ends Ratchet, as the game sets his health to 0 there without hitting him.

The pause menu's Options page has room for two items from all mods together (the
[multiplayer](../multiplayer/README.md) mod takes one). When two other mods have taken them, the
game's log says so, and the cheats stay as the settings say.

Cheats cannot be played in a multiplayer party (`"multiplayer": "solo"`).

## How it works

Each cheat is applied where the game takes the value, through one of OpenRAC's hook points
([docs/port/MODS.md](https://github.com/OpenRAC/OpenRAC/blob/master/docs/port/MODS.md), section
4) where there is one; with a cheat off its hook calls the game's own function as it was called,
and nothing is written.

| Cheat | Hook point | |
|---|---|---|
| Invincibility | `rac1.player.take_damage` (Hero_TakeDamage) | A hit that would take health is not passed on |
| Infinite ammo | `rac1.weapon.take_ammo` (Weapon_TakeAmmo) | A shot that would take rounds is allowed and takes none |
| Bolt multiplier | `rac1.moby.collect_bolt` (Moby_CollectBolt) | The game counts the bolt, then the mod adds the rest of the multiple (`openrac_api.bolts`) |
| Infinite oxygen | none | After each tick the hero's air (`underwater_air`, a full breath 10000) is put back to full, so the breath the tick took never brings it to 0 |

There is no hook point on the air: each tick Ratchet swims without the O2 Mask, the game
(try_transition_hero_water_state) takes a breath off it and sends him up when it reaches 0. A
hook point there would let the mod do without writing it each tick.

The refill writes each owned weapon's ammo (`weapon_ammo_counts`) to the max of the items' table
(the one the game's ammo pickups and vendor read), so it follows the
[difficulty](../difficulty-mod/README.md) mod's max ammo. The page is a page of the game's own
menus (`ui_page`, `ui_pause_item`, interface 1.9), and the HUD's line is `hud_text_set`
(interface 1.12), shown again after each level load.

### The page and the stick

The game turns the left stick into the pad's directions once it leans past about a third of the
way (its pad reading), and the page's rows step their value on Left and Right; OpenRAC's port then
steps the row forward, and chooses a button, for any of Cross, Left and Right. So the stick pushed
down onto a row, a little off straight, would switch it. The mod reads the first pad's reports
(`on_pad`, leaving them as they are) and checks each of the page's events against what the
player does (`src/common/pad_intent.cpp`):

- the stick pushed more up or down than sideways: the cursor moving, nothing changes and the page
  shows the cheat as it is;
- Left (the d-pad, or the stick pushed more left than up or down) steps back, Right and Cross
  forward;
- a button is chosen by Cross only, or by a press the pad does not show (a mouse click, a touch).

### In step with the launcher

The launcher keeps each mod's settings in the game's list of mods,
`<data>/plugins/plugins.json`, which the port reads when the game starts. OpenRAC's interface has
no call yet for a mod to change its own settings (docs/port/MODS.md, section 12, "settings in the
game"), so the mod edits that file itself, the way the launcher does
(`src/common/settings_file.cpp`):

- it finds the list in the folder the port names (`plugins_folder`, interface 1.14), else beside
  its own folder: the library is `<plugins>/cheats/lib/<platform>/...`, and the list is used only
  when that folder holds the mod's manifest and the list is there. A library loaded from
  elsewhere (a build folder) keeps its cheats for the run, and the log says so;
- a cheat switched on the page writes the four cheats' settings (`invincible`, `infinite_ammo`,
  `infinite_air`, `bolt_multiplier`) into the mod's entry, every other mod and setting kept, as a
  whole file written beside the list and renamed over it;
- about twice a second it looks at the list's time; when it changed and holds other cheats (the
  launcher's Mods tab), it takes them, the page and the HUD's line following.

The launcher reads the list again when its Mods tab is opened: a cheat switched in the game shows
there from then on.

The addresses (`src/rac1/discs.h`) are the ones the difficulty, ammo-max and item randomizer mods
already use, from the decompilation.

## Building

As every native mod here (the repository's [README](../README.md#building)). The tests:

- `cheats_cheats`: the shared part alone (the multiplier's steps, the bolts added, the HUD's line);
- `cheats_settings_file`: the launcher's list read and written (every other mod and setting kept),
  followed on disk, and found beside the mod's folder;
- `cheats_rac1_load`: the library as the port loads it, against a host table and game memory of
  the test's own, on both discs (the hooks, the page's rows and buttons, the air, the HUD's line).

## Layout

One library for every game it will support ([docs/MULTI_GAME.md](../docs/MULTI_GAME.md)):
`src/mod.cpp` hands the load to `src/rac1`, over `src/common`. Going Commando has the same
`take_damage` and `take_ammo` hook points (`rac2.player.take_damage`, `rac2.weapon.take_ammo`),
so its part is a `src/rac2` of its own.
