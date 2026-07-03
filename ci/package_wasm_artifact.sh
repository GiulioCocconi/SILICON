#!/usr/bin/env bash

set -euo pipefail

package_name="${1:?package name is required}"
package_version="${2:?package version is required}"
package_file="${package_name}-${package_version}.zip"
package_path="$(pwd)/out/${package_file}"
staging_dir="$(mktemp -d)"

cleanup() {
  rm -rf "${staging_dir}"
}
trap cleanup EXIT

mkdir -p out
rm -f "out/${package_file}"

html_files=(bin/*.html)
if [[ ${#html_files[@]} -ne 1 ]]; then
  echo "Expected exactly one WASM HTML entrypoint in bin/, found ${#html_files[@]}" >&2
  exit 1
fi

cp "${html_files[0]}" "${staging_dir}/index.html"
cp bin/*.js bin/*.wasm "${staging_dir}/"

(
  cd "${staging_dir}"
  zip -r "${package_path}" .
)
