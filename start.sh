#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="${RNET_ROOT:-$HOME/Projects/RNet-Dongle-Emu}"
APP="$HOME/.wine/drive_c/Program Files (x86)/PG Drives Technology/R-net Programmer OEM Generic"

BUILD_SCRIPT="$ROOT/scripts/build-can0-rollstuhl-emu.sh"
RUN_SCRIPT="$ROOT/scripts/run-can0-rollstuhl-emu.sh"
DEPLOY_SCRIPT="$ROOT/tools/deploy-wine-runtime.sh"

DLL="$ROOT/build/mingw32/ftd2xx.dll"
PROXY="$ROOT/build/rnet-can-proxy/rnet-can-proxy"
CHAIR="$ROOT/build/rollstuhl.emu/rollstuhl.emu"
INI="$APP/ftd2xx-emu.ini"
PROGRAMMER="$APP/RNet Programmer6 OEM.exe"

RUNDIR="$ROOT/run"
BACKEND_PID="$RUNDIR/can0-backend.pid"
PROGRAMMER_PID="$RUNDIR/rnet-programmer.pid"
PROGRAMMER_LOG="$RUNDIR/rnet-programmer.log"
PROXY_PORT=39000

MODE="${1:-emu}"

usage()
{
    cat >&2 <<USAGE
Aufruf: $0 emu|real|stop

  emu   Programmer -> FTD2XX -> can0 <-> can1 -> rollstuhl.emu
        rollstuhl.emu startet AUS und erzeugt erst nach "Rollstuhl EIN" CAN-Traffic.

  real  Programmer -> FTD2XX -> can0 -> echter Rollstuhl
        rollstuhl.emu wird nicht gestartet.

  stop  Programmer, Proxy und rollstuhl.emu beenden.
USAGE
}

die()
{
    echo "FEHLER: $*" >&2
    exit 1
}

pid_alive()
{
    local file="$1"
    [[ -f "$file" ]] || return 1

    local pid
    pid="$(cat "$file" 2>/dev/null || true)"
    [[ "$pid" =~ ^[0-9]+$ ]] || return 1
    kill -0 "$pid" 2>/dev/null
}

stop_pid_file()
{
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

stop_exact_project_processes()
{
    pkill -TERM -f "^${PROXY//./\\.}([[:space:]]|$)" 2>/dev/null || true
    pkill -TERM -f "^${CHAIR//./\\.}([[:space:]]|$)" 2>/dev/null || true
}

set_device_can0()
{
    [[ -f "$INI" ]] || die "INI fehlt: $INI"

    local tmp
    tmp="$(mktemp)"

    awk '
        BEGIN {
            in_emulator = 0
            emulator_seen = 0
            device_written = 0
        }

        /^\[emulator\][[:space:]]*$/ {
            if (in_emulator && !device_written) {
                print "Device=can0"
                device_written = 1
            }
            print
            in_emulator = 1
            emulator_seen = 1
            next
        }

        /^\[[^]]+\][[:space:]]*$/ {
            if (in_emulator && !device_written) {
                print "Device=can0"
                device_written = 1
            }
            print
            in_emulator = 0
            next
        }

        {
            if (in_emulator && $0 ~ /^[[:space:]]*Device[[:space:]]*=/) {
                if (!device_written) {
                    print "Device=can0"
                    device_written = 1
                }
                next
            }
            print
        }

        END {
            if (in_emulator && !device_written)
                print "Device=can0"

            if (!emulator_seen) {
                print ""
                print "[emulator]"
                print "Device=can0"
            }
        }
    ' "$INI" > "$tmp"

    mv -f "$tmp" "$INI"
}

case "$MODE" in
    emu|real)
        ;;
    stop)
        mkdir -p "$RUNDIR"
        stop_pid_file "$PROGRAMMER_PID" "R-Net Programmer"
        stop_pid_file "$BACKEND_PID" "CAN-Backend"
        stop_exact_project_processes
        echo "R-Net Testumgebung gestoppt."
        exit 0
        ;;
    -h|--help|help)
        usage
        exit 0
        ;;
    *)
        usage
        exit 2
        ;;
esac

[[ -d "$ROOT" ]] || die "Projekt fehlt: $ROOT"
cd "$ROOT"
mkdir -p "$RUNDIR"

command -v wine >/dev/null 2>&1 || die "wine wurde nicht gefunden"
command -v ss   >/dev/null 2>&1 || die "ss wurde nicht gefunden"

[[ -f "$RUN_SCRIPT" ]] || die "Startskript fehlt: $RUN_SCRIPT"
[[ -f "$PROGRAMMER" ]] || die "R-Net Programmer fehlt: $PROGRAMMER"

echo "============================================================"
if [[ "$MODE" == "emu" ]]; then
    echo " R-Net EMU"
    echo " Programmer -> FTD2XX -> can0 <-> can1 -> rollstuhl.emu"
    echo " rollstuhl.emu startet AUS"
else
    echo " R-Net REAL"
    echo " Programmer -> FTD2XX -> can0 -> echter Rollstuhl"
fi
echo " DLL: CAN-live-gated Dongle-Status; ohne CAN-RX keine Status-RX"
echo "============================================================"
echo

echo "== 0. Alte Instanz beenden =="
stop_pid_file "$PROGRAMMER_PID" "R-Net Programmer"
stop_pid_file "$BACKEND_PID" "CAN-Backend"
stop_exact_project_processes
sleep 0.2
echo

echo "== 1. CAN-Interfaces prüfen =="
ip link show can0 >/dev/null 2>&1 || die "can0 fehlt"
ip -details link show can0 | sed -n '1,3p'

if [[ "$MODE" == "emu" ]]; then
    ip link show can1 >/dev/null 2>&1 || die "can1 fehlt"
    ip -details link show can1 | sed -n '1,3p'
fi
echo

echo "== 2. Komponenten prüfen / bauen =="
need_build=0
[[ -f "$DLL" ]] || need_build=1
[[ -x "$PROXY" ]] || need_build=1
if [[ "$MODE" == "emu" && ! -x "$CHAIR" ]]; then
    need_build=1
fi

if (( need_build )); then
    [[ -f "$BUILD_SCRIPT" ]] || die "Buildskript fehlt: $BUILD_SCRIPT"
    bash "$BUILD_SCRIPT" release "$MODE"
else
    echo "OK: benötigte Komponenten vorhanden."
fi

[[ -f "$DLL" ]] || die "DLL fehlt: $DLL"
[[ -x "$PROXY" ]] || die "Proxy fehlt: $PROXY"
if [[ "$MODE" == "emu" ]]; then
    [[ -x "$CHAIR" ]] || die "rollstuhl.emu fehlt: $CHAIR"
fi

if [[ -f "$DEPLOY_SCRIPT" ]]; then
    bash "$DEPLOY_SCRIPT" "$DLL"
else
    cp -f "$DLL" "$APP/ftd2xx.dll"
fi

echo

echo "== 3. FTD2XX auf Device=can0 stellen =="
set_device_can0
grep -E '^[[:space:]]*Device[[:space:]]*=' "$INI" || true
echo

echo "== 4. CAN-Backend starten: $MODE =="
BACKEND_LOG="$RUNDIR/can0-${MODE}.log"
rm -f "$BACKEND_PID"
(
    cd "$ROOT"
    exec bash "$RUN_SCRIPT" "$MODE"
) >"$BACKEND_LOG" 2>&1 &
echo $! > "$BACKEND_PID"
echo "Backend gestartet, PID $(cat "$BACKEND_PID")"
echo "Log: $BACKEND_LOG"
echo

echo "== 5. Proxy auf UDP 127.0.0.1:$PROXY_PORT prüfen =="
proxy_ok=0
for _ in {1..80}; do
    if ss -lun 2>/dev/null |
        grep -Eq "127\\.0\\.0\\.1:${PROXY_PORT}|0\\.0\\.0\\.0:${PROXY_PORT}|\\[::\\]:${PROXY_PORT}"
    then
        proxy_ok=1
        break
    fi

    if ! pid_alive "$BACKEND_PID"; then
        break
    fi
    sleep 0.1
done

if (( proxy_ok )); then
    echo "OK: rnet-can-proxy hört auf Port $PROXY_PORT."
else
    echo "WARNUNG: Port $PROXY_PORT ist nicht sichtbar."
    tail -n 60 "$BACKEND_LOG" 2>/dev/null || true
fi
echo

echo "== 6. R-Net Programmer unter Wine starten =="
rm -f "$PROGRAMMER_PID"
(
    cd "$APP"
    exec wine "RNet Programmer6 OEM.exe"
) >"$PROGRAMMER_LOG" 2>&1 &
echo $! > "$PROGRAMMER_PID"
echo "Programmer gestartet, PID $(cat "$PROGRAMMER_PID")"
echo "Log: $PROGRAMMER_LOG"
echo

echo "============================================================"
echo " gestartet: $MODE"
echo " Backend : $BACKEND_LOG"
echo " Wine    : $PROGRAMMER_LOG"
echo " Device  : can0"
if [[ "$MODE" == "emu" ]]; then
    echo " Chair   : rollstuhl.emu auf can1, initial AUS"
else
    echo " Chair   : REAL auf can0"
fi
echo "============================================================"
