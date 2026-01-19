#  Copyright (c) 2026. Giulio Cocconi
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

import os
import sys
import argparse
import subprocess
from fnmatch import fnmatch
from typing import List, Optional

# Requires: pip install PyGithub
import github

# Global Configuration State
pr_rev: str = ""
base_rev: str = ""
repo_name: str = ""
changed_files_list: List[str] = []
token: str = ""
issue_number: int = 0
write_comments_on_pr: bool = False

# GitHub Objects
gh_instance: github.Github = None
pr: Optional[github.PullRequest.PullRequest] = None

class FormatAssistant:
    name: str
    matching_files_wildcard: List[str]
    diff_buffer: List[str]

    def __init__(self):
        self.diff_buffer = []

    @property
    def comment_tag(self) -> str:
        return f"<!--SILICON CODE FORMAT COMMENT: {self.name}-->"

    def run(self) -> None:
        for fn in changed_files_list:
            # strip whitespace to avoid matching errors
            fn = fn.strip()
            for wildcard in self.matching_files_wildcard:
                if fnmatch(fn, wildcard):
                    print(f"[{self.name}] Processing {fn}...")
                    self.diff_buffer.append(self.format(fn))
                    break

        self.finalize()

    def format(self, file_name: str):
        raise NotImplementedError()

    def finalize(self):
        """Updates, creates, or deletes comments on the PR based on findings."""

        self.diff_buffer = [diff for diff in self.diff_buffer if diff is not None]

        if not write_comments_on_pr or pr is None:
            if self.diff_buffer:
                print(f"[{self.name}] Issues found, but comment writing is disabled.")
                print("\n".join(self.diff_buffer))
            return

        comment = self.find_comment()

        if not self.diff_buffer:
            # No issues found. If a comment exists from a previous run, delete it.
            if comment:
                print(f"[{self.name}] formatting is correct. Deleting old comment.")
                comment.delete()
            return

        # Issues found. Construct the body.
        body = f"{self.comment_tag}\n\n### {self.name} Suggestions\n\n"
        body += "The following formatting changes are suggested:\n\n"
        body += "```diff\n"
        body += "\n".join(self.diff_buffer)
        body += "\n\n```"

        if comment:
            print(f"[{self.name}] Updating existing comment.")
            comment.edit(body)
        else:
            print(f"[{self.name}] Creating new comment.")
            pr.as_issue().create_comment(body)

    def find_comment(self) -> Optional[github.IssueComment.IssueComment]:
        if pr is None:
            return None

        # Filter comments to find the one owned by this bot/action with our tag
        comments = pr.as_issue().get_comments()
        for comment in comments:
            if self.comment_tag in comment.body:
                return comment
        return None


class ClangFormatAssistant(FormatAssistant):
    name = "clang-format"
    matching_files_wildcard = [
        "*.cpp",
        "*.hpp",
    ]

    # Assumes git-clang-format is in the same directory as this script
    GIT_CLANG_FORMAT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "git-clang-format")

    def format(self, file_name: str):
        # We invoke the python script directly to ensure execution works regardless of chmod
        cmd = [
            self.GIT_CLANG_FORMAT_PATH,
            "--diff",
            base_rev,
            pr_rev,
            "--",
            file_name
        ]

        try:
            # Capture stdout (the diff) and stderr
            result = subprocess.run(cmd, capture_output=True, text=True)
            output = result.stdout

            # If there is output, it means there is a diff
            if output and "no modified files to format" not in output:
                return f"{output}"

        except Exception as e:
            print(f"Failed to execute subprocess for {file_name}: {e}", file=sys.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run formatters on changed files in a PR.")
    parser.add_argument(
        "--token", type=str, required=True, help="GitHub authentication token"
    )
    parser.add_argument(
        "--repo",
        type=str,
        required=True,
        default="GiulioCocconi/SILICON",
    )
    parser.add_argument("--issue-number", type=int, required=True)
    parser.add_argument(
        "--pr-rev",
        type=str,
        required=True,
        help="Compute changes from this revision (e.g. HEAD).",
    )
    parser.add_argument(
        "--base-rev",
        type=str,
        required=True,
        help="Compute changes to this revision (e.g. main)"
    )
    parser.add_argument(
        "--changed-files",
        type=str,
        help="Comma separated list of files that has been changed",
        default=""
    )

    parser.add_argument('--write-comments-on-pr',
                        action=argparse.BooleanOptionalAction,
                        help="Whether you want to write comments on PR")

    args = parser.parse_args()

    token = args.token
    repo_name = args.repo
    issue_number = args.issue_number
    pr_rev = args.pr_rev
    base_rev = args.base_rev
    write_comments_on_pr = args.write_comments_on_pr

    # Process changed files list
    if args.changed_files:
        changed_files_list = [f.strip() for f in args.changed_files.split(",") if f.strip()]

    # Initialize GitHub connection
    if write_comments_on_pr:
        try:
            gh_instance = github.Github(token)
            repo_obj = gh_instance.get_repo(repo_name)
            pr = repo_obj.get_pull(issue_number)
        except Exception as e:
            print(f"Failed to initialize GitHub connection: {e}")
            sys.exit(1)

    enabled_assistants: List[FormatAssistant] = [
        ClangFormatAssistant()
    ]

    for assistant in enabled_assistants:
        assistant.run()
