#!/usr/bin/env bash
# Builds a release package for the current platform:
#   Linux: drav-<version>-Linux.tar.gz   macOS: drav-<version>-Darwin.dmg   Windows: drav-<version>-win64.zip
# Usage: scripts/package.sh [skia dir] [build dir]
# Needs Qt 6.8 on CMAKE_PREFIX_PATH (or $CONDA_PREFIX), CMake >= 3.24, Ninja and a static Skia build.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
skia="${1:-$root/third_party/skia}"
build="${2:-$root/build-release}"
prefix="${CMAKE_PREFIX_PATH:-${CONDA_PREFIX:-}}"

cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSKIA_DIR="$skia" \
    ${prefix:+-DCMAKE_PREFIX_PATH="$prefix"}
cmake --build "$build"
ctest --test-dir "$build" --output-on-failure
(cd "$build" && cpack)
ls "$build"/drav-*.*
