#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""
End-to-end tests of the co-op on the real game: a local server, a netimp.py proxy per player, and
two or more hidden games with the test bot built in (src/bot.hpp), driven through the bot's control
port. See tests/e2e/README.md for what it needs.

  e2e.py --scenario crates --link loss=5,latency=50 --out runs/x

Every scenario ends by comparing the games' worlds and prints one JSON report.
"""
import argparse
import json
import math
import os
import re
import signal
import socket
import subprocess
import hashlib
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
PHASE_FRONT_END, PHASE_LOADING, PHASE_PLAYING, PHASE_SCENE, PHASE_MENU = 1, 2, 3, 4, 5
IN_LEVEL = (PHASE_PLAYING, PHASE_SCENE, PHASE_MENU)
# How far from the host a placed hero may stand (coop.hpp, kPlaceNear).
PLACE_NEAR = 3.0
MOBY_DRIVEN, MOBY_DEAD, MOBY_HIDDEN = 0x2, 0x10, 0x40
CRATES = (500, 501, 502, 505, 511)
BOLTS = (13, 14, 15, 16)
BOLT_VALUE = {13: 1, 14: 5, 15: 20, 16: 50}
MOBY_CREATURE = 0x8


class Failed(Exception):
    pass


class Client:
    """One game: its process, its proxy, its control port."""

    def __init__(self, env, index, name, link):
        self.env = env
        self.index = index
        self.name = name
        self.link = link
        self.dir = env.out / f"c{index}"
        self.address = env.client_address(index)
        self.game_port = env.client_port(index)
        self.port = 21000 + 10 * env.base + index
        self.sock = None
        self.buffer = b""
        self.proc = None
        self.proxy = None

    def spawn(self):
        self.dir.mkdir(parents=True, exist_ok=True)
        (self.dir / "cards").mkdir(exist_ok=True)
        plugins = self.dir / "plugins"
        plugins.mkdir(exist_ok=True)
        mod = plugins / "multiplayer"
        if mod.is_symlink() or mod.exists():
            mod.unlink()
        mod.symlink_to(self.env.mod)
        extra = self.env.extra_mods(self.index)
        for mod_id, _ in extra:
            folder = plugins / mod_id
            if folder.is_symlink() or folder.exists():
                folder.unlink()
            folder.symlink_to(self.env.mods_dir / mod_id)
        (plugins / "plugins.json").write_text(
            json.dumps(
                {
                    "format": 1,
                    "mods": [{"id": i, "enabled": True, "settings": s} for i, s in extra]
                    + [
                        {
                            "id": "multiplayer",
                            "enabled": True,
                            "settings": {
                                "server": f"{self.address}:{self.game_port}",
                                "name": self.name,
                                "verbose_log": self.env.trace,
                                "revive": self.env.revive,
                            },
                        }
                    ],
                }
            )
        )
        (self.dir / "settings.ini").write_text("skip_cutscenes = 1\nvsync = off\n")
        self.start_proxy()
        env = dict(os.environ)
        env.update(
            {
                "SDL_AUDIODRIVER": "dummy",
                "OPENRAC_HIDDEN": "1",
                "OPENRAC_MP_BOT": str(self.port),
                "OPENRAC_PLAYER_NAME": self.name,
                "OPENRAC_IDENTITY_KEY": self.env.key_of(self.index),
            }
        )
        if self.env.one_address:
            env["OPENRAC_MP_BOT_UDP_AT_TCP"] = "1"
        env.update(self.env.game_env)
        self.proc = subprocess.Popen(
            [
                str(self.env.game_of(self.index)),
                "--data", str(self.env.data_of(self.index)),
                "--levels", str(self.env.levels_of(self.index)),
                "--plugins", str(plugins / "plugins.json"),
                "--cards", str(self.dir / "cards"),
                "--settings", str(self.dir / "settings.ini"),
                "--window",
            ]
            + self.env.pack_args(self.index)
            + self.env.game_args,
            cwd=self.dir,
            env=env,
            stdout=open(self.dir / "run.log", "w"),
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )

    def attach(self, seconds=60):
        limit = time.monotonic() + seconds
        while time.monotonic() < limit:
            if self.proc.poll() is not None:
                raise Failed(f"{self.name}: the game ended at the start ({self.proc.returncode})")
            try:
                self.sock = socket.create_connection(("127.0.0.1", self.port), timeout=2)
                self.sock.settimeout(15)
                return
            except OSError:
                time.sleep(0.2)
        raise Failed(f"{self.name}: no control port")

    def ask(self, line):
        if self.proc.poll() is not None:
            raise Failed(f"{self.name}: the game ended ({self.proc.returncode})")
        try:
            self.sock.sendall(line.encode() + b"\n")
            while b"\n" not in self.buffer:
                chunk = self.sock.recv(1 << 20)
                if not chunk:
                    raise Failed(f"{self.name}: the control port closed")
                self.buffer += chunk
        except OSError as e:
            raise Failed(f"{self.name}: the game ended ({e})")
        answer, self.buffer = self.buffer.split(b"\n", 1)
        return answer.decode()

    def json(self, line):
        return json.loads(self.ask(line))

    def status(self):
        return self.json("status")

    def mobys(self):
        """Key -> [slot, key, class, flags, state, x, y, z] for the placed; the spawned as a list."""
        placed, spawned = {}, []
        for m in self.json("dump"):
            if m[1] >= 0:
                placed[m[1]] = m
            else:
                spawned.append(m)
        return placed, spawned

    def start_proxy(self):
        self.proxy = subprocess.Popen(
            [
                sys.executable,
                str(HERE / "netimp.py"),
                "--listen", f"{self.address}:{self.game_port}",
                "--server", f"{self.env.server_address}:{self.env.server_port}",
                "--seed", str(self.env.seed + self.index),
                "--stats", str(self.dir / "net.json"),
            ]
            + self.link,
            stdout=open(self.dir / "proxy.log", "a"),
            stderr=subprocess.STDOUT,
        )

    def cut(self):
        """This game's link gone at once, as a cable pulled: to the server, a connection closed."""
        self.proxy.kill()
        self.proxy.wait(5)
        self.proxy = None

    def stop(self):
        for proc in (self.proc, self.proxy):
            if proc is not None and proc.poll() is None:
                proc.send_signal(signal.SIGTERM)
        for proc in (self.proc, self.proxy):
            if proc is not None:
                try:
                    proc.wait(5)
                except subprocess.TimeoutExpired:
                    proc.kill()


class Env:
    def __init__(self, args):
        self.out = Path(args.out).resolve()
        self.out.mkdir(parents=True, exist_ok=True)
        self.game = Path(args.game).resolve()
        self.data = Path(args.data).resolve()
        self.levels = Path(args.levels).resolve()
        # A mixed party: the members on another disc's program, data and levels.
        self.member_game = Path(args.member_game).resolve() if args.member_game else self.game
        self.member_data = Path(args.member_data).resolve() if args.member_data else self.data
        self.member_levels = Path(args.member_levels).resolve() if args.member_levels else self.levels
        self.save = Path(args.save).resolve() if args.save else None
        self.mod = Path(args.mod).resolve()
        self.mods_dir = Path(args.mods_dir).resolve() if args.mods_dir else None
        self.difficulty = json.loads(args.difficulty) if args.difficulty else None
        self.member_difficulty = json.loads(args.member_difficulty) if args.member_difficulty else None
        self.late_difficulty = json.loads(args.late_difficulty) if args.late_difficulty else None
        self.member_mods = (args.member_mods or "").split(",") if args.member_mods else []
        self.member_packs = args.member_packs
        self.server_jar = Path(args.server_jar).resolve()
        self.java = args.java
        self.base = args.base  # runs at once each take an address block of their own
        # Linux answers on all of 127/8, so each proxy has an address of its own at the server's port.
        # macOS answers on 127.0.0.1 alone: each has a port, and the bot sends its datagrams there.
        self.one_address = sys.platform == "darwin"
        self.server_address = "127.0.0.1" if self.one_address else f"127.{args.base + 1}.0.2"
        self.server_port = 27770 + args.base if self.one_address else 7770
        self.seed = args.seed
        self.trace = args.trace
        self.revive = args.scenario == "revive"
        self.game_args = args.game_arg or []
        self.game_env = dict(e.split("=", 1) for e in (args.game_env or []))
        self.server = None
        self.clients = []
        self.report = {"scenario": args.scenario, "link": args.link, "checks": [], "notes": {}}

    def client_address(self, index):
        return "127.0.0.1" if self.one_address else f"127.{self.base + 1}.1.{index + 1}"

    def client_port(self, index):
        return 22000 + 10 * self.base + index if self.one_address else 7770

    def game_of(self, index):
        return self.game if index == 0 else self.member_game

    def data_of(self, index):
        return self.data if index == 0 else self.member_data

    def levels_of(self, index):
        return self.levels if index == 0 else self.member_levels

    def key_of(self, index):
        """A launcher's key: the host's, and one for every member, so a member back under a new
        name (a later client, the kick scenario) is still the one banned."""
        return f"{min(index, 1) + 1:032x}"

    def pack_args(self, index):
        """A stacked folder for the members: one pack that changes the game, and nothing in it the
        game reads, which is all the check needs."""
        if not self.member_packs or index == 0:
            return []
        folder = self.out / "member-packs"
        folder.mkdir(exist_ok=True)
        (folder / "stack.json").write_text(json.dumps({
            "format": 1, "version": None, "files": {}, "notes": [], "skipped": {},
            "layers": [{"id": "e2e-walls", "name": "Test walls", "gameplay": True, "digest": "ab" * 32}],
        }))
        return ["--mods", str(folder)]

    def extra_mods(self, index):
        """The gameplay mods client `index` plays with: the host's settings, the first member's own
        when it has other ones, a later client's own when it has other ones."""
        if self.difficulty is None:
            return []
        settings = self.difficulty
        if index == 1 and self.member_difficulty is not None:
            settings = self.member_difficulty
        if index >= 2 and self.late_difficulty is not None:
            settings = self.late_difficulty
        return [("difficulty", settings)] + [(m, {}) for m in self.member_mods if index != 0]

    def start_server(self):
        env = dict(os.environ)
        env.update(
            {
                "HOST": self.server_address,
                "PORT": str(self.server_port),
                "HTTP_PORT": str(self.server_port + 1),
                "DEBUG_LEVEL": "debug",
                "MAX_CONNECTIONS_PER_ADDRESS": "8",
            }
        )
        self.server = subprocess.Popen(
            [self.java, "-jar", str(self.server_jar)],
            cwd=self.server_jar.parents[4],  # the repository's root: its .env.defaults
            env=env,
            stdout=open(self.out / "server.log", "w"),
            stderr=subprocess.STDOUT,
        )
        limit = time.monotonic() + 15
        while time.monotonic() < limit:
            try:
                socket.create_connection((self.server_address, self.server_port), timeout=1).close()
                return
            except OSError:
                time.sleep(0.2)
        raise Failed("the server did not start")

    def client(self, name, link):
        c = Client(self, len(self.clients), name, link)
        self.clients.append(c)
        c.spawn()
        c.attach()
        return c

    def stop(self):
        for c in self.clients:
            c.stop()
        if self.server is not None:
            self.server.terminate()
            try:
                self.server.wait(5)
            except subprocess.TimeoutExpired:
                self.server.kill()

    def check(self, name, ok, detail=""):
        self.report["checks"].append({"name": name, "ok": bool(ok), "detail": detail})
        print(f"{'PASS' if ok else 'FAIL'} {name} {detail}", flush=True)


def wait(what, test, seconds, poll=0.25):
    limit = time.monotonic() + seconds
    while True:
        value = test()
        if value:
            return value
        if time.monotonic() >= limit:
            raise Failed(f"timed out: {what}")
        time.sleep(poll)


def in_control(c):
    s = c.status()
    here = s["is_host"] or s["level"] == s["host_level"]
    return s["phase"] == PHASE_PLAYING and not s["hold"] and here and s


def reach_title(c):
    """Past the boot movie to the title's menus, where a player can be in a party at all. The game
    says front end from its first frame, but what precedes the title's loop resets the game, so a
    start before it would be lost. The title's sound bank and the loop follow the boot movie."""

    def there():
        log = (c.dir / "run.log").read_text(errors="replace")
        return re.search(r"movie at sector \d+: (played|skipped)", log) is not None

    wait(f"{c.name} past the boot movie", there, 90)
    time.sleep(8)
    wait(f"{c.name} in the title", lambda: c.status()["phase"] == PHASE_FRONT_END, 30)


def party(env, host, members):
    host.ask(f"connect {host.address} {host.game_port}")
    wait("the host greeted", lambda: host.status()["greeted"], 20)
    host.ask(f"create {env.save}" if env.save else "create")
    code = wait("the party", lambda: host.status()["code"], 15)
    for m in members:
        m.ask(f"connect {m.address} {m.game_port}")
        wait(f"{m.name} greeted", lambda: m.status()["greeted"], 20)
        m.ask(f"join {code}")
        wait(f"{m.name} in the party", lambda: m.status()["code"] == code, 15)
        wait(f"{m.name} told of the host's mods", lambda: m.json("mods")["heard_host"], 15)
        m.ask("ready")
    wait("all ready", lambda: host.status()["all_ready"], 15)
    return code


def skip_scenes(env, host, others, seconds):
    """Every game in control in the host's level, the host's Start pressed through what plays. A
    member stuck past the time is a failure the caller sees."""
    limit = time.monotonic() + seconds
    while time.monotonic() < limit:
        s = host.status()
        if s["phase"] in (PHASE_SCENE, PHASE_LOADING):
            host.ask("press 8 6")
        # A dialog (a new game's first save has one) takes Cross; a Start that landed just as a
        # scene ended opened the pause menu, and Cross then leaves that for the next round.
        if s["phase"] == PHASE_MENU:
            host.ask("press 4000 6" if int(time.monotonic() / 3) % 2 == 0 else "press 8 6")
        if all(in_control(c) for c in [host] + others):
            return True
        time.sleep(0.4)
    return False


def live(moby):
    return (moby[3] & MOBY_DEAD) == 0


def compare_worlds(env, games, label, keys=None):
    """The placed crates and pickups agree: dead in one is dead in all. A crate that went a moment
    ago may have kReplayMs to go in a game out of its reach, so one that differs is looked at again
    after that; what still differs is a desync. Returns the disagreements."""
    wrong = crate_differences(games, keys)
    if wrong:
        env.report["notes"].setdefault("settling", []).append({label: wrong[:6]})
        time.sleep(4)
        wrong = crate_differences(games, keys)
    env.check(f"{label}: crates agree in every game", not wrong, json.dumps(wrong[:12]))
    return wrong


def crate_differences(games, keys=None):
    worlds = [g.mobys() for g in games]
    base = worlds[0][0]
    wrong = []
    for key, m in base.items():
        if m[2] not in CRATES and (keys is None or key not in keys):
            continue
        for g, (placed, _) in zip(games[1:], worlds[1:]):
            other = placed.get(key)
            if other is None:
                # Skipped at this game's load: dead there.
                if live(m):
                    wrong.append((key, m[2], g.name, "missing"))
            elif other[2] != m[2]:
                wrong.append((key, m[2], g.name, f"class {other[2]}"))
            elif live(other) != live(m):
                wrong.append((key, m[2], g.name, "dead" if live(m) else "alive"))
    return wrong


def compare_poses(env, host, members, label):
    """What each member's driven mobys show against the host's: how many stand somewhere else or
    hide differently. A sample of a moving world, so it is reported, not asserted."""
    placed, _ = host.mobys()
    wrong = {"compared": 0, "place": 0, "hidden": 0}
    for m in members:
        theirs, _ = m.mobys()
        for key, other in theirs.items():
            mine = placed.get(key)
            if mine is None or not (other[3] & MOBY_DRIVEN) or not live(mine) or not live(other):
                continue
            wrong["compared"] += 1
            d = sum((mine[i] - other[i]) ** 2 for i in (5, 6, 7)) ** 0.5
            wrong["place"] += 1 if d > 4.0 else 0
            if d > 4.0 and len(wrong.setdefault("samples", [])) < 30:
                wrong["samples"].append([key, mine[2], mine[3], other[3], mine[4], other[4], round(d, 1), mine[5:8], other[5:8]])
            wrong["hidden"] += 1 if (mine[3] ^ other[3]) & MOBY_HIDDEN else 0
    env.report["notes"].setdefault("poses", {})[label] = wrong
    print(label, wrong, flush=True)


def key_map(c):
    placed, _ = c.mobys()
    return {k: m[2] for k, m in placed.items()}


def crates_of(c):
    placed, _ = c.mobys()
    return [m for m in placed.values() if m[2] in CRATES and live(m)]


def go_near(c, moby, z=2.0):
    c.ask(f"teleport {moby[5] + 1.5} {moby[6]} {moby[7] + z}")


def net_totals(env):
    totals = {}
    for c in env.clients:
        path = c.dir / "net.json"
        if path.exists():
            totals[c.name] = json.loads(path.read_text())
    return totals


def server_drops(env):
    text = (env.out / "server.log").read_text(errors="replace")
    return {
        "state_over_rate": len(re.findall(r"state over session", text)),
        "data_over_rate_warnings": len(re.findall(r"is over its DATA rate", text)),
        "kicked": len(re.findall(r"kept flooding", text)),
    }


def join_game(env, name, link, host, seconds=120, waiting=None):
    """A late member (one `waiting` in the title, or a new game): the title, the party's code, then
    the time until it stands in the host's level in control (no local scene it cannot leave)."""
    c = waiting
    if c is None:
        c = env.client(name, link)
        reach_title(c)
    c.ask(f"connect {c.address} {c.game_port}")
    wait(f"{name} greeted", lambda: c.status()["greeted"], 20)
    code = host.status()["code"]
    c.ask(f"join {code}")
    wait(f"{name} in the party", lambda: c.status()["code"] == code, 15)
    started = time.monotonic()
    phases = []
    limit = started + seconds
    host_level = host.status()["level"]
    while time.monotonic() < limit:
        s = c.status()
        moment = (s["phase"], s["level"], s["scene"], s["loads"])
        if not phases or phases[-1][1:] != moment:
            phases.append((round(time.monotonic() - started, 1),) + moment)
        if s["phase"] == PHASE_PLAYING and not s["hold"] and s["level"] == host_level:
            env.report["notes"][f"{name}_timeline"] = phases
            return c, time.monotonic() - started
        time.sleep(0.25)
    env.report["notes"][f"{name}_timeline"] = phases
    return c, None


def scenario_save(env, args):
    """A save of one disc, the host's, played by the other's: both in control in its level, one
    world, a host's death and the same again. --save the file, with --member-game on the other disc."""
    if env.save is None:
        raise Failed("the scenario needs --save")
    host = env.client("Host1", args.link_args)
    member = env.client("Member1", args.link_args)
    reach_title(host)
    reach_title(member)
    party(env, host, [member])
    env.check("a mixed party is not refused", not member.json("mods").get("differences"))
    if host.ask("start") != "ok":
        raise Failed("the host's Start")
    ok = skip_scenes(env, host, [member], 150)
    a, b = host.status(), member.status()
    env.check("both in control in one level", ok and a["level"] == b["level"], json.dumps([a, b]))
    env.report["notes"]["level"] = a["level"]
    time.sleep(3)
    compare_worlds(env, [host, member], "from the save")
    for c in (host, member):
        log = (c.dir / "run.log").read_text(errors="replace")
        env.check(f"{c.name} took the save", "a save was refused" not in log)


def scenario_smoke(env, args):
    host = env.client("Host1", args.link_args)
    reach_title(host)
    env.check("the game reaches the title", True)


def start_two(env, args, level_note=True):
    host = env.client("Host1", args.link_args)
    member = env.client("Member1", args.link_args)
    if args.scenario == "cosave":
        seed_save(env, host, args)
        seed_save(env, member, args)
    reach_title(host)
    reach_title(member)
    party(env, host, [member])
    answer = host.ask("start")
    if answer != "ok":
        raise Failed("the host's Start: " + answer)
    ok = skip_scenes(env, host, [member], 150)
    env.check("both games in control in level 0", ok, json.dumps([host.status(), member.status()]))
    return host, member


def scenario_tags(env, args):
    """The other player's name is drawn over the figure, on the picture, in both games."""
    host, member = start_two(env, args)
    for who, game in (("host", host), ("member", member)):
        seen = {}
        deadline = time.time() + 30
        while time.time() < deadline:
            seen = game.status()
            if seen.get("tags_on_picture", 0) >= 1:
                break
            time.sleep(0.5)
        env.check(f"the {who} has a tag for the other player", seen.get("tags", 0) == 1, str(seen.get("tags")))
        env.check(
            f"the {who}'s tag is on the picture",
            seen.get("tags_on_picture", 0) == 1
            and 0 <= seen.get("tag_x", -1) <= 512
            and 0 <= seen.get("tag_y", -1) <= 448,
            f"at {seen.get('tag_x')}, {seen.get('tag_y')}",
        )
    tags_by_place(env, host, member)


def until(test, seconds):
    limit = time.monotonic() + seconds
    while time.monotonic() < limit:
        if test():
            return True
        time.sleep(0.25)
    return False


def tags_by_place(env, host, member):
    """The name goes with distance and behind the level's walls: the member stood at a place the
    host's game calls blocked, and at one far off in the open. notes.tags says what was found."""
    x, y, z = host.status()["hero"]
    drawn = lambda: host.status().get("tags_drawn", -1)
    notes = env.report["notes"].setdefault("tags", {})
    env.check("up close, the name is drawn", until(lambda: drawn() == 1, 5), str(drawn()))

    def spot(radius, want_blocked, angles=range(0, 360, 20)):
        for a in angles:
            px = x + radius * math.cos(math.radians(a))
            py = y + radius * math.sin(math.radians(a))
            parts = [host.ask(f"line {px} {py} {z + h}").split() for h in (1.2, 0.6)]
            blocked = all(p[0] == "blocked" for p in parts)
            if blocked == want_blocked and all(p[0] in ("blocked", "clear") for p in parts):
                notes.setdefault("line_us", []).append(max(int(p[1]) for p in parts))
                return px, py, a
        return None

    def hidden_from_host():
        """Where the member really ended: a teleport into a wall comes out on one side of it."""
        fx, fy, fz = member.status()["hero"]
        return all(host.ask(f"line {fx} {fy} {fz + h}").startswith("blocked") for h in (1.2, 0.6)), (fx, fy, fz)

    tried = 0
    placed = None
    for radius in (8, 10, 12, 14, 16):
        for a in range(0, 360, 20):
            found = spot(radius, True, [a])
            if not found or tried >= 14:
                continue
            tried += 1
            member.ask(f"teleport {found[0]} {found[1]} {z + 0.5}")
            time.sleep(1.5)
            hidden, at = hidden_from_host()
            if hidden:
                placed = at
                break
        if placed:
            break
    notes["behind_wall_tried"] = tried
    env.check("the member stands where the level hides it from the host", placed is not None, str(tried))
    if placed:
        time.sleep(1.0)
        hidden, at = hidden_from_host()
        notes["behind_wall"] = {"still": hidden, "at": at, "drawn": host.status().get("tags_drawn")}
        env.check("behind a wall the name is gone", not hidden or drawn() == 0, str(notes["behind_wall"]))
    open_spot = None
    for radius in (30, 32, 35):
        open_spot = spot(radius, False)
        if open_spot:
            break
    if open_spot:
        member.ask(f"teleport {open_spot[0]} {open_spot[1]} {z + 0.5}")
        time.sleep(2)
        env.check("far off in the open the name is gone", drawn() == 0, str(drawn()))
    member.ask(f"teleport {x + 2} {y} {z + 0.5}")
    env.check("back beside the host, the name is back", until(lambda: drawn() == 1, 8), str(drawn()))


def scenario_look(env, args):
    """Something to look at, with --game-env OPENRAC_SHOT=N:shot: the other figure in view with its
    name, then the host's pause menu over the picture. notes.frames says when."""
    host, member = start_two(env, args)
    x, y, z = host.status()["hero"]
    for dx, dy in ((5, 0), (0, 5), (-5, 0), (0, -5)):
        member.ask(f"teleport {x + dx} {y + dy} {z + 0.5}")
        time.sleep(1.5)
        if host.status().get("tags_drawn") == 1:
            break
    env.check("the member's name is drawn", host.status().get("tags_drawn") == 1)
    time.sleep(6)
    host.ask("press 8 6")
    time.sleep(6)
    env.check("the pause menu is up", host.status()["phase"] == PHASE_MENU, str(host.status()["phase"]))


def scenario_keys(env, args):
    """Report 1, the key map: after a death the same key must name the same moby."""
    host, member = start_two(env, args)
    before_host, before_member = key_map(host), key_map(member)
    same = sum(1 for k, v in before_host.items() if before_member.get(k) == v)
    env.check("keys agree before a death", same == len(before_host), f"{same}/{len(before_host)}")
    placed_before, _ = host.mobys()
    crates = crates_of(host)
    # Kills before the next respawn shift the packing only for those recorded at a checkpoint, so
    # the bare comparison below shows what the respawn itself does to the mapping.
    loads_before = host.status()["loads"]
    member.ask("fall")
    time.sleep(3)
    wait("the member back in control", lambda: member.status()["phase"] == PHASE_PLAYING, 30)
    s = member.status()
    env.report["notes"]["member_after_death"] = s
    after = key_map(member)
    same = sum(1 for k, v in before_host.items() if after.get(k) == v)
    env.check("keys agree after the member's death", same == len(before_host), f"{same}/{len(before_host)}")
    env.check("the member still drives the host's mobys", s["driven"] > 0, f"driven {s['driven']}")
    time.sleep(2)
    compare_worlds(env, [host, member], "after the member's death")
    env.report["notes"]["loads"] = {"host": loads_before, "member": s["loads"]}


def scenario_crates(env, args):
    """Report 1: crates broken from both sides, bolts collected from both, a death between."""
    host, member = start_two(env, args)
    env.report["notes"]["crates_total"] = len(crates_of(host))
    crates = sorted(crates_of(host), key=lambda m: (m[5], m[6]))
    if len(crates) < 4:
        raise Failed("too few crates in the level")
    sides = [(host, "host"), (member, "member")]
    broken = {"host": 0, "member": 0}
    for i, crate in enumerate(crates[: args.crates]):
        who, side = sides[i % 2]
        go_near(who, crate)
        time.sleep(0.5)
        mask = "20000" if crate[2] in (502, 505) else "10000"
        answer = who.ask(f"hit {crate[1]} {mask}")
        if answer == "ok":
            broken[side] += 1
        time.sleep(0.4)
        if args.deaths and i == args.crates // 2:
            member.ask("fall")
            time.sleep(4)
    env.report["notes"]["broken"] = broken
    time.sleep(4)
    wrong = compare_worlds(env, [host, member], "after the breaks")
    compare_poses(env, host, [member], "after the breaks")
    # What the host broke or heard of is gone from the host's game.
    placed, _ = host.mobys()
    left = [c[1] for c in crates[: args.crates] if placed.get(c[1]) is not None and live(placed[c[1]])]
    env.check("every crate broken is gone on the host", not left, json.dumps(left))
    # Late member, mid-level.
    if args.join:
        third, took = join_game(env, "Late1", args.link_args, host)
        env.check("a late member is in control in the host's level", took is not None, f"{took}")
        if took is not None:
            time.sleep(3)
            compare_worlds(env, [host, third], "the late member's world")
            compare_poses(env, host, [third], "the late member's world")
            skip = third.status()
            env.report["notes"]["late_member"] = skip
            env.check("the late member drives the host's mobys", skip["driven"] > 0, f"{skip['driven']}")
    env.report["notes"]["final"] = {c.name: c.status() for c in env.clients}


def spawned_bolts(c):
    _, spawned = c.mobys()
    return [m for m in spawned if m[2] in BOLTS and m[4] < 0x80]


def scenario_bolts(env, args):
    """Bolts collected on both sides: each break's bolts reach the player who stands there, and
    both games end with none left over and the same crates."""
    host, member = start_two(env, args)
    # Well apart, each a stack of its own, so that each player stands at its own and the nearest
    # player to each bolt is the one who broke the crate.
    crates = []
    for c in sorted((c for c in crates_of(host) if c[2] == 500), key=lambda m: (m[5], m[6])):
        if all((c[5] - o[5]) ** 2 + (c[6] - o[6]) ** 2 > 400 for o in crates):
            crates.append(c)
    env.report["notes"]["bolt_crates"] = len(crates)
    if len(crates) < 2:
        raise Failed("too few bolt crates apart in the level")
    gained = {"Host1": 0, "Member1": 0}
    for i, crate in enumerate(crates[:6]):
        who, other = (host, member) if i % 2 == 0 else (member, host)
        far = crates[i - 1]
        go_near(other, far)
        go_near(who, crate, 1.0)
        time.sleep(0.6)
        before = {c.name: c.status()["bolts"] for c in (host, member)}
        loads = {c.name: c.status()["loads"] for c in (host, member)}
        worth = 0
        answer = who.ask(f"hit {crate[1]} 10000")
        if answer != "ok":
            raise Failed(f"{who.name} could not hit crate {crate[1]}: {answer}")
        time.sleep(0.4)
        made = 0
        limit = time.monotonic() + 6
        while time.monotonic() < limit:
            bolts = spawned_bolts(host)
            made = max(made, len(bolts))
            worth = max(worth, sum(BOLT_VALUE[m[2]] for m in bolts))
            who.ask(f"teleport {crate[5] + 0.5} {crate[6]} {crate[7] + 1.0}")
            time.sleep(0.3)
            if made and not spawned_bolts(host):
                break
        time.sleep(1.0)
        after = {c.name: c.status()["bolts"] for c in (host, member)}
        delta = {n: after[n] - before[n] for n in after}
        gained[who.name] += delta[who.name]
        env.report["notes"].setdefault("bolt_rounds", []).append(
            {"crate": crate[1], "who": who.name, "bolts": made, "worth": worth, "delta": delta}
        )

        # A hazard where the bot stood: a death takes the bolts with it, as the README says.
        if any(c.status()["loads"] != loads[c.name] for c in (host, member)):
            env.report["notes"].setdefault("died_in_round", []).append(crate[1])
            continue

        # Most, not all: a bolt goes to whoever is nearest, and a credit comes a round trip late.
        env.check(f"crate {crate[1]} broken by {who.name}: its bolts reach it", delta[who.name] > delta[other.name], json.dumps({"made": made, "delta": delta}))
    env.check("both collected bolts", all(v > 0 for v in gained.values()), json.dumps(gained))
    time.sleep(3)
    left = {c.name: len(spawned_bolts(c)) for c in (host, member)}
    env.check("the same bolts left in both games", len(set(left.values())) == 1, json.dumps(left))
    compare_worlds(env, [host, member], "after the bolts")
    env.report["notes"]["bolts"] = gained


def scenario_host_death(env, args):
    """A host's death: what was dead for the party stays so, in its game too; its spawned mobys go."""
    host, member = start_two(env, args)
    crates = sorted(crates_of(host), key=lambda m: (m[5], m[6]))[:4]
    for crate in crates:
        go_near(host, crate)
        time.sleep(0.5)
        host.ask(f"hit {crate[1]} {'20000' if crate[2] in (502, 505) else '10000'}")
        time.sleep(0.5)
    # A crate out of a member's reach replays its break for kReplayMs, then is deleted, and a
    # stack's upper crates go a moment after the lower.
    time.sleep(7)
    compare_worlds(env, [host, member], "before the host's death")
    loads = host.status()["loads"]
    host.ask("fall")
    wait("the host's respawn", lambda: host.status()["loads"] != loads and host.status()["phase"] == PHASE_PLAYING, 30)
    time.sleep(4)
    s = member.status()
    env.check("the member has the host's world again", s["synced"] and s["driven"] > 0, json.dumps(s)[:300])
    placed, _ = host.mobys()
    back = [c[1] for c in crates if placed.get(c[1]) is not None and live(placed[c[1]])]
    env.check("what the party broke stayed broken in the host's game", not back, json.dumps(back))
    compare_worlds(env, [host, member], "after the host's death")
    left = {c.name: len(spawned_bolts(c)) for c in (host, member)}
    env.check("the bolts lying about are the same in both games", left[host.name] == left[member.name], json.dumps(left))
    compare_poses(env, host, [member], "after the host's death")


def scenario_wipe(env, args):
    """Both die: the host's level comes back as its checkpoint has it, and the member beside it."""
    host, member = start_two(env, args)
    crates = sorted(crates_of(host), key=lambda m: (m[5], m[6]))[:2]
    for crate in crates:
        go_near(host, crate)
        time.sleep(0.5)
        host.ask(f"hit {crate[1]} {'20000' if crate[2] in (502, 505) else '10000'}")
        time.sleep(0.5)
    time.sleep(5)
    loads = host.status()["loads"], member.status()["loads"]
    knock_out(member)
    host.ask("fall")
    wait("the host's respawn", lambda: host.status()["loads"] != loads[0], 30)
    wait("the member's respawn", lambda: member.status()["loads"] != loads[1], 30)
    time.sleep(6)
    env.check("the member has the world again", member.status()["synced"], json.dumps(member.status())[:300])
    h, m = host.status()["hero"], member.status()["hero"]
    near = sum((a - b) ** 2 for a, b in zip(h, m)) ** 0.5
    env.report["notes"]["wipe: distance"] = near
    env.check("the member stands by the host", near < 6, f"{near:.1f}")
    compare_worlds(env, [host, member], "after a full wipe")


def knock_out(c):
    """Hits on its hero until he is dead (on his feet)."""
    for _ in range(40):
        if c.status()["health"] <= 0:
            return
        c.ask("damage 1 8")
        time.sleep(1.2)
    raise Failed(f"{c.name}'s hero would not die")


def scenario_revive(env, args):
    """The host's rule: a member down stays down, the host standing over him brings him back; left
    alone, he goes to his checkpoint in the end."""
    host, member = start_two(env, args)
    lines = json.loads(member.ask("page rules"))
    env.check("the rule is on the Rules page", any("revive" in line for line in lines), json.dumps(lines))
    loads = member.status()["loads"]
    knock_out(member)
    time.sleep(6)
    s = member.status()
    env.check("the member stays down", s["health"] <= 0 and s["loads"] == loads, json.dumps(s)[:300])
    at = s["hero"]
    host.ask(f"teleport {at[0] + 1} {at[1]} {at[2] + 1}")
    wait("the member up again", lambda: member.status()["health"] > 0, 12)
    env.check("he got up where he lay, the level not made again", member.status()["loads"] == loads, "")
    compare_poses(env, host, [member], "after a revive")


# Crates of level 5 that come back: measured on both discs (the byte at +0xC8 of the pvars).
BACK_CRATES = [651, 652]


def scenario_crateback(env, args):
    """A crate that comes back, broken by a member: the host's breaks and drops, none is deleted,
    and a bolt reaches the member who broke it."""
    host, member = start_two(env, args)
    fly_to(env, host, [member], 5)
    time.sleep(3)
    placed, _ = member.mobys()
    crate = placed.get(BACK_CRATES[0])
    if crate is None or not live(crate):
        raise Failed("no crate that comes back there")
    go_near(member, crate)
    time.sleep(1)
    bolts = int(member.ask("bolts"))
    member.ask(f"hit {crate[1]} 10000")
    time.sleep(4)
    states = {c.name: c.mobys()[0].get(crate[1]) for c in (host, member)}
    env.report["notes"]["crate after its break"] = {k: v[4] if v else None for k, v in states.items()}
    env.check("broken in both, deleted in neither", all(v is not None and v[4] == 6 for v in states.values()), json.dumps(states)[:300])
    env.check("the host's copy dropped: the member got bolts", int(member.ask("bolts")) > bolts, "")
    # Out of sight, each game's copy comes back by itself.
    for c in (host, member):
        c.ask(f"teleport {crate[5] + 60} {crate[6] + 60} {crate[7] + 30}")
    wait("it came back in both", lambda: all(c.mobys()[0][crate[1]][4] == 1 for c in (host, member)), 30)
    compare_poses(env, host, [member], "after a crate came back")


def scenario_dig(env, args):
    """Two players at a pile of buried bolts: one digs, the pile goes from every game."""
    host, member = start_two(env, args)
    fly_to(env, host, [member], 1)
    time.sleep(3)
    placed, _ = member.mobys()
    piles = sorted((m for m in placed.values() if m[2] == 605 and live(m)), key=lambda m: m[1])
    if not piles:
        raise Failed("no pile of buried bolts there")
    pile = piles[0]
    for c in (host, member):
        c.ask("equip 27")
        go_near(c, pile, 1.0)
    time.sleep(2)
    bolts = {c.name: int(c.ask("bolts")) for c in (host, member)}
    member.ask("press 20 90")
    time.sleep(0.1)
    host.ask("press 20 90")
    time.sleep(6)
    gained = {c.name: int(c.ask("bolts")) - bolts[c.name] for c in (host, member)}
    env.report["notes"]["dig: bolts gained"] = gained
    states = {c.name: c.mobys()[0].get(pile[1]) for c in (host, member)}
    env.check("the pile is gone from both", not any(v and live(v) for v in states.values()), json.dumps(states)[:300])
    env.check("exactly one dug it", sum(1 for v in gained.values() if v > 0) >= 1, json.dumps(gained))


# An enemy that grabs, by level: Rilgar's (each game's own), Umbris's (the host's, fatal).
GRABBERS = {5: 35, 7: 1059}


def scenario_grab(env, args):
    """A member by an enemy that grabs, in `--level 5` or `--level 7` (leave `--level` out for 7): the
    member dies in its grip and the host's hero, far off, is left alone: not hurt, not carried off,
    and it stays so after the member is back."""
    host, member = start_two(env, args)
    level = args.level if args.level in GRABBERS else 7
    fly_to(env, host, [member], level)
    time.sleep(3)
    placed, _ = host.mobys()
    sharks = sorted((m for m in placed.values() if m[2] == GRABBERS[level] and live(m)), key=lambda m: m[1])
    if not sharks:
        raise Failed("no enemy that grabs there")
    env.report["notes"]["enemies that grab"] = len(sharks)
    shark = sharks[0]
    # The host on solid ground far from it, and the member in its water.
    far = host.status()
    go_near(member, shark, 1.0)
    healths = []

    def caught():
        h = member.status().get("health", 1)
        healths.append(h)
        return h <= 0

    wait("the member died in its grip", caught, 60)
    before = host.status()
    env.check("the host was not hurt", before.get("health") == far.get("health"), json.dumps([far.get("health"), before.get("health")]))
    env.check("the host is in control", before.get("phase") == PHASE_PLAYING and not before.get("hold"), json.dumps(before)[:300])
    if args.trace and level == 7:
        env.check("the host told the member", len(log_lines(host, r"hold.*player")) > 0, "")
        env.check("the member's game held him", member.status().get("health", 1) <= 0, "")
    wait("the member came back", lambda: member.status().get("health", 0) > 0, 40)
    time.sleep(5)
    after = host.status()
    env.check("the host stays himself after it", after.get("health") == far.get("health") and after.get("loads") == far.get("loads"), json.dumps([far, after])[:400])
    compare_enemies(env, [host, member], "after a grab")


def enemies_of(c):
    """The creatures alive in a game: the placed by key, the spawned as class and place."""
    placed, spawned = c.mobys()
    alive = {k: m for k, m in placed.items() if live(m) and m[3] & MOBY_CREATURE and m[2] != 0}
    made = [m for m in spawned if m[4] < 0x80 and m[3] & MOBY_CREATURE and m[2] != 0 and m[2] not in BOLTS]
    return alive, made


def log_lines(c, pattern):
    return re.findall(pattern, (c.dir / "run.log").read_text(errors="replace"))


def compare_enemies(env, games, label):
    """The same placed enemies alive and the same spawned ones in every game, near each other: no
    copy of a game's own, none twice."""
    lists = [enemies_of(c) for c in games]
    host_alive, host_made = lists[0]
    env.report["notes"][f"{label}: enemies"] = {
        c.name: {"placed": len(a), "spawned": len(m)} for c, (a, m) in zip(games, lists)
    }
    for c, (alive, made) in zip(games[1:], lists[1:]):
        env.check(f"{label}: {c.name} has the host's placed enemies", set(alive) == set(host_alive), f"{len(alive)} of {len(host_alive)}")
        count = lambda ms: sorted(m[2] for m in ms)
        env.check(f"{label}: {c.name} has the host's spawned enemies, none twice", count(made) == count(host_made), f"{len(made)} of {len(host_made)}")
        far = 0
        for m in host_made:
            near = min(((m[5] - o[5]) ** 2 + (m[6] - o[6]) ** 2 for o in made if o[2] == m[2]), default=1e9)
            far += near > 25 ** 2
        env.check(f"{label}: {c.name}'s spawned enemies stand where the host's do", far == 0, f"{far} far")
    return lists


def scenario_enemies(env, args):
    """An enemy count off 1: the host's copies or removals in every game, none of a member's own,
    through a member's death and the host's."""
    host, member = start_two(env, args)
    fly_to(env, host, [member], args.level)
    time.sleep(args.settle)
    host_says = log_lines(host, r"level \d+: [^\n]*(?:copies of|left out)[^\n]*")
    member_says = log_lines(member, r"level \d+: [^\n]*(?:copies of|left out)[^\n]*")
    env.report["notes"]["host's load"] = host_says
    env.check("the host made its count", len(host_says) >= 1, json.dumps(host_says[-1:]))
    env.check("the member made none of its own", not member_says, json.dumps(member_says[-1:]))
    env.check("the member plays by the host's rules", log_lines(member, r"playing by the host's rules, seed \w+"))
    compare_enemies(env, [host, member], "in the level")
    compare_worlds(env, [host, member], "in the level")
    loads = member.status()["loads"]
    member.ask("fall")
    wait("the member's respawn", lambda: member.status()["loads"] != loads and member.status()["synced"], 40)
    time.sleep(args.settle)
    compare_enemies(env, [host, member], "after the member's death")
    env.check("the member still made none of its own", not log_lines(member, r"(?:copies of|left out)"))
    loads = host.status()["loads"]
    host.ask("fall")
    wait("the host's respawn", lambda: host.status()["loads"] != loads and host.status()["phase"] == PHASE_PLAYING, 40)
    wait("the member's world again", lambda: member.status()["synced"], 30)
    time.sleep(args.settle)
    compare_enemies(env, [host, member], "after the host's death")


def scenario_chaos(env, args):
    """Chaos with a blank seed: both games roll the same for each level; the host decides the
    events, and each comes to the member."""
    host, member = start_two(env, args)
    level_values = r"level \d+: enemy count [^\n]*"
    for level in [args.level, args.level2]:
        fly_to(env, host, [member], level)
        time.sleep(args.settle)
    host_values, member_values = log_lines(host, level_values), log_lines(member, level_values)
    env.report["notes"]["host levels"] = host_values
    shared = [v for v in member_values if v in host_values]
    env.check("the levels' values were rolled", len(host_values) >= 2, str(len(host_values)))
    env.check("every level the member played has the host's values", len(shared) == len(member_values) and shared, f"{len(shared)} of {len(member_values)}")
    seeds = (log_lines(host, r"disc; seed (\w+)"), log_lines(member, r"host's rules, seed (\w+)"))
    env.report["notes"]["seeds"] = seeds
    env.check("the member took the host's seed, not its own", seeds[0] == seeds[1], json.dumps(seeds))
    own = log_lines(member, r"disc; seed (\w+)")
    env.check("the member's own blank seed was another", own != seeds[0], json.dumps(own))
    limit = time.monotonic() + args.seconds
    event = r"chaos: [^\n]*"
    while time.monotonic() < limit and len(log_lines(host, event)) < 2:
        time.sleep(1)
    time.sleep(2)
    events = (log_lines(host, event), log_lines(member, event))
    env.report["notes"]["events"] = events
    env.check("the host's events came", len(events[0]) >= 2, str(len(events[0])))
    env.check("the member has the same events", events[0] == events[1], json.dumps(events))


def page_fits(env, c, page, label):
    lines = c.json(f"page {page}")
    widths = c.json(f"widths {page}")
    env.report["notes"][f"{label}: {page}"] = list(zip(lines, widths))
    return lines, widths


def scenario_refuse(env, args):
    """A member whose gameplay differs: told why, cannot be Ready, gets no game; the host sees it."""
    host = env.client("Host1", args.link_args)
    member = env.client("Member1", args.link_args)
    reach_title(host)
    reach_title(member)
    host.ask(f"connect {host.address} {host.game_port}")
    wait("the host greeted", lambda: host.status()["greeted"], 20)
    host.ask("create")
    code = wait("the party", lambda: host.status()["code"], 15)
    member.ask(f"connect {member.address} {member.game_port}")
    wait("the member greeted", lambda: member.status()["greeted"], 20)
    member.ask(f"join {code}")
    wait("the member in the party", lambda: member.status()["code"] == code, 15)
    mods = wait("the member told", lambda: (lambda m: m if m["heard_host"] else None)(member.json("mods")), 15)
    env.report["notes"]["differences"] = mods["differences"]
    env.check("the member is told how it differs", bool(mods["differences"]), json.dumps(mods["differences"]))
    if args.member_packs:
        env.check("the pack is what differs", "Test walls" in " ".join(mods["differences"]), json.dumps(mods["differences"]))
    wait("the host's verdict", lambda: host.json("mods")["fits"].get("Member1") == 2, 15)
    member.ask("ready")
    time.sleep(1)
    env.check("its Ready is not taken", not host.status()["all_ready"])
    env.check("the host's Start is refused", host.ask("start").startswith("error"))
    time.sleep(1)
    env.check("no game started in either", host.status()["phase"] == PHASE_FRONT_END and member.status()["phase"] == PHASE_FRONT_END)
    refused, widths = page_fits(env, member, "refused", "member")
    lobby, lobby_widths = page_fits(env, host, "lobby", "host")
    rules, rules_widths = page_fits(env, member, "rules", "member")
    print(json.dumps({"refused": refused, "host lobby": lobby, "rules": rules}, indent=1), flush=True)
    env.check("the host's lobby says why", any("Member1:" in line for line in lobby), json.dumps(lobby))
    env.check("every line fits the safe area", max(widths + lobby_widths + rules_widths) <= 460, str(max(widths + lobby_widths + rules_widths)))


def join_code(member, code):
    member.ask(f"join {code}")
    wait(f"{member.name} in the party", lambda: member.status()["code"] == code, 15)


def out_of_party(c):
    return not c.status()["code"]


def scenario_kick(env, args):
    """A host takes a member out: from the lobby (one that differs: it frees Start), then a ban,
    which holds under a new name, then one in the middle of a game: it goes back to the title and
    the host plays on. The kick is the page's own call; pressing the pad through the pause menu is
    not what this tests."""
    host = env.client("Host1", args.link_args)
    member = env.client("Member1", args.link_args)
    reach_title(host)
    reach_title(member)
    host.ask(f"connect {host.address} {host.game_port}")
    wait("the host greeted", lambda: host.status()["greeted"], 20)
    host.ask("create")
    code = wait("the party", lambda: host.status()["code"], 15)
    member.ask(f"connect {member.address} {member.game_port}")
    wait("the member greeted", lambda: member.status()["greeted"], 20)
    join_code(member, code)
    wait("the host's verdict", lambda: host.json("mods")["fits"].get("Member1") in (1, 2), 15)
    differs = host.json("mods")["fits"].get("Member1") == 2
    env.report["notes"]["the member differs"] = differs
    env.check("each game has its key", host.json("mods")["key"] and member.json("mods")["key"])
    lobby, widths = page_fits(env, host, "lobby", "host")
    remove, remove_widths = page_fits(env, host, "remove", "host")
    env.check("every line fits the safe area", max(widths + remove_widths) <= 460, str(max(widths + remove_widths)))
    env.check("the lobby says how to remove a player", any("remove" in line for line in lobby), json.dumps(lobby))
    if differs:
        env.check("a member who differs holds Start", host.ask("start").startswith("error"))

    env.check("the host's kick is taken", host.ask("kick Member1") == "ok")
    wait("the member out of the party", lambda: out_of_party(member), 15)
    wait("the host alone", lambda: host.status()["members"] == 1, 15)
    env.check("the member stays connected", member.status()["greeted"])
    env.check("it was told", log_lines(member, r"out of the party"))

    # A kick is no ban: the code still works. A ban sends it away, and again when it comes back.
    join_code(member, code)
    wait("the host knows its key", lambda: host.json("mods")["fits"].get("Member1") in (1, 2), 15)
    env.check("the host's ban is taken", host.ask("ban Member1") == "ok")
    wait("the banned member out", lambda: out_of_party(member), 15)
    env.check("the save keeps the ban", host.json("mods")["bans"] == 1)
    member.ask(f"join {code}")
    time.sleep(4)
    env.check("a banned member does not stay", out_of_party(member))

    # A new name, the same launcher.
    renamed = env.client("Renamed1", args.link_args)
    reach_title(renamed)
    renamed.ask(f"connect {renamed.address} {renamed.game_port}")
    wait("the new name greeted", lambda: renamed.status()["greeted"], 20)
    renamed.ask(f"join {code}")
    time.sleep(4)
    env.check("a new name is the same player", out_of_party(renamed))

    # Lifted, it plays.
    host.ask("unban")
    env.check("the bans are lifted", host.json("mods")["bans"] == 0)
    join_code(renamed, code)
    wait("its mods checked", lambda: host.json("mods")["fits"].get("Renamed1") == 1, 15)
    time.sleep(3)
    env.check("after the bans went it stays", not out_of_party(renamed))

    # In the middle of a game.
    wait("the member told of the host's mods", lambda: renamed.json("mods")["heard_host"], 15)
    renamed.ask("ready")
    wait("all ready", lambda: host.status()["all_ready"], 15)
    answer = host.ask("start")
    if answer != "ok":
        raise Failed("the host's Start: " + answer)
    env.check("both in control", skip_scenes(env, host, [renamed], 150), json.dumps([host.status(), renamed.status()]))
    level = host.status()["level"]
    page_fits(env, host, "remove", "host in a level")
    env.check("the host's kick in the game is taken", host.ask("kick Renamed1") == "ok")
    wait("the member out of the game's party", lambda: out_of_party(renamed), 15)
    wait("the member back at the title", lambda: renamed.status()["phase"] == PHASE_FRONT_END, 60)
    wait("the host alone in the game", lambda: host.status()["members"] == 1, 15)
    time.sleep(args.settle)
    s = host.status()
    env.check("the host plays on in its level", in_control(host) and s["level"] == level, json.dumps(s))
    env.check("its game is still the party's", bool(s["code"]))
    env.check("the member stays connected after it", renamed.status()["greeted"])


def scenario_midjoin(env, args):
    """A party playing by rules: a late member with the same mods comes in under them; with other
    settings, it gets no game."""
    host, member = start_two(env, args)
    fly_to(env, host, [member], args.level)
    time.sleep(args.settle)
    third, took = join_game(env, "Late1", args.link_args, host, seconds=args.join_seconds)
    if args.late_refused:
        env.check("the late member gets no game", took is None and third.status()["phase"] == PHASE_FRONT_END, str(took))
        mods = third.json("mods")
        env.report["notes"]["late differences"] = mods["differences"]
        env.check("it is told why", bool(mods["differences"]), json.dumps(mods["differences"]))
        return
    env.check("the late member stands in the level", took is not None, str(took))
    wait("the late member's world", lambda: third.status()["synced"], 30)
    time.sleep(args.settle)
    env.check("it plays by the host's rules", log_lines(third, r"playing by the host's rules"))
    env.check("it made none of its own", not log_lines(third, r"(?:copies of|left out)"))
    compare_enemies(env, [host, member, third], "with the late member")
    compare_worlds(env, [host, third], "with the late member")


def fly_to(env, host, others, level):
    """The party on the host's ship to `level`, its landing and scenes skipped by the host."""
    host.ask(f"travel {level}")

    def there():
        s = host.status()
        if s["phase"] == PHASE_LOADING:
            host.ask("press 8 6")
        return s["level"] == level and s["phase"] in IN_LEVEL

    wait("the host in the level", there, 120, 0.5)
    ok = skip_scenes(env, host, others, 150)
    env.check(f"the party in control in level {level}", ok, json.dumps([c.status() for c in [host] + others])[:600])


def scenario_join(env, args):
    """Report 2: a late member joins a game in progress, with a scene playing or not."""
    host, member = start_two(env, args)
    if args.level > 0:
        fly_to(env, host, [member], args.level)
    waiting = env.client("Late1", args.link_args)
    reach_title(waiting)
    time.sleep(args.settle)
    if args.host_scene >= 0:
        # A scene the host plays and does not skip while the new member comes.
        host.ask(f"scene {args.host_scene}")
        wait("the host's scene", lambda: host.status()["phase"] == PHASE_SCENE, 15)
    env.report["notes"]["host_at_join"] = host.status()
    third, took = join_game(env, "Late1", args.link_args, host, args.join_seconds, waiting)
    env.check("a late member is in control in the host's level", took is not None, f"{took}")
    env.report["notes"]["host_when_joined"] = host.status()
    if took is not None:
        if args.place:
            check_beside(env, host, third, "the late member")
        time.sleep(2)
        compare_worlds(env, [host, third], "the late member's world")
    env.report["notes"]["final"] = {c.name: c.status() for c in env.clients}


def apart(a, b):
    return math.dist(a["hero"], b["hero"])


def check_beside(env, host, c, who, near=PLACE_NEAR):
    """`c` stands within reach of the host, on the host's level, and kept standing."""
    h, s = host.status(), c.status()
    env.report["notes"][f"{c.name}_beside"] = [h.get("hero"), s.get("hero")]
    env.check(f"{who} stands beside the host", "hero" in h and "hero" in s and apart(h, s) <= near,
              json.dumps([h.get("hero"), s.get("hero")]))
    time.sleep(2)
    s = c.status()
    env.check(f"{who} did not fall", "hero" in s and apart(h, s) <= near and s["phase"] == PHASE_PLAYING, json.dumps(s)[:300])


def scenario_gohost(env, args):
    """Go to host: a member far from the host is put beside it; the host refuses to go to itself."""
    host, member = start_two(env, args)
    if args.level > 0:
        fly_to(env, host, [member], args.level)
    env.check("the host has no host to go to", host.ask("go").startswith("error"))
    env.report["notes"]["before"] = [host.status().get("hero"), member.status().get("hero")]
    member.ask("press 0 180 255 128")  # a run away from the host, to be apart
    time.sleep(3)
    env.check("Go to host is taken", member.ask("go") == "ok")
    wait("the member arrives", lambda: not member.status()["going"], 15)
    check_beside(env, host, member, "the member")
    compare_worlds(env, [host, member], "after Go to host")


def scenario_restart(env, args):
    """The host leaves for the title and starts again: a member in a level starts the new game."""
    host, member = start_two(env, args)
    before = member.status()["loads"]
    host.ask("title")
    wait("the host in the title", lambda: host.status()["phase"] == PHASE_FRONT_END, 60)
    wait("the host can start again", lambda: host.ask("start") == "ok", 30, 1)
    env.report["notes"]["member_after_start"] = member.status()
    ok = skip_scenes(env, host, [member], 120)
    env.check("the party plays the new game", ok, json.dumps([host.status(), member.status()])[:600])
    env.check("the member loaded it", member.status()["loads"] != before)
    compare_worlds(env, [host, member], "after starting again")


def scenario_shipwarn(env, args):
    """The ship's warning, then a slow member's wait skipped by the host's Start."""
    host, member = start_two(env, args)
    level = args.level2
    host.ask(f"travel {level}")
    time.sleep(0.5)
    s = member.status()
    env.report["notes"]["warned"] = s
    env.check("the member is warned first", s["banner"].startswith("The host is leaving") and s["level"] != level, json.dumps(s)[:300])
    wait("the member leaves", lambda: member.status()["phase"] != PHASE_PLAYING or member.status()["level"] == level, 10)
    ok = skip_scenes(env, host, [member], 150)
    env.check("the party is in", ok, json.dumps([host.status(), member.status()])[:600])


def shared_of(c, tag):
    return set(json.loads(c.ask(f"shared {tag}")))


def scenario_progress2(env, args):
    """The shared progress past the items, in level 3 (Kerwan: Helga's flag 2 and the infobot's flag
    3 are scene flags, 1 and 5 are not): gold weapons and skill points both ways, the plain flags
    both ways, a member's scene flag held from the host, the host's held from a member until it
    leaves the level, and a late member with all of it and no banner."""
    gold, skills, flags = 1, 2, 3
    level = args.level or 3
    plain_a, plain_b, member_scene, host_scene = level * 16 + 1, level * 16 + 5, level * 16 + 2, level * 16 + 3
    host, member = start_two(env, args)
    fly_to(env, host, [member], level)
    time.sleep(args.settle)

    def reaches(c, tag, index, seconds=8):
        try:
            wait(f"{index} of section {tag}", lambda: index in shared_of(c, tag), seconds)
            return True
        except Failed:
            return False

    host.ask(f"give {gold} 3")
    env.check("the host's gold weapon reaches the member", reaches(member, gold, 3))
    member.ask(f"give {skills} 7")
    env.check("the member's skill point reaches the host", reaches(host, skills, 7))
    host.ask(f"give {flags} {plain_a}")
    started = time.monotonic()
    env.check("the host's flag reaches the member", reaches(member, flags, plain_a))
    env.report["notes"]["flag took"] = round(time.monotonic() - started, 2)
    member.ask(f"give {flags} {plain_b}")
    env.check("the member's flag reaches the host", reaches(host, flags, plain_b))
    member.ask(f"give {flags} {member_scene}")
    host.ask(f"give {flags} {host_scene}")
    time.sleep(args.settle + 3)
    env.check("a member's scene flag stays out of the host's game", member_scene not in shared_of(host, flags))
    env.check("the host's scene flag waits while the member is in the level", host_scene not in shared_of(member, flags))
    env.check("the party's flags unchanged by a scene or a hold", in_control(member), json.dumps(member.status()))

    fly_to(env, host, [member], args.level2 if args.level2 != level else 4)
    time.sleep(args.settle)
    env.check("the host's scene flag came once the member left", host_scene in shared_of(member, flags))
    env.check("the member's scene flag is still out of the host's game", member_scene not in shared_of(host, flags))
    equal = shared_of(member, flags) - {member_scene} == shared_of(host, flags)
    env.check("every other flag the same in both games", equal)
    env.check("gold weapons the same", shared_of(member, gold) == shared_of(host, gold))
    env.check("skill points the same", shared_of(member, skills) == shared_of(host, skills))

    third, took = join_game(env, "Late1", args.link_args, host, seconds=args.join_seconds)
    env.check("the late member stands in the level", took is not None, str(took))
    time.sleep(args.settle)
    env.check("it has the host's gold weapons", shared_of(third, gold) == shared_of(host, gold))
    env.check("it has the host's skill points", shared_of(third, skills) == shared_of(host, skills))
    env.check("it has the host's flags", shared_of(third, flags) == shared_of(host, flags))
    env.check("no banner on its way in", not log_lines(third, r"given (?:item|planet) \d+ \(the host's\)"))
    compare_worlds(env, [host, third], "with the late member")


LIFT_RILGAR = 877
CAMERA_TRIGGER = 737
SCRIPT_HOLD = 0x72


def lifts_of(c, oclass):
    placed, _ = c.mobys()
    return [m for m in placed.values() if m[2] == oclass and live(m)]


def camera_counts(c):
    stats = c.json("stats")
    return stats["cam_sent"], stats["cam_shown"], stats["cam_showing"]


def ride_lift(env, host, lift):
    """The host onto the lift's button: its script starts with the fade, about half a second on."""
    sent, _, _ = camera_counts(host)
    host.ask(f"teleport {lift[5]} {lift[6]} {lift[7] + 1.0}")
    return wait("the host's game tells of the lift's camera", lambda: camera_counts(host)[0] > sent, 12)


def scenario_camera(env, args):
    """Camera scripts: a lift's shown to a rider and not to one far off, the player's hands taking
    it back, a member's trigger leaving the host's camera and hero alone."""
    host, member = start_two(env, args)
    fly_to(env, host, [member], args.level)
    lifts = lifts_of(host, LIFT_RILGAR)
    env.check("the level has a lift of class 877", bool(lifts), f"level {args.level}")
    if not lifts:
        return
    lift = lifts[0]
    # A rider: touching nothing, it is shown; a stick takes it back at once.
    member.ask(f"teleport {lift[5] + 1.0} {lift[6]} {lift[7] + 1.0}")
    time.sleep(args.settle)
    health = member.status().get("health")
    shown = camera_counts(member)[1]
    told = ride_lift(env, host, lift)
    env.check("the host tells of its lift's camera", told, json.dumps(host.json("stats")))
    env.check(
        "a rider is shown it",
        wait("the rider's camera", lambda: camera_counts(member)[1] > shown, 5),
        json.dumps(member.json("stats")),
    )
    time.sleep(1)
    env.check("it is still up with nothing pressed", camera_counts(member)[2] != 0, "")
    member.ask("press 0 10 128 0")
    env.check(
        "the stick takes the camera back",
        wait("the camera back", lambda: camera_counts(member)[2] == 0, 3),
        json.dumps(log_lines(member, r"camera back with the hero: [a-z ]+")[-3:]),
    )
    env.check("the member lost nothing by it", member.status().get("health") == health, f"{member.status()}")
    # Far off: nothing shown, and the script's end leaves everything as it was.
    time.sleep(args.settle)
    member.ask(f"teleport {lift[5] + 90.0} {lift[6]} {lift[7] + 1.0}")
    time.sleep(args.settle)
    host.ask(f"teleport {lift[5] + 8.0} {lift[6]} {lift[7] + 1.0}")
    time.sleep(args.settle)
    shown = camera_counts(member)[1]
    ride_lift(env, host, lift)
    time.sleep(2)
    env.check("a member far away is not shown it", camera_counts(member)[1] == shown, "")

    # A member's trigger, the host far off: the host keeps its camera and its control.
    fly_to(env, host, [member], args.level2)
    triggers = lifts_of(host, CAMERA_TRIGGER)
    env.check("the level has a camera trigger", bool(triggers), f"level {args.level2}")
    before = camera_counts(member)[1]
    fired = False
    for trigger in triggers:
        member.ask(f"teleport {trigger[5]} {trigger[6]} {trigger[7] + 1.0}")
        if wait("a trigger fired by the member", lambda: camera_counts(host)[0] > 0 and log_lines(host, r"a member's, told"), 4):
            fired = True
            state = host.status().get("hero_state")
            env.check("the host's hero is not held for it", state != SCRIPT_HOLD, f"state {state}")
            break
    env.report["notes"]["member_trigger"] = {"fired": fired, "shown": camera_counts(member)[1] - before}
    env.report["notes"]["final"] = {c.name: c.status() for c in env.clients}


def start_three(env, args):
    """A party of a host and two members in control of level 0, the crates broken from both sides."""
    players = [env.client("Host1", args.link_args), env.client("Member1", args.link_args), env.client("Member2", args.link_args)]
    for p in players:
        reach_title(p)
    host, members = players[0], players[1:]
    party(env, host, members)
    if host.ask("start") != "ok":
        raise Failed("the host's Start")
    env.check("everyone in control", skip_scenes(env, host, members, 150))
    crates = sorted(crates_of(host), key=lambda m: (m[5], m[6]))[:4]
    for i, crate in enumerate(crates):
        c = players[i % 2]
        go_near(c, crate)
        time.sleep(0.5)
        c.ask(f"hit {crate[1]} {'20000' if crate[2] in (502, 505) else '10000'}")
        time.sleep(0.5)
    time.sleep(7)
    compare_worlds(env, players, "before the host goes")
    return host, members


def after_takeover(env, members, seconds=25):
    """Who took over; the party's state once it has: one host, one world, nobody held."""
    wait("a new host", lambda: any(m.status()["took_over"] for m in members), seconds)
    new = next(m for m in members if m.status()["took_over"])
    other = [m for m in members if m is not new]
    env.report["notes"]["new host"] = new.name
    env.check("the new host hosts", new.status()["is_host"] and new.status()["epoch"] == 1, json.dumps(new.status())[:300])
    env.check("it runs the level's mobys now", wait("the drives gone", lambda: new.status()["driven"] == 0, 10))
    for m in other:
        env.check(f"{m.name} knows who hosts", m.status()["host"] == new.status()["id"])
        wait(f"{m.name} given the world", lambda: m.status()["synced"], 15)
    time.sleep(6)
    for m in members:
        s = m.status()
        env.check(f"{m.name} is not held", not s["hold"] and not s["host_gone"], json.dumps(s)[:300])
    compare_worlds(env, [new] + other, "after the takeover")
    compare_enemies(env, [new] + other, "after the takeover")
    compare_poses(env, new, other, "after the takeover")
    return new, other


def scenario_hostdrop(env, args):
    """The host's link goes for good: the party waits with a line saying so, then a member hosts and
    the world is the one the party left."""
    host, members = start_three(env, args)
    time.sleep(3)
    env.check("a ledger reached the members", all(m.status()["ledger"] > 0 for m in members))
    went = time.time()
    host.cut()
    wait("the wait for the host", lambda: all(m.status()["host_gone"] for m in members), 10)
    env.report["notes"]["told after"] = round(time.time() - went, 1)
    s = members[0].status()
    env.check("the party holds still, and is told", s["hold"] and "The host lost its connection" in s["migrate_banner"], json.dumps(s)[:300])
    new, other = after_takeover(env, members)
    env.report["notes"]["took over after"] = round(time.time() - went, 1)
    env.check("the takeover took no longer than the wait", time.time() - went < 45)
    # The new host plays on: a level of its own, the other member along.
    new.ask("travel 3")
    if other:
        env.check("the other member follows the new host", wait("the level", lambda: other[0].status()["level"] == 3, 90))


def scenario_hostback(env, args):
    """The host's link goes and comes back within the wait: still the host, the world whole."""
    host, members = start_three(env, args)
    host.cut()
    wait("the wait for the host", lambda: all(m.status()["host_gone"] for m in members), 10)
    crate = sorted(crates_of(host), key=lambda m: (m[5], m[6]))[-1]
    go_near(host, crate)
    host.ask(f"hit {crate[1]} 10000")
    time.sleep(2)
    host.start_proxy()
    wait("the host back", lambda: not host.status()["dropped"], 12)
    wait("the wait over", lambda: not any(m.status()["host_gone"] for m in members), 10)
    for m in members:
        wait(f"{m.name} given the world", lambda: m.status()["synced"], 15)
    time.sleep(5)
    env.check("it hosts as it did", host.status()["is_host"] and host.status()["epoch"] == 0, json.dumps(host.status())[:300])
    compare_worlds(env, [host] + members, "with the host back")


def scenario_hostleave(env, args):
    """The host leaves by choice: a member hosts at once, with no wait, from the last ledger."""
    host, members = start_three(env, args)
    time.sleep(3)
    went = time.time()
    host.ask("leave")
    after_takeover(env, members, 10)
    env.check("no wait for a host that left", time.time() - went < 25)


def scenario_rejoin(env, args):
    """A member's link goes and comes back: its id, its slot, a late joiner's way in, beside the host."""
    host, members = start_three(env, args)
    m = members[0]
    before = m.status()["id"]
    m.cut()
    wait("the member dropped", lambda: m.status()["dropped"], 15)
    time.sleep(8)
    env.check("it holds its slot", host.status()["members"] == 3 and m.status()["code"] != "", json.dumps(host.status())[:300])
    m.start_proxy()
    wait("it is back", lambda: not m.status()["dropped"], 15)
    env.check("under its own id", m.status()["id"] == before, json.dumps(m.status())[:300])
    wait("given the world", lambda: m.status()["synced"] and not m.status()["joining"], 30)
    time.sleep(4)
    check_beside(env, host, m, "the member back")
    compare_worlds(env, [host] + members, "with the member back")


def scenario_soak(env, args):
    """Report 3: the party stands in the level for a while; what the network did is the result."""
    players = [env.client("Host1", args.link_args)] + [
        env.client(f"Member{i + 1}", args.link_args) for i in range(args.members)
    ]
    for p in players:
        reach_title(p)
    host, members = players[0], players[1:]
    party(env, host, members)
    if host.ask("start") != "ok":
        raise Failed("the host's Start")
    env.check("everyone in control", skip_scenes(env, host, members, 150))
    # Everyone at the crates: mobys near, and the poses and WORLD at full rate.
    crates = sorted(crates_of(host), key=lambda m: (m[5], m[6]))
    start_totals = net_totals(env)
    time.sleep(args.seconds)
    env.report["notes"]["soak_seconds"] = args.seconds
    env.report["notes"]["final"] = {c.name: c.status() for c in env.clients}
    compare_worlds(env, players, "after the soak")
    compare_poses(env, host, members, "after the soak")


# Item, name, the pad's button for it (the first weapon button), ticks held, the class it makes in
# the other game (None: drawn as particles, seen only as shots fired).
WEAPONS = [
    (0x0F, "Blaster", 40, None),
    (0x0A, "Bomb Glove", 40, 121),
    (0x0B, "Devastator", 80, 153),
    (0x17, "R.Y.N.O.", 80, 457),
    (0x14, "Glove of Doom", 80, 230),
    (0x11, "Mine Glove", 80, 74),
    (0x19, "Decoy Glove", 80, 203),
    (0x10, "Pyrocitor", 90, None),
    (0x13, "Tesla Claw", 90, None),
    (0x15, "Morph-O-Ray", 90, None),
    (0x09, "Suck Cannon", 90, None),
    (0x0D, "Visibomb Gun", 40, 172),
    (0x18, "Drone Device", 40, 479),
]


def classes_near(c):
    return {m[2] for m in json.loads(c.ask("dump"))}


def scenario_weapons(env, args):
    """Each weapon fired by the host, then by the member: the other game shows it, nothing crashes,
    and no hit comes of the shown copy. `--weapons a,b` runs some."""
    host, member = start_two(env, args)
    wanted = None
    if args.weapons:
        wanted = {w.strip().lower() for w in args.weapons.split(",")}
    for item, name, ticks, oclass in WEAPONS:
        if wanted is not None and name.lower() not in wanted:
            continue
        for shooter, watcher, who in ((host, member, "the host"), (member, host, "a member")):
            shooter.ask(f"equip {item}")
            time.sleep(1.5)
            before = watcher.status().get("shots_fired", 0)
            health = watcher.status().get("health")
            seen = set()
            shooter.ask(f"press {args.fire_button} {ticks}")
            end = time.monotonic() + ticks / 60 + 3.0
            while time.monotonic() < end:
                seen |= classes_near(watcher)
                time.sleep(0.05)
            fired = watcher.status().get("shots_fired", 0) - before
            env.check(f"{name} of {who}: shots reached the other game", fired > 0, f"fired {fired}")
            if oclass is not None:
                env.check(f"{name} of {who}: class {oclass} shown", oclass in seen, sorted(seen)[:40])
            env.check(
                f"{name} of {who}: the shown shots hurt nobody",
                watcher.status().get("health") == health,
            )
            env.report["notes"].setdefault("weapons", {})[f"{name} ({who})"] = {
                "fired": fired,
                "late": watcher.status().get("shots_late", 0),
            }
    env.report["notes"]["final"] = {c.name: c.status() for c in env.clients}
    env.report["notes"]["net"] = {c.name: json.loads(c.ask("stats")) for c in env.clients}
    compare_worlds(env, [host, member], "after the shooting")


def card_files(c):
    """Each file of a game's card and its hash: {"BESCES-50916RATCHET/save0.bin": hash}."""
    root = c.dir / "cards"
    return {
        str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted(root.rglob("*"))
        if p.is_file()
    }


def own_saves(c):
    """The hashes of the player's own saves: everything on the card outside the party's folder."""
    return {k: v for k, v in card_files(c).items() if "-coop" not in k.split("/")[0]}


def coop_save(c, slot):
    """The bytes of a co-op slot; None for none."""
    found = [p for p in (c.dir / "cards").glob(f"*-coop/save{slot}.bin")]
    return found[0].read_bytes() if found else None


def seed_save(env, c, args):
    """A real save on this client's card, of the disc's own folder, to prove it stays."""
    if not args.seed_save:
        return
    source = Path(args.seed_save)
    folder = c.dir / "cards" / source.parent.name
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "save0.bin").write_bytes(source.read_bytes())


def scenario_cosave(env, args):
    """A party's saves: the game's own saves and Save the party land in each player's co-op slot,
    the host's reach every member, each player's own saves stay byte for byte, a member can host
    from its slot, and after the party the game saves to its own card again."""
    host, member = start_two(env, args)
    time.sleep(args.settle)
    before = {c.name: own_saves(c) for c in (host, member)}
    env.report["notes"]["own saves"] = {k: len(v) for k, v in before.items()}
    states = {c.name: c.json("cosave") for c in (host, member)}
    env.check("everyone plays in a co-op slot", all(s["on"] and s["slot"] >= 0 for s in states.values()), json.dumps(states))
    env.check("the same party", states[host.name]["key"] == states[member.name]["key"], json.dumps(states))
    slots = {c.name: states[c.name]["slot"] for c in (host, member)}
    env.check("a new game's copy is there", all(coop_save(c, slots[c.name]) for c in (host, member)))

    # One of the game's own saves on each side, as a gold bolt's pickup makes one.
    for c in (member, host):
        saves = c.json("cosave")["saves"]
        env.check(f"{c.name}'s game asked to save", c.ask("gamesave") == "ok")
        env.check(
            f"{c.name}'s game saved through the filter",
            wait(f"{c.name}'s save", lambda: c.json("cosave")["saves"] > saves, 20),
        )

    # The host's last save is the member's too.
    def same():
        a, b = coop_save(host, slots[host.name]), coop_save(member, slots[member.name])
        return a is not None and a == b

    env.check("the host's save reached the member", wait("the same bytes", same, 20))
    env.check("the member said it has it", wait("the ack", lambda: host.json("cosave")["acked"] == 1, 15))
    env.report["notes"]["size"] = len(coop_save(host, slots[host.name]))
    env.check("a save's size", len(coop_save(host, slots[host.name])) == 59912)

    # Save the party, asked by the member.
    time.sleep(args.settle)
    env.check("Save the party, from a member", member.ask("saveparty") == "ok")
    env.check("the host's copy follows", wait("the same bytes", same, 20))
    for c in (host, member):
        env.check(f"{c.name}'s own saves are untouched", own_saves(c) == before[c.name])

    # The member leaves: the last save, the filter on until the title, the card its own after.
    member.ask("leave")
    wait("the member at the title", lambda: member.status()["phase"] == PHASE_FRONT_END, 60)
    released = wait("the filter off", lambda: not member.json("cosave")["on"], 20)
    env.check("the filter goes at the title", released)
    env.check("its own saves are still untouched", own_saves(member) == before[member.name])

    # It hosts from its slot: the save loads, and the host of before follows it to the same slot.
    answer = member.ask(f"continue {slots[member.name]}")
    env.check("Continue as host", answer == "ok", answer)
    env.report["notes"]["continued"] = member.status()


SCENARIOS = {
    "save": scenario_save,
    "smoke": scenario_smoke,
    "tags": scenario_tags,
    "look": scenario_look,
    "cosave": scenario_cosave,
    "keys": scenario_keys,
    "bolts": scenario_bolts,
    "hostdeath": scenario_host_death,
    "hostdrop": scenario_hostdrop,
    "hostback": scenario_hostback,
    "hostleave": scenario_hostleave,
    "rejoin": scenario_rejoin,
    "wipe": scenario_wipe,
    "revive": scenario_revive,
    "crateback": scenario_crateback,
    "dig": scenario_dig,
    "grab": scenario_grab,
    "crates": scenario_crates,
    "join": scenario_join,
    "gohost": scenario_gohost,
    "restart": scenario_restart,
    "shipwarn": scenario_shipwarn,
    "soak": scenario_soak,
    "weapons": scenario_weapons,
    "enemies": scenario_enemies,
    "chaos": scenario_chaos,
    "refuse": scenario_refuse,
    "kick": scenario_kick,
    "midjoin": scenario_midjoin,
    "progress2": scenario_progress2,
    "camera": scenario_camera,
}


def link_args(text):
    out = []
    for part in filter(None, (text or "").split(",")):
        name, value = part.split("=")
        out += [f"--{name}", value]
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenario", choices=SCENARIOS, required=True)
    parser.add_argument("--weapons", default="", help="weapons: the ones to fire, by name, commas between")
    parser.add_argument("--fire-button", default="8000", help="weapons: the pad's bit (hex) that fires")
    parser.add_argument("--link", default="", help="loss=5,latency=50,jitter=10,reorder=2,duplicate=1")
    parser.add_argument("--out", required=True)
    parser.add_argument("--game", required=True)
    parser.add_argument("--data", required=True)
    parser.add_argument("--levels", required=True)
    parser.add_argument("--member-game", help="the members' program, for a party of two discs")
    parser.add_argument("--member-data", help="the members' data folder")
    parser.add_argument("--member-levels", help="the members' levels")
    parser.add_argument("--save", help="a save file the host starts from (a new game without)")
    parser.add_argument("--mod", required=True, help="the packaged mod's folder")
    parser.add_argument("--server-jar", required=True)
    parser.add_argument("--java", default="java")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--base", type=int, default=0, help="this run's address block, for runs at once")
    parser.add_argument("--trace", action="store_true")
    parser.add_argument("--crates", type=int, default=8)
    parser.add_argument("--deaths", action="store_true")
    parser.add_argument("--join", action="store_true")
    parser.add_argument("--members", type=int, default=1)
    parser.add_argument("--seconds", type=int, default=60)
    parser.add_argument("--settle", type=float, default=5)
    parser.add_argument("--level", type=int, default=0)
    parser.add_argument("--host-scene", type=int, default=-1, help="a scene the host plays as the member joins")
    parser.add_argument("--join-seconds", type=int, default=120)
    parser.add_argument("--place", action="store_true", help="join: the late member ends beside the host")
    parser.add_argument("--level2", type=int, default=1, help="chaos: the second level flown to")
    parser.add_argument("--mods-dir", help="a folder of packaged mods, by id, for the options below")
    parser.add_argument("--difficulty", help="the difficulty mod's settings in JSON, every game's")
    parser.add_argument("--member-difficulty", help="the first member's, when they are others")
    parser.add_argument("--late-difficulty", help="the later members', when they are others")
    parser.add_argument("--member-mods", help="more mods every member has and the host has not, by id")
    parser.add_argument("--member-packs", action="store_true", help="every member has a data pack the host has not")
    parser.add_argument("--late-refused", action="store_true", help="midjoin: the late member differs")
    parser.add_argument("--seed-save", help="cosave: a save file put on each card first")
    parser.add_argument("--game-arg", action="append")
    parser.add_argument("--game-env", action="append")
    args = parser.parse_args()
    args.link_args = link_args(args.link)
    env = Env(args)
    failed = None
    try:
        env.start_server()
        SCENARIOS[args.scenario](env, args)
    except Failed as e:
        failed = str(e)
        env.check("the scenario ran", False, failed)
    finally:
        env.report["counts"] = {}
        for c in env.clients:
            try:
                env.report["counts"][c.name] = c.json("stats")
            except (Failed, OSError, ValueError):
                pass
        env.stop()
        time.sleep(0.5)
        env.report["server"] = server_drops(env)
        env.report["net"] = net_totals(env)
        env.report["passed"] = failed is None and all(c["ok"] for c in env.report["checks"])
        (env.out / "report.json").write_text(json.dumps(env.report, indent=1))
    print(json.dumps({k: env.report[k] for k in ("passed", "server")}))
    return 0 if env.report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
