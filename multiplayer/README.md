# Multiplayer

Online multiplayer for Ratchet & Clank on OpenRAC's native port (PAL and NTSC-U discs): a
**Multiplayer** item in the game's main menu that connects to an OpenRAC server and puts you in a
party with other players, or, with **Direct connect**, plays without a server: one player's game
hosts and the others join it by its address. The host picks a save (or a new game), and its
**Start** starts that game for the whole party. Everyone plays in the host's level, sees the
others' Ratchets running about in it, and shares its world: the host's game runs the enemies,
doors and platforms, the others show them and fight them, and an enemy goes for whichever player
is nearest.

With the mod off, the game is the PlayStation 2 game.

What is still to be decided (saving each player's progress, the game's own saves on members'
cards, mods that differ from the host's, and more), and what already is, is in
[DECISIONS.md](DECISIONS.md).

## Playing

1. Install the mod from the launcher's **Mods** tab, switch it on, and set **Server** to the
   OpenRAC server you play on (`game.openrac.dev:7770` by default, the public OpenRAC server;
   `localhost:7770` for a server on your own computer).
2. Your name is the mod's **Name** setting, or, while that is empty, `${player_name}` (the default) or not a valid name, the
   launcher's **Multiplayer name** (Settings, General), which the launcher makes up at its first
   start. Either way it is 3 to 16 letters, digits and underscores. Without either (the game started
   without the launcher), the mod makes one up (`Player` and three digits).
3. Start the game. The title's main menu reads **New Game**, **Load Game**, **Multiplayer**,
   **Options**: choose **Multiplayer**.

Its pages are the game's own menus: its frames, its list and font, its prompt box and sounds.

| Page | What it does |
|---|---|
| Home | **Browse games**: the games others host. **Create match**: a party of your own, from a save (the next page), which Browse games lists and its code invites to. **Join by code**: the next page. The three wait for the server's hello (the cursor moves up to Browse games when it comes). **Direct connect**: the next page, no server needed. Below them, your name |
| Browse games | **Refresh**, then a row for each game being hosted that has room: its host's name, its save ("Pokitaru, 12 gold bolts", or "New game") and how many are in it (`2/4`). Cross on a row joins it. The list is asked again every 3 seconds while the page shows; with none, a line says so and points at Create match |
| Choose a save | **New game**, then your memory card's five saves, each with its planet, gold bolts and time played (an empty slot is dim). The one chosen is what the party plays; your card is never written by the party. Create match and Host directly both come here first |
| Join by code | A row per character of the party's code: Left and Right (or Cross) step it, Up and Down move between them; **Join** joins. Codes use the letters and digits that do not read alike (no 0, O, 1 or I). Once in the party, the lobby takes this page's place |
| Direct connect | **Host directly**: your game hosts (below); the lobby opens. **Join by address**: the address's rows take this page's place: **Address part 1** to **4** (each 0 to 255), **Port** (by 1) and **Port, by 100**, all stepped with Left and Right (held, they repeat, and after a moment step ten at a time), then **Join**; the title shows the address as it stands (`Join 192.168.1.1:7770` at first; the last one joined is kept until the game ends). Triangle goes back from the rows to the two buttons (giving up a join still connecting), then to Home |
| Lobby | The party's code in the title (a direct party: **Hosting on** your address and port, every address of yours that fits, or **Party at** the host's address), **Ready** (a member: it is ready; chosen again, **Not ready**; once the game is on, **Join the game**) or **Start** (the host: the game starts for everyone; dim, and refused with the game's refusal sound, until every member is ready, and the count line says **not all ready**), **Leave**, what the party plays (**Playing:** the save), then the members (up to 4), the host marked and each other one **(ready)** or **(not ready)**, and the round trip to the server. **Leave** and Triangle ask first (**Stop hosting?** for a direct host); when the host leaves, the party is over for everyone. Leaving a direct party goes back to Home and to the server |
| Pause menu (in a level) | **Options**, then **Multiplayer** (after Video): the party's code (a direct party: where it is), the host's level, each member with the host and you marked and its round trip to the server and whether its game state goes by UDP or TCP (updated every 2 seconds), then **Leave party**, which asks first (**End the party?** for the host, **Stop hosting?** for a direct host). Out of a party it says **Not in a party**. Leaving goes back to the title's Multiplayer page (below) |

## Playing together

1. The host: **Create match** (or **Direct connect**, **Host directly**), then a save or **New game**.
2. The others: **Browse games** and Cross on the host's game (or **Join by code**, or **Join by
   address** for a direct one). Each says **Ready**.
3. The host: **Start**, once everyone is Ready (one who joins later is not, until it says so). Every game leaves the menu and loads the host's game in the host's level:
   a new game starts on Veldin, a save where it was saved. The others' cards are not touched.
4. In a level each game shows the others' Ratchets where they are, running, jumping and standing
   as they do, about a tenth of a second behind. They walk through you and nothing aims at them.
5. Only the host flies the ship: when it picks another planet in the ship's planet select, every
   member's game leaves its level at that moment, from wherever it is (playing, a scene, the pause
   menu, a vendor), and flies there. Nobody gets control on the new planet, and nothing of the
   host's world shows there, until everyone has arrived ("Waiting for the other players to arrive"
   says why; at most 90 seconds for one that never comes). A member's own pick of another planet
   lands the ship again where it is ("Only the host flies the ship to another planet"). Someone who joins
   while the game is on gets the host's progress as it is then and starts in the host's level;
   someone who quits to the title finds **Join the game** in the lobby in Ready's place, which
   asks the host for its game again.

Each player sees the others' Ratchets holding what they hold: the wrench, the weapon or gadget,
Clank on the back, with their swings and throws, and their Blaster shots and bombs (shown only:
what they hit is told by the shooter's game).

The world is the host's:

- **Enemies, doors, platforms, pickups** run in the host's game; the others' games show them where
  the host's are (about a tenth of a second behind) and do not run them. A member's wrench or
  weapon hitting one hits the host's, so it takes the damage there and dies for everyone.
- **Enemies go for the nearest player**: in the host's game each one sees the player nearest it
  as the hero, so it chases, aims at and hits that player; the hit hurts that player in their own
  game (knocked back as the game does it).
- **Crates** run in every game: a crate broken in one game breaks in all, with its debris and
  sound.
- **Bolts are the host's, once in the world.** Only the host's game drops them: its enemies' and
  its crates', a crate a member broke too (the host's copy of it breaks and drops). The members'
  games drop none of their own. Every player sees the same bolts fly out of the same crate or enemy
  and land in the same places. A bolt flies to the player nearest it (in the host's game that
  player's figure stands in for the hero, as for an enemy), and what it brings is that player's:
  break a crate next to it and its bolts are yours. Once landed, each one hovers and flies to
  whichever Ratchet comes near, in every game.
- **A bolt is the first collector's.** A member's collected bolt (one dropped or one lying about
  in the level) adds nothing in that member's game: it asks the host, who gives it to the first
  to ask while it is still there, takes it from every game and sends that player its bolts. A
  later collector of the same bolt gets nothing (its bolt has gone, without a count). The host's
  own are the host's at once. Each player has their own bolt count.
- **Other pickups** (gold bolts, ammo, nanotech) run in every game: anyone can collect one, and the
  first to collect it takes it from every game. Ammo and health an enemy drops in the host's game,
  and what a nanotech or ammo crate gives, are made in every game, for each player to collect.
- **Progress is the party's**: a gold bolt anyone collects counts for everyone (marked collected
  in every game, so it is not there on a reload or the next visit either), and a weapon, gadget or
  other item anyone gets (found, given or bought) and a planet anyone unlocks, everyone has. The
  host's game keeps the party's progress: a member's goes to it, and from it to every game, given
  as the game gives it (its banner) once that player is in control. Nothing is ever taken away.
- **A gold bolt's scene is the collector's**: it plays in that player's game only; the others
  keep playing, and nobody waits for it.
- A placed enemy or pickup gone in the host's game is gone in everyone's, also for someone who
  arrives in the level later. A moby goes only when a game says it died, never because a snapshot
  is missing: a game that is loading or in a menu changes nothing in the others.
- **Vendors, the ship, people, help desks**: each game's own, for its own player; the level a
  member is in follows the host's, and only the host's ship leaves for another planet.
- **Menus do not pause** while the party plays: the pause menu and the game's dialogs open over a
  world that goes on (enemies, platforms, the other players); your Ratchet waits where he is.
- **Scenes play for everyone**: a scene of the level's own that starts in the host's game (a
  person spoken to, a place reached, Veldin's opening) starts in each member's game in that level
  too, from its start, as soon as that member has control (a member in a menu starts it once the
  menu is closed), with its own actors. A member who started it late sees it to the end, even
  when its menu or a load kept it until after the host's had ended.
- **Only the host skips**: the host's Start in a scene or a movie skips it in every game. A
  member's Start in a scene or a movie does nothing but show "Only the host can skip" for a few
  seconds; the scene plays on everywhere.
- **Scenes hide the others**: while a scene plays in your game, the other players' Ratchets, what
  they hold and their shots are not shown; they are back as soon as the scene is over.
- **Scenes end together**: when the host is out of its scene, a member still in one is skipped
  (one that started the host's scene late only once it had as long as it was late), and nobody
  gets control back until everyone is out (at most 10 seconds for the host; a member waits for
  the host's scene to end, and for its own copy of it to play). The ship's landing and take-off
  count, and so do level intros. A skip ends only the scene or movie it was pressed in, never
  the next one.

Leaving. A player whose party goes while it plays (it chose Leave party, the host ended the party,
or its connection went) leaves its level at once, from wherever it is (playing, a scene, the pause
menu), for the title, which then opens the Multiplayer page; its footer says why when the player
did not leave by itself (**The host left: the party is over**, **The host went away**, **The
server went away**). The host leaving takes everyone there, the host too; a member leaving goes
there alone, and the party plays on without it. A player already in the title's pages (the lobby)
goes back to Home.

Cross chooses, Triangle goes back a page (from Home, to the main menu). The footer's first line says
what the connection is doing: connecting, connected, **Server unreachable** (the mod tries again
every few seconds), or **The server went away**, and the server's answers (a code no party has, a
full party, the host leaving). Only Find match, Create match and Join by code need the server (they
are dim until it said hello); **Direct connect**, Host directly and Join by address are always
selectable. Choosing Direct connect stops the server retries, and the Direct pages never show the
server's state; leaving a direct party goes back to Home, which tries the server again.

| Setting | Default | |
|---|---|---|
| Server | `game.openrac.dev:7770` | The OpenRAC server, as host:port (`[::1]:7770` for an IPv6 address); 7770 when no port is given |
| Name | `${player_name}` (the launcher's Multiplayer name) | Your name in multiplayer, 3 to 16 letters, digits or underscores; empty, `${player_name}` or not valid: the launcher's Multiplayer name |
| Verbose log | off | For reports of the party's world going wrong (mobys lost when a player pauses): an `[mp-trace]` line for each event, one a line, in `mp-trace.log` in the game's working directory (beside its `run.log`; the game's log says where, once), on either side; also on with `OPENRAC_MP_TRACE=1`. Each line is `[mp-trace] f=<frame> t=<ms> <host, member or solo> <what> ...`, `what` one of: `pause` (the phase and the level's game mode changing, a menu entered and left, and each tick a menu is open: the camera mods read, the camera the game's run list measured from, the level's camera the pause menu puts up and what the menu's update did with the world), `run` (a moby the run list left out by distance while a menu is open, one going in or out of range, with its distance and its update and draw distances; once a second under a menu how many ran), `vis` (a moby hidden, shown, dead, gone from the listing, or drawn or not under a menu), `despawn` (a moby gone or hidden: its class, key, slot, driven, shared or local, who removed it, the sync for a message and its sender, this game's own update or another moby's, a mod, or vanished with no deletion seen, its last place, its distance from the hero and the camera, its state and flags, and on a member when the host last sent it; each moby_kill of the sync's), `eyes` (the points the host's game runs the world around), `snap` (each snapshot sent or received: its count, the ids added and missing since the last), `drive` (a moby driven from the host's or let go), `msg` (KILLED, DEAD, DESPAWN, BROKE and SPAWN sent or received) and `census` (every second, the live mobys by class and the ones that came and went). At most 2,000 lines a second, the rest counted in one line. Off: none of them |

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

The mod needs OpenRAC's mod interface 1.9: 1.7's pages of the game's own menus put the
**Multiplayer** item in the main menu under Load Game and let the mod say what its pages list
(the port draws them with the game's widgets), and 1.8's play calls tell it what the game is doing,
give it the hero's pose, make the figures that stand for the others (mobys of the hero's class
that run none of his logic, collide with nothing and are no target), start a game or a save from
the title, travel to a level, and read the memory card's saves; 1.9's world calls list the level's
mobys, drive, pose, kill and make them, gate each moby's update (with the hero standing in for
another player), watch and post hits, and keep mobys near other players running. It keeps one TCP connection to the server
(TCP_NODELAY), opened the first time Home shows and kept until the game ends, on a thread of its own: the game's thread only queues frames
and reads what came. The server is the OpenRAC server (the `Server` repository): it links players
into parties and forwards their traffic, and the host's game is to be the authoritative one.

The wire format is the server's own: every packet is a frame, a 4-byte big-endian length, the
packet id, an opcode for PARTY, then the payload, big-endian, strings behind a 16-bit length (-1
for an empty one). The mod speaks all of it:

| Packet | The mod sends | The mod reads |
|---|---|---|
| 0 HELLO | its version (`rac1-multiplayer/0.6.0`) and the player's name, on every connection | the session id |
| 1 PARTY | CREATE (0) with the party's description (the save's words), JOIN code (1), LEAVE (2), LIST (8); FIND (7) is still understood | INFO (3, with the description), JOINED (4), LEFT (5), END (6), LIST (8: per party its code, host's name, members, size and description) |
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

The lobby and the game are the mod's own, carried in DATA (the server only forwards it; a member's
DATA reaches the host only, the host's goes to everyone or to one), a kind byte first:

| Kind | From | Bytes after the kind | |
|---|---|---|---|
| 1 READY | a member | flag u8 | ready, or no longer |
| 2 STATE | the host | count u8, then id i32 + flag u8 each | who is ready |
| 3 START | the host | | the lobby's Start |
| 4 PLAY | the host, to all or one | play i32, mode u8 (0 new game, 1 save), level i32 (-1: the save's), save size i32, build string | play this game; a save follows in CHUNKs. `play` names the host's game. A member whose build (the mod's version, OpenRAC's, the disc) differs refuses it: "Incompatible version" |
| 5 CHUNK | the host | play i32, offset i32, up to 16,000 bytes | a piece of PLAY's save (a Ratchet & Clank save is 59,912 bytes: four pieces) |
| 6 LEVEL | the host, to all | play i32, level i32, scene u8, travel u16, dest i32, arriving u8 | the level the host plays (-1 none) and whether it plays a scene, once a second and at each change; its last ship travel (a count and the planet) and whether the party still arrives there. A member in another level travels there, a member in a scene the host is out of skips it, a member that has not left for the host's newest travel leaves at once (a LEVEL without the travel fields, an older mod, travels as before) |
| 7 POSE | everyone, as state | owner i32, sequence u16, stamp i64 (the server's clock, ms), level i8, flags u8 (1 blending, 2 teleport, 4 in a scene), position, rotation and velocity (3 x f32 each), state, health and item (i16 each), seq, frame and next frame (u8 each), anim time u8 (/255), anim speed i16 (/1000); then what it wears and holds: count u8, per piece slot u8, part u8, item u8, class i16, joint u8, flags u8 (1 hidden, 2 blending, 4 loose), seq, frame, next frame, anim time (u8 each), anim speed i16, and for a loose piece its position and rotation (3 x f32 each) | a player's Ratchet, 20 a second (65 bytes, then 13 a piece); a member's goes to the host, who passes it on to everyone. A pose that ends after 65 bytes (an older mod) wears nothing |
| 8 WANT | a member | play i32 | in the title without the host's game: asks for it again |
| 9 WORLD | the host to a member, as state | level i8, stamp i64, count u8, then per moby: id u16 (its key; 0x8000 \| a net id for one spawned), position 3 x f32, rotation 3 x i16 (1/10,000 rad), seq, frame, anim time and speed (u8 each), flags u8 (1: hidden; 2: loose, a bolt landed in the host's game, which the member's game now runs as its own) | the 46 mobys nearest that member that moved or animated, 15 times a second (a landed bolt once, then once a second) |
| 10 KILLED | the host | level i8, count u16, keys u16... | placed mobys gone in the host's game |
| 11 SPAWN, 12 DESPAWN | the host | level i8, net u16, class i16, position, yaw; or a list of nets | creatures and bolts the host's game made, and their end |
| 13 HIT | a member | id u16, mask u32, damage f32, position, velocity | its hit on the host's moby |
| 14 DAMAGE | the host | attacker id u16, mask u32, damage f32, position, velocity | an enemy hit that member |
| 15 BOLTS | the host | count i32 | bolts that are that member's: a claim granted (the bolt's value), or what flew to its figure in the host's game |
| 16 DEAD | the host | level i8, records u16, a bit each | the placed mobys dead in the host's level, for one arriving there |
| 17 BROKE | a member | level i8, key u16 | it broke a crate or took a pickup (not a bolt: CLAIM) |
| 18 SKIP | the host, to all | origin i32, sequence u16, play i32, what u8 (0 a scene of the game's own, 1 the host's scene, 2 a movie), scene u16 (the host's scene by SCENE's sequence, else 0) | the host's Start pressed in a scene or a movie: each game skips that one only, while it plays (a member's SKIP, from an older mod, the host ignores); a skip of an earlier scene of the host's, of another play, or without these fields (an older mod) skips nothing |
| 19 SHOT | everyone | owner i32, level i8, item i16, position, velocity (3 x f32 each), aim 4 x f32 | a shot a player fired (the Blaster's, a bomb); a member's goes to the host, who passes it on. Its figure fires it as far behind as its poses play, hitting nothing in that game |
| 20 SCENE | the host, to all | play i32, level i32, scene i32, sequence u16, elapsed i32 (ms) | an in-level scene the host's game plays (the level's number of it; a new sequence each start), when it starts, once a second while it plays and when it ends (scene -1). A member in that level starts the same scene once it has control (the SDK's `play_scene`) |
| 21 SCENE_STATE | a member to the host | play i32, sequence u16, state u8 (0 still to play, 1 playing, 2 out) | where a member is with the host's scene of that sequence, at each change and once a second; the host waits for those of its newest scene only |
| 22 CLAIM | a member | level i8, id u16 (a placed bolt's key; 0x8000 \| a net id for one the host dropped) | its Ratchet collected that bolt (its game added nothing): the host's first claim of a bolt still there gets BOLTS and the bolt's end everywhere (DESPAWN or KILLED); any other, nothing |
| 23 PROGRESS | a member to the host; the host to all | items u8 count + a byte each, gold bolts u8 count + a byte each (level x 4 + its number), planets u8 count + a byte each (non-zero had) | a member: what its game has, at each change and every 5 s; the host: what the party has (its own game's, given what the members have), likewise and at each member that comes. Each game is given what it lacks; nothing is taken |
| 64 LINK | a member | ping i32 (ms, -1 not known), udp u8 | its link to the server, every 2 seconds |
| 65 LINKS | the host, to all | count u8, then id i32 + ping i32 + udp u8 each | every member's link (the host's own among them), for the pause menu's Multiplayer page |

A member's game is started from the title with the host's save (`play_start`), its own card left
alone; one who joins while the game is on gets the host's progress as it is (`save_capture`) and
starts in the host's level. Each game shows the others' poses through a buffer per player
(`src/play.hpp`, after INTERPOLATION.md): played back about 100 ms behind (more with jitter),
the position along a curve through each pose's velocity, the cursor running up to 15 % fast or slow
to keep its delay, 150 ms of extrapolation past the newest pose, and a jump for a teleport (a level
change, a respawn, a step longer than 8 units). A figure is made when a player is in the same level
and removed when it leaves the level, dies, goes silent for 5 seconds or leaves the party.

The shared world (`src/world.hpp`) stands on interface 1.9's world calls. A placed moby is named by
its key, its record in the level's file, the same in every game whatever was killed before. A member
in the host's level drives every placed moby that has an update of its own (its game no longer
runs it; its animation still plays and its collision follows it) and poses it from the host's
WORLD snapshots through a buffer per moby. In the host's game the gate around each moby's update
has the nearest player stand in for the hero (his position and his moby, the figure's), so the
game's own enemy code chases and hits that player, and the bolts collected during it are moved to
that player. Mobys near the members run in the host's game as if near its camera (eyes).

## Known limits

- The host's save is where the party starts. Bolts, ammo, health and what a member spends are
  each player's own; only the items, gold bolts and planets are shared (above). The game's own saves
  (a gold bolt's pickup, a story moment) still write that player's card: the party's starting it
  does not (play_start), its saves later on do.
- Only those three lists are shared: what the game keeps elsewhere (the gold weapons, the skill
  points, a level's own story flags) stays each game's.
- Vendors and the ship are each game's own. A member's flight to the new planet is the game's
  flight between planets, not the host's take-off from the pad (members leave when the host's
  ship does, and usually land first, then wait).
- A scene a member joins late starts from its beginning (a scene is read from the disc as it
  plays: it cannot start part way); scripted camera shots that are not scenes (a gold bolt's, a
  door's or a lift's camera, the level's camera scripts) play in the game whose moby runs them
  only.
- Under the pause menu the world goes on but the picture behind the menu stays as it was.
- A crate a member broke drops its bolts a round trip later (the host's copy breaks when the
  member's word gets there); one the host's copy has not broken within 3 seconds is deleted
  without a drop.
- A landed bolt a member collects leaves its game at once, and its count comes a round trip later
  (BOLTS); one another player took first in the meantime brings nothing. The member's own collect
  sparkle and counter do not show for it.
- An enemy that keeps a pointer to the hero across ticks, grabs him or rides him may act on the
  host's hero instead of the player it went for; WORLD.md (in the plans) lists the classes tried.
- A figure holds what its player holds (the wrench, the weapon or gadget, Clank and the packs,
  the boots), each loaded when this game can have it beside its own hero's (two weapon classes fit
  at once): while it cannot (two other players with two other weapons, or under a menu), the
  figure holds the wrench, or nothing when even that cannot be had.
- Of the shots, the Blaster's and the Bomb Glove's are shown; the other weapons' (the Pyrocitor's
  flame, the Devastator's missiles, the Visibomb, the gloves of Doom and mines, the Decoy, the
  Tesla Claw, the R.Y.N.O.) are not yet. A figure's Blaster shot flies on its heading without
  homing on the shooter's target. A thrown wrench is shown where it is 20 times a second, not
  smoothed.
- Names are not drawn over the figures (the mod interface has no drawing over the game yet).
- A member already in a level when the host starts again keeps its own progress and only follows
  the level.
- Everyone needs the same disc and the same OpenRAC and mod versions.

## Building

Built with the repository's other native mods (the top README), with an SDK of interface 1.9 or
newer: until that SDK is released (`sdk.lock` pins an older one, and the top CMakeLists.txt then
skips this mod), build against OpenRAC's own `sdk/`:

```sh
cmake -S . -B build -G Ninja -DOPENRAC_SDK=<OpenRAC>/sdk
cmake --build build && ctest --test-dir build -R multiplayer
python3 <OpenRAC>/sdk/tools/package_mod.py build/package/multiplayer -o dist
```

`tests/play_test.cpp` checks the poses' bytes, the interpolation buffer (a steady stream, repeats
and late poses, teleports, a stall then a burst, extrapolation), and the co-op of a host and members
through a router that does what the server does with DATA and fake games: Start with a save and
with a new game, the level followed, a late joiner and a member that asks again, figures made in
the same level only and gone with the level, the silence or the party; what a player holds on its
figure and its shots fired there (a delay behind, in its level only); a build refused and the
title after the party's end; and the world: WORLD's bytes, the member's drives (not the crates,
not the static mobys), the host's spawned bolt, an enemy followed, the stand-in and its bolts, a
member's hit on the host's enemy, an enemy's hit on a member, a crate broken by a member, and a
kill and a pickup on the host; and the bolts: the host's crate's and a member's crate's drops made
once, by the host, two claims of one bolt (the first credited), the host's own pickup, and the
placed bolts; and the progress: a member's gold bolt and the host's, gone and counted in every game
with no scene shared or held, an item and a planet reaching every game (once in control), a late
member given it all.

`tests/multiplayer_test.cpp` checks the frames byte for byte against the server's encoding, the
lobby's logic against a recorder, the connection against a small server on the loopback, and
Direct connect: a host and two clients on the loopback (the party, chat, DATA both ways, UDP state,
leaving, the host stopping) and the server's limits held by the host; and the pages' own logic:
the address rows stepping around their ends and faster when held, the hosting lines, and the name
chosen from the setting and the launcher's.

| File | |
|---|---|
| `src/multiplayer.cpp` | the mod: the main-menu item and its pages |
| `src/session.hpp`, `.cpp` | the session and the party: hello, the clock, create, list, join, leave, Ready and Start |
| `src/coop.hpp`, `.cpp` | playing together: PLAY and its save, LEVEL, WANT, poses sent and figures shown |
| `src/play.hpp`, `.cpp` | the poses' bytes and the interpolation buffer |
| `src/world.hpp`, `.cpp` | the shared world: snapshots, kills, spawns, hits, damage, bolts, stand-ins |
| `src/progress.hpp`, `.cpp` | the shared progress: items, gold bolts, planets |
| `src/trace.hpp`, `.cpp`, `src/world_trace.cpp`, `src/despawn.hpp`, `.cpp` | the verbose log: its lines and their cap; the world's pause, run, vis, drive and census lines; the despawn lines, who removed a moby and why |
| `src/connection.hpp`, `.cpp` | the socket on its thread, reconnecting, UDP; the link to a server, a direct host or the embedded one |
| `src/host.hpp`, `.cpp` | Direct connect's embedded host: the server's party routing for one party |
| `src/direct.hpp`, `.cpp` | Direct connect's page logic: the address's rows (64-choice rings), held steps, the hosting lines |
| `src/sockets.hpp` | the socket calls the connection and the host share (Winsock and POSIX) |
| `src/protocol.hpp`, `.cpp` | the server's frames, packets and datagrams, both sides |
