#!/usr/bin/env python3
"""
rnet-bin2md.py
==============

Complete PGDT R-net Programmer database/configuration inventory to Markdown.

The report follows the Programmer's main tree:

  Profile Management
  Configuration
  Speeds
  Controls
  Latched
  Seating
  Motor
  Input Output Module
  Omni
  Mouse 1
  Mouse 2
  iDevice1
  iDevice2

Input formats:
  * a saved *.R-net file (recommended for a complete system image)
  * an emulator RNB2 rnet-block-state.bin overlay

The decrypted RND database is used as the authoritative metadata source for
parameter names, paths, ranges, defaults, units and NV locator records.

Examples:
  python3 rnet-bin2md.py example.R-net
  python3 rnet-bin2md.py rnet-block-state.bin
  python3 rnet-bin2md.py example.R-net report.md
  python3 rnet-bin2md.py example.R-net report.md \
      --rnd Generic_V33_1_1245.dec.bin

When --rnd is omitted, Generic_V33_1_1245.dec.bin is searched in the current
directory and next to this script.
"""

from __future__ import annotations

import argparse
import collections
import re
import struct
from pathlib import Path


SECTIONS = [
    "Profile Management",
    "Configuration",
    "Speeds",
    "Controls",
    "Latched",
    "Seating",
    "Motor",
    "Input Output Module",
    "Omni",
    "Mouse 1",
    "Mouse 2",
    "iDevice1",
    "iDevice2",
]

PROFILE_COUNT = 8
PM_PROFILE_SELECTOR = bytes.fromhex("01000100")
PROFILE_MGMT_SELECTOR = bytes.fromhex("02000000")
NAMES_SELECTOR = bytes.fromhex("06000100")

PROFILE_MGMT_FILE_HEADER = 6
PM_PROFILE_RECORD_SIZE = 30

MODE_NAME_RECORD_SIZE = 28
MODE_NAME_LENGTH = 20
PROFILE_NAME_RECORD_SIZE = 40
PROFILE_NAME_MARKER_OFFSET = 15
PROFILE_NAME_MARKER = b"Pddd"
PROFILE_NAME_OFFSET = 19
PROFILE_NAME_LENGTH = 20


def s32(v: int) -> int:
    return v - 0x100000000 if v & 0x80000000 else v


def selector_text(selector: bytes) -> str:
    return selector.hex().upper()


def read_lp(data: bytes, pos: int, max_len: int = 255):
    if pos < 0 or pos >= len(data):
        return None
    n = data[pos]
    if n > max_len or pos + 1 + n > len(data):
        return None
    raw = data[pos + 1:pos + 1 + n]
    try:
        text = raw.decode("latin1")
    except UnicodeDecodeError:
        return None
    return text, pos + 1 + n


def printable(text: str) -> bool:
    return all((32 <= ord(c) < 127) or c in "\r\n\t" for c in text)


def locate_default_rnd() -> Path | None:
    for p in (
        Path.cwd() / "Generic_V33_1_1245.dec.bin",
        Path(__file__).resolve().parent / "Generic_V33_1_1245.dec.bin",
    ):
        if p.is_file():
            return p
    return None


def parse_dictionary(rnd: bytes):
    """
    Parse the English dictionary entries before the NV-location area.

    Entry prefix:
      code, canonical name, display name, short NV name,
      abbreviation, description    (all 8-bit length-prefixed)

    The fixed metadata following the description contains min/max/step/default.
    Unit and category are also recovered from that structure.
    """
    result = []

    # For this Generic V33.1 database the dictionary is before the first
    # English NV-location records. Restricting the scan prevents localized
    # dictionary copies later in the file from appearing as duplicates.
    stop = rnd.find(b"  Fwd Speed Max         Spd^  ")
    if stop < 0:
        stop = min(len(rnd), 0xEC000)

    for pos in range(0x2000, stop):
        first = read_lp(rnd, pos, 32)
        if not first:
            continue

        code, p = first
        if not (
            2 <= len(code) <= 24
            and printable(code)
            and re.fullmatch(r"[A-Za-z0-9_]+", code)
        ):
            continue

        item = read_lp(rnd, p, 64)
        if not item:
            continue
        canonical, p = item
        if not (
            2 <= len(canonical) <= 48
            and printable(canonical)
            and re.fullmatch(r"[A-Za-z0-9_]+", canonical)
        ):
            continue

        strings = []
        ok = True
        for max_len in (100, 40, 12, 255):
            item = read_lp(rnd, p, max_len)
            if not item:
                ok = False
                break
            text, p = item
            if not printable(text):
                ok = False
                break
            strings.append(text)

        if not ok:
            continue

        display, short_name, abbreviation, description = strings
        meta = p

        if meta + 100 > len(rnd) or rnd[meta] > 3:
            continue

        try:
            nums = struct.unpack_from("<8I", rnd, meta + 1)
        except struct.error:
            continue

        # Unit is normally at meta+57. Some percentage-formatted entries
        # contain one additional format byte and therefore start at +58.
        unit = None
        category = None
        for unit_off in range(55, 61):
            u = read_lp(rnd, meta + unit_off, 40)
            if not u or not printable(u[0]):
                continue

            candidate_unit, unit_end = u
            c = read_lp(rnd, unit_end + 18, 120)
            if not c:
                continue
            candidate_category, _ = c

            if (
                candidate_category
                and printable(candidate_category)
                and re.search(r"[A-Za-z]", candidate_category)
            ):
                unit = candidate_unit
                category = candidate_category
                break

        if category is None:
            continue

        result.append({
            "pos": pos,
            "code": code,
            "canonical": canonical,
            "display": display,
            "short": short_name,
            "abbreviation": abbreviation,
            "description": description,
            "minimum": s32(nums[4]),
            "maximum": s32(nums[5]),
            "step": s32(nums[6]),
            "default": s32(nums[7]),
            "unit": unit or "",
            "category": category,
            "top": category.split("~", 1)[0],
        })

    # Scan can occasionally see the same byte sequence through another
    # syntactically-valid path. The code is unique in the real dictionary.
    by_code = {}
    for entry in result:
        by_code.setdefault(entry["code"], entry)

    return list(by_code.values())


def parse_file_layout(rnd: bytes):
    """
    Recover the RND file-layout map:
        selector -> module name and nominal ODI 0x89 value

    Example rows in this RND:
        01000100 / 38 -> Power
        06000100 / 22 -> Joystick
        0D000100 / 16 -> Omni
    """
    rows = {}
    end = min(len(rnd), 0x2400)

    for off in range(9, end):
        n = rnd[off - 1]
        if not (1 <= n <= 40) or off + n > end:
            continue

        raw_name = rnd[off:off + n]
        if not all(32 <= b < 127 for b in raw_name):
            continue

        selector = rnd[off - 9:off - 5]
        if len(selector) != 4:
            continue

        # PGDT repository selectors in this layout have 00 as byte 1 and 3,
        # and global/profile selector type in byte 2.
        if not (
            selector[1] == 0
            and selector[2] in (0, 1)
            and selector[3] == 0
            and selector[0] != 0
        ):
            continue

        value89 = struct.unpack_from("<I", rnd, off - 5)[0]
        if value89 > 255:
            continue

        name = raw_name.decode("ascii")
        current = rows.get(selector)

        # Prefer the first ordinary (non-ghost duplicate) layout row.
        if current is None:
            rows[selector] = {
                "name": name,
                "value89": value89,
                "offset": off,
            }

    return rows


def read_rnb2(path: Path):
    raw = path.read_bytes()
    if raw[:4] != b"RNB2":
        raise ValueError(f"{path}: not an RNB2 state file")

    count = struct.unpack_from("<I", raw, 4)[0]
    pos = 8
    blocks = {}

    for _ in range(count):
        if pos + 16 > len(raw):
            raise ValueError("truncated RNB2 header")

        selector = raw[pos:pos + 4]
        pos += 4

        flags, value89, size = struct.unpack_from("<III", raw, pos)
        pos += 12

        data = raw[pos:pos + size]
        pos += size

        if len(data) != size:
            raise ValueError("truncated RNB2 payload")

        blocks[selector] = {
            "selector": selector,
            "value89": value89,
            "checksum": None,
            "tag": None,
            "data": data,
            "source": "RNB2 overlay",
            "flags": flags,
        }

    if pos != len(raw):
        raise ValueError("unexpected bytes after RNB2 records")

    return blocks


def parse_rnet_archive(path: Path, layout):
    """
    Parse the saved *.R-net record chain.

    Each record is:
      selector[4]
      ODI89 u32 LE
      reserved u16 LE
      file checksum u16 LE
      archive tag u32 LE
      payload size u16 LE
      payload[size]

    The file has a private header before the record chain. We find the best
    chain automatically instead of hard-coding its offset.
    """
    raw = path.read_bytes()
    selectors = set(layout)
    best = None

    for start in range(0, max(0, len(raw) - 18)):
        selector = raw[start:start + 4]
        if selector not in selectors:
            continue

        pos = start
        records = []
        good = True

        while pos + 18 <= len(raw):
            header = raw[pos:pos + 18]
            sel = header[:4]

            if sel not in selectors:
                good = False
                break

            value89 = struct.unpack_from("<I", header, 4)[0]
            reserved = struct.unpack_from("<H", header, 8)[0]
            checksum = struct.unpack_from("<H", header, 10)[0]
            tag = struct.unpack_from("<I", header, 12)[0]
            size = struct.unpack_from("<H", header, 16)[0]

            if reserved != 0 or pos + 18 + size > len(raw):
                good = False
                break

            payload_start = pos + 18
            payload_end = payload_start + size

            records.append({
                "selector": sel,
                "value89": value89,
                "checksum": checksum,
                "tag": tag,
                "data": raw[payload_start:payload_end],
                "source": "*.R-net archive",
                "archive_header_offset": pos,
                "archive_payload_offset": payload_start,
            })

            pos = payload_end

            if pos == len(raw):
                break

        if records:
            score = (
                1 if pos == len(raw) else 0,
                len(records),
                pos - start,
            )
            if best is None or score > best[0]:
                best = (score, records, start, pos)

    if best is None:
        raise ValueError("no PGDT repository record chain found")

    _, records, start, end = best
    blocks = {r["selector"]: r for r in records}

    return blocks, {
        "record_chain_offset": start,
        "record_chain_end": end,
        "record_count": len(records),
        "file_size": len(raw),
    }


def merge_blocks(base_blocks, overlay_blocks):
    result = dict(base_blocks)
    for selector, block in overlay_blocks.items():
        result[selector] = block
    return result


def clean_nv_name(raw24: bytes) -> str:
    # First two bytes are table/display metadata, not part of the English name.
    return raw24[2:24].decode("latin1").rstrip()


def build_name_index(entries):
    index = collections.defaultdict(list)

    for e in entries:
        for name in (e["short"].strip(), e["display"].strip()):
            if name:
                index[name].append(e)

    return index


def choose_dictionary_entry(candidates, name, minimum, maximum):
    if not candidates:
        return None

    scored = []

    for e in candidates:
        score = 0

        if e["minimum"] == minimum:
            score += 4
        if e["maximum"] == maximum:
            score += 4
        if e["short"].strip() == name:
            score += 3
        if e["display"].strip() == name:
            score += 1

        scored.append((score, -e["pos"], e))

    scored.sort(reverse=True, key=lambda x: (x[0], x[1]))
    return scored[0][2]


def parse_nv_locators(rnd: bytes, entries, selectors):
    """
    Recover English 49-byte NV-location records.

    Instead of assuming table offsets, locate every repository selector in the
    RND and test whether it occurs at bytes 41..44 of a structurally valid
    record whose English name matches the dictionary.
    """
    name_index = build_name_index(entries)
    raw_records = []

    for selector in selectors:
        search_from = 0

        while True:
            hit = rnd.find(selector, search_from)
            if hit < 0:
                break
            search_from = hit + 1

            pos = hit - 41
            if pos < 0 or pos + 49 > len(rnd):
                continue

            rec = rnd[pos:pos + 49]

            if not all(32 <= b < 127 for b in rec[2:24]):
                continue

            name = clean_nv_name(rec[:24])
            if name not in name_index:
                continue

            abbreviation = rec[24:30].decode("latin1").rstrip()
            maximum = rec[30]
            minimum = rec[31]
            logical_offset = rec[39]

            entry = choose_dictionary_entry(
                name_index[name],
                name,
                minimum,
                maximum,
            )

            if entry is None:
                continue

            raw_records.append({
                "rnd_offset": pos,
                "name": name,
                "abbreviation": abbreviation,
                "maximum": maximum,
                "minimum": minimum,
                "logical_offset": logical_offset,
                "selector": selector,
                "entry": entry,
            })

    # Localized tables may keep identical English words such as "Port".
    # Deduplicate equivalent locator definitions, keeping the first occurrence,
    # which is the English table in this database.
    unique = {}

    for r in sorted(raw_records, key=lambda x: x["rnd_offset"]):
        key = (
            r["selector"],
            r["name"],
            r["logical_offset"],
            r["minimum"],
            r["maximum"],
            r["entry"]["code"],
        )
        unique.setdefault(key, r)

    return list(unique.values())


def locate_name_areas(payload: bytes):
    hits = [m.start() for m in re.finditer(re.escape(PROFILE_NAME_MARKER), payload)]

    if len(hits) < PROFILE_COUNT:
        return None

    hits = hits[:PROFILE_COUNT]

    if any(
        b - a != PROFILE_NAME_RECORD_SIZE
        for a, b in zip(hits, hits[1:])
    ):
        return None

    profile_base = hits[0] - PROFILE_NAME_MARKER_OFFSET
    mode_base = profile_base - PROFILE_COUNT * MODE_NAME_RECORD_SIZE

    if mode_base < 0:
        return None

    return mode_base, profile_base


def extract_names(blocks):
    block = blocks.get(NAMES_SELECTOR)

    fallback = {
        "profiles": [f"Profile {i}" for i in range(1, PROFILE_COUNT + 1)],
        "modes": [f"Mode {i}" for i in range(1, PROFILE_COUNT + 1)],
        "mode_base": None,
        "profile_base": None,
    }

    if not block:
        return fallback

    payload = block["data"]
    areas = locate_name_areas(payload)

    if not areas:
        return fallback

    mode_base, profile_base = areas
    modes = []
    profiles = []

    for i in range(PROFILE_COUNT):
        start = mode_base + i * MODE_NAME_RECORD_SIZE
        modes.append(
            payload[start:start + MODE_NAME_LENGTH]
            .decode("latin1")
            .rstrip()
        )

    for i in range(PROFILE_COUNT):
        start = (
            profile_base
            + i * PROFILE_NAME_RECORD_SIZE
            + PROFILE_NAME_OFFSET
        )
        profiles.append(
            payload[start:start + PROFILE_NAME_LENGTH]
            .decode("latin1")
            .rstrip()
        )

    return {
        "profiles": profiles,
        "modes": modes,
        "mode_base": mode_base,
        "profile_base": profile_base,
    }


def decode_bits(value: int, count: int = 8):
    return [i + 1 for i in range(count) if value & (1 << i)]


def profile_management_state(blocks, names):
    result = {
        "profile_mask": None,
        "mode_masks": [None] * PROFILE_COUNT,
        "id_type": None,
        "id_subtype": None,
    }

    block = blocks.get(PROFILE_MGMT_SELECTOR)
    if not block:
        return result

    p = block["data"]

    def byte_at(logical):
        absolute = PROFILE_MGMT_FILE_HEADER + logical
        return p[absolute] if absolute < len(p) else None

    result["id_type"] = byte_at(0x0A)
    result["id_subtype"] = byte_at(0x0C)
    result["profile_mask"] = byte_at(0x4A)

    for i in range(PROFILE_COUNT):
        result["mode_masks"][i] = byte_at(0x4C + i)

    return result


def pm_profile_current_values(blocks, entries):
    block = blocks.get(PM_PROFILE_SELECTOR)
    if not block:
        return None

    payload = block["data"]
    area_size = PROFILE_COUNT * PM_PROFILE_RECORD_SIZE

    if len(payload) < area_size:
        return None

    base = len(payload) - area_size

    wanted = [
        "FWDSPMAX", "FWDSPMIN", "REVSPMAX", "REVSPMIN",
        "TRNSPMAX", "TRNSPMIN",
        "FACCMAX", "FACCMIN", "RACCMAX", "RACCMIN",
        "FDECMAX", "FDECMIN", "RDECMAX", "RDECMIN",
        "TACCMAX", "TACCMIN", "TDECMAX", "TDECMIN",
        "PWRREDUC", "TORQUE", "TREMDAMP",
    ]

    by_code = {e["code"]: e for e in entries}

    # The PM profile record layout is explicitly confirmed by the NV table.
    # These are the logical record-byte offsets.
    offsets = {
        "FWDSPMAX": 2, "FWDSPMIN": 3,
        "REVSPMAX": 4, "REVSPMIN": 5,
        "TRNSPMAX": 6, "TRNSPMIN": 7,
        "FACCMAX": 8, "FACCMIN": 9,
        "RACCMAX": 10, "RACCMIN": 11,
        "FDECMAX": 12, "FDECMIN": 13,
        "RDECMAX": 14, "RDECMIN": 15,
        "TACCMAX": 16, "TACCMIN": 17,
        "TDECMAX": 18, "TDECMIN": 19,
        "PWRREDUC": 20,
        "TORQUE": 21,
        "TREMDAMP": 22,
    }

    rows = []

    for code in wanted:
        entry = by_code.get(code)
        off = offsets[code]
        values = []

        for profile in range(PROFILE_COUNT):
            absolute = base + profile * PM_PROFILE_RECORD_SIZE + off
            values.append(payload[absolute] if absolute < len(payload) else None)

        rows.append({
            "entry": entry,
            "code": code,
            "record_offset": off,
            "values": values,
        })

    return {
        "base": base,
        "rows": rows,
    }


def section_entries(entries, section):
    rows = [e for e in entries if e["top"] == section]

    # Preserve a stable Programmer-like grouping: subpath then display/code.
    rows.sort(
        key=lambda e: (
            e["category"].casefold(),
            e["display"].casefold(),
            e["code"].casefold(),
        )
    )
    return rows


def locators_by_code(locators):
    result = collections.defaultdict(list)
    for loc in locators:
        result[loc["entry"]["code"]].append(loc)
    return result


def range_text(entry):
    minimum = entry["minimum"]
    maximum = entry["maximum"]
    step = entry["step"]

    if minimum == maximum == 0:
        base = "—"
    else:
        base = f"{minimum}..{maximum}"

    if step not in (0, 1):
        base += f" / Schritt {step}"

    return base


def locator_text(locs):
    if not locs:
        return "—"

    parts = []

    for loc in sorted(
        locs,
        key=lambda x: (
            selector_text(x["selector"]),
            x["logical_offset"],
            x["rnd_offset"],
        ),
    ):
        parts.append(
            f"`{selector_text(loc['selector'])}:"
            f"0x{loc['logical_offset']:02X}`"
        )

    # Remove textual duplicates while preserving order.
    return ", ".join(dict.fromkeys(parts))


def module_name(layout, selector):
    row = layout.get(selector)
    return row["name"] if row else "unknown/special"


def write_repository_table(out, blocks, layout):
    out.append("## Repository files")
    out.append("")
    out.append("| Selector | RND module | ODI89 | Bytes | Checksum | Source |")
    out.append("|---|---|---:|---:|---:|---|")

    for selector, block in sorted(blocks.items(), key=lambda x: x[0]):
        checksum = (
            f"`{block['checksum']:04X}`"
            if block.get("checksum") is not None
            else "—"
        )

        out.append(
            f"| `{selector_text(selector)}` | "
            f"{module_name(layout, selector)} | "
            f"{block.get('value89', 0)} | "
            f"{len(block['data'])} | "
            f"{checksum} | "
            f"{block.get('source', '')} |"
        )

    out.append("")


def write_profile_management(out, entries, blocks, locmap, names):
    out.append("## Profile Management")
    out.append("")

    state = profile_management_state(blocks, names)

    if state["profile_mask"] is not None:
        enabled = set(decode_bits(state["profile_mask"]))

        out.append(
            f"Profile Enable: `0x{state['profile_mask']:02X}` "
            f"→ {', '.join(str(x) for x in sorted(enabled)) or 'none'}"
        )
        out.append("")
        out.append("| Profile | Name | Enabled | Mode Enable | Modes |")
        out.append("|---:|---|:---:|---:|---|")

        for i in range(PROFILE_COUNT):
            mask = state["mode_masks"][i]
            mode_ids = decode_bits(mask) if mask is not None else []
            mode_text = ", ".join(
                f"{m}: {names['modes'][m - 1]}"
                for m in mode_ids
            ) or "—"

            out.append(
                f"| {i + 1} | {names['profiles'][i]} | "
                f"{'Yes' if i + 1 in enabled else 'No'} | "
                f"{f'0x{mask:02X}' if mask is not None else '—'} | "
                f"{mode_text} |"
            )

        out.append("")

        if state["id_type"] is not None:
            value = state["id_type"]
            text = "JSM" if value == 1 else str(value)
            out.append(
                f"- Input Device Type raw: `{value}`"
                + (f" = **{text}**" if value == 1 else "")
            )

        if state["id_subtype"] is not None:
            value = state["id_subtype"]
            text = "All" if value == 0 else str(value)
            out.append(
                f"- Input Device Subtype raw: `{value}`"
                + (f" = **{text}**" if value == 0 else "")
            )

        out.append("")

    write_definition_table(out, entries, "Profile Management", locmap)


def write_speed_values(out, entries, blocks, names):
    current = pm_profile_current_values(blocks, entries)
    if not current:
        return

    out.append("### Current profile values")
    out.append("")
    out.append(
        "| Parameter | "
        + " | ".join(
            f"{i + 1}: {names['profiles'][i]}"
            for i in range(PROFILE_COUNT)
        )
        + " |"
    )
    out.append("|---|" + "|".join(["---:"] * PROFILE_COUNT) + "|")

    for row in current["rows"]:
        entry = row["entry"]
        label = entry["display"] if entry else row["code"]
        values = [
            "—" if v is None else str(v)
            for v in row["values"]
        ]
        out.append(f"| {label} | " + " | ".join(values) + " |")

    out.append("")
    out.append(
        f"PM profile records begin at `01000100:0x{current['base']:04X}`; "
        f"8 records × {PM_PROFILE_RECORD_SIZE} bytes."
    )
    out.append("")


def write_definition_table(out, entries, section, locmap):
    rows = section_entries(entries, section)

    out.append(f"### All RND parameters ({len(rows)})")
    out.append("")
    out.append(
        "| RND code | Parameter | Subgroup | Range | Default | Unit | NV locator |"
    )
    out.append("|---|---|---|---|---:|---|---|")

    for e in rows:
        subgroup = (
            e["category"].split("~", 1)[1]
            if "~" in e["category"]
            else "—"
        )

        default = (
            "—"
            if e["minimum"] == e["maximum"] == e["default"] == 0
            else str(e["default"])
        )

        unit = e["unit"] if e["unit"] not in ("", "None") else "—"

        out.append(
            f"| `{e['code']}` | {e['display'] or e['canonical']} | "
            f"{subgroup} | {range_text(e)} | {default} | {unit} | "
            f"{locator_text(locmap.get(e['code'], []))} |"
        )

    out.append("")


def make_report(rnd_path: Path, blocks, source_path: Path, archive_meta=None):
    rnd = rnd_path.read_bytes()
    entries = parse_dictionary(rnd)
    layout = parse_file_layout(rnd)
    locators = parse_nv_locators(rnd, entries, set(blocks))
    locmap = locators_by_code(locators)
    names = extract_names(blocks)

    out = []
    out.append("# R-Net complete configuration map")
    out.append("")
    out.append(f"- Source: `{source_path.name}`")
    out.append(f"- RND: `{rnd_path.name}`")
    out.append(f"- Repository blocks available: **{len(blocks)}**")
    out.append(f"- Parsed RND dictionary parameters: **{len(entries)}**")
    out.append(f"- Confirmed ordinary NV locators: **{len(locators)}**")

    if archive_meta:
        out.append(
            f"- `.R-net` repository record chain: "
            f"`0x{archive_meta['record_chain_offset']:X}` .. "
            f"`0x{archive_meta['record_chain_end']:X}` "
            f"({archive_meta['record_count']} records)"
        )

    out.append("")
    out.append(
        "> `NV locator` gives the selector and the logical RND NV offset. "
        "A dash does not mean the parameter is absent: several Programmer "
        "families (Configuration, Mouse/iDevice and other packed tables) use "
        "special/derived layouts rather than the ordinary 49-byte NV locator table."
    )
    out.append("")

    write_repository_table(out, blocks, layout)

    # Follow the visible Programmer tree exactly.
    write_profile_management(out, entries, blocks, locmap, names)

    for section in SECTIONS[1:]:
        out.append(f"## {section}")
        out.append("")

        if section == "Speeds":
            write_speed_values(out, entries, blocks, names)

        write_definition_table(out, entries, section, locmap)

    out.append("## Notes")
    out.append("")
    out.append(
        "- The report includes **all RND dictionary definitions** under the "
        "13 requested Programmer tree sections, not only parameters currently "
        "visible under a particular access level."
    )
    out.append(
        "- Current profile speed values are decoded directly from selector "
        "`01000100`."
    )
    out.append(
        "- Profile/Mode names and enable masks are decoded directly from "
        "`06000100` and `02000000`."
    )
    out.append(
        "- Parameters without an ordinary NV locator remain listed with their "
        "RND code, full path, limits, default and unit; their packed byte layout "
        "is intentionally not guessed."
    )
    out.append("")

    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(
        description="Convert PGDT R-Net binary/configuration data to Markdown."
    )
    ap.add_argument("input", type=Path, help="*.R-net file or RNB2 state .bin")
    ap.add_argument(
        "output",
        nargs="?",
        type=Path,
        help="Markdown output path (default: input basename + .md)",
    )
    ap.add_argument(
        "--rnd",
        type=Path,
        help="decrypted Generic_V33_1_1245.dec.bin",
    )
    ap.add_argument(
        "--base",
        type=Path,
        help="optional saved *.R-net baseline when INPUT is an RNB2 overlay",
    )

    args = ap.parse_args()

    rnd_path = args.rnd or locate_default_rnd()
    if rnd_path is None or not rnd_path.is_file():
        ap.error(
            "decrypted RND not found; use --rnd Generic_V33_1_1245.dec.bin"
        )

    if not args.input.is_file():
        ap.error(f"input file not found: {args.input}")

    rnd = rnd_path.read_bytes()
    layout = parse_file_layout(rnd)
    raw = args.input.read_bytes()

    archive_meta = None

    if raw[:4] == b"RNB2":
        overlay = read_rnb2(args.input)

        if args.base:
            base, archive_meta = parse_rnet_archive(args.base, layout)
            blocks = merge_blocks(base, overlay)
        else:
            blocks = overlay
    else:
        blocks, archive_meta = parse_rnet_archive(args.input, layout)

    output = args.output or args.input.with_suffix(".md")
    report = make_report(
        rnd_path,
        blocks,
        args.input,
        archive_meta=archive_meta,
    )

    output.write_text(report, encoding="utf-8")

    print(f"RND:    {rnd_path}")
    print(f"Input:  {args.input}")
    print(f"Blocks: {len(blocks)}")
    print(f"Report: {output}")
    print("OK")


if __name__ == "__main__":
    main()
