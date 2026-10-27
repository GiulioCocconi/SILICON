#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
preview_dir="$(mktemp -d "${TMPDIR:-/tmp}/siliconwebpage.XXXXXX")"
trap 'rm -rf -- "$preview_dir"' EXIT

cd -- "$repo_dir"
python3 ci/theme/generate.py

cd -- "$repo_dir/webpage"
bun install --frozen-lockfile
bun run build:all

cd -- "$repo_dir"
doxygen Doxyfile

mkdir -p "$preview_dir/SILICON/internaldocs"
cp -R webpage/dist/. "$preview_dir/SILICON/"
cp -R docs/html/. "$preview_dir/SILICON/internaldocs/"

printf 'Preview: http://127.0.0.1:8080/SILICON/\n'
python3 -m http.server 8080 --bind 127.0.0.1 --directory "$preview_dir"
