#!/usr/bin/env python3
"""Publish immutable SILICON artifacts to Cloudsmith and GitHub Releases."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path


def run(*args: str) -> str:
    result = subprocess.run(args, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"{' '.join(args)} failed: {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout


def checksum(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def cloudsmith_upload(owner: str, repo: str, name: str, version: str, path: Path) -> None:
    payload = json.loads(run(
        "cloudsmith", "list", "packages", f"{owner}/{repo}", "-F", "json",
        "-q", f"name:^{name}$ AND version:^{version}$",
    ))
    entries = payload.get("data", payload) if isinstance(payload, dict) else payload
    matches = [item for item in entries if item.get("name") == name and item.get("version") == version]
    if matches:
        if all(item.get("filename") == path.name and item.get("checksum_sha256") == checksum(path)
               for item in matches):
            print(f"Cloudsmith already has {path.name} with the same SHA-256")
            return
        raise RuntimeError(f"Cloudsmith has conflicting package {name} {version}")
    run("cloudsmith", "push", "raw", f"{owner}/{repo}", str(path),
        "--name", name, "--version", version)


def github_assets(repository: str, tag: str) -> list[dict]:
    pages = json.loads(run(
        "gh", "api", "--paginate", "--slurp",
        f"repos/{repository}/releases?per_page=100",
    ))
    releases = [item for page in pages for item in page if item.get("tag_name") == tag]
    if len(releases) != 1:
        raise RuntimeError(f"Expected one GitHub Release for {tag}, found {len(releases)}")
    return releases[0].get("assets", [])


def github_upload(repository: str, tag: str, paths: list[Path]) -> None:
    assets = github_assets(repository, tag)
    for path in paths:
        matches = [asset for asset in assets if asset["name"] == path.name]
        if len(matches) > 1:
            raise RuntimeError(f"Duplicate GitHub Release asset: {path.name}")
        if matches:
            if matches[0].get("digest") != f"sha256:{checksum(path)}":
                raise RuntimeError(f"GitHub Release asset differs: {path.name}")
            print(f"GitHub Release already has {path.name} with the same SHA-256")
        else:
            run("gh", "release", "upload", tag, str(path), "--repo", repository)


def main() -> None:
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    cloudsmith = commands.add_parser("cloudsmith")
    cloudsmith.add_argument("owner")
    cloudsmith.add_argument("repo")
    cloudsmith.add_argument("name")
    cloudsmith.add_argument("version")
    cloudsmith.add_argument("path", type=Path)
    github = commands.add_parser("github-assets")
    github.add_argument("repository")
    github.add_argument("tag")
    github.add_argument("paths", type=Path, nargs="+")
    args = parser.parse_args()
    if args.command == "cloudsmith":
        cloudsmith_upload(args.owner, args.repo, args.name, args.version, args.path)
    else:
        github_upload(args.repository, args.tag, args.paths)


if __name__ == "__main__":
    main()
