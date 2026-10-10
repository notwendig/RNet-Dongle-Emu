#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="/tmp/RNet-Dongle-Emu-build-v2"
rm -rf "$LOG_DIR"; mkdir -p "$LOG_DIR"
JOBS="${RNET_BUILD_JOBS:-$(nproc)}"
fail(){ echo "FEHLER: $*" >&2; exit 1; }
check_log(){ local log="$1"; if grep -Ein '(^|[[:space:]])warning:|(^|[[:space:]])error:|CMake Warning|CMake Error|FAILED:' "$log" >/dev/null; then grep -Ein '(^|[[:space:]])warning:|(^|[[:space:]])error:|CMake Warning|CMake Error|FAILED:' "$log" >&2 || true; fail "Warning/Error in $log"; fi; }
run_checked(){ local tag="$1"; shift; local log="$LOG_DIR/${tag}.log"; echo; echo "== $tag =="; set +e; "$@" 2>&1 | tee "$log"; local rc=${PIPESTATUS[0]}; set -e; ((rc==0)) || fail "$tag rc=$rc"; check_log "$log"; echo "[OK] $tag: 0 errors, 0 warnings"; }
native_source(){ local name="$1" p; for p in "$ROOT/$name" "$ROOT/src/$name" "$ROOT/tools/$name"; do [[ -f "$p/CMakeLists.txt" ]] && { printf '%s\n' "$p"; return; }; done; return 1; }
build_dll(){ rm -rf "$ROOT/build/mingw32"; run_checked dll-configure bash -lc "cd \"$ROOT\" && CXXFLAGS='${CXXFLAGS:-} -Werror' cmake --preset mingw32-release"; run_checked dll-build bash -lc "cd \"$ROOT\" && cmake --build --preset mingw32-release -j\"$JOBS\""; [[ -f "$ROOT/build/mingw32/ftd2xx.dll" ]] || fail "ftd2xx.dll fehlt"; }
build_proxy(){ local src testbin json o; src="$(native_source rnet-can-proxy)" || fail "Proxy-Source fehlt"; rm -rf "$ROOT/build/rnet-can-proxy"; run_checked proxy-configure env CXXFLAGS="${CXXFLAGS:-} -Werror" cmake -S "$src" -B "$ROOT/build/rnet-can-proxy" -G Ninja -DCMAKE_BUILD_TYPE=Release; run_checked proxy-build cmake --build "$ROOT/build/rnet-can-proxy" -j"$JOBS"; testbin="$ROOT/build/rnet-can-proxy/rnet-proxy-decode-test"; json="$ROOT/build/rnet-can-proxy/R-Net.json"; [[ -x "$testbin" && -f "$json" ]] || fail "Proxy-Selbsttest/JSON fehlt"; o="$($testbin "$json")" || fail "Proxy-Decoder-Selbsttest"; printf '%s\n' "$o"; grep -q 'RNetLampControlStatus' <<<"$o" || fail "RNetLampControlStatus nicht erkannt"; grep -q 'RNET-PROXY-DECODE-V42' <<<"$o" || fail "V42 Marker fehlt"; }
build_gui(){ local src; src="$(native_source rollstuhl.emu)" || fail "GUI-Source fehlt"; rm -rf "$ROOT/build/rollstuhl.emu"; run_checked gui-configure env CXXFLAGS="${CXXFLAGS:-} -Werror" cmake -S "$src" -B "$ROOT/build/rollstuhl.emu" -G Ninja -DCMAKE_BUILD_TYPE=Release; run_checked gui-build cmake --build "$ROOT/build/rollstuhl.emu" -j"$JOBS"; [[ -x "$ROOT/build/rollstuhl.emu/rollstuhl.emu" ]] || fail "rollstuhl.emu fehlt"; }
case "${1:-all}" in all) build_dll; build_proxy; build_gui;; dll) build_dll;; proxy) build_proxy;; gui) build_gui;; *) echo "Usage: ./build-clean.sh [all|dll|proxy|gui]"; exit 2;; esac
echo; echo "============================================================"; echo " v2.00 RC V42 BUILD: 0 errors, 0 warnings"; echo " Logs: $LOG_DIR"; echo "============================================================"
