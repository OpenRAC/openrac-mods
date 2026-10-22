# Live splits

Splits a [LiveSplit](https://livesplit.github.io/) timer as the player goes from planet to planet
in Ratchet & Clank, for OpenRAC's native port (PAL and NTSC-U discs): the mod talks to LiveSplit's
own server and splits at every planet, so a speedrunner does not have to touch the timer.

With the mod off, the game is the PlayStation 2 game.

## Playing

1. Install the mod from the launcher's **Mods** tab (**Add from a link**:
   `https://github.com/OpenRAC/rac1-mods`), switch it on, choose its settings and start the game.
2. In LiveSplit: **Settings**, the **LiveSplit Server** group, **Startup Behavior**: **Start TCP
   Server** (or **Start Websocket Server**), and the **Server Port** (27500 by default, which is
   what the mod looks for). The mod connects to it.
3. Load your splits file: one segment a planet, in the order below.
4. Start a new game. The mod resets LiveSplit's timer and starts it when the first planet is
   loaded, and splits at every planet after that. The run ends when the game goes back to the
   galaxy map after Veldin, the last planet, where the last boss is.

| Setting | Default | |
|---|---|---|
| Split | When a planet is reached | **When a planet is reached**: the split is taken when the planet's level is loaded, so the flight to it and its loading are in the segment before. **When a planet is left**: the split is taken when the game goes back to the galaxy map, so they are in the segment after |
| Start | At the first planet | **At the first planet**: the mod resets LiveSplit's timer and starts it when the run's first planet is loaded. **By hand**: the mod never resets or starts the timer; start it yourself, before the first split |
| Game time | on | Sends the game's own time (its ticks) to LiveSplit, for the **Game Time** timing method. With the timer started by hand, the time is counted from the first planet, not from the moment you start the timer |
| Server port | 27500 | The port LiveSplit's server listens on (its Settings) |
| Protocol | Ask the server | **Ask the server**: the mod tries the TCP server and, when that is not there, the WebSocket one. **TCP** and **WebSocket** pick one |
| Trace | off | For bug reports: a line in the game's log for every planet reached and left |

Start LiveSplit's server **before** the game: the mod keeps the commands it is given while the
server is not there and sends them when it comes, but a timer that starts late is a timer that
starts late.

**Starting over.** When the player enters a planet the run has already been to, the run begins
again: the mod resets the timer and starts it, and the splits go on from there. That is what
happens when the player goes back to the title and begins a new game, or loads a save: the way a
run is reset. It also happens when the player goes back to a planet he has already been to in the
same run, which is a mistake rather than a reset: the timer starts again from there.

**Dying does not split.** The game loads a level's gameplay again when Ratchet dies, which is not
a new planet: the mod keeps the planet it last saw, and the timer runs on.

## The splits

The game plays nineteen levels, one a planet, and every route goes through all of them: the
planets' coordinates are what the run is after. Your splits file should have one segment a
planet, named as the game names them:

| Level | Planet | | Level | Planet |
|---|---|---|---|---|
| 0 | Veldin | | 10 | Orxon |
| 1 | Novalis | | 11 | Pokitaru |
| 2 | Aridia | | 12 | Hoven |
| 3 | Kerwan | | 13 | Gemlik Base |
| 4 | Eudora | | 14 | Oltanis |
| 5 | Rilgar | | 15 | Quartu |
| 6 | Blarg Station | | 16 | Kalebo III |
| 7 | Umbris | | 17 | Drek's Fleet |
| 8 | Batalia | | 18 | Veldin (final) |
| 9 | Gaspar | | | |

The names are the game's own; the last planet is Veldin again, where Drek's robot is.

The mod splits at every planet, whatever the category: **Any %**, **100 %** and **All Gold Bolts**
all go through the nineteen planets, and the segments are the same.

## How it works

For modders and the curious. The parts:

| File | |
|---|---|
| [src/live_splits.cpp](src/live_splits.cpp) | the mod: the settings, the hook, the tick |
| [src/splits.cpp](src/splits.cpp) | the state machine: the game's levels in, LiveSplit's commands out |
| [src/client.cpp](src/client.cpp) | the connection to LiveSplit's server, on a thread of its own |
| [src/levels.hpp](src/levels.hpp) | the planets' names |

**What the game is read through.** Two things, and nothing else:

- **`Level_LoadGameplay`** (the hook point `rac1.level.load_gameplay`): the level's gameplay file
  is loaded, so the planet is reached. The game loads it again when Ratchet dies, which is not a
  new planet: the mod keeps the planet it last saw.
- **the level the port has loaded** (`openrac_api.loaded_level`): none means the game is in the
  galaxy map, so the planet before it was left.

Nothing of the game is changed, and no hook point of the game's is replaced.

**The commands.** The mod sends LiveSplit the ones its server knows: `reset`, `starttimer`,
`split` and `setgametime` (the game's own time, for the Game Time timing method). It speaks the
server's own line protocol, over TCP or over a WebSocket: the mod asks the server which one it is
when the setting says so, and the answer tells it.

**The thread.** The socket is a thread of its own: the game's thread only puts the commands in a
queue and reads what the client knows of the connection, and never waits for the network. The
client never calls into the port.

## Tests

```sh
ctest --test-dir build -R live_splits
```

- **splits**: the state machine alone: the two ways to split, the two ways to start the timer, the
  game's own surprises (Ratchet dies and the level is loaded again, the player goes back to a
  planet he left), the run's end, and the time as LiveSplit's parser reads it;
- **load**: the library as the port loads it, against a host of the test's own: its refusals, its
  hook point and its signature, and, against a LiveSplit server the test makes, the commands it
  sends over TCP, over a WebSocket and when it asks the server which one it is; and with no server
  at all, that it says so and goes on.

## What it needs

An OpenRAC whose port has the `Level_LoadGameplay` hook point (the behaviour catalogue's, in
every OpenRAC of 2026-10-21 and later). An older one refuses the mod with the reason.
