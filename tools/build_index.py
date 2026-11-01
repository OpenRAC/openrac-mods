#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""The mod index: every mod of this repository and every listed external mod, in one index.json.

    python3 tools/build_index.py [--out DIR] [--check] [--offline FIXTURE_DIR] [--check-entry FILE]
    python3 tools/build_index.py --validate index.json

Lists the in-repo mods (each top-level folder with an openrac-mod.json, plus its optional
listing.json) and the external entries index/<id>.json, reads their GitHub releases (tags
<id>-v<version> or v<version>), and writes DIR/index.json (format 1, INDEX_FORMAT), DIR/stats.json
(daily download totals) and DIR/media-src/ (the pictures for tools/media.py).

--check only validates (PR CI). A broken release skips only that version (the last good one stays);
a broken entry fails. --offline reads recorded responses (FIXTURE_DIR/routes.json) instead of the
network, for tests. GITHUB_TOKEN, if set, is sent to api.github.com only. MOD_INDEXES (comma-separated
index URLs) lists the other indexes ids must not clash with. Stdlib only.
"""
from __future__ import annotations

import argparse
import datetime as dt
import email.utils
import hashlib
import json
import os
import random
import re
import struct
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import media  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
SOURCE = "https://github.com/OpenRAC/openrac-mods"
INDEX_TAG = "index"
MEDIA_BASE = f"{SOURCE}/releases/download/{INDEX_TAG}/"
INDEX_URL = MEDIA_BASE + "index.json"
PORT_REPO = "OpenRAC/OpenRAC"
PORT_HEADER = "sdk/include/openrac/openrac_mod.h"
DEV_BRANCH = "develop"
STABLE_MANIFEST = f"https://github.com/{PORT_REPO}/releases/latest/download/latest.json"
DEV_MANIFEST = f"https://github.com/{PORT_REPO}/releases/download/launcher-dev-{DEV_BRANCH}/latest.json"

FORMAT = 1
EXPIRES = dt.timedelta(days=7)
STATS_DAYS = 30
TAGS = ["gameplay", "controls", "cosmetic", "qol", "speedrun", "randomizer", "multiplayer", "accessibility",
        "camera", "audio", "ui", "tools"]
IMPACT = ["cosmetic", "gameplay", "speedrun-safe", "touches-saves", "online"]
LINK_KEYS = ["bugs", "discord", "docs"]
TYPES = ["script", "native", "data", "pack"]
PLATFORMS = ["linux-x86_64", "linux-aarch64", "windows-x86_64", "windows-arm64", "macos-arm64", "macos-x86_64",
             "android-arm64"]
ID_RE = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")
SEMVER_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z]+(?:\.[0-9A-Za-z]+)*))?$")
SEMVER = r"\d+\.\d+\.\d+(?:-[0-9A-Za-z]+(?:\.[0-9A-Za-z]+)*)?"
API_RE = re.compile(r"^\d+\.\d+$")
CONSTRAINT_RE = re.compile(r"^(>=|=)" + SEMVER + "$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")
REPO_RE = re.compile(r"^https://github\.com/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)$")

MAX_ID = 64
MAX_DESCRIPTION = 300
MAX_TEXT = 64 * 1024  # readme, changelog
MAX_FILES = 500
MAX_SCREENSHOTS = 12
MAX_VIDEOS = 4
MAX_CAPTION = 200
MAX_ALIASES = 10
MAX_SUMS = 256 * 1024
MAX_CD = 2 * 1024 * 1024
COOL_DOWN = 24 * 3600  # seconds a new external native version waits
INDEX_FILES = ("schema.json", "revoked.json", "approved.json", "discussions.json")
LISTING_KEYS = {"id", "description", "tags", "impact", "links", "recommends", "suggests", "aliases", "featured",
                "media", "aiAssisted"}
ENTRY_KEYS = LISTING_KEYS | {"repo", "path", "type", "name", "author", "license", "homepage", "games", "readme",
                             "versions"}


class EntryError(Exception):
    """An entry that is broken: fails --check and the build."""


class ReleaseError(Exception):
    """One release that is broken: only its version is skipped."""


class NetError(Exception):
    """The network failed after the retries. status: the HTTP status, when an answer came."""

    def __init__(self, message: str = "", status: int | None = None):
        super().__init__(message)
        self.status = status


# ------------------------------------------------------------------------------------------------
# Versions
# ------------------------------------------------------------------------------------------------

def semver_key(v: str):
    """Sort key: a pre-release sorts before its release (semver 2.0.0 precedence)."""
    m = SEMVER_RE.match(v)
    if not m:
        return (-1, -1, -1, 0, ())
    pre = m.group(4)
    ids = ()
    if pre:
        ids = tuple((0, int(p), "") if p.isdigit() else (1, 0, p) for p in pre.split("."))
    return (int(m.group(1)), int(m.group(2)), int(m.group(3)), 0 if pre else 1, ids)


def iso(t: dt.datetime) -> str:
    return t.astimezone(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def norm_time(s: str | None) -> str:
    if not s:
        return ""
    try:
        return iso(dt.datetime.fromisoformat(s.replace("Z", "+00:00")))
    except ValueError:
        return ""


# ------------------------------------------------------------------------------------------------
# HTTP, with retries
# ------------------------------------------------------------------------------------------------

class Response:
    def __init__(self, status: int, headers, body: bytes):
        self.status, self.headers, self.body = status, headers or {}, body

    def header(self, name: str) -> str | None:
        for k, v in dict(self.headers).items():
            if k.lower() == name.lower():
                return v
        return None

    def json(self):
        return json.loads(self.body)


class Http:
    """GET with full-jitter backoff; honours Retry-After and X-RateLimit-Reset. The token goes to
    api.github.com only, and never across a redirect."""

    def __init__(self, token: str | None = None, sleep=time.sleep, rand=random.random, attempts: int = 5,
                 clock=time.time):
        self.token, self.sleep, self.rand, self.attempts, self.clock = token, sleep, rand, attempts, clock
        self.calls = 0

    def _open(self, url: str, headers: dict, limit: int) -> Response:
        req = urllib.request.Request(url, headers={"User-Agent": "openrac-mod-index", **headers})
        if self.token and urllib.parse.urlsplit(url).hostname == "api.github.com":
            req.add_unredirected_header("Authorization", f"Bearer {self.token}")
        with urllib.request.urlopen(req, timeout=30) as r:
            body = r.read(limit + 1)
            if len(body) > limit:
                raise NetError(f"{url}: over {limit} bytes")
            return Response(r.status, r.headers, body)

    def wait_for(self, status: int, headers, attempt: int) -> float | None:
        """How long to wait before trying again, or None to give up now."""
        r = Response(status, headers, b"")
        retry_after = r.header("Retry-After")
        if retry_after:
            try:
                return float(retry_after)
            except ValueError:
                when = email.utils.parsedate_to_datetime(retry_after)
                return max(0.0, when.timestamp() - self.clock())
        if r.header("X-RateLimit-Remaining") == "0" and r.header("X-RateLimit-Reset"):
            return max(0.0, float(r.header("X-RateLimit-Reset")) - self.clock()) + 1
        if status == 429 or status >= 500:
            return self.rand() * min(60.0, 2.0 ** attempt)
        return None

    def get(self, url: str, headers: dict | None = None, limit: int = 32 * 1024 * 1024) -> Response:
        headers = headers or {}
        for attempt in range(self.attempts):
            self.calls += 1
            try:
                return self._open(url, headers, limit)
            except urllib.error.HTTPError as e:
                wait = self.wait_for(e.code, e.headers, attempt) if e.code in (403, 429) or e.code >= 500 else None
                if wait is None:
                    return Response(e.code, e.headers, b"")
                if wait > 900:
                    raise NetError(f"{url}: rate limited for {int(wait)} s") from None
            except (urllib.error.URLError, TimeoutError, ConnectionError) as e:
                wait = self.rand() * min(60.0, 2.0 ** attempt)
                if attempt == self.attempts - 1:
                    raise NetError(f"{url}: {e}") from None
            if attempt < self.attempts - 1:
                self.sleep(wait)
        raise NetError(f"{url}: gave up after {self.attempts} tries")


class Offline(Http):
    """Recorded responses: FIXTURE_DIR/routes.json maps a URL to a file (or {file, status}).
    An unknown URL is a 404. Range requests get the slice."""

    def __init__(self, fixture: Path):
        super().__init__()
        self.fixture = Path(fixture)
        self.routes = json.loads((self.fixture / "routes.json").read_text())
        self.seen: list[str] = []

    def get(self, url: str, headers: dict | None = None, limit: int = 32 * 1024 * 1024) -> Response:
        self.calls += 1
        self.seen.append(url)
        route = self.routes.get(url)
        if route is None:
            return Response(404, {}, b"")
        if isinstance(route, str):
            route = {"file": route}
        body = (self.fixture / route["file"]).read_bytes() if route.get("file") else b""
        status = route.get("status", 200)
        rng = (headers or {}).get("Range")
        if rng and status == 200:
            m = re.match(r"bytes=(\d+)-(\d+)", rng)
            if m:
                body = body[int(m.group(1)):int(m.group(2)) + 1]
                status = 206
        if len(body) > limit:
            raise NetError(f"{url}: over {limit} bytes")
        return Response(status, route.get("headers", {}), body)


# ------------------------------------------------------------------------------------------------
# GitHub
# ------------------------------------------------------------------------------------------------

class GitHub:
    def __init__(self, http: Http):
        self.http = http
        self._releases: dict[str, list] = {}

    def api(self, path: str):
        r = self.http.get("https://api.github.com/" + path.lstrip("/"),
                          {"Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28"})
        if r.status == 404:
            return None
        if r.status != 200:
            raise NetError(f"GitHub API {path}: HTTP {r.status}", status=r.status)
        return r.json()

    def releases(self, owner: str, repo: str) -> list:
        key = f"{owner}/{repo}"
        if key not in self._releases:
            out = []
            for page in range(1, 11):
                batch = self.api(f"repos/{owner}/{repo}/releases?per_page=100&page={page}") or []
                out += batch
                if len(batch) < 100:
                    break
            self._releases[key] = out
        return self._releases[key]

    def raw(self, owner: str, repo: str, ref: str, path: str, limit: int = MAX_TEXT * 4) -> bytes | None:
        url = f"https://raw.githubusercontent.com/{owner}/{repo}/{urllib.parse.quote(ref, safe='')}/" + \
            urllib.parse.quote(path.lstrip("/"))
        r = self.http.get(url, limit=limit)
        if r.status == 404:
            return None
        if r.status != 200:
            raise NetError(f"{url}: HTTP {r.status}")
        return r.body

    def download(self, url: str, limit: int) -> bytes | None:
        r = self.http.get(url, limit=limit)
        if r.status == 404:
            return None
        if r.status != 200:
            raise NetError(f"{url}: HTTP {r.status}")
        return r.body

    def zip_names(self, url: str, size: int) -> list[str]:
        """The file names in a zip's central directory, read with HTTP Range (no full download)."""
        if size < 22:
            raise ReleaseError(f"{url}: too small to be a zip")
        tail_len = min(size, 65536 + 22)
        start = size - tail_len
        r = self.http.get(url, {"Range": f"bytes={start}-{size - 1}"}, limit=max(tail_len, MAX_CD) + 1)
        if r.status == 200 and len(r.body) == size:
            whole, tail = r.body, r.body[start:]
        elif r.status == 206:
            whole, tail = None, r.body
        else:
            raise ReleaseError(f"{url}: HTTP {r.status} for its zip directory")
        i = tail.rfind(b"PK\x05\x06")
        if i < 0 or i + 22 > len(tail):
            raise ReleaseError(f"{url}: no zip directory")
        count, cd_size, cd_off = struct.unpack("<xxxxxxxxxxHII", tail[i:i + 20])[0:3]
        if cd_off == 0xFFFFFFFF or cd_size > MAX_CD:
            return []  # zip64 or a huge directory: no file list
        if whole is not None:
            cd = whole[cd_off:cd_off + cd_size]
        elif cd_off >= start:
            cd = tail[cd_off - start:cd_off - start + cd_size]
        else:
            r = self.http.get(url, {"Range": f"bytes={cd_off}-{cd_off + cd_size - 1}"}, limit=MAX_CD + 1)
            if r.status != 206:
                raise ReleaseError(f"{url}: HTTP {r.status} for its zip directory")
            cd = r.body
        names, p = [], 0
        while p + 46 <= len(cd) and cd[p:p + 4] == b"PK\x01\x02":
            n, m, k = struct.unpack("<HHH", cd[p + 28:p + 34])
            name = cd[p + 46:p + 46 + n].decode("utf-8", "replace")
            if not name.endswith("/") and name not in names:
                names.append(name)
            p += 46 + n + m + k
        return names[:MAX_FILES]


# ------------------------------------------------------------------------------------------------
# Entries: in-repo folders and index/<id>.json
# ------------------------------------------------------------------------------------------------

def is_https(url) -> bool:
    return isinstance(url, str) and url.startswith("https://") and len(url) <= 500 and " " not in url


def check_id(value, what: str) -> str:
    if not isinstance(value, str) or not ID_RE.match(value) or len(value) > MAX_ID:
        raise EntryError(f"{what}: {value!r} is not a valid id (a-z, 0-9 and single hyphens, at most {MAX_ID})")
    return value


def check_listing(d: dict, where: str, extra_keys=frozenset()) -> None:
    """The listing fields (listing.json or an index entry): the fixed lists and the caps."""
    if not isinstance(d, dict):
        raise EntryError(f"{where}: not a JSON object")
    unknown = set(d) - LISTING_KEYS - set(extra_keys)
    if unknown:
        raise EntryError(f"{where}: unknown field(s) {', '.join(sorted(unknown))}")
    if "description" in d and (not isinstance(d["description"], str) or len(d["description"]) > MAX_DESCRIPTION):
        raise EntryError(f"{where}: description must be text of at most {MAX_DESCRIPTION} characters")
    for key, allowed in (("tags", TAGS), ("impact", IMPACT)):
        v = d.get(key, [])
        if not isinstance(v, list) or any(x not in allowed for x in v) or len(set(v)) != len(v):
            raise EntryError(f"{where}: {key} must be distinct values of: {', '.join(allowed)}")
    for key in ("recommends", "suggests", "aliases"):
        v = d.get(key, [])
        if not isinstance(v, list) or len(v) > MAX_ALIASES * 3:
            raise EntryError(f"{where}: {key} must be a list")
        for x in v:
            if key == "aliases":
                if not isinstance(x, str) or not x or len(x) > 64:
                    raise EntryError(f"{where}: aliases must be short names")
            else:
                check_id(x, f"{where}: {key}")
    if len(d.get("aliases", [])) > MAX_ALIASES:
        raise EntryError(f"{where}: at most {MAX_ALIASES} aliases")
    links = d.get("links", {})
    if not isinstance(links, dict) or set(links) - set(LINK_KEYS) or not all(is_https(u) for u in links.values()):
        raise EntryError(f"{where}: links may be {', '.join(LINK_KEYS)}, each an https URL")
    if not isinstance(d.get("featured", False), bool):
        raise EntryError(f"{where}: featured must be true or false")
    if not isinstance(d.get("aiAssisted", False), bool):
        raise EntryError(f"{where}: aiAssisted must be true or false")
    m = d.get("media", {})
    if not isinstance(m, dict) or set(m) - {"icon", "banner", "screenshots", "videos"}:
        raise EntryError(f"{where}: media may have icon, banner, screenshots, videos")
    for key in ("icon", "banner"):
        if m.get(key) is not None:
            check_src(m[key], f"{where}: media.{key}")
    shots = m.get("screenshots", [])
    if not isinstance(shots, list) or len(shots) > MAX_SCREENSHOTS:
        raise EntryError(f"{where}: at most {MAX_SCREENSHOTS} screenshots")
    for s in shots:
        if not isinstance(s, dict) or set(s) - {"src", "caption"}:
            raise EntryError(f"{where}: a screenshot is {{src, caption}}")
        check_src(s.get("src"), f"{where}: screenshot")
        check_caption(s.get("caption", ""), where)
    videos = m.get("videos", [])
    if not isinstance(videos, list) or len(videos) > MAX_VIDEOS:
        raise EntryError(f"{where}: at most {MAX_VIDEOS} videos")
    for v in videos:
        if not isinstance(v, dict) or set(v) - {"youtube", "caption"}:
            raise EntryError(f"{where}: a video is {{youtube, caption}}")
        try:
            media.youtube_id(v.get("youtube", ""))
        except media.MediaError as e:
            raise EntryError(f"{where}: {e}") from None
        check_caption(v.get("caption", ""), where)


def check_src(src, where: str) -> None:
    if not isinstance(src, str) or not src or src.startswith("/") or "\\" in src or ".." in src.split("/") \
            or "://" in src or len(src) > 200:
        raise EntryError(f"{where}: {src!r} must be a path inside the mod's folder")


def check_caption(c, where: str) -> None:
    if not isinstance(c, str) or len(c) > MAX_CAPTION:
        raise EntryError(f"{where}: a caption is text of at most {MAX_CAPTION} characters")


def read_json(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        raise EntryError(f"{path.name}: {e}") from None


def load_entries(root: Path, extra: Path | None = None) -> tuple[list[dict], list[str]]:
    """Every entry, with the errors of the broken ones."""
    entries, errors = [], []
    folders = sorted(p for p in root.iterdir() if p.is_dir() and (p / "openrac-mod.json").is_file())
    listed = root / "openrac-mods.json"
    if listed.is_file():
        names = set(read_json(listed).get("mods", []))
        folders = [p for p in folders if p.name in names]
    for folder in folders:
        try:
            manifest = read_json(folder / "openrac-mod.json")
            mid = check_id(manifest.get("id"), f"{folder.name}/openrac-mod.json")
            listing = read_json(folder / "listing.json") if (folder / "listing.json").is_file() else {}
            check_listing(listing, f"{folder.name}/listing.json")
            if listing.get("id", mid) != mid:
                raise EntryError(f"{folder.name}/listing.json: id {listing['id']!r} is not the manifest's {mid!r}")
            entries.append({"id": mid, "repo": SOURCE, "path": folder.name, "local": folder, "listing": listing,
                            "manifest": manifest, "where": f"{folder.name}/"})
        except EntryError as e:
            errors.append(str(e))
    files = sorted((root / "index").glob("*.json")) if (root / "index").is_dir() else []
    files = [f for f in files if f.name not in INDEX_FILES]
    if extra is not None:
        files = [f for f in files if f.stem != extra.stem] + [extra]
    for f in files:
        try:
            d = read_json(f)
            where = f"index/{f.name}"
            check_listing(d, where, ENTRY_KEYS - LISTING_KEYS)
            mid = check_id(d.get("id"), where)
            if f.stem != mid:
                raise EntryError(f"{where}: the file is named {f.stem}.json but its id is {mid!r}")
            if not REPO_RE.match(d.get("repo") or ""):
                raise EntryError(f"{where}: repo must be https://github.com/<owner>/<repo>")
            path = d.get("path", "")
            if not isinstance(path, str) or path.startswith("/") or ".." in path.split("/"):
                raise EntryError(f"{where}: path must be a folder inside the repo ('' for its top)")
            if d.get("type", "pack" if "versions" in d else None) == "pack":
                check_pack(d, where)
            listing = {k: v for k, v in d.items() if k in LISTING_KEYS}
            entries.append({"id": mid, "repo": d["repo"], "path": path.strip("/"), "local": None,
                            "listing": listing, "entry": d, "where": where})
        except EntryError as e:
            errors.append(str(e))
    seen: dict[str, str] = {}
    unique = []
    for e in entries:
        if e["id"] in seen:
            errors.append(f"{e['where']}: id {e['id']!r} is already used by {seen[e['id']]}")
        else:
            seen[e["id"]] = e["where"]
            unique.append(e)
    return unique, errors


def check_pack(d: dict, where: str) -> None:
    try:
        _check_pack(d, where)
    except ReleaseError as e:
        raise EntryError(str(e)) from None


def _check_pack(d: dict, where: str) -> None:
    for key in ("name", "author", "description"):
        if not isinstance(d.get(key), str) or not d[key]:
            raise EntryError(f"{where}: a pack needs {key}")
    if len(d["description"]) > MAX_DESCRIPTION:
        raise EntryError(f"{where}: description over {MAX_DESCRIPTION} characters")
    vs = d.get("versions")
    if not isinstance(vs, list) or not vs:
        raise EntryError(f"{where}: a pack needs versions: [{{version, released, requires}}]")
    for v in vs:
        if not isinstance(v, dict) or not SEMVER_RE.match(str(v.get("version"))):
            raise EntryError(f"{where}: a pack version needs a semver version")
        if not norm_time(v.get("released")):
            raise EntryError(f"{where}: pack {v.get('version')} needs released (ISO time)")
        reqs(v.get("requires", []), where)


def reqs(value, where: str) -> list[dict]:
    if not isinstance(value, list):
        raise ReleaseError(f"{where}: requires must be a list")
    out = []
    for r in value:
        if not isinstance(r, dict):
            raise ReleaseError(f"{where}: requires item must be {{id, version}}")
        item = {"id": check_id(r.get("id"), f"{where}: requires")}
        if r.get("version") is not None:
            if not isinstance(r["version"], str) or not CONSTRAINT_RE.match(r["version"]):
                raise ReleaseError(f"{where}: requires {item['id']}: version must be >=X.Y.Z or =X.Y.Z")
            item["version"] = r["version"]
        out.append(item)
    return out


# ------------------------------------------------------------------------------------------------
# One mod from its releases
# ------------------------------------------------------------------------------------------------

def parse_sums(text: bytes) -> dict[str, str]:
    out = {}
    for line in text.decode("utf-8", "replace").splitlines():
        m = re.match(r"^([0-9a-fA-F]{64})\s+\*?(.+?)\s*$", line)
        if m:
            out[m.group(2)] = m.group(1).lower()
    return out


def owner_repo(url: str) -> tuple[str, str]:
    m = REPO_RE.match(url)
    return m.group(1), m.group(2)


def join(path: str, name: str) -> str:
    return f"{path}/{name}" if path else name


def manifest_fields(m: dict, where: str) -> dict:
    """The version fields from an openrac-mod.json (a broken one is a ReleaseError)."""
    if not isinstance(m, dict):
        raise ReleaseError(f"{where}: openrac-mod.json is not an object")
    version = m.get("version")
    if not isinstance(version, str) or not SEMVER_RE.match(version):
        raise ReleaseError(f"{where}: version {version!r} is not MAJOR.MINOR.PATCH[-suffix]")
    kind = "data" if m.get("format") == 1 else m.get("type", "native")
    if kind not in ("native", "script", "data"):
        raise ReleaseError(f"{where}: type {kind!r} is not native or script")
    api = None
    if kind != "data":
        api = m.get("api")
        if not isinstance(api, str) or not API_RE.match(api):
            raise ReleaseError(f"{where}: api {api!r} is not MAJOR.MINOR")
    games = m.get("games", {"rac1": ["pal", "ntsc"]})
    if not isinstance(games, dict) or not games or not all(
            isinstance(k, str) and isinstance(v, list) and all(isinstance(x, str) for x in v) for k, v in games.items()):
        raise ReleaseError(f"{where}: games must be {{game: [discs]}}")
    conflicts = []
    for c in m.get("conflicts", []) or []:
        if not isinstance(c, dict):
            raise ReleaseError(f"{where}: conflicts item must be {{id, reason}}")
        item = {"id": check_id(c.get("id"), f"{where}: conflicts")}
        if isinstance(c.get("reason"), str) and c["reason"]:
            item["reason"] = c["reason"][:200]
        conflicts.append(item)
    hooks = m.get("hooks", []) or []
    if not isinstance(hooks, list) or not all(isinstance(h, str) for h in hooks):
        raise ReleaseError(f"{where}: hooks must be a list of ids")
    try:
        requires = reqs(m.get("requires", []) or [], where)
    except EntryError as e:
        raise ReleaseError(str(e)) from None
    return {"version": version, "type": kind, "api": api, "games": games, "requires": requires,
            "conflicts": conflicts, "hooks": hooks}


def build_version(entry: dict, rel: dict, gh: GitHub, by_id_tag: bool) -> tuple[dict, dict] | None:
    """One release as a Version, with the archives' download counts. None: not this mod's release."""
    mid, tag = entry["id"], rel["tag_name"]
    owner, repo = owner_repo(entry["repo"])
    raw = gh.raw(owner, repo, tag, join(entry["path"], "openrac-mod.json"))
    if raw is None:
        if by_id_tag:
            raise ReleaseError(f"{tag}: no {join(entry['path'], 'openrac-mod.json')} at the tag")
        return None
    try:
        manifest = json.loads(raw)
    except ValueError as e:
        raise ReleaseError(f"{tag}: openrac-mod.json: {e}") from None
    if manifest.get("id") != mid:
        if by_id_tag:
            raise ReleaseError(f"{tag}: the manifest's id is {manifest.get('id')!r}, not {mid!r}")
        return None
    f = manifest_fields(manifest, tag)
    ver = f["version"]
    if by_id_tag and tag != f"{mid}-v{ver}":
        raise ReleaseError(f"{tag}: the manifest says version {ver}")
    assets = {a["name"]: a for a in rel.get("assets", []) if a.get("state", "uploaded") == "uploaded"}
    sums = {}
    if "SHA256SUMS" in assets:
        text = gh.download(assets["SHA256SUMS"]["browser_download_url"], MAX_SUMS)
        sums = parse_sums(text or b"")
    platforms = {}
    if f["type"] == "native":
        names = [(p, f"{mid}-{ver}-{p}.zip") for p in PLATFORMS]
    else:
        names = [("any", f"{mid}-{ver}.zip")]
    for plat, name in names:
        a = assets.get(name)
        if a is None:
            continue
        sha = sums.get(name)
        if sha is None:
            if f["type"] == "native":
                raise ReleaseError(f"{tag}: {name} has no line in SHA256SUMS")
            continue  # a script mod without hashes is read from the repo at its tag
        digest = (a.get("digest") or "").lower()
        if digest.startswith("sha256:") and digest[7:] != sha:
            raise ReleaseError(f"{tag}: {name}'s SHA256SUMS line differs from the uploaded file")
        platforms[plat] = {"asset": name, "sha256": sha, "size": int(a.get("size", 0))}
    if f["type"] == "native" and not platforms:
        raise ReleaseError(f"{tag}: no {mid}-{ver}-<platform>.zip archive")
    files: list[str] = []
    merged = assets.get(f"{mid}-{ver}.zip")
    sources = [merged] if merged else [assets[p["asset"]] for p in platforms.values()]
    for a in sources:
        for n in gh.zip_names(a["browser_download_url"], int(a.get("size", 0))):
            if n not in files:
                files.append(n)
    files = files[:MAX_FILES]
    mine = [a for n, a in assets.items() if n != "SHA256SUMS" and (by_id_tag or n.startswith(f"{mid}-{ver}"))]
    counts = {"downloads": sum(int(a.get("download_count", 0)) for a in mine)}
    version = {
        "version": ver,
        "api": f["api"],
        "games": f["games"],
        "requires": f["requires"],
        "conflicts": f["conflicts"],
        "hooks": f["hooks"],
        "tag": tag,
        "released": norm_time(rel.get("published_at") or rel.get("created_at")),
        "prerelease": bool(rel.get("prerelease")) or "-" in ver,
        "changelog": clip(rel.get("body") or ""),
        "size": max((p["size"] for p in platforms.values()), default=0),
        "files": files,
        "platforms": platforms,
    }
    return version, {"type": f["type"], **counts}


def clip(text: str, limit: int = MAX_TEXT) -> str:
    b = text.replace("\r\n", "\n").encode("utf-8")
    return b[:limit].decode("utf-8", "ignore") if len(b) > limit else text.replace("\r\n", "\n")


def short_description(text: str) -> str:
    text = " ".join(str(text or "").split())
    if len(text) <= MAX_DESCRIPTION:
        return text
    cut = text[:MAX_DESCRIPTION - 1]
    for stop in (". ", "; ", ": "):
        i = cut.rfind(stop)
        if i > MAX_DESCRIPTION // 2:
            return cut[:i + 1]
    return cut[:cut.rfind(" ")].rstrip(",;:") + "…"


def held(entry: dict, v: dict, kind: str, hold: dict | None) -> str | None:
    """Why a new version waits (cool-down for external native mods), or None to publish it."""
    if hold is None or entry["local"] is not None or kind != "native":
        return None
    if v["version"] not in hold["approved"].get(entry["id"], []):
        return "waits for a maintainer's review (index/approved.json)"
    released = dt.datetime.fromisoformat(v["released"].replace("Z", "+00:00")).timestamp() if v["released"] else 0
    if hold["now"] - released < COOL_DOWN:
        return "is less than 24 h old"
    return None


def build_mod(entry: dict, gh: GitHub, prev: dict | None, log, hold: dict | None = None) -> tuple[dict | None, int]:
    """The Mod of one entry (None when it has no usable version yet), and its download total. With
    `hold`, new versions of external native mods wait for review and 24 h."""
    mid = entry["id"]
    owner, repo = owner_repo(entry["repo"])
    e = entry.get("entry", {})
    if e.get("type") == "pack" or ("versions" in e and "type" not in e):
        return build_pack(entry), 0
    head = entry.get("manifest")
    if head is None:
        raw = gh.raw(owner, repo, "HEAD", join(entry["path"], "openrac-mod.json"))
        if raw is None:
            raise EntryError(f"{entry['where']}: no {join(entry['path'], 'openrac-mod.json')} in {entry['repo']}")
        try:
            head = json.loads(raw)
        except ValueError as err:
            raise EntryError(f"{entry['where']}: openrac-mod.json: {err}") from None
    if head.get("id") != mid:
        raise EntryError(f"{entry['where']}: the manifest's id is {head.get('id')!r}, not {mid!r}")
    try:
        head_f = manifest_fields(head, entry["where"])
    except ReleaseError as err:
        raise EntryError(str(err)) from None
    prev_versions = {v["tag"]: v for v in (prev or {}).get("versions", []) if v.get("tag")}
    versions, downloads = [], 0
    id_tag = re.compile(rf"^{re.escape(mid)}-v({SEMVER})$")
    for rel in gh.releases(owner, repo):
        tag = rel.get("tag_name", "")
        if rel.get("draft") or tag == INDEX_TAG:
            continue
        by_id = bool(id_tag.match(tag))
        if not by_id and not re.match(rf"^v{SEMVER}$", tag):
            continue
        if by_id:  # every archive of the mod's own release counts, even a broken one
            downloads += sum(int(a.get("download_count", 0)) for a in rel.get("assets", []) if a["name"] != "SHA256SUMS")
        try:
            built = build_version(entry, rel, gh, by_id)
        except (ReleaseError, EntryError, NetError) as err:
            old = prev_versions.get(tag)
            log(f"{mid}: skipped {tag}: {err}" + ("; kept the last good one" if old else ""))
            if old:
                versions.append(old)
            continue
        if built is None:
            continue
        v, info = built
        if not by_id:
            downloads += info["downloads"]
        old = prev_versions.get(tag)
        if old and entry["local"] is None and old.get("platforms") != v["platforms"]:
            # External archives swapped after they were indexed: keep the hashes first seen.
            log(f"{mid}: {tag}'s archives changed after they were indexed; kept the first ones")
            v = old
        why = None if old else held(entry, v, info["type"], hold)
        if why:
            log(f"{mid}: {v['version']} not published yet: it {why}")
            hold["review"].append({"id": mid, "name": head.get("name", mid), "repo": entry["repo"],
                                   "version": v["version"], "tag": tag, "released": v["released"],
                                   "changelog": v["changelog"], "platforms": v["platforms"]})
            continue
        versions.append(v)
    if not versions and head_f["type"] == "script":
        commits = gh.api(f"repos/{owner}/{repo}/commits?path={urllib.parse.quote(entry['path'])}&per_page=1") or []
        if commits:
            c = commits[0]
            versions.append({
                "version": head_f["version"], "api": head_f["api"], "games": head_f["games"],
                "requires": head_f["requires"], "conflicts": head_f["conflicts"], "hooks": head_f["hooks"],
                "tag": c["sha"], "released": norm_time(c["commit"]["committer"]["date"]),
                "prerelease": "-" in head_f["version"], "changelog": "", "size": 0, "files": [], "platforms": {}})
    seen, unique = set(), []
    for v in sorted(versions, key=lambda v: (semver_key(v["version"]), v["released"]), reverse=True):
        if v["version"] not in seen:
            seen.add(v["version"])
            unique.append(v)
    if not unique:
        log(f"{mid}: no release yet; not listed")
        return None, downloads
    readme_ref = unique[0]["tag"] or "HEAD"
    readme = gh.raw(owner, repo, readme_ref, join(entry["path"], "README.md"), limit=MAX_TEXT * 4) or b""
    return mod_record(entry, head, head_f["type"], unique, clip(readme.decode("utf-8", "replace"))), downloads


def build_pack(entry: dict) -> dict:
    e = entry["entry"]
    games = e.get("games", {"rac1": ["pal", "ntsc"]})
    versions = []
    for v in e["versions"]:
        versions.append({
            "version": v["version"], "api": None, "games": games, "requires": reqs(v.get("requires", []), entry["where"]),
            "conflicts": [], "hooks": [], "tag": "", "released": norm_time(v["released"]),
            "prerelease": "-" in v["version"], "changelog": clip(v.get("changelog", "")), "size": 0, "files": [],
            "platforms": {}})
    versions.sort(key=lambda v: semver_key(v["version"]), reverse=True)
    head = {k: e[k] for k in ("name", "author", "description", "license", "homepage") if k in e}
    return mod_record(entry, head, "pack", versions, clip(e.get("readme", "")))


def mod_record(entry: dict, head: dict, kind: str, versions: list[dict], readme: str) -> dict:
    listing = entry["listing"]
    games: dict[str, list[str]] = {}
    for v in versions:
        for g, discs in v["games"].items():
            have = games.setdefault(g, [])
            have += [d for d in discs if d not in have]
    links = dict(listing.get("links", {}))
    links.setdefault("bugs", entry["repo"] + "/issues")
    mod = {
        "id": entry["id"],
        "name": str(head.get("name") or entry["id"])[:100],
        "author": str(head.get("author") or "")[:100],
        "description": listing.get("description") or short_description(head.get("description", "")),
    }
    if isinstance(head.get("license"), str) and head["license"]:
        mod["license"] = head["license"][:64]
    if is_https(head.get("homepage")):
        mod["homepage"] = head["homepage"]
    mod.update({
        "repo": entry["repo"],
        "path": entry["path"],
        "type": kind,
        "games": games,
        "tags": list(listing.get("tags", [])),
        "impact": list(listing.get("impact", [])),
        "aliases": list(listing.get("aliases", [])),
        "links": {k: links[k] for k in LINK_KEYS if k in links},
        "downloads": 0,
        "downloads7d": 0,
        "created": min(v["released"] for v in versions),
        "updated": versions[0]["released"],
        "featured": bool(listing.get("featured", False)),
        # What the author stated; left out when they have not said, which is not the same as "no".
        **({"aiAssisted": listing["aiAssisted"]} if "aiAssisted" in listing else {}),
        **({"discussion": entry["discussion"]} if entry.get("discussion") else {}),
        "recommends": list(listing.get("recommends", [])),
        "suggests": list(listing.get("suggests", [])),
        "readme": readme,
        "media": {"icon": None, "banner": None, "screenshots": [], "videos": []},
        "versions": versions,
    })
    return mod


# ------------------------------------------------------------------------------------------------
# Across the mods: dependencies, cycles, other indexes
# ------------------------------------------------------------------------------------------------

def check_dependencies(mods: dict[str, dict], log) -> None:
    """Drops versions whose requires or conflicts name a mod not in the index (and mods left with no
    version), until nothing changes."""
    changed = True
    while changed:
        changed = False
        for mid in sorted(mods):
            mod = mods[mid]
            keep = []
            for v in mod["versions"]:
                missing = [r["id"] for r in v["requires"] + v["conflicts"] if r["id"] not in mods]
                if missing:
                    log(f"{mid}: skipped {v['version']}: it names {', '.join(sorted(set(missing)))}, not in the index")
                    changed = True
                else:
                    keep.append(v)
            mod["versions"] = keep
            if not keep:
                log(f"{mid}: no usable version left; not listed")
                del mods[mid]


def find_cycle(mods: dict[str, dict]) -> list[str] | None:
    """A dependency cycle over every version's requires, or None."""
    graph = {m: sorted({r["id"] for v in mod["versions"] for r in v["requires"]}) for m, mod in mods.items()}
    state: dict[str, int] = {}
    stack: list[str] = []

    def visit(n: str):
        state[n] = 1
        stack.append(n)
        for d in graph.get(n, []):
            if state.get(d) == 1:
                return stack[stack.index(d):] + [d]
            if d in graph and state.get(d) is None:
                found = visit(d)
                if found:
                    return found
        stack.pop()
        state[n] = 2
        return None

    for n in sorted(graph):
        if state.get(n) is None:
            found = visit(n)
            if found:
                return found
    return None


def other_index_ids(http: Http, log) -> dict[str, str]:
    """Mod ids of the other indexes in MOD_INDEXES, by id: the index URL."""
    out = {}
    for url in [u.strip() for u in os.environ.get("MOD_INDEXES", "").split(",") if u.strip()]:
        if url == INDEX_URL:
            continue
        try:
            r = http.get(url, limit=64 * 1024 * 1024)
            if r.status != 200:
                log(f"other index {url}: HTTP {r.status}; ids not checked against it")
                continue
            for m in r.json().get("mods", []):
                out[m.get("id")] = url
        except (NetError, ValueError) as e:
            log(f"other index {url}: {e}; ids not checked against it")
    return out


# ------------------------------------------------------------------------------------------------
# Ports, stats, revoked
# ------------------------------------------------------------------------------------------------

def parse_api(header: str) -> str:
    major = re.search(r"^\s*#\s*define\s+OPENRAC_MOD_API_MAJOR\s+(\d+)", header, re.M)
    minor = re.search(r"^\s*#\s*define\s+OPENRAC_MOD_API_MINOR\s+(\d+)", header, re.M)
    if not major or not minor:
        raise ValueError("no OPENRAC_MOD_API_MAJOR/MINOR")
    return f"{major.group(1)}.{minor.group(1)}"


def read_ports(http: Http, gh: GitHub, prev: dict | None, log) -> dict:
    owner, repo = PORT_REPO.split("/")
    out = {}
    for channel, manifest_url in (("stable", STABLE_MANIFEST), ("dev", DEV_MANIFEST)):
        try:
            r = http.get(manifest_url, limit=4 * 1024 * 1024)
            if r.status != 200:
                raise NetError(f"{manifest_url}: HTTP {r.status}")
            launcher = str(r.json()["version"]).removeprefix("launcher-v").removeprefix("v")
            ref = f"launcher-v{launcher}" if channel == "stable" else DEV_BRANCH
            header = gh.raw(owner, repo, ref, PORT_HEADER, limit=1024 * 1024)
            if header is None:
                raise NetError(f"no {PORT_HEADER} at {ref}")
            out[channel] = {"launcher": launcher, "api": parse_api(header.decode("utf-8", "replace"))}
        except (NetError, ValueError, KeyError) as e:
            old = ((prev or {}).get("ports") or {}).get(channel)
            if not old:
                raise NetError(f"ports.{channel}: {e}") from None
            log(f"ports.{channel}: {e}; kept the last one")
            out[channel] = old
    return out


def update_stats(stats: dict | None, totals: dict[str, int], now: dt.datetime) -> tuple[dict, dict[str, int]]:
    """Adds today's totals, keeps 30 days, and gives each mod's growth over the last 7 days."""
    days = dict((stats or {}).get("days", {})) if isinstance(stats, dict) else {}
    today = now.date()
    days[today.isoformat()] = dict(sorted(totals.items()))
    keep_from = (today - dt.timedelta(days=STATS_DAYS - 1)).isoformat()
    days = {d: v for d, v in sorted(days.items()) if keep_from <= d <= today.isoformat()}
    week_ago = (today - dt.timedelta(days=7)).isoformat()
    older = [d for d in days if d <= week_ago]
    base_day = older[-1] if older else None
    growth = {}
    for mid, total in totals.items():
        growth[mid] = max(0, total - int(days[base_day].get(mid, 0))) if base_day else 0
    return {"format": 1, "days": days}, growth


def load_map(path: Path, kind) -> dict:
    """index/approved.json ({id: [versions]}) or index/discussions.json ({id: number})."""
    if not path.is_file():
        return {}
    data = read_json(path)
    ok = isinstance(data, dict) and all(isinstance(k, str) and ID_RE.match(k) for k in data)
    if kind is list:
        ok = ok and all(isinstance(v, list) and all(SEMVER_RE.match(str(x)) for x in v) for v in data.values())
    else:
        ok = ok and all(isinstance(v, int) and not isinstance(v, bool) and v > 0 for v in data.values())
    if not ok:
        raise EntryError(f"index/{path.name}: must be " + ('{"<id>": ["1.2.0"]}' if kind is list else '{"<id>": 12}'))
    return data


def load_revoked(root: Path) -> list[dict]:
    path = root / "index" / "revoked.json"
    if not path.is_file():
        return []
    data = read_json(path)
    if not isinstance(data, list):
        raise EntryError("index/revoked.json: must be a list")
    out = []
    for r in data:
        if not isinstance(r, dict) or set(r) - {"id", "versions", "reason"}:
            raise EntryError("index/revoked.json: each item is {id, versions, reason}")
        check_id(r.get("id"), "index/revoked.json")
        vs = r.get("versions")
        if not isinstance(vs, list) or not vs or not all(v == "*" or SEMVER_RE.match(str(v)) for v in vs):
            raise EntryError(f"index/revoked.json: {r['id']}: versions are semver or \"*\"")
        if not isinstance(r.get("reason"), str) or not r["reason"] or len(r["reason"]) > MAX_DESCRIPTION:
            raise EntryError(f"index/revoked.json: {r['id']}: a reason of at most {MAX_DESCRIPTION} characters")
        out.append({"id": r["id"], "versions": list(vs), "reason": r["reason"]})
    return sorted(out, key=lambda r: r["id"])


# ------------------------------------------------------------------------------------------------
# Media: collect the sources for tools/media.py, check the videos
# ------------------------------------------------------------------------------------------------

def media_source(entry: dict, src: str, gh: GitHub, root: Path) -> bytes:
    """The bytes of one picture: from this repo (in-repo mods, or index/media/<id>/...) or the author's."""
    if src.startswith("index/media/"):
        base, rel = root, src
        if not src.startswith(f"index/media/{entry['id']}/"):
            raise media.MediaError(f"{src}: pictures in index/media/ go under index/media/{entry['id']}/")
    elif entry["local"] is not None:
        base, rel = entry["local"], src
    else:
        owner, repo = owner_repo(entry["repo"])
        data = gh.raw(owner, repo, "HEAD", join(entry["path"], src), limit=media.MAX_BYTES)
        if data is None:
            raise media.MediaError(f"{src}: not found in {entry['repo']}")
        return data
    path = (base / rel).resolve()
    if not path.is_relative_to(base.resolve()) or not path.is_file():
        raise media.MediaError(f"{src}: not found")
    if path.stat().st_size > media.MAX_BYTES:
        raise media.MediaError(f"{src}: over the {media.MAX_BYTES // (1024 * 1024)} MB cap")
    return path.read_bytes()


def collect_media(entry: dict, mod: dict, gh: GitHub, http: Http, root: Path, out: Path | None,
                  requests: list, problem, log) -> None:
    m = entry["listing"].get("media", {})
    wanted = []
    if m.get("icon"):
        wanted.append(("icon", 0, m["icon"], "", f"{mod['name']} icon"))
    if m.get("banner"):
        wanted.append(("banner", 0, m["banner"], "", f"{mod['name']} banner"))
    for i, s in enumerate(m.get("screenshots", [])):
        cap = s.get("caption", "")
        wanted.append(("screenshot", i, s["src"], cap, cap or f"{mod['name']} screenshot {i + 1}"))
    for kind, i, src, caption, alt in wanted:
        try:
            data = media_source(entry, src, gh, root)
            info = media.check_source(data)
        except (media.MediaError, NetError) as e:
            problem(f"{entry['where']}: media {src}: {e}")
            continue
        for note in info["notes"]:
            log(f"{mod['id']}: {src}: {note}")
        if out is not None:
            name = hashlib.sha256(data).hexdigest() + "." + info["type"]
            (out / "media-src").mkdir(parents=True, exist_ok=True)
            (out / "media-src" / name).write_bytes(data)
            requests.append({"mod": mod["id"], "kind": kind, "index": i, "file": name, "src": src,
                             "caption": caption, "alt": alt})

    def fetch(url):
        try:
            r = http.get(url, limit=8 * 1024 * 1024)
            return r.status, r.body
        except NetError:
            return 0, b""

    for v in m.get("videos", []):
        vid = media.youtube_id(v["youtube"])
        ok = media.youtube_check(vid, fetch)
        if ok is None:
            log(f"{mod['id']}: video {vid} is private, removed or not embeddable; dropped")
            continue
        video = {"youtube": vid, "title": ok["title"], "caption": v.get("caption", "")}
        uploaded = media.youtube_uploaded(vid, fetch)
        if uploaded:
            video["uploaded"] = uploaded
        mod["media"]["videos"].append(video)


# ------------------------------------------------------------------------------------------------
# Index shape (the contract), used before writing and before publishing
# ------------------------------------------------------------------------------------------------

def validate_index(ix: dict) -> list[str]:
    """What is wrong with an index.json against format 1; [] when it is fine."""
    errs: list[str] = []

    def need(cond, msg):
        if not cond:
            errs.append(msg)

    def is_time(s):
        return isinstance(s, str) and bool(re.match(r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$", s))

    need(ix.get("format") == FORMAT, "format must be 1")
    need(isinstance(ix.get("serial"), int) and ix["serial"] > 0, "serial must be a positive integer")
    need(is_time(ix.get("generated")) and is_time(ix.get("expires")), "generated/expires must be ISO UTC times")
    need(is_https(ix.get("source")) and is_https(ix.get("media")), "source/media must be https URLs")
    ports = ix.get("ports") or {}
    for ch in ("stable", "dev"):
        p = ports.get(ch) or {}
        need(isinstance(p.get("launcher"), str) and API_RE.match(str(p.get("api"))), f"ports.{ch} is malformed")
    need(ix.get("tags") == TAGS, "tags must be the fixed list")
    mods = ix.get("mods")
    need(isinstance(mods, list), "mods must be a list")
    mods = mods if isinstance(mods, list) else []
    ids = [m.get("id") for m in mods]
    need(ids == sorted(ids) and len(set(ids)) == len(ids), "mods must be sorted by id, each once")
    for m in mods:
        w = f"mods[{m.get('id')}]"
        need(isinstance(m.get("id"), str) and ID_RE.match(m["id"]) and len(m["id"]) <= MAX_ID, f"{w}: bad id")
        for k in ("name", "author", "description", "repo", "path", "readme", "created", "updated"):
            need(isinstance(m.get(k), str), f"{w}: {k} must be text")
        need(len(m.get("description", "")) <= MAX_DESCRIPTION, f"{w}: description over {MAX_DESCRIPTION}")
        need(len(m.get("readme", "").encode()) <= MAX_TEXT, f"{w}: readme over 64 KiB")
        need(m.get("type") in TYPES, f"{w}: bad type")
        need(isinstance(m.get("games"), dict), f"{w}: games")
        need(all(t in TAGS for t in m.get("tags", [None])), f"{w}: tags")
        need(all(t in IMPACT for t in m.get("impact", [None])), f"{w}: impact")
        need(isinstance(m.get("aliases"), list), f"{w}: aliases")
        need(isinstance(m.get("links"), dict) and not set(m["links"]) - set(LINK_KEYS), f"{w}: links")
        need(isinstance(m.get("downloads"), int) and isinstance(m.get("downloads7d"), int), f"{w}: downloads")
        need(isinstance(m.get("featured"), bool), f"{w}: featured")
        need(isinstance(m.get("aiAssisted", False), bool), f"{w}: aiAssisted")
        need("discussion" not in m or (isinstance(m["discussion"], int) and m["discussion"] > 0), f"{w}: discussion")
        for k in ("recommends", "suggests"):
            need(isinstance(m.get(k), list) and all(x in ids for x in m[k]), f"{w}: {k} names a mod not here")
        md = m.get("media") or {}
        need(set(md) >= {"icon", "banner", "screenshots", "videos"}, f"{w}: media")
        for img in [md.get("icon"), md.get("banner")] + list(md.get("screenshots") or []):
            if img is None:
                continue
            need(isinstance(img.get("hash"), str) and re.match(r"^[0-9a-f]{12}$", img["hash"]), f"{w}: image hash")
            need(isinstance(img.get("widths"), list) and img["widths"] and all(isinstance(x, int) for x in img["widths"]),
                 f"{w}: image widths")
            need(isinstance(img.get("width"), int) and isinstance(img.get("height"), int), f"{w}: image size")
        for v in md.get("videos") or []:
            need(isinstance(v.get("youtube"), str) and media.YOUTUBE_ID.match(v["youtube"]), f"{w}: video id")
        vs = m.get("versions")
        need(isinstance(vs, list) and vs, f"{w}: at least one version")
        vs = vs if isinstance(vs, list) else []
        keys = [semver_key(v.get("version", "")) for v in vs]
        need(keys == sorted(keys, reverse=True), f"{w}: versions must be newest first")
        for v in vs:
            vw = f"{w}@{v.get('version')}"
            need(isinstance(v.get("version"), str) and SEMVER_RE.match(v["version"]), f"{vw}: bad version")
            need(v.get("api") is None or (isinstance(v["api"], str) and API_RE.match(v["api"])), f"{vw}: bad api")
            need(m.get("type") not in ("data", "pack") or v.get("api") is None, f"{vw}: a {m.get('type')} has api null")
            for r in v.get("requires", []):
                need(r.get("id") in ids, f"{vw}: requires {r.get('id')}, not in the index")
                need("version" not in r or CONSTRAINT_RE.match(r["version"]), f"{vw}: bad constraint")
            for c in v.get("conflicts", []):
                need(c.get("id") in ids, f"{vw}: conflicts with {c.get('id')}, not in the index")
            need(isinstance(v.get("hooks"), list), f"{vw}: hooks")
            need(isinstance(v.get("tag"), str), f"{vw}: tag")
            need(is_time(v.get("released")), f"{vw}: released")
            need(isinstance(v.get("prerelease"), bool), f"{vw}: prerelease")
            need(isinstance(v.get("changelog"), str), f"{vw}: changelog")
            need(isinstance(v.get("size"), int), f"{vw}: size")
            need(isinstance(v.get("files"), list) and len(v["files"]) <= MAX_FILES, f"{vw}: files")
            plats = v.get("platforms")
            need(isinstance(plats, dict), f"{vw}: platforms")
            for k, p in (plats or {}).items():
                need(k in PLATFORMS or k == "any", f"{vw}: platform {k}")
                need(isinstance(p.get("asset"), str) and HEX64.match(str(p.get("sha256"))) and isinstance(p.get("size"), int),
                     f"{vw}: platform {k} is malformed")
    for r in ix.get("revoked", None) if isinstance(ix.get("revoked"), list) else [None]:
        need(isinstance(r, dict) and isinstance(r.get("id"), str) and isinstance(r.get("versions"), list)
             and isinstance(r.get("reason"), str), "revoked is malformed")
    return errs


# ------------------------------------------------------------------------------------------------
# The build
# ------------------------------------------------------------------------------------------------

def previous(http: Http, name: str):
    try:
        r = http.get(MEDIA_BASE + name, limit=64 * 1024 * 1024)
        return r.json() if r.status == 200 else None
    except (NetError, ValueError):
        return None


def build(root: Path, http: Http, now: float, out: Path | None, check: bool, extra: Path | None = None,
          log=None, review: list | None = None) -> tuple[dict | None, list[str], list[str]]:
    """Builds the index. Returns (index, errors, warnings); index is None when there are errors.
    `review`, if given, gets the versions held for review."""
    warnings: list[str] = []
    out = None if check else out
    log = log or (lambda msg: (warnings.append(msg), print("warning: " + msg, file=sys.stderr)))
    gh = GitHub(http)
    entries, errors = load_entries(root, extra)
    try:
        revoked = load_revoked(root)
    except EntryError as e:
        errors.append(str(e))
        revoked = []
    try:
        approved = load_map(root / "index" / "approved.json", list)
        numbers = load_map(root / "index" / "discussions.json", int)
    except EntryError as e:
        errors.append(str(e))
        approved, numbers = {}, {}
    for e in entries:
        if numbers.get(e["id"]):
            e["discussion"] = numbers[e["id"]]
    hold = {"approved": approved, "now": now, "review": review if review is not None else []}
    prev_index = previous(http, "index.json")
    prev_mods = {m["id"]: m for m in (prev_index or {}).get("mods", []) if isinstance(m, dict)}
    entry_ids = {e["id"] for e in entries}
    others = other_index_ids(http, log)
    for e in entries:
        if e["id"] in others:
            errors.append(f"{e['where']}: id {e['id']!r} is already used by {others[e['id']]}")
        for key in ("recommends", "suggests"):
            for x in e["listing"].get(key, []):
                if x not in entry_ids and x not in others:
                    errors.append(f"{e['where']}: {key} {x!r}, which is not in the index")
                if x == e["id"]:
                    errors.append(f"{e['where']}: {key} itself")
    mods, totals, by_id = {}, {}, {e["id"]: e for e in entries}
    for e in entries:
        try:
            mod, downloads = build_mod(e, gh, prev_mods.get(e["id"]), log, hold)
        except EntryError as err:
            errors.append(str(err))
            continue
        except NetError as err:
            errors.append(f"{e['where']}: {err}")
            continue
        totals[e["id"]] = downloads
        if mod is not None:
            mod["downloads"] = downloads
            mods[e["id"]] = mod
    check_dependencies(mods, log)
    cycle = find_cycle(mods)
    if cycle:
        errors.append("dependency cycle: " + " -> ".join(cycle))
    try:
        ports = read_ports(http, gh, prev_index, log)
    except NetError as e:
        errors.append(str(e))
        ports = {}
    requests: list = []
    problem = errors.append if check else log
    for mid in sorted(mods):
        try:
            collect_media(by_id[mid], mods[mid], gh, http, root, out, requests, problem, log)
        except media.MediaError as e:
            errors.append(f"{by_id[mid]['where']}: {e}")
    for mod in mods.values():
        for key in ("recommends", "suggests"):
            gone = [x for x in mod[key] if x not in mods]
            if gone:
                log(f"{mod['id']}: {key} {', '.join(gone)} dropped (not listed)")
            mod[key] = [x for x in mod[key] if x in mods]
    when = dt.datetime.fromtimestamp(int(now), dt.timezone.utc)
    stats, growth = update_stats(previous(http, "stats.json"), totals, when)
    for mid, mod in mods.items():
        mod["downloads7d"] = growth.get(mid, 0)
    serial = int(now)
    if prev_index and isinstance(prev_index.get("serial"), int) and prev_index["serial"] >= serial:
        serial = prev_index["serial"] + 1
    index = {
        "format": FORMAT,
        "serial": serial,
        "generated": iso(when),
        "expires": iso(when + EXPIRES),
        "source": SOURCE,
        "media": MEDIA_BASE,
        "ports": ports,
        "tags": TAGS,
        "mods": [mods[k] for k in sorted(mods)],
        "revoked": revoked,
    }
    if errors:
        return None, errors, warnings
    shape = validate_index(index)
    if shape:
        return None, shape, warnings
    if out is not None and not check:
        out.mkdir(parents=True, exist_ok=True)
        write_json(out / "index.json", index)
        write_json(out / "stats.json", stats)
        write_json(out / "review.json", hold["review"])
        (out / "media-src").mkdir(exist_ok=True)
        write_json(out / "media-src" / "requests.json", requests)
        write_json(out / "media-src" / "cache.json", previous(http, "media-cache.json") or {})
        existing = []
        try:
            rel = gh.api(f"repos/{SOURCE.removeprefix('https://github.com/')}/releases/tags/{INDEX_TAG}")
            existing = sorted(a["name"] for a in (rel or {}).get("assets", []))
        except NetError:
            pass
        write_json(out / "media-src" / "existing.json", existing)
    return index, errors, warnings


def write_json(path: Path, data) -> None:
    path.write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=ROOT / "out", help="where index.json goes (default out/)")
    ap.add_argument("--check", action="store_true", help="validate only, write nothing")
    ap.add_argument("--offline", type=Path, metavar="FIXTURE_DIR", help="recorded responses, no network")
    ap.add_argument("--root", type=Path, help="the repository to list (default this one, or FIXTURE_DIR/repo)")
    ap.add_argument("--check-entry", type=Path, metavar="FILE", help="check one index entry file as if added")
    ap.add_argument("--validate", type=Path, metavar="INDEX", help="check a built index.json's shape and exit")
    ap.add_argument("--now", type=float, help="build time, unix seconds (for tests)")
    args = ap.parse_args(argv)
    if args.validate:
        errs = validate_index(json.loads(args.validate.read_text()))
        for e in errs:
            print(f"error: {e}", file=sys.stderr)
        print(f"{args.validate}: {'ok' if not errs else f'{len(errs)} error(s)'}")
        return 1 if errs else 0
    if args.offline:
        http: Http = Offline(args.offline)
        root = args.root or (args.offline / "repo" if (args.offline / "repo").is_dir() else ROOT)
    else:
        http = Http(os.environ.get("GITHUB_TOKEN") or None)
        root = args.root or ROOT
    check = args.check or args.check_entry is not None
    index, errors, warnings = build(root, http, args.now if args.now is not None else time.time(),
                                    None if check else args.out, check, args.check_entry)
    for e in errors:
        print(f"error: {e}", file=sys.stderr)
    if index is None:
        print(f"index: {len(errors)} error(s)")
        return 1
    n = sum(len(m["versions"]) for m in index["mods"])
    print(f"index: {len(index['mods'])} mods, {n} versions, {len(warnings)} warning(s), serial {index['serial']}"
          + ("" if check else f", written to {args.out / 'index.json'}"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
