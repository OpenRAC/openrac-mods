#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""Writes tools/tests/fixtures/github/: a small fake repo and recorded GitHub responses.

    python3 tools/tests/make_fixtures.py

The output is the same on every run. Archive files end in .bin (the repo ignores *.zip).
"""
from __future__ import annotations

import hashlib
import io
import json
import shutil
import struct
import zipfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "fixtures" / "github"
REPO = "https://github.com/OpenRAC/rac1-mods"
API = "https://api.github.com/repos/OpenRAC/rac1-mods"
RAW = "https://raw.githubusercontent.com/OpenRAC/rac1-mods"
DL = REPO + "/releases/download"
EXT = "https://github.com/someone/ext-hud"
EXT_API = "https://api.github.com/repos/someone/ext-hud"
EXT_RAW = "https://raw.githubusercontent.com/someone/ext-hud"

routes: dict = {}


def put(rel: str, data: bytes) -> str:
    path = OUT / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return rel


def route(url: str, rel: str, data: bytes | str | dict | list, **extra) -> None:
    if isinstance(data, (dict, list)):
        data = json.dumps(data, indent=1) + "\n"
    if isinstance(data, str):
        data = data.encode()
    put(rel, data)
    routes[url] = {"file": rel, **extra} if extra else rel


def png(w: int, h: int, rgb=(200, 80, 40)) -> bytes:
    row = b"\x00" + bytes(rgb) * w
    raw = zlib.compress(row * h, 9)

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", raw) + chunk(b"IEND", b""))


def zip_bytes(files: dict[str, bytes]) -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in files.items():
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            z.writestr(info, data)
    return buf.getvalue()


def manifest(mid: str, version: str, **kw) -> dict:
    m = {"format": 2, "id": mid, "name": kw.pop("name", mid.replace("-", " ").capitalize()), "version": version,
         "author": "OpenRAC contributors", "description": f"The {mid} test mod.", "license": "GPL-3.0-or-later",
         "homepage": f"{REPO}/tree/master/{mid}", "api": kw.pop("api", "1.1"), "games": {"rac1": ["pal", "ntsc"]}}
    m.update(kw)
    return m


def release(api: str, dl: str, tag: str, published: str, assets: list[tuple[str, bytes, int]], *, body="",
            prerelease=False, digests=True, rid=1) -> dict:
    out = []
    for i, (name, data, count) in enumerate(assets):
        url = f"{dl}/{tag}/{name}"
        rel = put(f"assets/{tag}/{name}.bin", data)
        routes[url] = rel
        a = {"name": name, "size": len(data), "download_count": count, "state": "uploaded",
             "browser_download_url": url, "url": f"{api}/releases/assets/{rid * 100 + i}"}
        if digests:
            a["digest"] = "sha256:" + hashlib.sha256(data).hexdigest()
        out.append(a)
    return {"id": rid, "tag_name": tag, "name": tag, "draft": False, "prerelease": prerelease,
            "created_at": published, "published_at": published, "body": body, "assets": out}


def sums(files: list[tuple[str, bytes]], skip=()) -> bytes:
    return "".join(f"{hashlib.sha256(d).hexdigest()}  {n}\n" for n, d in files if n not in skip).encode()


def native_release(mid, ver, published, m, *, rid, skip_sum=(), prerelease=False):
    plats = ["linux-x86_64", "windows-x86_64", "macos-arm64"]
    libs = {"linux-x86_64": "so", "windows-x86_64": "dll", "macos-arm64": "dylib"}
    mj = json.dumps(m).encode()
    zips, merged = [], {"openrac-mod.json": mj}
    for p in plats:
        lib = f"lib/{p}/{mid.replace('-', '_')}.{libs[p]}"
        merged[lib] = f"{mid} {ver} {p}".encode() * 10
        zips.append((f"{mid}-{ver}-{p}.zip", zip_bytes({"openrac-mod.json": mj, lib: merged[lib]})))
    zips.append((f"{mid}-{ver}.zip", zip_bytes(merged)))
    s = sums(zips, skip_sum)
    files = [(n, d, 10 + i) for i, (n, d) in enumerate(zips)] + [("SHA256SUMS", s, 50)]
    return release(API, DL, f"{mid}-v{ver}", published, files, body=f"## {ver}\n\n- Changes for {ver}.",
                   prerelease=prerelease, rid=rid)


def script_release(mid, ver, published, m, *, rid, prerelease=False):
    mj = json.dumps(m).encode()
    z = zip_bytes({"openrac-mod.json": mj, "main.luau": b"-- test\n"})
    files = [(f"{mid}-{ver}.zip", z, 7), ("SHA256SUMS", sums([(f"{mid}-{ver}.zip", z)]), 9)]
    return release(API, DL, f"{mid}-v{ver}", published, files, body=f"## {ver}", prerelease=prerelease, rid=rid)


def main() -> None:
    if OUT.exists():
        shutil.rmtree(OUT)
    repo = OUT / "repo"
    rels = []

    # lock-strafe: native; 1.2.0 is broken (no SHA256SUMS line for one archive); 2.0.0-beta.1 is a
    # pre-release by its suffix only.
    ls = {
        "1.0.0": manifest("lock-strafe", "1.0.0", name="Lock-strafe", api="1.0"),
        "1.1.0": manifest("lock-strafe", "1.1.0", name="Lock-strafe", api="1.0",
                          requires=[{"id": "camera-base", "version": ">=1.0.0"}],
                          conflicts=[{"id": "button-swap", "reason": "both take L2 and R2"}],
                          hooks=["rac1.pad.process_report"]),
        "1.2.0": manifest("lock-strafe", "1.2.0", name="Lock-strafe", api="1.1"),
        "2.0.0-beta.1": manifest("lock-strafe", "2.0.0-beta.1", name="Lock-strafe", api="1.11"),
    }
    rels.append(native_release("lock-strafe", "1.0.0", "2026-09-01T12:00:00Z", ls["1.0.0"], rid=1))
    rels.append(native_release("lock-strafe", "1.1.0", "2026-09-15T12:00:00Z", ls["1.1.0"], rid=2))
    rels.append(native_release("lock-strafe", "1.2.0", "2026-10-01T12:00:00Z", ls["1.2.0"], rid=3,
                               skip_sum=("lock-strafe-1.2.0-windows-x86_64.zip",)))
    rels.append(native_release("lock-strafe", "2.0.0-beta.1", "2026-10-20T12:00:00Z", ls["2.0.0-beta.1"], rid=4))
    head = dict(ls["1.1.0"], version="2.0.0-beta.1",
                description="Strafe as in the later games. " + "Hold L2 or R2 to lock your facing. " * 12)
    (repo / "lock-strafe").mkdir(parents=True)
    (repo / "lock-strafe" / "openrac-mod.json").write_text(json.dumps(head, indent=1))
    (repo / "lock-strafe" / "media").mkdir()
    (repo / "lock-strafe" / "media" / "icon.png").write_bytes(png(300, 300))
    (repo / "lock-strafe" / "media" / "shot1.png").write_bytes(png(1920, 1080, (30, 90, 160)))
    (repo / "lock-strafe" / "listing.json").write_text(json.dumps({
        "tags": ["gameplay", "controls"], "impact": ["gameplay"], "recommends": ["camera-base"],
        "suggests": ["ext-hud"], "featured": True,
        "links": {"discord": "https://discord.gg/example"},
        "media": {"icon": "media/icon.png", "screenshots": [{"src": "media/shot1.png", "caption": "Strafing"}]},
    }, indent=1))
    for tag in ("lock-strafe-v1.0.0", "lock-strafe-v1.1.0", "lock-strafe-v2.0.0-beta.1"):
        ver = tag.split("-v", 1)[1]
        route(f"{RAW}/{tag}/lock-strafe/openrac-mod.json", f"raw/{tag}/lock-strafe/openrac-mod.json", ls[ver])
    # 1.2.0's manifest at the tag is fine; its SHA256SUMS lacks a line.
    route(f"{RAW}/lock-strafe-v1.2.0/lock-strafe/openrac-mod.json",
          "raw/lock-strafe-v1.2.0/lock-strafe/openrac-mod.json", ls["1.2.0"])
    route(f"{RAW}/lock-strafe-v2.0.0-beta.1/lock-strafe/README.md",
          "raw/lock-strafe-v2.0.0-beta.1/lock-strafe/README.md", "# Lock-strafe\n\nStrafe as in the later games.\n")

    # button-swap: script; 1.1.0 is a pre-release by the release flag.
    bs = {v: manifest("button-swap", v, type="script", scripts=["main.luau"]) for v in ("1.0.0", "1.1.0")}
    rels.append(script_release("button-swap", "1.0.0", "2026-08-01T12:00:00Z", bs["1.0.0"], rid=5))
    rels.append(script_release("button-swap", "1.1.0", "2026-10-25T12:00:00Z", bs["1.1.0"], rid=6, prerelease=True))
    (repo / "button-swap").mkdir()
    (repo / "button-swap" / "openrac-mod.json").write_text(json.dumps(bs["1.1.0"], indent=1))
    (repo / "button-swap" / "listing.json").write_text(json.dumps({
        "description": "Swaps two buttons.", "tags": ["controls", "accessibility"], "impact": []}, indent=1))
    for v in bs:
        route(f"{RAW}/button-swap-v{v}/button-swap/openrac-mod.json",
              f"raw/button-swap-v{v}/button-swap/openrac-mod.json", bs[v])

    # camera-base: a data pack of the level editor (format 1), its folder named differently.
    cb = {"format": 1, "id": "camera-base", "name": "Camera base", "version": "1.0.0",
          "author": "OpenRAC contributors", "description": "Shared camera data.", "games": {"rac1": ["pal"]}}
    z = zip_bytes({"openrac-mod.json": json.dumps(cb).encode(), "data/camera.bin": b"\x00" * 16})
    rels.append(release(API, DL, "camera-base-v1.0.0", "2026-07-01T12:00:00Z",
                        [("camera-base-1.0.0.zip", z, 3), ("SHA256SUMS", sums([("camera-base-1.0.0.zip", z)]), 3)],
                        rid=7))
    (repo / "camera-data").mkdir()
    (repo / "camera-data" / "openrac-mod.json").write_text(json.dumps(cb, indent=1))
    route(f"{RAW}/camera-base-v1.0.0/camera-data/openrac-mod.json",
          "raw/camera-base-v1.0.0/camera-data/openrac-mod.json", cb)

    # The index's own pre-release, and a release of a mod that is not here: both ignored.
    rels.append({"id": 8, "tag_name": "index", "name": "index", "draft": False, "prerelease": True,
                 "created_at": "2026-10-30T00:00:00Z", "published_at": "2026-10-30T00:00:00Z", "body": "",
                 "assets": []})
    rels.sort(key=lambda r: r["published_at"], reverse=True)
    route(f"{API}/releases?per_page=100&page=1", "api/rac1-mods/releases-1.json", rels)

    # An external mod, its own repo, released with v<version> tags.
    ext = manifest("ext-hud", "0.2.0", name="Extra HUD", api="1.2", homepage=EXT)
    z_lin = zip_bytes({"openrac-mod.json": json.dumps(ext).encode(), "lib/linux-x86_64/ext_hud.so": b"elf" * 30})
    ext_rel = release(EXT_API, EXT + "/releases/download", "v0.2.0", "2026-10-10T12:00:00Z",
                      [("ext-hud-0.2.0-linux-x86_64.zip", z_lin, 40),
                       ("SHA256SUMS", sums([("ext-hud-0.2.0-linux-x86_64.zip", z_lin)]), 40)], rid=20)
    route(f"{EXT_API}/releases?per_page=100&page=1", "api/ext-hud/releases-1.json", [ext_rel])
    route(f"{EXT_RAW}/HEAD/openrac-mod.json", "raw/ext-hud/HEAD/openrac-mod.json", ext)
    route(f"{EXT_RAW}/v0.2.0/openrac-mod.json", "raw/ext-hud/v0.2.0/openrac-mod.json", ext)
    route(f"{EXT_RAW}/v0.2.0/README.md", "raw/ext-hud/v0.2.0/README.md", "# Extra HUD\n")
    (repo / "index").mkdir()
    (repo / "index" / "ext-hud.json").write_text(json.dumps({
        "id": "ext-hud", "repo": EXT, "path": "", "tags": ["ui"], "impact": ["cosmetic"],
        "media": {"videos": [{"youtube": "https://youtu.be/dQw4w9WgXcQ", "caption": "Trailer"},
                             {"youtube": "https://www.youtube.com/watch?v=aaaaaaaaaaa", "caption": "Gone"}]},
    }, indent=1))
    oembed = "https://www.youtube.com/oembed?url=https%3A%2F%2Fwww.youtube.com%2Fwatch%3Fv%3D"
    route(oembed + "dQw4w9WgXcQ&format=json", "youtube/oembed-ok.json", {"title": "Extra HUD trailer"})
    routes[oembed + "aaaaaaaaaaa&format=json"] = {"status": 401}
    route("https://www.youtube.com/watch?v=dQw4w9WgXcQ", "youtube/watch.html",
          '<meta itemprop="uploadDate" content="2026-09-01T00:00:00-07:00">')

    # A pack: only requires.
    (repo / "index" / "starter-pack.json").write_text(json.dumps({
        "id": "starter-pack", "repo": REPO, "path": "", "type": "pack", "name": "Starter pack",
        "author": "Community", "description": "Lock-strafe and button swap together.", "tags": ["controls"],
        "versions": [{"version": "1.0.0", "released": "2026-10-30T18:00:00Z",
                      "requires": [{"id": "lock-strafe", "version": ">=1.1.0"}, {"id": "button-swap"}]}],
    }, indent=1))
    (repo / "index" / "approved.json").write_text(json.dumps({"ext-hud": ["0.2.0"]}, indent=1))
    (repo / "index" / "discussions.json").write_text(json.dumps({"lock-strafe": 7}, indent=1))
    (repo / "index" / "revoked.json").write_text(json.dumps(
        [{"id": "lock-strafe", "versions": ["1.0.0"], "reason": "Broke saves."}], indent=1))

    # The ports: each channel's latest.json and the header at its tag or branch.
    port = "https://github.com/OpenRAC/OpenRAC/releases"
    route(f"{port}/latest/download/latest.json", "ports/stable-latest.json", {"version": "0.10.1"})
    route(f"{port}/download/launcher-dev-develop/latest.json", "ports/dev-latest.json", {"version": "0.11.0-dev.42"})
    hdr = "#define OPENRAC_MOD_API_MAJOR 1\n/* comment */\n#define OPENRAC_MOD_API_MINOR {}\n"
    praw = "https://raw.githubusercontent.com/OpenRAC/OpenRAC"
    route(f"{praw}/launcher-v0.10.1/sdk/include/openrac/openrac_mod.h", "ports/openrac_mod-stable.h", hdr.format(10))
    route(f"{praw}/develop/sdk/include/openrac/openrac_mod.h", "ports/openrac_mod-dev.h", hdr.format(11))

    # Last build's stats, 7 days before 2026-10-31.
    route(f"{DL}/index/stats.json", "index-release/stats.json",
          {"format": 1, "days": {"2026-10-24": {"lock-strafe": 100, "button-swap": 10}}})
    # Another game's index, for MOD_INDEXES.
    route("https://example.org/rac2/index.json", "other-index.json", {"format": 1, "mods": [{"id": "rac2-only"}]})

    (OUT / "routes.json").write_text(json.dumps(dict(sorted(routes.items())), indent=1) + "\n")


if __name__ == "__main__":
    main()
