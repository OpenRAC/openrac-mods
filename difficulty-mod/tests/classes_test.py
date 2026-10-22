#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The difficulty mod's classes (classes.tsv, src/classes.h) against the SDK's behaviour catalogue.

    classes_test.py CATALOGUE      (the SDK's catalogue/rac1/classes.json)

- every class of classes.tsv is in the catalogue, once, with a known role and, unless an enemy,
  a reason;
- every class the mod times (enemies, roamers, turrets) has an update the catalogue makes a hook
  point (void(u32)), and src/classes.h lists that hook point;
- coverage: every catalogue class whose name says it fights (a bot, a trooper, a turret, a mine,
  a boss, a thief, a ship, ...) is in classes.tsv, so none is copied, sped up or left out by
  accident, and a new class in the catalogue fails here until it is classified;
- src/classes.h gives each class the role classes.tsv gives it.
"""
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
HOSTILE = re.compile(r"bot|blarg|trooper|turret|mine|commando|amoeboid|shark|gunner|boss|tank|seeker|bomber|"
                     r"spider|sentry|toad|crab|snapper|screamer|puffoid|psytcopus|chomper|beast|queen|thief|"
                     r"commander|extermi|dog|interceptor|fighter|gun|pterodactyl|enemy|saw|axe|drone|squawker|"
                     r"mouse|frog|miner|deserter|gator|plasma|ship|helicopter|bombthrower|paratrooper")
# The player's own things and the game's projectiles and pickups, which the names above also match.
NOT_A_FOE = re.compile(r"rocket$|missile|glove|ammo|crate|infobot|^gold_|^visibomb|^bomb$|^ryno|_ship_?\d*$|"
                       r"^ship$|ship_rear|ship_front|ship_back|^shuttle")
ROLES = {"enemy": "DM_ENEMY", "roamer": "DM_ROAMER", "turret": "DM_TURRET", "boss": "DM_BOSS",
         "excluded": "DM_EXCLUDED"}


def main() -> int:
    catalogue = {c["id"]: c for c in json.loads(Path(sys.argv[1]).read_text())["classes"]}
    rows = {}
    errors = []
    for line in (HERE / "classes.tsv").read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        cid, role, name, reason = line.split("\t")
        cid = int(cid)
        if cid in rows:
            errors.append(f"class {cid} twice")
        rows[cid] = (role, name, reason)
        c = catalogue.get(cid)
        if c is None:
            errors.append(f"class {cid} ({name}) is not in the catalogue")
            continue
        if role not in ROLES:
            errors.append(f"class {cid}: role {role!r}")
        if role != "enemy" and reason == "-":
            errors.append(f"class {cid} ({name}): {role} without a reason")
        if c["name"] and c["name"] != name:
            errors.append(f"class {cid}: the catalogue calls it {c['name']}, classes.tsv {name}")
        if role in ("enemy", "roamer", "turret") and not any(
                u.get("hook") and u.get("signature") == "void(u32)" for u in c["updates"]):
            errors.append(f"class {cid} ({name}): timed, but its update is no hook point")
    for cid, c in sorted(catalogue.items()):
        n = c["name"] or ""
        if HOSTILE.search(n) and not NOT_A_FOE.search(n) and cid not in rows:
            errors.append(f"class {cid} ({n}) is not classified in classes.tsv")
    header = (HERE / "src" / "classes.h").read_text()
    entries = dict((int(i), r) for i, r in re.findall(r"\{(\d+), (DM_[A-Z]+), ", header))
    for cid, (role, name, _reason) in rows.items():
        if entries.get(cid) != ROLES[role]:
            errors.append(f"class {cid}: src/classes.h is stale (python3 tools/gen_classes.py)")
    if set(entries) != set(rows):
        errors.append("src/classes.h and classes.tsv list other classes (python3 tools/gen_classes.py)")
    hooks = header.split("k_dm_update_hooks")[1]
    timed = sum(1 for r in rows.values() if r[0] in ("enemy", "roamer", "turret"))
    if not hooks.count('"rac1.moby.') or hooks.count('"rac1.moby.') > timed:
        errors.append("src/classes.h's update hook points do not follow classes.tsv")
    for e in errors:
        print(e, file=sys.stderr)
    counts = {r: sum(1 for x in rows.values() if x[0] == r) for r in ROLES}
    print(f"classes_test: {len(rows)} classes ({counts}); {'ok' if not errors else 'FAILED'}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
