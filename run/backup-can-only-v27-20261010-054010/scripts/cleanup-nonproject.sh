#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
RNet-Dongle-Emu cleanup

Usage:
  scripts/cleanup-nonproject.sh --check
  scripts/cleanup-nonproject.sh --delete
  scripts/cleanup-nonproject.sh --purge-all

Modes:
  --check      Show what would be removed. Nothing is changed. Default.
  --delete     Remove tracked backup/temp files and untracked/ignored project
               clutter, while preserving important local R-Net research/runtime
               data.
  --purge-all  DANGEROUS: also remove local ignored/untracked research/runtime
               data. Tracked source files are still preserved.

Normal cleanup preserves:
  rnet-replay.txt
  rnet-block-state.bin
  *.pcap
  *.pcapng
  *.R-net
  *.rnd
  *.dec.bin
  .vscode/
EOF
}

MODE="check"
case "${1:---check}" in
    --check) MODE="check" ;;
    --delete) MODE="delete" ;;
    --purge-all) MODE="purge-all" ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
esac

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || true)"
if [[ -z "$ROOT" ]]; then
    echo "FEHLER: Nicht innerhalb eines Git-Repositories." >&2
    exit 1
fi

cd "$ROOT"

if [[ ! -f CMakeLists.txt || ! -d src ]]; then
    echo "FEHLER: Das sieht nicht wie RNet-Dongle-Emu aus: $ROOT" >&2
    exit 1
fi

echo "Projekt: $ROOT"
echo "Modus:   $MODE"
echo

is_junk_tracked() {
    local f="$1"
    case "$f" in
        *.bak|*.bak-*|*.backup-*|*.before-*|*.orig|*.rej|*~|*.tmp|*.swp|*.swo)
            return 0
            ;;
        */.DS_Store|.DS_Store|*/Thumbs.db|Thumbs.db)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

tracked_junk=()
while IFS= read -r -d '' f; do
    if is_junk_tracked "$f"; then
        tracked_junk+=("$f")
    fi
done < <(git ls-files -z)

echo "== Getrackte Backup-/Temp-Dateien =="
if ((${#tracked_junk[@]} == 0)); then
    echo "keine"
else
    printf '  %s\n' "${tracked_junk[@]}"
fi
echo

clean_excludes=(
    -e 'CMakeLists.txt'
    -e 'CMakePresets.json'
    -e 'cmake/'
    -e 'rollstuhl.emu/CMakeLists.txt'
    -e 'tools/rnet-can-proxy/CMakeLists.txt'
    -e 'assets/branding/'
    -e 'docs/assets/'
    -e 'docs/BRANDING.md'
    -e 'rnet-replay.txt'
    -e 'rnet-block-state.bin'
    -e '*.pcap'
    -e '*.pcapng'
    -e '*.R-net'
    -e '*.rnd'
    -e '*.dec.bin'
    -e '.vscode/'
)

echo "== Ungetrackte/ignorierte Dateien =="
if [[ "$MODE" == "purge-all" ]]; then
    git clean -ndx
else
    git clean -ndx "${clean_excludes[@]}"
fi
echo

if [[ "$MODE" == "check" ]]; then
    echo "Nichts gelöscht."
    echo "Zum Löschen:"
    echo "  bash scripts/cleanup-nonproject.sh --delete"
    exit 0
fi

if ((${#tracked_junk[@]} > 0)); then
    echo "== Lösche getrackte Backup-/Temp-Dateien mit git rm =="
    git rm -- "${tracked_junk[@]}"
fi

echo
echo "== Lösche ungetrackte/ignorierte Dateien =="

if [[ "$MODE" == "purge-all" ]]; then
    git clean -fdx
else
    git clean -fdx "${clean_excludes[@]}"
fi

echo
echo "== Ergebnis =="
git status --short
echo
echo "Bereinigung abgeschlossen."
