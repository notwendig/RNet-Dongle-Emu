#!/usr/bin/env bash
set -euo pipefail

# Convert a candump file to semicolon CSV using RNetMsgBrokerTest.
# Usage:
#   scripts/candump_to_csv.sh testdata/candump.txt testdata/candump.csv

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 <candump.txt> [out.csv]" >&2
  exit 2
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INPUT="$1"
OUTPUT="${2:-${INPUT%.*}.csv}"

BIN="${RNETMSGBROKERTEST:-}"
if [[ -z "$BIN" ]]; then
  for candidate in \
    "$ROOT_DIR/build/RNetMsgBrokerTest" \
    "$ROOT_DIR/build/Desktop_Debug/RNetMsgBrokerTest" \
    "$ROOT_DIR/build/Desktop_Qt_6_Debug/RNetMsgBrokerTest" \
    "$ROOT_DIR/build/Desktop_Qt_6_Release/RNetMsgBrokerTest"; do
    if [[ -x "$candidate" ]]; then
      BIN="$candidate"
      break
    fi
  done
fi

if [[ -z "$BIN" || ! -x "$BIN" ]]; then
  echo "RNetMsgBrokerTest nicht gefunden. Erst bauen: scripts/build.sh" >&2
  exit 3
fi

"$BIN" -d "$INPUT" --full -o "$OUTPUT"
echo "CSV: $OUTPUT"
