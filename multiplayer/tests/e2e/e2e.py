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
import os
import re
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
PHASE_FRONT_END, PHASE_LOADING, PHASE_PLAYING, PHASE_SCENE, PHASE_MENU = 1, 2, 3, 4, 5
IN_LEVEL = (PHASE_PLAYING, PHASE_SCENE, PHASE_MENU)
MOBY_DRIVEN, MOBY_DEAD, MOBY_HIDDEN = 0x2, 0x10, 0x40
CRATES = (500, 501, 502, 505, 511)
BOLTS = (13, 14, 15, 16)


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
        self.address = f"127.{env.base + 1}.1.{index + 1}"
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
        (plugins / "plugins.json").write_text(
            json.dumps(
                {
                    "format": 1,
                    "mods": [
                        {
                            "id": "multiplayer",
                            "enabled": True,
                            "settings": {
                                "server": f"{self.address}:7770",
                                "name": self.name,
                                "verbose_log": self.env.trace,
                            },
                        }
                    ],
                }
            )
        )
        (self.dir / "settings.ini").write_text("skip_cutscenes = 1\nvsync = off\n")
        self.proxy = subprocess.Popen(
            [
                sys.executable,
                str(HERE / "netimp.py"),
                "--listen", f"{self.address}:7770",
                "--server", f"{self.env.server_address}:7770",
                "--seed", str(self.env.seed + self.index),
                "--stats", str(self.dir / "net.json"),
            ]
            + self.link,
            stdout=open(self.dir / "proxy.log", "w"),
            stderr=subprocess.STDOUT,
        )
        env = dict(os.environ)
        env.update(
            {
                "SDL_AUDIODRIVER": "dummy",
                "OPENRAC_HIDDEN": "1",
                "OPENRAC_MP_BOT": str(self.port),
                "OPENRAC_PLAYER_NAME": self.name,
            }
        )
        env.update(self.env.game_env)
        self.proc = subprocess.Popen(
            [
                str(self.env.game),
                "--data", str(self.env.data),
                "--levels", str(self.env.levels),
                "--plugins", str(plugins / "plugins.json"),
                "--cards", str(self.dir / "cards"),
                "--settings", str(self.dir / "settings.ini"),
                "--window",
            ]
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
        self.mod = Path(args.mod).resolve()
        self.server_jar = Path(args.server_jar).resolve()
        self.java = args.java
        self.base = args.base  # runs at once each take an address block of their own
        self.server_address = f"127.{args.base + 1}.0.2"
        self.seed = args.seed
        self.trace = args.trace
        self.game_args = args.game_arg or []
        self.game_env = dict(e.split("=", 1) for e in (args.game_env or []))
        self.server = None
        self.clients = []
        self.report = {"scenario": args.scenario, "link": args.link, "checks": [], "notes": {}}

    def start_server(self):
        env = dict(os.environ)
        env.update(
            {
                "HOST": self.server_address,
                "PORT": "7770",
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
                socket.create_connection((self.server_address, 7770), timeout=1).close()
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
    host.ask(f"connect {host.address} 7770")
    wait("the host greeted", lambda: host.status()["greeted"], 20)
    host.ask("create")
    code = wait("the party", lambda: host.status()["code"], 15)
    for m in members:
        m.ask(f"connect {m.address} 7770")
        wait(f"{m.name} greeted", lambda: m.status()["greeted"], 20)
        m.ask(f"join {code}")
        wait(f"{m.name} in the party", lambda: m.status()["code"] == code, 15)
        m.ask("ready")
    wait("all ready", lambda: host.status()["all_ready"], 15)
    return code


def skip_scenes(env, host, others, seconds):
    """Every game in control in the host's level, the host's Start pressed through what plays. A
    member stuck past the time is a failure the caller sees."""
    limit = time.monotonic() + seconds
    while time.monotonic() < limit:
        s = host.status()
        if s["phase"] == PHASE_SCENE or s["phase"] == PHASE_LOADING:
            host.ask("press 8 6")
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
    c.ask(f"connect {c.address} 7770")
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


def scenario_smoke(env, args):
    host = env.client("Host1", args.link_args)
    reach_title(host)
    env.check("the game reaches the title", True)


def start_two(env, args, level_note=True):
    host = env.client("Host1", args.link_args)
    member = env.client("Member1", args.link_args)
    reach_title(host)
    reach_title(member)
    party(env, host, [member])
    answer = host.ask("start")
    if answer != "ok":
        raise Failed("the host's Start: " + answer)
    ok = skip_scenes(env, host, [member], 150)
    env.check("both games in control in level 0", ok, json.dumps([host.status(), member.status()]))
    return host, member


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
        answer = who.ask(f"hit {crate[1]} 10000")
        if answer != "ok":
            raise Failed(f"{who.name} could not hit crate {crate[1]}: {answer}")
        time.sleep(0.4)
        made = 0
        limit = time.monotonic() + 6
        while time.monotonic() < limit:
            made = max(made, len(spawned_bolts(host)))
            who.ask(f"teleport {crate[5] + 0.5} {crate[6]} {crate[7] + 1.0}")
            time.sleep(0.3)
            if made and not spawned_bolts(host):
                break
        time.sleep(1.0)
        after = {c.name: c.status()["bolts"] for c in (host, member)}
        delta = {n: after[n] - before[n] for n in after}
        gained[who.name] += delta[who.name]

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
    env.check("no bolt left of the old world", not any(left.values()), json.dumps(left))
    compare_poses(env, host, [member], "after the host's death")


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
        time.sleep(2)
        compare_worlds(env, [host, third], "the late member's world")
    env.report["notes"]["final"] = {c.name: c.status() for c in env.clients}


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


SCENARIOS = {
    "smoke": scenario_smoke,
    "keys": scenario_keys,
    "bolts": scenario_bolts,
    "hostdeath": scenario_host_death,
    "crates": scenario_crates,
    "join": scenario_join,
    "soak": scenario_soak,
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
    parser.add_argument("--link", default="", help="loss=5,latency=50,jitter=10,reorder=2,duplicate=1")
    parser.add_argument("--out", required=True)
    parser.add_argument("--game", required=True)
    parser.add_argument("--data", required=True)
    parser.add_argument("--levels", required=True)
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
