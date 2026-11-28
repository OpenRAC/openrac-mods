# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""Trust labels, held versions, community.json, details, author files, picks and the bot's part."""
from __future__ import annotations

import base64
import datetime as dt
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))
import build_index as b  # noqa: E402
import submit_bot as bot  # noqa: E402
import trust  # noqa: E402
from test_build_index import NOW, Fixture, mod  # noqa: E402
from test_submit_bot import EXT, Base, FakeRun, event  # noqa: E402

EXT_API = "https://api.github.com/repos/someone/ext-hud"
EXT_RAW = "https://raw.githubusercontent.com/someone/ext-hud/HEAD/"


def route(fx, url, data):
    """Fixture.route, safe for a URL that already has a route (Fixture names files by count)."""
    name = f"extra/t{len(list((fx.dir / 'extra').glob('t*'))) if (fx.dir / 'extra').is_dir() else 0}.json"
    (fx.dir / "extra").mkdir(exist_ok=True)
    (fx.dir / name).write_text(json.dumps(data))
    fx.routes[url] = {"file": name}
    (fx.dir / "routes.json").write_text(json.dumps(fx.routes))


def write(fx, rel, data):
    (fx.repo / rel).parent.mkdir(parents=True, exist_ok=True)
    (fx.repo / rel).write_text(json.dumps(data))


class Labels(unittest.TestCase):
    def setUp(self):
        self.fx = Fixture()

    def tearDown(self):
        self.fx.close()

    def test_default_labels(self):
        index, errors, _ = self.fx.build()
        self.assertEqual(errors, [])
        self.assertEqual(mod(index, "lock-strafe")["trust"], "openrac")
        self.assertEqual(mod(index, "ext-hud")["trust"], "unchecked")

    def test_lists_give_labels(self):
        write(self.fx, "trust/trusted.json", {"owners": {"Someone": {"since": "2026-11-27"}}})
        index, _, _ = self.fx.build()
        self.assertEqual(mod(index, "ext-hud")["trust"], "trusted")
        write(self.fx, "trust/verified.json", {"mods": {"ext-hud": {"since": "2026-11-27"}}})
        index, _, _ = self.fx.build()
        self.assertEqual(mod(index, "ext-hud")["trust"], "verified")

    def test_numeric_id_follows_a_renamed_account(self):
        write(self.fx, "trust/trusted.json", {"owners": {"old-name": {"since": "2026-11-27", "id": 4242}}})
        index, _, _ = self.fx.build()
        self.assertEqual(mod(index, "ext-hud")["trust"], "trusted")

    def test_bad_list_fails(self):
        write(self.fx, "trust/trusted.json", {"owners": ["someone"]})
        index, errors, _ = self.fx.build()
        self.assertIsNone(index)
        self.assertTrue(any("trusted.json" in e for e in errors))

    def test_openrac_names_are_reserved(self):
        self.fx.edit("index/ext-hud.json", lambda d: None)
        (self.fx.repo / "index/ext-hud.json").rename(self.fx.repo / "index/openrac-hud.json")
        self.fx.edit("index/openrac-hud.json", lambda d: d.update(id="openrac-hud"))
        _, errors, _ = self.fx.build()
        self.assertTrue(any("openrac" in e and "openrac-hud" in e for e in errors), errors)


class Held(unittest.TestCase):
    """A version whose scan found something dangerous is not published, and the mod is marked held."""

    def setUp(self):
        self.fx = Fixture()
        index, _, _ = self.fx.build()
        self.version = mod(index, "ext-hud")["versions"][0]

    def tearDown(self):
        self.fx.close()

    def cache(self, danger, labels=("Network",)):
        key = "ext-hud@" + self.version["tag"]
        route(self.fx, b.MEDIA_BASE + "held.json", {"cache": {key: {
            "sha256": b.archive_hashes(self.version), "labels": list(labels), "danger": danger}}})

    def test_danger_holds_the_version(self):
        self.cache(["reads saved secrets"])
        review = []
        index, errors, _ = b.build(self.fx.repo, b.Offline(self.fx.dir), NOW, None, True, log=lambda m: None,
                                   review=review)
        self.assertEqual(errors, [])
        self.assertNotIn("ext-hud", [m["id"] for m in index["mods"]])  # its only version is held
        held = [r for r in review if r.get("findings")]
        self.assertEqual([(r["id"], r["findings"]) for r in held], [("ext-hud", ["reads saved secrets"])])

    def test_cleared_findings_publish_with_labels(self):
        self.cache(["mentions powershell"])
        write(self.fx, "index/cleared.json", {"ext-hud": ["mentions powershell"]})
        index, errors, _ = self.fx.build()
        self.assertEqual(errors, [])
        v = mod(index, "ext-hud")["versions"][0]
        self.assertEqual(v["scan"], {"labels": ["Network"], "new": []})

    def test_held_mod_keeps_its_published_version(self):
        out = Path(tempfile.mkdtemp())
        try:
            index, _, _ = self.fx.build()
            route(self.fx, b.MEDIA_BASE + "index.json", index)  # 0.2.0 is published
            # 0.3.0 comes out, and its scan finds something.
            rels = json.loads((self.fx.dir / "api/ext-hud/releases-1.json").read_text())
            new = json.loads(json.dumps(rels[0]).replace("0.2.0", "0.3.0"))
            for a, was in zip(new["assets"], rels[0]["assets"]):
                a["browser_download_url"] = was["browser_download_url"]  # the same file
            route(self.fx, EXT_API + "/releases?per_page=100&page=1", [new] + rels)
            manifest = json.loads((self.fx.dir / "raw/ext-hud/v0.2.0/openrac-mod.json").read_text())
            route(self.fx, "https://raw.githubusercontent.com/someone/ext-hud/v0.3.0/openrac-mod.json",
                          {**manifest, "version": "0.3.0"})
            self.version = {**self.version, "tag": "v0.3.0"}
            self.cache(["reaches other processes"])
            index, errors, _ = self.fx.build(out=out)
            self.assertEqual(errors, [])
            m = mod(index, "ext-hud")
            self.assertEqual([v["version"] for v in m["versions"]], ["0.2.0"])
            self.assertEqual(m["held"], {"version": "0.3.0", "reasons": ["reaches other processes"]})
            held = json.loads((out / "held.json").read_text())
            self.assertEqual([h["version"] for h in held["held"]], ["0.3.0"])
        finally:
            shutil.rmtree(out)

    def test_young_version_waits(self):
        released = dt.datetime.fromisoformat(self.version["released"].replace("Z", "+00:00")).timestamp()
        index, _, _ = self.fx.build(now=released + 600)
        self.assertNotIn("ext-hud", [m["id"] for m in index["mods"]])


class Files(unittest.TestCase):
    def setUp(self):
        self.fx = Fixture()
        self.out = Path(tempfile.mkdtemp())

    def tearDown(self):
        self.fx.close()
        shutil.rmtree(self.out)

    def test_details_named_by_hash(self):
        index, errors, _ = self.fx.build(out=self.out)
        self.assertEqual(errors, [])
        m = mod(index, "lock-strafe")
        data = (self.out / "details" / m["details"]["asset"]).read_bytes()
        self.assertEqual(b.hashlib.sha256(data).hexdigest(), m["details"]["sha256"])
        self.assertEqual(json.loads(data)["readme"], m["readme"])

    def test_slim_index_leaves_texts_to_details(self):
        write(self.fx, "index/policy.json", {"slimIndex": True})
        index, _, _ = self.fx.build(out=self.out)
        m = mod(index, "lock-strafe")
        self.assertEqual(m["readme"], "")
        self.assertTrue(json.loads((self.out / "details" / "details-lock-strafe.json").read_text())["readme"])

    def test_new_unchecked_mod_goes_to_community(self):
        write(self.fx, "index/policy.json", {"community": True})
        index, errors, _ = self.fx.build(out=self.out)
        self.assertEqual(errors, [])
        self.assertNotIn("ext-hud", [m["id"] for m in index["mods"]])
        community = json.loads((self.out / "community.json").read_text())
        self.assertIn("ext-hud", [m["id"] for m in community["mods"]])
        self.assertEqual(b.validate_index(community, {m["id"] for m in index["mods"]}), [])

    def test_listed_unchecked_mod_stays_in_index(self):
        index, _, _ = self.fx.build()
        route(self.fx, b.MEDIA_BASE + "index.json", index)
        write(self.fx, "index/policy.json", {"community": True})
        index, _, _ = self.fx.build(out=self.out)
        self.assertIn("ext-hud", [m["id"] for m in index["mods"]])
        self.assertFalse((self.out / "community.json").exists())

    def test_author_listing_and_revoked(self):
        route(self.fx, EXT_RAW + "openrac-listing.json", {"description": "From the author.", "tags": ["ui", "qol"]})
        route(self.fx, EXT_RAW + "openrac-revoked.json", [{"versions": ["0.2.0"], "reason": "crashes"}])
        index, errors, _ = self.fx.build()
        self.assertEqual(errors, [])
        m = mod(index, "ext-hud")
        self.assertEqual((m["description"], m["tags"]), ("From the author.", ["ui", "qol"]))
        self.assertIn({"id": "ext-hud", "versions": ["0.2.0"], "reason": "By the author: crashes"}, index["revoked"])

    def test_broken_author_listing_is_skipped(self):
        route(self.fx, EXT_RAW + "openrac-listing.json", {"tags": ["nope"]})
        index, errors, warnings = self.fx.build()
        self.assertEqual(errors, [])
        self.assertTrue(any("openrac-listing.json skipped" in w for w in warnings))

    def test_gone_repo_stays_for_a_while(self):
        index, _, _ = self.fx.build()
        route(self.fx, b.MEDIA_BASE + "index.json", index)
        self.fx.routes[EXT_API] = {"file": "", "status": 404}
        (self.fx.dir / "routes.json").write_text(json.dumps(self.fx.routes))
        index2, errors, _ = self.fx.build(now=NOW + 3600)
        self.assertEqual(errors, [])
        self.assertEqual(mod(index2, "ext-hud")["unreachable"], 1)
        route(self.fx, b.MEDIA_BASE + "index.json", index2)
        index3, _, _ = self.fx.build(now=NOW + 7200)
        self.assertEqual(mod(index3, "ext-hud")["unreachable"], 2)
        route(self.fx, b.MEDIA_BASE + "index.json", index3)
        index4, _, _ = self.fx.build(now=NOW + 10800)  # the third failed read
        self.assertNotIn("ext-hud", [m["id"] for m in index4["mods"]])


class Provenance(unittest.TestCase):
    def att(self, repo, ref):
        st = {"predicate": {"buildDefinition": {"externalParameters": {"workflow": {"repository": repo, "ref": ref}}}}}
        return {"attestations": [{"bundle": {"dsseEnvelope": {"payload": base64.b64encode(json.dumps(st).encode()).decode()}}}]}

    def test_built_by_ci(self):
        self.assertTrue(trust.built_by_ci(self.att("https://github.com/A/B", "refs/tags/v1.0.0"),
                                          "https://github.com/a/b", "v1.0.0"))
        self.assertFalse(trust.built_by_ci(self.att("https://github.com/other/b", "refs/tags/v1.0.0"),
                                           "https://github.com/a/b", "v1.0.0"))
        self.assertFalse(trust.built_by_ci(self.att("https://github.com/a/b", "refs/heads/main"),
                                           "https://github.com/a/b", "v1.0.0"))
        self.assertFalse(trust.built_by_ci(None, "https://github.com/a/b", "v1.0.0"))


class Findings(unittest.TestCase):
    def test_labels_and_danger(self):
        lib = {"kind": "elf", "libraries": [], "capabilities": {"network": ["socket"], "reads saved secrets": ["x"]}}
        labels, danger = trust.findings({"libraries": {"a.so": lib}, "telling": {"cmd.exe"}, "problems": []})
        self.assertEqual(labels, ["Network"])
        self.assertEqual(danger, ["reads saved secrets", "mentions cmd.exe"])


def pick_mod(mid, author, label="trusted", games=("rac1",), updated="2026-11-20T00:00:00Z"):
    return {"id": mid, "author": author, "trust": label, "type": "native", "games": {g: ["pal"] for g in games},
            "description": "x", "updated": updated, "pictures": True, "downloads": 10,
            "versions": [{"api": "1.0"}], "media": {}}


class Picks(unittest.TestCase):
    NOW = dt.datetime(2026, 11, 27, 12, tzinfo=dt.timezone.utc)

    def days(self, totals):
        """{mid: [total 21 days ago, 14, 7, today]}"""
        out = {}
        for i, back in enumerate((21, 14, 7, 0)):
            day = (self.NOW.date() - dt.timedelta(days=back)).isoformat()
            out[day] = {m: t[i] for m, t in totals.items()}
        return out

    def test_rules(self):
        mods = [pick_mod("big", "a"), pick_mod("growing", "b"), pick_mod("same-author", "b"),
                pick_mod("ours-1", "OpenRAC", "openrac"), pick_mod("ours-2", "OpenRAC2", "openrac"),
                pick_mod("unchecked", "c", "unchecked"), pick_mod("old", "d", updated="2026-01-01T00:00:00Z")]
        days = self.days({"big": [1000, 2000, 3000, 4000], "growing": [0, 0, 0, 200], "same-author": [0, 0, 0, 150],
                          "ours-1": [0, 0, 0, 100], "ours-2": [0, 0, 0, 90], "unchecked": [0, 0, 0, 999],
                          "old": [0, 0, 0, 999]})
        p = trust.picks(mods, days, self.NOW, {"stable": {"api": "1.2"}}, [], None, [])
        self.assertEqual(p["week"], "2026-11-23")
        self.assertEqual(p["games"]["rac1"], ["growing", "ours-1", "big"])

    def test_week_is_kept_and_recent_picks_skipped(self):
        mods = [pick_mod("m1", "a"), pick_mod("m2", "b"), pick_mod("m3", "c"), pick_mod("m4", "d")]
        days = self.days({"m1": [0, 0, 0, 1], "m2": [0, 0, 0, 2], "m3": [0, 0, 0, 3], "m4": [0, 0, 0, 400]})
        history = {"history": {"2026-11-23": {"rac1": ["m1", "m2", "m3"]}, "2026-11-16": {"rac1": ["m4"]}}}
        p = trust.picks(mods, days, self.NOW, {}, [], history, [])
        self.assertEqual(p["games"]["rac1"], ["m1", "m2", "m3"])
        p = trust.picks(mods, days, self.NOW + dt.timedelta(days=7), {}, [], p, [])
        self.assertNotIn("m4", p["games"]["rac1"])  # picked within the last 4 weeks

    def test_pin(self):
        mods = [pick_mod("m1", "a"), pick_mod("m2", "b")]
        p = trust.picks(mods, self.days({"m1": [0, 0, 0, 100], "m2": [0, 0, 0, 1]}), self.NOW, {}, [], None,
                        [{"id": "m2", "until": "2026-12-01"}])
        self.assertEqual(p["games"]["rac1"][0], "m2")


class BotHeld(Base):
    ITEM = {"id": "ext-hud", "name": "Extra HUD", "repo": EXT, "version": "0.2.0", "tag": "v0.2.0",
            "released": "2026-10-10T12:00:00Z", "platforms": {}, "findings": ["reads saved secrets"]}

    def test_held_issue_opened_once(self):
        run = FakeRun({"issue list": "[]", "issue create": "https://github.com/OpenRAC/openrac-mods/issues/9"})
        bot.held({"held": [self.ITEM]}, run, b.Offline(self.fx.dir))
        create = [(a, i) for a, i in run.calls if "create" in a and "issue" in a]
        self.assertEqual(len(create), 1)
        self.assertIn("Held: ext-hud 0.2.0", create[0][0])
        self.assertEqual(bot.held_findings(create[0][1]), ["reads saved secrets"])
        same = json.dumps([{"number": 9, "title": "Held: ext-hud 0.2.0", "state": "OPEN", "body": create[0][1]}])
        run = FakeRun({"issue list": same})
        bot.held({"held": [self.ITEM]}, run, b.Offline(self.fx.dir))
        self.assertFalse(any("edit" in a or "create" in a for a, _ in run.calls if "issue" in a))

    def test_scan_policy_closes_review_issues(self):
        run = FakeRun({"--label mod-review": json.dumps([{"number": 360}])})
        bot.held({"held": [], "policy": {"review": "scan"}}, run, b.Offline(self.fx.dir))
        self.assertTrue(any(a[:4] == ["gh", "issue", "close", "360"] for a, _ in run.calls))

    def answer(self, label, sender="boss"):
        body = bot.held_body(self.ITEM, "scan")
        return {"action": "labeled", "label": {"name": label}, "sender": {"login": sender},
                "issue": {"number": 9, "title": "Held: ext-hud 0.2.0", "body": body,
                          "labels": [{"name": "held"}, {"name": label}]}}

    def test_cleared(self):
        run = FakeRun()
        bot.answer_held(self.answer("cleared"), self.fx.repo, b.Offline(self.fx.dir), run)
        self.assertEqual(json.loads((self.fx.repo / "index/cleared.json").read_text()),
                         {"ext-hud": ["reads saved secrets"]})
        self.assertTrue(any(a[:4] == ["gh", "issue", "close", "9"] for a, _ in run.calls))

    def test_revoke(self):
        bot.answer_held(self.answer("revoke"), self.fx.repo, b.Offline(self.fx.dir), FakeRun())
        revoked = json.loads((self.fx.repo / "index/revoked.json").read_text())
        self.assertTrue(any(r["id"] == "ext-hud" and r["versions"] == ["0.2.0"] for r in revoked))

    def test_only_maintainers(self):
        run = FakeRun()
        bot.answer_held(self.answer("cleared", "stranger"), self.fx.repo, b.Offline(self.fx.dir), run)
        self.assertFalse((self.fx.repo / "index/cleared.json").exists())


class SelfMerge(Base):
    def setUp(self):
        super().setUp()
        (self.fx.repo / "index" / "ext-hud.json").unlink()
        self.fx.edit("lock-strafe/listing.json", lambda d: d.update(suggests=[]))
        self.fields = {"kind": "Mod", "repo": EXT, "mods": "ext-hud", "description": "A HUD.",
                       "tags": "ui", "impact": "cosmetic", "ai": "No", "rules": True}

    def test_trusted_author_merges_by_itself(self):
        r = self.check(event(self.fields))
        self.assertTrue(r["ok"], self.failed(r))
        self.assertFalse(r["auto"])
        write(self.fx, "trust/trusted.json", {"owners": {"someone": {"since": "2026-11-27"}}})
        r = self.check(event(self.fields))
        self.assertTrue(r["auto"])
        write(self.fx, "index/policy.json", {"selfMerge": False})
        self.assertFalse(self.check(event(self.fields))["auto"])

    def test_apply_turns_on_auto_merge(self):
        write(self.fx, "trust/trusted.json", {"owners": {"someone": {"since": "2026-11-27"}}})
        r = self.check(event(self.fields))
        repo = Path(tempfile.mkdtemp())
        try:
            run = FakeRun({"pr list": "[]", "pr create": "https://github.com/OpenRAC/openrac-mods/pull/7",
                           "comments": "[]"})
            bot.apply(r, self.out / "files", repo, run)
            self.assertTrue(any(a[:3] == ["gh", "pr", "merge"] and "--auto" in a for a, _ in run.calls))
        finally:
            shutil.rmtree(repo)


if __name__ == "__main__":
    unittest.main()
