#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

BUILD_KIND="${1:-release}"
MODE="${2:-emu}"

case "$BUILD_KIND" in
    release|mingw32-release)
        PRESET=mingw32-release
        ;;
    debug|mingw32-debug)
        PRESET=mingw32-debug
        ;;
    *)
        echo "Aufruf: $0 [release|debug] [emu|real]" >&2
        exit 2
        ;;
esac

case "$MODE" in
    emu|real) ;;
    *)
        echo "Aufruf: $0 [release|debug] [emu|real]" >&2
        exit 2
        ;;
esac

echo "== FTD2XX DLL: $PRESET =="
cmake --preset "$PRESET"
cmake --build --preset "$PRESET" -j"$(nproc)"

echo
echo "== nativer SocketCAN-Proxy =="
cmake -S tools/rnet-can-proxy -B build/rnet-can-proxy -G Ninja
cmake --build build/rnet-can-proxy -j"$(nproc)"

if [[ "$MODE" == "emu" ]]; then
    echo
    echo "== Qt6 rollstuhl.emu =="
    cmake -S rollstuhl.emu -B build/rollstuhl.emu -G Ninja
    cmake --build build/rollstuhl.emu -j"$(nproc)"
else
    echo
    echo "== rollstuhl.emu =="
    echo "REAL-Modus: nicht benötigt, Build übersprungen."
fi

echo
echo "Fertig."
if [[ "$PRESET" == "mingw32-release" ]]; then
    echo "DLL:        $ROOT/build/mingw32/ftd2xx.dll"
else
    echo "DLL:        $ROOT/build/mingw32-debug/ftd2xx.dll"
fi
echo "CAN proxy:  $ROOT/build/rnet-can-proxy/rnet-can-proxy"
if [[ "$MODE" == "emu" ]]; then
    echo "Rollstuhl:  $ROOT/build/rollstuhl.emu/rollstuhl.emu"
fi
