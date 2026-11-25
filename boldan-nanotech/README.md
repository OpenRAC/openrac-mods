# Boldan Nanotech at any hour

A native mod for **Ratchet & Clank: Going Commando** (NTSC-U v1.01, PAL and NTSC-U v2.00).

On Boldan, the Nanotech Upgrade by the clock tower only appears on an even-numbered hour of the
PlayStation 2's clock (12, 2, 4, ... on the twelve-hour dial). With this mod it is there at every
hour, and Ratchet collects it as he would on an even hour: the game gives the Nanotech and keeps it
in the save as usual. The clock tower keeps its own time, hands and chimes, and whatever else it
does at its hours (at 3 o'clock) is unchanged.

It has no settings. Once the upgrade is collected the mod does nothing more.

## How it works

The clock tower's update (`UpdateBoldanClock` in the decompilation,
`src/levels/13_boldan/L13_004067F8-g8.c`) reads the console's clock when Boldan loads and keeps its
own time from then on. Once a second of that time it shows one moby on an even hour and hides it on
an odd one: the Nanotech Upgrade (class `0x11C4`), whose moby array index is the clock's pvars
`+0x28`. Hidden is flags `|= 0x43`, `0x1000` cleared and no collision. Shown is `0x41` and `0x2`
cleared, `0x1000` set and the class's collision back.

The mod takes the clock's update, OpenRAC's hook point `rac2.moby.boldan_clock.update`
(`Moby_BoldanClock_Update`, `void(u32)`). It runs the game's own update first, then, while the
clock's hour is odd, shows the upgrade again exactly as the clock does on an even hour. So the
upgrade is never drawn hidden, and the mod shares nothing with other mods. An upgrade already
collected (its state past 1) is left alone.

| Disc | `Moby_BoldanClock_Update` | `g_mobyTableBase` |
| --- | --- | --- |
| NTSC-U v1.01 (`ntsc`) | `func_L13_004067F8` | `0x001B1B5C` |
| PAL (`pal`) | `func_L13_0040B090` | `0x001B1B5C` |
| NTSC-U v2.00 (`ntsc2`) | `func_L13_00407D60` | `0x001B1ADC` |

The hook point is new in OpenRAC (OpenRAC/OpenRAC#918). On an OpenRAC without it, the mod finds
the clock itself, in the level's moby array (`g_mobyTableBase`, the loaded level's copy through
`relocate_data`). It tells the clock from every other moby by its pvars: `+0x10` and `+0x14` are
its minute and hour hands (classes `0xE9F` and `0xEA0`), and `+0x28` the upgrade. It shows the
upgrade right after each of the clock's updates (OpenRAC's moby gate, `on_moby_update`). If
another mod holds the gate, it does it on each tick instead, a frame later.

The addresses come from OpenRAC's `port/game/rac2-<disc>/host/rac2_addresses.h` and `libraries.tsv`. The level's own code and
data (the clock, its pvars and the moby fields) are the same on the three discs.

## Tests

`boldan_nanotech_test` builds the mod's code against a host table and game memory of its own, on
each disc. It lays out Boldan's mobys at that disc's address with the clock and its hands. With
the hook point, it checks that the upgrade the game's update hid on an odd hour is shown again once
that update returns, and that the mod then takes no tick or gate. Without it, it checks that the
upgrade is shown again after the clock's update and on the tick. On an even hour, once collected, off Boldan, without the clock's hands, or at another
disc's address, the upgrade is left as it is. It also checks that the mod refuses a game or disc
it does not know.
