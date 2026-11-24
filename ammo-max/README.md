# Ammo at the most + Buy All

Two complementary ways to buy ammunition at the Gadgetron or Megacorp vendor in
[OpenRAC](https://github.com/OpenRAC/OpenRAC):

1. **Individual purchase (RAC1):** opening the usual ammo purchase dialog starts
   the count at the most rounds you have room for and can afford, rather than 1.
   **Down** still reduces the count, and **Up** increases it again as usual.
2. **Buy All (RAC1 and RAC2):** while browsing the vendor's main weapon list,
   press the control mapped to **Square** (PlayStation) or **X** (Xbox) to refill
   ammunition across your owned weapons in one purchase.

The existing Ammo at the most behaviour and its setting are preserved.

## Buy All behaviour

- Uses the currently offered vendor prices and remaining ammo capacity.
- Only buys ammo for owned weapons; skips unowned weapons and unavailable stock.
- If bolts are limited, buys as much as possible in vendor-list order.
- Excludes weapon purchases and confirmation dialogs. Normal individual purchases
  keep working alongside the Buy All action.
- Plays the vendor's normal purchase or rejection sound.
- Displays a contextual HUD prompt with the cost, an ammo-full / insufficient-bolts
  status, partial refill notice, or purchase confirmation.
- Adds a contextual Square button to Android vendor touch controls.

**Supported discs:** RAC1 PAL and NTSC-U; RAC2 NTSC-U, PAL and NTSC-U v2.00.
**Requires:** OpenRAC mod SDK/API 1.20 or later.

### RAC1 HUD

The Buy All prompt appears between the Buy and Exit monitors. Keyboard activity
shows `F` (the default Square binding); gamepad activity switches back to `X` or
`Square`, according to the mod setting. The mod cannot automatically identify
the controller brand. Custom keyboard layouts may use a different key.

The RAC1 SDK's independent HUD text path cannot access native controller glyphs
or draw a filled rectangle while the vendor menu is active, so it uses readable
text with a dark outline.

### RAC2 HUD

The Buy All prompt is drawn near the vendor controls with native button glyphs.
The **RAC2 vendor HUD position** setting offers three positions.

## Settings

| Setting | Default | Purpose |
|---|---|---|
| Detailed log | Off | Logs each RAC1 individual-dialog count adjustment |
| Show vendor ammo HUD | On | Show Buy All prompts and feedback |
| RAC1 controller button label | Xbox X | Switch between Xbox X and PlayStation Square text |
| RAC2 vendor HUD position | Below vendor controls | Change the Buy All prompt location |

## Compatibility and migration

The mod keeps the ID `ammo-max`, so existing Ammo at the most installations
can update without losing their identity. If you have the separate
`vendor-buy-all` or older `rac2-buy-all-ammo` mod installed, **disable it**
when using this combined mod to avoid duplicate Square actions.

The RAC1 individual-dialog feature uses the vendor's own current ammo limits
and price (including the alternate PDA price), retaining its compatibility with
the difficulty mod's pricing changes. Buy All also uses the vendor's current
price and capacity records.

## Technical notes

The RAC1 dialog watcher is the original `ammo-max` implementation and still
only sets the game's dialog count once when it opens. The Buy All purchase
planner is separate and executes transactions only from the main vendor list.
Each registers its own frame callback using the SDK's supported callback list.
The original count tests, library load test, purchase planning tests and RAC1
vendor/runtime tests are included.

## Building and testing

From the [repository root](../README.md#building):

```sh
python3 tools/fetch_sdk.py
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Credits and licences

- **Keros / OpenRAC contributors:** original Ammo at the most feature,
  disc mappings and tests (GPL-3.0-or-later).
- **glawks:** Vendor Buy All RAC1/RAC2 purchase logic, HUD, input/touch
  integration and tests (MIT); incorporated with permission of its MIT licence.

The combined module is distributed under **GPL-3.0-or-later**. The contributed
MIT-licensed components retain their SPDX headers and full notice in
[LICENSE-VENDOR-BUY-ALL](LICENSE-VENDOR-BUY-ALL).
