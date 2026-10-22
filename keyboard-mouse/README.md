# Keyboard and mouse

Play Ratchet & Clank on OpenRAC's native port with a keyboard and mouse:
WASD to move, the mouse to aim the camera, left click to fire. Works with the
PAL (v2.00) and NTSC-U discs. A gamepad keeps working alongside it, so you can
switch between them whenever you like.

Without this mod, OpenRAC has a fixed keyboard layout of its own (WASD and
IJKL as the two sticks) and the mouse does nothing. With it on, that layout
is replaced by the bindings below, and you can change every one of them.

## Install

1. Open the OpenRAC launcher and go to **Mods**.
2. Choose **Add from a link** and paste `https://github.com/OpenRAC/rac1-mods`.
3. Find **Keyboard and mouse** in the list and choose **Install**. It is
   switched on straight away.
4. Play.

You need an OpenRAC with mod interface 1.2 or newer. With an older one, the
mod's row says that it needs a newer OpenRAC, and the game starts without it.

To install by hand, download `keyboard-mouse-<version>.zip` from the
[releases](https://github.com/OpenRAC/rac1-mods/releases), then choose
**Add a mod** in the launcher's **Mods** tab and pick that file.

## Default controls

| Action | Keys | PS2 button |
|---|---|---|
| Move | W A S D | left stick |
| Walk (hold) | Left Alt | left stick, half way |
| Camera | the mouse | right stick |
| Jump | Space | Cross |
| Fire | Left click | Circle |
| Quick Select (hold) | Right click | Triangle |
| Wrench | F, middle click | Square |
| Crouch | Left Ctrl, C | R1 |
| Strafe | Left Shift | L2 |
| First-person view (hold) | V, the mouse's back side button | L1 |
| Map | Tab, M | Select |
| Camera behind Ratchet | Q | none (the mod does it) |
| Pause | Escape, P | Start |
| Quick Select slots 1 to 8 | 1 to 8 | none (equips the item at once) |
| Next / previous weapon | wheel down / wheel up | none |
| Menus: confirm, back | Enter, Backspace | Cross, Triangle |
| Menus: move | the arrow keys | d-pad |

- **Quick Select.** Hold right click to open the ring. Move the mouse
  towards the slot you want, then let go to take that item. Slot 1 is at the
  top, and the slots go clockwise from there. Small movements don't count, so
  the ring stays where it is until the mouse has moved a little way (the
  dead zone setting below). Moving the other way changes slot at once.
- **Number keys.** Keys 1 to 8 equip whatever is in that slot of the Quick
  Select, without opening the ring. The wheel steps through the slots that
  hold something. In a vehicle, these keys do nothing.
- **Strafe.** Left Shift holds L2. With the [lock-strafe](../lock-strafe/README.md)
  mod in its *Hold L2 or R2* mode, holding Left Shift strafes.
- **Camera behind Ratchet.** Q swings the camera round behind him and
  levels it. Moving the mouse stops the swing.
- **The cursor.** It is hidden and kept inside the window while you play.
  You get it back in menus, on the pause screen, when you switch to another
  window, and when OpenRAC's own overlay (F3) is open.
- **There is only one Quick Select** in the first game. It holds eight slots.
  In a vehicle, right click opens the vehicle's own ring.

## Changing the controls

In the launcher, go to **Mods** and choose **Settings** on the Keyboard and
mouse row.

- **Rebind an action.** Every action shows its keys as buttons. Click one,
  then press the key, click the mouse button or turn the wheel you want for
  it.
  - Each action can have two binds. Click **Add** to give it a second one.
  - The **×** next to a bind removes it.
  - To cancel while the launcher is waiting for a key, click anywhere else.
- **Clashes.** If two actions share a key, both are marked *Also bound to …*.
  The game would do both at once, so give one of them another key. A key
  used both by a menu action and by an in-play action is not a clash, because
  the menu actions only count in menus.
- **Presets.** The **Controls** row sets every binding at once:
  - **Default**: the layout above.
  - **Left-handed**: move with I J K L; Right Alt walks; H is the wrench;
    Right Ctrl or N crouches; Right Shift strafes; U is first-person view;
    O swings the camera behind Ratchet.
  - **Arrow keys**: move with the arrow keys; Right Alt walks; Right Ctrl
    crouches; Right Shift strafes.

  You can change single bindings after choosing a preset.

These settings also adjust how the mouse and keys feel:

| Setting | Default | What it does |
|---|---|---|
| Mouse sensitivity | 1.0 | How far the camera turns for a given mouse movement |
| Mouse acceleration | 0 | Above 0, a fast flick turns further than a slow drag |
| Invert the mouse's up and down | off | Mouse up tilts the camera down |
| Camera tilt | Stays | *Stays*: the camera keeps the tilt you give it. *Levels out*: it comes back level when the mouse rests |
| Capture the cursor in play | on | Off leaves the cursor visible; the mouse then doesn't turn the camera |
| Walk speed | 0.5 | How fast the walk key moves Ratchet, from 0.2 to 0.9 |
| Stick ramp (ms) | 60 | How long the movement keys take to reach full speed: a tap nudges, holding runs |
| Quick Select dead zone | 30 | How far, in pixels, the mouse has to move before the ring picks a slot |

Changes take effect the next time the game starts. Your settings are saved
with your other launcher data on your computer, not in the folder where
OpenRAC is installed, so updating OpenRAC or the mod keeps them.

## Good to know

- The camera works the way the console's right stick does. The mouse sets
  how fast the camera turns, so it eases in and out as it does with a pad;
  this is not a direct mouse aim.
- If OpenRAC's own keys (F2, F3, F9 to F11) are bound to an action, they
  still do OpenRAC's thing as well.
- When a bug report asks for it, the game's log shows the line
  `[mod keyboard-mouse] keyboard and mouse on the pal disc`, plus a warning
  for any clash between bindings.

## Building

See the [repository's README](../README.md#building). The tests check:

- reading the binds and finding clashes;
- how the keys move the stick and how the mouse turns the camera;
- how the mouse's direction picks a Quick Select slot;
- the report the game reads.

The library is also loaded the way the game loads it: `ctest -R keyboard_mouse`.

## Licence

GPL-3.0-or-later, as the rest of this repository.
