#!/usr/bin/env bash
set -euo pipefail

# R-Net launcher V41 / v2.00 release candidate
#   ./start.sh emu     App -> DLL -> can0 <CAN> can1 -> rollstuhl.emu
#   ./start.sh dev     RealRollstuhl -> can0 <CAN> Device.emu_CJSM
#   ./start.sh stop
#   ./start.sh status
#
# V40 fixes stale-project-proxy ownership detection.  /proc/PID/exe is used
# when available; exact argv[0] from /proc/PID/cmdline is the safe fallback.
# This matters when an older process still runs from the same project path.

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
LAUNCHER_MARKER="RNET-LAUNCHER-V41"

PROGRAMMER_DIR="$HOME/.wine/drive_c/Program Files (x86)/PG Drives Technology/R-net Programmer OEM Generic"
PROGRAMMER_EXE="$PROGRAMMER_DIR/RNet Programmer6 OEM.exe"
INI="$PROGRAMMER_DIR/ftd2xx-emu.ini"

MODE_FILE="$RUN_DIR/mode"
PROXY_PID="$RUN_DIR/rnet-can-proxy.pid"
CHAIR_PID="$RUN_DIR/rollstuhl.emu.pid"
PROGRAMMER_PID="$RUN_DIR/programmer.pid"
PROXY_LOG="$RUN_DIR/rnet-can-proxy.log"

usage() {
    cat <<EOF
Usage: ./start.sh {emu|dev|stop|status}

  emu     Logs + build + bin löschen, frisch bauen, dann:
          App -> DLL -> $APP_CAN <CAN> $EMU_CAN -> rollstuhl.emu
  dev     Logs + build + bin löschen, frisch bauen, dann:
          RealRollstuhl -> $DEV_CAN <CAN> Device.emu_CJSM
  stop    laufende R-Net-Prozesse beenden
  status  Modus, CAN und Prozesse anzeigen

Optionale Umgebung:
  RNET_APP_CAN=can0
  RNET_EMU_CAN=can1
  RNET_DEV_CAN=can0
  RNET_BITRATE=125000
  RNET_PROXY_PORT=39000
  RNET_JSON=/path/to/R-Net.json
  RNET_PROXY_DECODE=0
EOF
}

pid_alive() {
    local file="$1" pid
    [[ -s "$file" ]] || return 1
    pid="$(cat "$file" 2>/dev/null || true)"
    [[ "$pid" =~ ^[0-9]+$ ]] || return 1
    kill -0 "$pid" 2>/dev/null
}

kill_one_pid() {
    local pid="$1"
    [[ "$pid" =~ ^[0-9]+$ ]] || return 0
    kill -0 "$pid" 2>/dev/null || return 0
    kill "$pid" 2>/dev/null || true
    for _ in {1..30}; do
        kill -0 "$pid" 2>/dev/null || return 0
        sleep 0.1
    done
    kill -KILL "$pid" 2>/dev/null || true
}

kill_pidfile() {
    local file="$1" pid
    if pid_alive "$file"; then
        pid="$(cat "$file")"
        kill_one_pid "$pid"
    fi
    rm -f "$file"
}

proxy_realpath() {
    readlink -f "$PROXY" 2>/dev/null || printf '%s\n' "$PROXY"
}

canonical_process_path() {
    local pid="$1" candidate="$2" cwd
    [[ -n "$candidate" ]] || return 1

    # argv[0] may be relative to the process cwd.
    if [[ "$candidate" != /* ]]; then
        cwd="$(readlink "/proc/$pid/cwd" 2>/dev/null || true)"
        [[ -n "$cwd" ]] || return 1
        candidate="$cwd/$candidate"
    fi

    readlink -f -- "$candidate" 2>/dev/null || printf '%s\n' "$candidate"
}

process_argv0() {
    local pid="$1" arg0=""
    [[ -r "/proc/$pid/cmdline" ]] || return 1

    # /proc/PID/cmdline is NUL separated.  The first item is argv[0].
    IFS= read -r -d '' arg0 <"/proc/$pid/cmdline" || true
    [[ -n "$arg0" ]] || return 1
    printf '%s\n' "$arg0"
}

is_project_proxy_pid() {
    local pid="$1" exe target arg0 arg0_path
    [[ "$pid" =~ ^[0-9]+$ ]] || return 1
    target="$(proxy_realpath)"

    # Preferred identity: the actual executable mapped by the process.
    exe="$(readlink "/proc/$pid/exe" 2>/dev/null || true)"
    exe="${exe% (deleted)}"
    if [[ -n "$exe" ]]; then
        exe="$(readlink -f -- "$exe" 2>/dev/null || printf '%s\n' "$exe")"
        [[ "$exe" == "$target" ]] && return 0
    fi

    # Fedora can leave an older project proxy alive while the binary was rebuilt.
    # In that case /proc/PID/exe may no longer compare as expected, but argv[0]
    # still names the exact project executable.  Match the complete canonical
    # path only; a same-named proxy elsewhere is NOT treated as ours.
    arg0="$(process_argv0 "$pid" 2>/dev/null || true)"
    if [[ -n "$arg0" ]]; then
        arg0_path="$(canonical_process_path "$pid" "$arg0" 2>/dev/null || true)"
        [[ -n "$arg0_path" && "$arg0_path" == "$target" ]] && return 0
    fi

    return 1
}

project_proxy_pids() {
    local proc pid
    for proc in /proc/[0-9]*; do
        [[ -d "$proc" ]] || continue
        pid="${proc##*/}"
        [[ "$pid" == "$$" ]] && continue
        if is_project_proxy_pid "$pid"; then
            printf '%s\n' "$pid"
        fi
    done
}

kill_stale_project_proxies() {
    local pid found=0
    while IFS= read -r pid; do
        [[ -n "$pid" ]] || continue
        found=1
        echo "[proxy] alter Projekt-Proxy PID $pid -> stop"
        kill_one_pid "$pid"
    done < <(project_proxy_pids)
    if (( found )); then
        sleep 0.1
    fi
}

stop_all() {
    kill_pidfile "$PROGRAMMER_PID"
    kill_pidfile "$CHAIR_PID"
    kill_pidfile "$PROXY_PID"
    # V41: auch alte Projekt-Proxies beseitigen, deren PID-Datei verloren ging.
    kill_stale_project_proxies
    rm -f "$MODE_FILE"
}

clean_generated_tree() {
    echo "[clean] alte Logs löschen"

    # Nur innerhalb des Projekts; .git niemals anfassen.
    find "$ROOT" \
        -path "$ROOT/.git" -prune -o \
        -type f -name '*.log' -print -delete 2>/dev/null || true

    # Die DLL kann ihr Log direkt neben dem Windows-Programmer ablegen.
    # Auch diese Laufzeitlogs gehören zu einem frischen Testlauf.
    if [[ -d "$PROGRAMMER_DIR" ]]; then
        find "$PROGRAMMER_DIR" -maxdepth 1 -type f -name '*.log' \
            -print -delete 2>/dev/null || true
    fi

    echo "[clean] $ROOT/build löschen"
    rm -rf -- "$ROOT/build"

    echo "[clean] $ROOT/bin löschen"
    rm -rf -- "$ROOT/bin"

    # run/ selbst bleibt bestehen, damit PID-/Logpfade stabil bleiben.
    mkdir -p "$RUN_DIR"
}

fresh_build() {
    local mode="$1"
    [[ -x "$ROOT/build-clean.sh" ]] || {
        echo "FEHLER: build-clean.sh fehlt: $ROOT/build-clean.sh" >&2
        return 1
    }

    case "$mode" in
        emu)
            echo "[build] kompletter Clean-Build: DLL + Proxy + GUI"
            "$ROOT/build-clean.sh" all
            ;;
        dev)
            echo "[build] Clean-Build: GUI"
            "$ROOT/build-clean.sh" gui
            ;;
        *)
            echo "FEHLER: unbekannter Build-Modus: $mode" >&2
            return 1
            ;;
    esac
}

# `ss -H -lun` auf Fedora: Spalte 4 ist Local Address:Port.
port_busy() {
    ss -H -lun 2>/dev/null | awk -v port="$PROXY_PORT" '
        {
            local_ep=$4
            sub(/^.*:/, "", local_ep)
            if (local_ep == port) found=1
        }
        END { exit(found ? 0 : 1) }
    '
}

port_pids() {
    if command -v fuser >/dev/null 2>&1; then
        fuser -n udp "$PROXY_PORT" 2>/dev/null \
            | tr ' ' '\n' | grep -E '^[0-9]+$' | sort -u || true
        return
    fi
    ss -H -lunp 2>/dev/null \
        | awk -v port="$PROXY_PORT" '
            {
                local_ep=$4
                sub(/^.*:/, "", local_ep)
                if (local_ep == port) print
            }
        ' \
        | grep -oE 'pid=[0-9]+' | cut -d= -f2 | sort -u || true
}

show_port_owner() {
    echo "[proxy] UDP $PROXY_PORT Diagnose:" >&2
    ss -H -lunp 2>/dev/null \
        | awk -v port="$PROXY_PORT" '
            {
                local_ep=$4
                sub(/^.*:/, "", local_ep)
                if (local_ep == port) print
            }
        ' >&2 || true
    if command -v fuser >/dev/null 2>&1; then
        fuser -v -n udp "$PROXY_PORT" >&2 2>/dev/null || true
    fi
}

clear_proxy_port() {
    local pid pids=()

    # Erst alle eindeutig zu DIESEM Projektbinary gehörenden Altprozesse weg.
    kill_stale_project_proxies

    for _ in {1..20}; do
        port_busy || return 0
        sleep 0.1
    done

    mapfile -t pids < <(port_pids)
    if (( ${#pids[@]} == 0 )); then
        echo "FEHLER: UDP $PROXY_PORT ist belegt; Besitzer konnte nicht ermittelt werden." >&2
        show_port_owner
        return 1
    fi

    for pid in "${pids[@]}"; do
        [[ "$pid" =~ ^[0-9]+$ ]] || continue
        if is_project_proxy_pid "$pid"; then
            echo "[proxy] stale Projekt-Proxy PID $pid auf UDP $PROXY_PORT -> stop"
            kill_one_pid "$pid"
        else
            echo "FEHLER: UDP $PROXY_PORT gehört einem fremden Prozess PID $pid" >&2
            tr '\0' ' ' <"/proc/$pid/cmdline" >&2 2>/dev/null || true
            echo >&2
            show_port_owner
            return 1
        fi
    done

    for _ in {1..50}; do
        port_busy || return 0
        sleep 0.1
    done
    echo "FEHLER: UDP $PROXY_PORT bleibt nach Stop des alten Projekt-Proxys belegt." >&2
    show_port_owner
    return 1
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
    python3 - "$INI" "$dev" <<'PYINI'
from pathlib import Path
import re, sys
p = Path(sys.argv[1]); dev = sys.argv[2]
text = p.read_text(encoding="utf-8") if p.exists() else ""
lines = text.splitlines(); section = None; found_section = False; found_device = False; out = []
for line in lines:
    m = re.match(r"\s*\[([^]]+)\]\s*$", line)
    if m:
        if section == "emulator" and not found_device:
            out.append(f"Device={dev}"); found_device = True
        section = m.group(1).strip().lower()
        if section == "emulator": found_section = True
        out.append(line); continue
    if section == "emulator" and re.match(r"\s*Device\s*=", line, re.I):
        if not found_device:
            out.append(f"Device={dev}"); found_device = True
        continue
    out.append(line)
if not found_section:
    if out and out[-1].strip(): out.append("")
    out.extend(["[emulator]", f"Device={dev}"])
elif section == "emulator" and not found_device:
    out.append(f"Device={dev}")
p.write_text("\n".join(out).rstrip() + "\n", encoding="utf-8")
print(f"[INI] Device={dev}: {p}")
PYINI
}

pid_owns_proxy_port() {
    local want="$1" p
    while IFS= read -r p; do
        [[ "$p" == "$want" ]] && return 0
    done < <(port_pids)
    return 1
}

wait_new_proxy() {
    local pid="$1" owners=()
    for _ in {1..80}; do
        if ! kill -0 "$pid" 2>/dev/null; then
            echo "FEHLER: der neu gestartete rnet-can-proxy PID $pid ist beendet." >&2
            tail -n 40 "$PROXY_LOG" >&2 || true
            return 1
        fi

        if grep -Fq 'bind(UDP):' "$PROXY_LOG" 2>/dev/null; then
            echo "FEHLER: Proxy meldet bind(UDP)-Fehler." >&2
            tail -n 40 "$PROXY_LOG" >&2 || true
            return 1
        fi

        if port_busy \
           && grep -Fq 'RNET-PROXY-DECODE-V39' "$PROXY_LOG" 2>/dev/null \
           && grep -Fq "rnet-can-proxy: $APP_CAN <-> 127.0.0.1:$PROXY_PORT" "$PROXY_LOG" 2>/dev/null; then
            mapfile -t owners < <(port_pids)
            if (( ${#owners[@]} == 0 )); then
                # Gleicher Benutzer, Port war direkt vor exec frei und PID lebt:
                # falls die Plattform keine Prozessinfos in ss/fuser liefert, ist
                # diese Kombination dennoch eindeutig genug.
                echo "[proxy] PID $pid online auf UDP $PROXY_PORT (Owner-PID nicht sichtbar)"
                return 0
            fi
            if pid_owns_proxy_port "$pid"; then
                echo "[proxy] PID $pid besitzt UDP $PROXY_PORT"
                return 0
            fi
            echo "FEHLER: UDP $PROXY_PORT gehört nicht dem neu gestarteten Proxy PID $pid." >&2
            show_port_owner
            return 1
        fi
        sleep 0.1
    done
    echo "FEHLER: neuer Proxy wurde nicht eindeutig online erkannt." >&2
    tail -n 40 "$PROXY_LOG" >&2 || true
    show_port_owner
    return 1
}

start_emu() {
    stop_all
    clean_generated_tree
    fresh_build emu

    ensure_binary "$PROXY" proxy
    ensure_binary "$CHAIR" gui

    clear_proxy_port || return 1
    if port_busy; then
        echo "FEHLER: UDP $PROXY_PORT ist unmittelbar vor Proxy-Start noch belegt." >&2
        show_port_owner
        return 1
    fi

    setup_can "$APP_CAN"
    setup_can "$EMU_CAN"
    set_ini_device "$APP_CAN"

    : > "$PROXY_LOG"
    nohup env RNET_PROXY_CAN="$APP_CAN" RNET_PROXY_PORT="$PROXY_PORT" \
        "$PROXY" "$APP_CAN" "$PROXY_PORT" >"$PROXY_LOG" 2>&1 &
    local proxy_pid=$!
    echo "$proxy_pid" > "$PROXY_PID"

    if ! wait_new_proxy "$proxy_pid"; then
        stop_all
        return 1
    fi

    echo emu > "$MODE_FILE"

    : > "$RUN_DIR/rollstuhl.emu.log"
    nohup env RNET_EMU_CAN="$EMU_CAN" "$CHAIR" emu \
        >"$RUN_DIR/rollstuhl.emu.log" 2>&1 &
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

    echo "$LAUNCHER_MARKER"
    echo "Mode: emu"
    echo "Pfad: App -> DLL -> $APP_CAN <CAN> $EMU_CAN -> rollstuhl.emu"
    echo "Proxy PID: $proxy_pid"
    echo "Log:  $PROXY_LOG"
    echo "Launcher: RNET-LAUNCHER-V41"
    echo "Decode: RNetMsgBroker V39 + passive CAN monitor"
}

start_dev() {
    stop_all
    clean_generated_tree
    fresh_build dev

    ensure_binary "$CHAIR" gui
    clear_proxy_port || return 1

    setup_can "$DEV_CAN"
    echo dev > "$MODE_FILE"

    : > "$RUN_DIR/device.emu_CJSM.log"
    nohup env RNET_DEV_CAN="$DEV_CAN" "$CHAIR" dev \
        >"$RUN_DIR/device.emu_CJSM.log" 2>&1 &
    echo $! > "$CHAIR_PID"

    echo "Mode: dev"
    echo "Pfad: RealRollstuhl -> $DEV_CAN <CAN> Device.emu_CJSM"
    echo "Programmer/DLL/Replay/Proxy: nicht gestartet"
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
        echo "CAN $dev: nicht vorhanden"; return
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
            echo "Proxy-Log: $PROXY_LOG"
            echo "Launcher: RNET-LAUNCHER-V41"
            echo "Decode: RNetMsgBroker V39"
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
