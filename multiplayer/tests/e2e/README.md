# End-to-end tests on the real game

Two or more hidden games with the test bot built in (`src/bot.hpp`), a local server, and a
`netimp.py` proxy between each game and the server. No window opens: SDL's offscreen driver with the
graphics card. Each run ends with a JSON report, `report.json`, in its folder.

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
| `keys` | a member's death: the key map, the drives and the crates afterwards |
| `crates` | crates broken from both sides, a member's death among them (`--deaths`), a late member (`--join`): every game's crates compared, and the mobys' places and hidden flags |
| `bolts` | bolts of crates broken on each side reach the player who stands there, none left over in either game |
| `hostdeath` | the host dies: what the party broke stays broken in its game, the member gets the world again, nothing of its old spawned mobys is left |
| `join` | a party in a level (`--level N`; leave `--skip-movies` out, so the game's movies are there), then a late member: its timeline until it stands in control in the host's level; `--host-scene N`: the host plays scene N as it joins |
| `soak` | `--members N` standing in the level for `--seconds S`: what the network carried, by kind |

`--link` takes `loss` and `reorder` and `duplicate` in %, `latency` and `jitter` in ms, each way
between a game and the server. `--base N` gives a run its own address block, so runs can go at once.
`--trace` turns on each game's verbose log. The report's `net` has each proxy's messages and bytes by
DATA kind, both ways: what the host sends less what the members receive is what the server dropped.

`sweep.sh` runs the crates scenario at 0, 1, 5 and 15 % loss and 30, 100 and 250 ms, and `report.py`
makes its tables.
