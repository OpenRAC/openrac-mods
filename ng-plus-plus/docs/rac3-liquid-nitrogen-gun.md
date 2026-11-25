# Up Your Arsenal's Lava Gun and its V5, the Liquid Nitrogen Gun

Notes for the Ice Gun in NG++ (written 2026-11-24 from the rac3 decomp, `OpenRAC/games/rac3/decomp`:
`src/levels/shared/L01_*.c`, `config/names.tsv`, `asm/levels/01_veldin/nonmatchings/text/`).
Addresses are PAL level-1 function names (`func_L01_...`) unless an NTSC-U address is given.
Anything marked (asm) was read from the assembly, not from matched C; (names) from `names.tsv` only.

## The gun is a stream, at every version

The Lava Gun is not a shooter of rounds. `lava_gun_update` (L01_003041A0, NTSC-U 0x303F88) runs
every frame while the gun is out:

- While the fire button is held the stream's strength springs up to 10 (`game_spring` with 0.009,
  0.0024 and a cap of 12), and back down to 0 on release. A round of ammo is spent every 10 frames.
- Each frame at strength > 0 it adds one **stream point** at the nozzle (`lava_stream_add`,
  L01_003057A0): position, a velocity along the gun's own -y (its barrel, through the moby's matrix
  at +0xC0) scaled by `strength * 0.05`, and a life of 0x21 frames (names.tsv says 40). The points
  live in a ring of **35** (`v[15]` the write index, `v[16]` set once it wrapped).
- `lava_stream_update` (L01_00304C48, asm) moves every point (a gravity of about 21.6 u/s^2), ray
  casts each step (`func_003FA5F0`), and on the first thing it hits stops the point:
  - a **wall or floor**: V1 to V4 spawn a **lava pool** (`lava_pool_spawn`, L01_00306628: the first
    free of 10 burning pools, two flames each, 60 frames of life); V5 instead calls
    `particle_spawn_cold_splash` (L01_004739D8, the "cold splash", a burst of blue sprites) and keeps
    only the pool's glows, in blue.
  - an **enemy**: it is hurt by the weapon level's damage (`D_L01_002C5600[level].f6C`, the level
    row's damage) and so is every enemy within 1.5 units of the point (+0x70 row radius); a 0.5 u
    damage ball also sits at the nozzle each frame (`func_003D4D00`, collision sphere test).
- The hit sound is entry 0x5C9 at V1 to V4 and **0xC59** at V5 (`func_004AAC98`).
- Nothing is a projectile moby: the stream is data in the gun's variables, drawn by the gun's own
  draw callback. That is the big difference from Going Commando's Lava Gun, whose shot is a meteor
  moby (class 2862) that flies an arc and blasts.

## What V5 changes (row `b1 >= 4`, the weapon's version byte)

Everything below switches on `D_L01_002C5600[hero weapon].b1 >= 4`:

| Part | V1 to V4 (lava) | V5 (Liquid Nitrogen) |
|---|---|---|
| Ribbon colours (`lava_stream_draw`, L01_00305AB8, ribbon `lava_stream_ribbon` L01_00305F00) | core/band `0x7F10307F` (R 7F, G 30, B 10: orange), glows `0x3F08183F` | `0x7F7F3010` (R 10, G 30, B 7F: **blue**), glows `0x3F3F1808` |
| Ribbon width | 0.05 | **0.1** (twice as wide) |
| Smoke | alternates two orange render modes | one blue mode `0x48` (`particle_spawn_darkening_smoke`, class 0x6D, sinks then rises, size x 2.5) |
| Stream blends | fixed-alpha added: core 0x64, band and glow 0x68 | same |
| Ground | burning lava pool: two flames of texture 23 (one blended, one added) and two glows of texture 11 | **glows alone, in blue** (`lava_pools_draw`, L01_00306A98) |
| Wall hit | lava pool | cold splash particles |
| Enemy hit | damage | damage **and a freeze** (below) |
| Sound | 0x5C9 | 0xC59 |

The stream is drawn as one ribbon through the up to 35 points from the nozzle (a core, a band and
a glow, each a strip, added to the screen), two glows at the nozzle and, from the points, the 0x6D
smoke every few points. That is what the player sees: a thick, vivid blue ribbon of liquid with
white-blue where layers overlap, blue smoke drifting off it, cold light pools on the ground.

## The freeze (enemy hit at V5)

On a hit (asm, `lava_stream_update`), after the damage:

1. `death_list_add(enemy, 5, damage_record_state_set)` (L01_00315900, NTSC-U 0x3154D8) puts the
   enemy in the shared list at 0x271BC0 with **kind 5**, a phase `t = 1.0` and a callback.
2. `ice_block_spawn(enemy)` (L01_003E31A8, NTSC-U 0x3E1418) makes a **block of class 0x1E52** at the
   enemy's middle (`func_L01_004CB128`, the target mark's point), with the enemy's matrix, 1.5 tall
   (its z axis x 1.5), scaled by the class's scale x the enemy's damage record size x 0.75, drawn
   by its matrix (flag 0x100), opacity 0.
3. `death_list_update` (L01_00314158, NTSC-U 0x313E28), each frame for kind 5: `t` falls by
   `dt * 0.02` (about a second: 50 steps), and **while `t >= 0.9`** (the first tenth) it calls
   `freeze_puffs_spawn` (L01_003155A8, NTSC-U 0x315180): five times a frame, at each of the
   enemy's collision spheres (`func_003BD668` fills the scratchpad with their position and
   radius; a sphere with `b.w == 0` takes its centre plus a random offset of its radius, else a
   random point between the two ends of its segment, jittered by 0.4 of its radius), a **cold
   puff** of size 0.7 to 1.0 x the sphere's radius (`func_L01_00472700`, a sprite of one of two
   textures, alpha 150 on NTSC-U / 125 on PAL).
4. When `t` reaches 0 the list calls the callback `damage_record_state_set` (L01_003DE198): the
   enemy's damage record state goes to 2 (it is free again).

How the enemy is stopped while the block stands is not in this code: the list only counts the
second and hands the enemy back (state 2 in its damage record). The visible part is the block and
the puffs.

## The ice block (class 0x1E52)

`nitrogen_ice_update` (L01_003E30B8, NTSC-U 0x3E1328) is its update:

- State 0: a counter counts up each frame and sets the block's **opacity** to `counter * 2`
  (0x23 byte; 0x80 is opaque), until the counter reaches **30**: so it fades in over 30 frames to
  about **47%** (60 of 128), then state 1.
- State 1: waits for the timer in its variables (`func_0038CAD0`, a countdown the spawn set when
  the freeze began). When it runs out it plays its sound-bank entry 0 (`func_003A52A0(0,0,o)`) and
  calls `func_L01_00451FE0(o, 63, tex, 255, 300, 0, 0, 35, 0, 3, 1.5, 3.0, 1.5, 3.0, -1, -1, 1, 1)`:
  the **shatter**, an explosion-of-debris effect on texture 63 with **35 pieces**, each 1.5 to 3.0
  across, 300 frames of reach, then destroys the block (`func_003C2880`).
- A block killed with its enemy (the enemy dies frozen) goes the same way when the enemy goes.

## Timings and numbers (summary)

| What | Value |
|---|---|
| Stream points | ring of 35, one added per frame while firing, life 0x21 (33) frames |
| Fire rate (ammo) | one round every 10 frames |
| Ribbon width | 0.05 (lava), 0.1 (nitrogen) |
| Ribbon colours (nitrogen) | core and band (R 0x10, G 0x30, B 0x7F), glows (0x08, 0x18, 0x3F), added |
| Splash / hit radius | 1.5 u around a hit point; 0.5 u damage ball at the nozzle |
| Freeze (death list kind 5) | 1.0 -> 0 in about 1 s; puffs for the first 0.1 |
| Cold puffs | 5 a frame, size 0.7 to 1.0 of the sphere's radius |
| Ice block | 1.5 tall, x0.75 of the enemy's record size, fades in over 30 frames to 47% |
| Shatter | 35 pieces of 1.5 to 3.0, texture 63, with a sound |
| Pool | 10 pools, 60 frames, glows only (blue) at V5 |

Not found: the V5 damage numbers themselves (they are the level row's, in the weapon table), and
how the enemy is actually stopped while its block stands.

## What NG++ copies, and what it cannot

NG++'s Ice Gun (Going Commando's Lava Gun from Alpha on) keeps the game's meteor as the thing that
hits (its flight, damage and blast are the game's), and copies what the player sees and the freeze:

| Up Your Arsenal | NG++ |
|---|---|
| Blue ribbon of added light, core + band + glow | The meteor is not drawn; each flies as a ribbon through its last points (an added streak with a built-in core/band/glow profile, tinted by the tier's ice colour), thinner and dimmer as the points age, with a glow at its head |
| Cold light pools on the ground | A flat blue glow on the ground where a meteor lands, fading in about a second |
| Cold splash particles | A burst of added blue puffs thrown up and out |
| Cold puffs on a freshly frozen enemy | Puffs rising from it for the first eighth of a second |
| Ice block, fading to about half opacity over 30 frames | A six-sided crystal block over the enemy, blended, fading in over 30 frames to 52% |
| Shatter into 35 pieces when the freeze ends or the enemy dies in it | 16 shards (26 when it dies in it) that fly out, tumble, fall and fade, and a flash |
| One-second freeze | The tier's freeze (perk_rules.cpp `ice_effect`), with moby_status |

Differences that stay: Going Commando's shot is a meteor moby (arc, impact blast), not a stream of
points, so the ribbon is a series of streaks, one per meteor; the game's own trail particles of the meteor stay (painted by the tier's colour with
interface 1.39). RAC3's sounds (0xC59, the block's shatter) are not copied: no RAC3 data is read.
Everything drawn is made by `tools/make_ice_meshes.py` from numbers.
