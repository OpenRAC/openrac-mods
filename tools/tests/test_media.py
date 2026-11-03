# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""tools/media.py: intake checks, the ladder, names, ThumbHash, YouTube. vips is faked."""
from __future__ import annotations

import base64
import hashlib
import json
import re
import shutil
import struct
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))
import build_index  # noqa: E402
import media  # noqa: E402
from make_fixtures import png  # noqa: E402

FIXTURE = HERE / "fixtures" / "github"
NOW = 1793440800  # 2026-10-31T10:00:00Z


def png_header(w: int, h: int) -> bytes:
    return b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) + b"IHDR" + struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)


def gif(w: int, h: int, frames: int) -> bytes:
    b = b"GIF89a" + struct.pack("<HHBBB", w, h, 0x80, 0, 0) + b"\x00\x00\x00\xff\xff\xff"
    for _ in range(frames):
        b += b"\x21\xf9\x04\x00\x0a\x00\x00\x00"
        b += b"\x2c" + struct.pack("<HHHHB", 0, 0, w, h, 0) + b"\x02\x02\x4c\x01\x00"
    return b + b";"


def jpeg(w: int, h: int) -> bytes:
    app0 = b"\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00"
    sof = b"\xff\xc0\x00\x11\x08" + struct.pack(">HH", h, w) + b"\x03\x01\x22\x00\x02\x11\x01\x03\x11\x01"
    return b"\xff\xd8" + app0 + sof + b"\xff\xda\x00\x08" + b"\x00" * 64 + b"\xff\xd9"


def webp_vp8x(w: int, h: int) -> bytes:
    body = b"VP8X" + struct.pack("<I", 10) + b"\x00\x00\x00\x00" + (w - 1).to_bytes(3, "little") \
        + (h - 1).to_bytes(3, "little")
    return b"RIFF" + struct.pack("<I", 4 + len(body)) + b"WEBP" + body


def avif(w: int, h: int) -> bytes:
    ftyp = b"\x00\x00\x00\x1cftypavif\x00\x00\x00\x00avifmif1miaf"
    ispe = struct.pack(">I", 20) + b"ispe" + b"\x00\x00\x00\x00" + struct.pack(">II", w, h)
    return ftyp + b"\x00\x00\x00\x2cmeta" + ispe


class FakeVips:
    """Stands in for the vips CLI: thumbnail writes a PNG of the size vips would make, copy writes
    bytes whose size `size(width, ext, q)` decides."""

    def __init__(self, size=lambda w, ext, q: 1000 + w):
        self.calls: list[list[str]] = []
        self.size = size

    def __call__(self, args):
        self.calls.append(list(args))
        if args[0] == "thumbnail":
            src = re.sub(r"\[.*\]$", "", args[1])
            data = Path(src).read_bytes()
            iw, ih = media.dimensions(data)
            bw, bh = int(args[3]), int(args[args.index("--height") + 1])
            if "--crop" in args:
                ow, oh = min(bw, iw), min(bh, ih)
            else:
                s = min(bw / iw, bh / ih, 1.0)
                ow, oh = max(1, round(iw * s)), max(1, round(ih * s))
            shade = hashlib.sha256(data).digest()[0]
            Path(args[2]).write_bytes(png(ow, oh, (shade, 100, 200 - shade // 2)))
        elif args[0] == "copy":
            target, opts = re.match(r"^(.*)\[(.*)\]$", args[2]).groups()
            q = int(re.search(r"Q=(\d+)", opts).group(1))
            w, _ = media.dimensions(Path(args[1]).read_bytes())
            Path(target).write_bytes(b"x" * self.size(w, target.rsplit(".", 1)[1], q))
        else:
            raise AssertionError(f"unexpected vips call {args}")


class Intake(unittest.TestCase):
    def test_magic_bytes(self):
        self.assertEqual(media.sniff(png(2, 2)), "png")
        self.assertEqual(media.sniff(jpeg(10, 10)), "jpeg")
        self.assertEqual(media.sniff(webp_vp8x(10, 10)), "webp")
        self.assertEqual(media.sniff(avif(10, 10)), "avif")
        self.assertEqual(media.sniff(gif(4, 4, 1)), "gif")

    def test_svg_and_junk_refused(self):
        for data in (b"<svg xmlns='http://www.w3.org/2000/svg'/>", b'<?xml version="1.0"?><svg/>',
                     b"  \n<svg>", b"%PDF-1.7", b"hello"):
            with self.assertRaises(media.MediaError):
                media.check_source(data)
        with self.assertRaisesRegex(media.MediaError, "SVG"):
            media.sniff(b"<svg/>")

    def test_header_dimensions(self):
        self.assertEqual(media.dimensions(png(300, 200)), (300, 200))
        self.assertEqual(media.dimensions(jpeg(640, 480)), (640, 480))
        self.assertEqual(media.dimensions(webp_vp8x(1920, 1080)), (1920, 1080))
        self.assertEqual(media.dimensions(avif(800, 600)), (800, 600))
        self.assertEqual(media.dimensions(gif(33, 44, 1)), (33, 44))

    def test_caps(self):
        with self.assertRaisesRegex(media.MediaError, "megapixels"):
            media.check_source(png_header(8000, 6250))  # 50 MP
        with self.assertRaisesRegex(media.MediaError, "8192"):
            media.check_source(png_header(9000, 100))
        with self.assertRaisesRegex(media.MediaError, "0x0"):
            media.check_source(png_header(0, 0))
        with self.assertRaisesRegex(media.MediaError, "MB"):
            media.check_source(b"\x89PNG\r\n\x1a\n" + b"\x00" * media.MAX_BYTES)

    def test_truncated_jpeg_refused(self):
        with self.assertRaises(media.MediaError):
            media.check_source(jpeg(640, 480)[:22])

    def test_animated_gif_noted(self):
        self.assertEqual(media.gif_frames(gif(4, 4, 1)), 1)
        self.assertEqual(media.gif_frames(gif(4, 4, 3)), 3)
        self.assertTrue(media.check_source(gif(4, 4, 3))["notes"])
        self.assertFalse(media.check_source(gif(4, 4, 1))["notes"])


class Ladder(unittest.TestCase):
    def test_widths(self):
        self.assertEqual([w for w, _ in media.ladder("screenshot", 2560, 1440)], [480, 960, 1600, 2400])
        self.assertEqual(media.ladder("screenshot", 1920, 1080), [(480, 270), (960, 540), (1600, 900)])
        self.assertEqual(media.ladder("icon", 512, 300), [(64, 64), (128, 128), (256, 256)])
        self.assertEqual(media.ladder("icon", 200, 300), [(64, 64), (128, 128)])
        self.assertEqual(media.ladder("banner", 3000, 1000), [(960, 411), (1600, 686)])
        self.assertEqual(media.ladder("banner", 4000, 2000), [(960, 411), (1600, 686), (2560, 1097)])

    def test_no_upscaling(self):
        self.assertEqual(media.ladder("screenshot", 300, 200), [(300, 200)])
        self.assertEqual(media.ladder("icon", 40, 50), [(40, 40)])
        for kind in media.LADDER:
            for w, h in media.ladder(kind, 1000, 700):
                self.assertLessEqual(w, 1000)
                self.assertLessEqual(h, 700)


class Pipeline(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_outputs_and_names(self):
        vips = FakeVips()
        res = media.process_image(png(1920, 1080), "screenshot", self.tmp / "a", vips)
        img = res["image"]
        self.assertEqual(img["widths"], [480, 960, 1600])
        self.assertEqual((img["width"], img["height"]), (1600, 900))
        self.assertRegex(img["hash"], r"^[0-9a-f]{12}$")
        names = sorted(p.name for p in (self.tmp / "a").iterdir())
        want = sorted(f"{img['hash']}-{w}.{e}" for w in (480, 960, 1600) for e in ("avif", "webp"))
        self.assertEqual(names, want)
        # Metadata is stripped on every save.
        self.assertTrue(all("strip" in c[2] for c in vips.calls if c[0] == "copy"))
        # Same picture, same names and ThumbHash; another picture, another name.
        again = media.process_image(png(1920, 1080), "screenshot", self.tmp / "b", FakeVips())["image"]
        self.assertEqual(again, img)
        other = media.process_image(png(1920, 1080, (1, 2, 3)), "screenshot", self.tmp / "c", FakeVips())["image"]
        self.assertNotEqual(other["hash"], img["hash"])

    def test_icon_and_banner_crops(self):
        icon = media.process_image(png(300, 200), "icon", self.tmp, FakeVips())["image"]
        self.assertEqual((icon["widths"], icon["width"], icon["height"]), ([64, 128], 128, 128))
        banner = media.process_image(png(2000, 1000), "banner", self.tmp, FakeVips())["image"]
        self.assertEqual((banner["widths"], banner["width"], banner["height"]), ([960, 1600], 1600, 686))

    def test_gif_first_frame_only(self):
        vips = FakeVips()
        try:
            media.process_image(gif(4, 4, 3), "icon", self.tmp, vips)
        except media.MediaError:
            pass  # the fake cannot read GIF pixels; only the load call matters
        self.assertTrue(vips.calls[0][1].endswith("[n=1]"))

    def test_budget_retry(self):
        vips = FakeVips(size=lambda w, ext, q: q * 5000 if w == 1600 else 100)
        res = media.process_image(png(1920, 1080), "screenshot", self.tmp, vips)
        qs = {ext: [int(re.search(r"Q=(\d+)", c[2]).group(1)) for c in vips.calls
                    if c[0] == "copy" and c[2].split("[")[0].endswith(f"-1600.{ext}")] for ext in ("avif", "webp")}
        self.assertEqual(qs["avif"], [55, 50])  # 275 KB, then 250 KB: under 250 KiB
        self.assertEqual(qs["webp"], [78, 73, 68])  # still over at the floor: kept, with a note
        self.assertTrue(any("budget" in n for n in res["notes"]))

    def test_thumbhash_reference_vectors(self):
        # From evanw/thumbhash's JavaScript rgbaToThumbHash on the same pixels.
        for w, h, alpha, want in ((16, 8, False, "F2gOJJpgdodxiIiGeIb0jA/HeA=="),
                                  (12, 12, True, "HciFFQoWcnewiZhoeOWzH/lXcHdwiId4hw==")):
            px = bytearray()
            for y in range(h):
                for x in range(w):
                    px += bytes((x * 16, y * 20, (x * y * 7) % 256, (x + y) * 10 if alpha else 255))
            self.assertEqual(base64.b64encode(media.thumbhash(w, h, bytes(px))).decode(), want)

    def test_png_reader(self):
        w, h, rgba = media.read_png(png(3, 2, (10, 20, 30)))
        self.assertEqual((w, h), (3, 2))
        self.assertEqual(rgba, bytes((10, 20, 30, 255)) * 6)


class YouTube(unittest.TestCase):
    def test_forms(self):
        for url in ("dQw4w9WgXcQ", "https://www.youtube.com/watch?v=dQw4w9WgXcQ&t=10s", "https://youtu.be/dQw4w9WgXcQ",
                    "youtu.be/dQw4w9WgXcQ?si=abc", "https://m.youtube.com/watch?v=dQw4w9WgXcQ",
                    "https://www.youtube.com/shorts/dQw4w9WgXcQ", "https://www.youtube.com/embed/dQw4w9WgXcQ",
                    "https://www.youtube-nocookie.com/embed/dQw4w9WgXcQ", "https://www.youtube.com/live/dQw4w9WgXcQ"):
            self.assertEqual(media.youtube_id(url), "dQw4w9WgXcQ", url)

    def test_junk_refused(self):
        for url in ("", "short", "https://vimeo.com/123456789", "https://evil.example/watch?v=dQw4w9WgXcQ",
                    "https://www.youtube.com/watch?v=bad", "javascript:alert(1)", "https://youtu.be/dQw4w9WgXcQ!!"):
            with self.assertRaises(media.MediaError, msg=url):
                media.youtube_id(url)

    def test_oembed(self):
        ok = lambda url: (200, json.dumps({"title": "Trailer"}).encode())  # noqa: E731
        self.assertEqual(media.youtube_check("dQw4w9WgXcQ", ok), {"title": "Trailer"})
        for status in (401, 403, 404):
            self.assertIsNone(media.youtube_check("dQw4w9WgXcQ", lambda url, s=status: (s, b"")))


class MediaJob(unittest.TestCase):
    """build_index.py's output, then the media job, offline."""

    def setUp(self):
        self.out = Path(tempfile.mkdtemp())
        index, errors, _ = build_index.build(FIXTURE / "repo", build_index.Offline(FIXTURE), NOW, self.out, False,
                                             log=lambda m: None)
        self.assertEqual(errors, [])

    def tearDown(self):
        shutil.rmtree(self.out)

    def test_fills_index(self):
        media.process_out(self.out, run=FakeVips(), vips_present=True)
        ix = json.loads((self.out / "index.json").read_text())
        self.assertEqual(build_index.validate_index(ix), [])
        m = next(m for m in ix["mods"] if m["id"] == "lock-strafe")["media"]
        self.assertEqual(m["icon"]["widths"], [64, 128, 256])
        self.assertEqual(m["icon"]["alt"], "Lock-strafe icon")
        self.assertEqual(m["screenshots"][0]["caption"], "Strafing")
        self.assertEqual(m["screenshots"][0]["widths"], [480, 960, 1600])
        for img in (m["icon"], m["screenshots"][0]):
            for w in img["widths"]:
                for ext in ("avif", "webp"):
                    self.assertTrue((self.out / "media" / f"{img['hash']}-{w}.{ext}").is_file())
        cache = json.loads((self.out / "media-cache.json").read_text())
        self.assertEqual(len(cache), 2)

    def test_without_vips(self):
        before = json.loads((self.out / "index.json").read_text())
        media.process_out(self.out, run=FakeVips(), vips_present=False)
        after = json.loads((self.out / "index.json").read_text())
        self.assertEqual(before, after)
        self.assertEqual(build_index.validate_index(after), [])

    def test_cache_reuses_published_files(self):
        media.process_out(self.out, run=FakeVips(), vips_present=True)
        cache = json.loads((self.out / "media-cache.json").read_text())
        names = [media.media_name(v["hash"], w, e) for v in cache.values() for w in v["widths"] for e in ("avif", "webp")]
        (self.out / "media-src" / "cache.json").write_text(json.dumps(cache))
        (self.out / "media-src" / "existing.json").write_text(json.dumps(names))
        index, _, _ = build_index.build(FIXTURE / "repo", build_index.Offline(FIXTURE), NOW, self.out, False,
                                        log=lambda m: None)
        (self.out / "media-src" / "cache.json").write_text(json.dumps(cache))
        (self.out / "media-src" / "existing.json").write_text(json.dumps(names))
        vips = FakeVips()
        media.process_out(self.out, run=vips, vips_present=False)
        self.assertEqual(vips.calls, [])
        ix = json.loads((self.out / "index.json").read_text())
        self.assertIsNotNone(next(m for m in ix["mods"] if m["id"] == "lock-strafe")["media"]["icon"])


if __name__ == "__main__":
    unittest.main()
