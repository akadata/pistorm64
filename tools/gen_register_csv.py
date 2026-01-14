#!/usr/bin/env python3
"""
Generate chip-specific and master register CSVs from the C headers.

This is a first pass: it parses #define registers in src/platforms/amiga/registers/*.h,
evaluates addresses where a BASE macro is available, and emits per-chip CSVs plus
master_registers.csv in docs/register_maps/.
"""
from __future__ import annotations

import ast
import csv
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

ROOT = Path(__file__).resolve().parent.parent
REGDIR = ROOT / "src" / "platforms" / "amiga" / "registers"
OUTDIR = ROOT / "docs" / "register_maps"
TXT_ROOT = ROOT / "Hardware" / "ADCD_2.1" / "_txt"
BASES_HEADER = REGDIR / "amiga_custom_chips.h"

# Chipset variant annotations (minimal, extend as needed)
CHIPSET_VARIANTS = {
    "agnus": "OCS/ECS (8371/8372A), Fatter Agnus 8375 (2MB); AGA Alice (2MB, 8 bitplanes)",
    "paula": "OCS/ECS Paula; AA/AGA Paula (same register set)",
    "denise": "OCS Denise / Super Denise; AGA Lisa (extended palette/bitplanes)",
    "cia": "8520 CIA-A/B (OCS/ECS/AGA)",
    "blitter": "OCS/ECS blitter (AGA: Alice-integrated blitter)",
    "amiga_custom_chips": "Aggregate custom chip header",
}

# very lightweight mapping of name prefixes to manual nodes for hints; extend as needed
MANUAL_HINTS = {
    "AUD": ["node00DB", "node00E0", "node00F2"],
    "DMACON": ["node002F"],
    "INTENA": ["node0036"],
    "INTREQ": ["node0037"],
    "ADKCON": ["node0036"],
    "DSK": ["node0030", "node0031", "node0032"],
    "COP": ["node0056", "node004B"],
    "BLT": ["node001B", "node001F"],
    "BPL": ["node0036", "node004C"],
    "SPR": ["node003F"],
    "CIA": ["node00D3"],  # memory map reference; CIA chapters live in includes/autodocs
    "JOY": ["node0038", "node017E", "node0180", "node0183"],
    "POT": ["node003B", "node0186", "node0187", "node018B"],
    "CLX": ["node0025", "node015D"],
    "DIW": ["node0083", "node0071"],
    "DDF": ["node0083"],
    "COLOR": ["node0036"],  # palette summary in register list
    "VPOS": ["node015F", "node004B", "node004C", "node004D"],
    "VHPOS": ["node015F", "node004B", "node004C", "node004D"],
    "STR": ["node004B", "node0059"],
    "VARBEAM": ["node004C"],
    "BEAMCON": ["node004B"],
    "HTOTAL": ["node004C", "node004D"],
    "HCENTER": ["node004C"],
    "HBSTRT": ["node004C", "node004D"],
    "HBSTOP": ["node004C", "node004D"],
    "HSSTRT": ["node004C", "node004D"],
    "HSSTOP": ["node004C", "node004D"],
    "VTOTAL": ["node015F", "node004B", "node004D"],
    "VBSTRT": ["node015F", "node004B", "node004D"],
    "VBSTOP": ["node015F", "node004B", "node004D"],
    "VSSTRT": ["node015F", "node004B", "node004D"],
    "VSSTOP": ["node015F", "node004B", "node004D"],
    "REFPTR": ["node0060"],
    "DENISEID": ["node0060"],
    "SER": ["node003D", "node01A3"],
    "CUSTOM_CHIP_SIZE": ["node0060"],
}

# Canonical register ownership for shared addresses (CSV generation only)
REGISTER_OWNER = {
    "DMACON": "agnus",
    "DMACONR": "agnus",
    "JOY0DAT": "denise",
    "JOY1DAT": "denise",
    "CLXDAT": "denise",
    "CLXCON": "denise",
}

# Per-register chipset revision flags (ECS/AGA/OCS notes)
REGISTER_VARIANTS = {
    # ECS/AGA-only beam/timing controls
    "BEAMCON0": "ECS/AGA (variable beam counter, SHRES/PAL bits)",
    "HTOTAL": "ECS/AGA only",
    "HSSTOP": "ECS/AGA only",
    "HBSTRT": "ECS/AGA only",
    "HBSTOP": "ECS/AGA only",
    "VTOTAL": "ECS/AGA only",
    "VSSTOP": "ECS/AGA only",
    "VBSTRT": "ECS/AGA only",
    "VBSTOP": "ECS/AGA only",
    "HSSTRT": "ECS/AGA only",
    "VSSTRT": "ECS/AGA only",
    "HCENTER": "ECS/AGA only",
    "DIWHIGH": "ECS/AGA only",
    # Extended/ID registers
    "DENISEID": "ECS/AGA (Lisa reports AGA revision)",
    # Extended priority/control
    "BPLCON3": "ECS/AGA (extra color/bitplane control)",
    # Copper control with ECS danger bit
    "COPCON": "OCS/ECS/AGA (CDANG only on ECS/AGA)",
}

CHIPSET_REV_HINTS = {
    re.compile(r"^COLOR"): "OCS/ECS 12-bit palette; AGA/Lisa uses 24-bit palette and extended color registers.",
    re.compile(r"^BPLCON0"): "OCS/ECS/AGA; AGA adds 8-bitplanes/HAM8/extra bits.",
    re.compile(r"^BPLCON2"): "OCS/ECS; AGA extends priority bits for 8 bitplanes.",
    re.compile(r"^AUD[0-3]LCH"): "OCS/ECS/AGA; ECS/AGA widen high address bits (5 vs 3).",
    re.compile(r"^DSKPTH"): "OCS/ECS/AGA; ECS/AGA widen high address bits (5 vs 3).",
}

# Simple interrupt/notes hints
INT_HINTS = {
    re.compile(r"^AUD([0-3])"): lambda m: f"INTF_AUD{m.group(1)}",
    re.compile(r"^VERTB"): lambda m: "INTF_VERTB",
    re.compile(r"^COP"): lambda m: "INTF_COPPER",
    re.compile(r"^BLT"): lambda m: "INTF_BLIT",
    re.compile(r"^DSK"): lambda m: "INTF_DSKBLK",
    re.compile(r"^RBF"): lambda m: "INTF_RBF",
    re.compile(r"^TBE"): lambda m: "INTF_TBE",
    re.compile(r"^EXTER"): lambda m: "INTF_EXTER",
}

NOTE_HINTS = {
    re.compile(r"^AUD"): "Audio channel; length in words; period >=123 PAL/124 NTSC; even addresses.",
    re.compile(r"^DMACON"): "Set/clear style; master DMA + per-channel bits.",
    re.compile(r"^INTEN"): "Interrupt enable; set/clear style; master bit 15.",
    re.compile(r"^INTREQ"): "Interrupt request; set/clear style; must clear explicitly.",
    re.compile(r"^ADKCON"): "Audio/disk control; set/clear style.",
    re.compile(r"^CIAA"): "CIAA 8520 (odd addresses, low byte lane); 8-bit registers; avoid 16-bit writes.",
    re.compile(r"^CIAB"): "CIAB 8520 (even addresses, high byte lane); 8-bit registers; avoid 16-bit writes.",
    re.compile(r"^CIA"): "CIA 8520; 8-bit registers; odd/even address lanes; avoid 16-bit writes.",
    re.compile(r"^DSK"): "Disk DMA; length in words; write bit sets direction.",
    re.compile(r"^SER"): "Serial port; period/control or data register.",
    re.compile(r"^POT"): "Proportional controllers; write POTGO to start counters; read during vblank; shared with mouse buttons/light pen.",
    re.compile(r"^JOY"): "Joystick/mouse quadrature counters; XOR low bits for fwd/back; also used for light pen position latch.",
    re.compile(r"^COP"): "Copper pointer/control; even addresses; DMA-controlled.",
    re.compile(r"^BLT"): "Blitter control/pointers; use minterms and modulos carefully.",
    re.compile(r"^BPL"): "Bitplane pointers/modulos; even addresses; DMA fetch (ECS: up to 6 planes; AGA: up to 8).",
    re.compile(r"^SPR"): "Sprite pointers/data; even addresses; DMA fetch during hblank.",
    re.compile(r"^DIW"): "Display window start/stop; lowres coords; even addresses; interlace/overscan interactions.",
    re.compile(r"^COLOR"): "Palette color registers; write-only; 12-bit RGB (OCS/ECS).",
    re.compile(r"^CLX"): "Collision registers; Denise/Paula shared space; read clears.",
    re.compile(r"^BPLCON"): "Bitplane control; ECS/AGA bits differ (e.g., HAM8, 8 bitplanes).",
    re.compile(r"^BPLMOD"): "Modulo; per-plane strides (ECS/AGA identical).",
    re.compile(r"^DDF"): "Data fetch start/stop; align to color clocks; AGA has extended fetch range.",
    re.compile(r"^VPOS"): "Beam position counter high bits; LOF interlace flag; used by Copper waits/skips and light pen.",
    re.compile(r"^VHPOS"): "Beam position counter low bits; used by Copper waits/skips and light pen; horizontal resolution 1/160 screen.",
    re.compile(r"^(H(TOTAL|SSTOP|BSTRT|BSTOP|SSTRT|CENTER)|V(TOTAL|SSTOP|BSTRT|BSTOP|SSTRT))"): "ECS/AGA beam-timing registers; use with BEAMCON0 VARBEAMEN/VARHSY/VARVSY to adjust sync/blanking.",
    re.compile(r"^BEAM"): "Beam control (ECS/AGA): PAL bit, super-hires, variable beam counter enable.",
    re.compile(r"^STR"): "Horizontal/vertical sync strobes; write-only timing strobes.",
    re.compile(r"^REFPTR"): "DRAM refresh pointer; driven by Agnus refresh logic.",
    re.compile(r"^DENISEID"): "Chip revision ID (Denise/Super Denise/Lisa).",
    re.compile(r"^CUSTOM_CHIP_SIZE"): "Size of custom chip register aperture.",
}


@dataclass
class Register:
    name: str
    addr: Optional[int]
    expr: str
    chip: str
    io: str = "chip-register"
    interrupt: str = ""
    relationship: str = ""
    notes: str = ""
    manual_nodes: List[str] = field(default_factory=list)
    chipset_rev: str = ""
    width_bits: int = 16
    access: str = "rw"  # default: read/write unless refined


def parse_header(path: Path, global_macros: Dict[str, int]) -> List[Register]:
    chip = path.stem
    text = path.read_text()
    # Collect base values
    bases: Dict[str, int] = dict(global_macros)
    base_re = re.compile(r"#define\s+([A-Za-z0-9_]*BASE)\s+(0x[0-9A-Fa-f]+|\d+)")
    for m in base_re.finditer(text):
        bases[m.group(1)] = int(m.group(2), 0)
    # Collect simple numeric constants for substitution (e.g., CIAPRA offsets)
    consts: Dict[str, int] = dict(global_macros)
    const_re = re.compile(r"#define\s+([A-Za-z0-9_]+)\s+(0x[0-9A-Fa-f]+|\d+)\b")
    for m in const_re.finditer(text):
        name, val = m.group(1), m.group(2)
        if name.endswith("BASE"):
            continue
        consts[name] = int(val, 0)

    reg_re = re.compile(r"#define\s+([A-Za-z0-9_]+)\s+\(([^)]+)\)")
    regs: List[Register] = []
    for m in reg_re.finditer(text):
        name, expr = m.group(1), m.group(2).strip()
        if name.endswith("BASE"):
            continue
        owner = REGISTER_OWNER.get(name)
        if owner and owner != chip:
            continue
        addr = evaluate_expr(expr, bases, consts)
        reg = Register(
            name=name,
            addr=addr,
            expr=expr,
            chip=chip,
            manual_nodes=manual_hints_for(name),
            chipset_rev=REGISTER_VARIANTS.get(name, ""),
            width_bits=default_width_for(name),
            access=default_access_for(name),
        )
        apply_hints(reg)
        regs.append(reg)
    return regs


def evaluate_expr(expr: str, bases: Dict[str, int], consts: Dict[str, int]) -> Optional[int]:
    try_expr = expr
    for base, value in bases.items():
        try_expr = re.sub(rf"\b{re.escape(base)}\b", hex(value), try_expr)
    for name, value in consts.items():
        try_expr = re.sub(rf"\b{re.escape(name)}\b", hex(value), try_expr)
    # strip casts and extraneous whitespace
    try_expr = try_expr.replace("U", "")
    try_expr = try_expr.replace("u", "")
    try_expr = try_expr.replace("L", "")
    try_expr = try_expr.replace("l", "")
    try_expr = try_expr.strip()
    # simple safety: allow hex, decimal, +, -, parentheses
    if not re.match(r"^[0-9xXa-fA-F\+\-\s\(\)]+$", try_expr):
        return None
    try:
        return ast.literal_eval(try_expr)
    except Exception:
        try:
            return int(eval(try_expr, {"__builtins__": {}}))
        except Exception:
            return None


def write_csv(path: Path, rows: List[Register]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow([
            "register",
            "address",
            "chip",
            "chipset_variant",
            "chipset_revisions",
            "width_bits",
            "access",
            "io",
            "interrupt",
            "relationship_to_other_chips",
            "notes",
            "manual_nodes",
        ])
        for r in rows:
            addr_str = f"0x{r.addr:06X}" if r.addr is not None else ""
            writer.writerow([
                r.name,
                addr_str,
                r.chip,
                CHIPSET_VARIANTS.get(r.chip, ""),
                r.chipset_rev,
                r.width_bits,
                r.access,
                r.io,
                r.interrupt,
                r.relationship,
                r.notes,
                ";".join(r.manual_nodes),
            ])


def main() -> int:
    global_macros = load_global_macros()
    headers = sorted(REGDIR.glob("*.h"))
    all_regs: List[Register] = []
    for hdr in headers:
        if hdr.name == BASES_HEADER.name:
            continue
        regs = parse_header(hdr, global_macros)
        if regs:
          write_csv(OUTDIR / f"{hdr.stem}.csv", regs)
          all_regs.extend(regs)
    # master by chip
    write_csv(OUTDIR / "master_registers.csv", sorted(all_regs, key=lambda r: (r.chip, r.addr or 0)))
    # master by address (resource map)
    write_csv(OUTDIR / "master_by_address.csv", sorted(all_regs, key=lambda r: (r.addr or 0, r.chip, r.name)))
    # overlap report
    overlaps = find_overlaps(all_regs)
    if overlaps:
        write_csv(OUTDIR / "overlaps.csv", overlaps)
    else:
        overlap_path = OUTDIR / "overlaps.csv"
        if overlap_path.exists():
            overlap_path.unlink()
    print(f"Wrote per-chip CSVs and master maps to {OUTDIR}")
    return 0


def manual_hints_for(name: str) -> List[str]:
    hints = []
    for prefix, nodes in MANUAL_HINTS.items():
        if name.startswith(prefix):
            hints.extend(nodes)
    return hints


def apply_hints(reg: Register) -> None:
    for pat, fn in INT_HINTS.items():
        m = pat.match(reg.name)
        if m:
            reg.interrupt = fn(m)
            break
    if not reg.chipset_rev:
        for pat, text in CHIPSET_REV_HINTS.items():
            if pat.match(reg.name):
                reg.chipset_rev = text
                break
    for pat, note in NOTE_HINTS.items():
        if pat.match(reg.name):
            reg.notes = note
            break
    # simple relationship hints
    if reg.name.startswith("AUD"):
        reg.relationship = "Paula audio DMA via Agnus"
    elif reg.name.startswith("DSK"):
        reg.relationship = "Paula disk DMA via Agnus"
    elif reg.name.startswith("BPL") or reg.name.startswith("SPR") or reg.name.startswith("BLT"):
        reg.relationship = "Agnus DMA"
    elif reg.name.startswith("CIA"):
        reg.relationship = "CIA-A/B (8520); interrupts into Paula"
    elif reg.name.startswith("COP"):
        reg.relationship = "Copper via Agnus"
    elif reg.name.startswith("COLOR"):
        reg.relationship = "Denise palette"
    # Fallback chipset revision text if still empty
    if not reg.chipset_rev:
        reg.chipset_rev = f"{CHIPSET_VARIANTS.get(reg.chip, reg.chip)} (common)"


def find_overlaps(regs: List[Register]) -> List[Register]:
    by_addr: Dict[int, List[Register]] = {}
    for r in regs:
        if r.addr is None:
            continue
        by_addr.setdefault(r.addr, []).append(r)
    overlaps: List[Register] = []
    for addr, items in by_addr.items():
        chips = {r.chip for r in items}
        if len(items) > 1 and len(chips) > 1:
            overlaps.extend(sorted(items, key=lambda r: (r.addr or 0, r.chip, r.name)))
    return sorted(overlaps, key=lambda r: (r.addr or 0, r.chip, r.name))


def default_width_for(name: str) -> int:
    # Pointers and data registers are word-wide, but long writes are allowed; keep 16 as default.
    return 16


def default_access_for(name: str) -> str:
    # Without per-register read/write metadata, mark as read/write by default.
    return "rw"


def load_global_macros() -> Dict[str, int]:
    macros: Dict[str, int] = {}
    if not BASES_HEADER.is_file():
        return macros
    define_re = re.compile(r"#define\s+([A-Za-z0-9_]+)\s+(.+)")
    lines = BASES_HEADER.read_text().splitlines()
    for line in lines:
        m = define_re.match(line.strip())
        if not m:
            continue
        name, expr = m.group(1), m.group(2)
        val = evaluate_expr(expr, macros, macros)
        if val is not None:
            macros[name] = val
    return macros


if __name__ == "__main__":
    raise SystemExit(main())
