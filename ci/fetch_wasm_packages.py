#!/usr/bin/env python3
"""Resolve published Cloudsmith WASM ZIPs for the Pages deployment."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import tempfile
import urllib.parse
import urllib.request
import zipfile

from pathlib import Path


def packages(owner: str, repo: str, name: str):
    query = urllib.parse.quote(f"format:raw AND name:^{name}$")
    page_size = 100
    for page in range(1, 51):
        url = (
            f"https://api.cloudsmith.io/packages/{owner}/{repo}/"
            f"?query={query}&page={page}&page_size={page_size}"
        )
        with urllib.request.urlopen(url, timeout=30) as response:
            payload = json.load(response)
            page_total = response.headers.get("X-Pagination-PageTotal")
        entries = payload if isinstance(payload, list) else payload.get("data", [])
        if not entries:
            return
        for item in entries:
            if item.get("name") == name and item.get("cdn_url"):
                yield item
        if page_total is not None:
            if page >= int(page_total):
                return
        elif isinstance(payload, dict) and "next" in payload:
            if not payload["next"]:
                return
        elif len(entries) < page_size:
            return
        else:
            raise RuntimeError(f"Missing Cloudsmith pagination headers while resolving {name}")
    raise RuntimeError(f"Too many Cloudsmith pages while resolving {name}")


def install(owner: str, repo: str, channel: str, version: str, root: Path) -> bool:
    name = f"silicon-{channel}-wasm"
    entries = list(packages(owner, repo, name))
    if version:
        matches = [item for item in entries if item.get("version") == version]
    else:
        matches = entries
    if not matches:
        if not version:
            return False
        raise RuntimeError(
            f"No published Cloudsmith package: {name} {version or '(latest)'}"
        )
    if not version:
        newest = max(item.get("uploaded_at_iso") or item.get("uploaded_at") or "" for item in matches)
        latest = [item for item in matches if (item.get("uploaded_at_iso") or item.get("uploaded_at") or "") == newest]
        versions = {item.get("version") for item in latest}
        if len(versions) != 1:
            raise RuntimeError(f"Ambiguous latest Cloudsmith package: {name}")
        matches = [item for item in matches if item.get("version") in versions]
    identities = {(item.get("version"), item.get("filename"), item.get("checksum_sha256")) for item in matches}
    if len(identities) != 1:
        raise RuntimeError(f"Ambiguous Cloudsmith package: {name} {version or '(latest)'}")
    selected = matches[0]
    if not selected.get("cdn_url"):
        raise RuntimeError(f"No download URL for {name} {selected.get('version')}")
    expected = selected.get("checksum_sha256")
    if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", expected):
        raise RuntimeError(f"Missing SHA-256 for {name} {selected.get('version')}")
    with tempfile.TemporaryDirectory(dir=root) as temporary:
        archive = Path(temporary) / "package.zip"
        directory = Path(temporary) / channel
        directory.mkdir()
        urllib.request.urlretrieve(selected["cdn_url"], archive)
        with archive.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        if digest.lower() != expected.lower():
            raise RuntimeError(f"SHA-256 mismatch for {name} {selected.get('version')}")
        with zipfile.ZipFile(archive) as package:
            seen: set[str] = set()
            for info in package.infolist():
                destination = (directory / info.filename).resolve()
                mode = info.external_attr >> 16
                if (not destination.is_relative_to(directory.resolve())
                        or info.filename in seen
                        or stat.S_ISLNK(mode)):
                    raise RuntimeError(f"Unsafe ZIP member: {info.filename}")
                seen.add(info.filename)
            package.extractall(directory)
        if not (directory / "index.html").is_file():
            raise RuntimeError(f"{name} has no index.html")
        destination = root / channel
        if destination.exists():
            shutil.rmtree(destination)
        shutil.move(str(directory), destination)
    return True


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stable-version", default="")
    parser.add_argument("--unstable-version", default="")
    parser.add_argument("--output", default="dist/wasm")
    args = parser.parse_args()
    owner = os.environ["CLOUDSMITH_OWNER"]
    repo = os.environ["CLOUDSMITH_RELEASE_REPO"]
    root = Path(args.output)
    root.mkdir(parents=True, exist_ok=True)
    stable = install(owner, repo, "stable", args.stable_version, root)
    unstable = install(owner, repo, "unstable", args.unstable_version, root)
    (root / "channels.json").write_text(
        json.dumps({"stable": stable, "unstable": unstable}) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
