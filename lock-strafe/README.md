# Lock-strafe

Strafing for Ratchet & Clank, as the later games have it, for OpenRAC's
native port (PAL and NTSC-U discs). With the mod off, Ratchet turns toward
the stick and runs where he faces, as on the PlayStation 2.

## What you get

The setting **Mode** chooses one of two:

| Mode | Value | Controls on foot |
|---|---|---|
| Lock with weapons (the default) | `lock` | With a forward-firing weapon ready in hand (the Suck Cannon, the Bomb Glove, the Devastator, the Blaster, the Pyrocitor, the Tesla Claw, the Morph-o-Ray, the R.Y.N.O.), Ratchet always faces the camera's way: the **right stick** turns the camera and him with it, standing or running, and the left stick strafes. The camera does not swing round to follow where he goes. With anything else in hand (the wrench, the Swingshot, the gadgets, and the weapons that do not fire ahead: the Visibomb, the Taunter, the Mine Glove, the Walloper, the Glove of Doom, the Drone Device, the Decoy Glove), Hold L2 or R2 |
| Hold L2 or R2 | `hold` | Hold **L2 or R2**: the camera swings round once behind the way Ratchet faces. From then on he faces the camera's way: the **right stick** turns the camera and him with it, and the left stick moves him sideways, back and forward without turning him toward it (jumps too); going back, the walk or the run plays backwards. Nothing pulls the camera back to where he faced at the press. Let go: the game's turning again, the camera left where it is |

In both, **L1** still looks in first person and **R1** still crouches (and
crouch-jumps); L2 and R2 strafe. **Trace** (off) writes a line in the game's
log for each pad report, for bug reports (as does `OPENRAC_STRAFE_TRACE=1`
in the environment).

**Known limitation: no strafe animations.** The first game has no side-step
or backward walk for Ratchet. Going backwards plays his walk or run in
reverse; going sideways he runs with the run's animation, facing ahead.

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
strafe. So L1 still looks and R1 still crouches, and L2 and R2 strafe.
Scenes, menus, vendors, vehicles and state 0x81 keep them.

**The strafe**, in [src/lock_strafe.c](src/lock_strafe.c), in the run
(states 2 and 0x73), the jump (7) and standing (0), on level ground:
`Hero_TurnToHeading` eases the facing toward the camera's yaw, and while
Hold's swing lasts toward the facing latched when L2 or R2 went down;
`Hero_SetPlanarVelocity` builds the velocity along the move yaw, as the
game's own run start from a standstill does; the follow camera asks for the
game's strafe camera, which slides along with him and does not turn. Hold's
swing turns the camera behind the latched facing through the game's own
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
addresses and function names are in [src/discs.h](src/discs.h); the decision
is in [src/strafe.h](src/strafe.h), apart from game memory, and tested by
`hero_strafe` (ctest); `lock_strafe_load` loads the library against a host
table of its own and checks its hooks.

**Not done.** Sideways he still runs with the run's animation, facing
ahead: no turn of the hips toward where he goes. The engine's joint
modifiers (`AttachManipulator`, the blink and the NPCs' head turns) would
carry one, but they act on the joints a class lists for them, so the hip
would first have to be found among Ratchet's, and turning it turns the
spine and everything above with it, which a second, opposite modifier on
the spine would have to undo, about axes that depend on each joint's own
frame. A level's camera (a wall, a camera volume) can still turn the camera
while L2 or R2 is held, and once the swing is done he turns with it.

## Settings

| Key | Type | Default | |
|---|---|---|---|
| `mode` | choice: `lock`, `hold` | `lock` | Lock with weapons, or Hold L2 or R2 |
| `trace` | bool | false | a line in the log each pad report: the mode, what the strafe does, the hero's state, item, buttons, whether Hold is latched and still swinging, the latched yaw, the right stick sideways, his facing, move yaw, the camera's yaw, his position, whether he backpedals, and his animation (sequences A/B, keys A-B, blend, speed) |

## History

In OpenRAC 0.8.0 to 0.8.4 the strafe was an option of the port's own, the row
**Strafe (experimental)** on the title menu's Video page (the `strafe` key of
the port's settings: `off`, `hold`, `lock`). It became this mod when OpenRAC
took mods as plugins (docs/port/MODS.md); with the mod off, the game is the
console's. Its history in OpenRAC: `d5fe4f64` (the first lock-strafe, PAL and
US), `e67d5a80` and `0ca783b9` (Hold L2-R2 and Lock with weapons), `17a034ea`
(Hold follows the camera after its swing), `81e687b2` (the walk or run played
backwards while strafing back).
