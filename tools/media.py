#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""Pictures and videos of the mod index: check them, shrink them, name them.

    python3 tools/media.py process OUT_DIR

reads OUT_DIR/media-src/requests.json (written by build_index.py), turns each picture into a
width ladder of AVIF and WebP files named <sha256[:12]>-<width>.<ext> in OUT_DIR/media/, and fills
the pictures into OUT_DIR/index.json. It needs no network. Without the `vips` CLI the pictures are
skipped, with a message, and the index stays valid (no pictures).

Also used by build_index.py: the type from the magic bytes, the size from the header, the caps,
and the YouTube checks. Stdlib only.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import math
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.parse
import zlib
from pathlib import Path

MAX_BYTES = 15 * 1024 * 1024  # a source picture, before download
MAX_SIDE = 8192
MAX_PIXELS = 40_000_000
TIMEOUT = 60  # seconds for each vips call

# The width ladder of each kind of picture.
LADDER = {
    "icon": [64, 128, 256],
    "screenshot": [480, 960, 1600, 2400],
    "banner": [960, 1600, 2560],
}
BANNER_RATIO = 21 / 9
# Budgets in bytes at 1600 px wide; over it, the encoder steps down.
BUDGET = {"screenshot": 250 * 1024, "banner": 300 * 1024}
BUDGET_WIDTH = 1600
AVIF_Q, AVIF_FLOOR = 55, 45
WEBP_Q, WEBP_FLOOR = 78, 65
Q_STEP = 5


class MediaError(Exception):
    """A picture or video that is refused, with the reason for the author."""


# ------------------------------------------------------------------------------------------------
# Intake: type and size from the bytes
# ------------------------------------------------------------------------------------------------

def sniff(data: bytes) -> str:
    """The picture's type from its first bytes: png, jpeg, webp, avif or gif. Others are refused."""
    if data.startswith(b"\x89PNG\r\n\x1a\n"):
        return "png"
    if data.startswith(b"\xff\xd8\xff"):
        return "jpeg"
    if data[:4] == b"RIFF" and data[8:12] == b"WEBP":
        return "webp"
    if data[:6] in (b"GIF87a", b"GIF89a"):
        return "gif"
    if data[4:8] == b"ftyp":
        size = struct.unpack(">I", data[:4])[0]
        brands = [data[8:12]] + [data[i:i + 4] for i in range(16, min(size, len(data), 64), 4)]
        if b"avif" in brands or b"avis" in brands:
            return "avif"
    head = data[:512].lstrip().lower()
    if head.startswith(b"<?xml") or head.startswith(b"<svg") or b"<svg" in head:
        raise MediaError("SVG is not allowed (it can carry scripts): use PNG")
    raise MediaError("not a PNG, JPEG, WebP, AVIF or GIF picture")


def dimensions(data: bytes, kind: str | None = None) -> tuple[int, int]:
    """Width and height read from the header, without decoding the picture."""
    kind = kind or sniff(data)
    try:
        w, h = _DIMS[kind](data)
    except (struct.error, IndexError, ValueError) as e:
        raise MediaError(f"the {kind.upper()} header cannot be read ({e})") from None
    if w <= 0 or h <= 0:
        raise MediaError(f"the picture says it is {w}x{h}")
    return w, h


def _png_dims(d: bytes) -> tuple[int, int]:
    if d[12:16] != b"IHDR":
        raise ValueError("no IHDR")
    return struct.unpack(">II", d[16:24])


def _gif_dims(d: bytes) -> tuple[int, int]:
    if len(d) < 10:
        raise ValueError("too short")
    return struct.unpack("<HH", d[6:10])


def _jpeg_dims(d: bytes) -> tuple[int, int]:
    i = 2
    while i + 4 <= len(d):
        if d[i] != 0xFF:
            raise ValueError("bad marker")
        marker = d[i + 1]
        if marker == 0xFF:
            i += 1
            continue
        if marker in (0xD8, 0x01) or 0xD0 <= marker <= 0xD7:
            i += 2
            continue
        length = struct.unpack(">H", d[i + 2:i + 4])[0]
        # SOF0..SOF15, but not DHT (C4), JPG (C8) or DAC (CC).
        if 0xC0 <= marker <= 0xCF and marker not in (0xC4, 0xC8, 0xCC):
            if i + 9 > len(d):
                raise ValueError("truncated")
            h, w = struct.unpack(">HH", d[i + 5:i + 9])
            return w, h
        if marker == 0xDA:
            break
        i += 2 + length
    raise ValueError("no frame header (truncated?)")


def _webp_dims(d: bytes) -> tuple[int, int]:
    chunk = d[12:16]
    if chunk == b"VP8X":
        w = 1 + int.from_bytes(d[24:27], "little")
        h = 1 + int.from_bytes(d[27:30], "little")
        return w, h
    if chunk == b"VP8L":
        if d[20] != 0x2F:
            raise ValueError("bad VP8L")
        b = int.from_bytes(d[21:25], "little")
        return (b & 0x3FFF) + 1, ((b >> 14) & 0x3FFF) + 1
    if chunk == b"VP8 ":
        if d[23:26] != b"\x9d\x01\x2a":
            raise ValueError("bad VP8")
        w, h = struct.unpack("<HH", d[26:30])
        return w & 0x3FFF, h & 0x3FFF
    raise ValueError("unknown WebP chunk")


def _avif_dims(d: bytes) -> tuple[int, int]:
    # The first 'ispe' property box (inside meta/iprp/ipco) holds the size.
    i = d.find(b"ispe")
    if i < 4:
        raise ValueError("no ispe box")
    return struct.unpack(">II", d[i + 8:i + 16])


_DIMS = {"png": _png_dims, "gif": _gif_dims, "jpeg": _jpeg_dims, "webp": _webp_dims, "avif": _avif_dims}


def gif_frames(d: bytes) -> int:
    """How many frames a GIF has (1 for a still one)."""
    i = 13
    flags = d[10]
    if flags & 0x80:
        i += 3 * (2 << (flags & 7))
    frames = 0
    while i < len(d):
        b = d[i]
        if b == 0x3B:
            break
        if b == 0x21:  # extension: label, then sub-blocks
            i += 2
        elif b == 0x2C:  # image: descriptor, colour table, LZW size, sub-blocks
            frames += 1
            f = d[i + 9]
            i += 10
            if f & 0x80:
                i += 3 * (2 << (f & 7))
            i += 1
        else:
            break
        while i < len(d) and d[i]:
            i += d[i] + 1
        i += 1
    return max(frames, 1)


def check_source(data: bytes) -> dict:
    """The intake checks of one picture: its type, its size, the caps. Returns what it found."""
    if len(data) > MAX_BYTES:
        raise MediaError(f"{len(data) // (1024 * 1024)} MB is over the {MAX_BYTES // (1024 * 1024)} MB cap")
    kind = sniff(data)
    w, h = dimensions(data, kind)
    if w > MAX_SIDE or h > MAX_SIDE:
        raise MediaError(f"{w}x{h} is over {MAX_SIDE} px on a side")
    if w * h > MAX_PIXELS:
        raise MediaError(f"{w}x{h} is over {MAX_PIXELS // 1_000_000} megapixels")
    notes = []
    if kind == "gif" and gif_frames(data) > 1:
        notes.append("animated; only its first frame is kept. Put gameplay footage in a YouTube video.")
    return {"type": kind, "width": w, "height": h, "notes": notes}


# ------------------------------------------------------------------------------------------------
# The width ladder
# ------------------------------------------------------------------------------------------------

def crop_size(kind: str, w: int, h: int) -> tuple[int, int]:
    """The largest crop of a w x h picture for this kind (square, 21:9 or as it is)."""
    if kind == "icon":
        s = min(w, h)
        return s, s
    if kind == "banner":
        if w / h > BANNER_RATIO:
            return round(h * BANNER_RATIO), h
        return w, round(w / BANNER_RATIO)
    return w, h


def ladder(kind: str, w: int, h: int) -> list[tuple[int, int]]:
    """The (width, height) of each output: the ladder's widths the source can fill, no upscaling.
    A source smaller than the first step keeps its own width."""
    cw, ch = crop_size(kind, w, h)
    out = []
    for step in LADDER[kind]:
        if step <= cw:
            out.append((step, max(1, round(step * ch / cw))))
    return out or [(cw, ch)]


def media_name(digest: str, width: int, ext: str) -> str:
    return f"{digest}-{width}.{ext}"


# ------------------------------------------------------------------------------------------------
# PNG reading (for the ThumbHash and the output sizes) and ThumbHash
# ------------------------------------------------------------------------------------------------

def read_png(data: bytes) -> tuple[int, int, bytes]:
    """Decodes an 8-bit PNG (grey, RGB, palette, with or without alpha) to RGBA."""
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise MediaError("not a PNG")
    i, idat, palette, trns = 8, [], b"", b""
    w = h = depth = ctype = interlace = 0
    while i < len(data):
        length, tag = struct.unpack(">I4s", data[i:i + 8])
        body = data[i + 8:i + 8 + length]
        if tag == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif tag == b"PLTE":
            palette = body
        elif tag == b"tRNS":
            trns = body
        elif tag == b"IDAT":
            idat.append(body)
        elif tag == b"IEND":
            break
        i += 12 + length
    if depth != 8 or interlace:
        raise MediaError("only 8-bit, non-interlaced PNG is read here")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(b"".join(idat))
    stride = w * channels
    rows, prev, pos = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for x in range(stride):
            a = line[x - channels] if x >= channels else 0
            b = prev[x]
            c = prev[x - channels] if x >= channels else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pred) & 255
        rows.append(bytes(line))
        prev = line
    out = bytearray()
    for line in rows:
        for x in range(w):
            px = line[x * channels:(x + 1) * channels]
            if ctype == 0:
                out += bytes((px[0], px[0], px[0], 255))
            elif ctype == 2:
                out += px + b"\xff"
            elif ctype == 3:
                k = px[0]
                out += palette[3 * k:3 * k + 3] + bytes((trns[k] if k < len(trns) else 255,))
            elif ctype == 4:
                out += bytes((px[0], px[0], px[0], px[1]))
            else:
                out += px
    return w, h, bytes(out)


def _round(x: float) -> int:
    # JavaScript's Math.round, which the reference encoder uses.
    return math.floor(x + 0.5)


def thumbhash(w: int, h: int, rgba: bytes) -> bytes:
    """ThumbHash of a picture of at most 100x100 (evanw/thumbhash, rgbaToThumbHash)."""
    if w > 100 or h > 100:
        raise ValueError(f"{w}x{h} does not fit in 100x100")
    n = w * h
    avg_r = avg_g = avg_b = avg_a = 0.0
    for i in range(n):
        alpha = rgba[4 * i + 3] / 255
        avg_r += alpha / 255 * rgba[4 * i]
        avg_g += alpha / 255 * rgba[4 * i + 1]
        avg_b += alpha / 255 * rgba[4 * i + 2]
        avg_a += alpha
    if avg_a:
        avg_r /= avg_a
        avg_g /= avg_a
        avg_b /= avg_a
    has_alpha = avg_a < n
    l_limit = 5 if has_alpha else 7
    lx = max(1, _round(l_limit * w / max(w, h)))
    ly = max(1, _round(l_limit * h / max(w, h)))
    lum, p, q, a = [0.0] * n, [0.0] * n, [0.0] * n, [0.0] * n
    for i in range(n):
        alpha = rgba[4 * i + 3] / 255
        r = avg_r * (1 - alpha) + alpha / 255 * rgba[4 * i]
        g = avg_g * (1 - alpha) + alpha / 255 * rgba[4 * i + 1]
        b = avg_b * (1 - alpha) + alpha / 255 * rgba[4 * i + 2]
        lum[i] = (r + g + b) / 3
        p[i] = (r + g) / 2 - b
        q[i] = r - g
        a[i] = alpha

    def encode(channel, nx, ny):
        dc, ac, scale = 0.0, [], 0.0
        for cy in range(ny):
            cx = 0
            while cx * ny < nx * (ny - cy):
                fx = [math.cos(math.pi / w * cx * (x + 0.5)) for x in range(w)]
                f = 0.0
                for y in range(h):
                    fy = math.cos(math.pi / h * cy * (y + 0.5))
                    row = y * w
                    for x in range(w):
                        f += channel[x + row] * fx[x] * fy
                f /= n
                if cx or cy:
                    ac.append(f)
                    scale = max(scale, abs(f))
                else:
                    dc = f
                cx += 1
        if scale:
            ac = [0.5 + 0.5 / scale * v for v in ac]
        return dc, ac, scale

    l_dc, l_ac, l_scale = encode(lum, max(3, lx), max(3, ly))
    p_dc, p_ac, p_scale = encode(p, 3, 3)
    q_dc, q_ac, q_scale = encode(q, 3, 3)
    if has_alpha:
        a_dc, a_ac, a_scale = encode(a, 5, 5)
    landscape = w > h
    header24 = (_round(63 * l_dc) | (_round(31.5 + 31.5 * p_dc) << 6) | (_round(31.5 + 31.5 * q_dc) << 12)
                | (_round(31 * l_scale) << 18) | (int(has_alpha) << 23))
    header16 = ((ly if landscape else lx) | (_round(63 * p_scale) << 3) | (_round(63 * q_scale) << 9)
                | (int(landscape) << 15))
    out = [header24 & 255, (header24 >> 8) & 255, header24 >> 16, header16 & 255, header16 >> 8]
    if has_alpha:
        out.append(_round(15 * a_dc) | (_round(15 * a_scale) << 4))
    start, k = len(out), 0
    for ac in ([l_ac, p_ac, q_ac, a_ac] if has_alpha else [l_ac, p_ac, q_ac]):
        for f in ac:
            idx = start + (k >> 1)
            while len(out) <= idx:
                out.append(0)
            out[idx] |= _round(15 * f) << ((k & 1) << 2)
            k += 1
    return bytes(out)


# ------------------------------------------------------------------------------------------------
# vips
# ------------------------------------------------------------------------------------------------

def run_vips(args: list[str]) -> None:
    """Runs the vips CLI with a timeout. Tests swap this for a fake."""
    subprocess.run(["vips", *args], check=True, timeout=TIMEOUT, stdout=subprocess.DEVNULL,
                   stderr=subprocess.PIPE)


def have_vips() -> bool:
    return shutil.which("vips") is not None


def load_spec(path: Path, kind: str) -> str:
    # GIF and WebP may be animated: load only the first frame.
    return f"{path}[n=1]" if kind in ("gif", "webp") else str(path)


def process_image(data: bytes, kind: str, out_dir: Path, run=run_vips) -> dict:
    """One picture through the pipeline. Returns the index's Image (without alt) and a report."""
    info = check_source(data)
    out_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="openrac-media-") as tmp:
        tmp = Path(tmp)
        src = tmp / f"source.{info['type']}"
        src.write_bytes(data)
        # Rotated by EXIF, in sRGB, full size: the normalized source the name comes from.
        norm = tmp / "norm.png"
        run(["thumbnail", load_spec(src, info["type"]), str(norm), str(MAX_SIDE), "--height", str(MAX_SIDE),
             "--size", "down"])
        norm_bytes = norm.read_bytes()
        nw, nh = dimensions(norm_bytes, "png")
        digest = hashlib.sha256(kind.encode() + b"\0" + norm_bytes).hexdigest()[:12]
        widths, sizes, largest = [], {}, None
        for w, h in ladder(kind, nw, nh):
            step = tmp / f"{w}.png"
            if kind == "screenshot":
                run(["thumbnail", str(norm), str(step), str(w), "--height", str(MAX_SIDE * 4), "--size", "down"])
            else:
                run(["thumbnail", str(norm), str(step), str(w), "--height", str(h), "--crop", "centre",
                     "--size", "down"])
            budget = BUDGET.get(kind) if w == BUDGET_WIDTH else None
            for ext, q, floor, opts in (("avif", AVIF_Q, AVIF_FLOOR, ",effort=4"), ("webp", WEBP_Q, WEBP_FLOOR, "")):
                target = out_dir / media_name(digest, w, ext)
                while True:
                    run(["copy", str(step), f"{target}[Q={q}{opts},strip]"])
                    size = target.stat().st_size
                    if budget is None or size <= budget or q - Q_STEP < floor:
                        break
                    q -= Q_STEP
                sizes[target.name] = size
                if budget is not None and size > budget:
                    info["notes"].append(f"{target.name} is {size // 1024} KB, over the {budget // 1024} KB budget")
            widths.append(w)
            largest = step
        lw, lh = dimensions(largest.read_bytes(), "png")
        small = tmp / "thumb.png"
        run(["thumbnail", str(largest), str(small), "100", "--height", "100", "--size", "down"])
        tw, th, rgba = read_png(small.read_bytes())
        image = {
            "hash": digest,
            "widths": widths,
            "width": lw,
            "height": lh,
            "thumbhash": base64.b64encode(thumbhash(tw, th, rgba)).decode(),
        }
    return {"image": image, "in": len(data), "out": sizes, "notes": info["notes"]}


# ------------------------------------------------------------------------------------------------
# YouTube
# ------------------------------------------------------------------------------------------------

YOUTUBE_ID = re.compile(r"^[A-Za-z0-9_-]{11}$")
YOUTUBE_HOSTS = {"youtube.com", "www.youtube.com", "m.youtube.com", "music.youtube.com",
                 "youtube-nocookie.com", "www.youtube-nocookie.com"}


def youtube_id(value: str) -> str:
    """The 11-character id from an id or any YouTube URL form (watch, youtu.be, shorts, embed, live)."""
    value = (value or "").strip()
    if YOUTUBE_ID.match(value):
        return value
    if "://" not in value:
        value = "https://" + value
    u = urllib.parse.urlsplit(value)
    host = (u.hostname or "").lower()
    parts = [p for p in u.path.split("/") if p]
    cand = None
    if host in ("youtu.be", "www.youtu.be") and parts:
        cand = parts[0]
    elif host in YOUTUBE_HOSTS:
        if parts[:1] == ["watch"]:
            cand = (urllib.parse.parse_qs(u.query).get("v") or [None])[0]
        elif len(parts) >= 2 and parts[0] in ("shorts", "embed", "live", "v", "e"):
            cand = parts[1]
    if cand and YOUTUBE_ID.match(cand):
        return cand
    raise MediaError(f"not a YouTube video: {value[:80]}")


def youtube_check(video_id: str, fetch) -> dict | None:
    """oEmbed: the video's title if it exists and allows embedding, else None.
    `fetch(url)` returns (status, bytes)."""
    url = "https://www.youtube.com/oembed?" + urllib.parse.urlencode(
        {"url": f"https://www.youtube.com/watch?v={video_id}", "format": "json"})
    status, body = fetch(url)
    if status != 200 or not body:
        return None
    try:
        data = json.loads(body)
    except ValueError:
        return None
    return {"title": str(data.get("title") or "")[:200]}


def youtube_uploaded(video_id: str, fetch) -> str | None:
    """The upload date (YYYY-MM-DD) from the watch page, when it can be read."""
    status, body = fetch(f"https://www.youtube.com/watch?v={video_id}")
    if status != 200 or not body:
        return None
    m = re.search(rb'itemprop="uploadDate" content="(\d{4}-\d{2}-\d{2})', body) or \
        re.search(rb'"uploadDate":"(\d{4}-\d{2}-\d{2})', body)
    return m.group(1).decode() if m else None


# ------------------------------------------------------------------------------------------------
# The media job: requests.json in, pictures out, index.json filled
# ------------------------------------------------------------------------------------------------

def process_out(out: Path, run=run_vips, vips_present=None) -> int:
    """Fills OUT/index.json's pictures from OUT/media-src. Returns the exit code."""
    index_path = out / "index.json"
    index = json.loads(index_path.read_text())
    req_path = out / "media-src" / "requests.json"
    requests = json.loads(req_path.read_text()) if req_path.exists() else []
    cache_path = out / "media-src" / "cache.json"
    cache = json.loads(cache_path.read_text()) if cache_path.exists() else {}
    existing_path = out / "media-src" / "existing.json"
    existing = set(json.loads(existing_path.read_text())) if existing_path.exists() else set()
    if vips_present is None:
        vips_present = have_vips()
    media_dir = out / "media"
    mods = {m["id"]: m for m in index["mods"]}
    new_cache, report, skipped = {}, [], 0
    for r in requests:
        mod = mods.get(r["mod"])
        if mod is None:
            continue
        data = (out / "media-src" / r["file"]).read_bytes()
        key = r["kind"] + ":" + hashlib.sha256(data).hexdigest()
        image = None
        hit = cache.get(key)
        if hit and all(media_name(hit["hash"], w, e) in existing for w in hit["widths"] for e in ("avif", "webp")):
            image = dict(hit)
        elif vips_present:
            try:
                res = process_image(data, r["kind"], media_dir, run)
                image = res["image"]
                report.append({"mod": r["mod"], "src": r["src"], "in": res["in"], "out": res["out"],
                               "notes": res["notes"]})
            except (MediaError, subprocess.SubprocessError, OSError) as e:
                print(f"media: {r['mod']}: {r['src']}: {e}", file=sys.stderr)
                report.append({"mod": r["mod"], "src": r["src"], "error": str(e)})
                continue
        else:
            skipped += 1
            continue
        new_cache[key] = {k: image[k] for k in ("hash", "widths", "width", "height", "thumbhash")}
        image["alt"] = r["alt"]
        media = mod["media"]
        if r["kind"] == "screenshot":
            image["caption"] = r.get("caption", "")
            media["screenshots"].append((r["index"], image))
        else:
            media[r["kind"]] = image
    for mod in mods.values():
        shots = mod["media"]["screenshots"]
        if shots and isinstance(shots[0], (list, tuple)):
            mod["media"]["screenshots"] = [img for _, img in sorted(shots, key=lambda t: t[0])]
    if skipped:
        print(f"media: vips is not installed; {skipped} picture(s) skipped. Install libvips-tools to make them.",
              file=sys.stderr)
    index_path.write_text(json.dumps(index, indent=1, ensure_ascii=False) + "\n")
    (out / "media-cache.json").write_text(json.dumps(dict(sorted(new_cache.items())), indent=1) + "\n")
    (out / "media-report.json").write_text(json.dumps(report, indent=1) + "\n")
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("process", help="make the pictures of OUT_DIR and fill its index.json")
    p.add_argument("out", type=Path)
    c = sub.add_parser("check", help="the intake checks of one picture file")
    c.add_argument("file", type=Path)
    args = ap.parse_args(argv)
    if args.cmd == "check":
        try:
            print(json.dumps(check_source(args.file.read_bytes())))
        except MediaError as e:
            print(f"{args.file}: {e}", file=sys.stderr)
            return 1
        return 0
    return process_out(args.out)


if __name__ == "__main__":
    sys.exit(main())
