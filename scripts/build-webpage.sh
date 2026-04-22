#!/usr/bin/env bash
set -euo pipefail

out=/tmp/silicon-webpage-dist
mkdir -p "$out"

doxygen Doxyfile || true
mkdir -p "$out/internaldocs"
cp -r docs/html/* "$out/internaldocs"

cd webpage
bun install --frozen-lockfile
bun run build
cd ..

cp -r webpage/dist/* "$out"

cd "$out"
exec python3 -m http.server 8080