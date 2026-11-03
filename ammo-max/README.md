# Ammo at the most

At the Gadgetron vendor in Ratchet & Clank, for OpenRAC's native port (PAL and NTSC-U discs),
buying ammo starts at the most you can buy instead of 1: the rounds the weapon has room for, or
as many as your bolts pay for when they pay for fewer.

With the mod off, the game is the PlayStation 2 game.

## Playing

Install it from the launcher's **Mods** tab (**Add from a link**:
`https://github.com/OpenRAC/openrac-mods`), switch it on and play. At the vendor, choose a weapon's
ammo as usual: the count already says the most.

| You have | Your bolts pay for | The count starts at |
|---|---|---|
| 0 of 160 | 160 rounds or more | 160 |
| 0 of 160 | 40 rounds | 40 |
| 150 of 160 | any number | 10 |

**Down** lowers it, **Up** raises it again up to that most, **Square** buys and **Triangle**
leaves, as in the game. When the game lets you buy no round (the weapon is full, or your bolts
do not pay for one), the dialog is the game's.

| Setting | Default | |
|---|---|---|
| Trace | off | For bug reports: a line in the game's log each time the count is set, with the weapon's ammo, the bolts and the price of a round |

It works with the [difficulty](../difficulty-mod/README.md) mod's ammo prices and max ammo: the
count is from the prices and the maximum the vendor has at that moment.

## How it works

The vendor's buy dialog (VendorBuyDialogUpdate: PAL `func_L00_0029CDE0`, NTSC-U
`FUN_L00_0029ba78`) sets its count to 1 when it opens, and lets Up raise it only to the rounds
the bolts pay for and the weapon has room for. After each game tick the mod reads the dialog's
state; the tick it has just opened on ammo that Up may raise, the mod sets the count to that
same most, once. It patches no code and takes no hook point; with the dialog closed it only
reads one word a tick.

The [tests](tests/) check the count (room, bolts, the dialog's second price, nothing to buy) and
the library as the port loads it, on a vendor laid out in memory at each disc's addresses: the
count set once each time the dialog opens, kept when the player lowers it, and left at the
game's 1 where the game allows no more.

## Building

From the repository's top (see [README.md](../README.md#building)).
