#!/usr/bin/env bash
set -euo pipefail

# R-Net launcher V30
#   ./start.sh emu     App -> DLL -> can0 <CAN> can1 -> rollstuhl.emu
#   ./start.sh dev     RealRollstuhl -> can0 <CAN> Device.emu_CJSM
#   ./start.sh stop
#   ./start.sh status

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$ROOT/run"
mkdir -p "$RUN_DIR"

APP_CAN="${RNET_APP_CAN:-can0}"
EMU_CAN="${RNET_EMU_CAN:-can1}"
DEV_CAN="${RNET_DEV_CAN:-can0}"
BITRATE="${RNET_BITRATE:-125000}"

PROXY="$ROOT/build/rnet-can-proxy/rnet-can-proxy"
CHAIR="$ROOT/build/rollstuhl.emu/rollstuhl.emu"
PROXY_PORT="${RNET_PROXY_PORT:-39000}"

PROGRAMMER_DIR="$HOME/.wine/drive_c/Program Files (x86)/PG Drives Technology/R-net Programmer OEM Generic"
PROGRAMMER_EXE="$PROGRAMMER_DIR/RNet Programmer6 OEM.exe"
INI="$PROGRAMMER_DIR/ftd2xx-emu.ini"

MODE_FILE="$RUN_DIR/mode"
PROXY_PID="$RUN_DIR/rnet-can-proxy.pid"
CHAIR_PID="$RUN_DIR/rollstuhl.emu.pid"
PROGRAMMER_PID="$RUN_DIR/programmer.pid"

usage() {
    cat <<EOF
Usage: ./start.sh {emu|dev|stop|status}

  emu     App -> DLL -> $APP_CAN <CAN> $EMU_CAN -> rollstuhl.emu
  dev     RealRollstuhl -> $DEV_CAN <CAN> Device.emu_CJSM
  stop    laufende R-Net-Prozesse beenden
  status  Modus, CAN und Prozesse anzeigen

Optionale Umgebung:
  RNET_APP_CAN=can0
  RNET_EMU_CAN=can1
  RNET_DEV_CAN=can0
  RNET_BITRATE=125000
EOF
}

pid_alive() {
    local file="$1" pid
    [[ -s "$file" ]] || return 1
    pid="$(cat "$file" 2>/dev/null || true)"
    [[ "$pid" =~ ^[0-9]+$ ]] || return 1
    kill -0 "$pid" 2>/dev/null
}

kill_pidfile() {
    local file="$1" pid
    if pid_alive "$file"; then
        pid="$(cat "$file")"
        kill "$pid" 2>/dev/null || true
        for _ in {1..20}; do
            kill -0 "$pid" 2>/dev/null || break
            sleep 0.1
        done
        kill -KILL "$pid" 2>/dev/null || true
    fi
    rm -f "$file"
}

stop_all() {
    kill_pidfile "$PROGRAMMER_PID"
    kill_pidfile "$CHAIR_PID"
    kill_pidfile "$PROXY_PID"
    rm -f "$MODE_FILE"
}

ensure_binary() {
    local bin="$1" target="$2"
    if [[ ! -x "$bin" ]]; then
        echo "[build] fehlt: $bin"
        [[ -x "$ROOT/build-clean.sh" ]] || {
            echo "FEHLER: build-clean.sh fehlt: $ROOT/build-clean.sh" >&2
            return 1
        }
        "$ROOT/build-clean.sh" "$target"
    fi
    [[ -x "$bin" ]] || { echo "FEHLER: fehlt nach Build: $bin" >&2; return 1; }
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
        return
    fi
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
text = p.read_text(encoding="utf-8") if p.exists() else ""
lines = text.splitlines()
section = None
found_section = False
found_device = False
out = []
for line in lines:
    m = re.match(r"\s*\[([^]]+)\]\s*$", line)
    if m:
        if section == "emulator" and not found_device:
            out.append(f"Device={dev}")
            found_device = True
        section = m.group(1).strip().lower()
        if section == "emulator":
            found_section = True
        out.append(line)
        continue
    if section == "emulator" and re.match(r"\s*Device\s*=", line, re.I):
        if not found_device:
            out.append(f"Device={dev}")
            found_device = True
        continue
    out.append(line)
if not found_section:
    if out and out[-1].strip():
        out.append("")
    out.extend(["[emulator]", f"Device={dev}"])
elif section == "emulator" and not found_device:
    out.append(f"Device={dev}")
p.write_text("\n".join(out).rstrip() + "\n", encoding="utf-8")
print(f"[INI] Device={dev}: {p}")
PY
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

start_emu() {
    stop_all
    ensure_binary "$PROXY" proxy
    ensure_binary "$CHAIR" gui
    setup_can "$APP_CAN"
    setup_can "$EMU_CAN"
    set_ini_device "$APP_CAN"
    echo emu > "$MODE_FILE"

    nohup "$PROXY" "$APP_CAN" "$PROXY_PORT" >"$RUN_DIR/rnet-can-proxy.log" 2>&1 &
    echo $! > "$PROXY_PID"
    wait_proxy

    RNET_EMU_CAN="$EMU_CAN" nohup "$CHAIR" emu >"$RUN_DIR/rollstuhl.emu.log" 2>&1 &
    echo $! > "$CHAIR_PID"

    if [[ ! -f "$PROGRAMMER_EXE" ]]; then
        echo "FEHLER: Programmer fehlt: $PROGRAMMER_EXE" >&2
        stop_all
        return 1
    fi
    (
        cd "$PROGRAMMER_DIR"
        nohup wine "$(basename "$PROGRAMMER_EXE")" >"$RUN_DIR/programmer.log" 2>&1 &
        echo $! > "$PROGRAMMER_PID"
    )

    echo "Mode: emu"
    echo "Pfad: App -> DLL -> $APP_CAN <CAN> $EMU_CAN -> rollstuhl.emu"
}

start_dev() {
    stop_all
    ensure_binary "$CHAIR" gui
    setup_can "$DEV_CAN"
    echo dev > "$MODE_FILE"

    RNET_DEV_CAN="$DEV_CAN" nohup "$CHAIR" dev >"$RUN_DIR/device.emu_CJSM.log" 2>&1 &
    echo $! > "$CHAIR_PID"

    echo "Mode: dev"
    echo "Pfad: RealRollstuhl -> $DEV_CAN <CAN> Device.emu_CJSM"
    echo "Programmer/DLL/Replay: nicht gestartet"
}

show_process() {
    local label="$1" file="$2"
    if pid_alive "$file"; then
        echo "$label: running (PID $(cat "$file"))"
    else
        echo "$label: stopped"
    fi
}

show_can() {
    local dev="$1" state bitrate
    if ! ip link show "$dev" >/dev/null 2>&1; then
        echo "CAN $dev: nicht vorhanden"
        return
    fi
    state="$(ip -brief link show "$dev" | awk '{print $2}')"
    bitrate="$(ip -details link show "$dev" | sed -nE 's/.*bitrate ([0-9]+).*/\1/p' | head -n1)"
    echo "CAN $dev: $state, bitrate ${bitrate:-?}"
}

status() {
    local mode=stopped
    [[ -s "$MODE_FILE" ]] && mode="$(cat "$MODE_FILE")"
    if [[ "$mode" == emu ]] && ! pid_alive "$CHAIR_PID" && ! pid_alive "$PROXY_PID" && ! pid_alive "$PROGRAMMER_PID"; then
        mode=stopped
    elif [[ "$mode" == dev ]] && ! pid_alive "$CHAIR_PID"; then
        mode=stopped
    fi

    echo "Mode: $mode"
    case "$mode" in
        emu)
            echo "Pfad: App -> DLL -> $APP_CAN <CAN> $EMU_CAN -> rollstuhl.emu"
            show_can "$APP_CAN"; show_can "$EMU_CAN"
            show_process rnet-can-proxy "$PROXY_PID"
            show_process rollstuhl.emu "$CHAIR_PID"
            show_process R-Net-Programmer "$PROGRAMMER_PID"
            ;;
        dev)
            echo "Pfad: RealRollstuhl -> $DEV_CAN <CAN> Device.emu_CJSM"
            show_can "$DEV_CAN"
            show_process Device.emu_CJSM "$CHAIR_PID"
            ;;
        *)
            show_process rnet-can-proxy "$PROXY_PID"
            show_process rollstuhl.emu "$CHAIR_PID"
            show_process R-Net-Programmer "$PROGRAMMER_PID"
            ;;
    esac
}

case "${1:-}" in
    emu) start_emu ;;
    dev) start_dev ;;
    stop) stop_all; echo "R-Net Emulator gestoppt." ;;
    status) status ;;
    *) usage; exit 2 ;;
esac
