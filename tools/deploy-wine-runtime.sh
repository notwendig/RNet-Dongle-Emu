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
    local value

    value=$(
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
    )

    [[ -n "$value" ]] || value="emu"
    printf '%s\n' "$value"
}

replay_has_tx()
{
    local f="$1"
    [[ -f "$f" ]] && grep -Eq '^[[:space:]]*TX[[:space:]]+[0-9A-Fa-f]+' "$f"
}

copy_if_newer "$DLL_SRC" "$APP/ftd2xx.dll" "DLL"
copy_if_newer "$INI_SRC" "$APP/ftd2xx-emu.ini" "INI"

DEVICE=$(read_device "$INI_SRC")
echo "[deploy] Device=$DEVICE"

case "$DEVICE" in
    can[0-9]*)
        echo "[deploy] CAN-Backend $DEVICE: Replay wird nicht benötigt und nicht kopiert."
        ;;
    emu)
        REPLAY_SRC=""
        for candidate in \
            "$ROOT/examples/rnet-replay.txt" \
            "$ROOT/rnet-replay.txt" \
            "$ROOT/examples/rnet-replay.example.txt"
        do
            if replay_has_tx "$candidate"; then
                REPLAY_SRC="$candidate"
                break
            fi
        done

        if [[ -n "$REPLAY_SRC" ]]; then
            copy_if_newer "$REPLAY_SRC" "$APP/rnet-replay.txt" "Replay"
        elif replay_has_tx "$APP/rnet-replay.txt"; then
            echo "[deploy] OK     vorhandener Replay bleibt erhalten"
            echo "         $APP/rnet-replay.txt"
        else
            echo "[deploy] FEHLER: Device=emu, aber kein gültiger Replay gefunden." >&2
            exit 1
        fi
        ;;
    *)
        echo "[deploy] FEHLER: ungültiges Device='$DEVICE' (erlaubt: emu oder canN)." >&2
        exit 1
        ;;
esac

echo "[deploy] fertig"
