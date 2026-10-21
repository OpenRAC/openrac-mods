# Button swap

Swaps two of the controller's buttons for Ratchet & Clank: **Cross and
Square** (jump and the wrench), **Circle and Triangle**, or **L1 and R1**,
chosen in the mod's **Swap** setting on the launcher's Mods tab. It works for
both controllers, everywhere in the game (menus too), on the PAL and NTSC-U
discs. Switch the mod off and the buttons are the game's again.

It is a **script mod**: a few lines of Luau that the game runs in a sandbox,
with no compiler and the same file on every system. It is the example of
OpenRAC's script mods (docs/port/MODS.md, section 11): one `openrac.on_pad`
callback that changes each pad report before the game reads it.

## Installing

In the OpenRAC launcher, **Mods**, **Add from a link**, paste
`https://github.com/OpenRAC/rac1-mods`, and **Install** Button swap (its row
says *Runs scripts (sandboxed)*). Or add this folder, or its `.zip` from the
repository's releases, with **Add a mod**. It needs an OpenRAC whose mod
interface is 1.1 or later (the first with script mods).

## How it works

The port's pad library hands the game each controller's report as the
console's `scePad2Read` fills it in
([port/game/common/lib/pad.c](https://github.com/OpenRAC/OpenRAC/blob/master/port/game/common/lib/pad.c)),
18 bytes, and calls the mods' pad callbacks with it first:

| Bytes | |
|---|---|
| 0-1 | the buttons, a 16-bit word, little-endian; a bit is **clear** while its button is held |
| 2-5 | the right stick (x, y), then the left (x, y), 0x80 at rest |
| 6-17 | each button's pressure, 0xFF held, 0 not, in the order right, left, up, down, triangle, circle, cross, square, L1, R1, L2, R2 |

The buttons' bits in the word:

| Bit | Button | Bit | Button |
|---|---|---|---|
| 0 | Select | 8 | L2 |
| 1 | L3 | 9 | R2 |
| 2 | R3 | 10 | L1 |
| 3 | Start | 11 | R1 |
| 4 | Up | 12 | Triangle |
| 5 | Right | 13 | Circle |
| 6 | Down | 14 | Cross |
| 7 | Left | 15 | Square |

A swap trades the two buttons' bits in the word and their pressure bytes, so
the game sees each one held exactly when the other is.

GPL-3.0-or-later, as the rest of rac1-mods.
