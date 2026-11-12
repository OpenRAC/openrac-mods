# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""tools/submit_bot.py: forms, checks, ownership, packs, media table, cool-down, Discussions."""
from __future__ import annotations

import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))
import build_index as b  # noqa: E402
import submit_bot as bot  # noqa: E402
from make_fixtures import png  # noqa: E402
from test_build_index import NOW, Fixture  # noqa: E402

ROOT = HERE.parent.parent
SHOT = "https://github.com/user-attachments/assets/0f1e2d3c-aaaa-bbbb-cccc-1234567890ab"
EXT = "https://github.com/someone/ext-hud"


def body(fields: dict, form="mod-submission") -> str:
    labels = {v: k for k, v in bot.FORMS[form].items()}
    out = []
    for key, label in labels.items():
        value = fields.get(key, "")
        if key == "rules":
            value = "- [X] My mod follows the content rules" if value else "- [ ] My mod follows the content rules"
        out += [f"### {label}", "", value or "_No response_", ""]
    return "\n".join(out)


def event(fields: dict, user="someone", form="mod-submission", action="opened", number=5) -> dict:
    return {"action": action, "issue": {"number": number, "state": "open", "title": "x", "user": {"login": user},
                                        "labels": [{"name": form}], "body": body(fields, form)},
            "sender": {"login": user}}


class FakeRun:
    """Stands in for git and gh: records each command, answers the ones the bot reads."""

    def __init__(self, answers=None, fail=()):
        self.calls: list[tuple[list[str], str | None]] = []
        self.answers = answers or {}
        self.fail = fail

    def __call__(self, args, input=None, check=True):
        self.calls.append((list(args), input))
        joined = " ".join(args)
        if any(f in joined for f in self.fail):
            if check:
                raise subprocess.CalledProcessError(1, args)
            return ""
        for key, answer in self.answers.items():
            if key in joined:
                return answer(args) if callable(answer) else answer
        return ""


class Base(unittest.TestCase):
    def setUp(self):
        self.fx = Fixture()
        self.fx.route(f"https://api.github.com/repos/someone/ext-hud", {"private": False})
        self.fx.route("https://api.github.com/repos/someone/ext-hud/collaborators/stranger/permission",
                      {"permission": "read", "role_name": "read"})
        self.fx.route("https://api.github.com/repos/OpenRAC/openrac-mods/collaborators/boss/permission",
                      {"permission": "admin", "role_name": "maintain"})
        self.fx.route("https://api.github.com/repos/OpenRAC/openrac-mods/collaborators/stranger/permission",
                      {"permission": "read", "role_name": "read"})
        (self.fx.dir / "extra" / "shot.png").write_bytes(png(640, 360))
        self.fx.routes[SHOT] = "extra/shot.png"
        (self.fx.dir / "routes.json").write_text(json.dumps(self.fx.routes))
        self.out = Path(tempfile.mkdtemp())

    def tearDown(self):
        self.fx.close()
        shutil.rmtree(self.out)

    def check(self, ev):
        return bot.check(ev, self.fx.repo, b.Offline(self.fx.dir), NOW, self.out)

    def failed(self, result):
        return [i["text"] for i in result["items"] if not i["ok"]]


class Forms(unittest.TestCase):
    def test_parse(self):
        text = ("### Kind\n\nMod\n\n### Repository\n\nhttps://github.com/a/b\n\n### Tags\n\n_No response_\n\n"
                "### Screenshots\n\n![x](https://github.com/user-attachments/assets/abc) | One\nsecond line\n\n"
                "### Content rules\n\n- [X] My mod follows the content rules\n")
        f = bot.parse_form(text, bot.FORMS["mod-submission"])
        self.assertEqual((f["kind"], f["repo"], f["tags"]), ("Mod", "https://github.com/a/b", ""))
        self.assertTrue(bot.checked(f["rules"]))
        pics, bad = bot.pictures(f["screenshots"])
        self.assertEqual(pics, [("https://github.com/user-attachments/assets/abc", "One")])
        self.assertEqual(bad, ["line 2"])
        self.assertEqual(bot.pictures("![x](https://evil.example/a.png)")[1], ["line 1"])

    def test_pictures_take_what_github_writes_on_upload(self):
        tag = '<img width="1920" height="1080" alt="Image" src="https://github.com/user-attachments/assets/abc" />'
        url = "https://github.com/user-attachments/assets/abc"
        self.assertEqual(bot.pictures(tag), ([(url, "")], []))
        self.assertEqual(bot.pictures(f"{tag} | Grinding"), ([(url, "Grinding")], []))
        # A picture in a repository is not an upload.
        self.assertEqual(bot.pictures("![](https://github.com/a/b/blob/main/docs/shot.jpg)")[1], ["line 1"])

    def test_templates_match_the_bot_and_the_contract(self):
        files = {"mod-submission": "submit-mod.yml", "listing-update": "update-listing.yml",
                 "mod-removal": "remove-mod.yml", "mod-revoke": "revoke.yml"}
        for form, name in files.items():
            text = (ROOT / ".github" / "ISSUE_TEMPLATE" / name).read_text()
            self.assertIn(f"labels: [{form}]", text)
            pairs = re.findall(r"^    id: (\S+)\n    attributes:\n      label: (.+)$", text, re.M)
            self.assertEqual({label: fid for fid, label in pairs}, bot.FORMS[form], name)
        ids = re.findall(r"^    id: (\S+)$", (ROOT / ".github/ISSUE_TEMPLATE/submit-mod.yml").read_text(), re.M)
        self.assertEqual(ids, ["kind", "repo", "mods", "name", "description", "tags", "impact", "ai", "bugs", "discord",
                               "docs", "video", "screenshots", "rules"])
        self.assertIn("info@openrac.dev", (ROOT / ".github/ISSUE_TEMPLATE/report-mod.yml").read_text())

    def test_md_cell_is_inert(self):
        self.assertEqual(bot.md("a|b\n@x <img>"), "a\\|b @​x &lt;img>")


class Submit(Base):
    def setUp(self):
        super().setUp()
        (self.fx.repo / "index" / "ext-hud.json").unlink()
        self.fx.edit("lock-strafe/listing.json", lambda d: d.update(suggests=[]))
        self.fields = {"kind": "Mod", "repo": EXT, "mods": "ext-hud", "description": "A HUD.",
                       "tags": "ui", "impact": "cosmetic", "ai": "No", "video": "https://youtu.be/dQw4w9WgXcQ",
                       "screenshots": f"![s]({SHOT}) | The HUD", "rules": True}

    def test_external_mod(self):
        r = self.check(event(self.fields))
        self.assertEqual(self.failed(r), [])
        self.assertTrue(r["ok"])
        entry = json.loads((self.out / "files" / "index/ext-hud.json").read_text())
        self.assertEqual((entry["repo"], entry["path"], entry["tags"]), (EXT, "", ["ui"]))
        self.assertEqual(entry["media"]["videos"], [{"youtube": "dQw4w9WgXcQ", "caption": "Trailer"}])
        shot = entry["media"]["screenshots"][0]
        self.assertRegex(shot["src"], r"^index/media/ext-hud/[0-9a-f]{12}\.png$")
        self.assertEqual(shot["caption"], "The HUD")
        self.assertTrue((self.out / "files" / shot["src"]).is_file())
        self.assertEqual(r["branch"], "listing/ext-hud")
        self.assertEqual(r["media"][0]["size"], "640x360")

    def test_mod_must_answer_ai_assistance(self):
        r = self.check(event({**self.fields, "ai": ""}))
        self.assertFalse(r["ok"])
        self.assertIn("AI assistance is answered", self.failed(r))

    def test_ai_answer_is_written_per_mod(self):
        for answer, value in (("Yes", True), ("No", False)):
            self.check(event({**self.fields, "ai": answer}))
            entry = json.loads((self.out / "files" / "index/ext-hud.json").read_text())
            self.assertIs(entry["aiAssisted"], value)

    def test_unchanged_listing_only_comments(self):
        first = self.check(event(self.fields))
        self.assertTrue(first["ok"])
        # The same form again, on a checkout that has what the first one wrote.
        r = self.check(event(self.fields))
        self.assertEqual(self.failed(r), [])
        self.assertFalse(r["ok"])
        self.assertTrue(r["unchanged"])
        self.assertEqual((r["changed"], r["deleted"]), ([], []))
        run = FakeRun({"issues/5/comments": "[]"})
        bot.apply(r, self.out / "files", self.fx.repo, run)
        cmds = [" ".join(a) for a, _ in run.calls]
        self.assertFalse(any(" commit " in c or "pr create" in c for c in cmds))
        comment = next(i for a, i in run.calls if a[:3] == ["gh", "issue", "comment"])
        self.assertIn("Nothing to change", comment)
        self.assertNotIn("Not yet", comment)
        self.assertNotIn(bot.MAINTAINERS, comment)

    def test_missing_repository(self):
        r = self.check(event({**self.fields, "repo": "https://github.com/someone/no-such-repo"}))
        self.assertFalse(r["ok"])
        self.assertEqual(self.failed(r), ["The repository is found"])
        self.assertFalse(any(i["text"] == "The repository is public" for i in r["items"]))
        self.assertFalse(r["unchanged"])

    def forbid_permission(self):
        self.fx.routes["https://api.github.com/repos/someone/ext-hud/collaborators/member/permission"] = {
            "status": 403}
        (self.fx.dir / "routes.json").write_text(json.dumps(self.fx.routes))

    def test_unreadable_permission_goes_to_a_maintainer(self):
        self.forbid_permission()
        r = self.check(event(self.fields, user="member"))
        self.assertEqual(self.failed(r), [])
        self.assertTrue(r["ok"])
        self.assertTrue(any("a maintainer confirms it" in i["text"] for i in r["items"]))
        run = FakeRun({"pr list": "[]", "pr create": "https://github.com/OpenRAC/openrac-mods/pull/9\n",
                       "issues/5/comments": "[]"})
        repo = Path(tempfile.mkdtemp())
        try:
            bot.apply(r, self.out / "files", repo, run)
        finally:
            shutil.rmtree(repo)
        create = next(a for a, _ in run.calls if a[:3] == ["gh", "pr", "create"])
        self.assertIn("Maintainer: confirm that @member can speak for someone/ext-hud before merging.",
                      create[create.index("--body") + 1])

    def add_release(self, tag, version, assets):
        """A release newer than the fixture's, with its manifest at the tag."""
        path = self.fx.dir / "api/ext-hud/releases-1.json"
        releases = json.loads(path.read_text())
        asset = lambda n: {"name": n, "size": 10, "browser_download_url": f"https://example.test/{tag}/{n}"}
        path.write_text(json.dumps([{**releases[0], "tag_name": tag, "assets": [asset(n) for n in assets]}, *releases]))
        manifest = json.loads((self.fx.dir / "raw/ext-hud/v0.2.0/openrac-mod.json").read_text())
        self.fx.route(f"https://raw.githubusercontent.com/someone/ext-hud/{tag}/openrac-mod.json",
                      {**manifest, "version": version})

    def test_broken_newest_release_is_named(self):
        # A valid old release would be listed; the author must hear the newest was left out.
        self.add_release("v0.3.0", "0.3.0", ["ext-hud-v0.3.0-linux-x86_64.zip"])
        r = self.check(event(self.fields))
        self.assertFalse(r["ok"])
        self.assertEqual(self.failed(r), ["ext-hud: the newest release, v0.3.0, can be listed"])
        fix = next(i["fix"] for i in r["items"] if not i["ok"])
        self.assertTrue(fix.startswith("no ext-hud-0.3.0-&lt;platform>.zip archive."), fix)

    def test_broken_older_release_is_fine(self):
        # The newest is good; an old broken one is left out quietly, as the index does.
        path = self.fx.dir / "api/ext-hud/releases-1.json"
        releases = json.loads(path.read_text())
        path.write_text(json.dumps([*releases, {**releases[0], "tag_name": "v0.1.0", "assets": []}]))
        manifest = json.loads((self.fx.dir / "raw/ext-hud/v0.2.0/openrac-mod.json").read_text())
        self.fx.route("https://raw.githubusercontent.com/someone/ext-hud/v0.1.0/openrac-mod.json",
                      {**manifest, "version": "0.1.0"})
        r = self.check(event(self.fields))
        self.assertEqual(self.failed(r), [])
        self.assertTrue(r["ok"])

    def test_ownership_refused(self):
        r = self.check(event(self.fields, user="stranger"))
        self.assertFalse(r["ok"])
        self.assertIn("You can push to someone/ext-hud", self.failed(r))

    def test_rules_and_bad_fields_refused(self):
        r = self.check(event(dict(self.fields, rules=False, tags="ui, bogus", video="https://vimeo.com/1",
                                  screenshots="https://evil.example/x.png | x")))
        failed = " / ".join(self.failed(r))
        for needle in ("content rules", "tags", "YouTube", "one picture"):
            self.assertIn(needle, failed)

    def test_injection_stays_text(self):
        evil = "$(touch /tmp/pwned) `id` ${{ secrets.MOD_INDEX_MINISIGN_KEY }}"
        with mock.patch("subprocess.run", side_effect=AssertionError("ran a command")):
            r = self.check(event(dict(self.fields, description=evil[:300])))
        entry = json.loads((self.out / "files" / "index/ext-hud.json").read_text())
        self.assertEqual(entry["description"], evil)
        self.assertNotIn("\n", bot.md(evil))
        run = FakeRun({"pr list": "[]", "pr create": "https://github.com/OpenRAC/openrac-mods/pull/9",
                       "issues/5/comments": "[]"})
        repo = Path(tempfile.mkdtemp())
        try:
            bot.apply(r, self.out / "files", repo, run)
        finally:
            shutil.rmtree(repo)
        for args, _ in run.calls:
            self.assertIsInstance(args, list)
            self.assertNotEqual(args[0], "sh")
            self.assertFalse(any(evil in a for a in args), args)

    def test_apply_opens_pr_and_comments(self):
        r = self.check(event(self.fields))
        run = FakeRun({"pr list": "[]", "pr create": "https://github.com/OpenRAC/openrac-mods/pull/9\n",
                       "issues/5/comments": "[]"})
        repo = Path(tempfile.mkdtemp())
        try:
            bot.apply(r, self.out / "files", repo, run)
            self.assertTrue((repo / "index/ext-hud.json").is_file())
        finally:
            shutil.rmtree(repo)
        cmds = [" ".join(a) for a, _ in run.calls]
        self.assertTrue(any("push --force origin HEAD:refs/heads/listing/ext-hud" in c for c in cmds))
        comment = next(i for a, i in run.calls if a[:3] == ["gh", "issue", "comment"])
        self.assertIn("✅ Ready: https://github.com/OpenRAC/openrac-mods/pull/9", comment)
        # The pull request pings the maintainers: a mention added by editing the comment would not.
        pr = next(a for a, _ in run.calls if a[:3] == ["gh", "pr", "create"])
        self.assertIn(bot.MAINTAINERS, pr[pr.index("--body") + 1])
        self.assertIn("| File | In | Out (1600w AVIF) | Saved | Notes |", comment)
        # An edit updates the bot's own comment.
        run2 = FakeRun({"pr list": '[{"url": "https://github.com/OpenRAC/openrac-mods/pull/9"}]',
                        "issues/5/comments": json.dumps([{"id": 77, "body": bot.MARKER + " old"}])})
        repo = Path(tempfile.mkdtemp())
        try:
            bot.apply(r, self.out / "files", repo, run2)
        finally:
            shutil.rmtree(repo)
        self.assertTrue(any("issues/comments/77" in " ".join(a) for a, _ in run2.calls))
        self.assertFalse(any(a[:3] == ["gh", "pr", "create"] for a, _ in run2.calls))

    def test_not_ok_comments_without_pr(self):
        r = self.check(event(self.fields, user="stranger"))
        run = FakeRun({"issues/5/comments": "[]"})
        bot.apply(r, self.out / "files", self.fx.repo, run)
        self.assertFalse(any(a[0] == "git" for a, _ in run.calls))
        comment = next(i for a, i in run.calls if a[:3] == ["gh", "issue", "comment"])
        self.assertIn("❌ Not yet", comment)
        self.assertIn("write access", comment)

    def test_unexpected_error_message(self):
        with mock.patch.object(bot.Bot, "submit", side_effect=KeyError("boom")), \
                mock.patch("traceback.print_exc"):
            r = self.check(event(self.fields))
        text = bot.render(r, run="42")
        self.assertIn("Something went wrong on our side (run #42)", text)
        self.assertIn(bot.MAINTAINERS, text)

    def test_media_table(self):
        res = {"title": "t", "items": [], "ok": False,
               "media": [{"file": "shot1.png", "in": 4_300_000, "out": 186_000, "notes": ["GPS removed"]}]}
        text = bot.render(res)
        self.assertIn("| shot1.png | 4.1 MB | 182 KB | 96% | GPS removed |", text)


class Pack(Base):
    def test_pack(self):
        r = self.check(event({"kind": "Modpack", "name": "Night pack", "mods": "lock-strafe@>=1.1.0, button-swap",
                              "description": "Two mods.", "tags": "controls", "rules": True}, user="anyone"))
        self.assertEqual(self.failed(r), [])
        entry = json.loads((self.out / "files" / "index/night-pack.json").read_text())
        self.assertEqual(entry["type"], "pack")
        self.assertEqual(entry["author"], "anyone")
        self.assertEqual(entry["versions"][0]["requires"], [{"id": "lock-strafe", "version": ">=1.1.0"},
                                                           {"id": "button-swap"}])
        self.assertEqual(entry["versions"][0]["version"], "1.0.0")

    def test_pack_needs_known_mods(self):
        r = self.check(event({"kind": "Modpack", "name": "Bad", "mods": "lock-strafe, nope", "description": "x",
                              "rules": True}, user="anyone"))
        self.assertIn("Every mod of the pack is in the index", self.failed(r))
        self.assertFalse((self.out / "files" / "index/bad.json").exists())

    def test_pack_name_taken_by_someone_else(self):
        r = self.check(event({"kind": "Modpack", "name": "Starter pack", "mods": "lock-strafe",
                              "description": "x", "rules": True}, user="stranger"))
        self.assertIn("The id starter-pack is free", self.failed(r))


class OtherForms(Base):
    def test_update_listing(self):
        r = self.check(event({"mod": "ext-hud", "tags": "ui, tools"}, form="listing-update"))
        self.assertTrue(r["ok"], self.failed(r))
        entry = json.loads((self.out / "files" / "index/ext-hud.json").read_text())
        self.assertEqual(entry["tags"], ["ui", "tools"])
        self.assertEqual(entry["impact"], ["cosmetic"])  # kept

    def test_update_keeps_ai_answer_when_left_empty(self):
        self.fx.edit("index/ext-hud.json", lambda d: d.update(aiAssisted=True))
        self.check(event({"mod": "ext-hud", "tags": "ui"}, form="listing-update"))
        entry = json.loads((self.out / "files" / "index/ext-hud.json").read_text())
        self.assertIs(entry["aiAssisted"], True)
        self.check(event({"mod": "ext-hud", "ai": "No"}, form="listing-update"))
        entry = json.loads((self.out / "files" / "index/ext-hud.json").read_text())
        self.assertIs(entry["aiAssisted"], False)

    def test_update_refused_for_stranger(self):
        r = self.check(event({"mod": "ext-hud", "tags": "ui"}, user="stranger", form="listing-update"))
        self.assertFalse(r["ok"])

    def test_unreadable_permission_is_refused_for_a_removal(self):
        self.fx.routes["https://api.github.com/repos/someone/ext-hud/collaborators/member/permission"] = {
            "status": 403}
        (self.fx.dir / "routes.json").write_text(json.dumps(self.fx.routes))
        r = self.check(event({"mod": "ext-hud", "reason": "x"}, user="member", form="mod-removal"))
        self.assertFalse(r["ok"])
        self.assertFalse(r.get("unchanged"))

    def test_remove_blocked_while_others_point_at_it(self):
        r = self.check(event({"mod": "ext-hud"}, form="mod-removal"))
        self.assertFalse(r["ok"])  # lock-strafe suggests ext-hud
        # Each run starts from a fresh checkout: put the entry back.
        shutil.copy(HERE / "fixtures/github/repo/index/ext-hud.json", self.fx.repo / "index/ext-hud.json")
        self.fx.edit("lock-strafe/listing.json", lambda d: d.update(suggests=[]))
        r = self.check(event({"mod": "ext-hud"}, form="mod-removal"))
        self.assertTrue(r["ok"], self.failed(r))
        self.assertIn("index/ext-hud.json", r["deleted"])

    def test_revoke_is_maintainers_only(self):
        r = self.check(event({"mod": "button-swap", "versions": "1.0.0", "reason": "Broke"}, user="stranger",
                             form="mod-revoke"))
        self.assertTrue(r["skip"])
        r = self.check(event({"mod": "button-swap", "versions": "1.0.0", "reason": "Broke"}, user="boss",
                             form="mod-revoke"))
        self.assertTrue(r["ok"], self.failed(r))
        revoked = json.loads((self.out / "files" / "index/revoked.json").read_text())
        self.assertIn({"id": "button-swap", "versions": ["1.0.0"], "reason": "Broke"}, revoked)
        self.assertEqual(r["branch"], "revoke/button-swap")

    def test_other_labels_ignored(self):
        ev = event({"mod": "x"}, form="mod-removal", action="labeled")
        ev["label"] = {"name": "approved"}
        self.assertTrue(self.check(ev)["skip"])


class CoolDown(Base):
    def build(self, now=NOW):
        review = []
        index, errors, warnings = b.build(self.fx.repo, b.Offline(self.fx.dir), now, None, True,
                                          log=lambda m: None, review=review)
        return index, errors, review

    def test_approved_and_old_is_published(self):
        index, errors, review = self.build()
        self.assertEqual(errors, [])
        self.assertIn("ext-hud", [m["id"] for m in index["mods"]])
        self.assertEqual(review, [])

    def test_not_approved_waits_for_review(self):
        (self.fx.repo / "index/approved.json").write_text("{}")
        index, errors, review = self.build()
        self.assertNotIn("ext-hud", [m["id"] for m in index["mods"]])
        self.assertEqual([(r["id"], r["version"]) for r in review], [("ext-hud", "0.2.0")])
        self.assertIn("linux-x86_64", review[0]["platforms"])

    def test_approved_but_young_waits(self):
        released = 1791633600  # 2026-10-10T12:00:00Z, ext-hud 0.2.0
        index, _, review = self.build(now=released + 1800)
        self.assertNotIn("ext-hud", [m["id"] for m in index["mods"]])
        index, _, _ = self.build(now=released + 2 * 3600)
        self.assertIn("ext-hud", [m["id"] for m in index["mods"]])

    def test_in_repo_and_script_not_held(self):
        (self.fx.repo / "index/approved.json").write_text("{}")
        index, _, _ = self.build()
        self.assertTrue({"lock-strafe", "button-swap"} <= {m["id"] for m in index["mods"]})

    def test_bad_approved_file_fails(self):
        (self.fx.repo / "index/approved.json").write_text('{"ext-hud": "0.2.0"}')
        _, errors, _ = self.build()
        self.assertTrue(any("approved.json" in e for e in errors))

    def test_review_issue(self):
        (self.fx.repo / "index/approved.json").write_text("{}")
        _, _, review = self.build()
        run = FakeRun({"issue list": "[]"})
        bot.review(review, run, b.Offline(self.fx.dir))
        create = next((a, i) for a, i in run.calls if a[:3] == ["gh", "issue", "create"])
        self.assertIn("### What the libraries can do", create[1])
        self.assertIn("Review ext-hud 0.2.0", create[0])
        self.assertIn(review[0]["platforms"]["linux-x86_64"]["sha256"], create[1])
        self.assertIn(bot.MAINTAINERS, create[1])
        # An open issue of that title is updated, not doubled.
        run = FakeRun({"issue list": json.dumps([{"number": 3, "title": "Review ext-hud 0.2.0", "state": "OPEN"}])})
        bot.review(review, run, b.Offline(self.fx.dir))
        self.assertTrue(any(a[:4] == ["gh", "issue", "edit", "3"] for a, _ in run.calls))
        self.assertFalse(any(a[:3] == ["gh", "issue", "create"] for a, _ in run.calls))

    SHA = {"linux-x86_64": "a" * 64, "windows-x86_64": "b" * 64}

    def approve_event(self, sender, sha=SHA):
        body = bot.review_body({"id": "ext-hud", "name": "HUD", "repo": "https://github.com/o/r", "version": "0.3.0",
                                "tag": "v0.3.0", "released": "", "changelog": "Fixes",
                                "platforms": {p: {"asset": "x.zip", "sha256": h, "size": 1} for p, h in sha.items()}})
        return {"action": "labeled", "label": {"name": "approved"}, "sender": {"login": sender},
                "issue": {"number": 3, "title": "Review ext-hud 0.3.0", "body": body,
                          "labels": [{"name": "mod-review"}, {"name": "approved"}]}}

    def test_approve_by_maintainer(self):
        run = FakeRun()
        bot.approve(self.approve_event("boss"), self.fx.repo, b.Offline(self.fx.dir), run)
        self.assertEqual(json.loads((self.fx.repo / "index/approved.json").read_text()),
                         {"ext-hud": ["0.2.0", {"version": "0.3.0", "sha256": self.SHA}]})
        cmds = [" ".join(a) for a, _ in run.calls]
        self.assertIn("push origin HEAD:refs/heads/master", " / ".join(cmds))
        self.assertTrue(any(c.startswith("gh issue close 3") for c in cmds))

    def test_approve_falls_back_to_pr(self):
        path = self.fx.repo / "index/approved.json"
        before = path.read_text()
        run = FakeRun({"pr create": "https://github.com/OpenRAC/openrac-mods/pull/4", "pr list": "[]",
                       "reset --hard": lambda a: path.write_text(before) and ""},
                      fail=("HEAD:refs/heads/master",))
        bot.approve(self.approve_event("boss"), self.fx.repo, b.Offline(self.fx.dir), run)
        self.assertTrue(any("approve/ext-hud-0.3.0" in " ".join(a) for a, _ in run.calls))
        cmds = [" ".join(a) for a, _ in run.calls]
        self.assertEqual(sum("push origin HEAD:refs/heads/master" in c for c in cmds), 4)  # retried on new master
        self.assertTrue(any("pr create" in c and "Closes #3" in c for c in cmds))

    def test_approve_twice_closes_without_a_commit(self):
        (self.fx.repo / "index/approved.json").write_text(
            json.dumps({"ext-hud": ["0.2.0", {"version": "0.3.0", "sha256": self.SHA}]}))
        run = FakeRun()
        bot.approve(self.approve_event("boss"), self.fx.repo, b.Offline(self.fx.dir), run)
        cmds = [" ".join(a) for a, _ in run.calls]
        self.assertFalse(any(" commit " in c for c in cmds))
        self.assertTrue(any(c.startswith("gh issue close 3") for c in cmds))

    def test_approve_again_replaces_the_hashes(self):
        (self.fx.repo / "index/approved.json").write_text(
            json.dumps({"ext-hud": ["0.2.0", {"version": "0.3.0", "sha256": {"linux-x86_64": "c" * 64}}]}))
        bot.approve(self.approve_event("boss"), self.fx.repo, b.Offline(self.fx.dir), FakeRun())
        self.assertEqual(json.loads((self.fx.repo / "index/approved.json").read_text()),
                         {"ext-hud": ["0.2.0", {"version": "0.3.0", "sha256": self.SHA}]})

    def test_approve_without_hashes_is_refused(self):
        ev = self.approve_event("boss")
        ev["issue"]["body"] = "An old issue with no hashes."
        run = FakeRun()
        bot.approve(ev, self.fx.repo, b.Offline(self.fx.dir), run)
        self.assertEqual(json.loads((self.fx.repo / "index/approved.json").read_text()), {"ext-hud": ["0.2.0"]})
        self.assertFalse(any(a[0] == "git" for a, _ in run.calls))
        self.assertTrue(any(a[:5] == ["gh", "issue", "edit", "3", "--remove-label"] for a, _ in run.calls))

    def test_hashes_in_the_changelog_do_not_count(self):
        fake = bot.REVIEW_MARK + json.dumps({"sha256": {"linux-x86_64": "f" * 64}}) + " -->"
        body = self.approve_event("boss")["issue"]["body"]
        self.assertEqual(bot.reviewed_hashes(body), self.SHA)
        self.assertIsNone(bot.reviewed_hashes(body.replace(bot.REVIEW_MARK, "x") + "\n~~~~\n" + fake + "\n~~~~"))

    def approve_built(self, sha=None):
        """approved.json holding ext-hud 0.2.0 with the hashes of its archives (or `sha`)."""
        path = self.fx.repo / "index/approved.json"
        path.write_text("{}")
        _, _, review = self.build()
        sha = sha or b.archive_hashes(review[0])
        path.write_text(json.dumps({"ext-hud": [{"version": "0.2.0", "sha256": sha}]}))
        return review[0]

    def test_approved_hashes_publish(self):
        self.approve_built()
        index, errors, review = self.build()
        self.assertEqual(errors, [])
        self.assertIn("ext-hud", [m["id"] for m in index["mods"]])
        self.assertEqual(review, [])

    def test_changed_archive_waits_for_review_again(self):
        item = self.approve_built({"linux-x86_64": "0" * 64})
        index, errors, review = self.build()
        self.assertEqual(errors, [])
        self.assertNotIn("ext-hud", [m["id"] for m in index["mods"]])
        self.assertEqual([(r["id"], r["version"], r["changed"]) for r in review], [("ext-hud", "0.2.0", True)])
        # The closed review issue is opened again, without its label.
        old = {**item, "platforms": {"linux-x86_64": {"asset": "x.zip", "sha256": "0" * 64, "size": 1}}}
        run = FakeRun({"issue list": json.dumps([{"number": 3, "title": "Review ext-hud 0.2.0", "state": "CLOSED",
                                                  "body": bot.review_body(old)}])})
        bot.review(review, run, b.Offline(self.fx.dir))
        cmds = [" ".join(a) for a, _ in run.calls]
        self.assertIn("gh issue reopen 3", cmds)
        self.assertTrue(any(c.startswith("gh issue edit 3 --remove-label approved") for c in cmds))

    def test_closed_again_is_not_reopened_every_hour(self):
        self.approve_built({"linux-x86_64": "0" * 64})
        _, _, review = self.build()
        run = FakeRun({"issue list": json.dumps([{"number": 3, "title": "Review ext-hud 0.2.0", "state": "CLOSED",
                                                  "body": bot.review_body(review[0])}])})
        bot.review(review, run, b.Offline(self.fx.dir))
        self.assertFalse(any(a[:3] == ["gh", "issue", "reopen"] for a, _ in run.calls))

    def test_platform_set_differs_after_approval_waits(self):
        # Approved for other archives than the release has now (one added, one taken away): not the same.
        item = self.approve_built()
        sha = {**b.archive_hashes(item), "windows-x86_64": "1" * 64}
        approved = {"ext-hud": [{"version": "0.2.0", "sha256": sha}]}
        (self.fx.repo / "index/approved.json").write_text(json.dumps(approved))
        index, _, review = self.build()
        self.assertNotIn("ext-hud", [m["id"] for m in index["mods"]])
        self.assertTrue(review[0]["changed"])

    def test_approve_reads_a_body_with_crlf(self):
        ev = self.approve_event("boss")
        ev["issue"]["body"] = ev["issue"]["body"].replace("\n", "\r\n") + "\r\n"
        bot.approve(ev, self.fx.repo, b.Offline(self.fx.dir), FakeRun())
        self.assertEqual(json.loads((self.fx.repo / "index/approved.json").read_text())["ext-hud"][-1],
                         {"version": "0.3.0", "sha256": self.SHA})

    def test_bad_hash_in_approved_file_fails(self):
        (self.fx.repo / "index/approved.json").write_text(
            json.dumps({"ext-hud": [{"version": "0.2.0", "sha256": {"linux-x86_64": "XYZ"}}]}))
        _, errors, _ = self.build()
        self.assertTrue(any("approved.json" in e for e in errors))

    def test_archive_changed_while_issue_open(self):
        (self.fx.repo / "index/approved.json").write_text("{}")
        _, _, review = self.build()
        old = {**review[0], "platforms": {"linux-x86_64": {"asset": "x.zip", "sha256": "0" * 64, "size": 1}}}
        run = FakeRun({"issue list": json.dumps([{"number": 3, "title": "Review ext-hud 0.2.0", "state": "OPEN",
                                                  "body": bot.review_body(old)}])})
        bot.review(review, run, b.Offline(self.fx.dir))
        cmds = [" ".join(a) for a, _ in run.calls]
        self.assertIn("gh issue edit 3 --remove-label approved", cmds)
        self.assertTrue(any(c.startswith("gh issue comment 3") for c in cmds))

    def test_approve_refused_for_others(self):
        run = FakeRun()
        bot.approve(self.approve_event("stranger"), self.fx.repo, b.Offline(self.fx.dir), run)
        self.assertEqual(json.loads((self.fx.repo / "index/approved.json").read_text()), {"ext-hud": ["0.2.0"]})
        self.assertFalse(any(a[0] == "git" for a, _ in run.calls))


class Discussions(Base):
    def index(self):
        index, errors, _ = b.build(self.fx.repo, b.Offline(self.fx.dir), NOW, None, True, log=lambda m: None)
        self.assertEqual(errors, [])
        return index

    def test_number_in_index(self):
        index = self.index()
        ls = next(m for m in index["mods"] if m["id"] == "lock-strafe")
        self.assertEqual(ls["discussion"], 7)
        self.assertNotIn("discussion", next(m for m in index["mods"] if m["id"] == "button-swap"))

    def graph(self, enabled=True, existing=()):
        def answer(args):
            q = next(a for a in args if a.startswith("query="))
            if "createDiscussion" in q:
                answer.made += 1
                return json.dumps({"data": {"createDiscussion": {"discussion": {"number": 100 + answer.made}}}})
            return json.dumps({"data": {"repository": {
                "id": "R1", "hasDiscussionsEnabled": enabled,
                "discussionCategories": {"nodes": [{"id": "C1", "name": "Mods"}]},
                "discussions": {"nodes": [{"number": n, "title": t} for n, t in existing]}}}})
        answer.made = 0
        return FakeRun({"graphql": answer})

    def test_disabled_skips(self):
        run = self.graph(enabled=False)
        with mock.patch("sys.stderr"):
            bot.discussions(self.index(), self.fx.repo, run)
        self.assertEqual(json.loads((self.fx.repo / "index/discussions.json").read_text()), {"lock-strafe": 7})
        self.assertFalse(any(a[0] == "git" for a, _ in run.calls))

    def test_creates_missing_and_reuses_existing(self):
        run = self.graph(existing=[(50, "Button swap (button-swap)")])
        bot.discussions(self.index(), self.fx.repo, run)
        numbers = json.loads((self.fx.repo / "index/discussions.json").read_text())
        self.assertEqual(numbers["lock-strafe"], 7)
        self.assertEqual(numbers["button-swap"], 50)
        self.assertEqual(set(numbers), {m["id"] for m in self.index()["mods"]})
        self.assertTrue(any("index/discussions.json" in a for a, _ in run.calls for a in [" ".join(a)]))


if __name__ == "__main__":
    unittest.main()
