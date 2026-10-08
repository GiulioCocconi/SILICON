#!/usr/bin/env python3
#  Copyright (c) 2026. Giulio Cocconi
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <http://www.gnu.org/licenses/>.

"""Resolve SILICON release and snapshot versions."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys

from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


ROOT = Path(__file__).resolve().parent.parent
SEMVER_SCRIPT = ROOT / "ci" / "semver.py"

VERSION_RE = re.compile(
    r"^v?"
    r"(?P<major>0|[1-9][0-9]*)"
    r"\.(?P<minor>0|[1-9][0-9]*)"
    r"\.(?P<patch>0|[1-9][0-9]*)"
    r"(?:-(?P<kind>alpha|beta|rc)\.(?P<number>0|[1-9][0-9]*))?"
    r"$"
)

PRERELEASE_ORDER = {
    "alpha": 0,
    "beta": 1,
    "rc": 2,
}


class ReleaseError(RuntimeError):
    pass


@dataclass(frozen=True, order=True)
class CoreVersion:
    major: int
    minor: int
    patch: int

    def __str__(self) -> str:
        return f"{self.major}.{self.minor}.{self.patch}"


# The old v0.0.1-beta.1 tag belongs to an abandoned prerelease line.
# The first release on the current version scheme starts at 0.1.0.
RELEASE_LINE_FLOOR = CoreVersion(0, 1, 0)


@dataclass(frozen=True)
class Version:
    core: CoreVersion
    prerelease: str | None = None
    prerelease_number: int | None = None

    @property
    def stable(self) -> bool:
        return self.prerelease is None

    def __str__(self) -> str:
        if self.prerelease is None:
            return str(self.core)

        return (
            f"{self.core}-"
            f"{self.prerelease}.{self.prerelease_number}"
        )


@dataclass(frozen=True)
class TaggedVersion:
    tag: str
    version: Version


@dataclass(frozen=True)
class ReleaseState:
    declared_version: Version
    latest_stable: TaggedVersion | None
    active_prerelease_core: CoreVersion | None
    latest_prerelease: TaggedVersion | None
    target_stable: CoreVersion
    next_beta: Version | None
    next_rc: Version
    unstable: str
    head: str


def run(
    command: Sequence[str],
    *,
    cwd: Path = ROOT,
) -> str:
    try:
        result = subprocess.run(
            command,
            cwd=cwd,
            capture_output=True,
            text=True,
        )
    except FileNotFoundError as error:
        raise ReleaseError(
            f"Command not found: {command[0]}"
        ) from error

    if result.returncode != 0:
        command_string = " ".join(command)
        message = result.stderr.strip() or result.stdout.strip()

        raise ReleaseError(
            f"Command failed: {command_string}\n{message}"
        )

    return result.stdout.strip()


def parse_version(value: str) -> Version:
    match = VERSION_RE.fullmatch(value.strip())

    if match is None:
        raise ReleaseError(f"Invalid SILICON version: {value!r}")

    core = CoreVersion(
        major=int(match.group("major")),
        minor=int(match.group("minor")),
        patch=int(match.group("patch")),
    )

    prerelease = match.group("kind")
    prerelease_number = match.group("number")

    return Version(
        core=core,
        prerelease=prerelease,
        prerelease_number=(
            int(prerelease_number)
            if prerelease_number is not None
            else None
        ),
    )


def read_declared_version() -> Version:
    if not SEMVER_SCRIPT.exists():
        raise ReleaseError(
            f"Version helper not found: {SEMVER_SCRIPT}"
        )

    version = run(
        [
            sys.executable,
            str(SEMVER_SCRIPT),
            "--print",
        ]
    )

    return parse_version(version)


def ensure_git_repository() -> None:
    run(["git", "rev-parse", "--is-inside-work-tree"])


def ensure_complete_history() -> None:
    shallow = run(
        ["git", "rev-parse", "--is-shallow-repository"]
    )

    if shallow == "true":
        raise ReleaseError(
            "Release version calculation requires complete Git history. "
            "Use fetch-depth: 0 in GitHub Actions and fetch all tags."
        )


def git_tags() -> list[TaggedVersion]:
    output = run(
        [
            "git",
            "tag",
            "--merged",
            "HEAD",
            "--list",
        ]
    )

    tags: list[TaggedVersion] = []

    for tag in output.splitlines():
        tag = tag.strip()

        if not tag:
            continue

        try:
            version = parse_version(tag)
        except ReleaseError:
            # Ignore tags unrelated to SILICON releases.
            continue

        tags.append(
            TaggedVersion(
                tag=tag,
                version=version,
            )
        )

    return tags


def latest_stable_tag(
    tags: Sequence[TaggedVersion],
) -> TaggedVersion | None:
    stable = [
        tagged
        for tagged in tags
        if tagged.version.stable
    ]

    if not stable:
        return None

    return max(
        stable,
        key=lambda tagged: tagged.version.core,
    )


def stable_cores(
    tags: Sequence[TaggedVersion],
) -> set[CoreVersion]:
    return {
        tagged.version.core
        for tagged in tags
        if tagged.version.stable
    }


def active_prerelease_core(
    tags: Sequence[TaggedVersion],
    latest_stable: TaggedVersion | None,
) -> CoreVersion | None:
    released = stable_cores(tags)

    candidates = {
        tagged.version.core
        for tagged in tags
        if not tagged.version.stable
        and tagged.version.core not in released
        and tagged.version.core >= RELEASE_LINE_FLOOR
    }

    if latest_stable is not None:
        candidates = {
            core
            for core in candidates
            if core > latest_stable.version.core
        }

    if not candidates:
        return None

    # Among current prerelease series, the highest unreleased version
    # is the active release line.
    return max(candidates)


def prereleases_for(
    tags: Sequence[TaggedVersion],
    core: CoreVersion,
) -> list[TaggedVersion]:
    return [
        tagged
        for tagged in tags
        if tagged.version.core == core
        and not tagged.version.stable
    ]


def latest_prerelease_for(
    tags: Sequence[TaggedVersion],
    core: CoreVersion,
) -> TaggedVersion | None:
    prereleases = prereleases_for(tags, core)

    if not prereleases:
        return None

    return max(
        prereleases,
        key=lambda tagged: (
            PRERELEASE_ORDER[tagged.version.prerelease],
            tagged.version.prerelease_number,
        ),
    )


def git_cliff_bumped_version() -> CoreVersion:
    output = run([
        "git", "cliff", "--config", "ci/cliff.toml",
        "--offline", "--bumped-version", "--output", "-",
    ])

    version = parse_version(output)

    if not version.stable:
        raise ReleaseError(
            "git-cliff returned a prerelease version when a stable "
            f"target was expected: {version}"
        )

    return version.core


def target_stable_version(
    tags: Sequence[TaggedVersion],
) -> CoreVersion:
    stable = latest_stable_tag(tags)

    active = active_prerelease_core(
        tags,
        stable,
    )

    # Once a prerelease exists, its base version is locked.
    #
    # v0.2.0-beta.1
    #        |
    #        +-- further feat commits do not move the target to 0.3.0
    #
    # The release line remains 0.2.0 until it is released or abandoned.
    if active is not None:
        return active

    return max(git_cliff_bumped_version(), RELEASE_LINE_FLOOR)


def next_prerelease(
    kind: str,
    tags: Sequence[TaggedVersion],
    target: CoreVersion,
) -> Version:
    if kind not in PRERELEASE_ORDER:
        raise ReleaseError(
            f"Unsupported prerelease kind: {kind}"
        )

    prereleases = prereleases_for(tags, target)

    later_stages = [
        tagged
        for tagged in prereleases
        if PRERELEASE_ORDER[tagged.version.prerelease]
        > PRERELEASE_ORDER[kind]
    ]

    if later_stages:
        latest = max(
            later_stages,
            key=lambda tagged: (
                PRERELEASE_ORDER[tagged.version.prerelease],
                tagged.version.prerelease_number,
            ),
        )

        raise ReleaseError(
            f"Cannot create {kind} release for {target}: "
            f"release series has already reached {latest.version}"
        )

    same_kind = [
        tagged.version.prerelease_number
        for tagged in prereleases
        if tagged.version.prerelease == kind
    ]

    if same_kind:
        number = max(same_kind) + 1
    else:
        number = 1

    return Version(
        core=target,
        prerelease=kind,
        prerelease_number=number,
    )


def short_head_sha() -> str:
    return run(
        [
            "git",
            "rev-parse",
            "--short=8",
            "HEAD",
        ]
    )


def unstable_version(
    target: CoreVersion,
) -> str:
    return f"{target}-dev.{short_head_sha()}"


def describe_state() -> ReleaseState:
    tags = git_tags()

    latest_stable = latest_stable_tag(tags)
    active_core = active_prerelease_core(
        tags,
        latest_stable,
    )

    target = target_stable_version(tags)

    try:
        beta = next_prerelease("beta", tags, target)
    except ReleaseError:
        beta = None

    return ReleaseState(
        declared_version=read_declared_version(),
        latest_stable=latest_stable,
        active_prerelease_core=active_core,
        latest_prerelease=latest_prerelease_for(
            tags,
            target,
        ),
        target_stable=target,
        next_beta=beta,
        next_rc=next_prerelease(
            "rc",
            tags,
            target,
        ),
        unstable=unstable_version(target),
        head=short_head_sha(),
    )


def print_state(state: ReleaseState) -> None:
    print(f"Declared version:       {state.declared_version}")

    if state.latest_stable is not None:
        print(
            "Latest stable:          "
            f"{state.latest_stable.tag}"
        )
    else:
        print("Latest stable:          none")

    if state.active_prerelease_core is not None:
        print(
            "Active release line:    "
            f"{state.active_prerelease_core}"
        )
    else:
        print("Active release line:    none")

    if state.latest_prerelease is not None:
        print(
            "Latest prerelease:      "
            f"{state.latest_prerelease.tag}"
        )
    else:
        print("Latest prerelease:      none")

    print(f"Target stable:          {state.target_stable}")
    print(f"Next beta:              {state.next_beta or 'unavailable'}")
    print(f"Next RC:                {state.next_rc}")
    print(f"Unstable:               {state.unstable}")
    print(f"HEAD:                   {state.head}")


def state_as_dict(state: ReleaseState) -> dict[str, str | None]:
    return {
        "declared_version": str(state.declared_version),
        "latest_stable_tag": (
            state.latest_stable.tag
            if state.latest_stable is not None
            else None
        ),
        "active_release_line": (
            str(state.active_prerelease_core)
            if state.active_prerelease_core is not None
            else None
        ),
        "latest_prerelease_tag": (
            state.latest_prerelease.tag
            if state.latest_prerelease is not None
            else None
        ),
        "target_stable": str(state.target_stable),
        "next_beta": str(state.next_beta) if state.next_beta else None,
        "next_rc": str(state.next_rc),
        "unstable": state.unstable,
        "head": state.head,
    }


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Resolve SILICON release versions"
    )

    subparsers = parser.add_subparsers(
        dest="command",
        required=True,
    )

    next_parser = subparsers.add_parser(
        "next",
        help="Print the next release version",
    )

    next_parser.add_argument(
        "channel",
        choices=[
            "stable",
            "beta",
            "rc",
        ],
        help="Release channel to calculate",
    )

    subparsers.add_parser(
        "unstable-version",
        help="Print the version for the current main snapshot",
    )

    describe_parser = subparsers.add_parser(
        "describe",
        help="Show the current release state",
    )

    describe_parser.add_argument(
        "--json",
        action="store_true",
        help="Emit machine-readable JSON",
    )

    return parser.parse_args()


def main() -> int:
    args = parse_arguments()

    try:
        ensure_git_repository()
        ensure_complete_history()

        tags = git_tags()
        target = target_stable_version(tags)

        if args.command == "next":
            if args.channel == "stable":
                print(target)
                return 0

            print(
                next_prerelease(
                    args.channel,
                    tags,
                    target,
                )
            )
            return 0

        if args.command == "unstable-version":
            print(unstable_version(target))
            return 0

        if args.command == "describe":
            state = describe_state()

            if args.json:
                print(
                    json.dumps(
                        state_as_dict(state),
                        indent=2,
                    )
                )
            else:
                print_state(state)

            return 0

    except ReleaseError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
