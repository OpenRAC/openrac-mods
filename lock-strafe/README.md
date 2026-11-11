# Lock-strafe

Strafing for Ratchet & Clank, as Going Commando has it, for OpenRAC's
native port (PAL and NTSC-U discs): Ratchet faces where the camera looks
while the left stick moves him, his hips and legs turn toward where he goes
while his body stays on the camera, and X with the stick pushed sideways or
back flips that way. With the mod off, Ratchet turns toward the stick and
runs where he faces, as on the PlayStation 2. How each part was taken from
Going Commando's code, with its addresses and values: [DESIGN.md](DESIGN.md).

## What you get

The setting **Mode** chooses one of two:

| Mode | Value | Controls on foot |
|---|---|---|
| Lock with weapons (the default) | `lock` | With a forward-firing weapon ready in hand (the Suck Cannon, the Bomb Glove, the Devastator, the Blaster, the Pyrocitor, the Tesla Claw, the Morph-o-Ray, the R.Y.N.O.), Ratchet always faces the camera's way: the **right stick** turns the camera and him with it, standing or running, and the left stick strafes. The camera does not swing round to follow where he goes. With anything else in hand (the wrench, the Swingshot, the gadgets, and the weapons that do not fire ahead: the Visibomb, the Taunter, the Mine Glove, the Walloper, the Glove of Doom, the Drone Device, the Decoy Glove), Hold |
| Hold the strafe button | `hold` | Hold **L2 or R2** (or the one **Strafe button** picks): Ratchet turns to face the camera's way (with the Classic feel, the camera swings round once behind him instead). From then on he faces the camera's way: the **right stick** turns the camera and him with it, and the left stick moves him sideways, back and forward without turning him toward it (jumps too); going back, the walk or the run plays backwards. Let go: the game's turning again, the camera left where it is |

**Strafe button** chooses the button held to strafe:

| Strafe button | Value | |
|---|---|---|
| L2 or R2 (the default) | `both` | Either strafes; on foot the game gets neither, as before 1.2.0 |
| L2 only | `l2` | L2 strafes; R2 stays the game's (R1 or R2 crouches) and free for other mods, such as one that raises the wrench on R2 |
| R2 only | `r2` | R2 strafes; L2 stays the game's (L1 or L2 looks in first person) and free for other mods |

**Feel** chooses how it plays:

| Feel | Value | |
|---|---|---|
| Going Commando (the default) | `commando` | As Ratchet & Clank 2: pressing L2 or R2 turns Ratchet to the camera and leaves the camera where it is; he turns at Going Commando's strafe rates (quicker on the ground, its own in the air); going from one side to the other stops him before he goes the new way, instead of sliding straight across; the walk or the run plays backwards only within 45 degrees of straight back |
| Classic (1.0) | `classic` | The mod as it was: Hold swings the camera behind him, the first game's own turn rates, the backward walk from 110 degrees off |

Apart from the feel:

- **Hip twist** (on): strafing with the stick pushed, his pelvis and legs turn
  toward where he goes and his upper body turns back to face ahead, as in
  Going Commando. Walking sideways, his legs walk sideways under a body that
  still aims where the camera looks. It needs an OpenRAC whose mods may turn
  the hero's joints (OpenRAC PR "let any mod turn the hero's joints"); on an
  older one he strafes without it.
- **Strafe flips** (on): strafing, standing or running, **X** with the stick
  pushed all the way left, right or back does the first game's side flip or
  back flip that way (its own animations), as Going Commando's strafe does.
  Forward, or with the stick not all the way, X jumps as usual.

In both, **L1** still looks in first person and **R1** still crouches (and
crouch-jumps); L2 and R2 strafe (or the one **Strafe button** picks, the other
left to the game). **Trace** (off) writes a line in the game's
log for each pad report, for bug reports (as does `OPENRAC_STRAFE_TRACE=1`
in the environment).

**Known limitations.** The first game has no backward walk for Ratchet
(Going Commando added one): going backwards plays his walk or run in reverse.
Going Commando never does its running jump while strafing; here strafing
forward and pressing X can still give the first game's running jump (its
longer leap), which the run's own code picks after every hook point the mod
has. He strafes through it all the same: he keeps facing ahead and the stick
steers him.

**The crosshair.** The first game has no crosshair in the third-person view:
the Blaster's reticle is drawn only in first person (L1), and the Devastator
marks what it has locked on. Both stay as the game has them; the mod adds
none.

## Where the two modes come from

The later games. Going Commando added a strafe on a held shoulder button (L2
or R2 with the left stick: he walks sideways while facing forward, without
turning), and Up Your Arsenal a control option, Lock Strafe, that keeps the
strafe on and plays like a third-person shooter, beside its Third Person and
First Person modes. Sources: the Ratchet & Clank Wiki's Going Commando
controls (ratchetandclank.fandom.com/wiki/Going_Commando_controls: "Left
analog stick + L2", walk sideways facing forward), a review of the HD
collection on Super Phillip Central (2015: the strafe on R2 on the
PlayStation 2 and 3), the wiki's walkthrough of Up Your Arsenal's first
mission (ratchetandclank.fandom.com/wiki/Save_Veldin!: the three modes, Lock
Strafe the developers' choice) and GameSpot's review of Up Your Arsenal (a
"strafe-locked, third-person mode" that controls like a shooter). Going
Commando's own strafe takes either L2 or R2; so does this mod.

## How it works

**How the game turns the stick into a heading**, each tick in three steps:
the stick handler (`Hero_SetHeadingFromStick`) sets the hero's move yaw
(`+0x180`) to the stick's angle plus the camera's yaw (`D_L00_00166ED8`);
the state code eases his facing toward the move yaw (`Hero_TurnToHeading`);
and the run builds his velocity along his facing (`Hero_SetPlanarVelocity`
given an angle above pi). He turns first, then runs where he faces. The game
has a strafe of its own in one state: in state 0x81 with L2 or R2 held, the
state puts the camera's yaw in the move yaw around its turn, and the follow
camera's per-tick requests (`FollowCamera_ApplyOverrides`, PAL
`func_L00_002E74B0`) switch to a strafe camera that slides along with him on
a stiff lateral spring.

**L2 and R2.** On foot the game gives the shoulder buttons in pairs: L1 or
L2 enters first-person look from standing (`held & 5`), R1 or R2 crouches,
crouch-jumps and slides out of a run (`held & 0xA`); nothing else on foot
tests L2 or R2 alone but state 0x81. With the mod on, in play (game mode 0)
on foot (`vehicle_mode` 0) and outside state 0x81, it takes L2 and R2 out of
the pad's state at each report (`Pad_ProcessReport`), as the game's own input
locks (`PadState.mode`) take buttons out for a poll, and keeps them for the
strafe. So L1 still looks and R1 still crouches, and L2 and R2 strafe. With **Strafe
button** on L2 only or R2 only, only that bit is taken and kept; the other
reaches the game, and the mods hooked after this one, untouched.
Scenes, menus, vendors, vehicles, state 0x81 and the hoverboard (control mode
`0x16`, whose tricks are on L2 and R2) keep them.

**The strafe**, in [src/rac1/lock_strafe.cpp](src/rac1/lock_strafe.cpp), in the run
(states 2 and 0x73), the jumps (7, the running jump 9 and the double jump
0xE) and standing (0), on level ground:
`Hero_TurnToHeading` eases the facing toward the camera's yaw, and while
Hold's swing lasts toward the facing latched when L2 or R2 went down;
`Hero_SetPlanarVelocity` builds the velocity along the move yaw, as the
game's own run start from a standstill does; the follow camera asks for the
game's strafe camera, which slides along with him and does not turn. With
the Classic feel, Hold's swing turns the camera behind the latched facing through the game's own
request to look along a heading (PAL `func_L00_002E9DC8`, what the level's
camera volumes use: a turn standing in for the right stick's, which the
stick overrides). It is asked for only from the press until the camera is
within 0.05 rad of that facing, the right stick is pushed 0.3 or more
sideways (the game's own test for the stick moved) or 120 ticks have gone
by; after that nothing turns the camera toward the old facing, so the right
stick's turn stays, and Hold plays as Lock does. Both turn him standing too,
early in his update (`Hero_UpdateMotionInput`), at the rates state 0x81 uses
(Hold once its swing is done).

**The backpedal.** The game has no animation for walking sideways or back.
In the run the walk (sequence 3) or the run (4) loops: the walk/run routine
(PAL `func_L00_002293E8`) sets its speed (hero `+0xA90`) from his ground
speed, and Ratchet's own animation advance (`Hero_AdvanceAnimation`, PAL
`func_L00_00232EF0`) moves it on by that speed times the key's rate
(`+0xA94`). That advance only goes forward (a negative speed would leave the
blend below 0 without ever stepping a key back; the moby's own `+0x58`,
which plays other mobys backwards, is not what moves Ratchet). So while he
strafes with the move yaw more than 110 degrees from his facing (until it is
back under 100), the mod steps the walk or the run back itself, by what the
advance would add, and leaves the blend that much lower, so the advance ends
where the step back did and does the rest (sounds, the items that follow his
speed, the distance moved) as the game does. The cycle plays backwards at
the speed the game chose. Only a sequence looping on itself is stepped (keys
A and B in it, no blend from another sequence, no sub-range or restart
pending); the blend into the run from a standstill plays as the game has it.

Everything else is the game's code: each of the six hook points
(`Pad_ProcessReport`, `Hero_UpdateMotionInput`, `Hero_TurnToHeading`,
`Hero_SetPlanarVelocity`, `FollowCamera_ApplyOverrides`,
`Hero_AdvanceAnimation`) calls what it replaced, the game's own or another
mod's, and whenever the hero is not strafing does nothing else. Each disc's
addresses and function names are in [src/rac1/discs.h](src/rac1/discs.h); the decision
is in [src/rac1/strafe.h](src/rac1/strafe.h), apart from game memory, and tested by
`hero_strafe` (ctest); `lock_strafe_load` loads the library against a host
table of its own and checks its hooks.

**Going Commando's feel.** Its turn replaces the rates the game passes to
`Hero_TurnToHeading` while he strafes: accel 0.037 and damping 0.25 times
the tick's `g_frame_scale_sq`, at most 12.2 radians a second (0.015, 0.2 and
15 in the jump), rac2's `func_L00_002B35A0` and jump group. Hold latches
without a swing. The side reversal is rac2's: two five-tick timers, one per
side; going left while the right is recent takes his speed (`ground_speed`,
`+0x194`) to 0 before `Hero_SetPlanarVelocity` builds his velocity, going
right while the left is recent lets it back by quarters.

**The hip twist** (rac2's `func_L00_002B7B80`): each tick, from the animation
hook, the pelvis (joint 1, parent of the spine, both legs and the tail) eases
toward the move yaw from his facing and the first spine joint (2) toward minus
that, on Going Commando's springs (accel 0.037 and 0.025, damping 0.27; they
go through 0, never round the back). Both turns are about the joint's own z,
a yaw, since every joint of Ratchet's skeleton stands as his model does. They
go to the hero through the SDK's `hero_set_joints`, composed after his
animation, and are taken off when the springs are back at rest, or at once
when he leaves his feet (a vehicle) or play. No twist while backpedalling or
with the stick near its centre, as in rac2.

**The strafe flips**: from the motion input hook, after the game reads the
stick and before his state's code sees X: strafing in the stand or the run,
X pressed this tick, the stick past 0.9 and `stick_direction_vs_facing` (PAL
`func_L00_00222580`) not forward, the mod asks for state 0xB
(`HeroEnterMoveState`, PAL `func_L00_00222B80`), the first game's side and
back flip, which Going Commando's strafe flip is the same state as. It picks
its own animation from the stick (0x1C left, 0x1D right, 0x1F back) and
flies as the game has it; the mod starts its drift at Going Commando's 5
units a second. If the game refuses the state, its own jump goes on.

**Not done.** A level's camera (a wall, a camera volume) can still turn the
camera while L2 or R2 is held, and he turns with it.

## Settings

| Key | Type | Default | |
|---|---|---|---|
| `mode` | choice: `lock`, `hold` | `lock` | Lock with weapons, or Hold the strafe button |
| `button` | choice: `both`, `l2`, `r2` | `both` | the button held to strafe: L2 or R2, L2 only or R2 only (the other left to the game and other mods) |
| `feel` | choice: `commando`, `classic` | `commando` | Going Commando's turn, stop and backpedal, no swing; or the mod's 1.0 behaviour |
| `twist` | bool | true | the hip twist |
| `flips` | bool | true | X with the stick pushed sideways or back flips that way |
| `trace` | bool | false | a line in the log each pad report: the mode, what the strafe does, the hero's state, item, buttons, whether Hold is latched and still swinging, the latched yaw, the right stick sideways, his facing, move yaw, the camera's yaw, his position, whether he backpedals, and his animation (sequences A/B, keys A-B, blend, speed), the feel, the twist's two angles and the side timers |

## History

In OpenRAC 0.8.0 to 0.8.4 the strafe was an option of the port's own, the row
**Strafe (experimental)** on the title menu's Video page (the `strafe` key of
the port's settings: `off`, `hold`, `lock`). It became this mod when OpenRAC
took mods as plugins (docs/port/MODS.md); with the mod off, the game is the
console's. Its history in OpenRAC: `d5fe4f64` (the first lock-strafe, PAL and
US), `e67d5a80` and `0ca783b9` (Hold L2-R2 and Lock with weapons), `17a034ea`
(Hold follows the camera after its swing), `81e687b2` (the walk or run played
backwards while strafing back). It was written in C on the interface's table at first, and
is now the SDK's reference C++ mod (openrac/mod.hpp), with the same behaviour and tests.
In 1.1.0 it took Going Commando's strafe (DESIGN.md): its feel, the hip twist
and the strafe flips, laid out for several games (docs/MULTI_GAME.md).
In 1.1.1 the hip twist turns the right way. 1.1.0 turned his hips away from where he
went, so strafing sideways his legs walked the other way (the moonwalk; DESIGN.md).
In 1.2.0 the setting **Strafe button** lets L2 or R2 alone strafe, leaving the
other to the game and to mods that use it (a wrench throw raised on R2).
In 1.2.1 he no longer slides forward as a strafe ends. Stopping (the stick let
go: the game turns the move yaw back to his facing) he slows along the stick's
last yaw; letting go of L2 or R2 while running on (the game turns him to the
stick over a few ticks and runs along his facing meanwhile) his speed follows
the stick until he faces it. A strafe flip goes straight to the side or back
(state 0xB carried his run speed along his facing; the strafe flip carries
none). Where the level puts its own camera (Kalebo III's robot waves, seen
from a wall) he does not strafe: facing that camera's yaw had him aim up at
the wall. L2 and R2 stay the mod's there, so they do nothing (given to the
game, L2 put him in first person, unable to move). The running jump (X while
strafing ahead) and the double jump strafe too: steering in the air no longer
turns him toward the stick.
