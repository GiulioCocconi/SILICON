#!/usr/bin/env python3
"""GitHub Actions release preparation and validation.

Version policy remains in release.py and semver.py; this module coordinates
those tools, git-cliff, and the reviewed release branch.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys

from pathlib import Path

import release


ROOT = Path(__file__).resolve().parent.parent


def run(*args: str, check: bool = True) -> str:
    result = subprocess.run(
        args, cwd=ROOT, text=True, capture_output=True,
    )
    if check and result.returncode:
        raise release.ReleaseError(
            f"{' '.join(args)} failed: {result.stderr.strip() or result.stdout.strip()}"
        )
    return result.stdout.strip()


def output(**values: str) -> None:
    path = os.environ.get("GITHUB_OUTPUT")
    if path:
        with open(path, "a", encoding="utf-8") as stream:
            for name, value in values.items():
                stream.write(f"{name}={value}\n")
    for name, value in values.items():
        print(f"{name}={value}")


def notes(version: str, channel: str, boundary: str | None, path: Path) -> None:
    command = [
        "git", "cliff", "--offline", "--config", "ci/cliff.toml",
        "--tag", f"v{version}", "--output", str(path),
    ]
    if channel != "stable":
        # The persistent changelog ignores prerelease tags. Release notes
        # deliberately use the previous beta/RC boundary when present.
        command += ["--ignore-tags", "^$"]
    if boundary:
        command.append(f"{boundary}..HEAD")
    run(*command)


def prepare(channel: str) -> None:
    release.ensure_complete_history()
    state = release.describe_state()
    version = run(sys.executable, "ci/release.py", "next", channel)
    if state.declared_version.core > release.parse_version(version).core:
        raise release.ReleaseError(
            f"Declared version {state.declared_version} is ahead of the "
            f"calculated release line {version}; reconcile metadata and tags first"
        )
    branch = f"release/v{version}"
    if run("git", "ls-remote", "--heads", "origin", branch):
        raise release.ReleaseError(f"Release branch already exists: {branch}")
    run(sys.executable, "ci/semver.py", "--set", version)
    run(sys.executable, "ci/semver.py", "--check")
    stable_tag = state.latest_stable.tag if state.latest_stable is not None else None
    if channel != "stable" and state.latest_prerelease is not None:
        boundary = state.latest_prerelease.tag
    else:
        boundary = stable_tag
    notes(version, channel, boundary, ROOT / "release-notes.md")
    if channel == "stable":
        # Keep stable history only. The range begins at the previous stable
        # tag, so beta tags never become changelog boundaries.
        changelog = ROOT / "CHANGELOG.md"
        changelog_command = [
            "git", "cliff", "--offline", "--config", "ci/cliff.toml",
            "--tag", f"v{version}",
        ]
        if changelog.exists():
            changelog_command += ["--prepend", "CHANGELOG.md"]
        else:
            changelog_command += ["--output", "CHANGELOG.md"]
        if boundary:
            changelog_command.append(f"{boundary}..HEAD")
        run(*changelog_command)
    previous_stable = state.latest_stable.tag if state.latest_stable else "none"
    body = (
        f"## Release preparation\n\n"
        f"- Channel: `{channel}`\n"
        f"- Calculated version: `{version}`\n"
        f"- Previous stable release: `{previous_stable}`\n"
        f"- Active prerelease line: `{state.active_prerelease_core or 'none'}`\n"
        f"- CHANGELOG.md updated: {'yes' if channel == 'stable' else 'no'}\n\n"
        "## Proposed release notes\n\n"
        + (ROOT / "release-notes.md").read_text(encoding="utf-8")
    )
    (ROOT / "release-pr-body.md").write_text(body, encoding="utf-8")
    (ROOT / "release-notes.md").unlink()
    output(version=version, branch=branch)


def reviewed_pr(repository: str, head: str, version: str, channel: str) -> int:
    """Find the release preparation PR merged into the expected commit."""
    owner = repository.split("/", 1)[0]
    branch = f"release/v{version}"

    prs = json.loads(run(
        "gh", "api", "-X", "GET", f"repos/{repository}/pulls",
        "-f", "state=closed",
        "-f", f"head={owner}:{branch}",
        "-f", "per_page=100",
    ))

    for pr in prs:
        body = pr.get("body") or ""
        if (
            pr.get("merged_at")
            and pr.get("merge_commit_sha") == head
            and pr["base"]["ref"] == "main"
            and pr["head"]["repo"]["full_name"] == repository
            and f"- Channel: `{channel}`" in body
            and f"- Calculated version: `{version}`" in body
        ):
            return pr["number"]

    raise release.ReleaseError(
        f"Commit {head} must be the merge of one reviewed "
        f"{branch} preparation PR for {channel}"
    )


def validate(channel: str) -> None:
    if os.environ.get("GITHUB_REF") != "refs/heads/main":
        raise release.ReleaseError("Publish must be dispatched from main")
    release.ensure_complete_history()
    head = run("git", "rev-parse", "HEAD")
    main = run("git", "rev-parse", "origin/main")
    if head != main:
        raise release.ReleaseError("Checkout differs from origin/main")
    run(sys.executable, "ci/semver.py", "--check")
    version = run(sys.executable, "ci/semver.py", "--print")
    parsed = release.parse_version(version)
    if channel == "stable" and not parsed.stable:
        raise release.ReleaseError(f"{version} is not a stable version")
    if channel != "stable" and parsed.prerelease != channel:
        raise release.ReleaseError(f"{version} does not match {channel} channel")
    pr_number = reviewed_pr(os.environ["GITHUB_REPOSITORY"], head, version, channel)
    tag = f"v{version}"
    tag_exists = bool(run("git", "tag", "--list", tag))
    if tag_exists:
        if run("git", "cat-file", "-t", f"refs/tags/{tag}") != "tag":
            raise release.ReleaseError(f"Tag {tag} is not annotated")
        tagged_sha = run("git", "rev-list", "-n", "1", tag)
        if tagged_sha != head:
            raise release.ReleaseError(
                f"Tag {tag} points to {tagged_sha}, not reviewed commit {head}"
            )

    # The tag lookup endpoint only returns published releases. Listing releases
    # with write access also finds drafts left by an interrupted publication.
    pages = json.loads(run(
        "gh", "api", "--paginate", "--slurp",
        f"repos/{os.environ['GITHUB_REPOSITORY']}/releases?per_page=100",
    ))
    matching = [
        item for page in pages for item in page
        if item["tag_name"] == tag
    ]
    if len(matching) > 1:
        raise release.ReleaseError(f"Multiple GitHub Releases use {tag}")
    draft_exists = bool(matching)
    if draft_exists and not tag_exists:
        raise release.ReleaseError(f"GitHub Release {tag} has no matching tag")
    if draft_exists and not matching[0]["draft"]:
        raise release.ReleaseError(f"GitHub Release {tag} is already published")

    if not tag_exists:
        expected = run(sys.executable, "ci/release.py", "next", channel)
        if version != expected:
            raise release.ReleaseError(
                f"Declared version {version} does not match the calculated "
                f"{channel} release {expected}"
            )

    # Exclude our own tag when regenerating notes for a retry.
    previous_tags = [
        tagged for tagged in release.git_tags() if tagged.tag != tag
    ]
    latest_stable = release.latest_stable_tag(previous_tags)
    latest_prerelease = release.latest_prerelease_for(
        previous_tags, parsed.core,
    )
    boundary = latest_stable.tag if latest_stable else None
    if channel != "stable" and latest_prerelease is not None:
        boundary = latest_prerelease.tag
    notes(version, channel, boundary, ROOT / "release-notes.md")
    output(
        version=version, sha=head, tag=tag, pr=str(pr_number),
        tag_exists=str(tag_exists).lower(),
        draft_exists=str(draft_exists).lower(),
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("operation", choices=["prepare", "validate"])
    parser.add_argument("channel", choices=["stable", "beta", "rc"])
    args = parser.parse_args()
    try:
        if args.operation == "prepare":
            prepare(args.channel)
        else:
            validate(args.channel)
    except release.ReleaseError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
