# Arsenal Mastery — OpenRAC port

This is a native OpenRAC port of **Arsenal Mastery** from
[RAC-RS](https://github.com/AsobiCodes/rac-rs/tree/main/bundled/mods/arsenal-mastery).

The goal is feature parity without importing RAC-RS's private `omni.*` host extensions into
OpenRAC. The port therefore lands in reviewable milestones and uses OpenRAC's stable mod hooks.

## Implemented in this port

- Damage-based weapon XP.
- The original V1–V5 thresholds and per-weapon XP scaling.
- The original V1–V5 weapon power curve.
- Projectile / attacker attribution for the RAC1 arsenal.
- The original planet-based enemy durability curve.
- Hard rebalance multiplier.
- New Game resets progression.
- New Game+ keeps progression for the running session and applies the original NG+ rebalance factor.
- PAL and NTSC-U support.
- Trace logging for attribution and balance testing.

The core uses `rac1.combat.get_hit` rather than RAC-RS's
`rac1.omni.damage.resolve`. Hits are classified from the game's hit record and moby classes, and
the damage in the hit record is scaled before the enemy consumes it.

## Not yet at RAC-RS parity

RAC-RS Arsenal Mastery uses host services that do not currently have direct equivalents in the
OpenRAC script API. They are intentionally **not faked** in this first port:

- per-save Arsenal Mastery data and migration;
- exact post-damage health removal and last-hit XP bonus;
- Gold / Mega / Giga / Omega / endless V99 progression;
- Gold-case integration;
- Nanotech levelling;
- ammo-capacity progression;
- perks and special weapon behaviours;
- pause-menu XP bars, icons and level-up HUD drawing.

XP currently lives for the lifetime of the game process. Starting a new game clears it; entering
New Game+ in the same process keeps it. Loading a different save does not yet restore separate
Arsenal Mastery progression.

Those items should be added as focused follow-up changes once the required save/UI/gameplay
interfaces are agreed with OpenRAC maintainers.

## Progression carried over from RAC-RS

V2, V3, V4 and V5 start at cumulative base XP 750, 2250, 5000 and 10000. The original weapon
scales are retained: early weapons use 1.0, mid-game weapons 0.75 and late weapons 0.5.

The V1–V5 power multipliers are 1.0, 1.2, 1.5, 1.9 and 2.5.

## Testing

Enable **Trace** in the launcher and play with several weapons on multiple planets. The log reports
the attributed weapon, XP gain, V-level, original damage, scaled damage and current rebalance
factor.

Important cases for review:

1. direct Wrench hits;
2. Blaster / Pyrocitor / Devastator projectile attribution;
3. repeated hit-record reads (must not award the same hit twice);
4. level transitions and increasing rebalance;
5. New Game reset;
6. New Game+ preserving session progression.

## Attribution

Original Arsenal Mastery design and RAC-RS implementation: **AsobiCodes**, MIT licensed.

This OpenRAC port keeps the original license and behavior constants where applicable. It contains
no game assets and requires the player's own copy of Ratchet & Clank.
