# Shoulder camera

Aim over Ratchet's shoulder in Ratchet & Clank: Going Commando, on OpenRAC's
native port (NTSC-U, PAL and NTSC-U v2.00), as in a modern third-person
shooter. And choose how far behind him the camera stays the rest of the time.
With the mod off, or its settings as they come, the camera is the game's.

These came from the built-in keyboard and mouse (OpenRAC/OpenRAC#874), which
keeps to the game as it was: they move the game's camera, so they are a mod.

## What you get

**Aim over the shoulder** (a key, unbound at first; or the pad's L2): on foot,
while it is held (or from one press to the next, with *Aim toggles*):

- Ratchet strafes: the mod holds L2, Going Commando's strafe, so he faces where
  the camera looks and the stick (or the movement keys) moves him sideways and
  back;
- the camera slides in along the line it looks along, to *Distance while
  aiming* (0.6 of where it was), so it keeps looking where it did; it goes back
  out when you stop;
- the screen's edges darken (*Vignette*), and a *Crosshair* shows where the
  shots go.

**The crosshair is not the screen's middle.** Going Commando's weapons fire
level, the way Ratchet faces, from his gun, and the game's own aim lifts the
shots to an enemy near that line. The crosshair is drawn 30 units ahead of the
gun on that line, so it shows where a shot goes with no enemy to lift it to.

**Camera distance** (1.0): how far behind Ratchet the camera stays when you do
not aim, as a share of the game's, from 0.6 to 1.15. Farther than about 1.2 the
game drops the camera's distance goal and the camera pulls in (measured in
game), so the setting stops at 1.15. Where the game places the camera itself (scenes, set pieces) it stays the
game's.

| Setting | Default | |
|---|---|---|
| Aim over the shoulder | none | the key or mouse button that aims |
| The pad's L2 aims too | off | holding L2 brings the camera in and shows the crosshair as well |
| Aim toggles | off | a press aims and another stops |
| Distance while aiming | 0.6 | how close the camera comes, a share of where it was (0.4 to 0.9) |
| Crosshair | on | where the shots go, while you aim |
| Vignette | on | the screen's edges darken while you aim |
| Camera distance | 1.0 | how far behind him the camera stays out of the aim (0.6 to 1.15) |

The aim works on foot in play: not in a menu, a dialog or a scene, not while
swimming, grinding or as Giant Clank.

## With the built-in keyboard and mouse

The built-in's own *Aim* key is the first-person view (L1). Its *Modern* preset
puts it on the right click: to aim over the shoulder with the right click
instead, give the built-in's Aim another key (or none) and this mod's *Aim over
the shoulder* the right click. The mouse turns the camera as usual while you
aim.

*The pad's L2 aims too* reads L2 as the game gets it, so the built-in's strafe
key, its *Dodge* and its *Strafe while firing* (which press L2) bring the camera
in as well: leave it off when you play with those.

## Needs

An OpenRAC whose Going Commando has the hook point
`rac2.camera.follow.read_stick` (mod SDK 1.40.1): the follow camera's stick
read, by which time the game has set the tick's goals for the eye's distance and
height, which the mod changes before the camera eases towards them. An older
OpenRAC does not load the mod and says why.

## How it works

- `src/rac2/shoulder_camera.cpp`: the pad report (the aim's key or L2, then L2
  held while aiming), the stick read's hook (the eye's distance and height and
  their goals, data `+0x18C`, `+0x190`, `+0x1A0`, `+0x1AC`; out of the aim the
  distance goal times *Camera distance*) and the overlay (the vignette, and the
  crosshair projected 30 units ahead of the gun along Ratchet's facing).
- `src/rac2/view.hpp`: the maths alone (the aim held or toggled, its look
  eased in over 8 ticks, the eye slid in along its view line: the distance
  shorter and the height lower by the view's pitch times the distance it came
  in).
- `src/rac2/discs.hpp`: each disc's addresses (the hero, his state and health,
  the game's state, the last view), from OpenRAC's `rac2_addresses.h`.

## Tests

`shoulder_camera_view` checks the maths alone; `shoulder_camera_load` loads
the library as the port does, against a game memory of its own at the PAL
disc's addresses (the aim held and toggled, L2 in the report, the eye in and
back out, the overlay, the camera distance, the pad's L2, no aim when paused or
swimming).
