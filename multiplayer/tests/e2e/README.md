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
| `hostdeath` | the host dies: what the party broke stays broken in its game, the member gets the world again, nothing of its old spawned mobys is left |
| `join` | a party in a level (`--level N`; leave `--skip-movies` out, so the game's movies are there), then a late member: its timeline until it stands in control in the host's level; `--host-scene N`: the host plays scene N as it joins |
| `tags` | a party of two in a level: each game draws a name tag for the other, on the picture (a figure out of its camera's view for 30 s fails it). How a tag looks takes a screenshot |
| `soak` | `--members N` standing in the level for `--seconds S`: what the network carried, by kind |
| `weapons` | each weapon given, drawn and fired by the host, then by a member (`--weapons Devastator,Pyrocitor` for some; `--fire-button` the pad's bit): the other game shows it (its class among the mobys, or its shots fired for what is drawn as particles), and nobody is hurt by the copy |
| `enemies` | an enemy count off 1 (`--difficulty '{"enemy_count": 2}'`), in level `--level N`: every game lists the host's enemies, none twice, none of a member's own, also after a member's and the host's death |
| `chaos` | `--difficulty '{"chaos": true, "chaos_events": true, "chaos_minutes": 1}'`, a blank seed: each level's values and the events are the host's, in levels `--level N`, `--level2 N`, for up to `--seconds S` |
| `save` | a host's save (`--save FILE`) started by the party: both in control in its level, with one world |
| `refuse` | a member whose settings or mods differ (`--member-difficulty`, `--member-mods`): told why, no Ready, no Start; the pages' lines measured in the game's font |
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
