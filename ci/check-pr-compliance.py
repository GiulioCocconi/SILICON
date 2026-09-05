#!/usr/bin/env python3
#  Copyright (c) 2026. Giulio Cocconi
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.

"""Run the checks from pr-compliance.yml against the current branch."""

import argparse
import os
import re
import subprocess
import sys


ALLOWED_TYPES = (
    "feat|fix|docs|style|refactor|perf|test|build|ci|chore|revert|"
    "contributors|misc"
)
SUBJECT_PATTERN = re.compile(rf"^({ALLOWED_TYPES})(\(.+\))?: .+")


def git(*args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args],
        check=check,
        capture_output=True,
        text=True,
    )


def find_base_ref() -> str:
    configured = os.environ.get("PR_COMPLIANCE_BASE_REF")
    candidates = [configured] if configured else []
    candidates.extend(["origin/main", "main", "origin/master", "master"])

    for candidate in candidates:
        exists = candidate and git(
            "rev-parse", "--verify", "--quiet", candidate, check=False
        ).returncode == 0
        if exists:
            return candidate

    raise RuntimeError(
        "cannot find a base branch; set PR_COMPLIANCE_BASE_REF to its Git ref"
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check the current branch for PR compliance"
    )
    parser.add_argument(
        "--github-output",
        help="Write GitHub Actions outputs and comment artifacts",
    )
    args = parser.parse_args()

    try:
        base_ref = find_base_ref()
        merge_base = git("merge-base", "HEAD", base_ref).stdout.strip()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f"PR compliance: {error}", file=sys.stderr)
        return 1

    errors: list[str] = []
    invalid_commits: list[str] = []
    unsigned_commits: list[str] = []
    authors_missing = False
    authors_error_type = ""

    whitespace = git("diff", "--check", f"{merge_base}...HEAD", check=False)
    if whitespace.returncode:
        errors.append(
            f"Whitespace issues:\n{whitespace.stdout}{whitespace.stderr}".rstrip()
        )

    commits = git("rev-list", "--no-merges", f"{merge_base}..HEAD").stdout.splitlines()
    for commit in commits:
        subject = git("show", "-s", "--format=%s", commit).stdout.rstrip("\n")
        message = git("show", "-s", "--format=%B", commit).stdout
        author = git("show", "-s", "--format=%an", commit).stdout.rstrip("\n")
        short = commit[:7]

        if not SUBJECT_PATTERN.fullmatch(subject):
            errors.append(f'{short}: invalid commit subject: "{subject}"')
            invalid_commits.append(f'* {short} - "{subject}" by {author}')
        if not re.search(r"^Signed-off-by:", message, re.MULTILINE):
            errors.append(f'{short}: missing Signed-off-by: "{subject}"')
            unsigned_commits.append(f'* {short} - "{subject}" by {author}')

    github_user = os.environ.get("PR_COMPLIANCE_GITHUB_USER")
    if args.github_output and github_user and not github_user.endswith("[bot]"):
        github_user = github_user.removeprefix("@")
        base_authors = git("show", f"{merge_base}:AUTHORS.md", check=False)
        if f"@{github_user}" not in base_authors.stdout:
            changed_files = git(
                "diff", "--name-only", f"{merge_base}...HEAD"
            ).stdout.splitlines()
            required_subject = f"contributors: add @{github_user} to AUTHORS.md"
            subjects = git(
                "log", "--format=%s", f"{merge_base}..HEAD"
            ).stdout.splitlines()

            if "AUTHORS.md" not in changed_files:
                errors.append(f"AUTHORS.md does not add @{github_user}")
                authors_missing = True
                authors_error_type = "no_file_change"
            elif required_subject not in subjects:
                errors.append(f'missing commit subject: "{required_subject}"')
                authors_missing = True
                authors_error_type = "bad_message"
    if args.github_output:
        whitespace_errors = f"{whitespace.stdout}{whitespace.stderr}"
        with open(args.github_output, "a", encoding="utf-8") as output:
            output.write(
                f"has_whitespace_issues={'true' if whitespace.returncode else 'false'}\n"
                f"has_unsigned={'true' if unsigned_commits else 'false'}\n"
                f"has_invalid={'true' if invalid_commits else 'false'}\n"
                f"is_missing={'true' if authors_missing else 'false'}\n"
                f"error_type={authors_error_type}\n"
            )
        if whitespace_errors:
            with open("whitespace_errors.txt", "w", encoding="utf-8") as output:
                output.write(whitespace_errors)
        if unsigned_commits:
            with open("unsigned_commits.txt", "w", encoding="utf-8") as output:
                output.write("\n".join(unsigned_commits) + "\n")
        if invalid_commits:
            with open("invalid_commits.txt", "w", encoding="utf-8") as output:
                output.write("\n".join(invalid_commits) + "\n")

    if errors:
        print("PR compliance failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 0 if args.github_output else 1

    print(f"PR compliance passed for {merge_base}..HEAD ({base_ref})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
