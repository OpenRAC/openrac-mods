# Going Commando's strafe on Ratchet & Clank 1: the design

What Ratchet & Clank: Going Commando (rac2) does when Ratchet strafes, read in
[rac2-decomp](https://github.com/OpenRAC/rac2-decomp) (NTSC-U v1.01, level 0's
copy of the shared hero code; the PAL copies under `src/pal/levels/shared` are
the same C with literals scaled for 50 Hz), and how this mod does each part on
Ratchet & Clank 1 (rac1, [rac1-decomp](https://github.com/OpenRAC/rac1-decomp),
PAL addresses). rac2's hero is the global `D_00189E20`. rac1's is
`g_hero` (PAL `0x0013F450`).

## What rac2 does

| | rac2, with evidence |
|---|---|
| **Entered** | L2 or R2 held. Strafing is a flag rather than a state: the short at hero `+0x1454`. The per-frame input step `func_L00_002B36E0` sets it while `pad.held & 3` and counts it down otherwise. No weapon is needed. L1 still looks (state 1), R1 crouches (state 4), and L2+R2 together lock a target (`func_L00_002CB038`). There is no lock-strafe option in rac2. |
| **Facing** | `func_L00_002B35A0` runs each walk tick while strafing. It saves the stick's yaw as the move yaw (`+0x1458`), puts the camera's yaw (`D_L00_001B8C80 +0x158`) in the target yaw and turns with `func_L00_002B3EF8(0, 0.037, 0.25, 0.2036)`: accel 0.037, damping 0.25, at most 0.2036 rad per 60 Hz frame (12.2 rad/s). Ratchet turns to where the camera looks. The camera does not turn to him. In the air the jump group turns him with 0.015 / 0.2 / 0.25. |
| **Moving** | Along the stick (`func_L00_002B4208(+0x1458)`), at the stick times the run's own speed (`D_001A7DC0` = 5.95 u/s, the speed table's run step), the same in every direction. The rac2 code sorts the stick into left, right, forward and back by 45-degree sectors (`func_L00_002B39F0`, byte `+0x1457`). Going left within 5 frames of going right multiplies the speed by -0.25 each frame. Going right after left multiplies it by 0, 0.25, 0.5 and 0.75 (timers `+0x146C` / `+0x146E`). Either way the old momentum is killed. |
| **Standing** | Idle with L2 or R2 held goes to the walk state when the facing is 10 degrees or more off the camera's yaw (`func_L00_002B3268(0.1745)`). He turns to the camera and stops. |
| **Legs** | No side-step animation. The legs play the ordinary walk (3) and run (4) (`func_L00_002CA0B0`). Backpedalling has its own animation (0x14, rate speed x 25). It applies within 45 degrees of straight back with the stick past 0.15 (flag `+0x1456`). |
| **Hips and upper body (the rig)** | `func_L00_002B7B80`: while strafing, with the stick past 0.15, not flipping (state 11) and not backpedalling, it writes the hero's pose table (`D_L00_0025BE10`, 0xB0-byte joint records). Record 0 (joint 9) is yawed by `wrap(move yaw - facing)` (spring 0.037 / 0.27). Record 1 (joint 10, its child) is yawed by minus that (spring 0.025 / 0.27). The springs run in `func_L00_002A5048` through `func_002AA898(.., direction 2)`: no limit, and they never wrap through half a turn. They turn about each joint's own z. The hips and legs turn toward where he goes, and the body above turns back to face the camera. The targets go back to 0 each frame, so the twist springs out when the strafe ends. |
| **Flips** | The jump decider `func_L00_002C9FC8` checks for X within the jump window while strafing, with the stick past 0.9 and not forward. When all hold it enters state 0xB with animation `0x1C + direction` (0x1C left, 0x1D right, 0x1F back). Forward or a weak stick gives the plain jump (7), and never the running jump (9). State 0xB (`func_L00_002C42E0`) has height 3.2 (3.25 held), takeoff at frame 5, apex at frame 18 and landing at frame 31. The facing is taken from 10 frames before, and the drift starts at 5 u/s along the strafe yaw. |
| **Camera** | `func_L00_004224D0`: while strafing, the strafe camera, which has no slide, a lateral spring of 0.04 / 0.2, and clears the recentre timers. These are the same requests rac1's state 0x81 makes. |
| **Weapons** | They fire along the facing, which is the camera's yaw. |

## On rac1

rac1's hero code is rac2's ancestor, so most of this maps one to one:

| rac2 | rac1 |
|---|---|
| Facing to the camera's yaw (`func_L00_002B3EF8`) | `Hero_TurnToHeading` (PAL `func_L00_002124E8`), the same (mode, accel, damping, limit). The mod's hook already puts the camera's yaw in the move yaw around it. With **Feel: Going Commando** it turns at rac2's rates, scaled the way rac1 scales its own: accel and damping x `g_frame_scale_sq`, the limit x 60 x `g_frame_time`. That is 0.037 / 0.25 / 12.2 rad/s on the ground and 0.015 / 0.2 / 15 rad/s in the air. The standing turn uses the ground rates. |
| No camera swing on L2 / R2 | The Hold mode no longer swings the camera behind him. He turns to the camera, as in rac2. **Feel: Classic** keeps the swing. |
| The run's speed along the stick | Already so: `Hero_SetPlanarVelocity` builds along the move yaw at `ground_speed` (`+0x194`), the run's speed from the stick (5.7 u/s in rac1). |
| Side reversal kills momentum | The rac2 timers, on rac1's `ground_speed` before the velocity is built. rac2's x -0.25 would make rac1's speed negative, so the mod takes it to 0. |
| Letting go of the stick | With the stick centred rac1's stick handler sets the move yaw to the facing and the target speed to 0, and the run slows him along that yaw, which after a strafe is forward. The mod keeps the stick's last move yaw while he slows. |
| Backpedal within 45 degrees of back | The backpedal starts 135 degrees from the facing and stops under 130. rac1 has no backpedal animation: its 0x14 is the run's stop. So the walk or run plays backwards, as before. |
| Joints 9 and 10 (rac2 Ratchet) | Joints 1 (pelvis: parent of the spine, both legs and the tail) and 2 (the first spine joint) of rac1's Ratchet (class 0, 111 joints). The bind pose has every joint's frame aligned with the model (z up), so turning about each joint's own z is a yaw, as in rac2. The pelvis goes to `wrap(move yaw - facing)` and the spine to minus that. rac2's springs are used, scaled like the turns. They are sent each tick through the SDK's `hero_set_joints` (composed after the animation). The port turns a joint about its z the other way from the hero's yaw, so the angles are sent negated. Ports before the OpenRAC change that opened the call to every mod return false, and Ratchet simply runs without the twist there. |
| Strafe flip, state 0xB | rac1's state 0xB is the same state (`HeroEnterMoveState`, PAL `func_L00_00222B80`). It has the same constants (3.2 / 3.25, takeoff 5, apex 18, land 31, facing from 10 ticks back) and the same animations `0x1C + stick_direction_vs_facing()`. rac1 enters it only from a crouch (R1 + stick + X, `try_start_hero_jump`). The mod enters it from the strafe. That happens in the stand and the run, on X pressed this tick, with the stick past 0.9 and `stick_direction_vs_facing` (PAL `func_L00_00222580`) not forward. It then starts the drift at 5 u/s, as rac2 does. It also clears the speed rac1's state 0xB carries along the facing (`air_target_speed`, `+0x454`), so the flip goes straight along the strafe. |
| Plain jump, never the running jump | Not done. rac1 picks the running jump inside the run state's code, after every hook point the mod has. Strafing forward and pressing X still gives rac1's running jump. A hook on `try_start_hero_jump` would do it, but needs a new SDK release first. |
| Strafe camera | Already the mod's: the same calls (`func_L00_002E9A18`, `func_L00_002E9A40` 0.04 / 0.2, `func_L00_002E9AF8`, pan timers 0). |

**Controls**, as rac2: L2 or R2 held strafes (Hold), or always with a weapon
that fires ahead in Lock. The left stick moves, the right stick turns the camera
and Ratchet with it, and X jumps. X with the stick pushed fully left, right or
back flips that way. L1 still looks and R1 still crouches.

**Settings**: `mode` (as before), `feel` (`commando`, the default, or
`classic`, the 1.0 behaviour: camera swing, rac1's own turn rates, backpedal
from 110 degrees), `twist` (the hip twist, default on), `flips` (default on),
`trace`.
