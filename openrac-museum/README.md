# OpenRAC Museum

A new planet for Ratchet & Clank on OpenRAC: a wooden museum house on a tropical island, with a
lagoon, mountains, two waterfalls, a volcano and small islets. Like the Insomniac Museum of the
later games, but about how OpenRAC is made. It is added as its own planet in the ship's menu;
Pokitaru and every other planet stay as they are.

## Install

1. Download `openrac-museum-pal.openrac-map` from this repository's
   [Releases](https://github.com/OpenRAC/openrac-mods/releases).
2. In the OpenRAC launcher, open **Mods** and add the file like any mod (pick it or drop it).
   OpenRAC Editor must be downloaded: it builds the map from your own disc.
3. Turn on **Mods** and play. Board the ship and pick **OpenRAC Museum**.

The file holds no game data. The plants, rocks, lamps, gadget targets and the ground and wood
pictures are named in it, and your disc gives them back when you add it. The map is made for the
Europe disc (PAL).

Adding a newer file updates the planet in place.

## What is in it

- **The island** in Pokitaru's style: rock walls with grass over every ledge, sea stacks, rock and
  grass islets out at sea, and turbine towers on the far islands.
- **The house** in the lagoon: an octagonal pavilion on stilts with two rings of 8 rooms round a
  tall atrium, a gallery reached by a double stair, and a gateway with a carved sign. Ground floor:
  Launcher, Renderer, Hostgen, The Port, Matching, Decompilation, Welcome, Mods and SDK. Upper
  floor: Multiplayer, Speedrun Corner, Enhanced, Android, Cutscenes, OpenRAC Editor, Devlog, Thank
  You. The rooms have signs, paintings and exhibit boards, ready for real exhibits.
- **Little scenes** on every patch of land: huts, canoes, campfires, totems, hammocks, fences,
  tide pools, stone lanterns.
- **Gadget routes** (Swingshot): the north jetty to the tower's top room, the coast to the
  message-in-a-bottle islet, and the lookout up the volcano to its shrine.
- **Handrails** on posts from the lookout to the tower and round the lagoon.
- **Easter eggs**: the Decomp Cave behind the second waterfall, a rubber duck in the tower, a
  sunken chest marked 0x1F, the volcano's "undefined behaviour" shrine, and bolt crates that spell
  OPENRAC on the east beach. Four gold bolts hide in the cave, the tower, on the islet and at the
  shrine.

## Notes

- A new-planet session starts without a save, so it has no gadgets: start it with your own save,
  or with `--unlock-all-gadgets`.
- The light is baked (sun, shadows, sky, every torch and lantern), so it costs nothing in the game.
- Collision covers where you can walk and where the gadget routes go. The high mountains away from
  the routes have none.

## Build the map file

For maintainers. Needs Python 3 with numpy and Pillow, an OpenRAC checkout and an OpenRAC Editor
checkout side by side, and a PAL disc image:

```
python3 tools/build_map.py DISC.iso OUT_FOLDER --editor path/to/OpenRAC-Editor
```

It reads levels 11 and 2 from the disc into a temporary folder, writes the museum there
(`tools/museum.py`), and exports it with OpenRAC Editor's `map export`. Upload the
`.openrac-map` it writes to a release. Never commit it with disc files.
