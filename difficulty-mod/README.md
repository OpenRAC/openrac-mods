# Difficulty

A slider for everything that makes Ratchet & Clank hard or easy, for
OpenRAC's native port (PAL v2.00 and NTSC-U discs): how many enemies there
are, the damage Ratchet takes and deals, how fast the enemies and Ratchet
move, what weapons and ammo cost, how much ammo a shot takes and a weapon
holds, how many bolts drop, Ratchet's health and what health pickups give.
Presets fill them in: **Easy**, **Retail**, **Hard**, **Brutal** and
**Chaos**, which rolls new values for every level from a seed. With every
slider at 1.0 (the Retail preset), or with the mod off, the game is the
PlayStation 2 game.

It needs an OpenRAC built from master on 2026-10-21 or later (the game's
hook points for damage, hits, ammo, deaths, the level load and Ratchet's
speed). An older one refuses the mod and names the missing hook point.

## Playing

Install it from the launcher's **Mods** tab (**Add from a link**:
`https://github.com/OpenRAC/rac1-mods`), switch it on, choose its settings
and play. Settings count from the game's next start; the enemy count and the
prices from the next level loaded.

### The presets

The presets are buttons above the settings in the Mods tab: each sets every
slider and the Chaos switch, and the button of the values the sliders have is
lit. Move a slider after and that is what plays (no button is lit then). This
mod's presets need an OpenRAC launcher built on 2026-10-22 or later.

| Preset | |
|---|---|
| **Retail** (default) | Everything 1.0: the game as it is |
| **Easy** | Fewer enemies (0.75), half the damage to Ratchet, 1.5 times the damage to enemies, slower enemies, cheaper weapons and ammo, more ammo, more bolts, 8 cells of health, double health pickups |
| **Hard** | 1.5 times the enemies, twice the damage to Ratchet, enemies 1.2 times as fast, 1.5 times the prices, a quarter more ammo per shot, less ammo and fewer bolts and pickups |
| **Brutal** | Twice the enemies, three times the damage to Ratchet, half the damage to enemies, enemies 1.4 times as fast, 2.5 times the prices, half the max ammo, the bolts and the pickups, and 2 cells of health |
| **Chaos** | Every slider at 1.0 and the **Chaos** switch on: each level rolls its own values, every slider within its Chaos range below, from the **seed** |

With Chaos on, a slider you move off 1.0 holds on every level and the others
roll. The game's log says what plays: `sliders`, `retail` or `chaos`, and then
every value.

The full table, slider by slider:

| Slider | Range | Easy | Hard | Brutal | Chaos range |
|---|---|---|---|---|---|
| Enemy count | 0.25 to 4 | 0.75 | 1.5 | 2 | 0.5 to 2.5 |
| Damage to Ratchet | 0.25 to 4 | 0.5 | 2 | 3 | 0.5 to 3 |
| Damage to enemies | 0.25 to 4 | 1.5 | 0.75 | 0.5 | 0.5 to 2 |
| Enemy speed | 0.5 to 2 | 0.85 | 1.2 | 1.4 | 0.7 to 1.6 |
| Ratchet's speed | 0.5 to 2 | 1.1 | 1 | 1 | 0.8 to 1.4 |
| Weapon prices | 0.1 to 5 | 0.75 | 1.5 | 2.5 | 0.5 to 2.5 |
| Ammo prices | 0.1 to 5 | 0.5 | 1.5 | 2.5 | 0.5 to 2.5 |
| Ammo per shot | 0.25 to 4 | 0.75 | 1.25 | 1.5 | 0.5 to 2 |
| Max ammo | 0.25 to 4 | 1.5 | 0.75 | 0.5 | 0.5 to 2 |
| Bolt drops | 0 to 5 | 1.5 | 0.75 | 0.5 | 0.5 to 3 |
| Max health | 0.25 to 2 | 2 | 1 | 0.5 | 0.5 to 2 |
| Health pickups | 0 to 4 | 2 | 0.75 | 0.5 | 0.5 to 2 |

### The sliders

| Slider | What it changes |
|---|---|
| **Enemy count** | Above 1: copies of the level's enemies beside them when the level loads (3 means about two copies of each). Below 1: that share of the free-roaming enemies left out (0.5: about half). Only enemies free of the level's scripts are copied or left out: never bosses, turrets, mines, spawners, the enemies of a wave or of a "defeat them all" door, nor any the story or another object needs (see [which mobys count as enemies](#which-mobys-count-as-enemies)). A copy stands on the same floor, in the open, 2.5 to 4 units from its original; where there is no such spot, it is not made. Copies drop no bolts and are not remembered as killed. How many copies a level gets is limited by room: see [how many enemies](#how-many-enemies) |
| **Damage to Ratchet** | How many cells of health a hit takes, on average: 2 is two cells a hit, 0.5 a cell every other hit, 1.5 one then two. Falls, crushers and other instant deaths are the game's |
| **Damage to enemies** | The damage of every hit on an enemy, a turret or a boss: your weapons, the wrench, explosions |
| **Enemy speed** | How fast enemies and turrets move, turn, animate, aim and fire: the clock their own code runs on. Bosses keep their own pace |
| **Ratchet's speed** | His walking and running speed (jumps and gravity are the game's) |
| **Weapon prices** | What the Gadgetron vendor asks for each weapon, and what NPCs ask for the gadgets they sell. The nanotech upgrades on Orxon cost what the game says |
| **Ammo prices** | The vendor's price for each round of ammo |
| **Ammo per shot** | Rounds a shot takes, on average (0.5: every other shot is free, 2: two a shot). A gun never fires empty, and its last rounds still fire when a shot would want more |
| **Max ammo** | How many rounds each weapon holds (and what a weapon comes with). Ammo you already have over a lower maximum is not added to, and the game may cut it down to the maximum when it loads |
| **Bolt drops** | Bolts from enemies and crates. 0: none. Rewards the story pays (the bolts NPCs give, races, the codebot) are the game's |
| **Max health** | Ratchet's cells of health, from what you bought (4, 5 with the Premium Nanotech, 8 with the Ultra): the HUD has places for 1 to 5 cells and for 8, so the nearest of those at or below. 2.0 makes 4 cells 8; Ultra's 8 is the most |
| **Health pickups** | Cells each nanotech pickup gives, on average. 0: none |

### Chaos

| Setting | Default | |
|---|---|---|
| Chaos | off | Each level rolls its own value for every slider left at 1.0, from the seed (the Chaos preset switches it on) |
| Seed | blank | For Chaos (and the events): the 8-character code the log shows (`seed 0A1B2C3D`) plays the same rolls again, and any other word gives its own. Blank: a new seed at each start, shown in the log |
| Chaos events | off | Every few minutes of play, one of these for 45 seconds: **Turbo Ratchet** (1.5 times as fast), **Slow motion** (the enemies at half speed), **Frenzy** (the enemies 1.5 times as fast), **Double time** (both), **Glass cannon** (twice the damage both ways), **Iron hide** (half the damage to Ratchet), **Bolt rain** (three times the bolts), **Ammo famine** (twice the ammo a shot), **Trigger happy** (a quarter), **Nanotech surge** (three times the health pickups). The log says when one starts and ends. With any preset |
| Minutes between events | 3 | |
| Trace | off | For bug reports: each level's values and every max health change in the game's log |

With Chaos, each level's values come from the seed and the level alone, so
the same seed is the same game: share the code from the log.

## How many enemies

The game keeps one pool of 255 slots per level for everything it makes as it
goes: shots, sparks, bolts, debris, pickups (Drek's fleet has 399). When it
is full the game cannot make what it wants, and on Gaspar its lava sparks
nearly fill it on a console already. So the copies take only what the game
can spare: **160 slots always stay free** for the game, and a level's copies
take at most the rest (95 of 255). An enemy whose own data is larger than a
spawned object's place (0x80 bytes) costs slots for that too. **Gaspar gets
no copies.** The log says what each level got: `level 4: 41 copies of 23
enemies (255 free slots, room for 95 beside the 160 kept for the game; 60
reserved for pvars; 3 without a clear spot)`. With a high enemy count the
copies are spread: each enemy gets one before any gets a second.

## Which mobys count as enemies

The game's objects are classified in [classes.tsv](classes.tsv), 158 of
them: 39 **enemies** (copied, left out, timed, hit harder or softer), 5
**roamers** (left out, timed, hit; never copied: they fly a path, drop in on
a cue, or are too big to stand beside the original), 9 **turrets** (timed and
hit; fixed to the level, so never copied or left out), 5 **bosses** (only
the damage to enemies applies to them) and 100 left as the game has them.
Everything not in the table is left as it is.

| Left as the game has it | Why |
|---|---|
| Ultramech (Quartu), giant Clank | The Ultramech fight's boss is scripted; giant Clank is you |
| Space fighters, interceptors, Blarg ships, Qwark's starfighter | The ship levels and the scenes' ships are left as they are |
| Bolt thieves | A scripted chase: the bolts are the level's |
| Mines, homing mines | Hazards laid out by the level, in patterns |
| Spawners, nests, eggs, the gadgebots' pads | What a spawner makes is the game's (and it reuses its group's enemies as templates) |
| The Glove of Doom's bots, the Drone Device's drones | Yours |
| Plasmabot hatch, sentrybot alarm | What the enemies come out of or answer to |
| Sand mice, frogs, the miners and constructorbots | Wildlife or workers, not known to attack |
| The Veldin robot enemy, the grass bombthrowers | Their update is shared with the vendor's, the grass's and the plants' |
| Deplanetizer guns, robot heads and arms, helicopters, airships, logging machinery, doors, lifts | Story or scenery |
| Batalia's deserter, codebots | Helpers and NPCs |

And among the enemies, only those free of the level's scripts are copied or
left out: a moby in a group (a wave, a door that opens when all are
defeated), one tied to a mission, one another object names, or one the
level's file does not describe is left as it is.

## How it works

Each value is changed where the game takes it, through OpenRAC's hook points
(stable ids, [hooks](https://github.com/OpenRAC/OpenRAC/blob/master/sdk/docs/hooks.md)),
or once a level at its source; nothing is patched every frame, and with a
slider at 1.0 its hook calls the game's own function exactly as the game
did.

| Slider | Where |
|---|---|
| Damage to Ratchet | `rac1.player.take_damage` (Hero_TakeDamage, which takes one cell a call): called that many times on average |
| Damage to enemies | `rac1.combat.get_hit` (MobyGetHitMessage, how every enemy reads the hit it takes): the hit's damage scaled once, however often it is read |
| Enemy speed | Each timed class's update, `rac1.moby.<class>.update` (the behaviour catalogue's hook points): the tick's time constants scaled for the call (the game's movement and timers are written in them), and the moby's animation speed |
| Ratchet's speed | `rac1.player.approach_speed` (Hero_ApproachSpeed): his ground speed eases toward his target speed scaled |
| Prices, max ammo | The items' table (`D_L00_001C43B0` on PAL), rewritten from the level's own values at each level's load (`rac1.level.load_gameplay`): never scaled twice |
| Ammo per shot | `rac1.weapon.take_ammo` (TakeAmmo) |
| Bolt drops | `rac1.moby.record_death` (SetDeathBits, which records a death and drops its bolts) |
| Max health | gMaxNanotech, kept to the upgrades' count times the slider each tick in play |
| Health pickups | `rac1.moby.nanotech_ball.update`: the cells the pickup gives |
| Enemy count | `rac1.level.load_gameplay` (LoadLevelGameplay): after the level's objects are placed and before any has run |

Saves: the mod writes nothing into a save. Max health, prices and the
enemies come back as the game has them when it is off; only the ammo you
had stays as it was when you saved.

The [tests](tests/) check the presets (each sets every slider and Chaos, in
range, Easy easier and Brutal harder than Hard), the sliders' ranges and
clamping, that Chaos's rolls and events are the same for the same seed, that
every catalogue class that fights is classified, the pool's guard on a level
laid out in memory, and the library as the port loads it.

## Building

From the repository's top (see [README.md](../README.md#building)). The
classes' header is generated: after changing [classes.tsv](classes.tsv), run
`python3 tools/gen_classes.py <SDK>/catalogue/rac1/classes.json
<OpenRAC>/sdk/bindings/rac1.ids.lock`.
