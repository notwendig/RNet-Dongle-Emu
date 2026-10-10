#!/usr/bin/env bash
set -Eeuo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
APP="${RNET_APP_DIR:-$HOME/.wine/drive_c/Program Files (x86)/PG Drives Technology/R-net Programmer OEM Generic}"

DLL_SRC="${1:-$ROOT/build/mingw32/ftd2xx.dll}"
INI_SRC="$ROOT/examples/ftd2xx-emu.ini.example"

mkdir -p "$APP"

copy_if_newer() {
    local src="$1"
    local dst="$2"
    local label="$3"

    [[ -f "$src" ]] || {
        echo "[deploy] FEHLER $label: Quelle fehlt: $src" >&2
        return 1
    }

    [[ -L "$dst" ]] && rm -f "$dst"

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

read_device() {
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
    ' "$1" | tail -n1
}

copy_if_newer "$DLL_SRC" "$APP/ftd2xx.dll" "DLL"
copy_if_newer "$INI_SRC" "$APP/ftd2xx-emu.ini" "INI"

DEVICE="$(read_device "$INI_SRC")"
[[ -n "$DEVICE" ]] || DEVICE="can0"

case "$DEVICE" in
    can[0-9]*)
        echo "[deploy] RNET-CAN-ONLY-V21 Device=$DEVICE"
        echo "[deploy] Kein Replay/Chair-Backend in der DLL; CAN-Gegenstelle ist extern."
        ;;
    *)
        echo "[deploy] FEHLER: CAN-only DLL erwartet Device=canN, gefunden: '$DEVICE'" >&2
        exit 1
        ;;
esac

echo "[deploy] fertig"
