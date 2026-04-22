#!/usr/bin/env bash

TMP="/tmp/siliconwebpage"

mkdir -p $TMP/SILICON

cd $(dirname "$0")
bun install
bun run build

cp -r dist/* $TMP/SILICON

cd ..
doxygen
cp -r docs/html/ $TMP/SILICON/internaldocs

python -m http.server 8080 --bind 127.0.0.1 --directory $TMP

rm -r $TMP
