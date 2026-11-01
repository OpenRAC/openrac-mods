# Item randomizer

Shuffles what Ratchet & Clank gives out, for OpenRAC's native port on either disc: the gadgets and
weapons NPCs and pickups give, the weapons the Gadgetron vendor sells, the gold bolts and the
planets' coordinates the infobots carry. Each seed is a new route through the game, and every seed
can be finished: what you need is only ever placed where you can get it without it. A seed gives
the same game on either disc. With the mod off, the game is the PlayStation 2 game.

## In a multiplayer party

The item randomizer is a gameplay mod: every player needs the same version and the same settings
as the host, or the join is refused and the game says which differ. **Seed**, **Spoiler log** and
**Show what the vendor gives** are not compared: everyone plays the host's seed, so every game has
the same placement, and the other two are your own.

An item another player found comes to you as that item, and the place it was found in is used up in
your game too: you never pay for the same reward twice. The place that item comes from in the
unshuffled game still holds its own reward. A gold bolt found in a place other than a gold bolt's
counts only for the player who found it (see [What is not known](#what-is-not-known)).

## Playing

Install it from the launcher's **Mods** tab (**Add from a link**:
`https://github.com/OpenRAC/rac1-mods`), switch it on, choose its settings and start a **new game**.

| Setting | Default | |
|---|---|---|
| Seed | blank | A **seed code** someone shared (`4ZRJ-6HB7-H6Q5`: it carries their settings, so you play their game), or any word (`ratchet`: the same word with the same settings gives the same game). Blank: a random seed, rolled the first time and kept for your next starts; to roll another, type any word, or delete `seed.txt` in the data folder below. Codes from before version 2 no longer work: their game cannot be made again |
| Logic | Standard | **Standard**: what you need is never behind a very expensive purchase, and each place is expected to be done with the weapons the game suggests there. **Relaxed**: what you need also comes early in the game, and never behind a purchase you have to save up for. **Expert**: what you need may be behind any purchase, even the most expensive, and you get by with any weapon |
| Shuffle gadgets | on | The Heli-Pack, the Swingshot, the Magneboots, the Grindboots, ... from NPCs and pickups |
| Shuffle weapons | on | The Suck Cannon, the Morph-o-Ray, the R.Y.N.O. |
| Shuffle the vendor | on | Each weapon the vendor sells is a slot: buying it, at its price, gives what the seed put there |
| Shuffle gold bolts | on | A gold bolt may be anywhere, and a gold bolt's place may hold a weapon or a useful extra, never something you need to go on. The pause screen counts the gold bolts you have found, wherever they were |
| Shuffle infobots | on | The planets' coordinates the infobots give join the pool |
| Show what the vendor gives | on | A line above the picture says what buying a weapon at the vendor gives you. Off: buying is a surprise |
| Keep the starting gear | on | Start with the Bomb Glove, as the game does. Off: what Veldin gives at the start is shuffled too |
| Spoiler log | on | Writes where everything is, and a way through, to the data folder |

The game's log names the seed's code at each start (`seed 4ZRJ-6HB7-H6Q5 (new), standard`): share
that code. All the settings but the spoiler log and the vendor's line are in it.

**The data folder** holds `seed.txt` (the seed a blank setting rolled) and `spoiler-<code>.txt`, one
per seed. It is in the launcher's per-user data folder, never beside the game:

| System | Folder |
|---|---|
| macOS | `~/Library/Application Support/dev.openrac.launcher/mods/item-randomizer` |
| Linux | `~/.local/share/dev.openrac.launcher/mods/item-randomizer` (or under `$XDG_DATA_HOME`) |
| Windows | `%LOCALAPPDATA%\dev.openrac.launcher\mods\item-randomizer` |

(`OPENRAC_MOD_DATA_DIR` in the environment names another.)

**Saves.** The mod writes nothing of its own into a save: what you have found is the game's own
record, and where everything is comes from the seed. So a save made with the mod **belongs to its
seed**: load it with the same seed code and settings. Loaded with another seed it still loads, but
what is left to find follows the other seed. With the mod off, a save made with it loads as a save
of the game, with the items you had. Saves from before version 2 belong to a seed that can no longer
be made: start a new game.

**What stays as the game has it.** The OmniWrench; the Persuader on Pokitaru; the planets'
coordinates the NPCs give (the mechanic on Novalis, Agnogg's ship, the bouncer, the commando and the
deserter, Sam, the scientist, ...): only the infobots' are shuffled. Gold weapons, ammo and bolts are
the game's.

**What you will notice.**

- An NPC or pickup plays its own scene and then gives the seed's reward; a line of text names it,
  once you are back in control. Its speech still talks of what it would have given.
- A vendor slot shows the weapon it stands for, at that weapon's price, and a line above the picture
  says what it gives. It goes into your inventory and quick select as if the game had sold it. Once
  bought, the slot becomes ammo for the reward, if the reward takes ammo. Ammo for a weapon you own
  whose slot is still for sale is sold too.
- An infobot shows its own planet's video, and gives what the seed put there.
- A gold bolt's place shows the gold bolt's scene, then the reward.
- Orxon starts with Clank until his part is done, even if you already have the O2 Mask: two things
  are found there.
- The Hydro-Pack does not go on your back when you get it: it comes on by itself in the water.

**Where to look when you are stuck.** Places that need something say so, as in the game. A few that
surprise people:

- Skid's agent on Aridia gives his reward only after you have won the Hoverboard race on Rilgar.
- The resort owner on Pokitaru gives his reward once the Blarg ships are shot down: that takes the
  Thruster-Pack, for the jet garage's switch, and the Pilot's Helmet, to fly.
- Gemlik's towers fall to the Devastator or the Visibomb Gun. With the vendor shuffled, these two can
  come from anywhere.
- Drek's Fleet's tanker takes the Hydro-Pack and the O2 Mask.

## How it works

For modders and the curious. The parts:

| File | |
|---|---|
| [src/logic.c](src/logic.c) | the locations: each NPC, pickup, vendor slot, infobot and gold bolt, its planet, what it gives in the game, the order the game's route reaches it in, what it needs, and the game's own words for it |
| [src/seed.c](src/seed.c) | the seed code and the random numbers |
| [src/solver.c](src/solver.c) | the placement (an assumed fill), the playthrough check and the spoiler log |
| [src/config.c](src/config.c) | the settings into the seed's settings |
| [src/item_randomizer.c](src/item_randomizer.c) | the mod: the hooks |
| [src/discs.h](src/discs.h) | what differs between the discs: addresses and names |
| [tools/disc_facts.py](tools/disc_facts.py) | reads every infobot and gold bolt off each disc's levels, into [tests/data/discs.tsv](tests/data/discs.tsv) |

**The locations come from the decompilation.** Every call of the game's `grant_inventory_item`
(GiveItem) and `unlock_planet` in OpenRAC's `games/rac1`, the function around it and the test it
makes before it gives; the gold bolt's update; the vendor's stock (`init_level_item_inventory`, its
table of one new weapon per planet). Which planet each is on, the infobots' planets and trigger slots
and the gold bolts' slots are read from each disc's level data, and they are the same on both.

**What a place needs comes from the game's own words.** The levels' text has the mission list and the
help messages, and they say it: "Destroy Blarg ships for O2 Mask", "You need a Hoverboard to race",
"To progress any further in this area, you must have a Gadgetron Hydrodisplacer". Each row of
logic.c quotes what it rests on. A row asks for too much rather than too little: asking for too much
narrows the shuffle, too little strands a player. What the game does not say is taken from what its
route carries there (`WALK`). Nothing says what each gold bolt takes, so a gold bolt counts as needing
every gadget that opens a way, and holds nothing the way on needs.

**What a location's "had" state is.** The game keeps it, in its save:

| Location | The game's byte |
|---|---|
| an NPC, a pickup, a vendor slot, the starting Bomb Glove | the item's *obtained* byte (`items_acquired[id]`): GiveItem sets it, and most pickups test it before they appear |
| an infobot | its trigger slot (`saved_mission_state[mission + level * 16]`), which the game sets right after the infobot's `unlock_planet` |
| a gold bolt | its byte (`gold_bolts_collected[level * 4 + slot]`) |

The items the player *owns* are the other array (`item_available`), which the hero, the menus and
the vendor read. The mod gives a reward's item through the game's own GiveItem (its starting ammo,
the quick select) and then puts that item's obtained byte back: it is that item's own location's.

**What the player reads.** A level's text holds only that level's messages, so the banner GiveItem
shows for an item of another level reads "Paradox: This message does not exist". The mod keeps the
banner as it was around GiveItem and `unlock_planet`, and writes the banner's text itself from the
names every level has, in the player's language, when the player is back in control. It writes it
again for a few ticks, over the banner the game's own code shows next for the item it thinks it gave.

**The hooks** (OpenRAC's hook points, by name and permanent id):

| Hook point | Id | What the mod does |
|---|---|---|
| `Item_Give` | `rac1.item.give` | the location of the item the game gives gives its reward; the location is marked had. After a purchase the hero holds the reward (or, for a reward with no store demo, the weapon held before), never the slot's own weapon |
| `Planet_Unlock` | `rac1.planet.unlock` | inside an infobot's update: the infobot's reward instead of its planet. Elsewhere (NPCs, the planet arrived at) the game's |
| `Vendor_InitLevelStock` | `rac1.vendor.init_level_stock` | a level's start: gives back a reward a save lost, and takes the had states as they are; with the starting gear shuffled, the Bomb Glove is taken back out of the stock and the double-tap weapon when it was not the reward |
| `Vendor_BuildItemList` | `rac1.vendor.build_item_list` | the stock as the menu sees it: slots not bought for sale, bought ones as their reward's ammo; the cursor stays on the list and the item is drawn again |
| `Vendor_MenuUpdate` | `rac1.vendor.menu_update` | the line above the picture over a slot |
| `Ammo_ChooseDrop` | `rac1.ammo.choose_drop` | a crate's drop chooses from the same stock as the menu |
| `GoldBolts_CountCollected` | `rac1.gold_bolt.count_collected` | with gold bolts shuffled: the gold bolt rewards had |
| `Moby_Infobot_Update` | `rac1.moby.infobot.update` | the infobot tests its planet's flag before it appears: while it runs, the flag shows the infobot's had state |
| `Moby_Helga_Update`, `Moby_RobotQwark_Update`, `Moby_BigAl_Update`, `Moby_Bob_Update`, `Moby_Edwina_Update`, `Moby_PilotsHelmet_Update`, `Moby_Class23_Update` | `rac1.moby.quest_giver.update`, `.moby914.`, `.guide.`, `.moby90.`, `.edwina.`, `.moby1290.`, `.pickup.` | these test *owned* rather than *obtained*: while each runs, its item's owned byte shows its had state, so that Helga is still there when you have the Swingshot from elsewhere. The helmet's pickup also hands the hero the helmet to wear: undone when it gave something else |
| `Moby_Class22_Update`, `Ship_InitTransition` | `rac1.moby.moby22.update`, `rac1.ship.init_transition` | Orxon starts Clank's part only without the O2 Mask: these two see none until it is done |
| `Help_Request` | `rac1.help.request` | a tutorial of the item the game thinks it gave is left out when it gave another |

`Help_Request` and `Ship_InitTransition` need an OpenRAC of interface 1.12; an older one still runs
the mod, as its log says. And once a tick (`on_frame`), any location the game marked had another way
(a gold bolt picked up; the O2 Mask's second route, which writes the bytes itself) gets its reward
then, unless it is yours already. A gold bolt's autosave is written in the update that picks it up,
before that check; so each level's start gives any place had its reward, quietly.

**The fill.** The rewards that open the way (the gadgets some place needs, and the planets'
coordinates) are placed first, one by one in a random order: the game is played through as if you
already had every such reward not yet placed, and the reward goes to a random empty place that
playthrough reaches. What is left fills the rest at random. The result is played through again from
the start, sphere by sphere (the spoiler log prints that playthrough), and refused if the end cannot
be reached or if any reward is left where it cannot be had. The random numbers are SplitMix64, and
everything is integer arithmetic on fixed tables: a seed is the same game on every computer and on
either disc.

**The seed code** is 60 bits in Crockford's base 32: the format, the difficulty, the starting gear,
the pools, a 40-bit seed and an 8-bit checksum, so a mistyped code is refused (the log says so and
the seed is read as a word). Format 2 is the table of version 2.

## Tests

```sh
ctest --test-dir build -R item_randomizer
build/item-randomizer/item_randomizer_solver_test 100000 0    # more seeds: COUNT FIRST
```

- `seed`: every setting survives the code, codes are read loosely and a mistyped one is refused,
  fixed values for the code and the random numbers;
- `solver`: the tables hold together, the game's own placement can be finished at every difficulty,
  every seed of every setting is solved, beatable and strands nothing, the same seed gives the same
  placement, a placement only moves the shuffled rewards among the shuffled places, progression keeps
  out of gold bolts and of the purchases each difficulty rules out, and one fixed seed's placement
  hash (the same on macOS, Linux and Windows); `solver_sweep` does that for 5,000 seeds of each of the
  192 settings;
- `table`: every infobot and gold bolt of both discs has its row and every row its moby; the
  requirements the game states are pinned; each softlock an earlier table allowed is built by hand and
  must be refused, and in solved placements every gadget moved behind a place that needs it must be
  seen;
- `settings`: the launcher's settings into the seed's settings, and the manifest declares every key
  the mod reads;
- `load`: the library as the port loads it, against a host of the test's own: its refusals, its hook
  points and their signatures, and each hook against a game memory that behaves as the game's bytes
  do: every vendor slot bought over 13 seeds and every kind of reward, the banner, equipping, a reward
  lost to a save, a party's item spending its place, Orxon, the helmet, tutorials and drops.

## What is not known

- What each gold bolt really takes: the rows assume all, so none holds what the way on needs.
- Rows without the game's own words are the route's (`WALK`): they may ask for more than a place takes.
- In a party, a gold bolt given by a place that is not a gold bolt's counts in the finder's game only:
  the game has no byte for it that a party shares.
- The second run of challenge mode: places can be had again, and give what you own already.

## What it needs

An OpenRAC with the item hook points above and the behaviour catalogue's. An older one refuses the
mod with the reason ("this OpenRAC cannot hook Item_Give").
