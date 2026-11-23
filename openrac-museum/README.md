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
  tall atrium, a gallery reached by a double stair, and a gateway with a carved sign.
- **One exhibit in each room**, about how OpenRAC is made: the launcher, the renderer, hostgen and
  matching (real code on the screens), the I-5 terminal, the 20-button codes on the floor, a map of
  the museum, mods, co-op, the speedrun record, the effects case, a handheld, a
  cutscene file, the test room, the devkit desk and a wall of everyone who made OpenRAC.
- **Cut from the game**, round the fountain: the Revolverator (its model from your disc), the E3
  demo's old names, the Mackerel 1000, and the developers' notes to themselves over the doors.
- **A gold tile before each exhibit**: stand on it and the OpenRAC Museum mod shows what you see
  and the story behind it.
- **Gadget routes** (Swingshot): the north jetty to the tower's top room, the coast to the
  message-in-a-bottle islet, and the lookout up the volcano to its shrine.
- **Easter eggs**: the Decomp Cave behind the second waterfall, a rubber duck in the tower, a
  sunken chest marked 0x1F, the volcano's "undefined behaviour" shrine, bolt crates that spell
  OPENRAC on the east beach, and a door that opens only at 3 AM. Four gold bolts hide in the cave, the
  tower, on the islet and at the shrine.
- **Handrails** on posts from the lookout to the tower and round the lagoon.

## The mod

The OpenRAC Museum mod (`mod/`, on the mod hub as `openrac-museum`) makes the exhibits work: the
tiles' words, the I-5 menu (Select leaves it), the effects case, a notice when
a 20-button code works, and the 3 AM door. Turn it on with the map. Its data is written from the
exhibits by `python3 tools/mod_data.py`.

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
