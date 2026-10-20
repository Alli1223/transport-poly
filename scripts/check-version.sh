#!/usr/bin/env bash
# Checks that the version in CMakeLists.txt and src/core/version.hpp agree, and with $1 if
# given (a release tag without its leading "v"; a pre-release suffix such as -rc1 is ignored).
# Prints the version.
set -euo pipefail
cd "$(dirname "$0")/.."

cmake_version=$(awk '/^project\(/ { inproject = 1 } inproject && /VERSION/ { print $2; exit }' CMakeLists.txt)
code_version=$(sed -n 's/.*kVersionString = "\([^"]*\)".*/\1/p' src/core/version.hpp)

if [[ -z "$cmake_version" || -z "$code_version" ]]; then
    echo "Could not read the version from CMakeLists.txt or src/core/version.hpp" >&2
    exit 1
fi
if [[ "$cmake_version" != "$code_version" ]]; then
    echo "CMakeLists.txt says $cmake_version but src/core/version.hpp says $code_version" >&2
    exit 1
fi
if [[ $# -ge 1 && "${1%%-*}" != "$cmake_version" ]]; then
    echo "The tag says $1 but the code says $cmake_version" >&2
    exit 1
fi
if ! grep -q "^## \[$cmake_version\]" CHANGELOG.md; then
    echo "CHANGELOG.md has no section for $cmake_version" >&2
    exit 1
fi
echo "$cmake_version"
