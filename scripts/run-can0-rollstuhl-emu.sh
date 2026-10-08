#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PROXY="$ROOT/build/rnet-can-proxy/rnet-can-proxy"
CHAIR="$ROOT/build/rollstuhl.emu/rollstuhl.emu"

[[ -x "$PROXY" ]] || { echo "FEHLER: $PROXY fehlt. Erst build-can0-rollstuhl-emu.sh starten." >&2; exit 1; }
[[ -x "$CHAIR" ]] || { echo "FEHLER: $CHAIR fehlt. Erst build-can0-rollstuhl-emu.sh starten." >&2; exit 1; }

ip link show can0 >/dev/null 2>&1 || { echo "FEHLER: can0 fehlt." >&2; exit 1; }
ip link show can1 >/dev/null 2>&1 || { echo "FEHLER: can1 fehlt." >&2; exit 1; }

"$PROXY" can0 &
PID=$!
trap 'kill "$PID" 2>/dev/null || true' EXIT INT TERM
sleep 0.2
set +e
"$CHAIR" can1
RC=$?
set -e
exit "$RC"
