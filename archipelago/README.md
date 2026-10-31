# Archipelago

Plays Ratchet & Clank, on OpenRAC's native port (PAL and NTSC-U discs), in an
[Archipelago](https://archipelago.gg/) multiworld. The gadgets, weapons, vendor slots, gold bolts
and infobots are locations. Each one may hold an item of any player, in any game. What the others
find for you comes to you as you play. Beat Drek to finish your part.

With the mod off, the game is the PlayStation 2 game.

## Playing

1. Whoever makes the room installs `rac1.apworld` from this repository's release (double-click
   it, or put it in Archipelago's `custom_worlds` folder), and generates with your player YAML
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
| `logic` | `standard` | **standard**: a place counts only with the gadgets the game's route has there, and an expensive purchase holds no progression. **relaxed**: the same, and no progression in a gold bolt either. **expert**: only what a place cannot be done without counts |
| `shuffle_gadgets`, `shuffle_weapons`, `shuffle_vendor`, `shuffle_gold_bolts`, `shuffle_infobots` | on | Off: that pool keeps the game's own items in place |
| `keep_starting_gear` | on | Start with the Bomb Glove. Off: the start's Bomb Glove is a location too |

## How it works

- The locations are the [item randomizer](../item-randomizer/README.md)'s table, and so are the
  hooks. The mod uses that mod's sources: they share one table.
- At a location, the game gives nothing. The server is told, and sends back what is there when it
  is yours.
- The game's own save keeps which locations you have had, and every item is one you keep. So the
  mod keeps no file of its own. At each level start it tells the server every location you have
  had, and each tick it gives you what the server sent and you lack.
- Items arrive only while you are in control in a level, not in a cutscene or a menu.
- The goal is sent when Drek's ending scene starts on Veldin.
- The connection runs on a thread of its own and comes back by itself when it drops. `wss://`
  uses mbedTLS and the system's root certificates.

## Building

From the repository's root, with the SDK as in the [main README](../README.md):

```
cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build -R archipelago
```

`build/apworld/rac1.apworld` is the APWorld. Its `table.json` is printed by
`tools/dump_table.c` at build time. To test it, put it in an Archipelago checkout's `worlds/` and
run `python -m pytest worlds/rac1`.
