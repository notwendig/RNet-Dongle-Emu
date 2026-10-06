#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=$(cat "$ROOT/VERSION")
OUT=${1:-"$ROOT/RNet-Dongle-Emu-v${VERSION}.zip"}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/RNet-Dongle-Emu-v${VERSION}"
rsync -a \
  --exclude '.git' \
  --exclude 'build' \
  --exclude '__pycache__' \
  --exclude '*.rnd' \
  --exclude '*.R-net' \
  --exclude '*.bin' \
  --exclude '*.exe' \
  --exclude '*.dll' \
  --exclude '*.log' \
  "$ROOT/" "$TMP/RNet-Dongle-Emu-v${VERSION}/"
(cd "$TMP" && zip -qr "$OUT" "RNet-Dongle-Emu-v${VERSION}")
echo "$OUT"
