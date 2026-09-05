# Contribution Workflow, Issues, and Git

This page owns the complete contribution workflow and the project's Git and
submission rules. Build, test, formatting, and pre-commit commands live in
[Build and Test Locally](./build.md); CI behavior lives in
[Continuous Integration](./ci.md).

## Step-by-step contribution workflow

### 1. Choose one contribution

Search the existing [issues](https://github.com/GiulioCocconi/SILICON/issues)
and pull requests before starting. For substantial features or architectural
changes, open an issue and agree on the intended behavior first. The
[issue guide](#write-a-useful-issue) explains what information to include.

### 2. Fork and clone SILICON

Create a fork with GitHub's **Fork** button, then clone your fork:

```bash
git clone https://github.com/<your-username>/SILICON.git
cd SILICON
```

The clone names your fork `origin`. Register the canonical repository as
`upstream` and verify both remotes:

```bash
git remote add upstream https://github.com/GiulioCocconi/SILICON.git
git remote -v
```

The expected roles are:

```text
origin    your fork; push contribution branches here
upstream  GiulioCocconi/SILICON; fetch canonical history here
```

GitHub's [fork documentation](https://docs.github.com/en/pull-requests/how-tos/work-with-forks)
explains this model in more detail.

### 3. Enter the contribution environment

Follow [Linux with Nix](./build.md#linux-with-nix) and keep the
development shell active throughout the work. That page defines the supported
environment and its tooling availability.

Build and test the unchanged `main` branch before editing. This distinguishes a
local environment problem from one introduced by the contribution.

### 4. Create a focused branch

Create the branch directly from current canonical history:

```bash
git fetch upstream
git switch -c fix-short-description upstream/main
```

Use a descriptive name such as `fix-wire-routing` or `docs-contributor-build`.
Keep unrelated changes on separate branches and in separate pull requests.

### 5. Implement and inspect the change

Follow nearby code patterns. SILICON uses C++23, treats warnings as errors on
project targets, and expects behavioral changes to include focused tests. Avoid
drive-by refactors and formatting. Files under `vendor/` are third-party code and
should change only when the contribution specifically updates that code.

Inspect both unstaged and staged changes before committing:

```bash
git status
git diff
git add <files>
git diff --cached
```

Run the relevant build, tests, and formatting checks from the active Nix shell
using the [local verification guide](./build.md).

### 6. Commit the work

Create one coherent commit for each logical change. Every commit needs a valid
subject and DCO sign-off:

```bash
git commit -s -m "fix(simulator): handle cancelled delayed updates"
```

Follow the [commit message rules](#commit-message-rules) and
[DCO requirements](#dco-sign-off-and-authorsmd). Before publishing the branch,
run the [pre-submission policy check](./build.md#run-the-pre-submission-policy-check)
from the same active Nix shell.

### 7. Push and open the pull request

```bash
git push -u origin fix-short-description
```

Open a pull request from your fork's branch to
`GiulioCocconi/SILICON:main`. Follow the
[pull-request guide](#write-a-reviewable-pull-request), check the **Files changed**
tab yourself, and use a draft when the work is not ready to merge.

After the PR is pushed to GitHub the CI starts. Read the

### 8. Respond to review

Push follow-up commits to the same branch; GitHub updates the pull request
automatically. Answer questions, record deliberate tradeoffs, and re-run the
relevant verification after each change. Use the [advanced Git guide](#advanced-git-guide)
to create fixup commits, synchronize with upstream, and safely update rewritten
history.

## Commit message rules

CI validates every non-merge commit subject against this shape:

```text
<type>(<optional-scope>): <imperative description>
```

| Type           | Use it for                                       |
| -------------- | ------------------------------------------------ |
| `feat`         | New user-facing behavior or capability.          |
| `fix`          | A defect correction.                             |
| `docs`         | Documentation-only changes.                      |
| `style`        | Formatting or other non-semantic source changes. |
| `refactor`     | Code restructuring without a feature or fix.     |
| `perf`         | Performance improvements.                        |
| `test`         | Adding or correcting tests.                      |
| `build`        | Build-system or dependency changes.              |
| `ci`           | Continuous-integration changes.                  |
| `chore`        | Maintenance outside source and test behavior.    |
| `revert`       | Reverting an earlier commit.                     |
| `contributors` | Updates to `AUTHORS.md`.                         |
| `misc`         | A last-resort category; prefer a specific type.  |

Use a short scope naming a recognizable subsystem, such as `core`, `ui`,
`yosys`, `simulator`, or `nix`. Write the description in imperative present
tense—“add”, “fix”, or “preserve”—and do not end it with a period.

Good examples:

```text
fix(waveform): preserve the selected track after refresh
feat(yosys): import multi-file Verilog projects
test(core): cover recursive subcircuit rejection
docs: explain the contributor CI checks
```

Avoid vague subjects such as `fix: changes`, subjects that merely name a file,
and commits that combine unrelated behavior and cleanup. When the reason is not
obvious, add a body after a blank line. Explain why the old behavior was wrong,
the invariant established by the change, and any tradeoff worth preserving; the
diff already shows how the files changed.

The convention is based on [Conventional Commits](https://www.conventionalcommits.org/),
with the SILICON-specific type list above.

## DCO sign-off and `AUTHORS.md`

SILICON is licensed under the GNU General Public License, version 3 or later.
Contributors retain copyright in their work and agree to license submitted work
under the same terms. You must be its author or otherwise have the right to
submit it.

Every commit must certify the
[Developer Certificate of Origin](https://developercertificate.org/) through the
`-s` option to `git commit`. It appends your configured identity:

```text
Signed-off-by: Your Name <your.email@example.com>
```

Check a commit when in doubt:

```bash
git show -s --format=%B HEAD
```

Git's `format.signoff` setting applies to patches created by `git format-patch`;
it does not sign ordinary commits automatically.

First-time contributors must add their GitHub username to `AUTHORS.md` in a
separate commit with this exact subject:

```text
contributors: add @username to AUTHORS.md
```

Replace `username` with the account opening the pull request. CI enforces both
the file change and exact subject.

### Responsible use of AI tools

The DCO requires you to know the origin of submitted work and have the right to
license it. Contributions that appear substantially generated without
understanding or careful verification will be rejected. Warning signs include
plausible but incorrect code, irrelevant verbosity, misleading documentation or
commit messages, and changes the contributor cannot explain.

AI may support learning, debugging, review, or consistency checks. You remain
responsible for every submitted line: verify it, test it, understand it, and
ensure that you have the right to contribute it.

## Write a reviewable pull request

A pull request should explain:

1. the problem and who it affects;
2. why the chosen approach is appropriate;
3. changed behavior and compatibility effects;
4. exact test commands and relevant manual scenarios; and
5. areas that deserve particular review attention.

Use `Closes #123` or `Fixes #123` when merging into the default branch should
close an issue; otherwise write `Related to #123`. GitHub documents the supported
[issue-closing keywords](https://docs.github.com/en/issues/tracking-your-work-with-issues/using-issues/linking-a-pull-request-to-an-issue).

Keep the diff focused. Include necessary tests and documentation, but move
unrelated cleanup elsewhere. Call out project-format, serialization, dependency,
or build changes. Provide before/after evidence for visual changes. For
performance work, describe the benchmark, input, build type, and measurements.

Use a draft pull request for early architectural feedback. Mark it ready only
after self-review, local verification, and removal of debug output, local paths,
generated build products, and accidental dependency changes. GitHub's guide to
[helping others review changes](https://docs.github.com/en/pull-requests/concepts/helping-others-review-your-changes)
provides more detail.

## Advanced Git guide

These tools are useful for larger or long-lived pull requests. A small
contribution does not need to use all of them.

### Keep the branch up to date

Fetch canonical history and rebase the contribution onto it:

```bash
git fetch upstream
git rebase upstream/main
```

If a conflict occurs, resolve it, stage the affected files, and continue. Abort
to return to the state before the rebase:

```bash
git add <resolved-files>
git rebase --continue

# Or cancel the operation
git rebase --abort
```

Avoid merging `upstream/main` into a contribution branch unless there is a
specific reason; rebasing keeps the review history linear.

### Organize commits with interactive rebase

```bash
git rebase -i upstream/main
```

Interactive rebase can reorder commits, squash related work, reword messages,
edit or split a commit, and remove unnecessary commits. Each resulting commit
should be coherent and leave the project in a reasonable state.

### Use fixup commits and autosquash

During review, attach a correction to the logical commit it belongs to:

```bash
git add <files>
git commit --fixup=<target-commit>
```

Create as many fixups as needed, then fold them into their targets:

```bash
git fetch upstream
git rebase -i --autosquash upstream/main
```

This preserves convenient review iterations without leaving `fixup!` commits in
the final history.

### Update rewritten history safely

After rebasing or squashing, update the branch on your fork with:

```bash
git push --force-with-lease origin <branch>
```

Prefer `--force-with-lease` to `--force`. It refuses to replace unexpected
remote work. Never force-push a branch belonging to another contributor.

### Recover from mistakes

The reflog records earlier positions of local references:

```bash
git reflog
git show <commit>
```

If a rewrite appears to lose a commit, locate and inspect its prior ID before
performing more history-changing operations.

### Compare two revisions of a pull request

After substantial reorganization, compare the old and new commit series:

```bash
git range-diff <old-base>..<old-tip> <new-base>..<new-tip>
```

Unlike a normal file diff, `range-diff` shows how corresponding commits changed
between review revisions.

### Find a regression

When an older revision works and the current one fails, bisect the history:

```bash
git bisect start
git bisect bad
git bisect good <known-good-commit>
```

Test each selected revision and mark it with `git bisect good` or `git bisect
bad`. Automate the loop when a command can identify the failure:

```bash
git bisect run <test-command>
```

Always restore the original branch when finished:

```bash
git bisect reset
```

### Remember the remote model

```text
upstream/main
    │
    ├── canonical SILICON history
    │
    └── your contribution commits
             │
             └── pushed to origin/<branch>
```

Fetch and rebase from `upstream`; push only to `origin`. If a rebase rewrites
already-published commits, use `--force-with-lease` and tell reviewers what
changed.

### Git reference

- [`git rebase`](https://git-scm.com/docs/git-rebase), including interactive
  rebasing and `--autosquash`;
- [`git commit`](https://git-scm.com/docs/git-commit), including `--fixup`;
- [`git push`](https://git-scm.com/docs/git-push), including
  `--force-with-lease`;
- [`git reflog`](https://git-scm.com/docs/git-reflog);
- [`git range-diff`](https://git-scm.com/docs/git-range-diff); and
- [`git bisect`](https://git-scm.com/docs/git-bisect).

## Write a useful issue

Keep one issue focused on one problem or proposal. Use a title that names the
affected behavior rather than a generic label such as “it does not work.”

::: tip
Before starting filing the issue [look at existing ones](https://github.com/GiulioCocconi/SILICON/issues)
to avoid duplications.
:::

For a bug, include:

- the SILICON version, nightly commit, or Git revision;
- operating system and whether the build is downloaded or built locally;
- exact steps to reproduce from a fresh launch or a minimal project;
- expected and actual behavior;
- relevant text logs, error messages, screenshots, or a small `.sil` reproducer;
- whether the problem is consistent and the last version known to work.

Remove credentials, private paths, and unrelated data. A small circuit that
isolates the failure is more useful than a large project with unknown relevant
state.

For a feature request, explain the user problem, an example workflow, the desired
outcome, and important constraints. Separate required behavior from optional
implementation ideas.
