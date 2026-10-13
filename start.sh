#!/usr/bin/env bash
set -Eeuo pipefail

# SINGLE-PROJECT-START-V7
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"

LOGDIR="${RNET_LOGDIR:-$HOME/.rnet.logs}"
RUN_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/RNet-Dongle-Emu/run"

APP_CAN="${RNET_APP_CAN:-can0}"
EMU_CAN="${RNET_EMU_CAN:-can1}"
DEV_CAN="${RNET_DEV_CAN:-can0}"
BITRATE="${RNET_BITRATE:-125000}"
PROXY_PORT="${RNET_PROXY_PORT:-39000}"

PROGRAMMER_DIR="$HOME/.wine/drive_c/Program Files (x86)/PG Drives Technology/R-net Programmer OEM Generic"
PROGRAMMER_EXE="$PROGRAMMER_DIR/RNet Programmer6 OEM.exe"
INI="$PROGRAMMER_DIR/ftd2xx-emu.ini"

PROXY="$ROOT/build/rnet-can-proxy/rnet-can-proxy"
CHAIR="$ROOT/build/rollstuhl.emu/rollstuhl.emu"
DLL="$ROOT/build/mingw32/ftd2xx.dll"

MODE_FILE="$RUN_DIR/mode"
PROXY_PID="$RUN_DIR/rnet-can-proxy.pid"
CHAIR_PID="$RUN_DIR/rollstuhl.emu.pid"
PROGRAMMER_PID="$RUN_DIR/programmer.pid"
CANDUMP_PID="$RUN_DIR/candump.pid"

mkdir -p "$LOGDIR" "$RUN_DIR"

usage() {
    cat <<EOF
Aufruf: ./start.sh {emu|real|stop|status}

  emu   App -> DLL -> $APP_CAN <CAN> $EMU_CAN -> rollstuhl.emu
  real  RealRollstuhl -> $DEV_CAN <CAN> Device.emu_CJSM
  stop  laufende Projektprozesse beenden
  status Status anzeigen
EOF
}

pid_alive() {
    local f="$1" pid
    [[ -s "$f" ]] || return 1
    pid="$(cat "$f" 2>/dev/null || true)"
    [[ "$pid" =~ ^[0-9]+$ ]] || return 1
    kill -0 "$pid" 2>/dev/null
}

kill_pidfile() {
    local label="$1" f="$2" pid
    if ! pid_alive "$f"; then
        rm -f "$f"
        return 0
    fi

    pid="$(cat "$f")"
    echo "[STOP] $label PID $pid"
    kill "$pid" 2>/dev/null || true

    for _ in {1..15}; do
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.1
    done

    kill -KILL "$pid" 2>/dev/null || true
    rm -f "$f"
}

kill_candump() {
    local pid
    if pid_alive "$CANDUMP_PID"; then
        pid="$(cat "$CANDUMP_PID")"
        echo "[STOP] candump PID $pid"
        kill -TERM -- "-$pid" 2>/dev/null || kill "$pid" 2>/dev/null || true
        sleep 0.1
        kill -KILL -- "-$pid" 2>/dev/null || true
    fi
    rm -f "$CANDUMP_PID"
    pkill -f 'candump[[:space:]].*-ca[[:space:]]+can0' 2>/dev/null || true
}

project_running() {
    pid_alive "$PROXY_PID" && return 0
    pid_alive "$CHAIR_PID" && return 0
    pid_alive "$PROGRAMMER_PID" && return 0
    pid_alive "$CANDUMP_PID" && return 0
    return 1
}

poweroff_before_stop() {
    [[ -s "$MODE_FILE" ]] || return 0
    [[ "$(cat "$MODE_FILE" 2>/dev/null || true)" == "emu" ]] || return 0
    pid_alive "$CHAIR_PID" || return 0
    command -v cansend >/dev/null 2>&1 || return 0
    ip link show "$EMU_CAN" >/dev/null 2>&1 || return 0

    echo "[STOP] PowerOff vor Ende: $EMU_CAN 002#R"
    timeout 1s cansend "$EMU_CAN" 002#R >/dev/null 2>&1 || true
    sleep 0.2
}

stop_all() {
    if ! project_running; then
        echo "[STOP] keine Projektprozesse aktiv"
        rm -f "$MODE_FILE"
        return 0
    fi

    poweroff_before_stop

    kill_candump
    kill_pidfile "R-Net Programmer" "$PROGRAMMER_PID"
    kill_pidfile "rollstuhl.emu" "$CHAIR_PID"
    kill_pidfile "rnet-can-proxy" "$PROXY_PID"

    # Reste älterer Läufe; kein Aufruf eines anderen start.sh.
    pkill -TERM -x rnet-can-proxy 2>/dev/null || true
    pkill -TERM -f 'rollstuhl[.]emu([[:space:]]|$)' 2>/dev/null || true
    pkill -TERM -f '[Rr][Nn]et Programmer6 OEM[.]exe' 2>/dev/null || true
    sleep 0.2
    pkill -KILL -x rnet-can-proxy 2>/dev/null || true
    pkill -KILL -f 'rollstuhl[.]emu([[:space:]]|$)' 2>/dev/null || true
    pkill -KILL -f '[Rr][Nn]et Programmer6 OEM[.]exe' 2>/dev/null || true

    rm -f "$MODE_FILE"
    echo "[STOP] fertig"
}

can_ready() {
    local dev="$1" d
    d="$(ip -details link show "$dev" 2>/dev/null || true)"
    grep -q 'state UP' <<<"$d" && grep -q "bitrate $BITRATE" <<<"$d"
}

setup_can() {
    local dev="$1"
    if can_ready "$dev"; then
        echo "[CAN] $dev bereits UP, bitrate $BITRATE"
        return 0
    fi

    ip link show "$dev" >/dev/null 2>&1 || {
        echo "FEHLER: CAN-Interface fehlt: $dev" >&2
        return 1
    }

    echo "[CAN] $dev -> bitrate $BITRATE"
    sudo ip link set "$dev" down 2>/dev/null || true
    sudo ip link set "$dev" type can bitrate "$BITRATE"
    sudo ip link set "$dev" up
}

set_ini_device() {
    local dev="$1"
    mkdir -p "$PROGRAMMER_DIR"

    python3 - "$INI" "$dev" <<'PY'
from pathlib import Path
import re, sys

p = Path(sys.argv[1])
dev = sys.argv[2]
text = p.read_text(encoding="utf-8") if p.exists() else "[emulator]\n"
lines = text.splitlines()
out = []
section = None
seen_section = False
seen_device = False

for line in lines:
    m = re.match(r"\s*\[([^]]+)\]\s*$", line)
    if m:
        if section == "emulator" and not seen_device:
            out.append(f"Device={dev}")
            seen_device = True
        section = m.group(1).strip().lower()
        if section == "emulator":
            seen_section = True
        out.append(line)
        continue

    if section == "emulator" and re.match(r"\s*Device\s*=", line, re.I):
        if not seen_device:
            out.append(f"Device={dev}")
            seen_device = True
        continue

    out.append(line)

if not seen_section:
    if out and out[-1].strip():
        out.append("")
    out.extend(["[emulator]", f"Device={dev}"])
elif section == "emulator" and not seen_device:
    out.append(f"Device={dev}")

p.write_text("\n".join(out).rstrip() + "\n", encoding="utf-8")
print(f"[INI] Device={dev}: {p}")
PY
}

build_all() {
    echo
    echo "============================================================"
    echo "2. Build komplett löschen"
    echo "============================================================"
    rm -rf "$ROOT/build"

    echo
    echo "============================================================"
    echo "3. Windows-DLL bauen"
    echo "============================================================"
    cmake -S "$ROOT" --preset mingw32-release
    (
        cd "$ROOT"
        cmake --build --preset mingw32-release --parallel
    )
    [[ -f "$DLL" ]] || {
        echo "FEHLER: DLL fehlt nach Build: $DLL" >&2
        return 1
    }

    echo
    echo "============================================================"
    echo "4. rnet-can-proxy bauen"
    echo "============================================================"
    cmake \
        -S "$ROOT/tools/rnet-can-proxy" \
        -B "$ROOT/build/rnet-can-proxy" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build "$ROOT/build/rnet-can-proxy" --parallel
    [[ -x "$PROXY" ]] || {
        echo "FEHLER: Proxy fehlt nach Build: $PROXY" >&2
        return 1
    }

    echo
    echo "============================================================"
    echo "5. rollstuhl.emu nativ bauen"
    echo "============================================================"
    cmake \
        -S "$ROOT/rollstuhl.emu" \
        -B "$ROOT/build/rollstuhl.emu" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=/usr/bin/gcc \
        -DCMAKE_CXX_COMPILER=/usr/bin/g++
    cmake --build "$ROOT/build/rollstuhl.emu" --parallel
    [[ -x "$CHAIR" ]] || {
        echo "FEHLER: rollstuhl.emu fehlt nach Build: $CHAIR" >&2
        return 1
    }

    echo
    echo "============================================================"
    echo "6. DLL / INI deployen"
    echo "============================================================"
    mkdir -p "$PROGRAMMER_DIR"
    cp -f "$DLL" "$PROGRAMMER_DIR/ftd2xx.dll"
    cp -f "$ROOT/examples/ftd2xx-emu.ini.example" "$INI"
    echo "[deploy] $DLL -> $PROGRAMMER_DIR/ftd2xx.dll"
}

new_log() {
    local stamp="$1" name="$2" p n
    p="$LOGDIR/${stamp}-${name}"
    if [[ ! -e "$p" ]]; then
        printf '%s\n' "$p"
        return
    fi

    n=1
    while [[ -e "$LOGDIR/${stamp}-${n}-${name}" ]]; do
        n=$((n + 1))
    done
    printf '%s\n' "$LOGDIR/${stamp}-${n}-${name}"
}

start_candump() {
    local stamp="$1" log
    command -v candump >/dev/null 2>&1 || {
        echo "FEHLER: candump fehlt" >&2
        return 1
    }

    log="$(new_log "$stamp" candump.log)"
    echo "[START] candump -tz -ca can0"
    echo "[LOG]   $log"

    setsid bash -c 'candump -tz -ca can0 | tee "$1"' bash "$log" &
    echo $! > "$CANDUMP_PID"
}

wait_proxy() {
    for _ in {1..50}; do
        if ss -H -lun 2>/dev/null | grep -Eq "[:.]${PROXY_PORT}[[:space:]]"; then
            return 0
        fi
        sleep 0.1
    done
    echo "FEHLER: rnet-can-proxy lauscht nicht auf UDP $PROXY_PORT" >&2
    return 1
}

start_emu_runtime() {
    local stamp="$1" proxy_log chair_log programmer_log win_log

    setup_can "$APP_CAN"
    setup_can "$EMU_CAN"
    set_ini_device "$APP_CAN"

    proxy_log="$(new_log "$stamp" rnet-can-proxy.log)"
    chair_log="$(new_log "$stamp" rollstuhl.emu.log)"
    programmer_log="$(new_log "$stamp" programmer.log)"

    start_candump "$stamp"

    nohup "$PROXY" "$APP_CAN" "$PROXY_PORT" >"$proxy_log" 2>&1 &
    echo $! > "$PROXY_PID"
    wait_proxy

    nohup env \
        RNET_STARTOFF_FALLBACK=0 \
        RNET_EMU_CAN="$EMU_CAN" \
        "$CHAIR" emu >"$chair_log" 2>&1 &
    echo $! > "$CHAIR_PID"

    [[ -f "$PROGRAMMER_EXE" ]] || {
        echo "FEHLER: Programmer fehlt: $PROGRAMMER_EXE" >&2
        stop_all
        return 1
    }

    win_log="Z:${LOGDIR//\//\\}\\${stamp}-ftd2xx-emu.log"
    export RNET_LOG_FILE="$win_log"

    (
        cd "$PROGRAMMER_DIR"
        nohup wine "$(basename "$PROGRAMMER_EXE")" >"$programmer_log" 2>&1 &
        echo $! > "$PROGRAMMER_PID"
    )

    echo emu > "$MODE_FILE"
    echo "[OK] emu gestartet"
    echo "Pfad: App -> DLL -> $APP_CAN <CAN> $EMU_CAN -> rollstuhl.emu"
}

start_real_runtime() {
    local stamp="$1" chair_log

    setup_can "$DEV_CAN"
    chair_log="$(new_log "$stamp" device.emu_CJSM.log)"

    start_candump "$stamp"

    nohup env \
        RNET_DEV_CAN="$DEV_CAN" \
        "$CHAIR" dev >"$chair_log" 2>&1 &
    echo $! > "$CHAIR_PID"

    echo real > "$MODE_FILE"
    echo "[OK] real gestartet"
    echo "Pfad: RealRollstuhl -> $DEV_CAN <CAN> Device.emu_CJSM"
    echo "Programmer/DLL/Proxy werden im real-Modus nicht gestartet."
}

show_proc() {
    local label="$1" file="$2"
    if pid_alive "$file"; then
        echo "$label: running (PID $(cat "$file"))"
    else
        echo "$label: stopped"
    fi
}

show_status() {
    local mode="stopped"
    [[ -s "$MODE_FILE" ]] && mode="$(cat "$MODE_FILE")"

    echo "Projekt: $ROOT"
    echo "Mode:    $mode"
    echo "Logs:    $LOGDIR"
    show_proc candump "$CANDUMP_PID"
    show_proc rnet-can-proxy "$PROXY_PID"
    show_proc rollstuhl.emu "$CHAIR_PID"
    show_proc R-Net-Programmer "$PROGRAMMER_PID"
}

fresh_start() {
    local mode="$1" stamp

    reset || true

    echo "Projekt: $ROOT"
    echo
    echo "============================================================"
    echo "1. Stoppen"
    echo "============================================================"
    stop_all

    build_all

    stamp="$(date +%Y%m%d-%H%M%S)"
    export RNET_RUN_STAMP="$stamp"

    echo
    echo "============================================================"
    echo "7. Starten: $mode"
    echo "============================================================"

    case "$mode" in
        emu)  start_emu_runtime "$stamp" ;;
        real) start_real_runtime "$stamp" ;;
    esac

    echo "[LOGS] alte Logs bleiben erhalten: $LOGDIR"
}

case "${1:-}" in
    emu)
        fresh_start emu
        ;;
    real)
        fresh_start real
        ;;
    stop)
        stop_all
        ;;
    status)
        show_status
        ;;
    *)
        usage
        exit 2
        ;;
esac
