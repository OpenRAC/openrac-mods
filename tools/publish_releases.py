#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Publish built archives for a tag or a pending randomizer release request."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)


def release_tags(ref):
    if ref.startswith("refs/tags/"):
        return [ref.removeprefix("refs/tags/")]
    if ref not in ("refs/heads/master", "refs/heads/main"):
        return []
    tags = []
    for mod in ("archipelago", "item-randomizer"):
        request = Path(f".github/mod-releases/{mod}.json")
        if not request.is_file():
            continue
        path = f"{mod}/openrac-mod.json"
        current = json.loads(Path(path).read_text(encoding="utf-8"))
        version = json.loads(request.read_text(encoding="utf-8"))["version"]
        # A later mod version needs its own explicit release request.
        if version != current["version"]:
            continue
        if not re.fullmatch(r"\d+\.\d+\.\d+", version):
            raise ValueError(f"Unsupported automatic release version: {version}")
        if current["id"] != mod:
            raise ValueError(f"Manifest id does not match {mod}")
        tag = f"{mod}-v{version}"
        existing = subprocess.run(
            ["git", "show-ref", "--verify", "--quiet", f"refs/tags/{tag}"],
            check=False,
        )
        if existing.returncode == 1:
            tags.append(tag)
        elif existing.returncode != 0:
            raise ValueError(f"Cannot check release tag {tag}")
    return tags


def release_files(dist, tag):
    if re.fullmatch(r"v\d[^/]*", tag):
        files = sorted([*dist.glob("*.zip"), *dist.glob("*.apworld")])
    else:
        match = re.fullmatch(r"([a-z0-9-]+)-v(\d[^/]*)", tag)
        if not match:
            raise ValueError(f"Unsupported release tag: {tag}")
        mod, version = match.groups()
        merged = dist / f"{mod}-{version}.zip"
        if not merged.is_file():
            raise ValueError(f"Missing merged archive: {merged}")
        files = sorted([merged, *dist.glob(f"{mod}-{version}-*.zip")])
        if mod == "archipelago":
            apworld = dist / "rac1.apworld"
            if not apworld.is_file():
                raise ValueError("Archipelago release is missing rac1.apworld")
            files.append(apworld)
    if not files:
        raise ValueError(f"No release archives for {tag}")
    return files


def publish(dist, tag, sha, dry_run):
    files = release_files(dist, tag)
    # A rerun may refresh its own release, but a version cannot point at another build.
    existing = subprocess.run(
        ["git", "rev-parse", "--verify", f"refs/tags/{tag}^{{commit}}"],
        text=True, capture_output=True,
    )
    if existing.returncode == 0 and existing.stdout.strip() != sha:
        raise ValueError(f"{tag} already points at another commit; bump the mod version")
    print(f"{tag}: {', '.join(file.name for file in files)}, SHA256SUMS", flush=True)
    if dry_run:
        return
    with tempfile.TemporaryDirectory(prefix="openrac-release-") as temporary:
        directory = Path(temporary)
        assets = []
        hashes = []
        for file in files:
            asset = directory / file.name
            shutil.copyfile(file, asset)
            assets.append(str(asset))
            hashes.append(f"{hashlib.sha256(asset.read_bytes()).hexdigest()}  {asset.name}\n")
        checksum = directory / "SHA256SUMS"
        checksum.write_text("".join(hashes), encoding="utf-8")
        assets.append(str(checksum))
        sdk = json.loads(Path("sdk.lock").read_text(encoding="utf-8"))["version"]
        notes = directory / "notes.md"
        notes.write_text(
            f"Built by the build workflow from {sha}, against the OpenRAC mod SDK {sdk}.\n\n"
            "Install: paste this repository's address in the OpenRAC launcher's Mods tab, "
            "or add a .zip there.\n",
            encoding="utf-8",
        )
        viewed = subprocess.run(["gh", "release", "view", tag], capture_output=True, text=True)
        if viewed.returncode == 0:
            run("gh", "release", "upload", tag, "--clobber", *assets)
        else:
            run("gh", "release", "create", tag, "--target", sha,
                "--title", tag, "--notes-file", str(notes), *assets)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ref", required=True)
    parser.add_argument("--sha", required=True)
    parser.add_argument("--dist", type=Path, default=Path("dist"))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    tags = release_tags(args.ref)
    # Validate every release's files before publishing any of them.
    for tag in tags:
        release_files(args.dist, tag)
    for tag in tags:
        publish(args.dist, tag, args.sha, args.dry_run)
    if not tags:
        print("No pending randomizer releases.")
    if not args.dry_run and os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
            output.write(f"released={'true' if tags else 'false'}\n")


if __name__ == "__main__":
    main()
