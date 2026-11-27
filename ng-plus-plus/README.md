# NG++

New Game++ for Going Commando, for OpenRAC's native port (NTSC-U, PAL and NTSC-U v2.00 discs):
eight more upgrades for every weapon after its last version, Alpha to Omega (V5 to V12). The game's
own versions stay as they are; NG++ adds its tiers on top and keeps track of them itself.
Version 0.5.0 adds weapon perks from Gamma on (the Lava Gun becomes the Ice Gun), eight armor tiers,
elite enemies, a modifier for each new game, an extra final RYNO II step (Ultima RYNO II since the path
changed) and health from Omega weapons.
Version 0.5.1 makes the Ice Gun a choice (or a fire path for the Meteor Gun), the Tesla Claw hit
steadily, the Zodiac hurt bosses and the decoy last longer (see "Weapon tuning").
Version 0.5.2 ties the tiers to your saves exactly as the game's own progress is, gives the RYNO II
its new path and prices, and makes Clank's sections fair again (see "What's new in 0.5.2").
Version 0.5.3 makes the PAL and NTSC-U v2.00 discs play as NTSC-U v1.01 does (see "What's new in
0.5.3").
Version 0.6.0 sells the armor tiers at the game's armor vendor and draws each as Carbonox repainted
in its color (see "What's new in 0.6.0").
Version 0.6.1 keeps the RYNO II's homing rockets from hovering, widens the Zodiac's reach and lets
the drones, the shield and the other auto-aiming weapons take enemies off screen (see "What's new
in 0.6.1").
Version 0.6.3 carries a save's tiers over when its clock moved while the mod was not running (see
"What's new in 0.6.3").
Version 0.6.4 makes the Zodiac earn experience from its kills (see "What's new in 0.6.4").
Version 0.6.2 is built against the mod SDK 1.34 and needs OpenRAC with mod interface 1.34 (see
"What's new in 0.6.2").
From the second challenge mode on, enemies get tougher, drops pay more bolts and part of each drop
comes as platinum bolts, worth five times the gold ones, and obsidian bolts, worth 25 times.

With the mod off, the game plays exactly as on the PlayStation 2.

## What's new in 0.6.23

- **The Ice Gun is a stream of liquid nitrogen, not meteors.** Up Your Arsenal's Lava Gun is a
  stream at every version, and its V5 (the Liquid Nitrogen Gun) turns it from a lava stream into a
  blue one that freezes what it hits. NG++ now draws that. The meteors the game fires are no longer
  drawn: each flies as a ribbon of added light through its last points (a white core in a bright
  band in a wide glow, in the tier's ice colour, thinner and dimmer as it ages) with a glow at its
  head, so a held trigger is one continuous blue stream. Where it lands, a cold glow lies on the
  ground for a second (the lava pool's place) and cold puffs are thrown up. An enemy it freezes is
  set in a block of ice: a crystal that fades in over half a second to about half its opacity, with
  cold puffs rising from the enemy as it begins; when the freeze ends, or the enemy dies in it, the
  block shatters into shards that fly out, tumble, fall and fade. The game's meteor still does the
  hitting (its damage, its blast, the tiers' freeze and shatter are as in 0.6.21); only how it
  looks changes. All of it is drawn with the mod SDK's props (OpenRAC with mod interface 1.12 or
  newer; no new OpenRAC is needed) and made procedurally by `tools/make_ice_meshes.py`: nothing is
  copied from any game's files. What Up Your Arsenal does, from its code, is in
  `docs/rac3-liquid-nitrogen-gun.md`. The game's own trails and sparks of the meteor stay, in the
  tier's blue with interface 1.39. Needs no new Going Commando build: the files are in the package's
  `assets` folder.

## What's new in 0.6.21

- **The Ice Gun is ice.** Its tiers didn't act at all: the Meteor Gun's shot is a meteor of its
  own (class 2862) that the mod didn't know as the Lava Gun's, so its hits never froze or slowed
  anything, and its tier's colour was the pale frost of the icon, which left the meteors orange.
  Now every tier freezes, as Up Your Arsenal's Liquid Nitrogen Gun does: a hit freezes the enemy
  solid (it stops moving, attacking and animating, painted in ice with a bright rim), then it is
  slowed for a moment; one killed while frozen or chilled shatters into shards that hurt the
  enemies around it. Alpha freezes for two thirds of a second, Gamma longer, Epsilon a second and
  Omega two (the table is under "The Ice Gun"). With OpenRAC's mod interface 1.39 the gun is
  repainted in pale ice with its glowing lava parts in deep blue, its meteors become shards of ice
  in the tier's blue and their trails, sparks and the fire they leave on enemies come out blue;
  with an older OpenRAC the shots and their effects glow in a clear ice blue (no longer grey).
- **The Bouncer's bomblets fly faster each tier**: 7.5% a tier, 1.6 times the game's speed at
  Omega. They fly the same arc (their fall grows with them), so they still land where the game
  aimed them, and at the top they move less in a tick than their own hit radius, so they never
  skip over an enemy.

## What's new in 0.6.19

- **Only Alpha and up change how a weapon looks.** Heavy, Mega and Ultra (the Zodiac's and the
  RYNO II's) no longer tint the icon or name, recolor the shots, add a glow, change the vendor's
  version colour or touch the weapon's mobys: they look completely stock. Their damage and ammo
  are unchanged.

## What's new in 0.6.18

- **The Zodiac and the RYNO II share one path, XP and purchase in turn all the way.** Heavy (XP),
  Mega (bought), Ultra (XP), Alpha (bought), Beta (XP), Gamma (bought), Delta (XP), Epsilon
  (bought), Zeta (XP), Eta (bought), Omega (XP) and, last, **Ultima** (bought): twelve steps. The
  Zodiac's Alpha is now bought instead of earned (0.6.17 had it by XP) and it gains Ultima Zodiac
  (x8.50 damage, +36 max ammo, black and red). The RYNO II gains Mega RYNO II and Ultra RYNO II
  after Heavy, its Alpha is bought as before, and its Ultima is bought after Omega instead of
  earned. Both cost the same: Mega 6,000,000, Alpha 8,000,000, Gamma 10,000,000, Epsilon
  16,000,000, Eta 25,000,000 and Ultima 40,000,000 (see the tables under "Tiers").
- **Saves keep their tiers.** The Zodiac's numbering is unchanged: a Zodiac at Ultra stays Ultra,
  with Alpha now for sale. The RYNO II's Alpha to Ultima move up two places to make room, so each
  keeps its name; Heavy stays. `tiers.txt` is moved over once when the mod loads (the old file kept
  as that day's backup next to it), and its first line is now `ngpp-tiers 4`. The launcher's save
  editor reads layouts 1 to 4, shows the Zodiac and the RYNO II as None, Heavy, Mega, Ultra, Alpha
  to Omega, Ultima, and writes layout 4.

## What's new in 0.6.17

- **No more half-dead enemies from the Zodiac's reach.** The reach beyond the gun's own pulse now
  takes only what the gun itself could: never an enemy whose update has not run yet (far up
  Siberius's cart track, a rider killed before it started came back alive with its body hidden and
  only its claws drawn), and an enemy whose death the gun leaves to its owner only when the game
  runs that owner, so the death always follows. Such enemies far off are simply left for when they
  come near.

## What's new in 0.6.16

- **Richer bolts, everywhere.** Every bolt reward pays half as much again on top of the run
  scale: enemy drops (x2.25, x3.75, x6, x9 from the second challenge mode on, generous half as much
  again; the cap on a drop's scale is x18), arena and race prizes, space mission pay, and
  Tabora's crystal and Grelbin's moonstone trades. Platinum and obsidian shares are as before, so
  their proportions of a drop are the same. Sellers' prices and the tiers' prices are unchanged (see
  "Bigger drops" and "Prizes and prices").
- **More enemies.** The extra copies come one at the second challenge mode, two at the third
  and three from the fourth (Barlow's hounds stay at two); see "More enemies each new game".
- **Busier space missions.** Extra waves: 1 at the second challenge mode, 2 at the third, 3 at the
  fourth and 4 from the sixth; elite ships from 25% (up to 70%, Elite guard up to 90%); Feltzin's
  targets keep their pace.

## What's new in 0.6.15

- **The Zodiac costs an arm and a leg, as the RYNO II does.** Its bought steps now use the RYNO II's
  absolute prices, whatever the Zodiac's own price in the game's table is (0.6.13 and 0.6.14 only
  multiplied that price, which could come out cheap): Mega Zodiac 6,000,000, Gamma 10,000,000,
  Epsilon 16,000,000 and Eta 25,000,000 (the RYNO II's 1,000,000 times 6, 10, 16 and 25; a higher
  table price raises them). The Zodiac's own first purchase is left to the game.

## What's new in 0.6.14

- **The Zodiac follows the other weapons' shape.** Its path is now Heavy Zodiac (by XP, the RYNO
  II Heavy's 37,500), Mega Zodiac (bought, the RYNO II's first price: 6,000,000 on a 1,000,000
  price), Ultra Zodiac (by XP, 50,000), then Alpha to Omega as before (see the table under
  "Tiers"). The names come from the game's own text, so every language works; each new step has
  its own damage and max ammo (+1, +2, +3 rounds, then +4 a tier from Alpha, as 0.6.10). Heavy,
  Mega and Ultra have no color (0.6.19): only Alpha and up change how a weapon looks.
- **Saves keep their Zodiac.** A Zodiac already at Alpha or above stays at the same tier; one at
  the base weapon keeps its XP, which now counts toward Heavy. `tiers.txt` is moved over once when
  the mod loads (the old file kept as that day's backup next to it), and its first line is now
  `ngpp-tiers 3`.

## What's new in 0.6.13

- **Every Zodiac kill earns it XP again.** The reach's kills were marked a tick ahead of the clock
  that read the marks, so nearly all of them (and the pulse's kills of enemies that had just come
  out) kept whatever weapon hit the enemy before, or none. Now the pulse's vaporized kills and the
  ones its owner's explosion finishes, and the reach's vaporized, owner and drained kills all name
  the Zodiac before the game's award reads it. An enemy the reach drains keeps the Zodiac's credit
  until its own death, unless another weapon hits it meanwhile; that weapon then gets the kill.
- **The Zodiac levels as slowly as the RYNO II.** It is an overpowered weapon, so each of its XP
  steps now needs what the RYNO II's step at the same place needs (Alpha 37,500 XP up to Omega
  150,000), and its bought tiers cost the RYNO II's prices for the same tiers (see the table under
  "Tiers").

## What's new in 0.6.12

The Zodiac keeps vaporizing enemies and earning XP after a big fight. Its reach (the enemies the
game's own pulse leaves out) used to hit them with a copy of the last hit any other weapon landed,
so they died their own death, a plain explosion, often after the Zodiac's credit had run out,
and a large group sent more of those hits at once than the game's queue of 64 holds. Now the reach
takes them the way the pulse itself does: an enemy at 100 health or less (or drained to nothing)
is vaporized in the pulse's sparks and its kill recorded for the Zodiac on the spot, a part or a
squad member is left to its owner as the game leaves it, and the rest lose the pulse's drain. An
enemy the pulse itself drains to nothing keeps the Zodiac's credit until its death animation ends.
Once a minute the log has a line saying what the Zodiac did: its pulses, its kills by how they
died, how many were credited to it, the XP it gained and where its next step stands.

## What's new in 0.6.11

Zodiac experience no longer stops at the higher tiers. The fields, blasts and chains NG++ posts
for a weapon after the shot is gone were credited to whatever weapon was in hand when the enemy
died, so a kill from the Zodiac's Omega fields (and from the decoy's, sheep's, shield's, Visibomb's,
Gravity Bomb's, Lava Gun's and Tesla Claw's perk hits) earned nothing for the weapon that caused it.
Now every such hit marks its victim with its own weapon, and the kill is credited to it.

## What's new in 0.6.10

The Zodiac's max ammo grows by exactly 4 rounds with each tier (Alpha +4, Beta +8, up to Omega +32)
and is filled to the new most on each level-up, as the other weapons' is.

## What's new in 0.6.9

When a RYNO II rocket splits, the homing split rockets fly at the parent rocket's target if it had
a live one in sight, so a single shot focuses all its damage on one enemy. With no such target
(or once it dies) they pick their own as before.

## What's new in 0.6.8

The Shield Charger and Tesla Barrier last longer, take more arcs before they collapse and hit
harder with each tier (see "Weapon tuning", Shield Charger and Tesla Barrier).

## What's new in 0.6.7

The Bomb Glove, Visibomb Gun, Tesla Claw, Walloper and Zodiac reach Alpha by XP: a full bar (6,000
XP) levels them up instead of leaving the bar full with nothing to buy.

## What's new in 0.6.6

The armor tier's paint is only on Ratchet in normal play while he wears Carbonox and owns a tier:
in cutscenes, and without Carbonox, he looks as in the stock game. The armor vendor's preview keeps
its paint.

## What's new in 0.6.4

- **The Zodiac earns XP from its kills.** The game gives a kill's experience to the weapon named
  in the dead enemy's health record, which a landed hit sets. The Zodiac's pulse posts no hit, and
  NG++'s reach (0.6.1) posts a copy of an earlier weapon's hit, so the Zodiac's kills went to
  another weapon or to none, and its Alpha tier ("on sale after 6,000 more XP") never unlocked.
  Now an enemy killed by the pulse, its drain or the reach names the Zodiac as its death is
  recorded, and the game's own award gives the Zodiac the usual experience (times the new game's
  and an elite's multiplier), once. Other weapons' kills are unchanged.

## What's new in 0.6.3

- **Tiers survive sessions the mod missed.** When the mod did not load for a while, the saves'
  clocks advanced without NG++ recording them, and the next load found "a save never seen" and
  showed no tiers. Now a loaded save whose clock the book does not have (and that shows weapon
  experience, so it is no new game) takes the levels of the book's closest earlier record, which
  is recorded under the new clock; the log names both clocks. Records with a later clock are
  never taken, and no entry is dropped or overwritten.
- Before tiers.txt is rewritten, one dated backup a day is kept next to it (tiers-YYYYMMDD.txt),
  the last 7.

## What's new in 0.6.2

- Built against the mod SDK 1.34.0 and declared for mod interface 1.34: it loads only on an
  OpenRAC with that interface or later, as the armor vendor's tiers need.

## What's new in 0.6.1

- **RYNO II rockets no longer hover.** A homing rocket only turned by its target's own motion, so
  near a target that stood still or shuffled in place it went back and forth over it like a
  firefly, and it chose enemies the game still held hidden at their spawn points (Grelbin's Yetis
  waiting to come out, enemies asleep far off, dying ones). Now each homing rocket flies at full
  speed and NG++ steers it: it only takes live, shown enemies the game is running, in sight (no
  wall between); it turns harder the nearer it gets, so it never circles; one that reaches its
  target without the game's hit goes off on it; and one whose target goes, hides or can't be
  reached flies straight on and takes the next enemy in sight at once.
- **The Zodiac reaches farther.** The pulse also hits the enemies the game's pick leaves out: up to
  50 units without a tier and 4 more each tier (90 at most), round corners and above or below the
  camera, but never through a wall (see "Off-screen enemies").
- **Off-screen targets for more weapons.** With OpenRAC from pull request 767 on, the Synthenoid
  and Kilonoid drones, the Seeker Gun's seekers, the Miniturret Glove's turrets, the Shield Charger
  and Tesla Barrier and the Tesla Claw also take the enemies around Ratchet that are not on screen,
  in sight only. An older OpenRAC keeps their on-screen targets (one line in the log says so).

## What's new in 0.6.0

- **The armor tiers at the armor vendor.** The tiers left the weapon vendor's list: the armor
  vendor (the Megacorp Vendor that sells Tetrafiber to Carbonox) now sells them, as the game's next
  armor once you wear Carbonox, at their price, with the vendor's own preview of Ratchet in the
  armor for sale and a description of what the tier does. Buy one as you buy an armor (Cross, then
  yes); the next tier takes its place in the list.
- **The tiers' look.** Each tier is Carbonox, the armor Ratchet wears, repainted in the tier's color:
  the same model, plates and shading, in steel, azure, jade, gold, amethyst, rose, ember or Omega's
  cycling colors, with a rim that glows brighter at each tier (none at Alpha). The vendor's preview
  shows the tier for sale in its paint, so you see what you buy. The flat glow over the whole body
  is gone.
- Both need OpenRAC with mod interface 1.34. With an older OpenRAC the tiers you already have keep
  protecting you and glow as before, but the armor vendor cannot sell them.

## What's new in 0.5.3

- **PAL's vendor.** On the PAL disc the mod looked for the vendor's list at v2.00's address, so the
  tier entries (and the armor's and the Ship Shack's) never showed and their writes landed in other
  memory. It now uses PAL's own, checked against the disc's code.
- **Level-up popups on PAL and v2.00.** The game's level-up popup and message line were looked up
  under names those builds do not have, so a tier earned by XP only showed the mod's own notice
  there; they now show the game's popup, naming the tier.
- **The texts of each disc.** The vendor's "Mega" label under a tier entry and Tabora's and
  Grelbin's trade lines have other text ids on PAL and v2.00: the tier names and the challenge mode
  pay now show on those discs too.
- **PAL's 50 Hz.** PAL's game ticks fifty times a second, so every NG++ time counted in ticks ran a
  fifth longer there (overdrive, freezes, burns, slows, fields, the Tesla Claw's steady hits, last
  stand, notices) and its own shot speeds were a fifth slower. They are now in seconds on every disc.

## What's new in 0.5.2

- **Tiers follow your saves.** A tier bought or levelled up after your last save belongs to the game
  in progress only, like the bolts it cost: load any save, the same one included, and you get that
  save's tiers back, never newer ones. Buying a tier, loading another save and loading back no
  longer gives you the tier and your bolts back; a tier you saved stays with that save, through
  other saves, a restart and the launcher's export (see "Saves"). The armor and ship weapon tiers
  and the weapons' XP counts follow the same rule.
- **The RYNO II's path** (the Zodiac's too since 0.6.18) alternates XP and purchase: Heavy, Mega,
  Ultra, Alpha to Omega, Ultima (bought after Omega); older `tiers.txt` files are moved over once. Its
  bought tiers cost 6, 8, 10, 16, 25 and 40 million (Mega, Alpha, Gamma, Epsilon, Eta, Ultima) and its XP goals rise by a quarter. Its rockets split by chance at the low
  tiers (25% at Alpha up to 80% at Epsilon, always from Zeta), into homing rockets that split again
  at Omega and Ultima, and the split rockets fly, home in and leave their trails (the trails need
  OpenRAC develop with #712).
- **Gentler Clank sections** (a setting, on): while you play as Clank, every hit takes at most one
  of his four health points, after the challenge modes' scaling and modifiers.
- **Weapon fixes** from an audit of every weapon: shots made at another shot's end start clear of
  it and only rockets in range split; the Plasma Coil's, Plasma Storm's and Pulse Rifle's shots
  count as the hero's; the Zodiac's Omega fields, every Blitz Gun pellet's spark and the Tesla
  storm's hit work; the armor's last stand cuts the lethal hit.
- **Armor tiers** are sold at the weapon vendor as soon as you own Carbonox, and their entry keeps
  its place in the list.
- **Extra enemies** are placed on every planet (OpenRAC #707), also when a challenge mode starts
  within a level, and the run's modifier is named on screen when the challenge mode changes.
- **Windows:** `tiers.txt` is closed before it is rewritten, so the RYNO II's move works there.

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

Damage and max ammo are multiplied from the weapon's last real version; max ammo stops at 999,
as many as the HUD's three digits show (the Omega Heavy Lancer's 1,000 made it crash). The
Zodiac is the exception: its capacity is small, so each tier adds a flat 4 rounds to the game's
(Alpha +4, Beta +8, up to Omega +32) instead of a multiplier, and its Heavy, Mega and Ultra add 1, 2
and 3. XP is the
game's own weapon XP (the bar in the quick select). It starts again from 0 at each tier, and the
first time the mod sees a weapon at its last version it resets the XP to 0, so XP from before the
mod or from earlier games never gives a tier right away. The XP bar (weapons screen and quick select) fills toward the
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
  their last version is bought and the game has no XP level-up after it, so Alpha comes by XP:
  a full bar (6,000 XP) levels the weapon up to Alpha with the game's level-up popup, then Beta
  by XP, Gamma bought, and so on as the other weapons.
- **Zodiac** (its path since 0.6.14, the RYNO II's since 0.6.18): XP and purchase in turn, Heavy
  Zodiac (XP), **Mega Zodiac** (bought), **Ultra Zodiac** (XP), Alpha (bought), Beta (XP), Gamma
  (bought), Delta (XP), Epsilon (bought), Zeta (XP), Eta (bought), Omega (XP) and **Ultima Zodiac**
  (bought). Each step by XP needs what the RYNO II's step at the same place needs, five times the
  usual, and the bought steps cost 6,000,000, 8,000,000, 10,000,000, 16,000,000, 25,000,000 and
  40,000,000 (1,000,000 times 6, 8, 10, 16, 25 and 40, whatever the Zodiac's own table price is).
  Alpha to Omega keep the other weapons' damage and ammo (below); Heavy, Mega and Ultra rise to
  meet them, Ultima tops Omega, and the max ammo gains 1, 2 and 3 rounds, then a flat 4 a tier:

  | Step | How | Price | XP | Damage | Max ammo | Color |
  |---|---|---|---|---|---|---|
  | Heavy | level up | | 37,500 | x1.15 | +1 | none |
  | Mega | buy | 6,000,000 | | x1.30 | +2 | none |
  | Ultra | level up | | 50,000 | x1.42 | +3 | none |
  | Alpha | buy | 8,000,000 | | x1.50 | +4 | lime |
  | Beta | level up | | 50,000 | x2.00 | +8 | cyan |
  | Gamma | buy | 10,000,000 | | x2.50 | +12 | emerald |
  | Delta | level up | | 75,000 | x3.00 | +16 | teal |
  | Epsilon | buy | 16,000,000 | | x3.75 | +20 | violet |
  | Zeta | level up | | 100,000 | x4.50 | +24 | magenta |
  | Eta | buy | 25,000,000 | | x5.50 | +28 | crimson |
  | Omega | level up | | 150,000 | x7.00 | +32 | rainbow |
  | Ultima | buy | 40,000,000 | | x8.50 | +36 | black and red, beating |

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
- **RYNO II**: first **Heavy RYNO II** at 37,500 XP, then Mega (bought), Ultra (XP), Alpha to Omega
  in turn (bought and XP), and last **Ultima RYNO II**, bought after Omega: twelve steps, Ultima the
  final one, on the Zodiac's path and prices. The
  RYNO II has its own curve, rising from Heavy to Ultima, and levels up five times slower than
  other weapons (`kRynoExpScale` in src/tiers.h). It is the strongest weapon, so it costs far more than
  the others' top tiers: 1,000,000 (its own price) times 6, 8, 10, 16, 25 and 40, about 2.5 to 2.8 times the
  ladder of 1.5M to 9M the others' biggest Mega prices reach. At the bolt scaling of the later
  challenge modes (x6 to x9, platinum and obsidian bolts) a common enemy at a full streak pays 14,400
  to 21,600, so Eta is a couple of thousand such kills:

  | Step | How | Price | XP | Damage | Max ammo | Color |
  |---|---|---|---|---|---|---|
  | Heavy | level up | | 37,500 | x1.40 | x1.50 | none |
  | Mega | buy | 6,000,000 | | x1.60 | x1.55 | none |
  | Ultra | level up | | 43,750 | x1.80 | x1.60 | none |
  | Alpha | buy | 8,000,000 | | x2.00 | x1.65 | lime |
  | Beta | level up | | 50,000 | x2.70 | x1.80 | cyan |
  | Gamma | buy | 10,000,000 | | x3.45 | x1.95 | emerald |
  | Delta | level up | | 75,000 | x4.30 | x2.15 | teal |
  | Epsilon | buy | 16,000,000 | | x5.20 | x2.35 | violet |
  | Zeta | level up | | 100,000 | x6.20 | x2.55 | magenta |
  | Eta | buy | 25,000,000 | | x7.30 | x2.75 | crimson |
  | Omega | level up | | 150,000 | x8.45 | x2.95 | rainbow |
  | Ultima | buy | 40,000,000 | | x9.70 | x3.20 | black and red, beating |

  At Ultima the XP bar shows full: there is nothing past it. Saves from before the path changed, when
  the RYNO II went Heavy, Ultima (bought), Alpha to Omega and Omega Prime, or (0.6.14 to 0.6.17)
  Heavy, Alpha to Omega, Ultima, are moved over once: Heavy stays Heavy, the old Ultima becomes
  Alpha, Alpha to Omega keep their name, and Omega Prime (or the last Ultima) becomes Ultima, each
  placed after the new Mega and Ultra.

**Names**: the tier replaces the game's top version in the weapon's name, in front of the name the
weapon had before its Mega. A four-version weapon uses its second version's name: "Beta Meteor
Gun", "Alpha Heavy Lancer" (not "Beta Lava Gun", nor the Mega's name). A two-version weapon uses
its first: "Alpha Bomb Glove". The Clank Zapper uses its last, "Alpha Clank Shocker"; the
Sheepinator its last XP version, "Alpha Black Sheepinator"; the Zodiac and the RYNO II their only
one ("Heavy Zodiac", "Mega Zodiac", "Ultra Zodiac", "Ultima Zodiac", "Heavy RYNO II", "Mega RYNO II", "Ultima
RYNO II"). The names come from the game's own text,
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
effects cycle through the rainbow, like its icon. Heavy, Mega and Ultra (the Zodiac's and the RYNO II's) change nothing: the weapon, its shots and its
icon look as the game draws them, and the tint, shot color, glow, vendor color and every other
visual start at Alpha. Alpha to Omega RYNO II are in the tiers' colors, and Ultima RYNO II beats between bright and deep red.

This is only how things look: shots fly, spread and hit exactly as at the tier's damage.

| Weapon | What takes the tier's color |
|---|---|
| Lancer / Heavy Lancer | the gun's glowing edge, the halo in its chamber and the flashes tied to the gun |
| Gravity Bomb / Mini-Nuke | the bomb in flight (its body and glow) and its blast |
| Chopper / Multi-Star | the spinning blades, body and glow |
| Blitz Gun / Blitz Cannon | the gun's glowing edge and the effects tied to it |
| Plasma Coil / Plasma Storm | the gun's glowing edge and the effects tied to it |
| Lava Gun / Meteor Gun (the Ice Gun) | the gun, its meteors with their trails and sparks, and the blobs it leaves on the ground. Its tiers are icy: frost, glacier, ice, arctic, deep ice, polar, cobalt, and aurora at Omega (see The Ice Gun); with mod interface 1.39 the gun is painted pale ice with deep blue where its lava glowed and the meteors are painted as shards of ice |
| Minirocket Tube / Megarocket Cannon | the tube, and each homing rocket (its body, glow and trail) |
| Spiderbot / Tankbot Glove | the bot itself (body and glow) and its exploding trail |
| Bouncer / Heavy Bouncer | the gun, and the bouncing bomblets with their blasts. The bomblets also fly 7.5% faster each tier (x1.6 at Omega) along the game's arc |
| Miniturret / Megaturret Glove | each turret you put down (body and glow) |
| Hoverbomb / Tetrabomb Gun | the gun, and each hoverbomb (body and glow) |
| Shield Charger / Tesla Barrier | the shield: its electric arcs and sparkles. It no longer turns green for good at the Mega: Alpha is lime, Beta cyan, Gamma emerald, Delta teal, Epsilon violet, Zeta magenta, Eta crimson, and Omega's shield cycles through them all |
| Zodiac | the gun's glowing edge and the effects tied to it |
| RYNO II | the launcher's glowing edge and the effects tied to it (orange at Heavy, then the tiers' colors, beating red at Ultima) |
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

With OpenRAC 1.29 the tier's color and glow also reach every shot a tiered weapon fires, its trail,
its sparks and its blast, on top of the guns and bodies from 1.25. That includes the extra shots
and split rockets the perks make.

### What each tier does

From 0.5.0 the tiers do more than damage. Each weapon gets perks at **Gamma** (Gamma and Delta),
**Epsilon** (Epsilon to Eta) and **Omega**. They are cumulative: Epsilon keeps Gamma's perk and
Omega keeps both, stronger each time. Perks come from your own hits and your weapons' own shots,
bombs, bots, shield and decoys, never from an enemy's. They add hits on other enemies on top of the
hit that landed, each a share of that hit's damage as it landed (after the tier and the enemies'
health), so "half" means half the hit's damage. Enemies get the extra hits one after another, nearest
first, and a weapon that hits every tick (a beam, a stream) rests a few ticks between perks. A
radius is in the game's units.

Cells marked **(OpenRAC 1.29)** need OpenRAC with mod interface 1.29 (OpenRAC develop 9753743cd
or later): the weapon's own shot is fired again by the game, a shot's end makes new shots, and
enemies can be frozen or slowed. On an older port those cells do nothing, except that the RYNO II's
split rockets, the Gravity Bomb's mini bombs and the Visibomb's mini rockets are dealt as hits and
blasts on the nearest enemies, with no rocket or bomb flying. A cell that says **can't be done yet**
is not done even on 1.29 (see "Not yet" at the end): the weapon only gets its tier's damage there.

"Ratchet's last weapon hit" below is the damage of the last hit he landed with a tiered weapon.

| Weapon | Gamma | Epsilon | Omega |
|---|---|---|---|
| Lancer | Each shot ricochets to the nearest other enemy within 8 units, for half the hit | Ricochets twice (each jump 85% of the one before) | Every 5th shot is a piercing round: full damage, 4 ricochets, and up to 2 enemies behind the victim within 10 units, also at full damage |
| Blitz Gun | can't be done yet | can't be done yet | Each pellet that hits leaves a spark for 1 s: every quarter second the enemies within 2.5 units take a quarter of the hit. (More pellets: can't be done yet) |
| Lava Gun | The Ice Gun (next section), or with the Meteor Gun setting the fire path (after it) | As Gamma | As Gamma |
| Plasma Coil | The arc chains to 1 more enemy within 9 units, 60% of the hit | Chains to 3 more (each jump 90% of the one before). The enemies it reaches are slowed to half speed for 1.5 s (OpenRAC 1.29) | Chains to 6 more, slowed for 2.5 s (OpenRAC 1.29), and the last jump explodes: enemies within 5 units take 80% of the hit |
| Minirocket Tube | 1 more rocket in every shot, fanned out (OpenRAC 1.29) | Each rocket splits in two half a second into its flight (OpenRAC 1.29) | 2 more rockets in every shot, and where a rocket ends it leaves 2 small blasts around it (OpenRAC 1.29) |
| Gravity Bomb | The blast reaches 9 units, for half the bomb's hit | 4 mini bombs scatter from the blast, 5 units around it, a third of a second later (OpenRAC 1.29: real mini bombs; before, hits: each a blast of 4 units for half the hit) | 8 mini bombs; each throw is 2 bombs, and a second bomb drops where the first ended, a second later (OpenRAC 1.29; before, a second pull-and-blast of 9 units at the full hit) |
| Chopper | can't be done yet | can't be done yet | can't be done yet |
| Spiderbot Glove | can't be done yet | can't be done yet | can't be done yet |
| Bouncer | 2 more bomblets in every throw (OpenRAC 1.29) | 3 more bomblets (OpenRAC 1.29) | Each blast drops a mini blast: enemies within 4 units take 40% of the hit. (More bounces: can't be done yet) |
| Miniturret Glove | 1 more turret in every throw (OpenRAC 1.29) | As Gamma | 2 more turrets, 3 in every throw (OpenRAC 1.29). (Longer-lived turrets: can't be done yet) |
| Hoverbomb Gun | 1 more hoverbomb in every throw (OpenRAC 1.29) | As Gamma | 3 more, 4 in every throw (OpenRAC 1.29). (Orbit and dive: can't be done yet) |
| Shield Charger | The reflect: can't be done yet | Every second the shield zaps the nearest enemy within 8 units for 60% of Ratchet's last weapon hit | The shield zaps the 3 nearest, and bursts when it ends: enemies within 8 units take twice Ratchet's last weapon hit (OpenRAC 1.29: the burst is the game's own blast) |
| Zodiac | can't be done yet | can't be done yet | Its pulse leaves a field for 3 s on each of the 8 enemies nearest the gun: every quarter second the enemies within 6 units take 15% of what the pulse drains in a quarter second (20 a frame) times the Zodiac's tier. (More stars: can't be done yet). At every tier it hurts bosses (see "Weapon tuning") |
| RYNO II | Split rockets by chance from Alpha, more and surer each tier (see below) | Always from Zeta, 3 | 4 split rockets, each splitting once more; at Ultima twice more (see below) |
| Seeker Gun | 2 more seekers in every shot (OpenRAC 1.29) | A seeker that ends sends one more seeking the nearest enemy (OpenRAC 1.29) | 4 more seekers in every shot, and the seekers it sends seek again, twice over (at most 6 made from one seeker; OpenRAC 1.29) |
| Pulse Rifle | Fires 1.25 times as fast (OpenRAC 1.29) | Fires 1.35 times as fast. Every 4th pulse explodes: enemies within 4 units take 60% of the hit | Fires 1.5 times as fast. Pulses pierce up to 2 enemies behind the victim within 12 units (80%) and chain to 2 more within 7 units (50%) |
| Visibomb Gun | The missile burns a trail: every quarter second the enemies within 3 units of it take 30% of Ratchet's last weapon hit | When the missile ends it releases 3 mini rockets that home in on the 3 nearest enemies within 12 units (OpenRAC 1.29; before, the 3 nearest take 80% of Ratchet's last weapon hit) | 6 mini rockets |
| Bomb Glove | 1 more bomb in every throw (OpenRAC 1.29) | Bombs chain-blast: enemies within 5 units of the victim take half the hit | Each bomb also bursts into 4 mini bombs (OpenRAC 1.29), and the 4 nearest enemies within 8 units take 40% of the hit |
| Decoy Glove | The taunt: can't be done yet | 1 more decoy in every throw (OpenRAC 1.29). The decoy explodes when it goes: enemies within 6 units take twice Ratchet's last weapon hit | 2 more decoys, 3 in every throw. The explosion reaches 9 units for three times, and goes off again half a second later |
| Synthenoid | can't be done yet | can't be done yet | can't be done yet |
| Tesla Claw | The beam arcs to 2 more enemies within 7 units, each arc one and a half times the beam's hit, falling by 15% a jump (with the steady claw, see "Weapon tuning", an arc a hit at most every 8 ticks) | Arcs to 5 more. The enemies the arcs reach are stunned (held still) for half a second (OpenRAC 1.29) | A storm: every third of a second an arc strikes an enemy within 10 units of Ratchet, one and a half times the claw's last hit (a whole one, not a steady hit's share), and stuns it. The stun lasts three quarters of a second (OpenRAC 1.29) |
| Walloper | A shockwave on each swing that lands: enemies within 4 units take half the hit | Wider: 6 units, 80%. The enemies it hits are staggered (held still) for a third of a second (OpenRAC 1.29) | The wave also chains to 3 more enemies within 8 units (70%) |
| Sheepinator | A sheep explodes when it goes: enemies within 4 units take Ratchet's last weapon hit | 6 units, 1.5 times. The sheepify chain: can't be done yet | 8 units, twice. The sheepify chain: can't be done yet |
| Clank Zapper | A wider zap: every other enemy within 4 units takes half the hit | Also the nearest enemy within 8 units, 80% | Chain lightning: 5 jumps within 8 units, 70% of the hit, falling by 10% a jump |

**Overdrive.** 20 kills in a row with one tiered weapon (from Gamma) switch it on for 10 seconds:
that weapon's perks act a stage higher (Gamma's as Epsilon's, Epsilon's as Omega's). Changing weapon
starts the count again.

The **Weapon perks** setting turns every perk off (the Ice Gun's and the fire path's too). Then
the tiers give damage, ammo and colour only. In a multiplayer party the host's setting counts.

### The Ice Gun

The Lava Gun's tiers are the **Ice Gun**: "Alpha Ice Gun" up to "Omega Ice Gun". The gun keeps
the Meteor Gun's model, repainted in pale ice with deep blue where its lava glowed (OpenRAC with
mod interface 1.39; an older one draws the shots and their effects in a clear ice blue over the
meteors' own look). It copies Up Your Arsenal's Liquid Nitrogen Gun (the Lava Gun's V5 there; what
that gun does is in `docs/rac3-liquid-nitrogen-gun.md`): the meteors the game fires are drawn as a
**stream of liquid nitrogen**, a ribbon of added light (a white core in a bright band in a wide
glow, in the tier's colour) with a glow at its head, cold puffs and a cold glow where it lands in
the place of the lava, and a **block of ice** around every enemy it freezes, which shatters into
shards when the freeze ends or the enemy dies in it (props of the mod SDK, interface 1.12 and up).
Its colours:

| Tier | Color |
|---|---|
| Alpha | frost |
| Beta | glacier |
| Gamma | ice |
| Delta | arctic |
| Epsilon | deep ice |
| Zeta | polar |
| Eta | cobalt |
| Omega | aurora (cycles through sea green, ice blue, white, deep blue and mint) |

What it does to an enemy it hits. With OpenRAC 1.29 the effects are real: a frozen enemy stops and
holds still (no walking, no attacking, no animation) but still takes hits and dies, and a slowed one
moves at half or a third of its speed. A hit on a slowed enemy counts in full. The effects wear off
by themselves:

| Tier | Effect |
|---|---|
| Alpha, Beta | Freeze: after each hit it is frozen solid for 2/3 s, painted in ice (glowing ice blue before mod interface 1.34), then slowed to half speed for the rest of 1.5 s, glowing blue. A fresh freeze comes only after it has thawed for half a second, so a stream holds it frozen most of the time, never for good. An enemy killed while frozen or chilled shatters: the game's own blast goes off on it in ice, and enemies within 3 units take the killing hit, counted as four hits of the stream (30% of each) |
| Gamma, Delta | Longer freeze: 5/6 s, then slowed to a third for the rest of 2 s; the shatter reaches 4 units at 45% |
| Epsilon to Eta | Freeze for 1 s, then slowed to a third for the rest of 2.5 s; the shatter reaches 5 units at 60% |
| Omega | Long freeze: 2 s, then slowed to a third for the rest of 3 s. The shatter reaches 8 units at full share, and the shards freeze what they hit |

**Bosses** only glow: they are never slowed or frozen. Before OpenRAC 1.29 there is no freeze or
slow: the gun only glows blue on its victims, and the shatter's damage is dealt as hits.

### The Meteor Gun's fire path

With the **Lava Gun's tiers** setting at **Meteor Gun**, the Lava Gun keeps the Meteor Gun's look and
its tiers burn instead of freezing, each stage with a name of its own: "Alpha Meteor Gun", "Beta
Meteor Gun", "Gamma Magma Gun", "Delta Magma Gun", "Epsilon Inferno Gun" to "Eta Inferno Gun" and
"Omega Supernova Gun". Its colours:

| Tier | Color |
|---|---|
| Alpha | ember |
| Beta | flame |
| Gamma | magma |
| Delta | blaze |
| Epsilon | inferno |
| Zeta | scorch |
| Eta | solar |
| Omega | supernova (cycles through red, orange, gold, white and rose) |

What a meteor does (shares are of the meteor's hit as it landed):

| Tier | Effect |
|---|---|
| Alpha, Beta | Burn: the enemy burns for 2 s, glowing orange, taking 10% every quarter second (+80% in all) |
| Gamma, Delta | The Magma Gun: a 3 s burn at 12% (+144%), and a splash of magma: enemies within 4 units take 40% and catch fire too |
| Epsilon to Eta | The Inferno Gun: the burn at 15% (+180%), a splash of 5 units at 50%, and a burning enemy killed erupts: the game's own blast goes off on it in the gun's colour, and enemies within 6 units take 1.5 times its last meteor and catch fire |
| Omega | The Supernova Gun: a 4 s burn at 18% (+288%), a splash of 6 units at 60%, eruptions of 8 units at twice the meteor, and every 4th meteor calls down a meteor storm: 3 blasts fall around its victim over half a second, each hitting the enemies within 4 units for 80% and setting them on fire |

Bosses burn too. A splash rests a tenth of a second on each enemy (the lingering blobs hit often). Burns and splashes never set off another splash, so a crowd catches fire without
the hits running away; the eruptions and the storm are where it spreads.

### Weapon tuning

0.5.1's changes to three weapons, from what their code does in the game:

- **Tesla Claw.** The game's claw hits each enemy it holds once when it takes hold, then not again
  until it lets go and takes hold again, about every 51 frames, while it spends ammo every 10: one
  hit a second at an uneven moment, with the ammo in between wasted. And a second hit from the
  same attacker inside 10 of the enemy's frames does nothing at all. With **Steady Tesla Claw** on
  (the default) NG++ hits each enemy the claw holds every tenth of a second for 15% of the claw's
  hit, its first hit made as small, each from the attacker the enemy did not see last, so every one
  lands: about a quarter more damage a second than the game's claw, times its tier, and the enemy
  flinches under it as under the Heavy Lancer's rounds (the game's flinch comes from hits landing,
  whatever the weapon). The tier, the enemies' curve and the perks see each hit as the game's.
- **Zodiac.** Its pulse posts no hit at all: each frame of it, it kills what has 100 health or less
  and takes a flat 20 off anything with more, whatever its tier, and nothing off a boss the cone or
  its class leaves out. Now each boss within the pulse's 40 units loses 4 times that a frame (the
  final boss 3 times, Todano's flying boss and the Damosel mothership's parts 5 times), times the
  Zodiac's tier and divided by the boss's health on NG++'s curve, as every other weapon's hits on
  it are.
- **Decoy.** The game gives a decoy 60 health with the upgraded glove, else 6. NG++ gives it 2.5
  times that, and its tier a fifth more again on top of the tier's damage factor: x4.5 at Alpha up
  to x21 at Omega. The enemies' hits on it count as their hits on Ratchet do each new game (they
  used to be scaled as Ratchet's hits on an enemy).

- **Shield Charger and Tesla Barrier.** The game ends the barrier when its charge runs out (40 for the
  Shield Charger, 100 for the Barrier, running down each frame) or when it has arced 40 times, so it
  shocks a few enemies and is gone. NG++ stretches both by the tier: the charge runs down, and the arcs
  are counted, that many times slower, and each arc hits harder on top of the tier's damage factor.
  No tier: the game's barrier. It needs no new hook: the mod rewrites the barrier's two words as the
  game updates it (`g_fade_level`, `g_tesla_hit_counter`).

  | Tier | Lasts, and arcs taken before it breaks (game: 40) | Damage per arc, on top of the tier's |
  |---|---|---|
  | Alpha | x1.5 (60) | x1.1 |
  | Beta | x1.86 (74) | x1.2 |
  | Gamma | x2.21 (88) | x1.3 |
  | Delta | x2.57 (102) | x1.4 |
  | Epsilon | x2.93 (117) | x1.5 |
  | Zeta | x3.29 (131) | x1.6 |
  | Eta | x3.64 (145) | x1.7 |
  | Omega | x4 (160) | x1.8 |

### Armor tiers

Past the best armour (Carbonox) there are **8 more armor tiers**, Alpha Armor to Omega Armor, sold
at the **armor vendor**, the Megacorp Vendor that sells the game's armors. Its list shows the
armors above the one you wear; once you wear Carbonox it has none of the game's left, and the next
tier is its row, at the tier's price, with the vendor's preview of Ratchet in the armor (Carbonox,
painted in the tier's color) and a line on what the tier does. Buy it as an armor (Cross, then
yes): the bolts are paid and the next tier takes the row's place. Each is bought, in order. Each
takes a share off every hit on Ratchet, on top of the game's armour.

The look is Carbonox, the armor Ratchet wears, **repainted** in the tier's color (OpenRAC's paint
material on his model, mod interface 1.34): the plates and their shading stay, the color is the
tier's, and a rim glows in it, brighter each tier (none at Alpha, the brightest at Omega). The
visor and the lamps (the model's own strongly colored details) are not flattened into the
suit's color: they take a bright **accent** of the tier's own, a shade against the suit's (red
against Alpha's steel, orange against azure, magenta against jade, sky against gold, lime against
amethyst, aqua against rose, yellow against ember, and Omega's cycles with its body); needs mod
interface 1.38, an older OpenRAC paints the whole suit in the tier's color. The
game's other armors share their textures with Ratchet's fur, so painting them would paint him too;
Carbonox is the whole-body suit and the model the tiers build on. With an older OpenRAC (1.25 to
1.33) the tiers glow over Carbonox instead and cannot be bought; their protection works with any.
The tier is saved with the weapons in the mod's `tiers.txt`, and the launcher's save editor has an
**Armor tier** picker.

| Tier | Damage taken | Price | Paint | Perk |
|---|---|---|---|---|
| Alpha | x0.90 | 900,000 | steel | |
| Beta | x0.82 | 1,200,000 | azure | |
| Gamma | x0.75 | 1,500,000 | jade | Thorns: a hit on Ratchet shocks the enemies within 5 units for half his last weapon hit |
| Delta | x0.68 | 1,950,000 | gold | Thorns as Gamma |
| Epsilon | x0.62 | 2,400,000 | amethyst | Thorns 7 units, the full hit. Last stand: a hit that would end him is cut to leave 1 point of health (before the game's damage, so no death starts), once a minute |
| Zeta | x0.56 | 3,000,000 | rose | As Epsilon |
| Eta | x0.50 | 3,600,000 | ember | As Epsilon |
| Omega | x0.45 | 5,400,000 | prismatic (cycles through all the colors) | Thorns 9 units, twice the hit. Last stand every half minute |

The prices are the weapons' ladder for a weapon whose every tier is bought, from 600,000. The armor
is not sold in a multiplayer party.

### RYNO II split rockets

From its first tier, **Alpha RYNO II** (after Heavy, Mega and Ultra, which don't split), on, a RYNO II rocket may split where it ends into smaller
homing rockets (OpenRAC 1.29): by chance at the low tiers, always from Zeta, and more of them up
the tiers. They start just clear of what the rocket hit, fly at the nearest other enemies within 40
units, home on them as the game's own rockets do and explode like them, in the tier's colour.
Heavy, Mega and Ultra RYNO II don't split.

| Tier | Chance a rocket splits | Split rockets | They split again |
|---|---|---|---|
| Alpha | 25% | 1 | |
| Beta | 35% | 1 | |
| Gamma | 50% | 2 | |
| Delta | 65% | 2 | |
| Epsilon | 80% | 3 | |
| Zeta | always | 3 | |
| Eta | always | 3 | |
| Omega | always | 4 | each in 2, once (12 from one rocket) |
| Ultima | always | 4 | each in 2, twice (at most 16 from one rocket) |

The roll is each rocket's own, fixed by the port's number for that shot (`split_roll` in
src/perk_rules.h), so a rocket never rolls twice; in a multiplayer party the host's NG++ makes the
splits. An overdrive raises the tier as for the other perks (Gamma's stage as Epsilon, Epsilon's as
Omega). On a port before 1.29, from Gamma the 2 nearest enemies within 15 units take 45% of the hit
instead (3 from Epsilon).

### Ultima RYNO II

The RYNO II's final step, after Omega, is **Ultima RYNO II**, bought for 40,000,000 (reached by
experience until 0.6.17). It does x9.7 damage and
x3.2 max ammo, and beats between black and red. Its rockets split in 4 and each split rocket
splits in 2 twice more (OpenRAC 1.29: real homing rockets, at most 16 from one rocket; before 1.29,
hits at 60% on at most 9 enemies). Nothing comes after it: its XP bar shows full.

### Elites and modifiers

**Elites.** From the third challenge mode, some of the extra enemies' copies are elites: drawn in
obsidian (OpenRAC 1.24), with 2.5 times the health, 3 times the bolts and twice the weapon
experience of an ordinary one of their kind. The share of copies is 15% in the third challenge mode,
5% more each run up to 35%. In the Maktar and Joba arenas, from the second challenge mode on, every
fourth enemy the waves send is an elite. Which copies are elites is fixed per level, so loading a
level again makes the same ones. Elites need extra enemies (OpenRAC 1.26). The **Elite enemies**
setting turns them off.

**Modifiers.** From the second challenge mode each new game has one modifier, named on screen when
its first level loads ("Challenge: Swarm"), in turn:

| Challenge mode | Modifier | What it does |
|---|---|---|
| 2nd, 6th, ... | Swarm | One more copy of each enemy that gets copies |
| 3rd, 7th, ... | Elite guard | Twice as many elites (30% in the 3rd, at most 70%) |
| 4th, 8th, ... | Volatile | Every enemy bursts when it dies: enemies within 5 units take 60% of Ratchet's last weapon hit |
| 5th, 9th, ... | Bounty | Enemies hit a fifth harder (x1.2) and drop a quarter more (x1.25) |

The **Challenge mode modifiers** setting turns them off (the elites stay on their own setting).
Swarm adds to the extra enemies and needs them on.

### The ship

**Ship weapon tiers.** Past the ship's own upgrades the ship gets **8 weapon tiers**, Alpha to
Omega, sold at Slim Cognito's **Ship Shack** as the **last entry** of its list, for raritanium like
the game's own ship parts (which cost 10 to 60). Each is bought, in order, and raises the damage of
every ship weapon (the twin guns, the specials, the missiles) and how fast they fire; Gamma, Epsilon
and Omega add a perk. The tier is saved with the weapons in the mod's `tiers.txt`, and the
launcher's save editor has a **Ship weapons tier** picker. Not sold to a multiplayer party's guest.

| Tier | Damage | Fire rate | Price (raritanium) | Perk |
|---|---|---|---|---|
| Alpha | x1.25 | x1.10 | 80 | |
| Beta | x1.5 | x1.20 | 120 | |
| Gamma | x1.8 | x1.30 | 160 | Overcharge: every fourth hit deals double |
| Delta | x2.1 | x1.40 | 200 | Overcharge |
| Epsilon | x2.5 | x1.50 | 240 | Overcharge; a hit that leaves a ship (not a boss) under a fifth of its health destroys it |
| Zeta | x3.0 | x1.65 | 280 | As Epsilon |
| Eta | x3.5 | x1.80 | 320 | As Epsilon |
| Omega | x4.2 | x2.00 | 360 | As Epsilon; 35% of each hit arcs to the nearest other enemy ship within 40 units |

**Space missions.** From the second challenge mode Feltzin's, Hrugis's and Gorn's space combat
missions get more of everything; never fewer ships than the game's:

| Challenge mode | Extra waves | Elite ships | Feltzin's targets |
|---|---|---|---|
| 1st | none | none | the game's pace |
| 2nd | 1 | 25% | x1.5 as often |
| 3rd | 2 | 35% | x1.5 as often |
| 4th, 5th | 3 | 45%, 55% | x2 as often |
| 6th on | 4 | 65% to 70% | x2 as often |

- A wave: when a fight's ships are down to a quarter of the most it had at once, every ship of it
  that was destroyed comes back, as the game brings its own ships to life when a mission starts. A
  mission with a boss or a capital ship, or fewer than three ships, keeps the game's waves. Swarm
  adds one more wave.
- Elite ships are drawn in obsidian (OpenRAC 1.24; violet energy before) and have twice the health;
  the ships of a wave NG++ brings back are elites 15% more often. Elite guard doubles the share (at
  most 90%).
- Every enemy ship takes the enemies' health curve (below), the bosses and the Gorn capital ship the
  bosses' column too.
- The **Space missions** setting turns the waves, elites and faster targets off (the health curve
  stays on its own setting).

**Ship looks.** The ship glows over its paint in its weapon tier's color (the weapons' tier colors,
Omega's rainbow), its shots too. The **Ship look** setting picks another look once the tier unlocks
it: Ion (cyan, Alpha), Plasma (violet, Beta), Solar (gold, Gamma), Inferno (crimson, Epsilon),
Obsidian (Eta) and Prismatic (a rainbow cycling, Omega); before its tier the ship takes the tier's
color. Off keeps the game's paint alone. The game's own 18 paints stay as they are: its paint list
is pictures on the disc, so the looks are drawn over the paint (OpenRAC 1.25's energy material).

### Health from Omega weapons

Ratchet gets one more health step (a nanotech step, as from the game's own orbs) for every **two
weapons at Omega**, at most 12 steps. The RYNO II counts at Ultima as at Omega.

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
| Weapon perks | on | What each tier does from Gamma, and the Ice Gun |
| Lava Gun's tiers | Ice Gun | The Ice Gun, or the Meteor Gun's fire path (above) |
| Steady Tesla Claw | on | The Tesla Claw's steady hits (Weapon tuning, above) |
| Elite enemies | on | Elites among the copies and in the arenas |
| Challenge mode modifiers | on | Swarm, Elite guard, Volatile and Bounty, one for each new game |
| Gentler Clank sections | on | As Clank, every hit takes at most one of his four health points, after the scaling and modifiers; Ratchet is not affected |

With the [difficulty](../difficulty-mod/README.md) mod, both apply and **multiply**: its "damage
dealt" and "damage taken" sliders scale the same hits as this mod's tiers and enemies, so with
damage dealt x0.5 and a Beta weapon in the second challenge mode a hit does x0.5 x 2 / 2. Turn
**Tougher enemies each new game** off to leave the enemies to the difficulty mod alone.

### More enemies each new game

From the second challenge mode on, the enemies of the table below get a copy beside each of them
when the level loads, two from the third challenge mode and three from the fourth (one for the big
ones, and two for Barlow's hounds, whose copies are the port's biggest):

| Challenge mode | 1st | 2nd | 3rd | 4th on |
|---|---|---|---|---|
| Copies of each enemy of the table | none | 1 | 2 (at most the table's) | 3 (at most the table's) |

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

**Pool headroom.** The pool is the game's 200 to 400 spare slots plus 1,024 asked for, and a copy is
only made while 160 are free, so a crowded planet simply makes fewer third copies (the first and second of
each enemy come first, as in 0.4.4). Barlow, the planet that filled the pool before (79 copies at two each, its hounds'
big pvars), keeps its hounds at 2 and its class 2234 at 1; every other planet's table is at most 3.

| Planet (level) | Enemy | Copies of each, at most |
|---|---|---|
| Aranos (0) | enemy (class 2460) | 3 |
| Aranos (0) | enemy (class 2826) | 3 |
| Aranos (0) | enemy (class 2827) | 3 |
| Oozla (1) | Mutant Swamp Beast | 3 |
| Oozla (1) | Mutant Firefly | 3 |
| Oozla (1) | Mutant Muckdweller | 3 |
| Maktar Nebula (2) | enemy (class 2501) | 3 |
| Maktar Nebula (2) | Megacorp Chickenbot | 3 |
| Endako (3) | Megacorp Rivet Bot | 3 |
| Endako (3) | Megacorp Laser Bot | 3 |
| Barlow (4) | enemy (class 2234) | 1 |
| Barlow (4) | Gadgetron Hound of Cuddly Death | 2 |
| Notak (6) | enemy (class 3002) | 3 |
| Notak (6) | enemy (class 3504) | 3 |
| Siberius (7) | enemy (class 3369) | 3 |
| Siberius (7) | enemy (class 3370) | 3 |
| Dobbo (9) | enemy (class 3306) | 1 |
| Dobbo (9) | Megacorp Trooper (class 3343) | 1 |
| Dobbo (9) | enemy (class 3415) | 3 |
| Dobbo (9) | enemy (class 3472) | 3 |
| Joba (11) | Joba Tribesman | 1 |
| Joba (11) | Jobian Saur-Beast | 1 |
| Todano (12) | Mutant Test-Squirrel | 3 |
| Boldan (13) | enemy (class 4587) | 3 |
| Aranos's prison (14) | enemy (class 4395) | 1 |
| Aranos's prison (14) | enemy (class 4396) | 1 |
| Smolg (17) | Grulch | 3 |
| Damosel (18) | Protopet | 3 |
| Yeedil (20) | Protopet | 3 |
| Yeedil (20) | Receiver Bot | 3 |
| Jamming array (26) | Megacorp Chickenbot | 3 |

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
in, half as much again as the tier prices' steps (x1.5, x2.5, x4, x6; since 0.6.16 the bolts pay
x2.25, x3.75, x6, x9), so the tier you are saving for takes about two thirds of the kills it took:

| New game | Challenge mode | Bolts (normal) | Bolts (generous) | Platinum at x1 streak | Platinum at x20 streak |
|---|---|---|---|---|---|
| 1 | - | x1 | x1 | none | none |
| 2 | 1st | x1 | x1 | 20% | 40% |
| 3 | 2nd | x2.25 | x3.375 | 10% | 50% |
| 4 | 3rd | x3.75 | x5.625 | 15% | 65% |
| 5 | 4th | x6 | x9 | 20% | 80% |
| 6 on | 5th on | x9 | x13.5 | 25% | 90% |

**Every enemy pays its own.** Each enemy has its own bolts in the level's data (the game's drop for
it, before your streak): on the disc, crates and small fry have under 20, the commonest enemies
around 45, the bigger ones 100 to 500 and the biggest up to 984. The game pays in proportion to
those up to its 3,000 limit; the mod pays a bigger enemy a little more on top of the run scale: x1
up to 20 bolts, then the square root of its bolts over 20, up to x2 (45 bolts: x1.5, 80 or more:
x2). The run scale and toughness together never pass x18 (generous included). A drop also gets more
pickups the tougher the enemy: two more than the game's three per x1 of toughness (5 to 7). What
the pickups can't hold is added to your bolts at once.

What one kill pays at a x20 streak (challenge mode's doubling and the game's limit included, before
its roll of about a quarter up or down):

| Enemy's own bolts | 3rd game (2nd challenge mode) | 4th (3rd) | 5th (4th) | 6th on (5th on) |
|---|---|---|---|---|
| 8 (small fry) | 675 | 1,125 | 1,800 | 2,700 |
| 45 (common) | 5,400 | 9,000 | 14,400 | 21,600 |
| 110 or more | 13,500 | 22,500 | 36,000 | 54,000 |

**Why 0.4.4 asks for more room.** The game makes every spawned moby (a bolt, a shot, an effect, an
enemy copy) from a pool of spare slots its level's file sets, 200 to 400 (Barlow's is 398), and a spawn
with none free is lost, a bolt with it. Barlow's 79 copies at the fourth challenge mode took a fifth of
the pool before the level spawned anything, and a long streak's drops fill the rest: in a hidden run a
flood of 300 pickups there made 228. From 0.4.4 NG++ asks OpenRAC (mod interface 1.27) for 1024 more spare
slots in every level, the most the port grants, about four times the game's, and nothing the mod spawns is cut back:

- Drops make the coins the game's limit and the toughness allow, as in 0.4.2, as long as the pool has
  free slots (the last 24 are the level's). If it ever runs short, the most valuable coins come first
  and what no coin holds is paid at once, so the bolts of a drop never change.
- A copy is made while 160 spare slots are free; the copies a load missed are tried again up to three
  times a second apart. The log says how many were made and left out, with the pool beside.
- An OpenRAC older than 1.27 keeps the game's pool, and then a burst makes at most 24 coins and the
  level holds at most 96 pickups at once, the rest paid at once.

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

**Collectors.** Tabora's crystals and Grelbin's moonstones pay the same x2.25, x3.75, x6 and x9 from
the second challenge mode (generous half as much again), so collecting keeps up with the tier prices with a margin. The collector's dialog shows the total you will get.

**Prizes and prices.** From the second challenge mode the hoverbike races on Barlow and Joba, the
space missions of Feltzin, Hrugis and Gorn (since 0.4.3) and the arena challenges on Maktar and
Joba pay the same x2.25, x3.75, x6 and x9 (generous half as much again), and the characters who sell
for bolts ask half that rise: x1.25, x1.75, x2.5 and x3.5 (generous x1.63, x2.38, x3.5, x5). The
race menu, the mission's reward panel, the arena menu and each seller's dialog show the new amount,
and the log says once a level what its races or missions pay ("level 5: the space missions pay
x6.00 (the second mission 5500 -> 33000)", before the game's own x10 in challenge mode). The weapon vendor's prices are the tiers' own and stay as they are.

| | First challenge mode (the game's) | 2nd (x2.25; prices x1.25) | 3rd (x3.75; x1.75) | 4th (x6; x2.5) | 5th on (x9; x3.5) |
|---|---|---|---|---|---|
| Barlow races, first win | 0 (the story race), 20,000, 30,000, 40,000, 60,000 | 45,000 to 135,000 | 75,000 to 225,000 | 120,000 to 360,000 | 180,000 to 540,000 |
| Barlow races, second win | 2,500, 7,500, 10,000, 12,000, 20,000 | 5,625 to 45,000 | 9,375 to 75,000 | 15,000 to 120,000 | 22,500 to 180,000 |
| Barlow races, later wins | 2,500, 2,500, 3,500, 4,000, 7,500 | 5,625 to 16,875 | 9,375 to 28,125 | 15,000 to 45,000 | 22,500 to 67,500 |
| Joba races, first win | 0, 40,000, 50,000, 60,000, 80,000 | 90,000 to 180,000 | 150,000 to 300,000 | 240,000 to 480,000 | 360,000 to 720,000 |
| Joba races, second win | 4,000, 12,000, 17,500, 20,000, 27,500 | 9,000 to 61,875 | 15,000 to 103,125 | 24,000 to 165,000 | 36,000 to 247,500 |
| Joba races, later wins | 4,000, 4,000, 6,000, 7,500, 9,000 | 9,000 to 20,250 | 15,000 to 33,750 | 24,000 to 54,000 | 36,000 to 81,000 |
| Maktar arena (first win) | 15,000 to 90,000 | 33,750 to 202,500 | 56,250 to 337,500 | 90,000 to 540,000 | 135,000 to 810,000 |
| Joba arena (first win) | 50,000 to 125,000, and 2,000,000 | 112,500 to 281,250, 4,500,000 | 187,500 to 468,750, 7,500,000 | 300,000 to 750,000, 12,000,000 | 450,000 to 1,125,000, 18,000,000 |
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
generous pays half as much again as normal (the table's generous column, up to the x18 cap), with
the same platinum share.

Platinum bolts need OpenRAC with interface 1.23 (the bolt hook points and the platinum look), and
obsidian bolts 1.24 (the obsidian look). On an OpenRAC with 1.23 the drops have platinum but no
obsidian; on an older one the drops stay the game's, and the rest of the mod works as before (the log
says so once).

### Saves

The tiers are kept with each save, the way the game keeps its own progress: the mod's data folder
has a `tiers.txt` with the tiers of each save the game made, by the save's own clock (the time the
game stamps it with when it saves, at an autosave and as a level starts). A tier is written there
when the game saves, with the tiers you had then; a tier bought or levelled up after your last save
is only the game in progress's, like the bolts it cost, and is gone if you load a save or quit
without saving. Each save gets only its own tiers, exactly as they were when it was saved:

- The mod watches the save's clock at the title screen too, where a loaded save shows its own clock
  before its level starts and the game stamps a new one; the new clock keeps the save's tiers.
- A clock the file doesn't have is the same save stamped again (when you save, at an autosave, at a
  level start) only if the save's data didn't change; if it did change (bolts, weapon XP or
  versions), you loaded a save the mod has never seen, and it starts with no tiers and its weapons'
  XP counted from 0, like any save new to the mod.
- The save you just made, loaded again before a new level starts, keeps its clock: the mod sees its
  data (bolts, raritanium, weapon XP and versions) come back exactly as saved and takes back the
  tiers bought since.
- After a new game's zero clock, the tiers carry over only into challenge mode's next game, which
  keeps your weapons; a plain new game starts with none.

A record with tiers is never overwritten with an empty one. Nothing is written to the memory card
or its folder.

The file's first line, `ngpp-tiers 4`, marks the RYNO II's and the Zodiac's path Heavy, Mega, Ultra,
Alpha to Omega, Ultima. A file without it, or with `ngpp-tiers 2` or `3` (from before the path), is
moved over once when the mod loads (see RYNO II above: its Alpha to Ultima move up two places; the
Zodiac's levels from layout 3 stay, and from layouts 1 and 2 move up three from Alpha), then written
with the line, so it is never moved twice.

The launcher's save editor shows each weapon's tier in its version picker for the save you open
while the mod is on, and saves your changes to `tiers.txt`; close the game first. It also lets you
set every weapon's XP, at its last version and for the RYNO II too. Exporting saves from the
launcher takes their tiers along, and importing them puts the tiers back, on any computer.

It works with the [item randomizer](../item-randomizer/README.md) (they use none of the same hook
points) and with the [difficulty](../difficulty-mod/README.md) mod (its prices and max ammo are what
the tiers multiply). In a [multiplayer](../multiplayer/README.md) party everyone plays with the
host's tiers.

### Off-screen enemies

The Zodiac's blast and the arcs of the Plasma Coil and the Plasma Storm hit every enemy in their
range, also the ones behind the camera or off screen. On the PlayStation 2 these weapons pick
their targets only among what the game drew in the last frame, so an enemy behind Ratchet was
missed. Their range and how many enemies they hit stay the game's. This is on at every tier and in
the first playthrough too; the setting **Zodiac and Plasma Coil hit off screen** turns it off. It
needs OpenRAC with mod interface 1.30; an older OpenRAC keeps the game's targets. With OpenRAC
from pull request 767 on the same setting opens the picks of the Synthenoid and Kilonoid drones,
the Seeker Gun's seekers, the Miniturret Glove's turrets, the Shield Charger and Tesla Barrier and
the Tesla Claw's held target, all within their own range and only in sight.

The Zodiac's pulse also reaches the enemies the game's own pick leaves out (one round a corner, far
from the camera, or more than 60 degrees above or below its view): every live, shown enemy within
50 units of the gun without a tier, 4 more each tier up to 90, that no wall hides from the gun. It
takes them as the game's pulse does, without a hit: what has 100 health or less (or what a pulse's
drain leaves with nothing) dies in the pulse's sparks, its kill the Zodiac's, and the others lose
a pulse's drain; bosses keep their own drain ("Weapon tuning").

## How it works

Through OpenRAC's rac2 hook points of SDK 1.14.6 (and `rac2.player.nanotech_steps` for 0.5.0), and the bolts' of interface 1.23, the obsidian
material of 1.24, the energy material of 1.25 and the weapon calls of 1.29 (OpenRAC develop 9753743cd or later) (the addresses are NTSC-U v1.01's):
material of 1.24 and the energy material of 1.25 (the addresses are NTSC-U v1.01's):

| | Where |
|---|---|
| For sale | `rac2.vendor.build_list` (`func_002F6608`): an upgrade entry for each weapon whose next tier is for sale, right after that weapon's own entry and built as the game builds its Mega entry; `rac2.vendor.price` (`func_002F6510`) prices it; `rac2.item.upgrade` (`func_002887B0`), which the vendor's purchase calls once it is paid, takes the tier instead of the game's Mega step |
| Experience | each tick, a weapon at its last version whose experience (which the game keeps adding up there) reaches the tier's takes it; its experience starts again from 0, as the game's own Mega purchase does |
| Experience bar | the last version's row (+0x6C, the experience to the next version, which the game leaves at 0 there: a full bar) set each tick to the next tier's goal, and back to the game's when none is left; the game's own level check skips every last version, so it never acts on it |
| Damage | `rac2.combat.post_hit` (`func_002A8E20`): a hit posted to anyone but Ratchet has its damage (+0x1C) scaled for that call by the tier of the weapon that dealt it: the weapon whose own moby is the request's source (+0x10, its class at +0xAA; `kHitSources` in src/discs.h: the Lancer, Blitz Gun and Cannon, Lava Gun and its pools, the Meteor Gun's meteors, Seeker and Minirocket shots, Gravity Bomb blasts, Bouncer blasts, Spiderbot trails, Shield Charger and Tesla Barrier, Tesla Claw, the Plasma Coil's and Plasma Storm's shots, the Pulse Rifle's beam, the Clank Zapper), else the weapon in Ratchet's hand |
| Max ammo | the last version's row (+0x8E) in the item table, rewritten each tick from what the game (or another mod) has there; a tier fills the ammo |
| Enemies | the save's challenge mode counter (+0x12 of its block); their health as the damage they take in `rac2.combat.post_hit` divided by the curve (a boss's, by the victim's class at +0xAA, by the boss column too, the final boss's by 1.5 more; the RYNO II's hits by its own rule, its tough enemies by their own bolts at +0xB4), the damage they deal in `rac2.player.take_damage` (`func_L00_002A4EF8`) times it, a fraction of a point carried to the next hit |
| Weapon experience | `rac2.moby.record_death` (`func_002A6C58`, `SetDeathBits`): every weapon's experience (the s32 table at `D_00139868`, << 5) read before the game's own award and after, each gain times the run's rate and the moby's (a boss by its class at +0xAA, an enemy's own bolts at +0xB4) |
| Color | `rac2.item.colors` (`func_00336F20`): the quick select's and the weapons screen's icons and names; `rac2.vendor.draw_list` (`func_002F9C80`) and `rac2.item.version_color` (`func_00336EF8`): the tier for sale's icon in the vendor's list (an OpenRAC without these two keeps the game's gold there) |
| Bolts | `rac2.bolt.spawn_pickups` (`func_002ADB78` and its copy in every level's code): the game's burst runs as it is (its amount, flags and coin limit: flag 8, `SetDeathBits` asks for three coins a kill, the rest going to the game's word at +0x1C of the save block, `D_001A7A14`, divided by the streak); the coins it makes through `rac2.bolt.spawn_pickup` (`func_00302470`) are only written down (worth, start, speed), and their total, from the second challenge mode times the run scale and the enemy's toughness (its own bolts, s16 at +0xB4, when its class record at +0x24 says kind 5 at +0x46, as `SetDeathBits` checks), at most x18, is made again as obsidian, platinum and gold coins at those places (a coin the game can't make, which it puts in its word, is taken back out and paid at once), the rarer ones drawn with `moby_material` (platinum from interface 1.23; obsidian from 1.24, its glow colour per class in the call's rgb); `rac2.bolt.collect` (`func_00301D10`) adds four times a platinum coin's class (24 times an obsidian one's) after the game's own and takes its look off. The kill streak is the byte at +0x3A of the save block (`D_001A7A32`); the mod's own coin limit is the game's plus two per x1 of toughness, and what it leaves goes to the bolts at once through `openrac_api.bolts` (in the first challenge mode, unscaled, to the game's word divided by the streak, as the game does) |
| Collectors | no hook point: their dialogs' texts (10135 Tabora's crystals, 10209 Grelbin's moonstones) through `rac2.text.by_id` get the scaled total in place of their second number; each tick a trade (the crystals at +0x22 of the save block, s16, or the moonstones at +0x24, s32, down to 0 in Tabora or Grelbin with the game's pay, 1,000 or 3,000 each, ten times in challenge mode, in the bolts at +0x00) gets the rest of the scaled pay through `openrac_api.bolts` |
| Races | no hook point (the race's panel, `func_L04_0040AD28`, and its finish, `race_update_player_progress`, read the prize straight from the table): while Barlow or Joba is loaded, each race's three prizes (+0x08, +0x0C, +0x10 of its 0x40 bytes in the level's race table, `D_L04_002C63C0`, `D_L11_002B6D40` in Joba) are written at the run scale, the game's kept and written back when the run pays as the game does; the game's x10 in challenge mode comes on top |
| Arenas | `rac2.arena.prize` (`func_L02_003FCDF8`, `moby_compute_scaled_entry_value`, one function in Maktar's and Joba's code): the game's prize times the run scale; the arena's menu prints it and the win pays it |
| Sellers | no hook point (each seller's dialog prints the price it then takes, both from the level's data): the bolt prices of the level's price table (`D_L06_002958D0`, `D_L09_0028E350`, `D_L14_00291850`, `D_L17_0028C550`: the game's and challenge mode's price of Notak's toll door, words 0-1, Dobbo's seller 4-5, the prison's seller 10-11, Smolg's machine 12-13; the crystals and moonstones of Tabora and Grelbin left alone) and the gadget sellers' item row (+0x80 of the item's row) are written at half the run scale's rise while their level is loaded, the same way as the races |
| Level-up popup | a tier earned with the weapon in hand plays the game's in-hand level-up (`func_L00_0037E6A8`); at its end the game's popup from its own level-up table (`help_show_message_for_id`, `func_00288508`) names the last version, or is text 0 (an empty box) for a weapon the table has no line for (RYNO II, Zodiac): for ten seconds after, the resolver gives that popup the tier's name |
| Weapon looks | each tick, the level's moby table (the first moby's and past the last's words, `g_mobyTableBase` and `g_mobyTableEnd`; 0x80 and up at +0x20 a dead slot) is looked through for the weapons' own classes (`kVisualClasses` in src/weapon_visuals.h: guns, shots, bombs, blasts, bots, turrets, the shield, each with its item); a moby whose weapon wears a tier is drawn with `moby_material`'s energy (interface 1.25) at the tier's strength in the tier's color (`effect_rgb`, `effect_strength` in src/tiers.h), sent again only when it changes (Omega and Ultima every other tick). OpenRAC draws the body with a touch of the color and a glowing rim, and the moby's glow packets, its draw callbacks' quads and the particles naming it as their owner in the color. On a port of 1.23 or 1.24, a tint on the shots', bombs' and bots' bodies only |
| Perks | the same `rac2.combat.post_hit` hook: the extra hits are posted through its original (the game's `QueueMobyImpact`), because OpenRAC's `moby_hit` and the update gate are rac1-only on the current port. That is still how the chains, bursts, fields and shockwaves deal damage, and how every perk deals it before 1.29. The shield, Visibomb missile, Gravity Bomb blasts, decoys, sheep and Tesla Claw beam are watched as mobys of their own classes (`kWatchedClasses` in src/perks.cpp) |
| Ice Gun | `moby_status` (interface 1.29): an enemy the Ice Gun hits is frozen (status 1) or slowed to half or a third (statuses 2 and 3) for the ticks of its tier; the port holds it still, or runs it one tick in two or three, and puts it back by itself. Nothing is written to the enemy's update word any more. Before 1.29 only the glow (`moby_material`) and the shatter's hits are done |
| Extra shots | `weapon_refire` (interface 1.29), asked from `on_weapon_fire` when the hero's weapon fires: the game's own spawn makes the shot again, fanned out by a small yaw (Minirocket Tube, Seeker Gun, Hoverbomb Gun, Bomb Glove, Gravity Bomb, Bouncer, Decoy Glove, Miniturret Glove) |
| Shot ends | `on_shot_end` (interface 1.29), told when a shot of the hero's ends: `moby_spawn_live` makes new shots where it ended (the RYNO II's split rockets, the Gravity Bomb's and Bomb Glove's mini bombs, the Seeker Gun's seekers seeking again, the Visibomb's mini rockets) and the game's own rocket blast (the Minirocket Tube's cluster, the Ice Gun's shatter, the end blasts of the decoy, sheep, shield, Volatile and Plasma Coil). A Minirocket Tube rocket is split in flight the same way, half a second after it leaves |
| Shot colours | `weapon_look` (interface 1.29): the tier's energy material and colour for a weapon's item, which the port applies to every shot of it, its trail, its sparks and its blast |
| Fire rate | `weapon_record` (interface 1.29): the Pulse Rifle's record in the level, its shots-a-second stat (+0x54) times the stage's rate, put back when the perk goes |
| Moby pool | `moby_pool_raise` (interface 1.27): the request is 1024 spare slots, the most the port grants. Every extra shot counts toward the port's shots-a-tick limit, which NG++ keeps (64 of the 128) |
| Version | the Ice Gun, extra shots, shot ends, shot colours and fire rate rows need OpenRAC develop 9753743cd or later (mod interface 1.29, SDK 1.29.0). Older ports keep the earlier perks |
| Space waves | no wave spawner in the game: a mission's ships are the level's placed mobys, brought to life by `moby_init_spawned_sub_object` (`func_L05_0037AEC0`, in Feltzin's, Hrugis's, Gorn's and Wupash's code; PAL `func_L05_0037D610`, NTSC-U v2.00 `func_L05_0037C130`), alive while their motion record's +0x110 is 1 (the record at pvars +0x14). Each tick the fight's ships are counted, and a due wave calls that init again (`function_at`, the level's own address) on the dead ones, at most 6 a tick: they are the level's records, so no moby pool slot is used. Feltzin's target spawner (class 4378) has its timer (pvars +0x26C) run faster |
| Space hits | `rac2.combat.post_hit` in a space level: the ship's hits (the shot's owner at its pvars +4, class 4031) times the ship tier and its perks, every enemy ship's divided by its health (the curve, a boss's, an elite's) |
| Ship tiers | the Ship Shack is the vendor's mode 2 (+0x50 of its state; `shop_build_vendor_list_by_flags`, `func_002F7738`): 0x1C-byte entries at +0x140, count at +0x740. NG++ appends its entry while the list shows (its name and description in the port's text slots 0 and 1); its value is the equipment word's unused bit 21 (`g_equipment_bits`, `0x1A7AF0`), which the game's confirm (`func_L00_00374348`) writes once it has taken the raritanium (the save block +4). NG++ then clears the bit and takes the tier: the book's item 54. The fire rate: the ship's five weapon timers (pvars +0x08, +0x0A, +0xA8 s16, +0x20C, +0x210 s32; `UpdateShipWeapons`) run faster |
| Ship looks | `moby_material` (energy, interface 1.25; obsidian, 1.24) each tick on the ship's mobys: class 4031 and its parts (4305 to 4325, 4742 to 4744), the hub's hull 3412 and its own shots (4090) |
| Health | `rac2.player.nanotech_steps`: the game's count of health steps plus one for every two Omega weapons (at most 12) |
| Armor | the damage share in `rac2.player.take_damage` (the hit times the tier's share, Bounty's included); the armour level is read at `g_player_stats` +0x18; the tier is the book's item 55; the armor vendor's row is added in `rac2.armor_vendor.build_list` (a row of armour 4, so the game's preview loads Carbonox) and bought in `rac2.armor_vendor.buy`; the look is `moby_material_class` with the paint material on Ratchet's class 0, in the world and on the vendor's picture (mod interface 1.34) |
| Texts | `rac2.text.by_id` (`func_00288FB8`), one resolver for every text: a looked-up id that is a text field of a row the mod names at a tier (the vendor's entry being drawn or named, the level-up line the mod shows, or the row of the one owned tiered weapon that holds it) gets the game's string with the tier's name in it, written into the port's text slots; optional (an OpenRAC without it shows the game's texts) |
| Off-screen targets | `weapon_ignore_culling` (interface 1.30) for the Zodiac (43) and the Plasma Coil (28), asked once at load unless `offscreen_targets` is off: the port's wrap of the game's target pick (`SelectBestTargetInCone`, `func_002AF580`) counts the enemies not drawn last frame as drawn for those weapons' gun and shots only |

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
| `src/perk_rules.h` | 0.5.0's rules alone: the perks, the shots' perks, the statuses, the Ice Gun, armor tiers, elites, modifiers, overdrive |
| `src/shots.h` | OpenRAC 1.29's weapon calls: extra shots, what a shot does as it ends, splits in flight, shot colours, the Pulse Rifle's fire rate, the statuses and blasts |
| `src/weapon_api.h`, `src/moby_pool.h` | interface 1.29's weapon calls and 1.27's pool calls, read from the port's table |
| `src/perks.h`, `src/armour.h`, `src/elites.h` | the perks and the Ice Gun, the armor tiers, the elites and modifiers in the game |
| `src/armour_vendor.h`, `src/armour_api.h` | the armor tiers' row at the armor vendor, and the entries of mod interface 1.34 they use |
| `src/combat.h` | the tiers' damage and the enemies across new games |
| `src/tuning_rules.h`, `src/tuning.h` | 0.5.1's weapon tuning: its rules alone, then the Tesla Claw's steady hits, the Zodiac's boss damage and the decoy's health in the game |
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
- Perks OpenRAC 1.29 still can't do, because these weapons have no shot moby to fire again or no
  call for the effect: the Chopper's and Spiderbot Glove's extra blades and bots, the Synthenoid's
  extra drones, the Blitz Gun's extra pellets, the Zodiac's extra stars, the Miniturret Glove's
  longer-lived turrets, the Bouncer's extra bounces, the Hoverbomb Gun's orbit and dive, the
  Visibomb missile's surviving a hit, the Decoy Glove's taunt, the Sheepinator's sheepify chain
  and the Shield Charger's reflect. For those weapons the tiers give damage, ammo and colour, plus
  the hit perks in the table. Before OpenRAC 1.29 (an older port) the perks that need it are not
  done at all, and the RYNO II's split rockets and the Gravity Bomb's mini bombs come as hits and
  blasts on the nearest enemies; there is no freeze or slow either.
- The Ice Gun keeps the Meteor Gun's model (repainted) and the game's meteors do the hitting; they
  are drawn as a stream of liquid nitrogen (0.6.23), but they are still meteors: they fly the
  game's arc, one every few ticks, and blast where they land. Up Your Arsenal's stream is a ring of
  points the gun moves itself, which Going Commando's gun does not have.
- The weapons' looks change only how things are drawn: shots are no faster (but the Bouncer's
  bomblets) and blasts reach no farther at a higher tier (the larger sparks and flashes are drawn larger, nothing more). The
  game's item rows keep the shots' speed and size in fields that mean different things for each
  weapon, so changing them waits until each weapon's fields are traced.
- An effect the game draws on its own, not tied to the gun or the shot that made it, keeps the
  game's colors, and so do a few weapons' shots whose moby classes are not yet traced (the Pulse
  Rifle's and the Plasma Coil's bolts among them, if they are mobys of their own).
