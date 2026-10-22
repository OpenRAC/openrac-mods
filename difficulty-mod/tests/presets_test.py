#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The difficulty mod's presets (openrac-mod.json "presets"), which the launcher fills the sliders from.

    presets_test.py

- the sliders are the knobs of src/rules.cpp, in its order, and with the same ranges;
- there are Easy, Retail (the id "default": the launcher then adds no Default of its own), Hard,
  Brutal and Chaos, each setting every slider and the Chaos switch, inside the sliders' ranges;
- Retail is every slider at 1.0, Chaos the same with the switch on;
- Easy is easier and Brutal harder than Hard, where it counts.
"""
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent


def main() -> int:
    m = json.loads((HERE / "openrac-mod.json").read_text())
    knobs = re.findall(r'\{"([a-z_]+)", "[^"]+", ([0-9.]+), ([0-9.]+),', (HERE / "src" / "rules.cpp").read_text())
    sliders = [s for s in m["settings"] if s["type"] == "float"]
    problems = []
    if [s["key"] for s in sliders] != [k for k, _, _ in knobs]:
        problems.append("the float settings are not rules.cpp's knobs, in its order")
    for s, (key, lo, hi) in zip(sliders, knobs):
        if (s["min"], s["max"], s["default"]) != (float(lo), float(hi), 1.0):
            problems.append(f"{key}: the slider's range is not rules.cpp's, or its default not 1.0")
    presets = {p["id"]: p for p in m.get("presets", [])}
    if list(presets) != ["easy", "default", "hard", "brutal", "chaos"]:
        problems.append(f"presets: {list(presets)}, not easy, default (Retail), hard, brutal, chaos")
    every = {s["key"] for s in sliders} | {"chaos"}
    for pid, p in presets.items():
        v = p["values"]
        if set(v) != every:
            problems.append(f"preset {pid}: it does not set every slider and chaos, or sets more")
        for s in sliders:
            x = v.get(s["key"], 1.0)
            if not (s["min"] <= x <= s["max"]):
                problems.append(f"preset {pid}: {s['key']} {x} is out of the slider's range")
    if presets:
        ones = {s["key"]: 1.0 for s in sliders}
        if presets["default"]["values"] != {**ones, "chaos": False} or presets["default"]["name"] != "Retail":
            problems.append("preset default: Retail is every slider at 1.0, Chaos off")
        if presets["chaos"]["values"] != {**ones, "chaos": True}:
            problems.append("preset chaos: every slider at 1.0, Chaos on")
        if any(presets[p]["values"]["chaos"] for p in ("easy", "hard", "brutal")):
            problems.append("only the Chaos preset switches Chaos on")
        e, h, b = (presets[p]["values"] for p in ("easy", "hard", "brutal"))
        if not (e["damage_taken"] < 1.0 < h["damage_taken"] < b["damage_taken"]):
            problems.append("damage to Ratchet: Easy < 1 < Hard < Brutal")
        if not (b["enemy_count"] > h["enemy_count"] > 1.0 and b["damage_dealt"] < h["damage_dealt"] < 1.0):
            problems.append("Brutal has more enemies and deals less damage than Hard")
        if not (e["max_health"] > 1.0 > b["max_health"]):
            problems.append("max health: Easy above 1, Brutal below")
    for p in problems:
        print(f"presets_test: {p}", file=sys.stderr)
    if not problems:
        print("presets_test: ok")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
