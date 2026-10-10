#!/usr/bin/env python3
"""Create the versioned RNetMsgBroker delivery ZIP.

The archive is a source delivery package for GitHub/releases. It intentionally
contains no build tree, no .git directory and no generated candump/CSV/log files.
"""

from __future__ import annotations

import argparse
import json
import os
import stat
import sys
import time
import zipfile
from pathlib import Path

EXCLUDED_DIR_NAMES = {
    ".git",
    ".idea",
    ".vscode",
    "__pycache__",
    ".pytest_cache",
    ".moc",
    ".obj",
    ".pch",
    ".rcc",
    ".uic",
}

EXCLUDED_FILE_SUFFIXES = {
    ".csv",
    ".out",
    ".log",
    ".pyc",
    ".o",
    ".obj",
    ".a",
    ".so",
    ".dll",
    ".exe",
    ".qm",
}

EXCLUDED_FILE_NAMES = {
    "CMakeCache.txt",
    "cmake_install.cmake",
    "compile_commands.json",
    "Makefile",
    ".DS_Store",
}


def is_build_like_dir(path: Path) -> bool:
    """Return true for common generated build directories."""
    name = path.name
    return name == "build" or name.startswith("build-") or name.startswith("build_")


def should_skip(path: Path, source_dir: Path, output: Path) -> bool:
    """Decide whether a path must stay out of the delivery ZIP."""
    try:
        if path.resolve() == output.resolve():
            return True
    except FileNotFoundError:
        pass

    rel_parts = path.relative_to(source_dir).parts
    if any(part in EXCLUDED_DIR_NAMES for part in rel_parts):
        return True
    if any(is_build_like_dir(Path(part)) for part in rel_parts):
        return True
    if path.is_dir():
        return False
    if path.name in EXCLUDED_FILE_NAMES:
        return True
    if path.suffix in EXCLUDED_FILE_SUFFIXES:
        return True
    if path.name.endswith("~") or path.name.endswith(".autosave"):
        return True
    return False


def validate_project(source_dir: Path, version: str) -> None:
    """Run cheap release sanity checks before packaging."""
    version_file = source_dir / "VERSION.txt"
    cmake_file = source_dir / "CMakeLists.txt"
    rnet_json = source_dir / "R-Net.json"

    if not version_file.exists():
        raise SystemExit("VERSION.txt fehlt")
    if not cmake_file.exists():
        raise SystemExit("CMakeLists.txt fehlt")
    if not rnet_json.exists():
        raise SystemExit("R-Net.json fehlt")

    first_line = version_file.read_text(encoding="utf-8").splitlines()[0].strip()
    if first_line != version:
        raise SystemExit(f"VERSION.txt={first_line!r}, erwartet {version!r}")

    cmake_text = cmake_file.read_text(encoding="utf-8")
    if f"project(RNetMsgBroker VERSION {version} " not in cmake_text:
        raise SystemExit("CMakeLists.txt does not contain the expected project version")

    with rnet_json.open("r", encoding="utf-8") as fh:
        json.load(fh)


def zip_date_time(timestamp: float) -> tuple[int, int, int, int, int, int]:
    """Return a ZIP-compatible timestamp tuple."""
    dt = time.localtime(timestamp)[:6]
    if dt[0] < 1980:
        return (1980, 1, 1, 0, 0, 0)
    return dt


def add_directory_entry(zf: zipfile.ZipFile, arcname: str, timestamp: float) -> None:
    """Add an explicit directory entry with stable permissions."""
    if not arcname.endswith("/"):
        arcname += "/"
    info = zipfile.ZipInfo(arcname)
    info.date_time = zip_date_time(timestamp)
    info.external_attr = (stat.S_IFDIR | 0o755) << 16
    zf.writestr(info, b"")


def add_file(zf: zipfile.ZipFile, path: Path, arcname: str) -> None:
    """Add one file while preserving Unix permission bits and mtime."""
    st = path.stat()
    info = zipfile.ZipInfo(arcname)
    info.date_time = zip_date_time(st.st_mtime)
    info.external_attr = (st.st_mode & 0xFFFF) << 16
    with path.open("rb") as fh:
        zf.writestr(info, fh.read(), compress_type=zipfile.ZIP_DEFLATED)


def create_zip(source_dir: Path, output: Path, version: str) -> int:
    """Create the delivery ZIP and return the number of packaged files."""
    top = "RNetMsgBroker"
    files: list[Path] = []

    for path in sorted(source_dir.rglob("*")):
        if should_skip(path, source_dir, output):
            continue
        if path.is_file():
            files.append(path)

    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        output.unlink()

    source_timestamp = source_dir.stat().st_mtime
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        add_directory_entry(zf, f"{top}/", source_timestamp)
        seen_dirs = {f"{top}/"}
        for path in files:
            rel = path.relative_to(source_dir).as_posix()
            arcname = f"{top}/{rel}"
            parent = str(Path(arcname).parent).replace(os.sep, "/") + "/"
            if parent not in seen_dirs:
                parts = parent.strip("/").split("/")
                accum = ""
                for part in parts:
                    accum = f"{accum}{part}/"
                    if accum not in seen_dirs:
                        add_directory_entry(zf, accum, source_timestamp)
                        seen_dirs.add(accum)
            add_file(zf, path, arcname)

    # Verify the file can be read immediately after writing.
    with zipfile.ZipFile(output, "r") as zf:
        bad = zf.testzip()
    if bad:
        raise SystemExit(f"ZIP-Test fehlgeschlagen bei {bad}")

    return len(files)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Create RNetMsgBroker delivery ZIP")
    parser.add_argument("--source-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--version", required=True)
    args = parser.parse_args(argv)

    source_dir = args.source_dir.resolve()
    output = args.output.resolve()
    validate_project(source_dir, args.version)
    count = create_zip(source_dir, output, args.version)
    print(f"DELIVER_OK {output} ({count} files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
