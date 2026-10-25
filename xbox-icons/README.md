# Xbox button icons

Ratchet & Clank's PlayStation 2 button icons as an Xbox controller's, for OpenRAC's native port
(PAL and NTSC-U discs), painted in the game's own style: the same dark, glossy buttons, with an
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
`https://github.com/OpenRAC/rac1-mods`), switch it on and play. It only changes pictures: which
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
texture table (`src/xbox_icons.h`); when their cells and palette are the game's own (a hash, the
same in every level), it paints them (`src/paint.h`), and the next frame draws the Xbox buttons.
Painted fonts are no longer the game's, so they are painted once a level; fonts the mod does not
know, or a bank still loading, are left alone. It patches no code and takes no hook point.

Painting starts from the game's own pixels, so the mod carries none of the game's pictures: at
each texel of the face buttons, the least coloured of the four gives the sphere without its
symbol; the shoulders are its left half, its middle column and its right half. The letters
(`src/letters.h`, written by `tools/letters.py`) take palette entries no texel uses any more, the
symbols' own among them; every entry a texel outside the cells uses stays as it was.

The [tests](tests/) paint fonts made in the test (nothing outside the cells changes, no symbol is
left, each letter is there in its colour, and nothing changes when the palette is full) and load
the library as the port does, at each disc's addresses. With `OPENRAC_RAC1_LEVEL` naming a level
that `openrac-extractor --decompile` wrote, they paint the game's own fonts too.

To see the icons without the game, `xbox_icons_preview` paints a decompiled level's fonts and
writes their row, before and after, as a picture:

```sh
build/xbox-icons/xbox_icons_preview decompiler_out/rac1/levels/01 icons.pam
```

## Building

From the repository's top (see [README.md](../README.md#building)).
