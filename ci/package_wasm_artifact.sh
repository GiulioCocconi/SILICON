#!/usr/bin/env bash

set -euo pipefail

package_name="${1:?package name is required}"
package_version="${2:?package version is required}"
package_file="${package_name}-${package_version}.zip"

mkdir -p out
rm -f "out/${package_file}"

# Replace the collected files below if the WASM deployment layout changes.
zip -j "out/${package_file}" \
  bin/*.html \
  bin/*.js \
  bin/*.wasm
