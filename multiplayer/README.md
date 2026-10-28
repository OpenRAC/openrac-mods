# Multiplayer

Online multiplayer for Ratchet & Clank on OpenRAC's native port (PAL and NTSC-U discs), first step:
a **Multiplayer** item in the game's main menu that connects to an OpenRAC server and puts you in a
party with other players, or, with **Direct connect**, plays without a server: one player's game
hosts and the others join it by its address. **Nothing of the game is played together yet**: this version is the menu,
the lobby and the connection. Seeing the other players in a level comes in a later version.

With the mod off, the game is the PlayStation 2 game.

## Playing

1. Install the mod from the launcher's **Mods** tab, switch it on, and set **Server** to the
   OpenRAC server you play on (`localhost:7770` by default: a server on your own computer).
2. Your name is the mod's **Name** setting, or, while that is empty, `${player_name}` (the default) or not a valid name, the
   launcher's **Multiplayer name** (Settings, General), which the launcher makes up at its first
   start. Either way it is 3 to 16 letters, digits and underscores. Without either (the game started
   without the launcher), the mod makes one up (`Player` and three digits).
3. Start the game. The title's main menu reads **New Game**, **Load Game**, **Multiplayer**,
   **Options**: choose **Multiplayer**.

Its pages are the game's own menus: its frames, its list and font, its prompt box and sounds.

| Page | What it does |
|---|---|
| Home | **Find match**: the server puts you in a waiting party that has room, or makes you the host of a new one others can find. **Create match**: a party of your own, which Find match fills and its code invites to. **Join by code**: the next page. The three wait for the server's hello. **Direct connect**: the next page, no server needed. Below them, your name |
| Join by code | A row per character of the party's code: Left and Right (or Cross) step it, Up and Down move between them; **Join** joins. Codes use the letters and digits that do not read alike (no 0, O, 1 or I). Once in the party, the lobby takes this page's place |
| Direct connect | **Host directly**: your game hosts (below); the lobby opens. **Join by address**: the address's rows take this page's place: **Address part 1** to **4** (each 0 to 255), **Port** (by 1) and **Port, by 100**, all stepped with Left and Right (held, they repeat, and after a moment step ten at a time), then **Join**; the title shows the address as it stands (`Join 192.168.1.1:7770` at first; the last one joined is kept until the game ends). Triangle goes back from the rows to the two buttons (giving up a join still connecting), then to Home |
| Lobby | The party's code in the title (a direct party: **Hosting on** your address and port, every address of yours that fits, or **Party at** the host's address), **Ready** (a member: it is ready, or no longer) or **Start** (the host: it starts the game for everyone; in this version that is all it does), **Leave**, then the members (up to 4) with the host and the ready ones marked, and the round trip to the server. **Leave** and Triangle ask first (**Stop hosting?** for a direct host); when the host leaves, the party is over for everyone. Leaving a direct party goes back to Home and to the server |

Cross chooses, Triangle goes back a page (from Home, to the main menu). The footer's first line says
what the connection is doing: connecting, connected, **Server unreachable** (the mod tries again
every few seconds), or **The server went away**, and the server's answers (a code no party has, a
full party, the host leaving). Only Find match, Create match and Join by code need the server (they
are dim until it said hello); **Direct connect**, Host directly and Join by address are always
selectable. Choosing Direct connect stops the server retries, and the Direct pages never show the
server's state; leaving a direct party goes back to Home, which tries the server again.

| Setting | Default | |
|---|---|---|
| Server | `localhost:7770` | The OpenRAC server, as host:port (`[::1]:7770` for an IPv6 address); 7770 when no port is given |
| Name | `${player_name}` (the launcher's Multiplayer name) | Your name in multiplayer, 3 to 16 letters, digits or underscores; empty, `${player_name}` or not valid: the launcher's Multiplayer name |

## Direct connect

Playing without a server: one player's game hosts, and the others join it by its address. The
default stays the server; Direct connect is for a LAN, or for friends who can reach each other.

Home, **Direct connect**:

- **Host directly**: your game listens on port **7770** (TCP, and UDP on the same port), and you are
  the host of its one party. The lobby's title shows your address and port to give the others
  (`Hosting on 192.168.1.20:7770`; more lines when the computer has more than one address). A port
  already taken (another host, or a server on the same computer) is refused: the page stays, and
  its footer says "Port 7770 is in use". **Leave** asks "Stop hosting?" and ends the party.
- **Join by address**: set the host's address with the rows (four parts and the port), then
  **Join**. You are in the host's party as soon as you are connected: there is no code to type.
  While it cannot reach the host, the footer says so and it keeps trying; Triangle gives up.

The game has no text entry, so the address is stepped in with Left and Right. Leaving a direct
party (or the host ending it) goes back to Home and to your server, so Find match, Create match and
Join by code work again.

On a LAN this works as it is: give the others the address the lobby shows. Over the internet the
host has to forward the port (**TCP and UDP 7770**) on their router to their computer, and give out
their public address (the lobby shows only the LAN ones). Addresses are visible: everyone in the
party sees the host's address, and the host sees theirs. When the host leaves or stops hosting,
the party is over for everyone.

The host's game runs a small copy of the server's party routing (`src/host.cpp`), so it speaks the
same packets and datagrams, and the lobby and the game data behave the same in both modes. Its
differences from the server: one party, whose code is `DIRECT` (a lobby shows the host's address
instead); a client is put in it right after its hello (the party's INFO comes unasked), and Find
match, Create match or Join by code (any code) put a client back in after it left; the host's own
game is linked in-process, not by a socket. It keeps the server's limits: frames of 64 KiB, names
of 1 to 32 characters and chat of 1 to 256, a party of 4, the same rates per connection with
strikes, hello within 10 seconds, 8 connections an address (and 8 clients at once).

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
| 0 HELLO | its version (`rac1-multiplayer/0.3.2`) and the player's name, on every connection | the session id |
| 1 PARTY | CREATE (0), JOIN code (1), LEAVE (2), FIND (7) | INFO (3), JOINED (4), LEFT (5), END (6) |
| 2 CHAT | (nothing yet) | sender and message (not shown yet) |
| 3 DATA | the lobby's Ready and Start, below | the same, and anything else for the game data to come |
| 4 ERROR | | the reason: no such party, full, already in one, not in one |
| 5 TIME | its monotonic clock in ms, once after hello | the echo and the server's clock: the round trip, and the server's clock offset, smoothed (5 % a sample) |
| 6 UDP | | the session's UDP token and the server's UDP port (an older server sends none) |

**UDP**, for game state only (`Session::send_state`), when the server offers it: the connection
binds a UDP socket to the server's address and that port, sends `BIND` (session id, token, kind 0)
every 3 seconds to keep a NAT mapping open, and sends state as `STATE` datagrams (kind 1, the
target, the bytes; at most 1,200 bytes a datagram, larger state goes as DATA). The server's STATE
datagrams (kind 1, the sender, the bytes) arrive as the DATA they stand for. Until datagrams come
back, state also goes as DATA, so blocked UDP loses nothing (state may then arrive twice, so it is
stamped and the latest wins); once they come back within 15 seconds, UDP only. Lobby messages and
game events always go as DATA.

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
lobby's logic against a recorder, the connection against a small server on the loopback, and
Direct connect: a host and two clients on the loopback (the party, chat, DATA both ways, UDP state,
leaving, the host stopping) and the server's limits held by the host; and the pages' own logic:
the address rows stepping around their ends and faster when held, the hosting lines, and the name
chosen from the setting and the launcher's.

| File | |
|---|---|
| `src/multiplayer.cpp` | the mod: the main-menu item and its pages |
| `src/session.hpp`, `.cpp` | the session and the party: hello, the clock, create, find, join, leave, Ready and Start |
| `src/connection.hpp`, `.cpp` | the socket on its thread, reconnecting, UDP; the link to a server, a direct host or the embedded one |
| `src/host.hpp`, `.cpp` | Direct connect's embedded host: the server's party routing for one party |
| `src/direct.hpp`, `.cpp` | Direct connect's page logic: the address's rows (64-choice rings), held steps, the hosting lines |
| `src/sockets.hpp` | the socket calls the connection and the host share (Winsock and POSIX) |
| `src/protocol.hpp`, `.cpp` | the server's frames, packets and datagrams, both sides |
