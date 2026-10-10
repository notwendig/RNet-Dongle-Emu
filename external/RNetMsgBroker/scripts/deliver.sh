#!/usr/bin/env bash
set -euo pipefail

# Configure, build and create the versioned delivery ZIP via CMake target deliver.
# Usage:
#   scripts/deliver.sh
#   BUILD_DIR=build/Desktop_Debug BUILD_TYPE=Debug scripts/deliver.sh

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
GENERATOR="${GENERATOR:-Ninja}"

if command -v ninja >/dev/null 2>&1; then
  cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G "$GENERATOR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
else
  cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
fi

cmake --build "$BUILD_DIR" --target deliver -j"$(nproc)"
