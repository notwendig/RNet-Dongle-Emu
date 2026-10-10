#!/usr/bin/env bash
set -Eeuo pipefail

# RNET-CAN-ONLY-V21
# Default emulator topology:
#   R-Net Programmer -> ftd2xx.dll -> rnet-can-proxy -> can0 <-> can1 -> rollstuhl.emu
#
# Both CAN interfaces are configured here.  The DLL itself has no local chair
# emulator anymore; it accepts Device=canN only.

ROOT="${RNET_ROOT:-$HOME/Projects/RNet-Dongle-Emu}"
APP="${RNET_APP_DIR:-$HOME/.wine/drive_c/Program Files (x86)/PG Drives Technology/R-net Programmer OEM Generic}"

CAN_DONGLE="${RNET_CAN_DONGLE:-can0}"
CAN_CHAIR="${RNET_CAN_CHAIR:-can1}"
CAN_BITRATE="${RNET_CAN_BITRATE:-125000}"
CAN_RESTART_MS="${RNET_CAN_RESTART_MS:-100}"
CAN_TXQLEN="${RNET_CAN_TXQLEN:-1000}"
CAN_SETUP="${RNET_CAN_SETUP:-1}"

BUILD_SCRIPT="$ROOT/scripts/build-can0-rollstuhl-emu.sh"
DEPLOY_SCRIPT="$ROOT/tools/deploy-wine-runtime.sh"
DLL="$ROOT/build/mingw32/ftd2xx.dll"
PROXY="$ROOT/build/rnet-can-proxy/rnet-can-proxy"
CHAIR="$ROOT/build/rollstuhl.emu/rollstuhl.emu"
PROGRAMMER="$APP/RNet Programmer6 OEM.exe"
INI="$APP/ftd2xx-emu.ini"

RUNDIR="$ROOT/run"
PROXY_PID="$RUNDIR/rnet-can-proxy.pid"
CHAIR_PID="$RUNDIR/rollstuhl.emu.pid"
PROGRAMMER_PID="$RUNDIR/rnet-programmer.pid"
PROXY_LOG="$RUNDIR/rnet-can-proxy.log"
CHAIR_LOG="$RUNDIR/rollstuhl.emu.log"
PROGRAMMER_LOG="$RUNDIR/rnet-programmer.log"

MODE="${1:-emu}"

usage() {
    cat <<USAGE
Aufruf: $0 [emu|stop|status]

  emu      CAN-Testumgebung starten (Default)
           Programmer -> DLL -> ${CAN_DONGLE} <-> ${CAN_CHAIR} -> rollstuhl.emu
           ${CAN_DONGLE} und ${CAN_CHAIR} werden auf ${CAN_BITRATE} bit/s eingerichtet.

  stop     Programmer, rnet-can-proxy und rollstuhl.emu beenden.

  status   Prozesse und CAN-Interfaces anzeigen.

Umgebungsvariablen:
  RNET_CAN_DONGLE=can0       DLL/Proxy-Seite
  RNET_CAN_CHAIR=can1        rollstuhl.emu-Seite
  RNET_CAN_BITRATE=125000    R-Net CAN-Bitrate
  RNET_CAN_RESTART_MS=100    automatischer CAN-Neustart
  RNET_CAN_TXQLEN=1000       TX-Queue
  RNET_CAN_SETUP=0           CAN-Konfiguration überspringen
USAGE
}

die() {
    echo "FEHLER: $*" >&2
    exit 1
}

pid_alive() {
    local file="$1"
    [[ -f "$file" ]] || return 1
    local pid
    pid="$(cat "$file" 2>/dev/null || true)"
    [[ "$pid" =~ ^[0-9]+$ ]] || return 1
    kill -0 "$pid" 2>/dev/null
}

stop_pid_file() {
    local file="$1"
    local label="$2"

    if ! pid_alive "$file"; then
        rm -f "$file"
        return 0
    fi

    local pid
    pid="$(cat "$file")"
    echo "Stoppe $label, PID $pid ..."
    kill "$pid" 2>/dev/null || true

    for _ in {1..30}; do
        if ! kill -0 "$pid" 2>/dev/null; then
            rm -f "$file"
            return 0
        fi
        sleep 0.1
    done

    kill -KILL "$pid" 2>/dev/null || true
    rm -f "$file"
}

as_root() {
    if (( EUID == 0 )); then
        "$@"
        return
    fi

    command -v sudo >/dev/null 2>&1 || \
        die "Für das CAN-Setup werden root-Rechte benötigt; sudo fehlt."
    sudo "$@"
}

can_index() {
    local iface="$1"
    if [[ "$iface" =~ ^can([0-9]+)$ ]]; then
        printf '%s\n' "${BASH_REMATCH[1]}"
        return 0
    fi
    return 1
}

setup_can() {
    local iface="$1"

    ip link show "$iface" >/dev/null 2>&1 || die "CAN-Interface fehlt: $iface"

    echo "Richte $iface ein: bitrate=$CAN_BITRATE restart-ms=$CAN_RESTART_MS txqueuelen=$CAN_TXQLEN"
    as_root ip link set dev "$iface" down 2>/dev/null || true

    if ! as_root ip link set dev "$iface" type can \
            bitrate "$CAN_BITRATE" restart-ms "$CAN_RESTART_MS"; then
        echo "Hinweis: restart-ms wird vom Treiber nicht akzeptiert; versuche ohne restart-ms."
        as_root ip link set dev "$iface" type can bitrate "$CAN_BITRATE"
    fi

    as_root ip link set dev "$iface" txqueuelen "$CAN_TXQLEN"
    as_root ip link set dev "$iface" up
    ip -details link show "$iface" | sed -n '1,4p'
}

set_runtime_device() {
    local iface="$1"
    [[ -f "$INI" ]] || die "INI fehlt: $INI"

    local tmp
    tmp="$(mktemp)"
    awk -v dev="$iface" '
        BEGIN { in_emulator=0; section_seen=0; device_written=0 }
        /^\[emulator\][[:space:]]*$/ {
            if (in_emulator && !device_written) print "Device=" dev
            print
            in_emulator=1
            section_seen=1
            device_written=0
            next
        }
        /^\[[^]]+\][[:space:]]*$/ {
            if (in_emulator && !device_written) print "Device=" dev
            print
            in_emulator=0
            next
        }
        {
            if (in_emulator && $0 ~ /^[[:space:]]*Device[[:space:]]*=/) {
                if (!device_written) {
                    print "Device=" dev
                    device_written=1
                }
                next
            }
            print
        }
        END {
            if (in_emulator && !device_written) print "Device=" dev
            if (!section_seen) {
                print ""
                print "[emulator]"
                print "Device=" dev
            }
        }
    ' "$INI" > "$tmp"
    mv -f "$tmp" "$INI"
}

show_status() {
    echo "== Prozesse =="
    for entry in \
        "$PROXY_PID:rnet-can-proxy" \
        "$CHAIR_PID:rollstuhl.emu" \
        "$PROGRAMMER_PID:R-Net-Programmer"
    do
        local f="${entry%%:*}"
        local label="${entry#*:}"
        if pid_alive "$f"; then
            echo "RUN  $label PID $(cat "$f")"
        else
            echo "STOP $label"
        fi
    done

    echo
    echo "== CAN =="
    ip -details link show "$CAN_DONGLE" 2>/dev/null | sed -n '1,4p' || true
    ip -details link show "$CAN_CHAIR" 2>/dev/null | sed -n '1,4p' || true

    echo
    echo "== Runtime =="
    [[ -f "$INI" ]] && grep -E '^[[:space:]]*Device[[:space:]]*=' "$INI" || true
}

case "$MODE" in
    -h|--help|help)
        usage
        exit 0
        ;;
    stop)
        mkdir -p "$RUNDIR"
        stop_pid_file "$PROGRAMMER_PID" "R-Net Programmer"
        stop_pid_file "$CHAIR_PID" "rollstuhl.emu"
        stop_pid_file "$PROXY_PID" "rnet-can-proxy"
        echo "R-Net CAN-Testumgebung gestoppt."
        exit 0
        ;;
    status)
        mkdir -p "$RUNDIR"
        show_status
        exit 0
        ;;
    emu)
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac

[[ -d "$ROOT" ]] || die "Projekt fehlt: $ROOT"
[[ "$CAN_DONGLE" != "$CAN_CHAIR" ]] || die "CAN_DONGLE und CAN_CHAIR müssen verschieden sein."
DONGLE_INDEX="$(can_index "$CAN_DONGLE")" || die "DLL unterstützt nur Device=canN; ungültig: $CAN_DONGLE"
PROXY_PORT=$((39000 + DONGLE_INDEX))
(( PROXY_PORT <= 65535 )) || die "CAN-Index ist zu groß: $CAN_DONGLE"

cd "$ROOT"
mkdir -p "$RUNDIR"

command -v ip >/dev/null 2>&1 || die "ip fehlt"
command -v ss >/dev/null 2>&1 || die "ss fehlt"
command -v wine >/dev/null 2>&1 || die "wine fehlt"
[[ -f "$PROGRAMMER" ]] || die "R-Net Programmer fehlt: $PROGRAMMER"

printf '%s\n' \
    "============================================================" \
    " R-Net CAN-EMU / RNET-CAN-ONLY-V21" \
    " Programmer -> FTD2XX -> $CAN_DONGLE <-> $CAN_CHAIR -> rollstuhl.emu" \
    " CAN: $CAN_BITRATE bit/s" \
    "============================================================"

echo
echo "== 0. Alte Instanz beenden =="
stop_pid_file "$PROGRAMMER_PID" "R-Net Programmer"
stop_pid_file "$CHAIR_PID" "rollstuhl.emu"
stop_pid_file "$PROXY_PID" "rnet-can-proxy"
sleep 0.2

echo
echo "== 1. CAN-Interfaces einrichten =="
if [[ "$CAN_SETUP" == "1" ]]; then
    setup_can "$CAN_DONGLE"
    setup_can "$CAN_CHAIR"
else
    echo "RNET_CAN_SETUP=$CAN_SETUP: CAN-Konfiguration wird übersprungen."
    ip -details link show "$CAN_DONGLE" | sed -n '1,4p'
    ip -details link show "$CAN_CHAIR" | sed -n '1,4p'
fi

echo
echo "== 2. Komponenten prüfen / bauen =="
need_build=0
[[ -f "$DLL" ]] || need_build=1
[[ -x "$PROXY" ]] || need_build=1
[[ -x "$CHAIR" ]] || need_build=1

if (( need_build )) || [[ "${RNET_FORCE_BUILD:-0}" == "1" ]]; then
    [[ -f "$BUILD_SCRIPT" ]] || die "Buildskript fehlt: $BUILD_SCRIPT"
    bash "$BUILD_SCRIPT" release emu
else
    echo "OK: DLL, Proxy und rollstuhl.emu sind vorhanden."
fi

[[ -f "$DLL" ]] || die "DLL fehlt: $DLL"
[[ -x "$PROXY" ]] || die "Proxy fehlt: $PROXY"
[[ -x "$CHAIR" ]] || die "rollstuhl.emu fehlt: $CHAIR"

echo
echo "== 3. CAN-only DLL deployen =="
if [[ -x "$DEPLOY_SCRIPT" || -f "$DEPLOY_SCRIPT" ]]; then
    bash "$DEPLOY_SCRIPT" "$DLL"
else
    mkdir -p "$APP"
    cp -f "$DLL" "$APP/ftd2xx.dll"
    cp -f "$ROOT/ftd2xx-emu.ini" "$INI"
fi
set_runtime_device "$CAN_DONGLE"
echo "Runtime: $(grep -E '^[[:space:]]*Device[[:space:]]*=' "$INI" | tail -n1)"

echo
echo "== 4. rnet-can-proxy starten =="
rm -f "$PROXY_PID"
"$PROXY" "$CAN_DONGLE" >"$PROXY_LOG" 2>&1 &
echo $! > "$PROXY_PID"
echo "PID $(cat "$PROXY_PID"), Log: $PROXY_LOG"

proxy_ok=0
for _ in {1..80}; do
    if ss -lun 2>/dev/null | grep -Eq ":${PROXY_PORT}[[:space:]]"; then
        proxy_ok=1
        break
    fi
    pid_alive "$PROXY_PID" || break
    sleep 0.1
done

if (( ! proxy_ok )); then
    tail -n 80 "$PROXY_LOG" 2>/dev/null || true
    die "rnet-can-proxy hört nicht auf UDP-Port $PROXY_PORT"
fi
echo "OK: UDP 127.0.0.1:$PROXY_PORT"

echo
echo "== 5. rollstuhl.emu auf $CAN_CHAIR starten =="
rm -f "$CHAIR_PID"
REPLAY=""
for candidate in "$ROOT/rnet-replay.txt" "$ROOT/examples/rnet-replay.txt"; do
    if [[ -f "$candidate" ]]; then
        REPLAY="$candidate"
        break
    fi
done

if [[ -n "$REPLAY" ]]; then
    "$CHAIR" "$CAN_CHAIR" "$REPLAY" >"$CHAIR_LOG" 2>&1 &
else
    "$CHAIR" "$CAN_CHAIR" >"$CHAIR_LOG" 2>&1 &
fi
echo $! > "$CHAIR_PID"
sleep 0.4
pid_alive "$CHAIR_PID" || {
    tail -n 80 "$CHAIR_LOG" 2>/dev/null || true
    die "rollstuhl.emu ist beim Start beendet worden"
}
echo "PID $(cat "$CHAIR_PID"), Log: $CHAIR_LOG"

echo
echo "== 6. R-Net Programmer starten =="
rm -f "$PROGRAMMER_PID"
(
    cd "$APP"
    exec wine "RNet Programmer6 OEM.exe"
) >"$PROGRAMMER_LOG" 2>&1 &
echo $! > "$PROGRAMMER_PID"
echo "PID $(cat "$PROGRAMMER_PID"), Log: $PROGRAMMER_LOG"

echo
printf '%s\n' \
    "============================================================" \
    " gestartet" \
    " DLL/Proxy : $CAN_DONGLE" \
    " Chair GUI : $CAN_CHAIR" \
    " Bitrate   : $CAN_BITRATE" \
    " Proxy     : UDP 127.0.0.1:$PROXY_PORT" \
    "============================================================"
