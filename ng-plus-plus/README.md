# NG++

New Game++ for Going Commando: eight more tiers for every Going Commando weapon, past its last version, for OpenRAC's native port
(NTSC-U, PAL and NTSC-U v2.00 discs). The game keeps its own versions: NG++ adds its tiers on top
of each weapon's last one and keeps its own level per weapon, so the game still sees its last row.

With the mod off, the game is the PlayStation 2 game.

## Playing

Install it from the launcher's **Mods** tab (**Add from a link**:
`https://github.com/OpenRAC/openrac-mods`), switch it on and play.

**Buy, then level**, as the game's own Mega purchase is followed by V4 by experience: once a weapon
is at its last version, the vendor sells its next tier; once that tier is levelled with experience,
the vendor sells the next.

| Level | Name | How | Price | Experience | Damage | Most ammo | Tint |
|---|---|---|---|---|---|---|---|
| V5 | Alpha | bought | 100,000 | | x1.25 | x1.10 | lime |
| V6 | Beta | experience | | 8,000 | x1.50 | x1.20 | cyan |
| V7 | Gamma | bought | 250,000 | | x1.75 | x1.30 | emerald |
| V8 | Delta | experience | | 12,000 | x2.00 | x1.40 | teal |
| V9 | Epsilon | bought | 500,000 | | x2.40 | x1.50 | violet |
| V10 | Zeta | experience | | 16,000 | x2.80 | x1.60 | magenta |
| V11 | Eta | bought | 1,000,000 | | x3.30 | x1.80 | crimson |
| V12 | Omega | experience | | 24,000 | x4.00 | x2.00 | prismatic (cycles through all the tints) |

The damage and the most ammo are times the weapon's last real version's. The experience is the
game's own weapon experience (what the quick select's bar counts), from the last tier on.

- **Four-version weapons** (Lancer, Chopper, ...): Alpha is for sale as soon as the weapon is V4.
- **Two-version weapons** (Bomb Glove, Visibomb Gun, Decoy Glove, Tesla Claw, Walloper), and the
  **Zodiac**: their last version is bought, and the game has no experience step after it, so Alpha
  goes on sale after 6,000 more experience with it.
- **Clank Zapper**: Alpha is for sale once it is V2.
- **Sheepinator**: the vendor never sells it, so every tier comes by experience (6,000 for each tier
  that would be bought).
- **RYNO II**: first **RYNO II Heavy** by 6,000 experience (x1.4 damage, x1.5 ammo, orange), then
  **RYNO II Ultima** at the vendor for 400,000 (x2.2 damage, x2 ammo, a black and red tint), then
  Alpha (after 6,000 more experience) to Omega, each adding its gain to Ultima's.

The tier's name ("Heavy Lancer Alpha": the game's name of the last version and the tier) shows at the
top of the screen when a weapon reaches a tier and when you take a tiered weapon in hand.

### Tougher enemies each new game

The tiers make weapons far stronger, so the enemies keep up across challenge mode's new games.
The game itself counts its challenge modes (its save's counter, +1 at each new game in challenge
mode) but only tests whether one is on: it doubles ammo pickups and nanotech orbs' health there,
and its bolt multiplier grows with kills; it never scales enemy health or damage by the count.
From the second challenge mode on, the mod does:

| New game | Challenge mode | Enemy health | Damage they deal | The tier expected by then |
|---|---|---|---|---|
| 1 | - | x1 | x1 | V1 to V3 |
| 2 | 1st | x1 | x1 | V4 (the game's own balance) |
| 3 | 2nd | x1.5 | x1.25 | Beta (x1.5) |
| 4 | 3rd | x2 | x1.5 | Delta (x2) |
| 5 | 4th | x2.8 | x1.75 | Zeta (x2.8) |
| 6 | 5th | x4 | x2 | Omega (x4) |
| 7 on | 6th on | +1 each | +0.25 each | |

The health follows the expected tier's damage, so a weapon at that tier takes about as long to kill
an enemy as V4 did in the first challenge mode. **How much tougher** scales what the curve adds
(0.5: x1.25 health at the second challenge mode instead of x1.5).

| Setting | Default | |
|---|---|---|
| Tier names on screen | on | That line at the top of the screen |
| Tougher enemies each new game | on | The curve above |
| How much tougher | 1 | Times what the curve adds, 0 to 3 |

With the [difficulty](../difficulty-mod/README.md) mod: both apply and **multiply**. Its "damage
dealt" and "damage taken" sliders scale the same hits as this mod's curve and tiers (the difficulty
mod's on the hit an enemy reads and on Ratchet's hit, this mod's where the hit is posted and on
Ratchet's hit), so with damage dealt x0.5 and a Beta weapon in the second challenge mode a hit is
x0.5 x 1.5 / 1.5. Switch **Tougher enemies each new game** off to leave the enemies to the
difficulty mod alone.

The tiers are kept with each save: the mod's data folder has a `tiers.txt` with the tiers at each
save the game made (by the save's own clock). Nothing is written to the memory card or its folder.

It works with the [item randomizer](../item-randomizer/README.md) (they take none of the same hook
points) and with the [difficulty](../difficulty-mod/README.md) mod (its prices and max ammo are the
base the tiers multiply). In a [multiplayer](../multiplayer/README.md) party everyone plays with the
host's tiers.

## How it works

Through OpenRAC's rac2 hook points of SDK 1.14.6 (the addresses are NTSC-U v1.01's):

| | Where |
|---|---|
| For sale | `rac2.vendor.build_list` (`func_002F6608`): an upgrade entry for each weapon whose next tier is for sale, after the game's; `rac2.vendor.price` (`func_002F6510`) prices it; `rac2.item.upgrade` (`func_002887B0`), which the vendor's purchase calls once it is paid, takes the tier instead of the game's Mega step |
| Experience | each tick, a weapon at its last version whose experience (which the game keeps adding up there) reaches the tier's takes it; its experience starts again from 0, as the game's own Mega purchase does |
| Damage | `rac2.combat.post_hit` (`func_002A8E20`): a hit posted to anyone but Ratchet while he holds a tiered weapon has its damage scaled for that call |
| Most ammo | the last version's row (+0x8E) in the item table, rewritten each tick from what the game (or another mod) has there; a tier fills the ammo |
| Enemies | the save's challenge mode counter (+0x12 of its block); their health as the damage they take in `rac2.combat.post_hit` divided by the curve, the damage they deal in `rac2.player.take_damage` (`func_L00_002A4EF8`) times it, a fraction of a point carried to the next hit |
| Tint | `rac2.item.colors` (`func_00336F20`): the quick select's and the weapons screen's icons and names |

Not yet:

- The tint shows in the quick select and on the weapons screen; the vendor's list and the HUD's
  weapon icon draw their own colours and keep the game's.
- The vendor's upgrade entry is the game's own kind of entry: its icon and wording are the game's.
- The weapon's name in the game's own menus is the game's; the tier's name is the line at the top.
- Damage scales by the weapon in Ratchet's hand: a Synthenoid's or a turret's shots scale by the
  weapon held when they hit.
- No fire rate change, and RYNO II Ultima's split volley is a follow-up: Ultima is the damage boost.
