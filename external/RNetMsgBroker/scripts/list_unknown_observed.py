#!/usr/bin/env python3
"""List and count frames with phase=unknown.observed in RNetMsgBroker CSV."""

from __future__ import annotations

import csv
import sys
from collections import Counter
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {Path(sys.argv[0]).name} <candump.csv>", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    counts: Counter[str] = Counter()
    rows: list[dict[str, str]] = []

    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter=";")
        required = {"zeile", "zeit", "can_id", "data", "name", "phase", "ausgabe"}
        missing = required - set(reader.fieldnames or [])
        if missing:
            print(f"Missing CSV columns: {', '.join(sorted(missing))}", file=sys.stderr)
            return 3

        for row in reader:
            if row.get("phase") == "unknown.observed":
                rows.append(row)
                counts[row.get("name", "")] += 1

    print("Counts:")
    if counts:
        for name, count in counts.most_common():
            print(f"{count:6d}  {name}")
    else:
        print("     0  unknown.observed")

    if rows:
        print("\nFirst rows:")
        print("zeile;zeit;can_id;data;name;ausgabe")
        for row in rows[:20]:
            print(";".join(row.get(col, "") for col in ("zeile", "zeit", "can_id", "data", "name", "ausgabe")))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
