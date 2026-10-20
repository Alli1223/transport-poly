#!/usr/bin/env bash
# Prints the CHANGELOG.md section of version $1 (without its heading, and ignoring a pre-release
# suffix such as -rc1), for the release notes.
set -euo pipefail
cd "$(dirname "$0")/.."
awk -v version="${1%%-*}" '
    /^## \[/ { if (printing) exit; printing = index($0, "## [" version "]") == 1; next }
    /^\[.*\]: / { next }
    printing { print }
' CHANGELOG.md
