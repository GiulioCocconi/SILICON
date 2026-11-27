#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

# Use the same native toolchain and documentation dependencies as Pages CI.
exec nix develop "$repo_dir#webpage" --command bash -s -- "$repo_dir" <<'BUILD'
set -euo pipefail
repo_dir="$1"

cd -- "$repo_dir/webpage"
bun install --frozen-lockfile
# Builds the website first, then generates the component reference and renders
# VitePress through the user_docs CMake target.
bun run build:all

cd -- "$repo_dir"
doxygen Doxyfile

preview_dir="$(mktemp -d "${TMPDIR:-/tmp}/siliconwebpage.XXXXXX")"
trap 'rm -rf -- "$preview_dir"' EXIT
mkdir -p "$preview_dir/SILICON/internaldocs"
cp -R webpage/dist/. "$preview_dir/SILICON/"
cp -R docs/html/. "$preview_dir/SILICON/internaldocs/"

printf 'Preview: http://127.0.0.1:8080/SILICON/\n'
python3 -m http.server 8080 --bind 127.0.0.1 --directory "$preview_dir"
BUILD
