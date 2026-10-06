#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 <program-directory> [dll-path]" >&2
    echo "Example: $0 \"$HOME/.wine/drive_c/Program Files (x86)/Vendor/Application\"" >&2
    exit 2
}

[[ $# -ge 1 && $# -le 2 ]] || usage

PROGRAM_DIR=$1
DLL=${2:-"$(cd "$(dirname "$0")/.." && pwd)/build/mingw32/ftd2xx.dll"}

[[ -d "$PROGRAM_DIR" ]] || {
    echo "Directory not found: $PROGRAM_DIR" >&2
    exit 1
}

[[ -f "$DLL" ]] || {
    echo "DLL not found: $DLL" >&2
    exit 1
}

TARGET="$PROGRAM_DIR/ftd2xx.dll"
STAMP=$(date +%Y%m%d-%H%M%S)
BACKUP="$PROGRAM_DIR/ftd2xx.dll.backup-$STAMP"

if [[ -L "$TARGET" ]]; then
    if [[ -e "$TARGET" ]]; then
        cp -a --dereference "$TARGET" "$BACKUP"
        echo "Backed up existing symlink target:"
        echo "  $BACKUP"
    else
        echo "Removing broken symlink:"
        echo "  $TARGET -> $(readlink "$TARGET")"
    fi
    rm -f "$TARGET"
elif [[ -e "$TARGET" ]]; then
    cp -a "$TARGET" "$BACKUP"
    echo "Backed up existing DLL:"
    echo "  $BACKUP"
fi

ln -s "$DLL" "$TARGET"

printf 'Installed symlink:\n  %s -> %s\n' "$TARGET" "$DLL"
