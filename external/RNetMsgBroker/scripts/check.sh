#!/usr/bin/env bash
set -euo pipefail

# Repository smoke test:
# 1. validate R-Net.json
# 2. build project if CMake/Qt are available
# 3. check version output
# 4. create CSV from bundled known-good test data
# 5. check that no decoder UNKNOWN appears in this known-good CSV

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

python3 -m json.tool R-Net.json >/dev/null
echo "JSON_OK"

python3 - <<'PY'
import json
from pathlib import Path
forbidden = {"fi"+"eds", "des"+"cipt", "big_endi"+"en", "pfa"+"se"}
data = json.loads(Path("R-Net.json").read_text(encoding="utf-8"))
found = []
def walk(obj, path="$"):
    if isinstance(obj, dict):
        for k, v in obj.items():
            if k in forbidden:
                found.append(f"{path}.{k}")
            walk(v, f"{path}.{k}")
    elif isinstance(obj, list):
        for i, v in enumerate(obj):
            walk(v, f"{path}[{i}]")
walk(data)
if found:
    raise SystemExit("Forbidden JSON keys found:\n" + "\n".join(found))
print("JSON_KEYS_OK")
PY

if command -v cmake >/dev/null 2>&1; then
  scripts/build.sh
else
  echo "cmake nicht gefunden: Build übersprungen" >&2
  exit 4
fi

BIN="$ROOT_DIR/build/RNetMsgBrokerTest"
if [[ ! -x "$BIN" ]]; then
  BIN="$ROOT_DIR/build/Desktop_Debug/RNetMsgBrokerTest"
fi

"$BIN" --version
LIBVERSION="$($BIN --libversion)"
if [[ -z "$LIBVERSION" ]]; then
  echo "leere Lib-Version" >&2
  exit 6
fi
echo "LIBVERSION_OK $LIBVERSION"

OUT="$ROOT_DIR/testdata/candump_sample.csv"
"$BIN" -d "$ROOT_DIR/testdata/candump_sample.txt" --full -o "$OUT"

test -s "$OUT"
if grep -q 'UNKNOWN ' "$OUT"; then
  echo "Decoder UNKNOWN in known-good sample $OUT gefunden" >&2
  grep 'UNKNOWN ' "$OUT" >&2
  exit 5
fi

echo "CSV_OK $OUT"
python3 scripts/list_unknown_observed.py "$OUT" || true
