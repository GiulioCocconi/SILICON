#!/usr/bin/env python3
"""Retain 30 days of SILICON's NuGet cache, always protecting each ID's newest version."""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
import json
import os
from pathlib import Path
from urllib.parse import quote, urlencode
from urllib.request import Request, urlopen


PREFIX = "silicon-vcpkg-"


class GitHubAPI:
    def __init__(self, token: str, base_url: str):
        self.base_url = base_url.rstrip("/")
        self.headers = {
            "Authorization": f"Bearer {token}",
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
        }

    def request(self, method: str, endpoint: str):
        request = Request(self.base_url + endpoint, headers=self.headers, method=method)
        with urlopen(request, timeout=60) as response:
            return json.load(response) if response.status != 204 else None

    def pages(self, endpoint: str, **params) -> list[dict]:
        items = []
        page = 1
        while True:
            query = urlencode({**params, "per_page": 100, "page": page})
            batch = self.request("GET", f"{endpoint}?{query}")
            if not isinstance(batch, list):
                raise ValueError(f"Expected a list from {endpoint}")
            items.extend(batch)
            if len(batch) < 100:
                return items
            page += 1


def created_at(version: dict) -> datetime:
    value = datetime.fromisoformat(version["created_at"].replace("Z", "+00:00"))
    if value.tzinfo is None:
        raise ValueError("Package version has no creation timezone")
    return value


def newest_id(versions: list[dict]) -> int | None:
    if not versions:
        return None
    newest = max(versions, key=lambda version: (created_at(version), int(version["id"])))
    return int(newest["id"])


def associated(package: dict, repository: dict) -> bool:
    linked = package.get("repository") or {}
    owner = package.get("owner") or {}
    return (
        package.get("package_type") == "nuget"
        and package.get("name", "").startswith(PREFIX)
        and owner.get("login", "").casefold() == repository["owner"]["login"].casefold()
        and linked.get("id") == repository["id"]
        and linked.get("full_name", "").casefold() == repository["full_name"].casefold()
    )


@dataclass
class Report:
    dry_run: bool
    versions: int = 0
    retained: int = 0
    skipped_packages: int = 0
    deleted: list[str] = field(default_factory=list)
    eligible: list[str] = field(default_factory=list)
    failures: list[str] = field(default_factory=list)

    def summary(self) -> str:
        lines = [
            "## vcpkg binary cache retention",
            "",
            f"Mode: {'dry run (no deletions)' if self.dry_run else 'delete eligible versions'}",
            "",
            "Policy: strictly older than 30 days; always keep the newest version per package ID.",
            "",
            f"- Versions inspected in verified packages: {self.versions}",
            f"- Retained versions: {self.retained}",
            f"- Deleted versions: {len(self.deleted)}",
            f"- Would delete (dry run): {len(self.eligible)}",
            f"- Skipped packages (association unverified): {self.skipped_packages}",
            f"- Failures: {len(self.failures)}",
        ]
        for title, entries in (("Deleted versions", self.deleted),
                               ("Would delete", self.eligible), ("Failures", self.failures)):
            if entries:
                lines.extend(["", f"### {title}", ""])
                lines.extend(f"- {entry}" for entry in entries)
        return "\n".join(lines) + "\n"


def cleanup(api: GitHubAPI, repository_name: str, dry_run: bool,
            now: datetime | None = None) -> Report:
    report = Report(dry_run=dry_run)
    cutoff = (now or datetime.now(timezone.utc)) - timedelta(days=30)
    try:
        repository = api.request("GET", f"/repos/{repository_name}")
        if (repository["name"].casefold() != "silicon"
                or repository["full_name"].casefold() != repository_name.casefold()):
            raise ValueError("Cleanup must run in the SILICON repository")
        owner = repository["owner"]
        if owner["type"] not in ("Organization", "User"):
            raise ValueError("Unknown package owner type")
        scope = "orgs" if owner["type"] == "Organization" else "users"
        root = f"/{scope}/{quote(owner['login'], safe='')}/packages"
        # Finish pagination before deleting anything: deletions can shift pages.
        packages = api.pages(root, package_type="nuget")
        for package in packages:
            name = package.get("name", "")
            if not name.startswith(PREFIX) or package.get("package_type") != "nuget":
                continue
            endpoint = f"{root}/nuget/{quote(name, safe='')}"
            try:
                detail = api.request("GET", endpoint)
                if detail.get("name") != name or not associated(detail, repository):
                    report.skipped_packages += 1
                    continue
                versions = api.pages(f"{endpoint}/versions", state="active")
                protected = newest_id(versions)
                report.versions += len(versions)
                report.retained += len(versions)
                candidates = [version for version in versions
                              if int(version["id"]) != protected and created_at(version) < cutoff]
                for version in candidates:
                    label = f"{name} / {version['name']} (ID {version['id']})"
                    try:
                        # Recheck ownership, the newest version, and creation time
                        # immediately before each individual version deletion.
                        detail = api.request("GET", endpoint)
                        if detail.get("name") != name or not associated(detail, repository):
                            report.skipped_packages += 1
                            break
                        current = api.pages(f"{endpoint}/versions", state="active")
                        version_id = int(version["id"])
                        if (version_id == newest_id(current)
                                or not any(int(item["id"]) == version_id for item in current)):
                            continue
                        fresh = api.request("GET", f"{endpoint}/versions/{version_id}")
                        if int(fresh["id"]) != version_id or created_at(fresh) >= cutoff:
                            continue
                        if dry_run:
                            report.eligible.append(label)
                        else:
                            api.request("DELETE", f"{endpoint}/versions/{version_id}")
                            report.deleted.append(label)
                            report.retained -= 1
                    except Exception as error:
                        report.failures.append(f"{label}: {error}")
            except Exception as error:
                report.failures.append(f"{name}: {error}")
    except Exception as error:
        report.failures.append(f"Cannot enumerate cache packages: {error}")
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", choices=("true", "false"), default="true")
    args = parser.parse_args()
    api = GitHubAPI(os.environ["GITHUB_TOKEN"], os.getenv("GITHUB_API_URL", "https://api.github.com"))
    report = cleanup(api, os.environ["GITHUB_REPOSITORY"], args.dry_run == "true")
    summary = report.summary()
    print(summary)
    if os.getenv("GITHUB_STEP_SUMMARY"):
        with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a") as output:
            output.write(summary)
    if report.failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
