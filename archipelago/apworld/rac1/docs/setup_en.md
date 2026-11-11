# Ratchet & Clank setup guide

## What you need

- [OpenRAC](https://github.com/OpenRAC/OpenRAC) with your own PAL or NTSC-U disc.
- The **Archipelago** mod: in the OpenRAC launcher, **Mods**, **Add from a link**,
  `https://github.com/OpenRAC/openrac-mods`, then install **Archipelago**.
- `rac1.apworld` from the same release, for whoever makes the room (double-click it, or put it in
  Archipelago's `custom_worlds` folder; the Linux AppImage reads `~/.local/share/Archipelago/worlds`
  instead). Restart the launcher after, then **Generate Template Options** writes
  `Ratchet & Clank.yaml` in `Players/Templates`.

## Playing

1. Make your player YAML (Archipelago's Options Creator, game "Ratchet & Clank"). If that gives
   you none, this one has the defaults (the README says what each option does):

   ```yaml
   name: Player1
   game: Ratchet & Clank
   Ratchet & Clank:
     logic: standard
     shuffle_gadgets: true
     shuffle_weapons: true
     shuffle_vendor: true
     shuffle_gold_bolts: true
     shuffle_infobots: true
     shuffle_skill_points: false
     starting_weapons: vanilla
   ```

2. In the launcher, set the mod's **Server** (the room's address), **Slot name** and **Password**.
3. Start a new game. The line at the top of the screen says when the mod is connected.
