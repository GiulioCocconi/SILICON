#!/usr/bin/env python3
"""Reject a Pages deployment whose source commit is no longer main."""

from __future__ import annotations

import argparse
import subprocess


def current_main() -> str:
    result = subprocess.run(
        ["git", "ls-remote", "origin", "refs/heads/main"],
        text=True, capture_output=True, check=True,
    )
    try:
        return result.stdout.split()[0]
    except IndexError as error:
        raise RuntimeError("origin/main is unavailable") from error


def check(expected: str) -> None:
    actual = current_main()
    if expected != actual:
        raise RuntimeError(f"Stale Pages source {expected}; current main is {actual}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("expected_sha")
    args = parser.parse_args()
    check(args.expected_sha)


if __name__ == "__main__":
    main()
