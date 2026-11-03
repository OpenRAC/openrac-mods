#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""Posts one line to the maintainers' Discord channel.

    DISCORD_WEBHOOK=https://discord.com/api/webhooks/... python3 tools/alert.py "message"

Without DISCORD_WEBHOOK it does nothing: alerts are optional, the GitHub issue is the record.
It never fails the job that calls it (a Discord outage must not look like a broken index), and it
mentions nobody, so no text can ping @everyone.
"""
from __future__ import annotations

import json
import os
import sys
import urllib.parse
import urllib.request

HOSTS = {"discord.com", "discordapp.com", "ptb.discord.com", "canary.discord.com"}


def payload(message: str) -> bytes:
    return json.dumps({"username": "openrac-bot", "content": message[:1900], "allowed_mentions": {"parse": []}}).encode()


def valid(url: str) -> bool:
    u = urllib.parse.urlsplit(url)
    return u.scheme == "https" and u.hostname in HOSTS and u.path.startswith("/api/webhooks/")


def main(argv: list[str], env=os.environ, opener=urllib.request.urlopen) -> int:
    url = env.get("DISCORD_WEBHOOK", "").strip()
    if not url:
        print("alert: no DISCORD_WEBHOOK, skipped")
        return 0
    if not valid(url):
        print("alert: DISCORD_WEBHOOK is not a Discord webhook address, skipped")
        return 0
    message = " ".join(argv[1:]).strip()
    if not message:
        return 0
    req = urllib.request.Request(url, data=payload(message), headers={"Content-Type": "application/json", "User-Agent": "openrac-mods-alert"})
    try:
        with opener(req, timeout=10) as res:
            print(f"alert: sent ({res.status})")
    except Exception as e:  # noqa: BLE001 - reported, never fatal
        print(f"alert: not sent ({type(e).__name__})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
