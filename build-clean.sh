#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$ROOT/run/build-v35"
mkdir -p "$RUN_DIR"
JOBS="${RNET_BUILD_JOBS:-$(nproc)}"

fail() { echo "FEHLER: $*" >&2; exit 1; }

check_log() {
    local log="$1"
    if grep -Ein '(^|[[:space:]])warning:|(^|[[:space:]])error:|CMake Warning|CMake Error|FAILED:' "$log" >/dev/null; then
        echo >&2
        echo "FEHLER: Ausgabe enthält Warnung oder Fehler:" >&2
        grep -Ein '(^|[[:space:]])warning:|(^|[[:space:]])error:|CMake Warning|CMake Error|FAILED:' "$log" >&2 || true
        exit 1
    fi
}

run_checked() {
    local tag="$1"; shift
    local log="$RUN_DIR/${tag}.log"
    echo
    echo "== $tag =="
    : > "$log"
    set +e
    "$@" 2>&1 | tee "$log"
    local rc=${PIPESTATUS[0]}
    set -e
    (( rc == 0 )) || fail "$tag fehlgeschlagen (rc=$rc), Log: $log"
    check_log "$log"
    echo "[OK] $tag: 0 errors, 0 warnings"
}

native_source() {
    local name="$1" p
    for p in "$ROOT/$name" "$ROOT/src/$name" "$ROOT/tools/$name"; do
        if [[ -f "$p/CMakeLists.txt" ]]; then
            printf '%s\n' "$p"
            return 0
        fi
    done
    return 1
}

build_dll() {
    [[ -f "$ROOT/CMakePresets.json" ]] || fail "CMakePresets.json fehlt"
    rm -rf "$ROOT/build/mingw32"
    run_checked dll-configure bash -lc "cd \"$ROOT\" && CXXFLAGS='${CXXFLAGS:-} -Werror' cmake --preset mingw32-release"
    run_checked dll-build bash -lc "cd \"$ROOT\" && cmake --build --preset mingw32-release -j\"$JOBS\""
    [[ -f "$ROOT/build/mingw32/ftd2xx.dll" ]] || fail "ftd2xx.dll wurde nicht erzeugt"
}

build_proxy() {
    local src testbin json out
    src="$(native_source rnet-can-proxy)" || fail "CMake-Source für rnet-can-proxy nicht gefunden"
    [[ -f "$ROOT/external/RNetMsgBroker/CMakeLists.txt" ]] || fail "external/RNetMsgBroker fehlt"
    [[ -f "$ROOT/external/RNetMsgBroker/R-Net.json" ]] || fail "external/RNetMsgBroker/R-Net.json fehlt"

    rm -rf "$ROOT/build/rnet-can-proxy"
    run_checked proxy-configure env CXXFLAGS="${CXXFLAGS:-} -Werror" cmake \
        -S "$src" -B "$ROOT/build/rnet-can-proxy" -G Ninja -DCMAKE_BUILD_TYPE=Release
    run_checked proxy-build cmake --build "$ROOT/build/rnet-can-proxy" -j"$JOBS"

    [[ -x "$ROOT/build/rnet-can-proxy/rnet-can-proxy" ]] || fail "rnet-can-proxy wurde nicht erzeugt"
    [[ -f "$ROOT/build/rnet-can-proxy/R-Net.json" ]] || fail "R-Net.json wurde nicht neben den Proxy kopiert"

    testbin="$ROOT/build/rnet-can-proxy/rnet-proxy-decode-test"
    json="$ROOT/build/rnet-can-proxy/R-Net.json"
    [[ -x "$testbin" ]] || fail "rnet-proxy-decode-test wurde nicht erzeugt"
    out="$($testbin "$json")" || fail "Proxy-Decoder-Selbsttest fehlgeschlagen"
    printf '%s\n' "$out"
    grep -q 'RNetLampControlStatus' <<<"$out" || fail "Proxy-Decoder erkennt RNetLampControlStatus nicht"
    grep -q 'RNET-PROXY-DECODE-V35' <<<"$out" || fail "V35 Decoder-Marker fehlt"
    echo "[OK] Proxy-Decoder-Selbsttest"
}

build_gui() {
    local src
    src="$(native_source rollstuhl.emu)" || fail "CMake-Source für rollstuhl.emu nicht gefunden"
    rm -rf "$ROOT/build/rollstuhl.emu"
    run_checked gui-configure env CXXFLAGS="${CXXFLAGS:-} -Werror" cmake \
        -S "$src" -B "$ROOT/build/rollstuhl.emu" -G Ninja -DCMAKE_BUILD_TYPE=Release
    run_checked gui-build cmake --build "$ROOT/build/rollstuhl.emu" -j"$JOBS"
    [[ -x "$ROOT/build/rollstuhl.emu/rollstuhl.emu" ]] || fail "rollstuhl.emu wurde nicht erzeugt"
}

case "${1:-all}" in
    all) build_dll; build_proxy; build_gui ;;
    dll) build_dll ;;
    proxy) build_proxy ;;
    gui) build_gui ;;
    *) echo "Usage: ./build-clean.sh [all|dll|proxy|gui]" >&2; exit 2 ;;
esac

echo
echo "============================================================"
echo " BUILD V35: 0 errors, 0 warnings"
echo "============================================================"
