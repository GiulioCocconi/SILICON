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

"""Read, validate, and update SILICON's project version."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import tempfile

from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent

CMAKE_FILE = ROOT / "CMakeLists.txt"
VCPKG_FILE = ROOT / "vcpkg.json"
NIX_FILE = ROOT / "flake.nix"


SEMVER_RE = re.compile(
    r"^(?:0|[1-9][0-9]*)"
    r"\.(?:0|[1-9][0-9]*)"
    r"\.(?:0|[1-9][0-9]*)"
    r"(?:-(?:alpha|beta|rc)\.(?:0|[1-9][0-9]*))?$"
)

CMAKE_VERSION_RE = re.compile(
    r'(?P<prefix>\bset\s*\(\s*SILICON_VERSION\s+")'
    r'(?P<version>[^"]+)'
    r'(?P<suffix>"(?:\s+CACHE\s+STRING\s+"[^"]*")?\s*\))',
    re.IGNORECASE,
)

CMAKE_PROJECT_VERSION_RE = re.compile(
    r'(?P<prefix>\bproject\s*\(\s*"?SILICON"?'
    r'.*?\bVERSION\s+)'
    r'(?P<version>(?:[0-9]+\.[0-9]+\.[0-9]+|\$\{SILICON_VERSION_CORE\}))'
    r"(?=\s|\)|#)",
    re.IGNORECASE | re.DOTALL,
)

VCPKG_VERSION_RE = re.compile(
    r'(?m)^(?P<indent>\s*)"version(?:-semver)?"'
    r'(?P<separator>\s*:\s*)"[^"]+"'
    r"(?P<suffix>\s*,?\s*)$"
)

NIX_VERSION_RE = re.compile(
    r'(?P<prefix>'
    r'mkSilicon\s*=.*?'
    r'pname\s*=\s*"SILICON";.*?'
    r'version\s*=\s*")'
    r'(?P<version>[^"]+)'
    r'(?P<suffix>";)',
    re.DOTALL,
)


class VersionError(RuntimeError):
    pass


@dataclass(frozen=True)
class Versions:
    cmake: str
    vcpkg: str
    nix: str

    @property
    def values(self) -> set[str]:
        return {self.cmake, self.vcpkg, self.nix}

    @property
    def consistent(self) -> bool:
        return len(self.values) == 1

    @property
    def version(self) -> str:
        if not self.consistent:
            raise VersionError("Project version declarations are inconsistent")
        return self.cmake


def read_text(path: Path) -> str:
    try:
        with path.open("r", encoding="utf-8", newline="") as stream:
            return stream.read()
    except OSError as error:
        raise VersionError(f"Cannot read {path}: {error}") from error


def require_single_match(
    pattern: re.Pattern[str],
    text: str,
    description: str,
) -> re.Match[str]:
    matches = list(pattern.finditer(text))

    if not matches:
        raise VersionError(f"Could not find {description}")

    if len(matches) != 1:
        raise VersionError(
            f"Expected exactly one {description}, found {len(matches)}"
        )

    return matches[0]


def validate_version(version: str, path: str) -> None:
    if not SEMVER_RE.fullmatch(version):
        raise VersionError(
            f"Invalid SILICON version: {version!r} @ {path}\n"
            "Expected X.Y.Z or X.Y.Z-{alpha,beta,rc}.N"
        )


def numeric_version(version: str) -> str:
    return version.split("-", maxsplit=1)[0]


def read_cmake_version() -> str:
    text = read_text(CMAKE_FILE)

    matches = list(CMAKE_VERSION_RE.finditer(text))

    if len(matches) > 1:
        raise VersionError(
            f"Expected exactly one SILICON_VERSION declaration in "
            f"{CMAKE_FILE}, found {len(matches)}"
        )

    if matches:
        version = matches[0].group("version")
        validate_version(version, CMAKE_FILE)
        if "VERSION ${SILICON_VERSION_CORE}" not in text:
            raise VersionError(
                f"{CMAKE_FILE} must pass the numeric SILICON_VERSION_CORE "
                "to project(VERSION ...)"
            )
        return version

    # Legacy fallback. This allows --check/--set to work for stable versions
    # before SILICON_VERSION has been introduced, but prereleases require the
    # explicit SILICON_VERSION variable.
    match = require_single_match(
        CMAKE_PROJECT_VERSION_RE,
        text,
        f"SILICON project version in {CMAKE_FILE}",
    )

    version = match.group("version")
    validate_version(version, CMAKE_FILE)
    return version


def read_vcpkg_version() -> str:
    text = read_text(VCPKG_FILE)

    try:
        manifest = json.loads(text)
    except json.JSONDecodeError as error:
        raise VersionError(f"Invalid JSON in {VCPKG_FILE}: {error}") from error

    if "version" in manifest and "version-semver" in manifest:
        raise VersionError(f"{VCPKG_FILE} contains both version fields")

    if "version-semver" in manifest:
        version = manifest["version-semver"]
    else:
        raise VersionError(
            f"{VCPKG_FILE} does not contain a package version"
        )

    if not isinstance(version, str):
        raise VersionError(f"Invalid version field in {VCPKG_FILE}")

    validate_version(version, VCPKG_FILE)
    return version


def read_nix_version() -> str:
    text = read_text(NIX_FILE)

    match = require_single_match(
        NIX_VERSION_RE,
        text,
        f"SILICON package version in {NIX_FILE}",
    )

    version = match.group("version")
    validate_version(version, NIX_FILE)
    return version


def read_versions() -> Versions:
    return Versions(
        cmake=read_cmake_version(),
        vcpkg=read_vcpkg_version(),
        nix=read_nix_version(),
    )


def replace_single(
    pattern: re.Pattern[str],
    text: str,
    replacement,
    description: str,
) -> str:
    matches = list(pattern.finditer(text))

    if not matches:
        raise VersionError(f"Could not find {description}")

    if len(matches) != 1:
        raise VersionError(
            f"Expected exactly one {description}, found {len(matches)}"
        )

    return pattern.sub(replacement, text, count=1)


def update_cmake(text: str, version: str) -> str:
    full_matches = list(CMAKE_VERSION_RE.finditer(text))

    if len(full_matches) > 1:
        raise VersionError(
            "Expected at most one SILICON_VERSION declaration in "
            f"{CMAKE_FILE}, found {len(full_matches)}"
        )

    if full_matches:
        text = CMAKE_VERSION_RE.sub(
            lambda match: (
                f'{match.group("prefix")}'
                f'{version}'
                f'{match.group("suffix")}'
            ),
            text,
            count=1,
        )
    elif "-" in version:
        raise VersionError(
            "CMakeLists.txt has no SILICON_VERSION variable.\n"
            "Prerelease versions cannot be represented by project(VERSION ...).\n"
            'Add e.g. set(SILICON_VERSION "0.1.0") first.'
        )

    core = numeric_version(version)

    text = replace_single(
        CMAKE_PROJECT_VERSION_RE,
        text,
        lambda match: (
            f'{match.group("prefix")}'
            f'{"${SILICON_VERSION_CORE}" if full_matches else core}'
        ),
        f"SILICON project version in {CMAKE_FILE}",
    )

    return text


def update_vcpkg(text: str, version: str) -> str:
    # Validate the manifest before modifying it.
    try:
        manifest = json.loads(text)
    except json.JSONDecodeError as error:
        raise VersionError(f"Invalid JSON in {VCPKG_FILE}: {error}") from error

    if "version" in manifest and "version-semver" in manifest:
        raise VersionError(
            f"{VCPKG_FILE} contains both 'version' and 'version-semver'"
        )

    return replace_single(
        VCPKG_VERSION_RE,
        text,
        lambda match: (
            f'{match.group("indent")}'
            f'"version-semver"'
            f'{match.group("separator")}'
            f'"{version}"'
            f'{match.group("suffix")}'
        ),
        f"package version in {VCPKG_FILE}",
    )


def update_nix(text: str, version: str) -> str:
    return replace_single(
        NIX_VERSION_RE,
        text,
        lambda match: (
            f'{match.group("prefix")}'
            f'{version}'
            f'{match.group("suffix")}'
        ),
        f"SILICON package version in {NIX_FILE}",
    )


def write_text(path: Path, content: str) -> None:
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, path.stat().st_mode)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def set_version(version: str) -> None:
    validate_version(version, "command argument")

    originals = {path: read_text(path) for path in (CMAKE_FILE, VCPKG_FILE, NIX_FILE)}
    updated = {
        CMAKE_FILE: update_cmake(originals[CMAKE_FILE], version),
        VCPKG_FILE: update_vcpkg(originals[VCPKG_FILE], version),
        NIX_FILE: update_nix(originals[NIX_FILE], version),
    }
    changed: list[Path] = []
    try:
        for path, content in updated.items():
            if content != originals[path]:
                write_text(path, content)
                changed.append(path)
        versions = read_versions()
        if not versions.consistent or versions.version != version:
            raise VersionError("Version update produced inconsistent metadata")
    except Exception as error:
        failures = []
        for path in reversed(changed):
            try:
                write_text(path, originals[path])
            except OSError as rollback_error:
                failures.append(f"{path}: {rollback_error}")
        if failures:
            raise VersionError(
                f"Version update failed ({error}); rollback failed: {', '.join(failures)}"
            ) from error
        raise VersionError(f"Version update failed and was rolled back: {error}") from error


def check_versions() -> Versions:
    versions = read_versions()

    if not versions.consistent:
        raise VersionError(
            "Project versions do not match:\n"
            f"  CMake: {versions.cmake}\n"
            f"  vcpkg: {versions.vcpkg}\n"
            f"  Nix:   {versions.nix}"
        )

    return versions


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Manage the SILICON project version"
    )

    action = parser.add_mutually_exclusive_group(required=True)

    action.add_argument(
        "--print",
        dest="print_version",
        action="store_true",
        help="Print the current version after verifying consistency.",
    )

    action.add_argument(
        "--check",
        action="store_true",
        help="Check that CMake, vcpkg, and Nix versions agree.",
    )

    action.add_argument(
        "--set",
        dest="set_version",
        metavar="VERSION",
        help="Set the project version in CMake, vcpkg, and Nix.",
    )

    return parser.parse_args()


def main() -> int:
    args = parse_arguments()

    try:
        if args.set_version is not None:
            set_version(args.set_version)
            print(f"Set SILICON version to {args.set_version}")
            return 0

        versions = check_versions()

        if args.print_version:
            print(versions.version)
            return 0

        if args.check:
            print(f"CMake: {versions.cmake}")
            print(f"vcpkg: {versions.vcpkg}")
            print(f"Nix:   {versions.nix}")
            print()
            print("Version metadata is consistent.")
            return 0

    except VersionError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
