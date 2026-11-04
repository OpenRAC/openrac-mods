# Modern Controls (experimental)

A separate RAC1 PAL / NTSC-U mod for a quicker, more adjustable camera and movement response.

Configure it in **Launcher → Mods → Modern Controls → Settings**, then restart the game.

| Setting | Default | Range |
| --- | --- | --- |
| Mouse horizontal / vertical sensitivity | 150% each | 10–1000% |
| Right stick horizontal speed | 240 degrees/s | 30–720 |
| Right stick vertical speed | 150 degrees/s | 30–360 |
| Right stick radial deadzone | 10% | 0–40% |
| Invert vertical look | Off | On / Off |
| Movement acceleration and braking | 175% | 100–300% |

Mouse, Steam Input gyro-to-mouse and trackpad-to-mouse turn the camera directly, without stick emulation, acceleration or smoothing. Stick rotation is linear outside a circular deadzone. Horizontal and vertical settings are independent. Mouse counts depend on the operating system / Steam Input sensitivity too. The mod uses its own vertical invert option for both input types.

The follow camera keeps its pitch when the stick or mouse stops, and its original camera-volume locks and pitch limits still apply. First-person aiming is also direct. Other camera modes, menus, cutscenes and the Triangle weapon ring keep the game's controls. The left stick is unchanged. Acceleration and braking approach the game's existing target speed faster; maximum run speed, jumping and animation speed are unchanged. Set movement response to 100% to retain the original acceleration/braking. Disable Modern Controls to return entirely to the game's controls.

## Steam Deck Pack

Keep the Steam Deck Pack enabled for its icons, but switch **Gyro / trackpad mouse camera** off in that pack. Modern Controls becomes the one mouse-camera provider. Its own mouse setting can be disabled for a stick-only setup. Do not combine it with Keyboard & Mouse; both control the camera.

All settings are supplied by the launcher. This mod deliberately does not consume another native pause Options slot: the port currently permits only two mod entries there, which may already be occupied by other mods.

## Development and verification

Uses the SDK's stable `rac1.camera.follow.apply_overrides` and `rac1.player.approach_speed` hooks. Camera math and disc layouts are reused from `keyboard-mouse/src/kbm.hpp` and `discs.h`, under the repository's GPL-3.0-or-later license. Build through the root CMake project with SDK 1.9 or newer.

Tests cover radial deadzone boundaries, diagonal magnitude, mouse scaling, 60 Hz game-tick normalization (both OpenRAC disc builds), and camera vector length. A native-library host test exercises both discs on synthetic game RAM with interface 1.10: camera and first-person movement, duplicate polls, pitch bounds, movement response, menus, focus loss, weapon selection and special-camera passthrough. No game assets are included.

This is a first implementation. Automated tests do not establish how it feels on hardware. Actual Mac / Steam Deck gameplay, unusual camera volumes, gravity boots and multiplayer need playtesting before proposing an upstream release. Other mods changing the same camera state may interact even when their hooks chain correctly.
