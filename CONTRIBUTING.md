# Contributing

Thank you for contributing to SILICON. The authoritative contribution guide is
**[Contributing to SILICON](https://giuliococconi.github.io/SILICON/docs/guide/contributing/)**.
It covers setup, Debug builds, tests, issues, commits, pull requests, review, and
the complete CI environment.

The Nix development shell is the first-class contribution environment. The
repository's `pre-commit` executable and hooks are available only while that
shell is active; see the authoritative guide before running contributor checks.

## Essential requirements

- SILICON is licensed under the GNU General Public License, version 3 or later.
  You retain copyright in your contribution and agree to submit it under the same
  license.
- You must be the author of the submitted work or otherwise have the right to
  submit it.
- Every commit must certify the
  [Developer Certificate of Origin](https://developercertificate.org/) with a
  `Signed-off-by:` line. Create commits with `git commit -s`.
- Commit subjects must use `<type>(<optional-scope>): <imperative description>`.
  The allowed types are `feat`, `fix`, `docs`, `style`, `refactor`, `perf`,
  `test`, `build`, `ci`, `chore`, `revert`, `contributors`, and `misc`.
- A first-time contributor must add their GitHub username to `AUTHORS.md` in a
  separate commit with the exact subject
  `contributors: add @username to AUTHORS.md`.

Git's `format.signoff` setting affects patches created by `git format-patch`; it
does not automatically sign off ordinary commits. Use `git commit -s` explicitly.

## Responsible use of AI tools

The DCO requires contributors to know the origin of their work and have the right
to submit it. Contributions that appear substantially generated without
understanding or verification will be rejected, including plausible-looking but
incorrect code or documentation and changes the contributor cannot explain.

AI tools may support learning, debugging, review, and consistency checks, but
contributors remain responsible for understanding, licensing, testing, and
verifying every submitted change. See the
[full policy](https://giuliococconi.github.io/SILICON/docs/guide/contributing/workflow#responsible-use-of-ai-tools).
