#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""The SDK for CI until sdk.lock's release exists: sdk/ of OpenRAC at sdk.lock's `dev_ref`.

    python3 tools/dev_sdk.py

Prints the SDK's folder, for -DOPENRAC_SDK. Without a `dev_ref`, runs tools/fetch_sdk.py as usual.
Delete `dev_ref` when SDK 1.12.0 is tagged and pinned; this file can go with it. Stdlib only.
"""
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REPOSITORY = "https://github.com/OpenRAC/OpenRAC"


def git(*args: str, cwd: Path) -> None:
    subprocess.run(["git", *args], cwd=cwd, check=True, stdout=sys.stderr)


def main() -> int:
    ref = json.loads((ROOT / "sdk.lock").read_text()).get("dev_ref")
    if not ref:
        return subprocess.run([sys.executable, str(ROOT / "tools" / "fetch_sdk.py")]).returncode
    checkout = ROOT / ".sdk" / f"dev-{ref}"
    if not (checkout / "sdk" / "cmake").is_dir():
        checkout.mkdir(parents=True, exist_ok=True)
        git("init", "-q", cwd=checkout)
        git("sparse-checkout", "set", "sdk", cwd=checkout)
        git("fetch", "-q", "--depth", "1", "--filter=blob:none", REPOSITORY, ref, cwd=checkout)
        git("checkout", "-q", "FETCH_HEAD", cwd=checkout)
    print((checkout / "sdk").as_posix())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
