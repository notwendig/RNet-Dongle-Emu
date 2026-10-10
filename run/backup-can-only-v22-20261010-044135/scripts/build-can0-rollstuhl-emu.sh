#!/usr/bin/env bash
set -Eeuo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

BUILD_KIND="${1:-release}"
case "$BUILD_KIND" in
    release|mingw32-release) PRESET=mingw32-release; DLLDIR=mingw32 ;;
    debug|mingw32-debug)     PRESET=mingw32-debug;   DLLDIR=mingw32-debug ;;
    *)
        echo "Aufruf: $0 [release|debug] [emu]" >&2
        exit 2
        ;;
esac

# Zweites Argument bleibt aus Kompatibilitätsgründen erlaubt; es gibt nur noch
# den externen CAN-EMU-Aufbau.
if [[ "${2:-emu}" != "emu" ]]; then
    echo "FEHLER: RNET-CAN-ONLY-V21 unterstützt nur den CAN-basierten emu-Aufbau." >&2
    exit 2
fi

echo "== FTD2XX CAN-only DLL: $PRESET =="
# Function/data sections plus --gc-sections ensure the no longer referenced
# legacy DLL-local replay/chair helpers do not remain in the release DLL.
cmake --preset "$PRESET" \
    -DCMAKE_CXX_FLAGS_RELEASE="${CMAKE_CXX_FLAGS_RELEASE:-} -ffunction-sections -fdata-sections" \
    -DCMAKE_SHARED_LINKER_FLAGS="${CMAKE_SHARED_LINKER_FLAGS:-} -Wl,--gc-sections"
cmake --build --preset "$PRESET" -j"$(nproc)"

echo
echo "== nativer SocketCAN-Proxy =="
cmake -S tools/rnet-can-proxy -B build/rnet-can-proxy -G Ninja
cmake --build build/rnet-can-proxy -j"$(nproc)"

echo
echo "== Qt6 rollstuhl.emu =="
cmake -S rollstuhl.emu -B build/rollstuhl.emu -G Ninja
cmake --build build/rollstuhl.emu -j"$(nproc)"

echo
echo "Fertig."
echo "DLL:        $ROOT/build/$DLLDIR/ftd2xx.dll"
echo "CAN proxy:  $ROOT/build/rnet-can-proxy/rnet-can-proxy"
echo "Rollstuhl:  $ROOT/build/rollstuhl.emu/rollstuhl.emu"
