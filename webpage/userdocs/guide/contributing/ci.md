# Continuous Integration

SILICON uses GitHub Actions for policy checks, native and WASM builds, tests,
snapshot packaging, releases, and documentation deployment. A green local build
is necessary, but CI also validates commit history and platform-specific paths
that may not exist on your machine.

Linux CI jobs that run release, policy, formatting, and publication scripts use
`nix develop .#lightCI --command ...`. This lightweight flake shell supplies
Python (including PyGithub) and git-cliff without installing them separately in
each workflow. Platform builds keep their own build environments.

## Pull-request checks

The following workflows run when a pull request is opened, updated, or reopened.

### PR compliance

`ci/check-pr-compliance.py` computes the merge base with the pull request's base
branch and checks every non-merge contribution commit. It fails the workflow and
updates one bot comment if it finds:

- whitespace errors reported by `git diff --check`;
- a violation of the documented
  [commit-message rules](./workflow.md#commit-message-rules) or
  [DCO sign-off](./workflow.md#dco-sign-off-and-authorsmd); or
- a violation of the first-contributor
  [`AUTHORS.md` rule](./workflow.md#dco-sign-off-and-authorsmd).

The local counterpart and its Nix-shell requirement are documented once in
[Run the pre-submission policy check](./build.md#run-the-pre-submission-policy-check).
The identity-based `AUTHORS.md` check remains CI-only because it uses the pull
request author's GitHub username.

### Formatting assistant

The formatting workflow examines changed `.cpp` and `.hpp` files outside
`vendor/`. If `clang-format` would change the edited lines, the bot creates or
updates a pull-request comment containing the suggested diff. It does not modify
the contributor's branch. Stage and check C++ changes locally to avoid this
round trip; see [Formatting and static analysis](./build.md#formatting-and-static-analysis).

### Snapshot builds

The snapshot workflow runs only when a pull request changes C++, headers, Python,
Yosys inputs, CMake files, Nix files, workflows, composite actions, CI scripts,
vcpkg manifests/configuration, or overlay ports under `vcpkg-ports/`.
Documentation-only changes do not trigger it.

| Job         | Pull-request behavior                                                                                                                       |
| ----------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| Linux (Nix) | Runs `nix build --print-build-logs`; the flake enables its check phase and default sanitizer configuration.                                 |
| Windows     | Configures a MinGW/Ninja Debug build, builds it, and runs `ctest --output-on-failure`.                                                      |
| WASM        | Installs Emscripten 4.0.7 and Qt 6.11.0 for single-threaded WASM, builds Debug, and packages the result. Tests are disabled for Emscripten. |

Windows and WASM packages are passed between jobs as GitHub Actions artifacts
with one-day retention. Pull requests from forks are built but are not published
to Cloudsmith because fork workflows do not receive publication authority.
Branches in the canonical repository may publish PR preview packages. Builds for
the same pull request share a concurrency group, so pushing a new revision cancels
an older in-progress run.

### CI dependency binary cache

Windows and WASM CI use `.github/actions/setup-vcpkg` to fetch the exact
`builtin-baseline` from the checked-out `vcpkg.json` into runner temporary
storage and bootstrap it before CMake configures manifest dependencies. The
prepared source directory is passed as `FETCHCONTENT_SOURCE_DIR_VCPKG`.
Ubuntu installs Mono to run the NuGet executable obtained by `vcpkg fetch nuget`.

The cache is the owner's GitHub Packages NuGet feed,
`https://nuget.pkg.github.com/<repository-owner>/index.json`. A temporary NuGet
configuration holds source credentials, an API key, and `defaultPushSource`.
Authentication uses `GITHUB_TOKEN`; no PAT secret is required. Both reusable
workflows and their callers declare `packages: write`, while the vcpkg provider
uses **read** for every PR (including internal release-preparation PRs) and
**readwrite** for main snapshots and releases. GitHub additionally downgrades
fork PR tokens to read-only.

CI sets `VCPKG_BINARY_SOURCES=clear;nugetconfig,<temporary-config>,<mode>` and
`X_VCPKG_NUGET_ID_PREFIX=silicon-vcpkg-`. Package metadata associates cache
packages with the workflow repository through `VCPKG_NUGET_REPOSITORY`.
The old `build/vcpkg_installed` and `build/_deps` directory caches are no longer
restored or saved. vcpkg's ABI identifiers determine compatibility; misses build
from source and writable jobs upload the resulting binaries. See the
[vcpkg binary cache configuration](https://learn.microsoft.com/en-us/vcpkg/reference/binarycaching)
and [GitHub NuGet authentication](https://docs.github.com/en/packages/working-with-a-github-packages-registry/working-with-the-nuget-registry).
This setup applies only to CI; local builds keep their existing CMake bootstrap.

### Binary cache retention

**Cleanup vcpkg binary cache** runs Sundays at **03:00 UTC**. Scheduled runs
delete eligible versions. Manual dispatch defaults `dry_run` to **true**;
inspect the workflow summary before explicitly selecting a deleting run.
Cleanup runs are serialized and use a dedicated `packages: write` job,
independent of builds and releases.

Only NuGet package IDs starting with `silicon-vcpkg-` whose owner and repository
association can be verified against SILICON are managed. Unknown associations
are skipped. For each package ID, cleanup always protects the newest version by
`created_at`, using the higher version ID to break ties. Other versions are
deleted only when strictly older than **30 days**. Downloads, recent updates,
and release associations do not extend retention. Packages and versions are
fully paginated, ownership and eligibility are rechecked before deletion, and
only individual versions are deleted. The workflow never deletes an entire
package. Its summary lists deletions, retained counts, dry-run candidates, and
failures; any cleanup failure fails that job.

The workflow repository needs **admin access** to these cache packages in
addition to its token's `packages: write` permission. Packages published by its
workflow receive that access automatically. For pre-existing packages, grant
the SILICON repository admin access in the package's Actions access settings.
Unverifiable associations are never treated as permission to delete.

Source rebuilds on cache misses are the normal recovery path, including when an
older release is rebuilt after its cached version expires. GitHub can restore a
deleted version only within **30 days of deletion** and only while its package
namespace and version remain available; restoration also requires package admin
access. See [GitHub's deletion and restoration rules](https://docs.github.com/en/packages/learn-github-packages/deleting-and-restoring-a-package).

::: tip
GitHub shows a workflow as skipped when none of its configured paths changed.
That is different from a failed job. Expand the check and read its trigger before
trying to fix a skipped documentation-only snapshot build.
:::

## What runs after merge

Code and build-system changes on `main` start Linux/Nix, Windows, and WASM
validation. The Windows and WASM ZIPs are built once from the exact main commit
and handed to the publication job as GitHub Actions artifacts.

The main snapshot version is `X.Y.Z-dev.<short-sha>`, calculated by
`python ci/release.py unstable-version`. It is passed to CMake as
`SILICON_VERSION`, so the running application reports it. The dev version is
never committed into CMake/vcpkg/Nix and is never tagged. Once all builds pass,
the ZIPs are published to `silicon-unstable-windows` and
`silicon-unstable-wasm` in the Cloudsmith release repository using OIDC.

Only after the unstable WASM package is published does the snapshot workflow
deploy Pages. Pages downloads existing `silicon-stable-wasm` and
`silicon-unstable-wasm` packages into `/wasm/stable/` and `/wasm/unstable/`.
Website/documentation-only pushes deploy Pages directly using the latest
published packages; they never rebuild SILICON. `WASM_DEFAULT_CHANNEL` chooses
the generic online action's preferred channel (`stable` by default). If no
stable package exists, the website offers the unstable channel instead.
Either WASM channel may be absent when Pages selects the latest available
packages; an explicitly requested version must exist. Downloads are checked
against Cloudsmith's SHA-256 before extraction. Pages deployment checks that
its source commit is still the current `main` commit, so an older queued run
fails instead of replacing a newer site.

Composite action, overlay-port, and vcpkg configuration changes count as build
changes in both the snapshot trigger and documentation deployment classification.
When a push also changes documentation, Pages waits for snapshot publication.

When an internal, merged pull request had Cloudsmith preview packages, the cleanup
workflow deletes its Windows and WASM previews. A separate workflow removes the
closed pull request's GitHub Actions caches.

## Releases and dependency updates

Maintainers use **Actions → Release → Run workflow**:

1. Choose `prepare` and `stable` or `beta` (or `rc`).
2. Review and edit the generated release preparation PR, then merge it to `main`.
3. Choose `publish` and the matching channel. Do not type a version.

Preparation calls `ci/release.py next <channel>` to calculate the version,
updates CMake/vcpkg/Nix with `ci/semver.py`, and opens a branch named
`release/vX.Y.Z...`. A stable preparation also updates `CHANGELOG.md` with
git-cliff. Beta and RC preparations put incremental git-cliff notes in the PR
body but leave the persistent changelog alone.

Publication verifies that the exact `main` commit is the merge of the matching
`release/vX.Y.Z...` preparation PR and validates the merged metadata before building.
Linux/Nix, Windows, and WASM use the same reviewed commit SHA. After successful
builds/tests it creates an annotated tag, a **draft** GitHub Release, publishes
the exact ZIPs to Cloudsmith through OIDC, uploads those same ZIPs and
`SHA256SUMS` to GitHub, and finally publishes the Release. A failed Cloudsmith
publication leaves the GitHub Release as a draft. Stable publication then
refreshes `/wasm/stable/`; beta/RC publication does not.
Retries skip Cloudsmith packages and GitHub assets whose SHA-256 already matches;
an existing artifact with different contents fails publication. The reviewed
commit must still be current `main` when Pages is deployed.

| Channel | Version | Persistent metadata | GitHub | Cloudsmith |
| --- | --- | --- | --- | --- |
| Unstable | `X.Y.Z-dev.<sha>` | Unchanged; never tagged | None | `silicon-unstable-*` |
| Beta | `X.Y.Z-beta.N` | Committed; beta tags ignored as changelog boundaries | Tagged prerelease, not latest | `silicon-prerelease-*` |
| Stable | `X.Y.Z` | Committed; `CHANGELOG.md` updated | Tagged stable latest release | `silicon-stable-*` |

`python ci/semver.py --check` runs in ordinary snapshot CI and release
preparation/publication. Version drift fails before builds. The full version
comes from the CMake cache variable `SILICON_VERSION`; `project(VERSION ...)`
receives only its numeric core.

Dependabot checks vcpkg and GitHub Actions dependencies weekly. Its configured
commit prefixes follow the repository convention: `chore(vcpkg):` and
`chore(ci):`.

## Investigating a failure

1. Open the failed check and identify the first failing command, not only the
   final job summary.
2. Compare the job's operating system, build type, compiler, generator, and
   dependency versions with your local environment.
3. Reproduce the narrowest equivalent command locally using the
   [build and test guide](./build.md).
4. Fix the underlying problem and push normally. Do not close and recreate the
   pull request merely to rerun CI.
5. If the failure looks unrelated or intermittent, record the failing job link
   and relevant log section in the pull request before rerunning it.

The root `CODEOWNERS` rule assigns the current maintainer to all paths. Approval
and merge requirements are repository settings rather than behavior defined by
these workflow files, so do not assume that a locally passing command represents
every repository-side merge rule.
