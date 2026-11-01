# End-to-end tests on the real game

Two or more hidden games with the test bot built in (`src/bot.hpp`), a local server, and a
`netimp.py` proxy between each game and the server. No window opens: SDL's offscreen driver with the
graphics card. Each run ends with a JSON report, `report.json`, in its folder.

A party of two discs: `--member-game`, `--member-data` and `--member-levels` put every member on
the other disc's program, data and levels; `crossplay.sh` runs every stage of it.

What it needs: a build of the port, the mod built with `-DMULTIPLAYER_BOT=ON` and packaged, a data
folder (`openrac-extractor IMAGE --game rac1 --proj-path DIR --extract --decompile`), extracted levels
(`python3 editor/extract.py port IMAGE build/levels`) and the server's jar with a JDK.

```sh
python3 e2e.py --scenario crates --crates 8 --deaths --join --link loss=5,latency=50,jitter=20 \
    --out runs/x --game <port>/openrac-rac1-pal --data <data> --levels <levels> \
    --mod <mod build>/package/multiplayer --server-jar <Server>/services/server/build/libs/openrac-server-1.0.0.jar \
    --java <jdk>/bin/java --game-env SDL_VIDEODRIVER=offscreen --game-arg=--skip-movies
```

| Scenario | What it plays |
|---|---|
| `cosave` | the party's saves: the game's own saves and Save the party land in each player's co-op slot, the host's reach the member byte for byte, every file of each player's own card stays the same by hash (`--seed-save FILE` puts a real save there first), the filter goes at the title, a member hosts from its slot |
| `keys` | a member's death: the key map, the drives and the crates afterwards |
| `crates` | crates broken from both sides, a member's death among them (`--deaths`), a late member (`--join`): every game's crates compared, and the mobys' places and hidden flags |
| `bolts` | bolts of crates broken on each side reach the player who stands there, none left over in either game |
| `hostdeath` | the host dies: what the party broke stays broken in its game, the member gets the world again, the bolts lying about agree |
| `wipe` | both die at once: the host's level comes back as its checkpoint has it, the member comes back beside the host |
| `revive` | the host's revive rule on: a member's dead hero stays down, the host standing over him brings him back where he lay |
| `crateback` | Rilgar's crates that come back: one broken by a member breaks in the host's game too, its bolts reach the member, none is deleted, each comes back by itself |
| `dig` | Novalis: two players at a pile of buried bolts: the pile goes from both games, one digs it |
| `join` | a party in a level (`--level N`; leave `--skip-movies` out, so the game's movies are there), then a late member: its timeline until it stands in control in the host's level; `--host-scene N`: the host plays scene N as it joins; `--place`: it ends beside the host, and stays there (`--level 5` for the Rilgar lift: the host on a moby is not steady, so its last safe spot is taken) |
| `gohost` | a member runs off and takes Go to host: beside the host, and the worlds agree |
| `restart` | the host goes to the title and starts again: a member in a level starts the new game |
| `shipwarn` | the host's ship: the member is warned, then pulled along |
| `tags` | a party of two in a level: each game draws a name tag for the other, on the picture (a figure out of its camera's view for 30 s fails it). How a tag looks takes a screenshot |
| `soak` | `--members N` standing in the level for `--seconds S`: what the network carried, by kind |
| `weapons` | each weapon given, drawn and fired by the host, then by a member (`--weapons Devastator,Pyrocitor` for some; `--fire-button` the pad's bit): the other game shows it (its class among the mobys, or its shots fired for what is drawn as particles), and nobody is hurt by the copy |
| `enemies` | an enemy count off 1 (`--difficulty '{"enemy_count": 2}'`), in level `--level N`: every game lists the host's enemies, none twice, none of a member's own, also after a member's and the host's death |
| `chaos` | `--difficulty '{"chaos": true, "chaos_events": true, "chaos_minutes": 1}'`, a blank seed: each level's values and the events are the host's, in levels `--level N`, `--level2 N`, for up to `--seconds S` |
| `save` | a host's save (`--save FILE`) started by the party: both in control in its level, with one world |
| `refuse` | a member whose settings, mods or data packs differ (`--member-difficulty`, `--member-mods`, `--member-packs`): told why, no Ready, no Start; the pages' lines measured in the game's font |
| `kick` | a host removes a member (one that differs, with `--member-difficulty`: Start is free again), who stays connected and can come back; a ban sends it away and again when it returns, under a new name too (each game's launcher key is the run's); lifted, it stays; then the same in a game: the member back at the title, connected, the host playing on |
| `progress2` | the shared progress past the items, in `--level N` (0: Kerwan, 3): a gold weapon and a skill point each way, plain flags each way, a member's scene flag held from the host, the host's held from a member until it leaves the level (to `--level2 N`), then a late member with all of it and no banner |
| `camera` | camera scripts, `--level 5` (Rilgar's lift) and `--level2 1` (Novalis's camera triggers): the host's lift shown to a member on it and not to one far off, a stick taking it back with no health lost, a member's trigger leaving the host's camera and hero alone (`notes.member_trigger` says whether a trigger fired: where a volume lies is the level's, the scenario stands the member at each trigger moby). Add `--member-game` and its fellows for a party of two discs |
| `midjoin` | a party under rules in `--level N`, then a late member: under the rules, or with `--late-difficulty ... --late-refused`, no game |

`--mods-dir DIR` is a folder of packaged mods by id; `--difficulty JSON` gives every game the difficulty
mod with those settings. The bolts scenario under `--difficulty '{"bolts": 3}'` reports each crate's
bolts in its notes.

`--link` takes `loss` and `reorder` and `duplicate` in %, `latency` and `jitter` in ms, each way
between a game and the server. `--base N` gives a run its own address block, so runs can go at once.
`--trace` turns on each game's verbose log. The report's `net` has each proxy's messages and bytes by
DATA kind, both ways: what the host sends less what the members receive is what the server dropped.

`sweep.sh` runs the crates scenario at 0, 1, 5 and 15 % loss and 30, 100 and 250 ms, and `report.py`
makes its tables.
