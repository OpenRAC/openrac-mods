# Multiplayer

Online multiplayer for Ratchet & Clank on OpenRAC's native port (PAL and NTSC-U discs), first step:
a **Multiplayer** item in the game's main menu that connects to an OpenRAC server and puts you in a
party with other players. **Nothing of the game is played together yet**: this version is the menu,
the lobby and the connection. Seeing the other players in a level comes in a later version.

With the mod off, the game is the PlayStation 2 game.

## Playing

1. Install the mod from the launcher's **Mods** tab, switch it on, and set **Server** to the
   OpenRAC server you play on (`localhost:7770` by default: a server on your own computer).
2. Your name is the launcher's **Multiplayer name** (Settings, General). The launcher makes one up
   at its first start; change it there (3 to 16 letters, digits and underscores).
3. Start the game. The title's main menu reads **New Game**, **Load Game**, **Multiplayer**,
   **Options**: choose **Multiplayer**.

Its pages are the game's own menus: its frames, its list and font, its prompt box and sounds.

| Page | What it does |
|---|---|
| Home | **Find match**: the server puts you in an open party that has room, or makes you the host of a new one others can find. **Create match**: a private party, which only its code joins. **Join by code**: the next page. The three wait for the server's hello. Below them, your name |
| Join by code | A row per character of the party's code: Left and Right (or Cross) step it, Up and Down move between them; **Join** joins. Codes use the letters and digits that do not read alike (no 0, O, 1 or I). Once in the party, the lobby takes this page's place |
| Lobby | The party's code in the title, **Ready** (a member: it is ready, or no longer) or **Start** (the host: it starts the game for everyone; in this version that is all it does), **Leave**, then the members (up to 4) with the host and the ready ones marked, and the round trip to the server. **Leave** and Triangle ask first; when the host leaves, the party is over for everyone |

Cross chooses, Triangle goes back a page (from Home, to the main menu). The footer's first line says
what the connection is doing: connecting, connected, **server unreachable** (the mod tries again
every few seconds), or **not connected** when the server went away, and the server's answers (a code
no party has, a full party, the host leaving).

| Setting | Default | |
|---|---|---|
| Server | `localhost:7770` | The OpenRAC server, as host:port (`[::1]:7770` for an IPv6 address); 7770 when no port is given |

## How it works

The mod needs OpenRAC's mod interface 1.7, whose pages of the game's own menus put the
**Multiplayer** item in the main menu under Load Game and let the mod say what its pages list
(the port draws them with the game's widgets). It keeps one TCP connection to the server
(TCP_NODELAY), opened the first time Home shows and kept until the game ends, on a thread of its own: the game's thread only queues frames
and reads what came. The server is the OpenRAC server (the `Server` repository): it links players
into parties and forwards their traffic, and the host's game is to be the authoritative one.

The wire format is the server's own: every packet is a frame, a 4-byte big-endian length, the
packet id, an opcode for PARTY, then the payload, big-endian, strings behind a 16-bit length (-1
for an empty one). The mod speaks all of it:

| Packet | The mod sends | The mod reads |
|---|---|---|
| 0 HELLO | its version (`rac1-multiplayer/0.2.0`) and the player's name, on every connection | the session id |
| 1 PARTY | CREATE (0), JOIN code (1), LEAVE (2), FIND (7) | INFO (3), JOINED (4), LEFT (5), END (6) |
| 2 CHAT | (nothing yet) | sender and message (not shown yet) |
| 3 DATA | the lobby's Ready and Start, below | the same, and anything else for the game data to come |
| 4 ERROR | | the reason: no such party, full, already in one, not in one |
| 5 TIME | its monotonic clock in ms, once after hello | the echo and the server's clock: the round trip, and the server's clock offset, smoothed (5 % a sample) |

Ready and Start are the mod's own, carried in DATA, a kind byte first: **1** READY with a flag
byte (a member to the host), **2** STATE with a count byte and an id int and a flag byte per member
(the host to everyone: who is ready), **3** START (the host to everyone). Any other first byte is
left for the game data of a later version.

## Building

Built with the repository's other native mods (the top README), with an SDK of interface 1.7 or
newer: until that SDK is released (`sdk.lock` pins an older one, and the top CMakeLists.txt then
skips this mod), build against OpenRAC's own `sdk/`:

```sh
cmake -S . -B build -G Ninja -DOPENRAC_SDK=<OpenRAC>/sdk
cmake --build build && ctest --test-dir build -R multiplayer
python3 <OpenRAC>/sdk/tools/package_mod.py build/package/multiplayer -o dist
```

`tests/multiplayer_test.cpp` checks the frames byte for byte against the server's encoding, the
lobby's logic against a recorder, and the connection against a small server on the loopback.

| File | |
|---|---|
| `src/multiplayer.cpp` | the mod: the main-menu item and its pages |
| `src/session.hpp`, `.cpp` | the session and the party: hello, the clock, create, find, join, leave, Ready and Start |
| `src/connection.hpp`, `.cpp` | the socket on its thread, reconnecting |
| `src/protocol.hpp`, `.cpp` | the server's frames and packets |
