#!/usr/bin/env python3
"""
Lightweight validator for Amiga register headers and generated CSVs.

Checks:
  - duplicate #define symbols across headers
  - duplicate register names across per-chip CSVs
  - basic address sanity (custom registers in 0xDFFxxx, CIAs in 0xBFD/BFE)
"""
from __future__ import annotations

import csv
import re
import sys
from pathlib import Path
from typing import Dict, List, Set, Tuple

ROOT = Path(__file__).resolve().parent.parent
REGDIR = ROOT / "src" / "platforms" / "amiga" / "registers"
CSVDIR = ROOT / "docs" / "register_maps"


def parse_header_symbols() -> Tuple[Dict[str, Path], List[str]]:
    seen: Dict[str, Path] = {}
    dupes: List[str] = []
    define_re = re.compile(r"^#define\s+([A-Za-z0-9_]+)\s+\(")
    for hdr in sorted(REGDIR.glob("*.h")):
        for line in hdr.read_text().splitlines():
            m = define_re.match(line.strip())
            if not m:
                continue
            name = m.group(1)
            if name in seen:
                dupes.append(f"{name}: {hdr} (already in {seen[name]})")
            else:
                seen[name] = hdr
    return seen, dupes


def parse_csv_registers() -> Tuple[Dict[str, str], List[str]]:
    seen: Dict[str, str] = {}
    dupes: List[str] = []
    for csv_path in sorted(CSVDIR.glob("*.csv")):
        if csv_path.name.startswith("master"):
            continue
        with csv_path.open() as f:
            reader = csv.DictReader(f)
            for row in reader:
                name = row.get("register")
                if not name:
                    continue
                if name in seen:
                    dupes.append(f"{name}: {csv_path} (already in {seen[name]})")
                else:
                    seen[name] = csv_path.name
    return seen, dupes


def address_sanity(csv_path: Path) -> List[str]:
    errors: List[str] = []
    with csv_path.open() as f:
        reader = csv.DictReader(f)
        for row in reader:
            addr = row.get("address", "")
            chip = row.get("chip", "")
            if not addr:
                continue
            try:
                val = int(addr, 0)
            except ValueError:
                errors.append(f"{csv_path}: bad address {addr}")
                continue
            if chip == "cia":
                if not (0xBFD000 <= val <= 0xBFEFFF):
                    errors.append(f"{csv_path}: CIA address out of range {addr}")
            else:
                if chip != "amiga_custom_chips" and not (0xDFF000 <= val <= 0xDFFFFF):
                    errors.append(f"{csv_path}: custom address out of range {addr} ({chip})")
    return errors


def main() -> int:
    _, hdr_dupes = parse_header_symbols()
    _, csv_dupes = parse_csv_registers()
    addr_errors = address_sanity(CSVDIR / "master_registers.csv")

    if hdr_dupes:
        print("Header duplicate symbols:")
        for d in hdr_dupes:
            print(f"  {d}")
    if csv_dupes:
        print("CSV duplicate registers:")
        for d in csv_dupes:
            print(f"  {d}")
    if addr_errors:
        print("Address range errors:")
        for e in addr_errors:
            print(f"  {e}")

    if hdr_dupes or csv_dupes or addr_errors:
        return 1
    print("reglint: OK (no duplicate symbols or address errors)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
