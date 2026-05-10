#!/usr/bin/env bash

set -euo pipefail

owner="${1:?owner is required}"
repo="${2:?repo is required}"
shift 2

if [[ "$#" -eq 0 ]]; then
  echo "At least one package name is required" >&2
  exit 1
fi

deleted_any=false

for package_name in "$@"; do
  query="name:${package_name} format:raw"
  json="$(cloudsmith list packages "${owner}/${repo}" -F pretty_json -q "${query}")"
  mapfile -t package_ids < <(jq -r '.data[]?.slug_perm // empty' <<<"${json}")

  if [[ "${#package_ids[@]}" -eq 0 ]]; then
    echo "No Cloudsmith packages found for ${package_name}"
    continue
  fi

  for package_id in "${package_ids[@]}"; do
    echo "Deleting ${owner}/${repo}/${package_id}"
    cloudsmith delete -y "${owner}/${repo}/${package_id}"
    deleted_any=true
  done
done

if [[ "${deleted_any}" == "false" ]]; then
  echo "No Cloudsmith PR preview packages matched the requested package names"
fi
