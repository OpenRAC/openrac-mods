#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""The listing bot: issue forms in, pull requests and a checklist comment out.

    python3 tools/submit_bot.py check   --event FILE --out DIR   # read-only: checks, files to change
    python3 tools/submit_bot.py apply   --event FILE --result DIR  # write: branch, PR, the comment
    python3 tools/submit_bot.py approve --event FILE             # a maintainer's `approved` label
    python3 tools/submit_bot.py review  --file out/review.json   # review issues for held versions
    python3 tools/submit_bot.py discussions --index out/index.json  # one Discussion per mod

Forms (by label): mod-submission, listing-update, mod-removal, mod-revoke (maintainers only).
The issue text is read from the event file only and is never run: it goes into files, JSON and
command arguments, not into a shell. Stdlib only, plus `git` and `gh` for the write steps.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import traceback
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_index as b  # noqa: E402
import media  # noqa: E402

ROOT = b.ROOT
REPO_SLUG = b.SOURCE.removeprefix("https://github.com/")
MAINTAINERS = "@OpenRAC/maintainers"
MARKER = "<!-- openrac-listing-bot -->"
SITE = "https://openrac.dev/mods/"
WRITERS = {"admin", "maintain", "write"}
MAINTAIN = {"admin", "maintain"}
ATTACHMENT = re.compile(r"https://github\.com/user-attachments/assets/[0-9A-Za-z-]+")

# Each form's headings (the templates' labels) to field ids.
COMMON = {"Short description": "description", "Tags": "tags", "Impact": "impact", "Bug tracker": "bugs",
          "Discord": "discord", "Docs": "docs", "YouTube video": "video", "Screenshots": "screenshots"}
FORMS = {
    "mod-submission": {"Kind": "kind", "Repository": "repo", "Mod ids": "mods", "Modpack name": "name",
                       **COMMON, "Content rules": "rules"},
    "listing-update": {"Mod id": "mod", **COMMON},
    "mod-removal": {"Mod id": "mod", "Reason": "reason"},
    "mod-revoke": {"Mod id": "mod", "Versions": "versions", "Reason": "reason"},
}


class Unexpected(Exception):
    pass


# ------------------------------------------------------------------------------------------------
# Reading the form
# ------------------------------------------------------------------------------------------------

def parse_form(body: str, labels: dict[str, str]) -> dict:
    """An issue form's Markdown body ("### Label" then the value) as {field id: text}."""
    out: dict = {v: "" for v in labels.values()}
    current = None
    lines: list[str] = []

    def flush():
        if current is not None:
            text = "\n".join(lines).strip()
            out[current] = "" if text == "_No response_" else text

    for line in (body or "").replace("\r\n", "\n").split("\n"):
        m = re.match(r"^###\s+(.+?)\s*$", line)
        if m and m.group(1) in labels:
            flush()
            current, lines = labels[m.group(1)], []
        elif current is not None:
            lines.append(line)
    flush()
    return out


def split_list(text: str) -> list[str]:
    return [x.strip() for x in re.split(r"[,\n]", text or "") if x.strip()]


def checked(text: str) -> bool:
    return bool(re.search(r"^- \[[xX]\]", text or "", re.M))


def pictures(text: str) -> tuple[list[tuple[str, str]], list[str]]:
    """(url, caption) for each line of the screenshots field, and the lines that are not pictures."""
    found, bad = [], []
    for n, line in enumerate((text or "").split("\n"), 1):
        line = line.strip()
        if not line:
            continue
        left, _, caption = line.partition(" | ")
        urls = ATTACHMENT.findall(left)
        if len(urls) != 1:
            bad.append(f"line {n}")
            continue
        found.append((urls[0], " ".join(caption.split())[:b.MAX_CAPTION]))
    return found, bad


def slug(name: str) -> str:
    s = re.sub(r"[^a-z0-9]+", "-", (name or "").lower()).strip("-")
    return s[:b.MAX_ID].strip("-")


def md(text) -> str:
    """Untrusted text made safe for one table cell: no pipes, no newlines, no mentions."""
    return " ".join(str(text).replace("|", "\\|").replace("@", "@​").replace("<", "&lt;").split())[:300]


# ------------------------------------------------------------------------------------------------
# The checks
# ------------------------------------------------------------------------------------------------

class Bot:
    def __init__(self, root: Path, http: b.Http, now: float, vips=None):
        self.root, self.http, self.now = Path(root), http, now
        self.gh = b.GitHub(http)
        self.vips = vips  # a vips runner for the media table's output sizes, or None
        self.items: list[dict] = []
        self.media_rows: list[dict] = []
        self.changed: list[str] = []
        self.deleted: list[str] = []

    def item(self, ok: bool, text: str, fix: str = "") -> bool:
        self.items.append({"ok": bool(ok), "text": text, "fix": "" if ok else fix})
        return bool(ok)

    @property
    def ok(self) -> bool:
        return all(i["ok"] for i in self.items)

    def permission(self, owner: str, repo: str, user: str) -> str:
        if user.lower() == owner.lower():
            return "admin"
        try:
            d = self.gh.api(f"repos/{owner}/{repo}/collaborators/{user}/permission")
        except b.NetError:
            return "none"
        if not d:
            return "none"
        return d.get("role_name") or d.get("permission") or "none"

    def entries(self) -> dict[str, dict]:
        entries, _ = b.load_entries(self.root)
        return {e["id"]: e for e in entries}

    def write(self, rel: str, data) -> None:
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(data, (dict, list)):
            data = (json.dumps(data, indent=2, ensure_ascii=False) + "\n").encode()
        path.write_bytes(data)
        if rel not in self.changed:
            self.changed.append(rel)

    def remove(self, rel: str) -> None:
        path = self.root / rel
        if path.is_dir():
            for f in sorted(path.rglob("*")):
                if f.is_file():
                    self.deleted.append(str(f.relative_to(self.root)))
            shutil.rmtree(path)
        elif path.exists():
            path.unlink()
            self.deleted.append(rel)

    def find_mod(self, owner: str, repo: str, mid: str) -> tuple[str, dict] | None:
        """Where the mod's openrac-mod.json is in the repo (top, a listed folder, a folder <id>)."""
        cands = [""]
        listed = self.gh.raw(owner, repo, "HEAD", "openrac-mods.json")
        if listed:
            try:
                cands += [str(x) for x in json.loads(listed).get("mods", [])]
            except ValueError:
                pass
        cands.append(mid)
        top = self.gh.api(f"repos/{owner}/{repo}/contents/") or []
        cands += [x["name"] for x in top if isinstance(x, dict) and x.get("type") == "dir"][:60]
        for path in dict.fromkeys(cands):
            raw = self.gh.raw(owner, repo, "HEAD", b.join(path, "openrac-mod.json"))
            if not raw:
                continue
            try:
                m = json.loads(raw)
            except ValueError:
                continue
            if isinstance(m, dict) and m.get("id") == mid:
                return path, m
        return None

    # Listing fields shared by submit and update --------------------------------------------------

    def listing_fields(self, form: dict, mid: str, old: dict | None) -> dict:
        listing = dict(old or {})
        if form.get("description"):
            listing["description"] = " ".join(form["description"].split())
        for key in ("tags", "impact"):
            if form.get(key):
                listing[key] = [x.lower() for x in split_list(form[key])]
        links = dict(listing.get("links", {}))
        for key in b.LINK_KEYS:
            if form.get(key):
                links[key] = form[key].strip()
        if links:
            listing["links"] = links
        m = dict(listing.get("media", {}))
        if form.get("video"):
            try:
                vid = media.youtube_id(form["video"])
                ok = media.youtube_check(vid, self.fetch)
                if self.item(ok is not None, "The YouTube video can be embedded",
                             "Make the video public and allow embedding, or leave the field empty."):
                    m["videos"] = [{"youtube": vid, "caption": "Trailer"}]
            except media.MediaError as e:
                self.item(False, "The video is a YouTube link", f"{md(e)}. Paste a youtube.com or youtu.be link.")
        if form.get("screenshots"):
            self.take_pictures(form["screenshots"], mid, m)
        if m:
            listing["media"] = m
        try:
            b.check_listing({k: v for k, v in listing.items() if k in b.LISTING_KEYS}, "the listing")
            self.item(True, "Description, tags, impact and links are valid")
        except b.EntryError as e:
            self.item(False, "Description, tags, impact and links are valid", md(e))
        return listing

    def fetch(self, url):
        try:
            r = self.http.get(url, limit=8 * 1024 * 1024)
            return r.status, r.body
        except b.NetError:
            return 0, b""

    def take_pictures(self, text: str, mid: str, m: dict) -> None:
        found, bad = pictures(text)
        self.item(not bad, "Each screenshots line is one picture dragged into the form",
                  f"Fix {', '.join(bad)}: drag the picture into the field (it becomes a "
                  "github.com/user-attachments link), then ' | ' and a caption.")
        shots, kind_seen = [], set()
        for url, caption in found:
            name = url.rsplit("/", 1)[1][:12]
            try:
                r = self.http.get(url, limit=media.MAX_BYTES)
                if r.status != 200:
                    raise media.MediaError(f"download failed (HTTP {r.status})")
                info = media.check_source(r.body)
            except (media.MediaError, b.NetError) as e:
                self.media_rows.append({"file": name, "in": 0, "out": None, "notes": [str(e)], "ok": False})
                self.item(False, f"Picture {name} is accepted", md(e))
                continue
            data = r.body
            notes = list(info["notes"])
            if info["type"] == "jpeg" and b"Exif" in data[:65536] and b"\x88\x25" in data[:65536]:
                notes.append("GPS removed")
            kind = caption.lower() if caption.lower() in ("icon", "banner") and caption.lower() not in kind_seen \
                else "screenshot"
            kind_seen.add(kind)
            rel = f"index/media/{mid}/{hashlib.sha256(data).hexdigest()[:12]}.{info['type']}"
            self.write(rel, data)
            out = None
            if self.vips is not None:
                tmp = Path(tempfile.mkdtemp(prefix="openrac-bot-"))
                try:
                    res = media.process_image(data, kind, tmp, self.vips)
                    sizes = res["out"]
                    pick = [v for k, v in sizes.items() if k.endswith("-1600.avif")] or \
                        [v for k, v in sizes.items() if k.endswith(".avif")]
                    out = max(pick) if pick else None
                    notes += [n for n in res["notes"] if n not in notes]
                except (media.MediaError, subprocess.SubprocessError, OSError) as e:
                    notes.append(f"not converted here: {e}")
                finally:
                    shutil.rmtree(tmp, ignore_errors=True)
            self.media_rows.append({"file": name, "in": len(data), "out": out, "notes": notes, "ok": True,
                                    "size": f"{info['width']}x{info['height']}", "kind": kind})
            if kind == "screenshot":
                shots.append({"src": rel, **({"caption": caption} if caption else {})})
            else:
                m[kind] = rel
        if found:
            m["screenshots"] = shots

    def index_check(self, mid: str, where: str) -> dict | None:
        """The whole index checked with the change in place; this entry's problems become items."""
        review: list = []
        index, errors, warnings = b.build(self.root, self.http, self.now, None, True, log=lambda w: None,
                                          review=review)
        mine = [e for e in errors if (where and where in e) or re.search(rf"\b{re.escape(mid)}\b", e)]
        self.item(not mine, "The index builds with this entry",
                  "; ".join(md(e) for e in mine[:3]) or "")
        held = [r for r in review if r["id"] == mid]
        return {"index": index, "held": held}

    # The forms -----------------------------------------------------------------------------------

    def submit(self, form: dict, user: str) -> dict:
        self.item(checked(form.get("rules")), "The content rules are accepted",
                  "Tick 'My mod follows the content rules' (see CONTENT.md).")
        if form.get("kind", "Mod").strip() == "Modpack":
            return self.submit_pack(form, user)
        repo_url = (form.get("repo") or "").strip().removesuffix("/").removesuffix(".git")
        if not self.item(bool(b.REPO_RE.match(repo_url)), "The repository is https://github.com/owner/repo",
                         "Paste the repository's address, such as https://github.com/you/your-mod."):
            return {}
        owner, repo = b.owner_repo(repo_url)
        info = self.gh.api(f"repos/{owner}/{repo}")
        if not self.item(bool(info) and not info.get("private"), "The repository is public",
                         "Make the repository public: the launcher reads it without an account."):
            return {}
        perm = self.permission(owner, repo, user)
        self.item(perm in WRITERS, f"You can push to {owner}/{repo}",
                  "Open this issue from an account with write access to the repository, or ask its owner to.")
        ids = split_list(form.get("mods"))
        if not self.item(bool(ids), "Mod ids are given", "List the ids of the mods to list, comma-separated."):
            return {}
        entries = self.entries()
        first = None
        for mid in ids:
            try:
                b.check_id(mid, "id")
            except b.EntryError as e:
                self.item(False, f"{md(mid)} is a valid id", md(e))
                continue
            found = self.find_mod(owner, repo, mid)
            if not self.item(found is not None, f"{mid}: openrac-mod.json is found",
                             f"Put {mid}'s openrac-mod.json at the top of the repository or in a folder of its own."):
                continue
            path, manifest = found
            try:
                b.manifest_fields(manifest, "openrac-mod.json")
                self.item(True, f"{mid}: the manifest is valid")
            except (b.ReleaseError, b.EntryError) as e:
                self.item(False, f"{mid}: the manifest is valid",
                          f"{md(e)}. Check it with the SDK's package_mod.py --check.")
                continue
            old = entries.get(mid)
            same = old is not None and old["repo"] == repo_url
            if not self.item(old is None or same, f"{mid}: the id is free",
                             f"{mid} is already listed from {old['repo'] if old else ''}; pick another id."):
                continue
            first = first or mid
            listing_form = form if mid == first else {k: v for k, v in form.items() if k != "screenshots"}
            if repo_url == b.SOURCE:
                rel = f"{path}/listing.json"
                cur = json.loads((self.root / rel).read_text()) if (self.root / rel).is_file() else {}
                listing = self.listing_fields(listing_form, mid, cur)
                self.write(rel, listing)
            else:
                cur = json.loads((self.root / f"index/{mid}.json").read_text()) if same else {}
                listing = self.listing_fields(listing_form, mid, {k: v for k, v in cur.items()
                                                                  if k in b.LISTING_KEYS})
                self.write(f"index/{mid}.json", {"id": mid, "repo": repo_url, "path": path,
                                                 **{k: v for k, v in listing.items() if k != "id"}})
            res = self.index_check(mid, mid)
            listed = res["index"] and any(m["id"] == mid for m in res["index"]["mods"])
            if res["held"]:
                vs = ", ".join(h["version"] for h in res["held"])
                self.item(True, f"{mid}: releases found; {vs} wait for a maintainer's review "
                                "(native mods from other repositories: 24 h and an approval)")
            else:
                self.item(bool(listed) or res["index"] is None, f"{mid}: a release with its archives is found",
                          f"Push a tag {mid}-v<version>: the SDK's workflow publishes the archives and SHA256SUMS.")
        return {"id": first or (ids[0] if ids else ""), "title": f"List {', '.join(ids)}"}

    def submit_pack(self, form: dict, user: str) -> dict:
        mid = slug(form.get("name"))
        if not self.item(bool(mid) and bool(b.ID_RE.match(mid)), "The modpack has a name",
                         "Give the modpack a name (its id is made from it)."):
            return {}
        entries = self.entries()
        old = entries.get(mid)
        old_entry = old.get("entry", {}) if old else {}
        mine = old is not None and old_entry.get("type") == "pack" and (
            old_entry.get("author") == user or self.permission(*REPO_SLUG.split("/"), user) in MAINTAIN)
        self.item(old is None or mine, f"The id {mid} is free", "Pick another modpack name.")
        requires, bad = [], []
        for part in split_list(form.get("mods")):
            m = re.match(rf"^([a-z0-9-]+)(?:@(>=|=)?({b.SEMVER}))?$", part)
            if not m:
                bad.append(part)
                continue
            item = {"id": m.group(1)}
            if m.group(3):
                item["version"] = (m.group(2) or "=") + m.group(3)
            if m.group(1) not in entries or m.group(1) == mid:
                bad.append(part)
                continue
            requires.append(item)
        self.item(bool(requires) and not bad, "Every mod of the pack is in the index",
                  (f"Not found: {', '.join(md(x) for x in bad)}. " if bad else "") +
                  "List mods of the index as id or id@>=1.0.0.")
        desc = " ".join((form.get("description") or "").split())
        self.item(0 < len(desc) <= b.MAX_DESCRIPTION, "The description is 1 to 300 characters",
                  "Write a short description of the pack.")
        listing = self.listing_fields({k: v for k, v in form.items() if k not in ("description", "video")}, mid,
                                      {k: v for k, v in old_entry.items() if k in b.LISTING_KEYS})
        versions = sorted(old_entry.get("versions", []), key=lambda v: b.semver_key(v["version"]), reverse=True)
        if not versions or versions[0].get("requires") != requires:
            ver = "1.0.0"
            if versions:  # a changed pack: the next minor version
                major, minor = b.SEMVER_RE.match(versions[0]["version"]).groups()[:2]
                ver = f"{major}.{int(minor) + 1}.0"
            when = b.iso(dt.datetime.fromtimestamp(int(self.now), dt.timezone.utc))
            versions.insert(0, {"version": ver, "released": when, "requires": requires})
        if not self.ok:
            return {"id": mid, "title": f"List the modpack {mid}"}
        self.write(f"index/{mid}.json", {"id": mid, "repo": b.SOURCE, "path": "", "type": "pack",
                                         "name": " ".join(form["name"].split())[:100], "author": user,
                                         "description": desc,
                                         **{k: v for k, v in listing.items() if k not in ("id", "description")},
                                         "versions": versions})
        self.index_check(mid, f"index/{mid}.json")
        return {"id": mid, "title": f"List the modpack {mid}"}

    def owned(self, mid: str, user: str) -> dict | None:
        """The entry if the user may change it (push access to its repo; for a pack, its author)."""
        e = self.entries().get(mid)
        if not self.item(e is not None, f"{md(mid)} is in the index", "Check the mod id (as on its page)."):
            return None
        if e.get("entry", {}).get("type") == "pack":
            ok = e["entry"].get("author") == user or self.permission(*REPO_SLUG.split("/"), user) in MAINTAIN
        else:
            ok = self.permission(*b.owner_repo(e["repo"]), user) in WRITERS
        return e if self.item(ok, f"You may change {mid}",
                              "Only someone with write access to the mod's repository (or the pack's author) can.") \
            else None

    def update(self, form: dict, user: str) -> dict:
        mid = (form.get("mod") or "").strip()
        e = self.owned(mid, user)
        if e is None:
            return {"id": mid}
        if e["local"] is not None:
            rel = f"{e['path']}/listing.json"
            self.write(rel, self.listing_fields(form, mid, e["listing"]))
        else:
            rel = f"index/{mid}.json"
            cur = json.loads((self.root / rel).read_text())
            listing = self.listing_fields(form, mid, {k: v for k, v in cur.items() if k in b.LISTING_KEYS})
            self.write(rel, {**cur, **{k: v for k, v in listing.items() if k != "id"}})
        self.index_check(mid, mid)
        return {"id": mid, "title": f"Update the listing of {mid}"}

    def removal(self, form: dict, user: str) -> dict:
        mid = (form.get("mod") or "").strip()
        e = self.owned(mid, user)
        if e is None:
            return {"id": mid}
        if not self.item(e["local"] is None, f"{mid} is listed from index/",
                         "Mods kept in this repository are removed by the maintainers: ask in this issue."):
            return {"id": mid}
        self.remove(f"index/{mid}.json")
        self.remove(f"index/media/{mid}")
        self.index_check(mid, None)
        return {"id": mid, "title": f"Remove {mid} from the index"}

    def revoke(self, form: dict, user: str) -> dict | None:
        if self.permission(*REPO_SLUG.split("/"), user) not in MAINTAIN:
            return None  # maintainers only: ignored, no comment
        mid = (form.get("mod") or "").strip()
        versions = split_list(form.get("versions"))
        reason = " ".join((form.get("reason") or "").split())
        path = self.root / "index" / "revoked.json"
        current = json.loads(path.read_text()) if path.is_file() else []
        rest = [r for r in current if r.get("id") != mid]
        old = next((r for r in current if r.get("id") == mid), None)
        merged = sorted(set((old or {}).get("versions", [])) | set(versions), key=lambda v: (v != "*", b.semver_key(v)))
        if "*" in merged:
            merged = ["*"]
        new = sorted(rest + [{"id": mid, "versions": merged, "reason": reason}], key=lambda r: r["id"])
        self.write("index/revoked.json", new)
        try:
            b.load_revoked(self.root)
            self.item(True, "The revocation is valid")
        except b.EntryError as e:
            self.item(False, "The revocation is valid", md(e))
        return {"id": mid, "title": f"Revoke {mid} {', '.join(merged)}", "branch": f"revoke/{mid}"}


def check(event: dict, root: Path, http: b.Http, now: float, out: Path, vips=None) -> dict:
    """The read-only half: checks, and the files to change copied into OUT/files."""
    issue = event.get("issue") or {}
    labels = [l.get("name") for l in issue.get("labels", [])]
    result = {"issue": issue.get("number"), "skip": True}
    if event.get("action") == "labeled" and (event.get("label") or {}).get("name") not in FORMS:
        return result
    form_name = next((f for f in FORMS if f in labels), None)
    if form_name is None or issue.get("state") == "closed":
        return result
    user = (issue.get("user") or {}).get("login", "")
    form = parse_form(issue.get("body") or "", FORMS[form_name])
    bot = Bot(root, http, now, vips)
    try:
        handler = {"mod-submission": bot.submit, "listing-update": bot.update, "mod-removal": bot.removal,
                   "mod-revoke": bot.revoke}[form_name]
        meta = handler(form, user)
    except Exception:  # noqa: BLE001 - the author gets a plain message, the log the traceback
        traceback.print_exc()
        return {**result, "skip": False, "error": True, "form": form_name}
    if meta is None:
        return result
    mid = meta.get("id") or ""
    result.update({"skip": False, "form": form_name, "id": mid, "ok": bot.ok and bool(bot.changed or bot.deleted),
                   "items": bot.items, "media": bot.media_rows, "title": meta.get("title", mid),
                   "branch": meta.get("branch") or f"listing/{mid}", "changed": bot.changed,
                   "deleted": bot.deleted})
    for rel in bot.changed:
        dst = out / "files" / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / rel, dst)
    return result


# ------------------------------------------------------------------------------------------------
# The comment
# ------------------------------------------------------------------------------------------------

def size(n) -> str:
    if n is None:
        return "—"
    return f"{n / (1024 * 1024):.1f} MB" if n >= 1024 * 1024 else f"{max(1, round(n / 1024))} KB"


def render(result: dict, pr_url: str | None = None, run: str = "") -> str:
    if result.get("error"):
        return (f"{MARKER}\nSomething went wrong on our side (run #{run}). {MAINTAINERS}, please have a look. "
                "You do not need to do anything.")
    head = (f"✅ Ready: {pr_url}. A maintainer reviews and merges it; the mod is live within the hour."
            if result.get("ok") and pr_url else
            "❌ Not yet. Fix the items marked ❌ and edit this issue: the bot checks it again.")
    lines = [MARKER, f"**{md(result.get('title', ''))}**", "", head, "", "| | Check | How to fix |", "|---|---|---|"]
    for i in result.get("items", []):
        lines.append(f"| {'✅' if i['ok'] else '❌'} | {md(i['text'])} | {md(i['fix'])} |")
    rows = result.get("media") or []
    if rows:
        lines += ["", "| File | In | Out (1600w AVIF) | Saved | Notes |", "|---|---|---|---|---|"]
        for r in rows:
            saved = f"{round(100 * (1 - r['out'] / r['in']))}%" if r.get("out") and r.get("in") else "—"
            lines.append(f"| {md(r['file'])} | {size(r['in']) if r['in'] else '—'} | {size(r.get('out'))} | {saved} "
                         f"| {md('; '.join(r.get('notes') or []))} |")
    return "\n".join(lines) + "\n"


# ------------------------------------------------------------------------------------------------
# The write half: git and gh
# ------------------------------------------------------------------------------------------------

def run_cmd(args: list[str], input: str | None = None, check: bool = True) -> str:
    r = subprocess.run(args, input=input, capture_output=True, text=True, check=False)
    if check and r.returncode:
        raise subprocess.CalledProcessError(r.returncode, args, r.stdout, r.stderr)
    return r.stdout


def upsert_comment(issue: int, body: str, run=run_cmd) -> None:
    comments = json.loads(run(["gh", "api", f"repos/{{owner}}/{{repo}}/issues/{issue}/comments?per_page=100"]) or "[]")
    mine = [c for c in comments if MARKER in (c.get("body") or "")]
    if mine:
        run(["gh", "api", "-X", "PATCH", f"repos/{{owner}}/{{repo}}/issues/comments/{mine[-1]['id']}",
             "-f", f"body={body}"])
    else:
        run(["gh", "issue", "comment", str(issue), "--body-file", "-"], input=body)


def apply(result: dict, files: Path, repo: Path, run=run_cmd, run_id: str = "") -> int:
    if result.get("skip"):
        return 0
    issue = result["issue"]
    pr_url = None
    if result.get("ok"):
        for rel in result["deleted"]:
            (repo / rel).unlink(missing_ok=True)
        for rel in result["changed"]:
            if not safe_rel(rel):
                raise Unexpected(f"refusing path {rel}")
            (repo / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(files / rel, repo / rel)
        branch = result["branch"]
        git = ["git", "-C", str(repo)]
        run(["gh", "auth", "setup-git"])
        run(git + ["config", "user.name", "github-actions[bot]"])
        run(git + ["config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com"])
        run(git + ["switch", "-C", branch])
        run(git + ["add", "-A", "--", *sorted(set(result["changed"] + result["deleted"]))])
        run(git + ["commit", "-m", f"feat(index): {result['title']}"[:100]])
        run(git + ["push", "--force", "origin", f"HEAD:refs/heads/{branch}"])
        prs = json.loads(run(["gh", "pr", "list", "--head", branch, "--state", "open", "--json", "url"]) or "[]")
        body = f"From #{issue}, checked by the listing bot.\n\nCloses #{issue}"
        if prs:
            pr_url = prs[0]["url"]
        else:
            pr_url = run(["gh", "pr", "create", "--base", "master", "--head", branch, "--title",
                          result["title"][:100], "--body", body]).strip()
    upsert_comment(issue, render(result, pr_url, run_id), run)
    return 0


def safe_rel(rel: str) -> bool:
    parts = rel.split("/")
    return bool(rel) and not rel.startswith("/") and ".." not in parts and not rel.startswith(".git")


def push_master(repo: Path, paths: list[str], message: str, branch: str, run=run_cmd) -> str:
    """Commits to master; if master refuses the push (protected), opens a pull request instead."""
    git = ["git", "-C", str(repo)]
    run(["gh", "auth", "setup-git"])
    run(git + ["config", "user.name", "github-actions[bot]"])
    run(git + ["config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com"])
    run(git + ["add", "--", *paths])
    run(git + ["commit", "-m", message])
    try:
        run(git + ["push", "origin", "HEAD:refs/heads/master"])
        return "master"
    except subprocess.CalledProcessError:
        run(git + ["push", "--force", "origin", f"HEAD:refs/heads/{branch}"])
        return run(["gh", "pr", "create", "--base", "master", "--head", branch, "--title", message,
                    "--body", "Opened by the index bot: master did not take a direct push."]).strip()


# ------------------------------------------------------------------------------------------------
# Cool-down: review issues and approvals
# ------------------------------------------------------------------------------------------------

def review_title(item: dict) -> str:
    return f"Review {item['id']} {item['version']}"


def review_body(item: dict) -> str:
    fence = "~" * max(4, max((len(x) for x in re.findall(r"~+", item.get("changelog", ""))), default=0) + 1)
    rows = "\n".join(f"| {p} | `{v['asset']}` | `{v['sha256']}` | {v['size']} |"
                     for p, v in item["platforms"].items())
    return (f"A new version of an external native mod waits for review before players get it.\n\n"
            f"- Mod: `{item['id']}` ({md(item.get('name', ''))}), {item['repo']}\n"
            f"- Release: {item['repo']}/releases/tag/{item['tag']} ({item['released']})\n\n"
            f"| Platform | Archive | SHA-256 | Bytes |\n|---|---|---|---|\n{rows}\n\n"
            f"Changelog:\n\n{fence}\n{item.get('changelog', '')}\n{fence}\n\n"
            "To approve, a maintainer applies the label `approved`. It is published 24 h after its release.\n")


def review(items: list[dict], run=run_cmd) -> int:
    run(["gh", "label", "create", "mod-review", "--color", "FBCA04", "--description",
         "A mod version waits for review", "--force"], check=False)
    for item in items:
        title = review_title(item)
        found = json.loads(run(["gh", "issue", "list", "--label", "mod-review", "--state", "all", "--search",
                                f"\"{title}\" in:title", "--json", "number,title,state"]) or "[]")
        same = [i for i in found if i["title"] == title]
        body = review_body(item)
        if not same:
            run(["gh", "issue", "create", "--title", title, "--label", "mod-review", "--body-file", "-"], input=body)
        elif same[0]["state"] == "OPEN":
            run(["gh", "issue", "edit", str(same[0]["number"]), "--body-file", "-"], input=body)
    return 0


def approve(event: dict, repo: Path, http: b.Http, run=run_cmd) -> int:
    issue = event.get("issue") or {}
    labels = [l.get("name") for l in issue.get("labels", [])]
    if (event.get("label") or {}).get("name") != "approved" or "mod-review" not in labels:
        return 0
    m = re.match(rf"^Review ([a-z0-9]+(?:-[a-z0-9]+)*) ({b.SEMVER})$", issue.get("title", ""))
    if not m:
        return 0
    sender = (event.get("sender") or {}).get("login", "")
    bot = Bot(repo, http, time.time())
    num = str(issue["number"])
    if bot.permission(*REPO_SLUG.split("/"), sender) not in MAINTAIN:
        run(["gh", "issue", "comment", num, "--body", "Only maintainers can approve a version."])
        run(["gh", "issue", "edit", num, "--remove-label", "approved"], check=False)
        return 0
    mid, version = m.group(1), m.group(2)
    path = repo / "index" / "approved.json"
    data = json.loads(path.read_text()) if path.is_file() else {}
    vs = sorted(set(data.get(mid, [])) | {version}, key=b.semver_key)
    data[mid] = vs
    path.write_text(json.dumps(dict(sorted(data.items())), indent=2) + "\n")
    where = push_master(repo, ["index/approved.json"], f"chore(index): approve {mid} {version}",
                        f"approve/{mid}-{version}", run)
    if where == "master":
        run(["gh", "workflow", "run", "index.yml", "--ref", "master"], check=False)
        run(["gh", "issue", "close", num, "--comment",
             f"Approved by {sender}: published at the next index build, 24 h after its release at the earliest."])
    else:
        run(["gh", "issue", "comment", num, "--body", f"Approved by {sender}; merge {where} to publish it."])
    return 0


# ------------------------------------------------------------------------------------------------
# Discussions: one per mod, numbers in index/discussions.json
# ------------------------------------------------------------------------------------------------

CATEGORY_QUERY = """query($owner: String!, $name: String!) {
  repository(owner: $owner, name: $name) {
    id hasDiscussionsEnabled
    discussionCategories(first: 50) { nodes { id name } }
    discussions(first: 100, orderBy: {field: CREATED_AT, direction: DESC}) { nodes { number title } }
  }
}"""
CREATE = """mutation($repo: ID!, $cat: ID!, $title: String!, $body: String!) {
  createDiscussion(input: {repositoryId: $repo, categoryId: $cat, title: $title, body: $body}) {
    discussion { number }
  }
}"""


def graphql(query: str, run=run_cmd, **variables) -> dict:
    args = ["gh", "api", "graphql", "-f", f"query={query}"]
    for k, v in variables.items():
        args += ["-F" if isinstance(v, int) else "-f", f"{k}={v}"]
    return json.loads(run(args))["data"]


def discussions(index: dict, repo: Path, run=run_cmd, category: str = "Mods") -> int:
    owner, name = REPO_SLUG.split("/")
    try:
        d = graphql(CATEGORY_QUERY, run, owner=owner, name=name)["repository"]
    except (subprocess.CalledProcessError, KeyError, ValueError, TypeError) as e:
        print(f"warning: discussions: cannot read the repository ({e}); skipped", file=sys.stderr)
        return 0
    if not d.get("hasDiscussionsEnabled"):
        print("warning: Discussions are off for this repository; no Discussion made", file=sys.stderr)
        return 0
    cat = next((c["id"] for c in d["discussionCategories"]["nodes"] if c["name"] == category), None)
    if cat is None:
        print(f"warning: no Discussion category {category!r}; no Discussion made", file=sys.stderr)
        return 0
    path = repo / "index" / "discussions.json"
    numbers = json.loads(path.read_text()) if path.is_file() else {}
    existing = {}
    for n in d["discussions"]["nodes"]:
        m = re.search(r"\(([a-z0-9-]+)\)$", n["title"])
        if m:
            existing.setdefault(m.group(1), n["number"])
    changed = False
    for mod in index["mods"]:
        if mod.get("discussion") or numbers.get(mod["id"]):
            continue
        number = existing.get(mod["id"])
        if number is None:
            body = (f"Questions, ideas and feedback about **{md(mod['name'])}**. Mod page: {SITE}{mod['id']}\n\n"
                    "Bugs go to the mod's own tracker, linked on its page.")
            number = graphql(CREATE, run, repo=d["id"], cat=cat, title=f"{mod['name']} ({mod['id']})",
                             body=body)["createDiscussion"]["discussion"]["number"]
        numbers[mod["id"]] = number
        changed = True
    if changed:
        path.write_text(json.dumps(dict(sorted(numbers.items())), indent=2) + "\n")
        push_master(repo, ["index/discussions.json"], "chore(index): discussion numbers", "index/discussions", run)
    return 0


# ------------------------------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check")
    c.add_argument("--event", type=Path, required=True)
    c.add_argument("--out", type=Path, required=True)
    c.add_argument("--offline", type=Path)
    a = sub.add_parser("apply")
    a.add_argument("--event", type=Path, required=True)
    a.add_argument("--result", type=Path, required=True)
    p = sub.add_parser("approve")
    p.add_argument("--event", type=Path, required=True)
    r = sub.add_parser("review")
    r.add_argument("--file", type=Path, required=True)
    d = sub.add_parser("discussions")
    d.add_argument("--index", type=Path, required=True)
    args = ap.parse_args(argv)
    run_id = os.environ.get("GITHUB_RUN_ID", "?")
    http = b.Offline(args.offline) if getattr(args, "offline", None) else b.Http(os.environ.get("GITHUB_TOKEN"))
    if args.cmd == "check":
        event = json.loads(args.event.read_text())
        args.out.mkdir(parents=True, exist_ok=True)
        result = check(event, ROOT, http, time.time(), args.out, media.run_vips if media.have_vips() else None)
        (args.out / "result.json").write_text(json.dumps(result, indent=1, ensure_ascii=False))
        print(json.dumps({k: result.get(k) for k in ("issue", "form", "id", "ok", "skip", "error")}))
        return 0
    if args.cmd == "apply":
        event = json.loads(args.event.read_text())
        number = (event.get("issue") or {}).get("number")
        path = args.result / "result.json"
        result = json.loads(path.read_text()) if path.is_file() else {"issue": number, "error": True}
        try:
            return apply(result, args.result / "files", ROOT, run_id=run_id)
        except Exception:  # noqa: BLE001
            traceback.print_exc()
            upsert_comment(number, render({"error": True}, run=run_id))
            return 1
    if args.cmd == "approve":
        return approve(json.loads(args.event.read_text()), ROOT, http)
    if args.cmd == "review":
        return review(json.loads(args.file.read_text()) if args.file.is_file() else [])
    return discussions(json.loads(args.index.read_text()), ROOT)


if __name__ == "__main__":
    sys.exit(main())
