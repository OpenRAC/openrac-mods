# Archipelago

Plays Ratchet & Clank, on OpenRAC's native port (PAL and NTSC-U discs), in an
[Archipelago](https://archipelago.gg/) multiworld. The gadgets, weapons, vendor slots, gold bolts
and infobots are locations. Each one may hold an item of any player, in any game. What the others
find for you comes to you as you play. Beat Drek to finish your part.

With the mod off, the game is the PlayStation 2 game.

Archipelago can't be played in a multiplayer party: its checks and items belong to one slot, and a
party shares its items. With it on, the Multiplayer pages say so: turn it off to host or join.

## Playing

1. Whoever makes the room installs `rac1.apworld` from this repository's release (double-click
   it, or put it in Archipelago's `custom_worlds` folder; the Linux AppImage reads
   `~/.local/share/Archipelago/worlds`), and generates with your player YAML
   (game `Ratchet & Clank`).
2. Install the mod from the launcher's **Mods** tab (**Add from a link**:
   `https://github.com/OpenRAC/openrac-mods`), switch it on and fill in its settings.
3. Start a **new game**. A line at the top of the screen says when the mod is connected, and what
   is found and received.

| Setting | Default | |
|---|---|---|
| Server | `archipelago.gg:38281` | The room's address. `host:port` tries `wss://` first, then `ws://`. Write `ws://` or `wss://` to use only that one |
| Slot name | your launcher name | The `name` in your player YAML |
| Password | blank | The room's password |

## The YAML's options

| Option | Default | |
|---|---|---|
| `logic` | `standard` | **standard**: an expensive purchase holds no progression, and a place counts only with the weapons the game suggests there. **relaxed**: the same, and no progression in a purchase you save up for either. **expert**: progression may be in any purchase, and any weapon will do |
| `shuffle_gadgets`, `shuffle_weapons`, `shuffle_vendor`, `shuffle_gold_bolts`, `shuffle_infobots` | on | Off: that pool keeps the game's own items in place |
| `shuffle_skill_points` | off | On: the 30 skill points are locations too, with 30 more gold bolts among the items. With gold cases shuffled, their gold bolts can finance purchases; otherwise they never hold progression. Named by planet and number, in the game's flag order |
| `shuffle_gold_weapons` | off | Add the ten gold-weapon case purchases as locations, with the ten gold weapons among the items. Cases keep their retail bolt prices and cost four gold bolts each. Receiving a gold weapon gives its base weapon and gold upgrade; it never spends currency or buys the original case |
| `start_in_new_game_plus` | off | Enable the game's New Game+ (challenge mode) after connecting and validating the save. Uses fresh Archipelago inventory and the selected starting weapon; does not import completed-save items or checks. False leaves an already earned New Game+ save as it is |
| `starting_weapons` | `vanilla` | **vanilla**: start with Bomb Glove. **shuffled**: shuffle the starting location, with no guaranteed weapon. **non_broken**: start with one random normal or gold weapon except the R.Y.N.O. **unrestricted**: start with any one normal or gold weapon. This option controls the starting location independently of `shuffle_weapons`; random starting weapons exchange slots with Bomb Glove before shuffling, including when their pools are off |

For a weighted choice, put this under `Ratchet & Clank:` in your player YAML:

```yaml
  starting_weapons:
    vanilla: 50
    shuffled: 0
    non_broken: 0
    unrestricted: 0
```

`starting_weapons` replaces `keep_starting_gear`: use `vanilla` for the former on behavior,
or `shuffled` for the former off behavior with weapons shuffled.

These are relative weights: this example always selects vanilla. Set `non_broken: 50` and
`vanilla: 0` to always start with a random weapon other than the R.Y.N.O. The weapon is chosen
when the seed is generated. Install an APWorld containing this option before generating.

To start in New Game+ with a random non-R.Y.N.O. weapon:

```yaml
  start_in_new_game_plus: true
  starting_weapons: non_broken
```

This needs both an APWorld and a native mod containing the New Game+ option. Generate a new seed,
connect to its server, and start a new game. The mod enables challenge mode once the server has
sent the option and accepted the save; it does not run the completed-save restart or grant its inventory.

To shuffle the gold-weapon cases and allow a random normal or gold starting weapon:

```yaml
  shuffle_gold_weapons: true
  start_in_new_game_plus: true
  starting_weapons: non_broken
```

Each gold weapon is one location shared across its appearances in both shops. Purchase checks
are saved separately from owned upgrades, including across a completed-save New Game+ restart.
Gold bolts become progression currency for this option; the game still spends only four per
purchase, and the balance is capped at the retail total of 40. The logic conservatively expects
all 40 before relying on these purchases so their order cannot exhaust currency. The player's
own gold-bolt currency is never placed in these cases. Other rewards follow the normal
expensive-purchase progression restrictions. Without a New Game+ start, the logic treats the
full case selection as postgame; with it, full Novalis shop access is enabled without granting
any upgrades or marking any cases bought. Cases retain their retail models and dialogue; a nearby-case preview names the actual scouted
reward, recipient, bolt price and four-gold-bolt cost before purchase. Archipelago delivers the
shuffled reward when the purchase is checked.

Random starting-weapon modes include all ten gold variants even with gold-case shuffle off.
With gold cases off, a gold starter replaces its base weapon's item and moves Bomb Glove to that
base weapon's original slot. With gold cases on, it exchanges rewards with its own case.

Randomized gold-case purchases need an OpenRAC build containing
[OpenRAC/OpenRAC#444](https://github.com/OpenRAC/OpenRAC/pull/444), which adds the independent
unspent-gold-bolt hook. Other mod options still load on older builds; gold-case purchases are
blocked with an update notice rather than charging the wrong currency.

## How it works

- The locations are the [item randomizer](../item-randomizer/README.md)'s table, and so are the
  hooks. The mod uses that mod's sources: they share one table.
- At a location, the game gives nothing. The server is told, and sends back what is there when it
  is yours.
- The game's own save keeps which locations you have had, and every item is one you keep. So the
  mod keeps no file of its own. At each level start it tells the server every location you have
  had, and each tick it gives you what the server sent and you lack.
- A save from another seed is caught, and its checks are not sent (a line says so). The mod keeps
  which room's game each memory card slot holds, in its own folder (`save-slots.tsv`; needs an
  OpenRAC with mod interface 1.11). With an older OpenRAC, or a slot it has not seen yet, it
  guesses from the checks: a save with checks the server never saw from your slot that lacks some
  it saw.
- Items arrive only while you are in control in a level, not in a cutscene or a menu.
- Until Bomb Glove is owned, the game's starting equipment and Circle/Triangle shortcuts cannot
  bring it out. Stale Bomb Glove selections switch back to the wrench; other weapons and a Bomb
  Glove awarded by the server keep their normal behavior.
- The goal is sent when Drek's ending scene starts on Veldin.
- The connection runs on a thread of its own and comes back by itself when it drops. `wss://`
  uses mbedTLS and the system's root certificates. The server's messages may come compressed
  (permessage-deflate, inflated with miniz).

## Building

From the repository's root, with the SDK as in the [main README](../README.md):

```
cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build -R archipelago
```

`build/apworld/rac1.apworld` is the APWorld: the `apworld/rac1` folder, zipped.

`apworld/rac1/data.py` holds the locations, items and rules. It is written by `tools/dump_table.c`
from the item randomizer's table, and committed so the logic can be read. Do not edit it. After a
change to `item-randomizer/src/logic.c`, write it again; the `archipelago_data` test fails until
you do:

```
cmake --build build --target archipelago_data
```

To test the world in an Archipelago checkout: put `rac1.apworld` in its `worlds/` and run
`AP_TEST_WORLDS=rac1 python -m pytest test/general` (Archipelago's own checks, on the zip), then
copy `apworld/rac1` to `worlds/rac1` instead and run `python -m pytest worlds/rac1`.

Starting a new game over an occupied save slot replaces that slot's seed association only after the save succeeds. Cancelling or failing the overwrite keeps the old association. Loading a save from another seed still blocks its checks; saving that loaded game does not change its seed association.
