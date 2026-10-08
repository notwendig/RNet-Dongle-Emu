#!/usr/bin/env bash
set -Eeuo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
PROXY="$ROOT/build/rnet-can-proxy/rnet-can-proxy"
CHAIR="$ROOT/build/rollstuhl.emu/rollstuhl.emu"
REPLAY="$ROOT/examples/rnet-replay.txt"
MODE="${1:-emu}"

usage()
{
    echo "Aufruf: $0 emu|real" >&2
}

case "$MODE" in
    emu|real) ;;
    *) usage; exit 2 ;;
esac

[[ -x "$PROXY" ]] || {
    echo "FEHLER: $PROXY fehlt. Erst build-can0-rollstuhl-emu.sh starten." >&2
    exit 1
}

ip link show can0 >/dev/null 2>&1 || {
    echo "FEHLER: can0 fehlt." >&2
    exit 1
}

if [[ "$MODE" == "emu" ]]; then
    [[ -x "$CHAIR" ]] || {
        echo "FEHLER: $CHAIR fehlt. Erst build-can0-rollstuhl-emu.sh starten." >&2
        exit 1
    }
    ip link show can1 >/dev/null 2>&1 || {
        echo "FEHLER: can1 fehlt." >&2
        exit 1
    }
fi

proxy_pid=""
chair_pid=""

cleanup()
{
    [[ -n "$chair_pid" ]] && kill "$chair_pid" 2>/dev/null || true
    [[ -n "$proxy_pid" ]] && kill "$proxy_pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

"$PROXY" can0 &
proxy_pid=$!
sleep 0.2

if [[ "$MODE" == "real" ]]; then
    echo "REAL: rnet-can-proxy auf can0; kein rollstuhl.emu"
    wait "$proxy_pid"
    exit $?
fi

echo "EMU: rnet-can-proxy auf can0 + rollstuhl.emu auf can1"
if [[ -f "$REPLAY" ]]; then
    "$CHAIR" can1 "$REPLAY" &
else
    "$CHAIR" can1 &
fi
chair_pid=$!
wait "$chair_pid"
