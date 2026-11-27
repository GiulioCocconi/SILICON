#!/usr/bin/env bash
# Copyright (c) 2026. Giulio Cocconi
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cmake -S "$repo_dir" -B "$repo_dir/build/docs" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY="$repo_dir/build/docs/lib" \
  -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="$repo_dir/build/docs/lib" \
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$repo_dir/build/docs/bin" \
  -DSILICON_USE_VCPKG=OFF \
  -DSILICON_BUILD_DOCUMENTATION=ON \
  -DSILICON_BUILD_TESTS=OFF \
  -DSILICON_BUILD_BENCHMARKS=OFF
cmake --build "$repo_dir/build/docs" --target "${1:-user_docs}" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
