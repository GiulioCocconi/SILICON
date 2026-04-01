#!/usr/bin/env bash

# Exit immediately if a command exits with a non-zero status
set -e

DEPS_TO_REMOVE=(
  "qtbase"
  "qtsvg"
  "gtest"
)

VCPKG_FILE="${1:-vcpkg.json}"

# Check if jq is installed
if ! command -v jq &> /dev/null; then
    echo "Error: 'jq' is not installed."
    exit 1
fi

# Check if file exists
if [ ! -f "$VCPKG_FILE" ]; then
    echo "Error: File '$VCPKG_FILE' not found!"
    exit 1
fi

echo "Processing $VCPKG_FILE..."

# Loop through the hardcoded dependencies and remove them
for DEP in "${DEPS_TO_REMOVE[@]}"; do
    echo " -> Removing '$DEP'..."

    # Use jq to filter out the dependency (handles both strings and objects)
    jq --arg dep "$DEP" '
        if has("dependencies") then
            .dependencies |= map(select(
                if type == "string" then
                    . != $dep
                else
                    .name != $dep
                end
            ))
        else
            .
        end
    ' "$VCPKG_FILE" > "${VCPKG_FILE}.tmp"

    # Replace the original file with the updated one
    mv "${VCPKG_FILE}.tmp" "$VCPKG_FILE"
done

echo "Done! Successfully updated $VCPKG_FILE."
