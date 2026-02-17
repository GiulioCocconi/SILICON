#  Copyright (C) 2026 Giulio Cocconi

#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.

#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.

#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <http://www.gnu.org/licenses/>.

# This script should be called manually to generate a draft of the changelog

import sys
import argparse
import subprocess

from typing import List, Tuple
import re

def get_commits(start_rev : str, end_rev : str) -> List[str]:
    result = subprocess.run(
        ["git", "log", "--format=%s", f"{start_rev}..{end_rev}"],
        capture_output=True,
        text=True
    )
    if result.returncode != 0:
        print(f"Error getting commits: {result.stderr}", file=sys.stderr)
        return []
    commits = result.stdout.strip().split("\n")
    return [commit for commit in commits if commit]

def parse_commits(commits : List[str]) -> Tuple[List[str], List[str], List[str]]:
    features : List[str] = []
    fixes : List[str] = []
    contributors : List[str] = []

    for commit in commits:
        r = re.search(r"^(.*)\((.*)\): (.*)", commit)
        if r:
            commit_type : str = r.group(1)
            commit_scope : str = r.group(2)
            commit_desc : str = r.group(3)
        else:
            r = re.search(r"^(.*): (.*)", commit)
            if not r:
                continue # Filter out non-compliant commits

            commit_type = r.group(1)
            commit_scope = ""
            commit_desc = r.group(2)

        if commit_type == "feat":
            if commit_scope != "":
                features.append(f"{commit_scope}: {commit_desc}")
            else:
                features.append(commit_desc)
        elif commit_type == "fix":
            if commit_scope != "":
                fixes.append(f"{commit_scope}: {commit_desc}")
            else:
                fixes.append(commit_desc)
        elif commit_type == "contributors":
            contributors.append(commit_desc)

    return features, fixes, contributors

def generate_changelog(start_rev : str, end_rev : str) -> str:
    res : str = f"# SILICON changelog _{end_rev}_ (from _{start_rev}_)\n\n"

    commits = get_commits(start_rev, end_rev)
    features, fixes, contributors = parse_commits(commits)

    res += "## New features\n"
    for feat_commit in features:
        res += f"- {feat_commit}\n"


    res += "\n## Bug fixes:\n"
    for fix_commit in fixes:
        res += f"- {fix_commit}\n"

    res += "\n## Contributors:\n"
    for contributor_commit in contributors:
        res += f"- {contributor_commit}\n"
    return res

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate a changelog from semantic commits")

    parser.add_argument(
        "--start",
        type=str,
        default="origin/main",
        help="Starting revision for the changelog."
    )

    parser.add_argument(
        "--end",
        type=str,
        default="HEAD",
        help="Last revision for the changelog."
    )

    parser.add_argument(
        "--output-file",
        type=str,
        default="",
        help="Output file name (if required)."
    )

    args = parser.parse_args()

    start_rev : str = args.start
    end_rev : str = args.end
    output_fn : str = args.output_file

    print(f"Generating the changelog for {end_rev} (starting from {start_rev})")
    print()

    changelog = generate_changelog(start_rev, end_rev)

    if output_fn != "":
        with open(output_fn, "w") as f:
            f.write(changelog)
    else:
        from rich.console import Console
        from rich.markdown import Markdown

        console = Console()
        md = Markdown(changelog)
        console.print(md)
