# SPDX-License-Identifier: GPL-3.0-or-later
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import alert  # noqa: E402


class Alert(unittest.TestCase):
    def test_nothing_without_a_webhook(self):
        calls = []
        self.assertEqual(alert.main(["alert", "hi"], env={}, opener=lambda *a, **k: calls.append(a)), 0)
        self.assertEqual(calls, [])

    def test_only_discord_webhooks(self):
        for bad in ["http://discord.com/api/webhooks/1/x", "https://evil.example/api/webhooks/1/x", "https://discord.com/other", "https://discord.com.evil.example/api/webhooks/1/x"]:
            self.assertFalse(alert.valid(bad), bad)
        self.assertTrue(alert.valid("https://discord.com/api/webhooks/1/abc"))

    def test_posts_without_mentions_and_never_fails(self):
        sent = []

        class Res:
            status = 204
            def __enter__(self): return self
            def __exit__(self, *a): return False

        def ok(req, timeout):
            sent.append(json.loads(req.data))
            return Res()

        env = {"DISCORD_WEBHOOK": "https://discord.com/api/webhooks/1/abc"}
        self.assertEqual(alert.main(["alert", "@everyone", "broken"], env=env, opener=ok), 0)
        self.assertEqual(sent[0]["allowed_mentions"], {"parse": []})
        self.assertEqual(sent[0]["content"], "@everyone broken")

        def down(req, timeout):
            raise OSError("down")

        self.assertEqual(alert.main(["alert", "x"], env=env, opener=down), 0)


if __name__ == "__main__":
    unittest.main()
