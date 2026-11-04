# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

SCRIPT = Path(__file__).resolve().parents[1] / "extras/tools/setup_deck.py"
spec = importlib.util.spec_from_file_location("setup_deck", SCRIPT)
deck = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deck)


def elf(machine=62):
    value = bytearray(64)
    value[:6] = b"\x7fELF\x02\x01"
    value[18:20] = machine.to_bytes(2, "little")
    return value


class SetupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name).resolve()
        self.game = self.root / "openrac-rac1-pal"
        self.game.write_bytes(elf())
        self.game.chmod(0o755)
        self.data = self.root / "existing game data"
        (self.data / "port/pal/level_99").mkdir(parents=True)
        (self.data / "port/pal/level_99/manifest.json").write_text("{}")
        (self.data / "iso_data/rac1").mkdir(parents=True)
        (self.data / "iso_data/rac1/disc.iso").write_bytes(b"not game data; test fixture")
        (self.data / "mods/pal").mkdir(parents=True)
        (self.data / "probes/pal").mkdir(parents=True)
        (self.data / "plugins/xbox-icons").mkdir(parents=True)
        (self.data / "plugins/xbox-icons/openrac-mod.json").write_text('{"id":"xbox-icons"}')
        self.plugins = {"format": 1, "mods": [{"id": "xbox-icons", "enabled": True, "settings": {"trace": False}}]}
        self.original_plugins = self.data / "plugins/plugins.json"
        self.original_plugins.write_text(json.dumps(self.plugins))
        self.package = self.root / "package"
        (self.package / "lib/linux-x86_64").mkdir(parents=True)
        (self.package / "lib/linux-x86_64/steam_deck_pack.so").write_bytes(elf())
        self.manifest = {"id": "steam-deck-pack", "games": {"rac1": ["pal", "ntsc"]},
                         "plugin": {"linux-x86_64": "lib/linux-x86_64/steam_deck_pack.so"}}
        self.write_manifest()
        self.settings = self.root / "original.ini"
        self.settings.write_text("# old\naspect = 4:3\nvolume = 72\ngame_language = 4\naspect = 16:9\n")
        self.output = self.root / "new pack"

    def tearDown(self):
        self.temp.cleanup()

    def write_manifest(self):
        (self.package / "openrac-mod.json").write_text(json.dumps(self.manifest))

    def plan(self, **extra):
        return deck.plan(self.game, self.data, self.package, self.output, "pal", self.settings, **extra)

    def test_preflight_and_isolation(self):
        original_settings = self.settings.read_bytes()
        original_plugins = self.original_plugins.read_bytes()
        config = self.plan()
        self.assertFalse(self.output.exists())
        start = deck.install(config)
        self.assertTrue(start.is_file() and os.access(start, os.X_OK))
        self.assertEqual(self.settings.read_bytes(), original_settings)
        self.assertEqual(self.original_plugins.read_bytes(), original_plugins)
        settings = (self.output / "profiles/balanced.ini").read_text()
        self.assertIn("game_language = 4", settings)
        self.assertIn("volume = 72", settings)
        self.assertEqual(settings.count("aspect ="), 1)
        self.assertIn("aspect = 16:10", settings)
        self.assertIn("frame_rate = 40", (self.output / "profiles/battery.ini").read_text())
        snapshot = json.loads((self.output / "plugins/plugins.json").read_text())
        self.assertFalse(snapshot["mods"][0]["enabled"])
        self.assertEqual(snapshot["mods"][0]["settings"], self.plugins["mods"][0]["settings"])
        self.assertTrue(json.loads(self.original_plugins.read_text())["mods"][0]["enabled"])
        self.assertEqual(snapshot["mods"][-1]["id"], "steam-deck-pack")
        self.assertTrue((self.output / "plugins/xbox-icons").is_symlink())
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.plan()

    def test_wrong_platform_and_missing_data(self):
        self.game.write_bytes(b"macOS Mach-O is not Linux")
        with self.assertRaisesRegex(ValueError, "ELF"):
            self.plan()
        self.game.write_bytes(elf(183)) # Linux arm64 is not the Deck's x86-64.
        with self.assertRaisesRegex(ValueError, "ELF"):
            self.plan()
        self.game.write_bytes(elf())
        self.manifest["plugin"] = {"macos-arm64": "lib/mac.dylib"}
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, "no Linux"):
            self.plan()
        self.manifest["plugin"] = {"linux-x86_64": "../openrac-rac1-pal"}
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, "leaves"):
            self.plan()

    def test_keyboard_mouse_conflict_only_changes_snapshot(self):
        (self.data / "plugins/keyboard-mouse").mkdir()
        (self.data / "plugins/keyboard-mouse/openrac-mod.json").write_text("{}")
        self.plugins["mods"].append({"id": "keyboard-mouse", "enabled": True})
        self.original_plugins.write_text(json.dumps(self.plugins))
        with self.assertRaisesRegex(ValueError, "owns the mouse"):
            self.plan()
        config = self.plan(disable_keyboard_mouse=True)
        self.assertFalse(config["plugins"]["mods"][1]["enabled"])
        self.assertTrue(json.loads(self.original_plugins.read_text())["mods"][1]["enabled"])

    def test_flat_legacy_pal_and_duplicate_ids(self):
        source = self.data / "port/pal/level_99"
        source.rename(self.data / "port/level_99")
        self.assertEqual(self.plan()["levels"], self.data / "port")
        self.plugins["mods"] *= 2
        self.original_plugins.write_text(json.dumps(self.plugins))
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            self.plan()

    def test_generated_shell_keeps_paths_and_arguments_literal(self):
        if os.name == "nt":
            self.skipTest("Generated launcher targets native SteamOS / POSIX sh")
        self.game = self.root / "game '$HOME;touch PWNED %"
        self.game.write_text("#!/usr/bin/env python3\nimport os,sys,json\nfrom pathlib import Path\nPath(os.environ['DECK_TEST_RECORD']).write_text(json.dumps(sys.argv[1:]))\n")
        self.game.chmod(0o755)
        self.output = self.root / "Deck '$HOME;touch PWNED %"
        with mock.patch.object(deck, "validate_binary"):
            start = deck.install(self.plan())
        record = self.root / "record.json"
        env = dict(os.environ, DECK_TEST_RECORD=str(record), OPENRAC_DECK_PRESET="battery")
        subprocess.run(["/bin/sh", str(start), "--frames", "1"], env=env, check=True)
        argv = json.loads(record.read_text())
        self.assertEqual(argv[argv.index("--data") + 1], str(self.data))
        self.assertEqual(argv[argv.index("--settings") + 1], str(self.output / "profiles/battery.ini"))
        self.assertEqual(argv[-2:], ["--frames", "1"])
        self.assertNotIn("--no-mods", argv)
        self.assertFalse((self.data / "PWNED").exists())
        env["OPENRAC_DECK_PRESET"] = "../../outside"
        result = subprocess.run(["/bin/sh", str(start)], env=env, capture_output=True)
        self.assertEqual(result.returncode, 64)
        desktop = (self.output / "Ratchet & Clank 1.desktop").read_text()
        self.assertIn("%%", desktop)
        self.assertIn("\\$HOME", desktop)

    def test_copy_failure_leaves_no_partial_pack(self):
        config = self.plan()
        with mock.patch.object(deck.shutil, "copytree", side_effect=OSError("copy failed")):
            with self.assertRaises(OSError):
                deck.install(config)
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob(".deck-pack-*")))
        self.assertEqual(json.loads(self.original_plugins.read_text()), self.plugins)

    def test_temporary_appimage_mount_is_not_a_standalone_install(self):
        mount = self.root / ".mount_OpenRACabc"
        mount.mkdir()
        self.game = mount / "game"
        self.game.write_bytes(elf())
        self.game.chmod(0o755)
        with self.assertRaisesRegex(ValueError, "temporary AppImage"):
            self.plan()

    def test_cannot_recursively_copy_package_into_itself(self):
        self.output = self.package / "nested pack"
        with self.assertRaisesRegex(ValueError, "outside the package"):
            self.plan()

    def test_desktop_exec_uses_both_escape_layers(self):
        original = '/home/deck/A \\ " $ ` 100%/start.sh'
        encoded = deck.desktop_quote(original)
        # General desktop string escaping runs first; every escaped pair here is a backslash.
        general, index = "", 0
        while index < len(encoded):
            if encoded[index] == "\\":
                self.assertEqual(encoded[index + 1], "\\")
                general += "\\"
                index += 2
            else:
                general += encoded[index]
                index += 1
        self.assertEqual(general[0], '"')
        self.assertEqual(general[-1], '"')
        # Exec double-quote parsing then consumes escapes for \\, ", ` and $.
        argument, index = "", 1
        while index < len(general) - 1:
            if general[index] == "\\":
                index += 1
            argument += general[index]
            index += 1
        self.assertEqual(argument.replace("%%", "%"), original)

    def test_pack_needs_no_separate_icon_mod(self):
        self.original_plugins.write_text(json.dumps({"format": 1, "mods": []}))
        config = self.plan()
        self.assertEqual([entry["id"] for entry in config["plugins"]["mods"]], ["steam-deck-pack"])
        deck.install(config)
        self.assertFalse((self.output / "plugins/xbox-icons").exists())
        self.assertTrue((self.output / "plugins/steam-deck-pack/openrac-mod.json").is_file())

    def test_dry_run_writes_nothing(self):
        result = subprocess.run([sys.executable, str(SCRIPT), "--game", str(self.game),
                                 "--data", str(self.data), "--package", str(self.package),
                                 "--output", str(self.output), "--settings", str(self.settings), "--dry-run"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.output.exists())
        self.assertIn("no files written", result.stdout)


if __name__ == "__main__":
    unittest.main()
