# Difficulty: speed fixes

Bugs found with **Enemy speed** off 1.0 in Ratchet & Clank, and what was done.
Every slider at 1.0 is still the retail game: the code below only runs when the
slider is moved.

## Root cause

The mod used to speed an enemy up by scaling the tick's time values during its
update, and by scaling its animation speed. Many enemies fire on an animation
frame: an exact frame (the Blarg bomb thrower throws only on frame 19.0) or a
one-tick window of frames (the Elite Blarg fires on frames 2, 17 and 32). With a
scaled animation, frames are skipped or never land on a whole number, so the
attack never comes out. Things an enemy throws also got scaled per-tick values
(speed, gravity, fuse) and then flew in normal time.

The fix (1.5.2): an enemy at speed k now runs k ticks of its own per game tick,
on average. At 1.5 it runs 3 ticks for every 2 of the game; at 0.5 it runs every
other tick. Each tick runs exactly as in the game: the animation step, the
update, the bounds refresh (the game's moby loop). Nothing is scaled, so every
frame is seen and every timer runs as written. On a skipped tick the animation
is held still.

## The list

| # | Report | What was wrong | Fix | Checked |
|---|---|---|---|---|
| 1 | Gaspar turret fires no shots | The turret you ride was sped up like an enemy; its shot waits for a frame window that a sped-up animation skips | Left out of the speed slider (#166). It never reached players: 1.5.1 was never tagged, so the launcher still gives 1.5.0 | Load test: the class gets the game's one plain call |
| 2 | Hoven turret too fast | Same: the turret you ride was sped up | Left out (#166), same release problem | Load test, as above |
| 3 | Blarg bomb throwers (Batalia) never throw | Throw only on frame exactly 19.0; the scaled animation never lands on it | Ticks of its own | Load test; in game (Batalia, NTSC): 3 of 3 throws at 1.4, was 0 of 3 |
| 4 | Small floating robots cannot shock | Same family: the shock waits for a frame window (scoutbot, 252) | Ticks of its own | In game (Batalia): at 1.4 the shock was missed 4 times in 21 attacks, now 0 in 44 |
| 5 | Elite Blarg fires 1 rocket of 3 | Rockets on frame windows 2, 17, 32; the window is one tick of animation, but the mod gave the game the unscaled speed, so later frames were skipped | Ticks of its own | Load test; in game (Veldin 2): at 1.4 6 rockets in 6 volleys, now 39 in 13 |
| 6 | (found) Bombs and rockets fly wrong when sped up | Their speed, gravity and fuse were set from scaled values | Nothing is scaled now | By design |
| 8 | Floating robots: the shock hits but its beam does not show | The beam is a draw callback the robot adds on its attack frames. A skipped tick (below 1.0) added none, so the beam showed on half its frames at 0.5; a second run (above 1.0) added it twice | A frame keeps the draw callbacks of the last run only; a skipped tick draws again what the last run drew | Load test (one draw at 2.0, one each tick at 0.5); in game (Batalia): beam on 20 of 20 frames at 0.5 (was 10), once per frame at 2.0 |
| 7 | (found) Fix not released | Tags make releases; difficulty-v1.5.1 was never pushed | Push tag `difficulty-v1.5.2` after merge | - |

## Other enemies checked

All timed classes in [classes.tsv](classes.tsv) were scanned in the decomp for
attacks or moves tied to animation frames. The new tick model covers all of
them the same way, so none needs its own fix. These read the animation frame
(exact frame, frame window or frame range) and are worth a look in game:

- Exact frame: blarg bomb thrower (333), bomber tank (253), peckbot (577)
- Frame window: elite commando (638), scoutbot (252), blarg trooper (294),
  woodcut bot (563), sand shark (580), horny toad (749), psytcopus (1231),
  chomper (1445), minibomber (31)
- Frame range: plasmabots (28, 211), sentrybot (44), robosquawker (63), big
  axebot (217), circular saw (340), blarg trooper (427), birdbot (459),
  amoeboids (572, 865, 866), robot dogs (573, 1382), extermibot (623), blarg
  paratrooper (1023), alien snapper (1048), space commandos (1068, 1262), toxic
  crab (1202), puffoid (1246)

The scan reads each update and the helpers it calls in the same file, so a
check deeper down could be missed. The tick model does not depend on the scan.

## Tests

- `tests/rac1/load_test.cpp`: at 2.0 an enemy runs two full ticks
  (refresh, animation step, update) with the game's clock and animation speed;
  at 0.5 it runs every other tick and its animation is held on the skipped
  tick; a turret you ride gets one plain call; at 1.0 one plain call. The old
  code fails this test.
- In game (container, NTSC, Ratchet held near the enemies), old mod / new mod:

  | Enemy | 1.0 | 1.4 | 0.7 |
  |---|---|---|---|
  | Bomb thrower (Batalia), throws / throw animations | 2/2 and 2/2 | 0/3 and 3/3 | 0/1 and 1/1 |
  | Scoutbot (Batalia), shocks / attacks | 31/32 and 31/32 | 17/21 and 44/44 | 19/21 and 24/26 |
  | Elite Blarg (Veldin 2), rockets / volleys | 18/6 and 18/6 | 6/6 and 39/13 | 25/9 and 12/4 |

  At 1.0 both mods give the same numbers. An attack still running when the run
  ends counts as not done. No crash.
- Not tested in game: the turrets you ride (Gaspar, Hoven); the load test checks
  they get the game's one plain call.
