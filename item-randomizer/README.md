# Item randomizer

Shuffles what Ratchet & Clank gives out, for OpenRAC's native port (PAL and
NTSC-U discs): the gadgets and weapons NPCs and pickups give, the weapons
the Gadgetron vendor sells, the gold bolts and the planets' coordinates the
infobots carry. Each seed is a new route through the game, and every seed
can be finished: a gadget is only ever placed where you can get it without
that gadget. With the mod off, the game is the PlayStation 2 game.

## Playing

Install it from the launcher's **Mods** tab (**Add from a link**:
`https://github.com/OpenRAC/rac1-mods`), switch it on, choose its settings
and start a **new game**.

| Setting | Default | |
|---|---|---|
| Seed | blank | A **seed code** someone shared (`2ZRJ-6HB7-H6P7`: it carries their settings, so you play their game), or any word (`ratchet`: the same word with the same settings gives the same game). Blank: a random seed, rolled the first time and kept for your next starts; to roll another, type any word, or delete `seed.txt` in the data folder below |
| Logic | Standard | **Standard**: each gadget is placed where you can reach it with what you have by then, counting what each place uses. **Relaxed**: the same, with the gadgets early in the game, and never in a gold bolt or an expensive purchase. **Expert**: only what a place cannot be done at all without counts (you know the levels) |
| Shuffle gadgets | on | The Heli-Pack, the Swingshot, the Magneboots, the Grindboots, ... from NPCs and pickups |
| Shuffle weapons | on | The Suck Cannon, the Morph-o-Ray, the R.Y.N.O. |
| Shuffle the vendor | on | Each weapon the vendor sells is a slot: buying it, at its price, gives what the seed put there |
| Shuffle gold bolts | on | A gold bolt's place may hold a gadget, and the gold bolts may be anywhere. The pause screen counts the gold bolts you have found, wherever they were |
| Shuffle infobots | on | The planets' coordinates the infobots give join the pool |
| Keep the starting gear | on | Start with the Bomb Glove, as the game does. Off: what Veldin gives at the start is shuffled too |
| Spoiler log | on | Writes where everything is, and a way through, to the data folder |

The game's log names the seed's code at each start (`seed 2ZRJ-6HB7-H6P7
(new), standard, on the pal disc`): share that code. All the settings but
the spoiler log are in it.

**The data folder** holds `seed.txt` (the seed a blank setting rolled) and
`spoiler-<code>.txt`, one per seed. It is in the launcher's per-user data
folder, never beside the game:

| System | Folder |
|---|---|
| macOS | `~/Library/Application Support/dev.openrac.launcher/mods/item-randomizer` |
| Linux | `~/.local/share/dev.openrac.launcher/mods/item-randomizer` (or under `$XDG_DATA_HOME`) |
| Windows | `%LOCALAPPDATA%\dev.openrac.launcher\mods\item-randomizer` |

(`OPENRAC_MOD_DATA_DIR` in the environment names another.)

**Saves.** The mod writes nothing of its own into a save: what you have found
is the game's own bytes, and where everything is comes from the seed. So a
save made with the mod **belongs to its seed**: load it with the same seed
code and settings. Loaded with another seed it still loads, but what is left
to find follows the other seed's placement. With the mod off, a save made
with it loads as a save of the game, with the items you had.

**What stays as the game has it.** The OmniWrench; the Persuader on Pokitaru
(its NPC writes the item without the game's item routine); the planets'
coordinates the NPCs give (the plumber on Novalis, Agnogg's ship, the
bouncer, the commando and the deserter, Sam, the scientist, ...): only the
infobots' are shuffled. Gold weapons, ammo and bolts are the game's.

**What you will notice.** A vendor slot shows the weapon it stands for, at
that weapon's price, and gives the seed's reward: it goes into your
inventory and quick select as if the game had sold it, and stays when you
switch weapons. Once bought, the slot becomes ammo for the reward, if the
reward takes ammo. Ammo for a weapon you own whose slot is still for sale is
sold too. An NPC or pickup plays its own scene and then gives the seed's
reward, with that reward's banner. An infobot shows its own planet's video.
A gold bolt's place shows the gold bolt's scene, then the reward.

## How it works

For modders and the curious. The parts:

| File | |
|---|---|
| [src/logic.c](src/logic.c) | the locations: each NPC, pickup, vendor slot, infobot and gold bolt, its planet, what it gives in the game, the order the game's route reaches it in, and what it needs (`hard`, `soft`) |
| [src/seed.c](src/seed.c) | the seed code and the random numbers |
| [src/solver.c](src/solver.c) | the placement (an assumed fill), the playthrough check and the spoiler log |
| [src/config.c](src/config.c) | the settings into the seed's settings |
| [src/item_randomizer.c](src/item_randomizer.c) | the mod: the hooks |
| [src/discs.h](src/discs.h) | each disc's addresses and names |

**The locations come from the decompilation.** Every call of the game's
`GiveItem` (PAL `func_L00_002618D8`) and `UnlockPlanet`
(`func_L00_00261848`) in OpenRAC's `games/rac1`, the function around it and
the test it makes before it gives; the gold bolt's update; the vendor's
stock (`VendorStockLevelInit`, its table of one new weapon per planet).
Which planet each is on, the infobots' planets and trigger slots and the
gold bolts' slots were read from the PAL disc's level data with OpenRAC's
level readers. What each place needs (`hard`: it cannot be done without;
`soft`: play there expects it) is how the levels are played, written
conservatively; it is the one table to change when a seed turns out to ask
too much or too little, and the tests check every setting again.

**What a location's "had" state is.** The game keeps it, in its save:

| Location | The game's byte |
|---|---|
| an NPC, a pickup, a vendor slot, the starting Bomb Glove | the item's *obtained* byte (`D_0013D5F0[id]`): `GiveItem` sets it, and most pickups test it before they appear |
| an infobot | its trigger slot (`D_0014C150[mission + level * 16]`), which the game sets right after the infobot's `UnlockPlanet` |
| a gold bolt | its byte (`D_0014BFC0[level * 4 + slot]`) |

The items the player *owns* are the other array (`D_0013D5C8`), which the
hero, the menus and the vendor read. The mod gives a reward's item through
the game's own `GiveItem` (its banner, its starting ammo, the quick select)
and then puts that item's obtained byte back: it is that item's own
location's.

**The hooks** (OpenRAC's hook points, by name and permanent id; the first
five were added to OpenRAC for this mod, the moby updates are the behaviour
catalogue's):

| Hook point | Id | PAL / NTSC-U | What the mod does |
|---|---|---|---|
| `Item_Give` | `rac1.item.give` | `func_L00_002618D8` / `FUN_L00_00260860` | the location of the item the game gives gives its reward; the location is marked had. After a purchase the hero holds the reward (or, for a reward with no store demo, the weapon held before), never the slot's own weapon |
| `Planet_Unlock` | `rac1.planet.unlock` | `func_L00_00261848` / `FUN_L00_002607d0` | inside an infobot's update: the infobot's reward instead of its planet. Elsewhere (NPCs, the planet arrived at) the game's |
| `Vendor_InitLevelStock` | `rac1.vendor.init_level_stock` | `func_L00_0023DB30` / `FUN_L00_0023d198` | a level's start: the mod takes the had states as they are (a save just loaded); with the starting gear shuffled, the Bomb Glove is taken back out of the stock and the double-tap weapon when it was not the reward |
| `Vendor_BuildItemList` | `rac1.vendor.build_item_list` | `func_L00_0029B770` / `FUN_L00_0029a400` | the stock as the menu sees it: slots not bought for sale, bought ones as their reward's ammo; the cursor stays on the list and the item is drawn again |
| `GoldBolts_CountCollected` | `rac1.gold_bolt.count_collected` | `func_002160E0` / `FUN_00215290` | with gold bolts shuffled: the gold bolt rewards had |
| `Moby_Infobot_Update` | `rac1.moby.infobot.update` | `func_L00_002D7298` / `FUN_L00_002d5de8` | the infobot tests its planet's flag before it appears: while it runs, the flag shows the infobot's had state |
| `Moby_Helga_Update`, `Moby_BigAl_Update`, `Moby_Bob_Update`, `Moby_Edwina_Update`, `Moby_PilotsHelmet_Update` | `rac1.moby.quest_giver.update`, `.guide.`, `.moby90.`, `.edwina.`, `.moby1290.` | Kerwan, Kerwan, Pokitaru, Hoven, the pickup on Gaspar | these five test *owned* rather than *obtained*: while each runs, its item's owned byte shows its had state, so that Helga is still there when you have the Swingshot from elsewhere |

And once a tick (`on_frame`), any location the game marked had another way
(a gold bolt picked up; the O2 Mask's second route, which writes the bytes
itself) gets its reward then, unless it is yours already.

**The fill.** The rewards that open the way (the gadgets some place needs,
and the planets' coordinates) are placed first, one by one in a random
order: the game is played through as if you already had every such reward
not yet placed, and the reward goes to a random empty place that
playthrough reaches. What is left fills the rest at random. The result is
played through again from the start, sphere by sphere (the spoiler log
prints that playthrough), and refused if the end cannot be reached. The
random numbers are SplitMix64, and everything is integer arithmetic on fixed
tables: a seed is the same game on every computer.

**The seed code** is 60 bits in Crockford's base 32: the format, the
difficulty, the starting gear, the pools, a 40-bit seed and an 8-bit
checksum, so a mistyped code is refused (the log says so and the seed is read
as a word).

## Tests

```sh
ctest --test-dir build -R item_randomizer
build/item-randomizer/item_randomizer_solver_test 100000 0    # more seeds: COUNT FIRST
```

- `seed`: every setting survives the code, codes are read loosely and a
  mistyped one is refused, fixed values for the code and the random numbers;
- `solver`: the tables hold together (each key once; the game's own
  placement can be finished at every difficulty), every seed of every
  setting is solved and beatable, the same seed gives the same placement, a
  placement only moves the shuffled rewards among the shuffled places, and
  one fixed seed's placement hash (the same on macOS, Linux and Windows);
  `solver_sweep` does that for 5,000 seeds of each of the 192 settings;
- `settings`: the launcher's settings into the seed's settings, and the
  manifest declares every key the mod reads;
- `load`: the library as the port loads it, against a host of the test's
  own: its refusals, its eleven hook points and their signatures, and each
  hook against a game memory that behaves as the game's bytes do, and every
  vendor slot bought over 13 seeds and every kind of reward: the hero holds
  the reward, the inventory keeps it, and the store's list stays whole.

## What it needs

An OpenRAC whose port has the item hook points above (OpenRAC c46a749c,
2026-10-21) and the behaviour catalogue's. An older one refuses the mod with
the reason ("this OpenRAC cannot hook Item_Give").
