# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""Trust labels, the scan of every native version, build provenance and the weekly picks.

Used by tools/build_index.py (hourly) and tools/submit_bot.py. Stdlib only.

- Labels: openrac (the mod's folder is in this repository), verified (its id or its GitHub owner
  is in trust/verified.json), trusted (its owner is in trust/trusted.json), unchecked (none).
- Scan: tools/binary_scan.py on each new native version. Some findings only label the version
  (Network, ...); the dangerous ones hold it back until a maintainer clears them
  (index/cleared.json) or revokes it.
- Provenance: GitHub's build attestation of an archive says which workflow built it, from which
  tag. Built by CI or Uploaded by hand is a label only.
- Picks: three mods per game, kept for a week (Monday 00:00 UTC).
"""
from __future__ import annotations

import base64
import datetime as dt
import json
import re
from pathlib import Path

import binary_scan

LABELS = ("openrac", "verified", "trusted", "unchecked")
CHECKED = ("openrac", "verified", "trusted")

# binary_scan's categories that only label a version, by the short name players see.
SCAN_LABELS = {
    "network": "Network",
    "starts programs": "Starts programs",
    "loads code at run time": "Loads code",
    "makes memory executable": "Executable memory",
    "reads the clipboard": "Clipboard",
}
# The ones that hold a version for a person.
DANGER = ("reads saved secrets", "reaches other processes", "watches the keyboard or screen",
          "changes system settings")

POLICY_DEFAULT = {
    "review": "scan",          # "scan": no per-version review; "approve": index/approved.json as before
    "community": False,        # new Not checked mods go to community.json (only once launchers read it)
    "selfMerge": True,         # the bot merges a trusted author's listing PR when every check passes
    "slimIndex": False,        # leave readme and changelogs out of index.json (details-<id>.json only)
    "picks": True,             # the weekly picks in index.json
    "provenanceRequired": False,  # a native version must be built by CI (after the grace period)
}


class ListError(Exception):
    pass


# ------------------------------------------------------------------------------------------------
# Lists in trust/ (who is checked) and index/ (cleared findings, policy)
# ------------------------------------------------------------------------------------------------

LOGIN_RE = re.compile(r"^[A-Za-z0-9](?:[A-Za-z0-9-]{0,38})$")
ID_RE = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")


def _read(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        raise ListError(f"{path.parent.name}/{path.name}: {e}") from None


def _people(value, where: str) -> dict:
    """{login: {since, note?, id?}}: GitHub accounts. `id` (numeric) keeps a renamed account on."""
    if not isinstance(value, dict):
        raise ListError(f"{where}: must be {{\"<login>\": {{\"since\": \"YYYY-MM-DD\"}}}}")
    for login, info in value.items():
        if not LOGIN_RE.match(login) or not isinstance(info, dict) or set(info) - {"since", "note", "id"}:
            raise ListError(f"{where}: {login!r}: a GitHub login with {{since, note, id}}")
        if "id" in info and (not isinstance(info["id"], int) or isinstance(info["id"], bool) or info["id"] <= 0):
            raise ListError(f"{where}: {login}: id is GitHub's numeric account id")
    return value


def load_lists(root: Path) -> dict:
    """trust/verified.json ({mods: {id: {...}}, owners: {login: {...}}}), trust/trusted.json
    ({owners: {login: {...}}}), index/cleared.json ({id: [finding]}) and index/policy.json."""
    idx = Path(root) / "index"
    lists = Path(root) / "trust"
    out = {"verified_mods": {}, "verified_owners": {}, "trusted_owners": {}, "cleared": {},
           "policy": dict(POLICY_DEFAULT), "pins": []}
    if (lists / "verified.json").is_file():
        d = _read(lists / "verified.json")
        if not isinstance(d, dict) or set(d) - {"mods", "owners"}:
            raise ListError("trust/verified.json: must be {\"mods\": {...}, \"owners\": {...}}")
        mods = d.get("mods", {})
        if not isinstance(mods, dict) or not all(ID_RE.match(k) and isinstance(v, dict) for k, v in mods.items()):
            raise ListError("trust/verified.json: mods is {\"<id>\": {\"since\": ..., \"note\": ...}}")
        out["verified_mods"] = mods
        out["verified_owners"] = _people(d.get("owners", {}), "trust/verified.json: owners")
    if (lists / "trusted.json").is_file():
        d = _read(lists / "trusted.json")
        if not isinstance(d, dict) or set(d) - {"owners"}:
            raise ListError("trust/trusted.json: must be {\"owners\": {...}}")
        out["trusted_owners"] = _people(d.get("owners", {}), "trust/trusted.json: owners")
    if (idx / "cleared.json").is_file():
        d = _read(idx / "cleared.json")
        if not isinstance(d, dict) or not all(
                ID_RE.match(k) and isinstance(v, list) and all(isinstance(x, str) for x in v) for k, v in d.items()):
            raise ListError("index/cleared.json: must be {\"<id>\": [\"<finding>\"]}")
        out["cleared"] = d
    if (idx / "policy.json").is_file():
        d = _read(idx / "policy.json")
        if not isinstance(d, dict) or set(d) - set(POLICY_DEFAULT):
            raise ListError(f"index/policy.json: keys are {', '.join(POLICY_DEFAULT)}")
        for k, v in d.items():
            if type(v) is not type(POLICY_DEFAULT[k]) or (k == "review" and v not in ("scan", "approve")):
                raise ListError(f"index/policy.json: {k} has the wrong kind of value")
        out["policy"].update(d)
    if (idx / "picks-pin.json").is_file():
        d = _read(idx / "picks-pin.json")
        ok = isinstance(d, list) and all(isinstance(p, dict) and set(p) == {"id", "until"} and ID_RE.match(str(p["id"]))
                                         and re.match(r"^\d{4}-\d\d-\d\d$", str(p["until"])) for p in d)
        if not ok:
            raise ListError("index/picks-pin.json: must be [{\"id\": \"<mod>\", \"until\": \"YYYY-MM-DD\"}]")
        out["pins"] = d
    return out


def _has(people: dict, owner: dict | None) -> bool:
    if not owner:
        return False
    login, num = str(owner.get("login", "")).lower(), owner.get("id")
    for name, info in people.items():
        if "id" in info and num is not None:
            if info["id"] == num:
                return True
        elif name.lower() == login:
            return True
    return False


def label(mid: str, local: bool, owner: dict | None, lists: dict) -> str:
    """The first label that applies. `owner` is the repo's GitHub owner {login, id}."""
    if local:
        return "openrac"
    if mid in lists["verified_mods"] or _has(lists["verified_owners"], owner):
        return "verified"
    if _has(lists["trusted_owners"], owner):
        return "trusted"
    return "unchecked"


def reserved_name(mid: str, name: str) -> bool:
    """Names and ids with "openrac" belong to the mods of this repository."""
    return "openrac" in mid.lower() or "openrac" in re.sub(r"[^a-z]", "", str(name).lower())


# ------------------------------------------------------------------------------------------------
# Scan
# ------------------------------------------------------------------------------------------------

def findings(scan: dict) -> tuple[list[str], list[str]]:
    """(labels, dangerous findings) of a merged binary_scan result."""
    caps = binary_scan.capabilities(scan)
    labels = [SCAN_LABELS[c] for c in SCAN_LABELS if c in caps]
    danger = [c for c in DANGER if c in caps]
    danger += [f"mentions {t}" for t in sorted(scan.get("telling", ()))]
    if scan.get("problems"):
        danger.append("a file cannot be read")
    return labels, danger


def not_cleared(danger: list[str], mid: str, lists: dict) -> list[str]:
    ok = set(lists["cleared"].get(mid, []))
    return [d for d in danger if d not in ok]


# ------------------------------------------------------------------------------------------------
# Provenance
# ------------------------------------------------------------------------------------------------

def built_by_ci(attestations, repo_url: str, tag: str) -> bool:
    """Whether one of GitHub's attestations of an archive says a workflow of `repo_url` built it
    from `tag`. GitHub serves them for the repo, so only the statement inside is read."""
    want_repo = repo_url.lower().rstrip("/")
    for a in (attestations or {}).get("attestations", []) if isinstance(attestations, dict) else []:
        try:
            payload = a["bundle"]["dsseEnvelope"]["payload"]
            statement = json.loads(base64.b64decode(payload))
            wf = statement["predicate"]["buildDefinition"]["externalParameters"]["workflow"]
        except (KeyError, TypeError, ValueError):
            continue
        if str(wf.get("repository", "")).lower().rstrip("/") == want_repo and wf.get("ref") == f"refs/tags/{tag}":
            return True
    return False


# ------------------------------------------------------------------------------------------------
# Weekly picks
# ------------------------------------------------------------------------------------------------

PICKS = 3
PICK_GAP_WEEKS = 4
PICK_UPDATED_DAYS = 90
PICK_BONUS_DAYS = 14


def week_start(now: dt.datetime) -> dt.date:
    d = now.date()
    return d - dt.timedelta(days=d.weekday())


def _days_since(ts: str, now: dt.datetime) -> float:
    try:
        t = dt.datetime.fromisoformat(ts.replace("Z", "+00:00"))
    except (ValueError, AttributeError):
        return 1e9
    return (now - t).total_seconds() / 86400


def _api_ok(api, port_api) -> bool:
    if api is None:
        return True
    try:
        a, p = [tuple(int(x) for x in s.split(".")) for s in (api, port_api)]
    except (ValueError, AttributeError):
        return False
    return a[0] == p[0] and a[1] <= p[1]


def eligible(mod: dict, now: dt.datetime, ports: dict, revoked: list) -> bool:
    if mod.get("trust") not in CHECKED or mod.get("held") or mod["type"] == "pack":
        return False
    md = mod.get("media") or {}
    if not (md.get("banner") or md.get("screenshots") or mod.get("pictures")) or not mod.get("description"):
        return False
    if _days_since(mod.get("updated", ""), now) > PICK_UPDATED_DAYS:
        return False
    if any(r["id"] == mod["id"] for r in revoked):
        return False
    stable = (ports or {}).get("stable", {}).get("api")
    return stable is None or any(_api_ok(v.get("api"), stable) for v in mod["versions"])


def score(mod: dict, days: dict, now: dt.datetime) -> tuple[float, int]:
    """Installs in the last 7 days over the mod's own average week before (so a growing mod beats a
    merely big one), plus a bonus for a recent update. Total downloads break ties."""
    dates = sorted(days)
    today = now.date()

    def total(daysago: int) -> int | None:
        want = (today - dt.timedelta(days=daysago)).isoformat()
        older = [d for d in dates if d <= want]
        return int(days[older[-1]].get(mod["id"], 0)) if older else None

    t0, t7 = total(0), total(7)
    week = max(0, (t0 or 0) - (t7 if t7 is not None else (t0 or 0)))
    before = 0.0
    for weeks in (3, 2, 1):  # the longest window stats.json has before this week
        old = total(7 + 7 * weeks)
        if t7 is not None and old is not None:
            before = max(0, t7 - old) / weeks
            break
    s = week / (before + 5.0)
    if _days_since(mod.get("updated", ""), now) <= PICK_BONUS_DAYS:
        s += 0.5
    return s, int(mod.get("downloads", 0))


def picks(mods: list[dict], days: dict, now: dt.datetime, ports: dict, revoked: list, history: dict | None,
          pins: list) -> dict:
    """{"week": "YYYY-MM-DD", "games": {game: [id, ...]}, "history": {week: {game: [ids]}}}. The week's
    picks are kept as they were chosen; a pick that is no longer eligible is replaced."""
    week = week_start(now).isoformat()
    hist = dict((history or {}).get("history", {})) if isinstance(history, dict) else {}
    by_id = {m["id"]: m for m in mods}
    ok = {m["id"] for m in mods if eligible(m, now, ports, revoked)}
    recent = {i for w, g in hist.items() if w < week and w >= (week_start(now) - dt.timedelta(
        weeks=PICK_GAP_WEEKS)).isoformat() for ids in g.values() for i in ids}
    pinned = [p["id"] for p in pins if p["until"] >= now.date().isoformat() and p["id"] in by_id]
    games = sorted({g for m in mods for g in m.get("games", {})})
    kept = hist.get(week, {})
    out = {}
    for game in games:
        chosen = [i for i in kept.get(game, []) if i in ok and game in by_id[i]["games"]]
        pool = [m for m in mods if m["id"] in ok and game in m["games"] and m["id"] not in recent]
        pool.sort(key=lambda m: (score(m, days, now), m["id"]), reverse=True)
        for i in pinned:
            if i not in chosen and game in by_id[i]["games"]:
                chosen.insert(0, i)
        for m in pool:
            if len(chosen) >= PICKS:
                break
            if m["id"] in chosen:
                continue
            authors = {by_id[i]["author"].lower() for i in chosen}
            if m["author"].lower() in authors:
                continue
            if m["trust"] == "openrac" and any(by_id[i]["trust"] == "openrac" for i in chosen):
                continue
            chosen.append(m["id"])
        out[game] = chosen[:PICKS]
    hist[week] = out
    keep_from = (week_start(now) - dt.timedelta(weeks=PICK_GAP_WEEKS + 1)).isoformat()
    hist = {w: g for w, g in sorted(hist.items()) if w >= keep_from}
    return {"week": week, "games": out, "history": hist}
