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

### How each weapon looks

Each tier also changes how the weapon looks when you use it. The gun, and everything it fires or
puts down (shots, bombs, blasts, bots, turrets, the shield), takes the tier's color: the model keeps
its own colors with a touch of the tier's and gets a glowing edge in it, and its glow and the
effects tied to it (sparks, flashes, arcs, trails, halos) are drawn in the tier's color instead of
the game's. Each tier is a little stronger than the one before: the effects are brighter and the
sparks and flashes larger, up to about half again as bright and a third larger at Omega. Omega's
effects cycle through the rainbow, like its icon. Heavy RYNO II glows orange and Ultima RYNO II
beats between bright and deep red.

This is only how things look: shots fly, spread and hit exactly as at the tier's damage.

| Weapon | What takes the tier's color |
|---|---|
| Lancer / Heavy Lancer | the gun's glowing edge, the halo in its chamber and the flashes tied to the gun |
| Gravity Bomb / Mini-Nuke | the bomb in flight (its body and glow) and its blast |
| Chopper / Multi-Star | the spinning blades, body and glow |
| Blitz Gun / Blitz Cannon | the gun's glowing edge and the effects tied to it |
| Plasma Coil / Plasma Storm | the gun's glowing edge and the effects tied to it |
| Lava Gun / Meteor Gun | the gun, and the burning blobs it leaves on the ground |
| Minirocket Tube / Megarocket Cannon | the tube, and each homing rocket (its body, glow and trail) |
| Spiderbot / Tankbot Glove | the bot itself (body and glow) and its exploding trail |
| Bouncer / Heavy Bouncer | the gun, and the bouncing bomblets with their blasts |
| Miniturret / Megaturret Glove | each turret you put down (body and glow) |
| Hoverbomb / Tetrabomb Gun | the gun, and each hoverbomb (body and glow) |
| Shield Charger / Tesla Barrier | the shield: its electric arcs and sparkles. It no longer turns green for good at the Mega: Alpha is lime, Beta cyan, Gamma emerald, Delta teal, Epsilon violet, Zeta magenta, Eta crimson, and Omega's shield cycles through them all |
| Zodiac | the gun's glowing edge and the effects tied to it |
| RYNO II | the launcher's glowing edge and the effects tied to it (orange at Heavy, beating red at Ultima, then the tiers' colors) |
| Seeker Gun | the gun, and each seeker (body, glow and trail) |
| Pulse Rifle / Vaporizer | the gun's glowing edge and the effects tied to it |
| Visibomb Gun | the gun, and the missile you steer (body and glow) |
| Bomb Glove | each bomb (body and glow) |
| Decoy Glove | the glove, and the decoy |
| Synthenoid | each drone (body and glow) |
| Tesla Claw, Walloper, Sheepinator, Clank Zapper | the weapon's glowing edge and the effects tied to it |

Some of a weapon's effects are drawn by the game on their own, not tied to the gun or its shot
(the trail of a stray spark, a ricochet): those keep the game's colors.

This needs OpenRAC with mod interface 1.25. With 1.23 or 1.24 only the bodies of the shots, bombs,
bots and turrets take the tier's color, and with an older OpenRAC everything looks as in the game.

### Tougher enemies each new game

The tiers make weapons much stronger, so enemies get tougher across challenge mode's new games. The
game itself counts your challenge modes but only checks whether one is on: it doubles ammo pickups
and nanotech orbs' health there, and its bolt multiplier grows with kills. It never makes enemies
tougher. From the second challenge mode on, the mod does:

| New game | Challenge mode | Enemy health | Boss health (on top) | Enemy damage | Tier you should have by then | Hits to kill with it, vs V4 in the 1st | Hits vs 0.4.0 |
|---|---|---|---|---|---|---|---|
| 1 | - | x1 | x1 | x1 | V1 to V3 | | |
| 2 | 1st | x1 | x1 | x1 | V4, then Alpha to Gamma | x1 (V4), x0.67 to x0.4 | the same |
| 3 | 2nd | x3.75 | x2 | x1.375 | Delta (x3) | x1.25 | +50% |
| 4 | 3rd | x6 | x2.25 | x1.75 | Zeta (x4.5) | x1.33 | +50% |
| 5 | 4th | x9 | x2.5 | x2.125 | Omega (x7) | x1.29 | +64% |
| 6 | 5th | x9.75 | x2.75 | x2.5 | Omega (x7) | x1.39 | +63% |
| 7 | 6th | x10.25 | x3 | x2.875 | Omega (x7) | x1.46 | +58% |
| 8 on | 7th on | x10.5 | x3 | +0.375 each, up to x4 | Omega (x7) | x1.5 | +56% |

Since 0.4.1 enemies outgrow your tiers: even with the tier you should have by then, a fight takes a
quarter to a half more hits than one with V4 in the first challenge mode, and the later runs are
clearly harder, Omega or not (health stops at x10.5, half as many hits again as Omega's x7). A
weapon below the tier you should have takes even more. Enemy damage grows more slowly, since longer
fights already cost you more health. **How much tougher** scales what the mod adds (0.5: x2.375
health in the third new game instead of x3.75).

**Bosses** have the boss column's health on top: x2 in the second challenge mode, a quarter more
each new game, up to x3, so with Omega in the 6th challenge mode a boss takes about 4.4 times the
hits V4 needed in the first. **The final boss** (the mutated Protopet on Yeedil) has half as much
again on top of that (x3 to x4.5). The bosses: the Swamp Monster (Oozla), Chainblade and the B2
Brawler (Maktar arena), the bosses of Endako, Siberius, Hrugis Cloud, Todano and Snivelak, the Joba
arena bosses, the Thugs-4-Less leader, the leader's mech on Damosel, the mothership (Damosel orbit)
and the mutated Protopet. A boss whose fight counts hits rather than damage is not changed.

**The RYNO II stays the strongest weapon.** Enemy health doesn't apply to it: ordinary enemies take
its hits as the game made them (its tiers on top), so whatever it killed in one shot it still does.
Tough enemies, bosses and the final boss do hold out against it, from the second challenge mode on,
whatever the RYNO's tier:

| New game | Tough enemy (110 bolts or more) | the biggest (984 bolts) | Boss | Final boss |
|---|---|---|---|---|
| 3 (2nd challenge mode) | x2 | x6 | x3 | x4.5 |
| 4 (3rd) | x2.5 | x7.5 | x4 | x6 |
| 5 (4th) | x3 | x9 | x5 | x7.5 |
| 6 (5th) | x3.5 | x10.5 | x6 | x9 |
| 7 on (6th on) | x4 | x12 | x7 | x10.5 |

These are times the hits the game's own first RYNO II needs. A tough enemy is one of the top quarter
by its own bolts (the level data's measure of how big it is: 110 or more on Going Commando's disc),
and a bigger one holds out longer, by the square root of its bolts over 110, at most three times.
On ordinary enemies the RYNO II's Omega still hits over eight times as hard as the game's RYNO II in
every run, four times what any other weapon's Omega keeps of its hit after enemy health.

**Weapon experience keeps pace.** Since 0.4.1 a kill gives more weapon experience each new game, so
a tier takes about as long to level as Beta did in the first challenge mode, despite the extra hits:
x1.5, x2.5, x3.5 and x4.5 from the second challenge mode on (the 5th on stays at x4.5). On top of
that a bigger enemy gives a little more, the fourth root of its own bolts over 20 (45 bolts: x1.22,
320: x2, at most x2.45), a boss x5 and the final boss x10. The game's own experience already grows
with an enemy, and the share it gives your other weapons grows the same way. The RYNO II keeps its
five times slower goals.

The tier you level by experience in each new game, with the kills it takes counted against the
100 kills that Beta takes in the first challenge mode (same enemies; small fry have 20 bolts or
fewer, a common enemy 45):

| New game | Tier by experience | Its goal | Kills before | Kills now (small fry / common) | Hits a kill takes, vs the 1st | Play time now, vs Beta in the 1st |
|---|---|---|---|---|---|---|
| 2 (1st challenge mode) | Beta | 8,000 | 100 | 100 | x1 | x1 |
| 3 (2nd) | Delta | 12,000 | 150 | 100 / 82 | x1.25 | x1.25 / x1.02 |
| 4 (3rd) | Zeta | 16,000 | 200 | 80 / 65 | x1.33 | x1.07 / x0.87 |
| 5 (4th) | Omega | 24,000 | 300 | 86 / 70 | x1.29 | x1.10 / x0.90 |
| 6 (5th) | Omega (another weapon) | 24,000 | 300 | 67 / 54 | x1.39 | x0.93 / x0.76 |
| 7 (6th) | Omega | 24,000 | 300 | 67 / 54 | x1.46 | x0.98 / x0.80 |
| 8 on | Omega | 24,000 | 300 | 67 / 54 | x1.5 | x1 / x0.82 |

Before 0.4.1 the same steps took 1.9, 2.7, 3.9, 4.2, 4.4 and 4.5 times as long as Beta in the first.

(0.4.0's enemy health went x2.5, x4, x5.5, x6, x6.5, then x6.75, and divided the RYNO II's hits too,
with no boss extra. Before 0.4.0 it went x2, x3, x4, x5, then +1 a game, and enemy damage x1.25,
then +0.25 a game. Before 0.2.1 the tiers gave x1.25 to x4 and enemy health went x2.5, x4, x6, x8,
+2 each.)

| Setting | Default | |
|---|---|---|
| Tier names on screen | on | The weapon's name at the top of the screen |
| Tougher enemies each new game | on | The table above |
| How much tougher | 1 | Multiplies what the table adds, 0 to 3 |
| Bolt scaling | normal | Platinum and obsidian bolts, bigger drops and the collectors' pay (below): off, normal or generous |
| Extra enemies | on | Copies of each enemy from the second challenge mode (below) |

With the [difficulty](../difficulty-mod/README.md) mod, both apply and **multiply**: its "damage
dealt" and "damage taken" sliders scale the same hits as this mod's tiers and enemies, so with
damage dealt x0.5 and a Beta weapon in the second challenge mode a hit does x0.5 x 2 / 2. Turn
**Tougher enemies each new game** off to leave the enemies to the difficulty mod alone.

### More enemies each new game

From the second challenge mode on, the enemies of the table below get a copy beside each of them
when the level loads, and two from the fourth challenge mode (one for the big ones):

| Challenge mode | 1st | 2nd and 3rd | 4th on |
|---|---|---|---|
| Copies of each enemy of the table | none | 1 | 2 (1 for those marked 1) |

A copy stands on its enemy's floor, in the open, 2.5 to 4 units away, and wakes, runs, fights and
dies with its squad. It drops the bolts that enemy drops, once (with the run's bolt scaling,
platinum and obsidian bolts), counts for your kill streak, and is gone when it dies as the enemy it
copies is; killing it never changes what the game remembers of that enemy (a killed enemy stays
killed, and its bolts do not fall off any faster).

**Why a table.** 0.4.1 copied every enemy OpenRAC allowed, and most copies never woke: a squad's
enemies sleep until the squad is woken through its list of members, which the copies were not on,
so they stood undrawn and unhittable where they were made, and the ones you saw "dead" were never
alive. OpenRAC now puts a copy on its squad's list (mod interface 1.26), and NG++ copies only the
enemies a hidden test run of their planet saw copied, woken with their originals, hit, killed,
dropping their bolts once and gone. Copies of an enemy that holds a part of its own (a gun or a
head made as a second moby) are never made: OpenRAC refuses them (Todano's and Dobbo's guards hung
or crashed the game). Bosses, turrets, spawners, the waves', arenas' and doors' enemies, Tabora's
and Grelbin's crystal and moonstone creatures and the Y.E.T.I.s are never in the table, and
OpenRAC refuses them too.

| Planet (level) | Enemy | Copies of each, at most |
|---|---|---|
| Aranos (0) | enemy (class 2460) | 2 |
| Aranos (0) | enemy (class 2826) | 2 |
| Aranos (0) | enemy (class 2827) | 2 |
| Oozla (1) | Mutant Swamp Beast | 2 |
| Oozla (1) | Mutant Firefly | 2 |
| Oozla (1) | Mutant Muckdweller | 2 |
| Maktar Nebula (2) | enemy (class 2501) | 2 |
| Maktar Nebula (2) | Megacorp Chickenbot | 2 |
| Endako (3) | Megacorp Rivet Bot | 2 |
| Endako (3) | Megacorp Laser Bot | 2 |
| Barlow (4) | enemy (class 2234) | 1 |
| Barlow (4) | Gadgetron Hound of Cuddly Death | 2 |
| Notak (6) | enemy (class 3002) | 2 |
| Notak (6) | enemy (class 3504) | 2 |
| Siberius (7) | enemy (class 3369) | 2 |
| Siberius (7) | enemy (class 3370) | 2 |
| Dobbo (9) | enemy (class 3306) | 1 |
| Dobbo (9) | Megacorp Trooper (class 3343) | 1 |
| Dobbo (9) | enemy (class 3415) | 2 |
| Dobbo (9) | enemy (class 3472) | 2 |
| Joba (11) | Joba Tribesman | 1 |
| Joba (11) | Jobian Saur-Beast | 1 |
| Todano (12) | Mutant Test-Squirrel | 2 |
| Boldan (13) | enemy (class 4587) | 2 |
| Aranos's prison (14) | enemy (class 4395) | 1 |
| Aranos's prison (14) | enemy (class 4396) | 1 |
| Smolg (17) | Grulch | 2 |
| Damosel (18) | Protopet | 2 |
| Yeedil (20) | Protopet | 2 |
| Yeedil (20) | Receiver Bot | 2 |
| Jamming array (26) | Megacorp Chickenbot | 2 |

Each was tried in hidden runs of its planet (OpenRAC's `OPENRAC_PLAY_TRACE=copies=1,copywatch,copykill=F,origkill=F`,
Ratchet placed beside the enemies at one to three spots): its copies woke when their originals did,
took hits, died, dropped their bolts once and were gone. Left out on purpose: Endako's Sweeper Bots
(they hide and come back by design), Siberius's class 3357 (the originals die twice), Todano's
guards (class 3889: they hold a part of their own and froze or crashed the game; OpenRAC refuses
such copies now), and the enemies that woke with their originals but whose copies no test hit
killed (Boldan's and Snivelak's classes 3747 and 3748, Damosel's class 4440, Grelbin's Protopets,
Yeedil's Hover Tank) or that never woke where they were tried (Tabora's Dune Crawlers and Sandflies,
Grelbin's Receiver Bots, the Aranos prison's class 2484, Yeedil's class 4847).

In a crowded level some enemies get fewer copies: OpenRAC keeps room for what the level spawns, and
has 64 KB for the copies' state past what a spawned moby holds (Barlow's 91 hounds fill it with one
copy each). Every enemy's first copy is made before any second one.
In a multiplayer party only the host's game copies enemies. Turn **Extra enemies** off to play
with the game's own enemies. This needs OpenRAC with mod interface 1.26; an older one (1.25's
copies never woke) plays without copies and says so once in the log.

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

**Bigger drops.** The game works out a drop as always: the enemy's bolts, your kill streak up to
x20, challenge mode's doubling, its pacing and its limit of three pickups for a kill (at most 3,000
bolts; what doesn't fit goes to the game's reserve divided by your streak and trickles into later
drops, as in the original). Then the mod multiplies what the pickups hold by the new game you are
in, the same steps as the tier prices (x1.5, x2.5, x4, x6), so the tier you are saving for takes
about as many kills in every new game:

| New game | Challenge mode | Bolts (normal) | Bolts (generous) | Platinum at x1 streak | Platinum at x20 streak |
|---|---|---|---|---|---|
| 1 | - | x1 | x1 | none | none |
| 2 | 1st | x1 | x1 | 20% | 40% |
| 3 | 2nd | x1.5 | x2.25 | 10% | 50% |
| 4 | 3rd | x2.5 | x3.75 | 15% | 65% |
| 5 | 4th | x4 | x6 | 20% | 80% |
| 6 on | 5th on | x6 | x9 | 25% | 90% |

**Every enemy pays its own.** Each enemy has its own bolts in the level's data (the game's drop for
it, before your streak): on the disc, crates and small fry have under 20, the commonest enemies
around 45, the bigger ones 100 to 500 and the biggest up to 984. The game pays in proportion to
those up to its 3,000 limit; the mod pays a bigger enemy a little more on top of the run scale: x1
up to 20 bolts, then the square root of its bolts over 20, up to x2 (45 bolts: x1.5, 80 or more:
x2). The run scale and toughness together never pass x12 (generous included). A drop also gets more
pickups the tougher the enemy: two more than the game's three per x1 of toughness (5 to 7). What
the pickups can't hold is added to your bolts at once.

What one kill pays at a x20 streak (challenge mode's doubling and the game's limit included, before
its roll of about a quarter up or down):

| Enemy's own bolts | 3rd game (2nd challenge mode) | 4th (3rd) | 5th (4th) | 6th on (5th on) |
|---|---|---|---|---|
| 8 (small fry) | 450 | 750 | 1,200 | 1,800 |
| 45 (common) | 3,600 | 6,000 | 9,600 | 14,400 |
| 110 or more | 9,000 | 15,000 | 24,000 | 36,000 |

**Why 0.4.4 asks for more room.** The game makes every spawned moby (a bolt, a shot, an effect, an
enemy copy) from a pool of spare slots its level's file sets, 200 to 400 (Barlow's is 398), and a spawn
with none free is lost, a bolt with it. Barlow's 79 copies at the fourth challenge mode took a fifth of
the pool before the level spawned anything, and a long streak's drops fill the rest: in a hidden run a
flood of 300 pickups there made 228. From 0.4.4 NG++ asks OpenRAC (mod interface 1.27) for 400 more spare
slots in every level, and keeps to what is free:

- A burst makes at most 24 coins and the level at most 96 bolt pickups alive (fewer when the pool has
  fewer than 24 slots free), the most valuable coins first. What no coin holds is paid at once, so the
  bolts of a drop never change: a 256,000-bolt drop made 66 coins in 0.4.3 and 24 in 0.4.4, and all
  256,000 were collected. The log says when a drop was folded.
- A copy is made only while 160 spare slots are free, so the bolts and the level keep their room. The
  copies a load missed are tried again, up to three times a second apart. The log says how many were
  made and left out, with the pool beside.
- An OpenRAC older than 1.27 keeps the game's pool: the same caps hold by counting the pickups alive.

**Why 0.4.2 pays less.** 0.4.1 multiplied the amount the game was asked for, streak included, by
the run scale and a toughness of up to x6, and lifted the game's three-pickup limit, so the streak,
the run and toughness all multiplied each other: a 250-bolt enemy at x20 paid 53,034 in the second
challenge mode (now 9,000) and the biggest 1,416,960 in the fifth (now 36,000), about 2 million
bolts in five minutes of fighting. Now the streak only counts as far as the game's own limit, and
the mod scales what the game paid.

0.4.1 also fed itself: a drop of hundreds of coins was more than the game can make, and each coin it
could not make went into the game's word of owed bolts (it saves with the game), of which every kill
takes 2% back into its drop, which 0.4.1 then scaled again. A save played with it can hold millions
there, which is how ordinary kills at a x1 streak paid 60,000 to 90,000 bolts. Now what a kill takes
from the word is paid as it is, never scaled; under the game's three-coin limit that is at most
3,000 bolts a kill until the word is spent, as in the game itself. A word over 100,000 (the game's
own drops leave a few thousand there at most) is cut back to 100,000 when the game runs, and the log
says so once.

The log shows the first five drops one by one, then about once a minute a line with every bolt the
mod paid since: the drops, how much of them came from the game's word, the biggest, what was paid at
once and the extra worth of the platinum and obsidian coins collected.

Bolts per minute of fighting, modelled on a typical planet: 6 kills a minute, 35% small fry (12
bolts), 40% common (45), 15% at 110, 8% at 250 and 2% at 600, against one tier of a weapon with a
600,000 Mega (the Mega itself in the 1st challenge mode, then Mega x1.5, x2.5, x4, x6). The target
is that each run's tier takes about as long as the Mega did in the 1st challenge mode, the original
game's pace; enemies taking more hits each run (above) slow the kills, so the real minutes are a
little longer:

| Challenge mode | Tier price | x10 streak: 0.4.1 | 0.4.2 | x20 streak: 0.4.1 | 0.4.2 |
|---|---|---|---|---|---|
| 1st (the game's) | 600,000 | 6,100/min, 98 min | the same | 9,400/min, 64 min | the same |
| 2nd | 900,000 | 37,100/min, 24 min | 16,300/min, 55 min | 74,300/min, 12 min | 24,200/min, 37 min |
| 3rd | 1,500,000 | 61,900/min, 24 min | 27,200/min, 55 min | 124,000/min, 12 min | 40,400/min, 37 min |
| 4th | 2,400,000 | 99,000/min, 24 min | 43,500/min, 55 min | 198,000/min, 12 min | 64,600/min, 37 min |
| 5th on | 3,600,000 | 149,000/min, 24 min | 65,200/min, 55 min | 297,000/min, 12 min | 97,000/min, 37 min |

**Platinum share.** The longer your kill streak, the more of a drop comes as platinum, from the
"x1" column at no streak up to the "x20" one at a full x20 streak, in even steps between. It changes
how the bolts come, not how many: a drop of 15,000 at 65% is a platinum 5,000, a platinum 2,500,
four platinum 500s and a platinum 250 (9,750), and 5,250 in gold (as many as the pickups hold, the
rest added at once). Fewer, bigger pickups.

**Obsidian share.** From the second challenge mode on, obsidian bolts show up now and then as rare
hints, mostly at long kill streaks: the share grows slowly with your streak at first and fast near x20, and grows
with each new game. Obsidian takes its part of a drop first, then platinum its share, then gold.

| New game | Challenge mode | Obsidian at x1 streak | at x10 | at x20 streak |
|---|---|---|---|---|
| 1 and 2 | - and 1st | none | none | none |
| 3 | 2nd | none | 1% | 5% |
| 4 | 3rd | 1% | 3% | 8% |
| 5 | 4th | 2% | 4% | 12% |
| 6 on | 5th on | 3% | 6% | 15% |

Within a drop's pickups each rarer kind keeps a place, so a small drop can still show an obsidian
or platinum bolt. The biggest obsidian bolt a kill shows is a 2,500, from the fourth challenge mode
at long streaks.

**Collectors.** Tabora's crystals and Grelbin's moonstones pay the same x1.5, x2.5, x4 and x6 from
the second challenge mode (generous half as much again), so collecting keeps up with the tier
prices. The collector's dialog shows the total you will get.

**Prizes and prices.** From the second challenge mode the hoverbike races on Barlow and Joba, the
space missions of Feltzin, Hrugis and Gorn (since 0.4.3) and the arena challenges on Maktar and
Joba pay the same x1.5, x2.5, x4 and x6 (generous half as much again), and the characters who sell
for bolts ask half that rise: x1.25, x1.75, x2.5 and x3.5 (generous x1.63, x2.38, x3.5, x5). The
race menu, the mission's reward panel, the arena menu and each seller's dialog show the new amount,
and the log says once a level what its races or missions pay ("level 5: the space missions pay
x4.00 (the second mission 5500 -> 22000)", before the game's own x10 in challenge mode). The weapon vendor's prices are the tiers' own and stay as they are.

| | First challenge mode (the game's) | 2nd (x1.5; prices x1.25) | 3rd (x2.5; x1.75) | 4th (x4; x2.5) | 5th on (x6; x3.5) |
|---|---|---|---|---|---|
| Barlow races, first win | 0 (the story race), 20,000, 30,000, 40,000, 60,000 | 30,000 to 90,000 | 50,000 to 150,000 | 80,000 to 240,000 | 120,000 to 360,000 |
| Barlow races, second win | 2,500, 7,500, 10,000, 12,000, 20,000 | 3,750 to 30,000 | 6,250 to 50,000 | 10,000 to 80,000 | 15,000 to 120,000 |
| Barlow races, later wins | 2,500, 2,500, 3,500, 4,000, 7,500 | 3,750 to 11,250 | 6,250 to 18,750 | 10,000 to 30,000 | 15,000 to 45,000 |
| Joba races, first win | 0, 40,000, 50,000, 60,000, 80,000 | 60,000 to 120,000 | 100,000 to 200,000 | 160,000 to 320,000 | 240,000 to 480,000 |
| Joba races, second win | 4,000, 12,000, 17,500, 20,000, 27,500 | 6,000 to 41,250 | 10,000 to 68,750 | 16,000 to 110,000 | 24,000 to 165,000 |
| Joba races, later wins | 4,000, 4,000, 6,000, 7,500, 9,000 | 6,000 to 13,500 | 10,000 to 22,500 | 16,000 to 36,000 | 24,000 to 54,000 |
| Maktar arena (first win) | 15,000 to 90,000 | 22,500 to 135,000 | 37,500 to 225,000 | 60,000 to 360,000 | 90,000 to 540,000 |
| Joba arena (first win) | 50,000 to 125,000, and 2,000,000 | 75,000 to 187,500, 3,000,000 | 125,000 to 312,500, 5,000,000 | 200,000 to 500,000, 8,000,000 | 300,000 to 750,000, 12,000,000 |
| Notak's toll door | 6,000 | 7,500 | 10,500 | 15,000 | 21,000 |
| Dobbo's seller | 10,000 | 12,500 | 17,500 | 25,000 | 35,000 |
| Aranos prison's seller | 20,000 | 25,000 | 35,000 | 50,000 | 70,000 |
| Smolg's machine | 40,000 | 50,000 | 70,000 | 100,000 | 140,000 |

An arena challenge won before pays less each time (the game's 0.3 per win, at least 5,000 in
challenge mode), and the scale applies on top. The gadget sellers (Oozla's Tractor Beam, Barlow's
Thermanator, Joba's Levitator, Damosel's Hypnomatic) ask half the rise too, should one still be
selling. The arenas need an OpenRAC with the arena hook point; without it they pay as the game does
and the rest works.

A drop is always the game's own pickups after its roll, kill streak, doubling, pacing and limit,
times the scale. The first game is left alone; the first challenge mode gets a few platinum bolts,
a taste of them, with drops the same size. **Bolt scaling** off leaves every drop to the game;
generous pays half as much again as normal (the table's generous column, up to the x12 cap), with
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

Through OpenRAC's rac2 hook points of SDK 1.14.6, and the bolts' of interface 1.23, the obsidian
material of 1.24 and the energy material of 1.25 (the addresses are NTSC-U v1.01's):

| | Where |
|---|---|
| For sale | `rac2.vendor.build_list` (`func_002F6608`): an upgrade entry for each weapon whose next tier is for sale, right after that weapon's own entry and built as the game builds its Mega entry; `rac2.vendor.price` (`func_002F6510`) prices it; `rac2.item.upgrade` (`func_002887B0`), which the vendor's purchase calls once it is paid, takes the tier instead of the game's Mega step |
| Experience | each tick, a weapon at its last version whose experience (which the game keeps adding up there) reaches the tier's takes it; its experience starts again from 0, as the game's own Mega purchase does |
| Experience bar | the last version's row (+0x6C, the experience to the next version, which the game leaves at 0 there: a full bar) set each tick to the next tier's goal, and back to the game's when none is left; the game's own level check skips every last version, so it never acts on it |
| Damage | `rac2.combat.post_hit` (`func_002A8E20`): a hit posted to anyone but Ratchet has its damage (+0x1C) scaled for that call by the tier of the weapon that dealt it: the weapon whose own moby is the request's source (+0x10, its class at +0xAA; `kHitSources` in src/discs.h: the Lancer, Blitz Gun and Cannon, Lava Gun and its pools, Seeker and Minirocket shots, Gravity Bomb blasts, Bouncer blasts, Spiderbot trails, Shield Charger and Tesla Barrier, Tesla Claw), else the weapon in Ratchet's hand |
| Max ammo | the last version's row (+0x8E) in the item table, rewritten each tick from what the game (or another mod) has there; a tier fills the ammo |
| Enemies | the save's challenge mode counter (+0x12 of its block); their health as the damage they take in `rac2.combat.post_hit` divided by the curve (a boss's, by the victim's class at +0xAA, by the boss column too, the final boss's by 1.5 more; the RYNO II's hits by its own rule, its tough enemies by their own bolts at +0xB4), the damage they deal in `rac2.player.take_damage` (`func_L00_002A4EF8`) times it, a fraction of a point carried to the next hit |
| Weapon experience | `rac2.moby.record_death` (`func_002A6C58`, `SetDeathBits`): every weapon's experience (the s32 table at `D_00139868`, << 5) read before the game's own award and after, each gain times the run's rate and the moby's (a boss by its class at +0xAA, an enemy's own bolts at +0xB4) |
| Color | `rac2.item.colors` (`func_00336F20`): the quick select's and the weapons screen's icons and names; `rac2.vendor.draw_list` (`func_002F9C80`) and `rac2.item.version_color` (`func_00336EF8`): the tier for sale's icon in the vendor's list (an OpenRAC without these two keeps the game's gold there) |
| Bolts | `rac2.bolt.spawn_pickups` (`func_002ADB78` and its copy in every level's code): the game's burst runs as it is (its amount, flags and coin limit: flag 8, `SetDeathBits` asks for three coins a kill, the rest going to the game's word at +0x1C of the save block, `D_001A7A14`, divided by the streak); the coins it makes through `rac2.bolt.spawn_pickup` (`func_00302470`) are only written down (worth, start, speed), and their total, from the second challenge mode times the run scale and the enemy's toughness (its own bolts, s16 at +0xB4, when its class record at +0x24 says kind 5 at +0x46, as `SetDeathBits` checks), at most x12, is made again as obsidian, platinum and gold coins at those places (a coin the game can't make, which it puts in its word, is taken back out and paid at once), the rarer ones drawn with `moby_material` (platinum from interface 1.23; obsidian from 1.24, its glow colour per class in the call's rgb); `rac2.bolt.collect` (`func_00301D10`) adds four times a platinum coin's class (24 times an obsidian one's) after the game's own and takes its look off. The kill streak is the byte at +0x3A of the save block (`D_001A7A32`); the mod's own coin limit is the game's plus two per x1 of toughness, and what it leaves goes to the bolts at once through `openrac_api.bolts` (in the first challenge mode, unscaled, to the game's word divided by the streak, as the game does) |
| Collectors | no hook point: their dialogs' texts (10135 Tabora's crystals, 10209 Grelbin's moonstones) through `rac2.text.by_id` get the scaled total in place of their second number; each tick a trade (the crystals at +0x22 of the save block, s16, or the moonstones at +0x24, s32, down to 0 in Tabora or Grelbin with the game's pay, 1,000 or 3,000 each, ten times in challenge mode, in the bolts at +0x00) gets the rest of the scaled pay through `openrac_api.bolts` |
| Races | no hook point (the race's panel, `func_L04_0040AD28`, and its finish, `race_update_player_progress`, read the prize straight from the table): while Barlow or Joba is loaded, each race's three prizes (+0x08, +0x0C, +0x10 of its 0x40 bytes in the level's race table, `D_L04_002C63C0`, `D_L11_002B6D40` in Joba) are written at the run scale, the game's kept and written back when the run pays as the game does; the game's x10 in challenge mode comes on top |
| Arenas | `rac2.arena.prize` (`func_L02_003FCDF8`, `moby_compute_scaled_entry_value`, one function in Maktar's and Joba's code): the game's prize times the run scale; the arena's menu prints it and the win pays it |
| Sellers | no hook point (each seller's dialog prints the price it then takes, both from the level's data): the bolt prices of the level's price table (`D_L06_002958D0`, `D_L09_0028E350`, `D_L14_00291850`, `D_L17_0028C550`: the game's and challenge mode's price of Notak's toll door, words 0-1, Dobbo's seller 4-5, the prison's seller 10-11, Smolg's machine 12-13; the crystals and moonstones of Tabora and Grelbin left alone) and the gadget sellers' item row (+0x80 of the item's row) are written at half the run scale's rise while their level is loaded, the same way as the races |
| Level-up popup | a tier earned with the weapon in hand plays the game's in-hand level-up (`func_L00_0037E6A8`); at its end the game's popup from its own level-up table (`help_show_message_for_id`, `func_00288508`) names the last version, or is text 0 (an empty box) for a weapon the table has no line for (RYNO II, Zodiac): for ten seconds after, the resolver gives that popup the tier's name |
| Weapon looks | each tick, the level's moby table (the first moby's and past the last's words, `g_mobyTableBase` and `g_mobyTableEnd`; 0x80 and up at +0x20 a dead slot) is looked through for the weapons' own classes (`kVisualClasses` in src/weapon_visuals.h: guns, shots, bombs, blasts, bots, turrets, the shield, each with its item); a moby whose weapon wears a tier is drawn with `moby_material`'s energy (interface 1.25) at the tier's strength in the tier's color (`effect_rgb`, `effect_strength` in src/tiers.h), sent again only when it changes (Omega and Ultima every other tick). OpenRAC draws the body with a touch of the color and a glowing rim, and the moby's glow packets, its draw callbacks' quads and the particles naming it as their owner in the color. On a port of 1.23 or 1.24, a tint on the shots', bombs' and bots' bodies only |
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
| `src/weapon_visuals.h` | the tiers' looks on the weapons' mobys: which classes, which look, sent each tick |
| `src/text.h` | the text resolver behind `rac2.text.by_id` |
| `src/vendor.h` | the vendor's entries, prices, purchases and tints |
| `src/experience.h` | steps by experience, the level-up, the rows' ammo and bar, the line at the top |
| `src/combat.h` | the tiers' damage and the enemies across new games |
| `src/bolt_rules.h` | the bolt rules alone: the run scale, toughness, the drop's scale and extra coins, the platinum and obsidian shares and the split into coins |
| `src/platinum_bolts.h` | platinum and obsidian bolts and bolt scaling behind the bolt hook points |
| `src/collectors.h` | Tabora's and Grelbin's collectors' pay and dialogs |
| `src/rewards.h` | the races' and arenas' prizes and the sellers' prices, and their tables per disc |
| `src/bolt_api.h` | interface 1.23's `moby_material` (and 1.24's obsidian), read from the port's table while the mods build against SDK 1.20 |
| `src/ng_plus_plus.cpp` | the entry point: the disc, the state, the hooks and the tick |

The tests: `ng_plus_plus_rules` (the rules and the book), `ng_plus_plus_bolt_rules` (the bolt
rules), `ng_plus_plus_text` (the resolver's substitution) and `ng_plus_plus_load` (the built library
against a game of the test's own, on the three discs, with a port of 1.25 that draws energy, one of
1.24 that draws obsidian, one of 1.23 that has the bolt hook points and platinum, one of 1.22
without, and the bolt setting off; the weapons' looks: which moby gets which material and color at
a tier, a dead slot, another class and a level left).

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
- The weapons' looks change only how things are drawn: shots are no faster and blasts reach no
  farther at a higher tier (the larger sparks and flashes are drawn larger, nothing more). The
  game's item rows keep the shots' speed and size in fields that mean different things for each
  weapon, so changing them waits until each weapon's fields are traced.
- An effect the game draws on its own, not tied to the gun or the shot that made it, keeps the
  game's colors, and so do a few weapons' shots whose moby classes are not yet traced (the Pulse
  Rifle's and the Plasma Coil's bolts among them, if they are mobys of their own).
