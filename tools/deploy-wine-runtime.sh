#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
APP="$HOME/.wine/drive_c/Program Files (x86)/PG Drives Technology/R-net Programmer OEM Generic"

DLL_SRC="${1:-$ROOT/build/mingw32/ftd2xx.dll}"
INI_SRC="$ROOT/examples/ftd2xx-emu.ini.example"

mkdir -p "$APP"

copy_if_newer()
{
    local src="$1"
    local dst="$2"
    local label="$3"

    if [[ ! -f "$src" ]]; then
        echo "[deploy] FEHLER $label: Quelle fehlt: $src" >&2
        return 1
    fi

    if [[ -L "$dst" ]]; then
        rm -f "$dst"
    fi

    if [[ ! -e "$dst" || "$src" -nt "$dst" ]]; then
        cp -f "$src" "$dst"
        echo "[deploy] UPDATE $label"
        echo "         $src"
        echo "      -> $dst"
    else
        echo "[deploy] OK     $label ist aktuell"
        echo "         $dst"
    fi
}

read_device()
{
    local f="$1"
    awk -F= '
        /^[[:space:]]*[#;]/ { next }
        {
            key=$1
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", key)
            if (tolower(key) == "device") {
                value=$2
                gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
                print value
            }
        }
    ' "$f" | tail -n 1
}

copy_if_newer "$DLL_SRC" "$APP/ftd2xx.dll" "DLL"
copy_if_newer "$INI_SRC" "$APP/ftd2xx-emu.ini" "INI"

DEVICE=$(read_device "$INI_SRC")
[[ -n "$DEVICE" ]] || DEVICE="can0"

case "$DEVICE" in
    can[0-9]*)
        echo "[deploy] Device=$DEVICE"
        echo "[deploy] Passiver CAN-Brückenbetrieb; kein lokaler Replay/Chair-Emulator."
        ;;
    *)
        echo "[deploy] FEHLER: V15 erwartet Device=canN, gefunden: '$DEVICE'" >&2
        exit 1
        ;;
esac

echo "[deploy] fertig"
