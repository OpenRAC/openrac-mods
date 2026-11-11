# Xbox button icons

Ratchet & Clank's and Going Commando's PlayStation 2 button icons as an Xbox controller's, for
OpenRAC's native port (Ratchet & Clank: PAL and NTSC-U; Going Commando: NTSC-U, PAL and NTSC-U
v2.00), painted in the game's own style: the same dark, glossy buttons, with an
Xbox letter where the PlayStation symbol was. Everywhere the game prints a button: help
messages, menus, the save screens, the vendor.

With the mod off, the game is the PlayStation 2 game.

| The game's | Becomes |
|---|---|
| Cross | **A**, green |
| Circle | **B**, red |
| Square | **X**, blue |
| Triangle | **Y**, yellow |
| L1, R1 | **LB**, **RB** |
| L2, R2 | **LT**, **RT** |
| the analog sticks | kept: they are an Xbox controller's too |

The face buttons keep the game's sphere and its green, blue and red (the Triangle's, the Cross's,
the Circle's), with a yellow as bright for Y and the darker ring round the letter the game's
symbols have. The shoulders, which the game draws as square tabs, are pills of the same sphere,
so the eight buttons are one set.

## Playing

Install it from the launcher's **Mods** tab (**Add from a link**:
`https://github.com/OpenRAC/openrac-mods`), switch it on and play. It only changes pictures: which
button does what is the game's (or [button-swap](../button-swap/README.md)'s, which it works
with: the prompts show the buttons the game asks for).

| Setting | Default | |
|---|---|---|
| Trace | off | For bug reports: a line in the game's log each time the fonts are painted, or found not to be the game's |

The game's words stay as they are: a message that says START or SELECT in letters still does.

## How it works

The game prints a button as a glyph of its three fonts (bytes 0x10 to 0x19 of a text): 24 x 16
cells of the fonts' row 96, each font an effect texture of 8-bit texels with one palette of 256
colours for the three. A level keeps its effect bank in game memory, and the game sends the fonts
to the GS from there every frame. After each game tick the mod finds the fonts through the effect
texture table (`src/rac1/rac1.c`, `src/rac2/rac2.c`); when their cells and palette are the game's
own (a hash, one per set of fonts the game has), it paints them (`src/common/paint.h`), and the next
frame draws the Xbox buttons.
Painted fonts are no longer the game's, so they are painted once a level; fonts the mod does not
know, or a bank still loading, are left alone. It patches no code and takes no hook point.
Going Commando's title (its main menu, Options and the memory card's questions) loads only the
first two fonts, with a palette of its own: those two are painted the same way.

Painting starts from the game's own pixels, so the mod carries none of the game's pictures: at
each texel of the face buttons, the least coloured of the four gives the sphere without its
symbol; the shoulders are its left half, its middle column and its right half. The letters
(`src/common/letters.h`, written by `tools/letters.py`) take palette entries no texel uses any
more, the symbols' own among them; every entry a texel outside the cells uses stays as it was.

The [tests](tests/) paint fonts made in the test (nothing outside the cells changes, no symbol is
left, each letter is there in its colour, and nothing changes when the palette is full) and load
the library as the port does, in each game at each disc's addresses. With `OPENRAC_RAC1_LEVEL` or
`OPENRAC_RAC2_LEVEL` naming a level that `openrac-extractor --decompile` wrote (`core_index.bin`
and `unpacked/core_data.bin`), they paint the game's own fonts too.

## Going Commando

Going Commando keeps Ratchet & Clank's fonts as they were: the same three effect textures (256 x
128, one palette), the same glyph bytes in the same cells of row 96, so the mod paints them the
same way. In five of the NTSC-U levels and in every level of the other two discs, the fonts are touched up
(some letters, a few texels of the buttons), and Barlow's palette is a shade apart; the mod knows
all four sets (`src/rac2/rac2.c` says where they were read). The title's menus (the main menu, its
Options and the memory card's questions, Yes and No among them) load only the large and the small
font, with a palette of their own, and the mod knows that pair on each disc too. Only where the
effect bank and its table are differs by disc:

| Disc | Effect bank (word) | Effect texture table |
|---|---|---|
| NTSC-U (`ntsc`) | `0x001B1604` | `0x001B9740` |
| PAL (`pal`) | `0x001B1604` | `0x001B9740` |
| NTSC-U v2.00 (`ntsc2`) | `0x001B1584` | `0x001B96C0` |

The game prints a button in its help messages, its menus and the vendor's, as Ratchet & Clank
does; a button the game draws as a picture of its own rather than a letter of a font (the HUD's
icons, the Options page's) stays as it was.

To see the icons without the game, `xbox_icons_preview` paints a decompiled level's fonts and
writes their row, before and after, as a picture:

```sh
build/xbox-icons/xbox_icons_preview decompiler_out/rac1/levels/01 icons.pam
```

## Building

From the repository's top (see [README.md](../README.md#building)).
