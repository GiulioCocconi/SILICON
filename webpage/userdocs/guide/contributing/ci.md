# Continuous Integration

SILICON uses GitHub Actions for policy checks, native and WASM builds, tests,
snapshot packaging, releases, and documentation deployment. A green local build
is necessary, but CI also validates commit history and platform-specific paths
that may not exist on your machine.

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
Yosys inputs, CMake files, Nix files, workflows, CI scripts, or the vcpkg manifest.
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

::: tip
GitHub shows a workflow as skipped when none of its configured paths changed.
That is different from a failed job. Expand the check and read its trigger before
trying to fix a skipped documentation-only snapshot build.
:::

## What runs after merge

Every push to `main` starts the GitHub Pages workflow. It:

1. builds a Release WASM application;
2. generates Doxygen API documentation;
3. builds the React website and VitePress user documentation with Bun;
4. assembles those outputs into one Pages artifact; and
5. deploys the artifact to GitHub Pages.

Code and build-system changes matching the snapshot path filters also start
Release snapshot builds. Main-branch Windows and WASM packages are versioned from
the short commit SHA as `v<sha>-unstable` and published to the configured
Cloudsmith release repository under `silicon-unstable-windows` and
`silicon-unstable-wasm`. This is the downloadable nightly/unstable channel; it is
not the same as the local Debug build in the README.

When an internal, merged pull request had Cloudsmith preview packages, the cleanup
workflow deletes its Windows and WASM previews. A separate workflow removes the
closed pull request's GitHub Actions caches.

## Releases and dependency updates

Publishing a GitHub release triggers Release Windows and WASM packages only when
the tag matches one of these forms:

```text
v1.2.3
v1.2.3-rc.1
v1.2.3-beta.1
```

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
