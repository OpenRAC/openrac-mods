# NG++

New Game++ for Going Commando, for OpenRAC's native port (NTSC-U, PAL and NTSC-U v2.00 discs):
eight more upgrades for every weapon after its last version, Alpha to Omega (V5 to V12). The game's
own versions stay as they are; NG++ adds its tiers on top and keeps track of them itself.
From the second challenge mode on, enemies get tougher, drops pay more bolts and part of each drop
comes as platinum bolts, worth five times the gold ones, and obsidian bolts, worth 25 times.

With the mod off, the game plays exactly as on the PlayStation 2.

## Playing

Install it from the launcher's **Mods** tab (**Add from a link**:
`https://github.com/OpenRAC/openrac-mods`), switch it on and play.

**Buy, then level up**, like the game's own Mega upgrade followed by V4: once a weapon is at its
last version, the vendor sells its next tier. Once you level that tier up with XP, the vendor sells
the next one.

| Level | Name | How | Price | XP | Damage | Max ammo | Color |
|---|---|---|---|---|---|---|---|
| V5 | Alpha | buy | Mega x1.5 | | x1.50 | x1.10 | lime |
| V6 | Beta | level up | | 8,000 | x2.00 | x1.20 | cyan |
| V7 | Gamma | buy | Mega x2.5 | | x2.50 | x1.30 | emerald |
| V8 | Delta | level up | | 12,000 | x3.00 | x1.40 | teal |
| V9 | Epsilon | buy | Mega x4 | | x3.75 | x1.50 | violet |
| V10 | Zeta | level up | | 16,000 | x4.50 | x1.60 | magenta |
| V11 | Eta | buy | Mega x6 | | x5.50 | x1.80 | crimson |
| V12 | Omega | level up | | 24,000 | x7.00 | x2.00 | rainbow (cycles through all the colors) |

Damage and max ammo are multiplied from the weapon's last real version. XP is the game's own weapon
XP (the bar in the quick select). It starts again from 0 at each tier, and the first time the mod
sees a weapon at its last version it resets the XP to 0, so XP from before the mod or from earlier
games never gives a tier right away. The XP bar (weapons screen and quick select) fills toward the
next tier, or toward the 6,000 XP a bought tier needs before you can buy the next one. It shows full
only when the next tier is already for sale or there is nothing left.

**Prices** follow each weapon's own Mega price (150,000 to 1,500,000; at least 150,000, so the Bomb
Glove's 1,000 Mega counts as 150,000), times 1.5, 2.5, 4 and 6, rounded (562,500 becomes 565,000),
each higher than the one before and higher than the Mega. A weapon with no Mega (Clank Zapper,
Sheepinator) starts from 1,500,000: 2,250,000, 3,750,000, 6,000,000 and 9,000,000. Lancer and
Chopper (Mega 150,000): 225,000, 375,000, 600,000, 900,000. Gravity Bomb (1,500,000): 2,250,000,
3,750,000, 6,000,000, 9,000,000.

- **Four-version weapons** (Lancer, Chopper, ...): Alpha is for sale as soon as the weapon is V4.
- **Two-version weapons** (Bomb Glove, Visibomb Gun, Tesla Claw, Walloper) and the **Zodiac**:
  their last version is bought and the game has no XP level-up after it, so Alpha goes on sale
  after 6,000 more XP.
- **Decoy Glove**: the game gives XP to the weapon that landed the killing hit, and the decoy deals
  no damage (it only draws fire), so it can never earn XP. All its tiers are bought instead, each
  for sale as soon as you buy the one before: Alpha, Gamma, Epsilon and Eta at the usual prices,
  Beta, Delta and Zeta in between (its Mega times 2, 3.25 and 5) and Omega at 9 times. At the
  150,000 minimum: 225,000, 300,000, 375,000, 490,000, 600,000, 750,000, 900,000 and 1,350,000.
  Every other weapon earns its own XP from its kills: shots, beams, blasts, sheep, Synthenoids,
  Spiderbots and turrets all count.
- **Clank Zapper**: Alpha is for sale once it is V2.
- **Sheepinator**: like the four-version weapons, Alpha is for sale once it is V4 (it has no Mega,
  so its prices start from 1,500,000).
- **RYNO II**: first **Heavy RYNO II** at 30,000 XP (x1.4 damage, x1.5 max ammo, orange), then
  **Ultima RYNO II** from the vendor for 1,500,000 (x2.2 damage, x2 max ammo, black and red), then
  Alpha (after 30,000 more XP) to Omega, each adding its bonus on top of Ultima's. Its prices are
  1,000,000 times 1.5, 2.5, 4, 6 and 9: Ultima 1,500,000, Alpha 2,500,000, Gamma 4,000,000,
  Epsilon 6,000,000, Eta 9,000,000. The RYNO II levels up five times slower than other weapons: its
  Beta, Delta, Zeta and Omega need 40,000, 60,000, 80,000 and 120,000 XP (`kRynoExpScale` in
  src/tiers.h).

**Names**: the tier replaces the game's top version in the weapon's name, in front of the name the
weapon had before its Mega. A four-version weapon uses its second version's name: "Beta Meteor
Gun", "Alpha Heavy Lancer" (not "Beta Lava Gun", nor the Mega's name). A two-version weapon uses
its first: "Alpha Bomb Glove". The Clank Zapper uses its last, "Alpha Clank Shocker"; the
Sheepinator its last XP version, "Alpha Black Sheepinator"; the Zodiac and the RYNO II their only
one ("Heavy RYNO II", "Ultima RYNO II", "Alpha RYNO II"). The names come from the game's own text,
so every language works. A tier reached by XP shows the game's own level-up popup and message with
the new name; a bought one shows the vendor's usual purchase message. The name also shows at the
top of the screen when you switch to a tiered weapon.

Every text the game shows for the weapon uses the tier's name. The game keeps several texts for each
version of a weapon (rows of its item table), one per screen:

| Row field | Where the game shows it |
|---|---|
| +0x08 | the name in the vendor's dialog and buy prompt, the item menus, the weapons screen and the upgrade price |
| +0x38, +0x3A | the ammo pickup message, for several rounds (with the count) and for one |
| +0x42 | the pause menu's weapon panel |
| +0x46 | the vendor's description of the weapon |
| +0x48 | the name on the weapon wheel |

The mod takes the game's text and swaps in the tier's name for the version's name ("Got 12 Ultra
Meteor Gun ammo" becomes "Got 12 Beta Meteor Gun ammo"), so every language and the ammo count stay.
The game's messages put color codes around the name, even between its words ("Got 12 Ultra
<code>Heavy <code>Lancer<code><code> ammo"), and some names end in a space the message doesn't
have: the mod matches the name through both and keeps the colors. A text that doesn't contain the
version's name, or that two weapons share, stays as it is (the Mega Rocket's ammo message says
"Ultra Megarocket Cannon" while its name is "Ultra Mega Rocket", so it keeps the game's text). In
the vendor, "Mega" under the next tier's icon becomes the tier's name ("Alpha"), and that entry's
dialog and description name the weapon at the tier for sale.

In the vendor's list, the next tier's entry shows that tier's color, and the weapon's own entry the
color of the tier it has.

### Tougher enemies each new game

The tiers make weapons much stronger, so enemies get tougher across challenge mode's new games. The
game itself counts your challenge modes but only checks whether one is on: it doubles ammo pickups
and nanotech orbs' health there, and its bolt multiplier grows with kills. It never makes enemies
tougher. From the second challenge mode on, the mod does:

| New game | Challenge mode | Enemy health | Enemy damage | Tier you should have by then | How fast you kill, vs V4 in the 1st |
|---|---|---|---|---|---|
| 1 | - | x1 | x1 | V1 to V3 | |
| 2 | 1st | x1 | x1 | V4, then Alpha to Gamma | x1 (V4), x1.5 to x2.5 |
| 3 | 2nd | x2.5 | x1.375 | Delta (x3) | x1.2 |
| 4 | 3rd | x4 | x1.75 | Zeta (x4.5) | x1.125 |
| 5 | 4th | x5.5 | x2.125 | Omega (x7) | x1.27 |
| 6 | 5th | x6 | x2.5 | Omega (x7) | x1.17 |
| 7 | 6th | x6.5 | x2.875 | Omega (x7) | x1.08 |
| 8 on | 7th on | x6.75 | +0.375 each, up to x4 | Omega (x7) | x1.04 |

Enemies get tougher half as fast again as before 0.4.0, but enemy health stays just behind your
damage: a weapon at the tier you should have by then still kills a little faster than V4 did in the
first challenge mode, and the health stops at x6.75, so a fully upgraded (Omega) weapon stays ahead
in every new game. Enemy damage grows more slowly, since longer fights already cost you more health.
**How much tougher** scales what the mod adds (0.5: x1.75 health in the third new game instead of
x2.5; above 1 enemies can pass your tiers).

(Before 0.4.0 enemy health went x2, x3, x4, x5, then +1 a game, and enemy damage x1.25, then +0.25 a
game. Before 0.2.1 the tiers gave x1.25 to x4 and enemy health went x2.5, x4, x6, x8, +2 each.)

| Setting | Default | |
|---|---|---|
| Tier names on screen | on | The weapon's name at the top of the screen |
| Tougher enemies each new game | on | The table above |
| How much tougher | 1 | Multiplies what the table adds, 0 to 3 |
| Bolt scaling | normal | Platinum and obsidian bolts, bigger drops and the collectors' pay (below): off, normal or generous |

With the [difficulty](../difficulty-mod/README.md) mod, both apply and **multiply**: its "damage
dealt" and "damage taken" sliders scale the same hits as this mod's tiers and enemies, so with
damage dealt x0.5 and a Beta weapon in the second challenge mode a hit does x0.5 x 2 / 2. Turn
**Tougher enemies each new game** off to leave the enemies to the difficulty mod alone.

### Platinum and obsidian bolts and bolt scaling

Tiers cost millions, so from the second challenge mode on, enemies and crates drop more bolts, and
part of every drop comes as **platinum bolts**: the game's own bolt pickups in a shiny white metal,
each worth five times its gold twin. Above them are **obsidian bolts**: glossy black with a glow
inside, violet on the small ones through magenta to ember red on the biggest, each worth 25 times its
gold twin.

| Gold bolt | 1 | 5 | 20 | 50 | 100 | 500 | 1,000 |
|---|---|---|---|---|---|---|---|
| **Platinum bolt** | **5** | **25** | **100** | **250** | **500** | **2,500** | **5,000** |
| **Obsidian bolt** | **25** | **125** | **500** | **1,250** | **2,500** | **12,500** | **25,000** |

**Bigger drops.** The game works out a drop as always (the enemy's bolts, your kill streak up to
x20, challenge mode's doubling), then the mod multiplies it by the new game you are in, the same
steps as the tier prices (x1.5, x2.5, x4, x6), so the tier you are saving for takes about as many
kills in every new game:

| New game | Challenge mode | Bolts (normal) | Bolts (generous) | Platinum at x1 streak | Platinum at x20 streak |
|---|---|---|---|---|---|
| 1 | - | x1 | x1 | none | none |
| 2 | 1st | x1 | x1 | 20% | 40% |
| 3 | 2nd | x1.5 | x2.25 | 10% | 50% |
| 4 | 3rd | x2.5 | x3.75 | 15% | 65% |
| 5 | 4th | x4 | x6 | 20% | 80% |
| 6 on | 5th on | x6 | x9 | 25% | 90% |

**Tough enemies pay more.** On top of that, an enemy that drops a lot by itself (the game's own
bolts for it, before your streak) pays up to twice as much: x1 up to 20 bolts, then the square root
of its bolts over 20 (45 bolts: x1.5, 80 or more: x2). Small enemies and crates keep x1.

**Platinum share.** The longer your kill streak, the more of a drop comes as platinum, from the
"x1" column at no streak up to the "x20" one at a full x20 streak, in even steps between. It changes
how the bolts come, not how many: a drop of 15,000 at 65% is a platinum 5,000, a platinum 2,500,
four platinum 500s and a platinum 250 (9,750), and 5,250 in gold. Fewer, bigger pickups.

**Obsidian share.** From the second challenge mode on, obsidian bolts show up now and then, mostly
at long kill streaks: the share grows slowly with your streak at first and fast near x20, and grows
with each new game. Obsidian takes its part of a drop first, then platinum its share, then gold.

| New game | Challenge mode | Obsidian at x1 streak | at x10 | at x20 streak |
|---|---|---|---|---|
| 1 and 2 | - and 1st | none | none | none |
| 3 | 2nd | none | 2% | 8% |
| 4 | 3rd | 2% | 5% | 15% |
| 5 | 4th | 5% | 10% | 25% |
| 6 on | 5th on | 8% | 14% | 35% |

When the game limits how many pickups a drop may have, each rarer kind keeps a place, so a small
drop can still show an obsidian or platinum bolt.

**Collectors.** Tabora's crystals and Grelbin's moonstones pay the same x1.5, x2.5, x4 and x6 from
the second challenge mode (generous half as much again), so collecting keeps up with the tier
prices. The collector's dialog shows the total you will get.

The total is always the game's own after its roll, so its kill streak, doubling and its own pacing
still count. The first game is left alone; the first challenge mode gets a few platinum bolts, a taste of them, with drops the same size. **Bolt scaling** off leaves every
drop to the game; generous pays half as much again as normal (the table's generous column), with
the same platinum share.

Platinum bolts need OpenRAC with interface 1.23 (the bolt hook points and the platinum look), and
obsidian bolts 1.24 (the obsidian look). On an OpenRAC with 1.23 the drops have platinum but no
obsidian; on an older one the drops stay the game's, and the rest of the mod works as before (the log
says so once).

### Saves

The tiers are kept with each save: the mod's data folder has a `tiers.txt` with the tiers of each
save the game made (by the save's own clock), and each new tier is written there right away, so a
restart or a mod update loses nothing. Each save gets only its own tiers:

- The mod watches the save's clock at the title screen too, where a loaded save shows its own clock
  before its level starts and the game stamps a new one; the new clock keeps the save's tiers.
- A clock the file doesn't have is the same save stamped again (when you save, at an autosave, at a
  level start) only if the save's data didn't change; if it did change (bolts, weapon XP or
  versions), you loaded a save the mod has never seen, and it starts with no tiers and its weapons'
  XP counted from 0, like any save new to the mod.
- After a new game's zero clock, the tiers carry over only into challenge mode's next game, which
  keeps your weapons; a plain new game starts with none.

A record with tiers is never overwritten with an empty one. Nothing is written to the memory card
or its folder.

The launcher's save editor shows each weapon's tier in its version picker for the save you open
while the mod is on, and saves your changes to `tiers.txt`; close the game first. It also lets you
set every weapon's XP, at its last version and for the RYNO II too. Exporting saves from the
launcher takes their tiers along, and importing them puts the tiers back, on any computer.

It works with the [item randomizer](../item-randomizer/README.md) (they use none of the same hook
points) and with the [difficulty](../difficulty-mod/README.md) mod (its prices and max ammo are what
the tiers multiply). In a [multiplayer](../multiplayer/README.md) party everyone plays with the
host's tiers.

## How it works

Through OpenRAC's rac2 hook points of SDK 1.14.6, and the bolts' of interface 1.23 and the obsidian
material of 1.24 (the addresses are NTSC-U v1.01's):

| | Where |
|---|---|
| For sale | `rac2.vendor.build_list` (`func_002F6608`): an upgrade entry for each weapon whose next tier is for sale, right after that weapon's own entry and built as the game builds its Mega entry; `rac2.vendor.price` (`func_002F6510`) prices it; `rac2.item.upgrade` (`func_002887B0`), which the vendor's purchase calls once it is paid, takes the tier instead of the game's Mega step |
| Experience | each tick, a weapon at its last version whose experience (which the game keeps adding up there) reaches the tier's takes it; its experience starts again from 0, as the game's own Mega purchase does |
| Experience bar | the last version's row (+0x6C, the experience to the next version, which the game leaves at 0 there: a full bar) set each tick to the next tier's goal, and back to the game's when none is left; the game's own level check skips every last version, so it never acts on it |
| Damage | `rac2.combat.post_hit` (`func_002A8E20`): a hit posted to anyone but Ratchet has its damage (+0x1C) scaled for that call by the tier of the weapon that dealt it: the weapon whose own moby is the request's source (+0x10, its class at +0xAA; `kHitSources` in src/discs.h: the Lancer, Blitz Gun and Cannon, Lava Gun and its pools, Seeker and Minirocket shots, Gravity Bomb blasts, Bouncer blasts, Spiderbot trails, Shield Charger and Tesla Barrier, Tesla Claw), else the weapon in Ratchet's hand |
| Max ammo | the last version's row (+0x8E) in the item table, rewritten each tick from what the game (or another mod) has there; a tier fills the ammo |
| Enemies | the save's challenge mode counter (+0x12 of its block); their health as the damage they take in `rac2.combat.post_hit` divided by the curve, the damage they deal in `rac2.player.take_damage` (`func_L00_002A4EF8`) times it, a fraction of a point carried to the next hit |
| Color | `rac2.item.colors` (`func_00336F20`): the quick select's and the weapons screen's icons and names; `rac2.vendor.draw_list` (`func_002F9C80`) and `rac2.item.version_color` (`func_00336EF8`): the tier for sale's icon in the vendor's list (an OpenRAC without these two keeps the game's gold there) |
| Bolts | `rac2.bolt.spawn_pickups` (`func_002ADB78` and its copy in every level's code): from the second challenge mode, the game's burst runs with its amount (low and high) times the run scale and the enemy's toughness (its own bolts, s16 at +0xB4, when its class record at +0x24 says kind 5 at +0x46, as `SetDeathBits` checks); the coins it makes through `rac2.bolt.spawn_pickup` (`func_00302470`) are only written down (worth, start, speed), and their total is made again as obsidian, platinum and gold coins at those places, the rarer ones drawn with `moby_material` (platinum from interface 1.23; obsidian from 1.24, its glow colour per class in the call's rgb); `rac2.bolt.collect` (`func_00301D10`) adds four times a platinum coin's class (24 times an obsidian one's) after the game's own and takes its look off. The kill streak is the byte at +0x3A of the save block (`D_001A7A32`); what a coin limit leaves goes to the game's word at +0x1C (`D_001A7A14`), divided by the streak, as the game does |
| Collectors | no hook point: their dialogs' texts (10135 Tabora's crystals, 10209 Grelbin's moonstones) through `rac2.text.by_id` get the scaled total in place of their second number; each tick a trade (the crystals at +0x22 of the save block, s16, or the moonstones at +0x24, s32, down to 0 in Tabora or Grelbin with the game's pay, 1,000 or 3,000 each, ten times in challenge mode, in the bolts at +0x00) gets the rest of the scaled pay through `openrac_api.bolts` |
| Level-up popup | a tier earned with the weapon in hand plays the game's in-hand level-up (`func_L00_0037E6A8`); at its end the game's popup from its own level-up table (`help_show_message_for_id`, `func_00288508`) names the last version, or is text 0 (an empty box) for a weapon the table has no line for (RYNO II, Zodiac): for ten seconds after, the resolver gives that popup the tier's name |
| Texts | `rac2.text.by_id` (`func_00288FB8`), one resolver for every text: a looked-up id that is a text field of a row the mod names at a tier (the vendor's entry being drawn or named, the level-up line the mod shows, or the row of the one owned tiered weapon that holds it) gets the game's string with the tier's name in it, written into the port's text slots; optional (an OpenRAC without it shows the game's texts) |

The code (C++17, on the SDK's `openrac/mod.hpp`, laid out by OpenRAC's
[coding conventions](https://github.com/OpenRAC/OpenRAC/blob/develop/port/docs/CODING_CONVENTIONS.md)):

| File | What it owns |
|---|---|
| `src/tiers.h` | the rules alone: steps, prices, experience, damage, ammo, tints, names, the enemies' curve |
| `src/save_book.h` | the book of saves (`tiers.txt`'s records) and which save a moved clock means |
| `src/save_tiers.h` | the tiers in play, the clock's watch, the file, a party's levels |
| `src/discs.h` | the game's addresses per disc (one table) and its records' layouts, the row text fields among them |
| `src/game_view.h` | typed reads and writes of the game: rows, items, hero, vendor, save, texts and text slots |
| `src/weapons.h` | which weapon, which row, which tier: what every other part asks |
| `src/text.h` | the text resolver behind `rac2.text.by_id` |
| `src/vendor.h` | the vendor's entries, prices, purchases and tints |
| `src/experience.h` | steps by experience, the level-up, the rows' ammo and bar, the line at the top |
| `src/combat.h` | the tiers' damage and the enemies across new games |
| `src/bolt_rules.h` | the bolt rules alone: the run scale, toughness, the platinum share and the split into coins |
| `src/platinum_bolts.h` | platinum and obsidian bolts and bolt scaling behind the bolt hook points |
| `src/collectors.h` | Tabora's and Grelbin's collectors' pay and dialogs |
| `src/bolt_api.h` | interface 1.23's `moby_material` (and 1.24's obsidian), read from the port's table while the mods build against SDK 1.20 |
| `src/ng_plus_plus.cpp` | the entry point: the disc, the state, the hooks and the tick |

The tests: `ng_plus_plus_rules` (the rules and the book), `ng_plus_plus_bolt_rules` (the bolt
rules), `ng_plus_plus_text` (the resolver's substitution) and `ng_plus_plus_load` (the built library
against a game of the test's own, on the three discs, with a port of 1.24 that draws obsidian, one
of 1.23 that has the bolt hook points and platinum, one of 1.22 without, and the bolt setting off).

Not yet:

- The HUD's weapon icon draws its own colour and keeps the game's.
- The vendor's upgrade entry is the game's own kind of entry: its icon is the game's. Its label is
  the tier's only on NTSC-U v1.01 (the one disc with `rac2.vendor.draw_list`).
- A text longer than the port's text slot (95 characters) once the tier's name is in it stays the
  game's.
- Damage from a source `kHitSources` does not list scales by the weapon in Ratchet's hand: a
  Synthenoid's or a turret's shots, the Chopper, Pulse Rifle, Plasma Coil, Hoverbomb and the rest
  whose moby classes are not yet traced to their hit code, scale by the weapon held when they hit.
- The game's health drains (`weapon_start_health_drain`, the drain slots) take health straight off
  an enemy without posting a hit, so neither the tier nor the enemies' health applies to them.
- A platinum or obsidian bolt's extra worth goes to your bolts, not to the level's bolt score the
  game keeps for its own pacing. If the port can't draw a coin as platinum or obsidian (over 1,024
  at once), its extra worth is added when it drops.
- No extra enemies yet: Going Commando has no table of which enemies a level's scripts need (waves,
  arenas, doors that open when all are defeated), so copies could stall a level; the difficulty
  mod leaves the enemy count out of Going Commando for the same reason.
- No fire rate change, and RYNO II Ultima's split volley is a follow-up: Ultima is the damage boost.
