#!/usr/bin/env bash
set -euo pipefail

usage() {ploy-wine.sh <program-directory> [repository-directory]

Examples:
  scripts/deploy-wine.sh \
    "$HOME/.wine/drive_c/Program Files (x86)/PG Drives Technology/R-net Programmer OEM Generic"

  scripts/deploy-wine.sh \
    "$HOME/.wine/drive_c/Program Files (x86)/PG Drives Technology/R-net Programmer OEM Generic" \
    "$HOME/Projects/RNet-Dongle-Emu-v0.1.0"

Optional environment variable:

    cat >&2 <<'EOF'
Usage:
  scripts/de  RNET_REPLAY_FILE=/path/to/rnet-replay.txt

If repository-directory is omitted, the repository root is derived from
the location of this script.
EOF
    exit 2
}

[[ $# -ge 1 && $# -le 2 ]] || usage

PROGRAM_DIR=$1
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

if [[ $# -eq 2 ]]; then
    REPO_DIR=$(realpath -m -- "$2")
else
    REPO_DIR=$(realpath -m -- "$SCRIPT_DIR/..")
fi

DLL="$REPO_DIR/build/mingw32/ftd2xx.dll"
INI="$REPO_DIR/ftd2xx-emu.ini"
INI_EXAMPLE="$REPO_DIR/examples/ftd2xx-emu.ini.example"
REPLAY="$REPO_DIR/rnet-replay.txt"
MINGW_PTHREAD="/usr/i686-w64-mingw32/sys-root/mingw/bin/libwinpthread-1.dll"

[[ -d "$PROGRAM_DIR" ]] || {
    echo "Directory not found: $PROGRAM_DIR" >&2
    exit 1
}

[[ -d "$REPO_DIR" ]] || {
    echo "Repository directory not found: $REPO_DIR" >&2
    exit 1
}

[[ -f "$DLL" ]] || {
    echo "DLL not found: $DLL" >&2
    echo "Build the mingw32 preset/target first." >&2
    exit 1
}

# The public repository contains only an example INI.  Create the local,
# untracked working INI automatically on the first deployment.
if [[ ! -f "$INI" ]]; then
    if [[ -f "$INI_EXAMPLE" ]]; then
        cp -- "$INI_EXAMPLE" "$INI"
        echo "Created local emulator configuration:"
        echo "  $INI"
        echo "  from $INI_EXAMPLE"
        echo
    else
        echo "INI not found: $INI" >&2
        echo "INI example not found: $INI_EXAMPLE" >&2
        exit 1
    fi
fi

# Replay captures are machine/session-specific and intentionally not shipped
# in the public repository.
#
# Resolution order:
#   1. RNET_REPLAY_FILE environment variable
#   2. repository root/rnet-replay.txt
#   3. an already working file/symlink in the Programmer directory
#
# We never replace a real replay with examples/rnet-replay.example.txt.
REPLAY_SOURCE=""

if [[ -n "${RNET_REPLAY_FILE:-}" ]]; then
    candidate=$(realpath -m -- "$RNET_REPLAY_FILE")
    if [[ ! -f "$candidate" ]]; then
        echo "RNET_REPLAY_FILE does not exist: $candidate" >&2
        exit 1
    fi
    REPLAY_SOURCE=$candidate
elif [[ -f "$REPLAY" ]]; then
    REPLAY_SOURCE=$(realpath -- "$REPLAY")
elif [[ -f "$PROGRAM_DIR/rnet-replay.txt" ]]; then
    # -f follows symlinks, so this is true only for a working existing link/file.
    REPLAY_SOURCE=$(realpath -- "$PROGRAM_DIR/rnet-replay.txt")
fi

STAMP=$(date +%Y%m%d-%H%M%S)

backup_regular_file() {
    local target=$1

    if [[ -f "$target" && ! -L "$target" ]]; then
        local backup="$target.backup-$STAMP"
        cp -a -- "$target" "$backup"
        printf 'Backup:\n  %s\n' "$backup"
    fi
}

install_link() {
    local source=$1
    local target=$2

    backup_regular_file "$target"
    rm -f -- "$target"
    ln -s -- "$source" "$target"

    printf 'Linked:\n  %s -> %s\n' "$target" "$source"
}

echo "Repository:"
echo "  $REPO_DIR"
echo
echo "Program directory:"
echo "  $PROGRAM_DIR"
echo

# R-net Programmer installations have been observed requesting both spellings.
install_link "$DLL" "$PROGRAM_DIR/ftd2xx.dll"
install_link "$DLL" "$PROGRAM_DIR/FTD2XX.dll"

# Local emulator configuration.
install_link "$INI" "$PROGRAM_DIR/ftd2xx-emu.ini"

# Captured replay, only when a real one is available.
if [[ -n "$REPLAY_SOURCE" ]]; then
    # If the resolved source is the existing Programmer path itself and it is a
    # regular file, leave it in place. Otherwise install/update the symlink.
    target="$PROGRAM_DIR/rnet-replay.txt"

    if [[ ! -L "$target" && -f "$target" && "$(realpath -- "$target")" == "$REPLAY_SOURCE" ]]; then
        echo "Replay already present as regular file:"
        echo "  $target"
    elif [[ -L "$target" && -e "$target" && "$(realpath -- "$target")" == "$REPLAY_SOURCE" ]]; then
        echo "Replay link already valid:"
        echo "  $target -> $(readlink "$target")"
    else
        install_link "$REPLAY_SOURCE" "$target"
    fi
else
    echo "WARNING: no real rnet-replay.txt was found."
    echo "The public repository intentionally does not ship captured replay data."
    echo
    echo "Provide it in one of these ways:"
    echo "  cp /path/to/your/rnet-replay.txt \"$REPO_DIR/rnet-replay.txt\""
    echo "or:"
    echo "  RNET_REPLAY_FILE=/path/to/your/rnet-replay.txt \\"
    echo "    \"$0\" \"$PROGRAM_DIR\" \"$REPO_DIR\""
    echo
fi

# MinGW runtime dependency used by the 32-bit DLL.
if [[ -f "$MINGW_PTHREAD" ]]; then
    install_link "$MINGW_PTHREAD" "$PROGRAM_DIR/libwinpthread-1.dll"
else
    echo "WARNING: MinGW runtime not found:"
    echo "  $MINGW_PTHREAD"
    echo "libwinpthread-1.dll was not changed."
    echo
fi

echo "Deployment status:"
for name in \
    ftd2xx.dll \
    FTD2XX.dll \
    ftd2xx-emu.ini \
    rnet-replay.txt \
    libwinpthread-1.dll
do
    target="$PROGRAM_DIR/$name"

    if [[ -L "$target" ]]; then
        if [[ -e "$target" ]]; then
            printf '  %-22s -> %s\n' "$name" "$(readlink "$target")"
        else
            printf '  %-22s -> %s  [BROKEN]\n' "$name" "$(readlink "$target")"
        fi
    elif [[ -f "$target" ]]; then
        printf '  %-22s   regular file\n' "$name"
    else
        printf '  %-22s   missing\n' "$name"
    fi
done

echo
echo "Done."
