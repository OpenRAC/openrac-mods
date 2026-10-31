# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""tools/build_index.py against recorded GitHub responses (fixtures/github, made by make_fixtures.py)."""
from __future__ import annotations

import datetime as dt
import email.message
import io
import json
import os
import re
import shutil
import sys
import tempfile
import unittest
import urllib.error
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
import build_index as b  # noqa: E402

FIXTURE = HERE / "fixtures" / "github"
SHARED = TOOLS / "fixtures" / "index" / "index.json"
NOW = 1793440800  # 2026-10-31T10:00:00Z


class Fixture:
    """A writable copy of the fixture, so a test can break one thing."""

    def __init__(self):
        self.dir = Path(tempfile.mkdtemp()) / "github"
        shutil.copytree(FIXTURE, self.dir)
        self.repo = self.dir / "repo"
        self.routes = json.loads((self.dir / "routes.json").read_text())

    def route(self, url, data, **extra):
        name = f"extra/{len(self.routes)}.json"
        (self.dir / "extra").mkdir(exist_ok=True)
        (self.dir / name).write_text(json.dumps(data))
        self.routes[url] = {"file": name, **extra}
        (self.dir / "routes.json").write_text(json.dumps(self.routes))

    def edit(self, rel, change):
        path = self.repo / rel
        d = json.loads(path.read_text()) if path.exists() else {}
        change(d)
        path.write_text(json.dumps(d))

    def build(self, check=False, out=None, now=NOW):
        warnings = []
        index, errors, _ = b.build(self.repo, b.Offline(self.dir), now, out, check, log=warnings.append)
        return index, errors, warnings

    def close(self):
        shutil.rmtree(self.dir.parent)


def mod(index, mid):
    return next(m for m in index["mods"] if m["id"] == mid)


class Builder(unittest.TestCase):
    def setUp(self):
        self.fx = Fixture()

    def tearDown(self):
        self.fx.close()

    def test_offline_fixture(self):
        index, errors, warnings = self.fx.build()
        self.assertEqual(errors, [])
        self.assertEqual([m["id"] for m in index["mods"]],
                         ["button-swap", "camera-base", "ext-hud", "lock-strafe", "starter-pack"])
        ls = mod(index, "lock-strafe")
        self.assertEqual([v["version"] for v in ls["versions"]], ["2.0.0-beta.1", "1.1.0", "1.0.0"])
        self.assertEqual((ls["type"], ls["path"], ls["featured"]), ("native", "lock-strafe", True))
        self.assertEqual(ls["recommends"], ["camera-base"])
        self.assertEqual(ls["suggests"], ["ext-hud"])
        self.assertLessEqual(len(ls["description"]), 300)
        self.assertEqual(ls["readme"], "# Lock-strafe\n\nStrafe as in the later games.\n")
        self.assertEqual(ls["created"], "2026-09-01T12:00:00Z")
        self.assertEqual(ls["updated"], "2026-10-20T12:00:00Z")
        self.assertEqual(ls["links"], {"bugs": "https://github.com/OpenRAC/openrac-mods/issues",
                                       "discord": "https://discord.gg/example"})

    def test_manifest_becomes_version(self):
        index, _, _ = self.fx.build()
        v = next(v for v in mod(index, "lock-strafe")["versions"] if v["version"] == "1.1.0")
        self.assertEqual(v["api"], "1.0")
        self.assertEqual(v["requires"], [{"id": "camera-base", "version": ">=1.0.0"}])
        self.assertEqual(v["conflicts"], [{"id": "button-swap", "reason": "both take L2 and R2"}])
        self.assertEqual(v["hooks"], ["rac1.pad.process_report"])
        self.assertEqual(v["tag"], "lock-strafe-v1.1.0")
        self.assertEqual(list(v["platforms"]), ["linux-x86_64", "windows-x86_64", "macos-arm64"])
        self.assertEqual(v["files"][0], "openrac-mod.json")
        self.assertIn("lib/windows-x86_64/lock_strafe.dll", v["files"])
        self.assertEqual(v["size"], max(p["size"] for p in v["platforms"].values()))
        sums = b.parse_sums((FIXTURE / "assets/lock-strafe-v1.1.0/SHA256SUMS.bin").read_bytes())
        for p in v["platforms"].values():
            self.assertEqual(p["sha256"], sums[p["asset"]])
        bs = mod(index, "button-swap")["versions"][0]
        self.assertEqual((list(bs["platforms"]), bs["files"]), (["any"], ["openrac-mod.json", "main.luau"]))
        cb = mod(index, "camera-base")
        self.assertEqual((cb["type"], cb["versions"][0]["api"], cb["path"]), ("data", None, "camera-data"))
        ext = mod(index, "ext-hud")
        self.assertEqual((ext["versions"][0]["tag"], ext["repo"]), ("v0.2.0", "https://github.com/someone/ext-hud"))
        pack = mod(index, "starter-pack")
        self.assertEqual((pack["type"], pack["versions"][0]["tag"], pack["versions"][0]["platforms"]), ("pack", "", {}))

    def test_prerelease_from_flag_and_suffix(self):
        index, _, _ = self.fx.build()
        ls = {v["version"]: v["prerelease"] for v in mod(index, "lock-strafe")["versions"]}
        self.assertEqual(ls, {"2.0.0-beta.1": True, "1.1.0": False, "1.0.0": False})
        bs = {v["version"]: v["prerelease"] for v in mod(index, "button-swap")["versions"]}
        self.assertEqual(bs, {"1.1.0": True, "1.0.0": False})

    def test_broken_release_skips_only_that_version(self):
        index, errors, warnings = self.fx.build()
        self.assertEqual(errors, [])
        self.assertNotIn("1.2.0", [v["version"] for v in mod(index, "lock-strafe")["versions"]])
        self.assertTrue(any("lock-strafe-v1.2.0" in w and "SHA256SUMS" in w for w in warnings))

    def test_broken_release_keeps_last_good(self):
        good, _, _ = self.fx.build()
        prev = json.loads(json.dumps(good))
        ls = mod(prev, "lock-strafe")
        old = dict(ls["versions"][1], version="1.2.0", tag="lock-strafe-v1.2.0", released="2026-10-01T12:00:00Z")
        ls["versions"].insert(1, old)
        self.fx.route(b.MEDIA_BASE + "index.json", prev)
        index, errors, warnings = self.fx.build()
        self.assertEqual(errors, [])
        versions = mod(index, "lock-strafe")["versions"]
        self.assertEqual([v["version"] for v in versions], ["2.0.0-beta.1", "1.2.0", "1.1.0", "1.0.0"])
        self.assertEqual(versions[1], old)
        self.assertTrue(any("kept the last good one" in w for w in warnings))

    def test_external_archives_swapped_keep_first_hashes(self):
        good, _, _ = self.fx.build()
        prev = json.loads(json.dumps(good))
        plat = mod(prev, "ext-hud")["versions"][0]["platforms"]["linux-x86_64"]
        plat["sha256"] = "0" * 64
        self.fx.route(b.MEDIA_BASE + "index.json", prev)
        index, _, warnings = self.fx.build()
        self.assertEqual(mod(index, "ext-hud")["versions"][0]["platforms"]["linux-x86_64"]["sha256"], "0" * 64)
        self.assertTrue(any("changed after" in w for w in warnings))

    def test_serial_and_expires(self):
        index, _, _ = self.fx.build()
        self.assertEqual(index["serial"], NOW)
        self.assertEqual(index["generated"], "2026-10-31T10:00:00Z")
        self.assertEqual(index["expires"], "2026-11-07T10:00:00Z")
        # The serial only grows, even with a clock behind the last build.
        self.fx.route(b.MEDIA_BASE + "index.json", dict(index, serial=NOW + 50))
        later, _, _ = self.fx.build(now=NOW - 100)
        self.assertEqual(later["serial"], NOW + 51)

    def test_ports(self):
        index, _, _ = self.fx.build()
        self.assertEqual(index["ports"], {"stable": {"launcher": "0.10.1", "api": "1.10"},
                                          "dev": {"launcher": "0.11.0-dev.42", "api": "1.11"}})
        header = (FIXTURE / "ports" / "openrac_mod-dev.h").read_text()
        self.assertEqual(b.parse_api(header), "1.11")
        with self.assertRaises(ValueError):
            b.parse_api("#define SOMETHING 1\n")

    def test_downloads_and_stats(self):
        out = Path(tempfile.mkdtemp())
        try:
            index, _, _ = self.fx.build(out=out)
            ls = mod(index, "lock-strafe")
            self.assertEqual(ls["downloads7d"], ls["downloads"] - 100)
            # The broken release's archives count too.
            rels = json.loads((FIXTURE / "api/openrac-mods/releases-1.json").read_text())
            want = sum(a["download_count"] for r in rels if r["tag_name"].startswith("lock-strafe-v")
                       for a in r["assets"] if a["name"] != "SHA256SUMS")
            self.assertEqual(ls["downloads"], want)
            stats = json.loads((out / "stats.json").read_text())
            self.assertEqual(sorted(stats["days"]), ["2026-10-24", "2026-10-31"])
        finally:
            shutil.rmtree(out)

    def test_stats_window(self):
        now = dt.datetime(2026, 10, 31, tzinfo=dt.timezone.utc)
        days = {(now.date() - dt.timedelta(days=n)).isoformat(): {"a": 100 - n} for n in range(1, 40)}
        stats, growth = b.update_stats({"days": days}, {"a": 100}, now)
        self.assertEqual(len(stats["days"]), 30)
        self.assertEqual(growth, {"a": 7})
        self.assertEqual(b.update_stats(None, {"a": 5}, now)[1], {"a": 0})

    def test_id_mismatch_refused(self):
        self.fx.edit("button-swap/listing.json", lambda d: d.update(id="other-name"))
        _, errors, _ = self.fx.build(check=True)
        self.assertTrue(any("not the manifest's" in e for e in errors), errors)

    def test_external_manifest_id_mismatch_refused(self):
        bad = json.loads((FIXTURE / "raw/ext-hud/HEAD/openrac-mod.json").read_text())
        bad["id"] = "someone-else"
        self.fx.route("https://raw.githubusercontent.com/someone/ext-hud/HEAD/openrac-mod.json", bad)
        _, errors, _ = self.fx.build(check=True)
        self.assertTrue(any("manifest's id" in e for e in errors), errors)

    def test_entry_file_name_must_match_id(self):
        shutil.copy(self.fx.repo / "index/ext-hud.json", self.fx.repo / "index/renamed.json")
        _, errors, _ = self.fx.build(check=True)
        self.assertTrue(any("named renamed.json" in e for e in errors), errors)

    def test_bad_ids_and_lists_refused(self):
        for change, needle in ((lambda d: d.update(tags=["gameplay", "bogus"]), "tags"),
                               (lambda d: d.update(impact=["safe"]), "impact"),
                               (lambda d: d.update(links={"web": "https://x"}), "links"),
                               (lambda d: d.update(recommends=["Bad_Id"]), "valid id"),
                               (lambda d: d.update(media={"icon": "../../etc/passwd"}), "inside"),
                               (lambda d: d.update(media={"videos": [{"youtube": "https://vimeo.com/1"}]}), "YouTube")):
            fx = Fixture()
            try:
                fx.edit("button-swap/listing.json", change)
                _, errors, _ = fx.build(check=True)
                self.assertTrue(any(needle in e for e in errors), (needle, errors))
            finally:
                fx.close()

    def test_missing_recommendation_refused(self):
        self.fx.edit("button-swap/listing.json", lambda d: d.update(recommends=["no-such-mod"]))
        _, errors, _ = self.fx.build(check=True)
        self.assertTrue(any("no-such-mod" in e for e in errors), errors)

    def test_missing_dependency_skips_version(self):
        self.fx.edit("index/starter-pack.json", lambda d: d["versions"][0]["requires"].append({"id": "no-such-mod"}))
        index, errors, warnings = self.fx.build()
        self.assertEqual(errors, [])
        self.assertNotIn("starter-pack", [m["id"] for m in index["mods"]])
        self.assertTrue(any("no-such-mod" in w for w in warnings))

    def test_cycle_refused(self):
        (self.fx.repo / "index/pack-b.json").write_text(json.dumps({
            "id": "pack-b", "repo": b.SOURCE, "path": "", "type": "pack", "name": "B", "author": "x",
            "description": "B", "versions": [{"version": "1.0.0", "released": "2026-10-30T00:00:00Z",
                                              "requires": [{"id": "starter-pack"}]}]}))
        self.fx.edit("index/starter-pack.json", lambda d: d["versions"][0]["requires"].append({"id": "pack-b"}))
        index, errors, _ = self.fx.build(check=True)
        self.assertIsNone(index)
        self.assertTrue(any("cycle" in e and "pack-b" in e and "starter-pack" in e for e in errors), errors)

    def test_id_taken_in_another_index(self):
        self.fx.route("https://example.org/rac2/index.json", {"format": 1, "mods": [{"id": "button-swap"}]})
        with mock.patch.dict(os.environ, {"MOD_INDEXES": f"{b.INDEX_URL},https://example.org/rac2/index.json"}):
            _, errors, _ = self.fx.build(check=True)
        self.assertTrue(any("button-swap" in e and "example.org" in e for e in errors), errors)

    def test_revoked_checked(self):
        (self.fx.repo / "index/revoked.json").write_text(json.dumps([{"id": "x", "versions": ["latest"], "reason": "r"}]))
        _, errors, _ = self.fx.build(check=True)
        self.assertTrue(any("revoked" in e for e in errors), errors)

    def test_index_tag_ignored(self):
        _, _, warnings = self.fx.build()
        self.assertFalse(any("index" == w.split(":")[0] for w in warnings))
        self.assertFalse(any(u.endswith("/index/openrac-mod.json") for u in self._seen()))

    def _seen(self):
        http = b.Offline(self.fx.dir)
        b.build(self.fx.repo, http, NOW, None, True, log=lambda m: None)
        return http.seen

    def test_deterministic_and_check_writes_nothing(self):
        outs = []
        for _ in range(2):
            out = Path(tempfile.mkdtemp())
            self.fx.build(out=out)
            outs.append((out / "index.json").read_bytes())
            shutil.rmtree(out)
        self.assertEqual(outs[0], outs[1])
        out = Path(tempfile.mkdtemp())
        shutil.rmtree(out)
        self.fx.build(check=True, out=out)
        self.assertFalse(out.exists())

    def test_cli_check_offline(self):
        with mock.patch("sys.stdout", new=io.StringIO()) as stdout, mock.patch("sys.stderr", new=io.StringIO()):
            code = b.main(["--check", "--offline", str(FIXTURE), "--now", str(NOW)])
        self.assertEqual(code, 0)
        self.assertIn("5 mods", stdout.getvalue())


class Contract(unittest.TestCase):
    """The output has the shape of INDEX_FORMAT (and of the shared fixture)."""

    def test_shared_fixture_is_valid(self):
        self.assertEqual(b.validate_index(json.loads(SHARED.read_text())), [])

    def test_output_matches_contract_shape(self):
        fx = Fixture()
        try:
            index, _, _ = fx.build()
        finally:
            fx.close()
        self.assertEqual(b.validate_index(index), [])
        shared = json.loads(SHARED.read_text())
        self.assertEqual(list(index), list(shared))
        self.assertEqual(index["tags"], shared["tags"])
        mod_keys = set(shared["mods"][0])
        ver_keys = set(shared["mods"][0]["versions"][0])
        for m in index["mods"]:
            self.assertLessEqual(set(m), mod_keys | {"discussion"}, m["id"])
            self.assertLessEqual(mod_keys - set(m), {"license", "homepage"}, m["id"])
            for v in m["versions"]:
                self.assertEqual(set(v), ver_keys, m["id"])
        json.dumps(index, allow_nan=False)

    def test_validate_catches_breakage(self):
        ix = json.loads(SHARED.read_text())
        ix["mods"].reverse()
        ix["mods"][0]["versions"][0]["platforms"] = {"amiga": {"asset": "x", "sha256": "z", "size": 1}}
        errs = b.validate_index(ix)
        self.assertTrue(any("sorted" in e for e in errs))
        self.assertTrue(any("platform amiga" in e for e in errs))


class Retry(unittest.TestCase):
    def http_error(self, code, headers):
        msg = email.message.Message()
        for k, v in headers.items():
            msg[k] = v
        return urllib.error.HTTPError("https://api.github.com/x", code, "x", msg, io.BytesIO(b""))

    def test_retry_after_and_reset(self):
        sleeps = []
        http = b.Http(sleep=sleeps.append, rand=lambda: 0.5, clock=lambda: 1000.0)
        answers = [self.http_error(429, {"Retry-After": "3"}),
                   self.http_error(403, {"X-RateLimit-Remaining": "0", "X-RateLimit-Reset": "1010"}),
                   self.http_error(503, {}),
                   b.Response(200, {}, b"{}")]

        def fake(url, headers, limit):
            a = answers.pop(0)
            if isinstance(a, Exception):
                raise a
            return a
        http._open = fake
        self.assertEqual(http.get("https://api.github.com/x").status, 200)
        self.assertEqual(sleeps, [3.0, 11.0, 0.5 * 4])  # full jitter: rand * 2**attempt

    def test_plain_403_and_404_not_retried(self):
        http = b.Http(sleep=lambda s: self.fail("slept"))
        for code in (403, 404):
            http._open = lambda url, h, l, c=code: (_ for _ in ()).throw(self.http_error(c, {}))
            self.assertEqual(http.get("https://api.github.com/x").status, code)

    def test_token_only_to_api_and_not_redirected(self):
        seen = []

        class R:
            status, headers = 200, {}

            def read(self, n):
                return b""

            def __enter__(self):
                return self

            def __exit__(self, *a):
                return False

        def urlopen(req, timeout):
            seen.append(req)
            return R()
        http = b.Http(token="secret")
        with mock.patch("urllib.request.urlopen", urlopen):
            http.get("https://api.github.com/repos/a/b")
            http.get("https://github.com/a/b/releases/download/t/x.zip")
        self.assertEqual(seen[0].unredirected_hdrs.get("Authorization"), "Bearer secret")
        self.assertNotIn("Authorization", seen[0].headers)
        self.assertNotIn("Authorization", seen[1].unredirected_hdrs)


class Schema(unittest.TestCase):
    """index/schema.json agrees with the builder; every listing in the repo follows it."""

    @classmethod
    def setUpClass(cls):
        cls.schema = json.loads((ROOT / "index" / "schema.json").read_text())

    def check(self, value, s, path="$"):
        """A small JSON Schema subset: enough for index/schema.json."""
        if "$ref" in s:
            return self.check(value, self.schema["$defs"][s["$ref"].split("/")[-1]], path)
        if "oneOf" in s:
            ok = [sub for sub in s["oneOf"] if not self.check(value, sub, path)]
            return [] if len(ok) == 1 else [f"{path}: matches {len(ok)} of oneOf"]
        errs = []
        t = s.get("type")
        types = {"object": dict, "array": list, "string": str, "boolean": bool, "null": type(None)}
        if t and not isinstance(value, types[t]):
            return [f"{path}: not {t}"]
        if "enum" in s and value not in s["enum"]:
            errs.append(f"{path}: {value!r} not in enum")
        if isinstance(value, str):
            if len(value) > s.get("maxLength", 1 << 30) or len(value) < s.get("minLength", 0):
                errs.append(f"{path}: length")
            if "pattern" in s and not re.search(s["pattern"], value):
                errs.append(f"{path}: pattern")
        if isinstance(value, list):
            if len(value) > s.get("maxItems", 1 << 30) or len(value) < s.get("minItems", 0):
                errs.append(f"{path}: items count")
            if s.get("uniqueItems") and len(set(map(json.dumps, value))) != len(value):
                errs.append(f"{path}: not unique")
            for i, x in enumerate(value):
                errs += self.check(x, s.get("items", {}), f"{path}[{i}]")
        if isinstance(value, dict):
            props = s.get("properties", {})
            for k in s.get("required", []):
                if k not in value:
                    errs.append(f"{path}: needs {k}")
            for k, v in value.items():
                if k in props:
                    errs += self.check(v, props[k], f"{path}.{k}")
                elif s.get("additionalProperties") is False:
                    errs.append(f"{path}: unknown {k}")
                elif isinstance(s.get("additionalProperties"), dict):
                    errs += self.check(v, s["additionalProperties"], f"{path}.{k}")
        return errs

    def test_enums_match_builder(self):
        listing = self.schema["$defs"]["listing"]["properties"]
        self.assertEqual(listing["tags"]["items"]["enum"], b.TAGS)
        self.assertEqual(listing["impact"]["items"]["enum"], b.IMPACT)
        self.assertEqual(sorted(listing["links"]["properties"]), sorted(b.LINK_KEYS))

    def test_repo_listings_follow_schema(self):
        files = sorted(ROOT.glob("*/listing.json")) + sorted(
            f for f in (ROOT / "index").glob("*.json") if f.name not in b.INDEX_FILES)
        files += sorted((FIXTURE / "repo").glob("*/listing.json")) + sorted(
            f for f in (FIXTURE / "repo" / "index").glob("*.json") if f.name not in b.INDEX_FILES)
        self.assertGreaterEqual(len(files), 11)
        for f in files:
            self.assertEqual(self.check(json.loads(f.read_text()), self.schema), [], f)

    def test_schema_refuses_what_builder_refuses(self):
        bad = {"tags": ["bogus"]}
        self.assertTrue(self.check(bad, self.schema))
        with self.assertRaises(b.EntryError):
            b.check_listing(bad, "x")

    def test_repo_entries_load(self):
        entries, errors = b.load_entries(ROOT)
        self.assertEqual(errors, [])
        self.assertGreaterEqual(len(entries), 11)
        self.assertEqual(b.load_revoked(ROOT), [])


class Workflow(unittest.TestCase):
    def test_no_expressions_in_scripts(self):
        # ${{ }} only in if:, env values, paths and groups, never inside a run: script.
        for name in ("index.yml", "test.yml", "submit.yml"):
            text = (ROOT / ".github" / "workflows" / name).read_text()
            for line in text.splitlines():
                if "${{" in line:
                    self.assertRegex(line.strip(), r"^(if:|group:|path:|[A-Z_]+:)", (name, line))
                if line.strip().startswith("uses:") or line.strip().startswith("- uses:"):
                    self.assertRegex(line, r"@[0-9a-f]{40}\b", line)

    def test_index_release_is_a_prerelease(self):
        text = (ROOT / ".github" / "workflows" / "index.yml").read_text()
        self.assertIn("--prerelease", text)
        self.assertNotIn("${{ github.event.issue", text)
        for line in text.splitlines():
            if line.strip().startswith("- uses:") or line.strip().startswith("uses:"):
                self.assertRegex(line, r"@[0-9a-f]{40}\b", line)


if __name__ == "__main__":
    unittest.main()
