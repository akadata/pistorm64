#!/usr/bin/env python3
"""
adfmanager.py - Manage A314 disk service (disk.py) images via control port.

Features:
- CLI: list / insert / eject
- Web UI: --web (simple HTML + JSON endpoints)
- No external Python deps; uses stdlib only.

Defaults:
- ADF_DIR=/home/smalley/Amiga/adf
- HOST=127.0.0.1
- PORT=23890
"""

from __future__ import annotations

import argparse
import html
import json
import os
import re
import shutil
import socket
import subprocess
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

DEFAULT_ADF_DIR = "/home/smalley/Amiga/adf"
DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 23890
DEFAULT_CFG_PATH = "/home/smalley/pistorm64/default.cfg"
DEFAULT_CFG_DIR = "/home/smalley/pistorm64"
DEFAULT_KICK_DIR = "/home/smalley/Amiga/kick"
DEFAULT_HDF_DIR = "/home/smalley/Amiga/hdf"
DEFAULT_CSS_PATH = "/home/smalley/pistorm/web/adf.css"
DEFAULT_XDFTOOL = "/usr/local/bin/xdftool"
DEFAULT_A314_CONF = "/home/smalley/pistorm64/src/a314/files_pi/a314d.conf"


def send_cmd(host: str, port: int, cmd: str, timeout: float = 2.0) -> str:
    """
    Send a single command line to the disk service control socket and return response.
    Always terminates the command with '\n' and reads until socket close.
    """
    if not cmd.endswith("\n"):
        cmd += "\n"

    data_out = cmd.encode("utf-8", errors="strict")
    chunks: list[bytes] = []

    try:
        with socket.create_connection((host, port), timeout=timeout) as s:
            s.settimeout(timeout)
            s.sendall(data_out)
            # disk.py typically closes after responding; read until close/timeout
            while True:
                try:
                    b = s.recv(4096)
                except socket.timeout:
                    break
                if not b:
                    break
                chunks.append(b)
    except OSError as exc:
        return f"error: {exc}"

    return b"".join(chunks).decode("utf-8", errors="replace").strip()


def list_images(adf_dir: Path) -> list[dict]:
    exts = {".adf", ".hdf"}
    out = []
    if not adf_dir.exists():
        return out

    for p in sorted(adf_dir.rglob("*")):
        if not p.is_file():
            continue
        if p.suffix.lower() not in exts:
            continue
        try:
            st = p.stat()
            rel = p.relative_to(adf_dir).as_posix()
            group = rel.split("/", 1)[0] if "/" in rel else "root"
            out.append(
                {
                    "name": p.name,
                    "relpath": rel,
                    "group": group,
                    "path": str(p),
                    "size": st.st_size,
                }
            )
        except FileNotFoundError:
            continue
    return out


def human_size(n: int) -> str:
    # simple, no decimals unless needed
    units = ["B", "KiB", "MiB", "GiB", "TiB"]
    v = float(n)
    u = 0
    while v >= 1024.0 and u < len(units) - 1:
        v /= 1024.0
        u += 1
    if u == 0:
        return f"{int(v)} {units[u]}"
    return f"{v:.1f} {units[u]}"


def get_status(host: str, port: int) -> dict | None:
    resp = send_cmd(host, port, "status")
    if not resp or resp.startswith("error:"):
        return None
    try:
        return json.loads(resp)
    except json.JSONDecodeError:
        return None


def pick_unit(preferred: int | None, status: dict | None) -> int | None:
    if not status:
        return preferred if preferred is not None else 0
    units = status.get("units", [])
    used = {u.get("unit") for u in units if u.get("present")}
    if preferred is not None and preferred not in used:
        return preferred
    for i in range(4):
        if i not in used:
            return i
    return None


def xdftool_path() -> str | None:
    return shutil.which("xdftool") or (DEFAULT_XDFTOOL if os.path.exists(DEFAULT_XDFTOOL) else None)


def run_cmd(args: list[str]) -> tuple[int, str]:
    res = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
    return res.returncode, res.stdout.strip()


def create_blank_adf(dest: Path, volume: str = "BLANK", force: bool = False) -> tuple[bool, str]:
    if dest.exists() and not force:
        return False, f"exists: {dest}"
    tool = xdftool_path()
    if not tool:
        return False, "xdftool not found"
    if dest.exists() and force:
        dest.unlink()
    code, out = run_cmd([tool, str(dest), "create"])
    if code != 0:
        return False, out or "xdftool create failed"
    code, out = run_cmd([tool, str(dest), "format", volume])
    if code != 0:
        return False, out or "xdftool format failed"
    return True, f"created {dest}"


def clone_image(src: Path, dest: Path, force: bool = False) -> tuple[bool, str]:
    if not src.exists():
        return False, f"not found: {src}"
    if dest.exists() and not force:
        return False, f"exists: {dest}"
    if dest.exists() and force:
        dest.unlink()
    shutil.copy2(src, dest)
    return True, f"cloned to {dest}"


def list_dir_files(path: str, exts: set[str]) -> list[str]:
    p = Path(path)
    if not p.exists():
        return []
    items = []
    for entry in sorted(p.iterdir()):
        if entry.is_file() and entry.suffix.lower() in exts:
            items.append(entry.name)
    return items


def list_cfg_files(path: str) -> list[str]:
    return list_dir_files(path, {".cfg"})


def safe_cfg_name(name: str) -> str | None:
    base = os.path.basename(name)
    if base != name or not base.endswith(".cfg"):
        return None
    return base


CPU_RE = re.compile(r"^\s*cpu\s+(\S+)", re.IGNORECASE)
KICK_RE = re.compile(r"^\s*map\s+.*\bid=kickstart\b", re.IGNORECASE)
SIZE_RE = re.compile(r"\bsize=([0-9]+)([KMG])\b", re.IGNORECASE)
FILE_RE = re.compile(r"\bfile=([^\s]+)", re.IGNORECASE)
CPU_SLOT_RE = re.compile(r"^\s*map\s+.*\bid=cpu_slot_ram\b", re.IGNORECASE)
Z2_RE = re.compile(r"^\s*map\s+.*\bid=z2_autoconf_fast\b", re.IGNORECASE)
Z3_RE = re.compile(r"^\s*map\s+.*\bid=z3_autoconf_fast\b", re.IGNORECASE)
CHIP_RE = re.compile(r"^\s*map\s+.*\baddress=0x0\b.*\bsize=2M\b", re.IGNORECASE)
LOOPCYCLES_RE = re.compile(r"^\s*loopcycles\s+([0-9]+)", re.IGNORECASE)
PLATFORM_RE = re.compile(r"^\s*platform\s+(\S+)(?:\s+(\S+))?", re.IGNORECASE)
KEYBOARD_RE = re.compile(r"^\s*keyboard\s+(\S+)(?:\s+(\S+))?(?:\s+(\S+))?", re.IGNORECASE)
MOUSE_RE = re.compile(r"^\s*mouse\s+(\S+)(?:\s+(\S+))?(?:\s+(\S+))?", re.IGNORECASE)
KBFILE_RE = re.compile(r"^\s*kbfile\s+(\S+)", re.IGNORECASE)
SETVAR_RE = re.compile(r"^\s*setvar\s+(\S+)(?:\s+(.+))?$", re.IGNORECASE)
PISCSI_RE = re.compile(r"^\s*setvar\s+piscsi([0-6])\s+(.+)$", re.IGNORECASE)
PISCSI_ENABLE_RE = re.compile(r"^\s*setvar\s+piscsi\b", re.IGNORECASE)


def _strip_comment(line: str) -> tuple[str, bool]:
    stripped = line.lstrip()
    if stripped.startswith("#"):
        return stripped[1:].lstrip(), True
    return stripped, False


def _comment_line(line: str) -> str:
    if line.lstrip().startswith("#"):
        return line
    return "#" + line


def _uncomment_line(line: str) -> str:
    if not line.lstrip().startswith("#"):
        return line
    idx = line.find("#")
    pre = line[:idx]
    rest = line[idx + 1 :]
    if rest.startswith(" "):
        rest = rest[1:]
    return pre + rest


def _replace_file(line: str, new_path: str) -> str:
    if "file=" in line:
        return FILE_RE.sub(f"file={new_path}", line)
    return line.rstrip() + f" file={new_path}\n"


def _replace_size(line: str, size_token: str) -> str:
    if "size=" in line:
        return SIZE_RE.sub(f"size={size_token}", line)
    return line.rstrip() + f" size={size_token}\n"


def _setvar_line(name: str, val: str | None) -> str:
    if val is None or val == "":
        return f"setvar {name}\n"
    return f"setvar {name} {val}\n"


def _parse_size_mb(line: str) -> int:
    m = SIZE_RE.search(line)
    if not m:
        return 0
    value = int(m.group(1))
    unit = m.group(2).upper()
    if unit == "K":
        return value // 1024
    if unit == "G":
        return value * 1024
    return value


def parse_default_cfg(path: str) -> dict:
    data = {
        "cpu": None,
        "kickstart": None,
        "cpu_slot_mb": 0,
        "z2_mb": 0,
        "z3_mb": 0,
        "chip_ram": False,
        "loopcycles": 300,
        "platform": None,
        "rtg": False,
        "rtg_dpms": False,
        "rtg_width": 0,
        "rtg_height": 0,
        "pi_ahi": False,
        "pi_ahi_device": "",
        "pi_ahi_samplerate": 0,
        "cdtv": False,
        "enable_rtc_emulation": None,
        "pi_net": False,
        "a314": True,
        "a314_conf": DEFAULT_A314_CONF,
        "move_slow_to_chip": False,
        "swap_df0_df": 0,
        "physical_z2_first": False,
        "kick13": False,
        "no_pistorm_dev": False,
        "keyboard": {"enabled": False, "key": "k", "grab": False, "autoconnect": False},
        "kbfile": "",
        "mouse": {"enabled": False, "file": "", "key": "m", "autoconnect": False},
        "piscsi_enable": False,
        "piscsi": {str(i): "" for i in range(7)},
    }
    p = Path(path)
    if not p.exists():
        return data

    for line in p.read_text(errors="replace").splitlines():
        raw, commented = _strip_comment(line)
        if not raw:
            continue

        m = CPU_RE.match(raw)
        if m and not commented:
            data["cpu"] = m.group(1)

        if KICK_RE.match(raw) and not commented:
            mf = FILE_RE.search(raw)
            if mf:
                data["kickstart"] = mf.group(1)

        if CPU_SLOT_RE.match(raw):
            mb = _parse_size_mb(raw)
            if mb:
                data["cpu_slot_mb"] = mb

        if Z2_RE.match(raw) and not commented:
            data["z2_mb"] = _parse_size_mb(raw)

        if Z3_RE.match(raw) and not commented:
            data["z3_mb"] = _parse_size_mb(raw)

        if CHIP_RE.match(raw) and not commented:
            data["chip_ram"] = True

        m = LOOPCYCLES_RE.match(raw)
        if m and not commented:
            data["loopcycles"] = int(m.group(1))

        m = PLATFORM_RE.match(raw)
        if m and not commented:
            data["platform"] = m.group(1)

        m = KEYBOARD_RE.match(raw)
        if m:
            data["keyboard"]["key"] = m.group(1)
            if m.group(2):
                data["keyboard"]["grab"] = m.group(2).lower() == "grab"
            if m.group(3):
                data["keyboard"]["autoconnect"] = m.group(3).lower() == "autoconnect"
            if not commented:
                data["keyboard"]["enabled"] = True

        m = MOUSE_RE.match(raw)
        if m:
            data["mouse"]["file"] = m.group(1)
            if m.group(2):
                data["mouse"]["key"] = m.group(2)
            if m.group(3):
                data["mouse"]["autoconnect"] = m.group(3).lower() == "autoconnect"
            if not commented:
                data["mouse"]["enabled"] = True

        m = KBFILE_RE.match(raw)
        if m and not commented:
            data["kbfile"] = m.group(1)

        if PISCSI_ENABLE_RE.match(raw) and not commented:
            data["piscsi_enable"] = True

        m = PISCSI_RE.match(raw)
        if m:
            unit = m.group(1)
            path_val = m.group(2).strip()
            if not commented:
                data["piscsi"][unit] = path_val

        m = SETVAR_RE.match(raw)
        if m:
            name = m.group(1).lower()
            val = (m.group(2) or "").strip()
            if name.startswith("piscsi"):
                continue
            if name == "rtg":
                if not commented:
                    data["rtg"] = True
            elif name == "rtg-dpms":
                if not commented:
                    data["rtg_dpms"] = True
            elif name == "rtg-width":
                if val.isdigit():
                    data["rtg_width"] = int(val)
            elif name == "rtg-height":
                if val.isdigit():
                    data["rtg_height"] = int(val)
            elif name == "pi-ahi":
                if not commented:
                    data["pi_ahi"] = True
                if val:
                    data["pi_ahi_device"] = val
            elif name == "pi-ahi-samplerate":
                if val.isdigit():
                    data["pi_ahi_samplerate"] = int(val)
            elif name == "cdtv":
                if not commented:
                    data["cdtv"] = True
            elif name == "enable_rtc_emulation":
                if val.isdigit():
                    data["enable_rtc_emulation"] = int(val)
            elif name == "pi-net":
                if not commented:
                    data["pi_net"] = True
            elif name == "a314":
                if not commented:
                    data["a314"] = True
            elif name == "a314_conf":
                if val:
                    data["a314_conf"] = val
            elif name == "move-slow-to-chip":
                if not commented:
                    data["move_slow_to_chip"] = True
            elif name == "swap-df0-df":
                if val.isdigit():
                    data["swap_df0_df"] = int(val)
            elif name == "physical-z2-first":
                if not commented:
                    data["physical_z2_first"] = True
            elif name == "kick13":
                if not commented:
                    data["kick13"] = True
            elif name == "no-pistorm-dev":
                if not commented:
                    data["no_pistorm_dev"] = True
    if not data.get("platform"):
        data["platform"] = "amiga"
    if not data.get("a314_conf"):
        data["a314_conf"] = DEFAULT_A314_CONF
    data["a314"] = True
    return data


def update_default_cfg(path: str, new_cfg: dict) -> tuple[bool, str]:
    p = Path(path)
    if not p.exists():
        return False, f"not found: {p}"

    if not new_cfg.get("platform"):
        new_cfg["platform"] = "amiga"
    if not new_cfg.get("a314_conf"):
        new_cfg["a314_conf"] = DEFAULT_A314_CONF
    new_cfg["a314"] = True

    lines = p.read_text(errors="replace").splitlines(keepends=True)
    out = []
    cpu_done = False
    kick_done = False
    cpu_slot_done = False
    z2_done = False
    z3_done = False
    chip_done = False
    loop_done = False
    platform_done = False
    keyboard_done = False
    mouse_done = False
    kbfile_done = False
    piscsi_enable_done = False
    piscsi_done = {str(i): False for i in range(7)}
    setvar_done = {
        "rtg": False,
        "rtg-dpms": False,
        "rtg-width": False,
        "rtg-height": False,
        "pi-ahi": False,
        "pi-ahi-samplerate": False,
        "cdtv": False,
        "enable_rtc_emulation": False,
        "pi-net": False,
        "a314_conf": False,
        "a314": False,
        "move-slow-to-chip": False,
        "swap-df0-df": False,
        "physical-z2-first": False,
        "kick13": False,
        "no-pistorm-dev": False,
    }

    for line in lines:
        raw, commented = _strip_comment(line)
        if CPU_RE.match(raw):
            if not cpu_done and new_cfg.get("cpu"):
                out.append(f"cpu {new_cfg['cpu']}\n")
                cpu_done = True
            else:
                out.append(_comment_line(line))
            continue

        if KICK_RE.match(raw):
            if not kick_done and new_cfg.get("kickstart"):
                updated = _replace_file(raw, new_cfg["kickstart"])
                out.append(_uncomment_line(updated + ("\n" if not updated.endswith("\n") else "")))
                kick_done = True
            else:
                out.append(_comment_line(line))
            continue

        if CPU_SLOT_RE.match(raw):
            if not cpu_slot_done and new_cfg.get("cpu_slot_mb", 0) > 0:
                size = f"{new_cfg['cpu_slot_mb']}M"
                updated = _replace_size(raw, size)
                out.append(_uncomment_line(updated + ("\n" if not updated.endswith("\n") else "")))
            else:
                out.append(_comment_line(line))
            cpu_slot_done = True
            continue

        if Z2_RE.match(raw):
            if not z2_done and new_cfg.get("z2_mb", 0) > 0:
                size = f"{new_cfg['z2_mb']}M"
                updated = _replace_size(raw, size)
                out.append(_uncomment_line(updated + ("\n" if not updated.endswith("\n") else "")))
            else:
                out.append(_comment_line(line))
            z2_done = True
            continue

        if Z3_RE.match(raw):
            if not z3_done and new_cfg.get("z3_mb", 0) > 0:
                size = f"{new_cfg['z3_mb']}M"
                updated = _replace_size(raw, size)
                out.append(_uncomment_line(updated + ("\n" if not updated.endswith("\n") else "")))
            else:
                out.append(_comment_line(line))
            z3_done = True
            continue

        if CHIP_RE.match(raw):
            if new_cfg.get("chip_ram"):
                out.append(_uncomment_line(line))
            else:
                out.append(_comment_line(line))
            chip_done = True
            continue

        if LOOPCYCLES_RE.match(raw):
            if not loop_done and new_cfg.get("loopcycles", 0) > 0:
                out.append(f"loopcycles {new_cfg['loopcycles']}\n")
                loop_done = True
            else:
                out.append(_comment_line(line))
            continue

        if PLATFORM_RE.match(raw):
            if not platform_done and new_cfg.get("platform"):
                out.append(f"platform {new_cfg['platform']}\n")
                platform_done = True
            else:
                out.append(_comment_line(line))
            continue

        if KEYBOARD_RE.match(raw):
            if not keyboard_done and new_cfg.get("keyboard", {}).get("enabled"):
                kb = new_cfg["keyboard"]
                grab = "grab" if kb.get("grab") else "nograb"
                auto = "autoconnect" if kb.get("autoconnect") else "noautoconnect"
                out.append(f"keyboard {kb.get('key', 'k')} {grab} {auto}\n")
                keyboard_done = True
            else:
                out.append(_comment_line(line))
            continue

        if MOUSE_RE.match(raw):
            if not mouse_done and new_cfg.get("mouse", {}).get("enabled"):
                ms = new_cfg["mouse"]
                auto = "autoconnect" if ms.get("autoconnect") else "noautoconnect"
                out.append(f"mouse {ms.get('file', '/dev/input/mice')} {ms.get('key', 'm')} {auto}\n")
                mouse_done = True
            else:
                out.append(_comment_line(line))
            continue

        if KBFILE_RE.match(raw):
            if not kbfile_done and new_cfg.get("kbfile"):
                out.append(f"kbfile {new_cfg['kbfile']}\n")
                kbfile_done = True
            else:
                out.append(_comment_line(line))
            continue

        if PISCSI_ENABLE_RE.match(raw):
            if new_cfg.get("piscsi_enable"):
                out.append(_uncomment_line(line))
            else:
                out.append(_comment_line(line))
            piscsi_enable_done = True
            continue

        m = PISCSI_RE.match(raw)
        if m:
            unit = m.group(1)
            path_val = new_cfg.get("piscsi", {}).get(unit, "")
            if not piscsi_done[unit] and path_val:
                out.append(f"setvar piscsi{unit} {path_val}\n")
            else:
                out.append(_comment_line(line))
            piscsi_done[unit] = True
            continue

        m = SETVAR_RE.match(raw)
        if m:
            name = m.group(1).lower()
            if name.startswith("piscsi"):
                out.append(line)
                continue
            if name not in setvar_done:
                out.append(line)
                continue
            if setvar_done[name]:
                out.append(_comment_line(line))
                continue

            if name == "rtg":
                out.append(_uncomment_line(_setvar_line("rtg", None)) if new_cfg.get("rtg") else _comment_line(line))
            elif name == "rtg-dpms":
                out.append(
                    _uncomment_line(_setvar_line("rtg-dpms", None)) if new_cfg.get("rtg_dpms") else _comment_line(line)
                )
            elif name == "rtg-width":
                val = new_cfg.get("rtg_width", 0)
                if val > 0:
                    out.append(_uncomment_line(_setvar_line("rtg-width", str(val))))
                else:
                    out.append(_comment_line(line))
            elif name == "rtg-height":
                val = new_cfg.get("rtg_height", 0)
                if val > 0:
                    out.append(_uncomment_line(_setvar_line("rtg-height", str(val))))
                else:
                    out.append(_comment_line(line))
            elif name == "pi-ahi":
                if new_cfg.get("pi_ahi"):
                    dev = new_cfg.get("pi_ahi_device") or ""
                    out.append(_uncomment_line(_setvar_line("pi-ahi", dev)))
                else:
                    out.append(_comment_line(line))
            elif name == "pi-ahi-samplerate":
                val = new_cfg.get("pi_ahi_samplerate", 0)
                if val > 0:
                    out.append(_uncomment_line(_setvar_line("pi-ahi-samplerate", str(val))))
                else:
                    out.append(_comment_line(line))
            elif name == "cdtv":
                out.append(_uncomment_line(_setvar_line("cdtv", None)) if new_cfg.get("cdtv") else _comment_line(line))
            elif name == "enable_rtc_emulation":
                val = new_cfg.get("enable_rtc_emulation")
                if val is None:
                    out.append(line)
                else:
                    out.append(_uncomment_line(_setvar_line("enable_rtc_emulation", str(int(val)))))
            elif name == "pi-net":
                out.append(_uncomment_line(_setvar_line("pi-net", None)) if new_cfg.get("pi_net") else _comment_line(line))
            elif name == "a314_conf":
                if new_cfg.get("a314_conf"):
                    out.append(_uncomment_line(_setvar_line("a314_conf", new_cfg["a314_conf"])))
                else:
                    out.append(_comment_line(line))
            elif name == "a314":
                if new_cfg.get("a314_conf") and not setvar_done["a314_conf"]:
                    out.append(_uncomment_line(_setvar_line("a314_conf", new_cfg["a314_conf"])))
                    setvar_done["a314_conf"] = True
                out.append(_uncomment_line(_setvar_line("a314", None)) if new_cfg.get("a314") else _comment_line(line))
            elif name == "move-slow-to-chip":
                out.append(
                    _uncomment_line(_setvar_line("move-slow-to-chip", None))
                    if new_cfg.get("move_slow_to_chip")
                    else _comment_line(line)
                )
            elif name == "swap-df0-df":
                val = int(new_cfg.get("swap_df0_df", 0))
                if val:
                    out.append(_uncomment_line(_setvar_line("swap-df0-df", str(val))))
                else:
                    out.append(_comment_line(line))
            elif name == "physical-z2-first":
                out.append(
                    _uncomment_line(_setvar_line("physical-z2-first", None))
                    if new_cfg.get("physical_z2_first")
                    else _comment_line(line)
                )
            elif name == "kick13":
                out.append(_uncomment_line(_setvar_line("kick13", None)) if new_cfg.get("kick13") else _comment_line(line))
            elif name == "no-pistorm-dev":
                out.append(
                    _uncomment_line(_setvar_line("no-pistorm-dev", None))
                    if new_cfg.get("no_pistorm_dev")
                    else _comment_line(line)
                )

            setvar_done[name] = True
            continue

        out.append(line)

    if not cpu_done and new_cfg.get("cpu"):
        out.insert(0, f"cpu {new_cfg['cpu']}\n")

    if not kick_done and new_cfg.get("kickstart"):
        out.append(f"map type=rom address=0xF80000 size=0x80000 file={new_cfg['kickstart']} ovl=0 id=kickstart autodump_mem\n")

    if not cpu_slot_done and new_cfg.get("cpu_slot_mb", 0) > 0:
        out.append(f"map type=ram address=0x08000000 size={new_cfg['cpu_slot_mb']}M id=cpu_slot_ram\n")

    if not z2_done and new_cfg.get("z2_mb", 0) > 0:
        out.append(f"map type=ram address=0x200000 size={new_cfg['z2_mb']}M id=z2_autoconf_fast\n")

    if not z3_done and new_cfg.get("z3_mb", 0) > 0:
        out.append(f"map type=ram address=0x10000000 size={new_cfg['z3_mb']}M id=z3_autoconf_fast\n")

    if not chip_done and new_cfg.get("chip_ram"):
        out.append("map type=ram address=0x0 size=2M\n")

    if not loop_done and new_cfg.get("loopcycles", 0) > 0:
        out.append(f"loopcycles {new_cfg['loopcycles']}\n")

    if not platform_done and new_cfg.get("platform"):
        out.append(f"platform {new_cfg['platform']}\n")

    if not keyboard_done and new_cfg.get("keyboard", {}).get("enabled"):
        kb = new_cfg["keyboard"]
        grab = "grab" if kb.get("grab") else "nograb"
        auto = "autoconnect" if kb.get("autoconnect") else "noautoconnect"
        out.append(f"keyboard {kb.get('key', 'k')} {grab} {auto}\n")

    if not mouse_done and new_cfg.get("mouse", {}).get("enabled"):
        ms = new_cfg["mouse"]
        auto = "autoconnect" if ms.get("autoconnect") else "noautoconnect"
        out.append(f"mouse {ms.get('file', '/dev/input/mice')} {ms.get('key', 'm')} {auto}\n")

    if not kbfile_done and new_cfg.get("kbfile"):
        out.append(f"kbfile {new_cfg['kbfile']}\n")

    if not piscsi_enable_done and new_cfg.get("piscsi_enable"):
        out.append("setvar piscsi\n")

    for unit, done in piscsi_done.items():
        path_val = new_cfg.get("piscsi", {}).get(unit, "")
        if not done and path_val:
            out.append(f"setvar piscsi{unit} {path_val}\n")

    if not setvar_done["rtg"] and new_cfg.get("rtg"):
        out.append("setvar rtg\n")
    if not setvar_done["rtg-dpms"] and new_cfg.get("rtg_dpms"):
        out.append("setvar rtg-dpms\n")
    if not setvar_done["rtg-width"] and new_cfg.get("rtg_width", 0) > 0:
        out.append(f"setvar rtg-width {new_cfg['rtg_width']}\n")
    if not setvar_done["rtg-height"] and new_cfg.get("rtg_height", 0) > 0:
        out.append(f"setvar rtg-height {new_cfg['rtg_height']}\n")
    if not setvar_done["pi-ahi"] and new_cfg.get("pi_ahi"):
        dev = new_cfg.get("pi_ahi_device") or ""
        out.append(_setvar_line("pi-ahi", dev))
    if not setvar_done["pi-ahi-samplerate"] and new_cfg.get("pi_ahi_samplerate", 0) > 0:
        out.append(f"setvar pi-ahi-samplerate {new_cfg['pi_ahi_samplerate']}\n")
    if not setvar_done["cdtv"] and new_cfg.get("cdtv"):
        out.append("setvar cdtv\n")
    if not setvar_done["enable_rtc_emulation"] and new_cfg.get("enable_rtc_emulation") is not None:
        out.append(f"setvar enable_rtc_emulation {int(new_cfg['enable_rtc_emulation'])}\n")
    if not setvar_done["pi-net"] and new_cfg.get("pi_net"):
        out.append("setvar pi-net\n")
    if not setvar_done["a314_conf"] and new_cfg.get("a314_conf"):
        out.append(f"setvar a314_conf {new_cfg['a314_conf']}\n")
    if not setvar_done["a314"] and new_cfg.get("a314"):
        out.append("setvar a314\n")
    if not setvar_done["move-slow-to-chip"] and new_cfg.get("move_slow_to_chip"):
        out.append("setvar move-slow-to-chip\n")
    if not setvar_done["swap-df0-df"] and int(new_cfg.get("swap_df0_df", 0)):
        out.append(f"setvar swap-df0-df {int(new_cfg['swap_df0_df'])}\n")
    if not setvar_done["physical-z2-first"] and new_cfg.get("physical_z2_first"):
        out.append("setvar physical-z2-first\n")
    if not setvar_done["kick13"] and new_cfg.get("kick13"):
        out.append("setvar kick13\n")
    if not setvar_done["no-pistorm-dev"] and new_cfg.get("no_pistorm_dev"):
        out.append("setvar no-pistorm-dev\n")

    p.write_text("".join(out))
    return True, "updated"


def cmd_list(args) -> int:
    imgs = list_images(Path(args.dir))
    if not imgs:
        print("(no .adf/.hdf files found)")
        return 0

    for it in imgs:
        print(f"{it['group']}\t{it['name']}\t{human_size(it['size'])}\t{it['relpath']}")
    return 0


def cmd_eject(args) -> int:
    resp = send_cmd(args.host, args.port, f"eject {args.unit}")
    print(resp or "(no response)")
    return 0


def cmd_insert(args) -> int:
    p = Path(args.image)
    if not p.is_absolute():
        p = Path(args.dir) / p
    if not p.exists():
        print(f"error: not found: {p}", file=sys.stderr)
        return 2

    rw_flag = "-rw " if args.rw else ""
    unit = args.unit
    status = get_status(args.host, args.port)
    if args.auto or (status and any(u.get("unit") == unit and u.get("present") for u in status.get("units", []))):
        picked = pick_unit(unit, status)
        if picked is None:
            print("error: no free unit", file=sys.stderr)
            return 2
        unit = picked
    # disk.py expects: insert <unit> [-rw] <path>
    resp = send_cmd(args.host, args.port, f"insert {unit} {rw_flag}{p}")
    print(resp or "(no response)")
    return 0


def cmd_status(args) -> int:
    status = get_status(args.host, args.port)
    if not status:
        print("status unavailable", file=sys.stderr)
        return 2
    print(json.dumps(status, indent=2))
    return 0


def cmd_create(args) -> int:
    dest = Path(args.dir) / args.name
    ok, msg = create_blank_adf(dest, volume=args.volume, force=args.force)
    if not ok:
        print(f"error: {msg}", file=sys.stderr)
        return 2
    print(msg)
    return 0


def cmd_clone(args) -> int:
    src = Path(args.src)
    if not src.is_absolute():
        src = Path(args.dir) / src
    dest = Path(args.dir) / args.dest
    ok, msg = clone_image(src, dest, force=args.force)
    if not ok:
        print(f"error: {msg}", file=sys.stderr)
        return 2
    print(msg)
    return 0


# ---------------- Web UI ----------------

INDEX_HTML = """<!doctype html>
<html>
<head>
  <meta charset="utf-8"/>
  <title>ADF Manager</title>
  <link rel="stylesheet" href="/adf.css"/>
</head>
<body>
  <header class="topbar">
    <div class="title">ADF Manager</div>
    <nav class="nav">
      <a href="/config">Config</a>
    </nav>
  </header>

  <section class="panel">
    <div class="row">
      <label>Profile:
        <select id="cfgfile"></select>
      </label>
      <button onclick="loadProfile()">Load</button>
      <button onclick="activateProfile()">Activate</button>
      <div class="label" id="activeCfg"></div>
    </div>
  </section>

  <section class="panel">
    <div class="row">
      <button onclick="refresh()">Refresh</button>
      <label>Unit:
        <select id="ejectUnit">
          <option>0</option><option>1</option><option>2</option><option>3</option>
        </select>
      </label>
      <button onclick="ejectUnit()">Eject</button>
    </div>
    <div id="unitStatus" class="units"></div>
  </section>

  <section class="panel">
    <div class="row">
      <div class="label">Create Blank ADF</div>
      <input id="newName" placeholder="new_disk.adf"/>
      <input id="newVol" placeholder="volume name" value="BLANK"/>
      <button onclick="createBlank()">Create</button>
    </div>
  </section>

  <section class="panel">
    <div id="groups"></div>
  </section>

  <section class="panel log mono" id="log"></section>

<script>
let statusCache = null;

function log(s) {
  const el = document.getElementById('log');
  el.textContent = (s + "\\n" + el.textContent).slice(0, 4000);
}

async function apiGet(path) {
  const r = await fetch(path);
  return r.json();
}

async function apiPost(path, data) {
  const body = new URLSearchParams(data);
  const r = await fetch(path, {method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'}, body});
  return r.json();
}

function escapeHtml(s) {
  return s.replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('"','&quot;').replaceAll("'","&#039;");
}

function unitOptions() {
  return `
    <option value="auto">auto</option>
    <option value="0">0</option>
    <option value="1">1</option>
    <option value="2">2</option>
    <option value="3">3</option>`;
}

function renderStatus(status) {
  if (!status || !status.units) {
    document.getElementById('unitStatus').textContent = 'unit status unavailable';
    return;
  }
  const parts = status.units.map(u => {
    if (!u.present) return `PD${u.unit}: empty`;
    const name = u.filename ? u.filename.split('/').slice(-1)[0] : 'disk';
    const rw = u.writable ? 'rw' : 'ro';
    return `PD${u.unit}: ${name} (${rw})`;
  });
  document.getElementById('unitStatus').textContent = parts.join(' | ');
}

async function loadProfiles() {
  const j = await apiGet('/api/configs');
  const sel = document.getElementById('cfgfile');
  sel.innerHTML = '';
  for (const f of j.items || []) {
    const opt = document.createElement('option');
    opt.value = f;
    opt.textContent = f;
    sel.appendChild(opt);
  }
  if (j.selected) sel.value = j.selected;
  const active = j.active ? `active: ${j.active}` : '';
  document.getElementById('activeCfg').textContent = active;
}

async function loadProfile() {
  const name = document.getElementById('cfgfile').value;
  const j = await apiPost('/api/config/select', {name});
  log(j.response || j.error || 'profile loaded');
  await loadProfiles();
}

async function activateProfile() {
  const name = document.getElementById('cfgfile').value;
  const j = await apiPost('/api/config/activate', {name});
  log(j.response || j.error || '(no response)');
  await loadProfiles();
}

function renderGroups(items) {
  const container = document.getElementById('groups');
  container.innerHTML = '';
  if (!items || items.length === 0) {
    container.textContent = 'no .adf/.hdf files found';
    return;
  }

  const groups = {};
  for (const it of items) {
    const g = it.group || 'root';
    if (!groups[g]) groups[g] = [];
    groups[g].push(it);
  }
  const names = Object.keys(groups).sort((a, b) => {
    if (a === 'root') return -1;
    if (b === 'root') return 1;
    return a.localeCompare(b);
  });

  let i = 0;
  for (const name of names) {
    const list = groups[name];
    const details = document.createElement('details');
    details.className = 'group';
    const summary = document.createElement('summary');
    summary.textContent = `${name} (${list.length})`;
    details.appendChild(summary);

    const table = document.createElement('table');
    table.innerHTML = `
      <thead>
        <tr>
          <th>Unit</th>
          <th>Name</th>
          <th>Size</th>
          <th>Path</th>
          <th>Actions</th>
        </tr>
      </thead>
      <tbody></tbody>
    `;
    const tb = table.querySelector('tbody');

    for (const it of list) {
      const tr = document.createElement('tr');
      const unitId = `unit_${i}`;
      const rwId = `rw_${i}`;
      tr.innerHTML = `
        <td>
          <select id="${unitId}">${unitOptions()}</select>
          <label class="rw"><input type="checkbox" id="${rwId}">rw</label>
        </td>
        <td class="mono">${escapeHtml(it.name)}</td>
        <td>${it.hsize}</td>
        <td class="mono">${escapeHtml(it.relpath || it.path)}</td>
        <td>
          <button onclick="insertDisk('${encodeURIComponent(it.path)}','${unitId}','${rwId}')">Insert</button>
          <button onclick="cloneDisk('${encodeURIComponent(it.path)}')">Clone</button>
        </td>
      `;
      tb.appendChild(tr);
      i++;
    }
    details.appendChild(table);
    container.appendChild(details);
  }
}

async function refresh() {
  const [list, status] = await Promise.all([apiGet('/api/list'), apiGet('/api/status')]);
  statusCache = status;
  renderStatus(statusCache);

  renderGroups(list.items || []);
  log('refreshed list');
}

async function ejectUnit() {
  const unit = document.getElementById('ejectUnit').value;
  const j = await apiPost('/api/eject', {unit});
  log(j.response || j.error || '(no response)');
  refresh();
}

async function insertDisk(pathEnc, unitId, rwId) {
  const unit = document.getElementById(unitId).value;
  const rw = document.getElementById(rwId).checked ? '1' : '0';
  const j = await apiPost('/api/insert', {unit, rw, path: decodeURIComponent(pathEnc)});
  log(j.response || j.error || '(no response)');
  refresh();
}

async function cloneDisk(pathEnc) {
  const src = decodeURIComponent(pathEnc);
  const base = src.split('/').slice(-1)[0];
  const dest = prompt('Clone as (filename):', base.replace(/\\.adf$/i, '-copy.adf'));
  if (!dest) return;
  const j = await apiPost('/api/clone', {src, dest});
  log(j.response || j.error || '(no response)');
  refresh();
}

async function createBlank() {
  const name = document.getElementById('newName').value.trim();
  const volume = document.getElementById('newVol').value.trim() || 'BLANK';
  if (!name) {
    log('name required');
    return;
  }
  const j = await apiPost('/api/create', {name, volume});
  log(j.response || j.error || '(no response)');
  refresh();
}

loadProfiles().then(refresh);
</script>
</body>
</html>
"""

CONFIG_HTML = """<!doctype html>
<html>
<head>
  <meta charset="utf-8"/>
  <title>PiStorm64 Config</title>
  <link rel="stylesheet" href="/adf.css"/>
</head>
<body>
  <header class="topbar">
    <div class="title">PiStorm64 Config</div>
    <nav class="nav">
      <a href="/">ADF Manager</a>
    </nav>
  </header>

  <section class="panel">
    <div class="row">
      <label>Config:
        <select id="cfgfile"></select>
      </label>
      <button onclick="loadCfg()">Load</button>
      <button onclick="saveCfg()">Save</button>
      <button onclick="activateCfg()">Activate</button>
    </div>
    <div class="row">
      <input id="newCfgName" placeholder="new-profile.cfg"/>
      <button onclick="createCfg()">Create</button>
      <div class="label" id="activeCfg"></div>
    </div>
  </section>

  <section class="panel">
    <div class="row">
      <label>CPU:
        <select id="cpu">
          <option>68000</option>
          <option>68010</option>
          <option>68020</option>
          <option>68EC020</option>
          <option>68030</option>
          <option>68EC030</option>
          <option>68040</option>
          <option>68EC040</option>
          <option>68LC040</option>
        </select>
      </label>
      <label>Platform:
        <input id="platform" placeholder="amiga"/>
      </label>
      <label>Loopcycles:
        <input id="loopcycles" type="range" min="1" max="10000" step="1"/>
        <span id="loopcyclesVal" class="mono"></span>
      </label>
    </div>

    <div class="row">
      <label>Kickstart:
        <select id="kick"></select>
      </label>
      <label>CPU Slot RAM (MB):
        <input id="cpu_slot_mb" type="number" min="0" step="1"/>
      </label>
    </div>

    <div class="row">
      <label>Z2 Fast:
        <select id="z2">
          <option value="0">disabled</option>
          <option value="2">2 MB</option>
          <option value="4">4 MB</option>
          <option value="8">8 MB</option>
        </select>
      </label>
      <label>Z3 Fast:
        <select id="z3">
          <option value="0">disabled</option>
          <option value="64">64 MB</option>
          <option value="128">128 MB</option>
          <option value="256">256 MB</option>
          <option value="512">512 MB</option>
          <option value="1024">1024 MB</option>
        </select>
      </label>
      <label><input type="checkbox" id="chip"> fake chip 2MB</label>
    </div>
  </section>

  <section class="panel">
    <div class="row">
      <label><input type="checkbox" id="rtg"> enable RTG</label>
      <label><input type="checkbox" id="rtg_dpms"> RTG DPMS</label>
      <label>RTG width:
        <input id="rtg_width" type="number" min="0" step="1"/>
      </label>
      <label>RTG height:
        <input id="rtg_height" type="number" min="0" step="1"/>
      </label>
    </div>
  </section>

  <section class="panel">
    <div class="row">
      <label><input type="checkbox" id="pi_ahi"> enable Pi-AHI</label>
      <label>AHI device:
        <input id="pi_ahi_device" placeholder="plughw:1,0"/>
      </label>
      <label>AHI samplerate:
        <input id="pi_ahi_samplerate" type="number" min="0" step="1"/>
      </label>
    </div>
  </section>

  <section class="panel">
    <div class="row">
      <label><input type="checkbox" id="a314" disabled> enable A314</label>
      <label>A314 conf:
        <input id="a314_conf" placeholder="/home/smalley/pistorm64/src/a314/files_pi/a314d.conf" disabled/>
      </label>
    </div>
  </section>

  <section class="panel">
    <div class="row">
      <label><input type="checkbox" id="cdtv"> CDTV mode</label>
      <label><input type="checkbox" id="pi_net"> Pi-Net</label>
      <label><input type="checkbox" id="move_slow_to_chip"> move-slow-to-chip</label>
      <label>swap-df0-df:
        <select id="swap_df0_df">
          <option value="0">off</option>
          <option value="1">on</option>
        </select>
      </label>
    </div>
    <div class="row">
      <label><input type="checkbox" id="physical_z2_first"> physical-z2-first</label>
      <label><input type="checkbox" id="kick13"> kick13</label>
      <label><input type="checkbox" id="no_pistorm_dev"> no-pistorm-dev</label>
      <label>RTC emulation:
        <select id="rtc">
          <option value="">(leave)</option>
          <option value="1">enabled</option>
          <option value="0">disabled</option>
        </select>
      </label>
    </div>
  </section>

  <section class="panel">
    <div class="row">
      <label><input type="checkbox" id="kbd_enabled"> keyboard forwarding</label>
      <label>toggle key:
        <input id="kbd_key" maxlength="1"/>
      </label>
      <label><input type="checkbox" id="kbd_grab"> grab</label>
      <label><input type="checkbox" id="kbd_autoconnect"> autoconnect</label>
    </div>
    <div class="row">
      <label>kbfile:
        <input id="kbfile" placeholder="/dev/input/event1"/>
      </label>
    </div>
    <div class="row">
      <label><input type="checkbox" id="mouse_enabled"> mouse forwarding</label>
      <label>device:
        <input id="mouse_file" placeholder="/dev/input/mouse0"/>
      </label>
      <label>toggle key:
        <input id="mouse_key" maxlength="1"/>
      </label>
      <label><input type="checkbox" id="mouse_autoconnect"> autoconnect</label>
    </div>
  </section>

  <section class="panel">
    <div class="row">
      <label><input type="checkbox" id="piscsi"> enable PiSCSI</label>
    </div>

    <div class="row">
      <div class="label">PiSCSI Units (0-6)</div>
    </div>
    <div id="piscsiRows"></div>
  </section>

  <section class="panel log mono" id="log"></section>

<script>
function log(s) {
  const el = document.getElementById('log');
  el.textContent = (s + "\\n" + el.textContent).slice(0, 4000);
}

async function apiGet(path) {
  const r = await fetch(path);
  return r.json();
}

async function apiPost(path, data) {
  const r = await fetch(path, {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(data)});
  return r.json();
}

function buildSelect(id, files, current) {
  const sel = document.getElementById(id);
  sel.innerHTML = '<option value="">(none)</option>';
  for (const f of files) {
    const opt = document.createElement('option');
    opt.value = f;
    opt.textContent = f;
    sel.appendChild(opt);
  }
  if (current) sel.value = current;
}

function buildPiscsiRows(files, current) {
  const root = document.getElementById('piscsiRows');
  root.innerHTML = '';
  for (let i = 0; i < 7; i++) {
    const row = document.createElement('div');
    row.className = 'row';
    row.innerHTML = `
      <label>piscsi${i}:
        <select id="piscsi_${i}">
          <option value="">(none)</option>
        </select>
      </label>`;
    root.appendChild(row);
    const sel = document.getElementById(`piscsi_${i}`);
    for (const f of files) {
      const opt = document.createElement('option');
      opt.value = f;
      opt.textContent = f;
      sel.appendChild(opt);
    }
    if (current && current[String(i)]) {
      sel.value = current[String(i)];
    }
  }
}

function togglePiscsi() {
  const enabled = document.getElementById('piscsi').checked;
  for (let i = 0; i < 7; i++) {
    const sel = document.getElementById(`piscsi_${i}`);
    if (sel) sel.disabled = !enabled;
  }
}

function updateLoopcyclesLabel() {
  const v = document.getElementById('loopcycles').value;
  document.getElementById('loopcyclesVal').textContent = v;
}

async function loadCfgList() {
  const j = await apiGet('/api/configs');
  const sel = document.getElementById('cfgfile');
  sel.innerHTML = '';
  for (const f of j.items || []) {
    const opt = document.createElement('option');
    opt.value = f;
    opt.textContent = f;
    sel.appendChild(opt);
  }
  if (j.selected) sel.value = j.selected;
  const active = j.active ? `active: ${j.active}` : '';
  document.getElementById('activeCfg').textContent = active;
}

function applyCfg(j) {
  document.getElementById('cpu').value = j.config.cpu || '68030';
  document.getElementById('platform').value = j.config.platform || 'amiga';
  document.getElementById('loopcycles').value = String(j.config.loopcycles || 300);
  updateLoopcyclesLabel();
  document.getElementById('cpu_slot_mb').value = String(j.config.cpu_slot_mb || 0);
  buildSelect('kick', j.kickstarts, j.config.kickstart || '');
  document.getElementById('z2').value = String(j.config.z2_mb || 0);
  document.getElementById('z3').value = String(j.config.z3_mb || 0);
  document.getElementById('chip').checked = !!j.config.chip_ram;

  document.getElementById('rtg').checked = !!j.config.rtg;
  document.getElementById('rtg_dpms').checked = !!j.config.rtg_dpms;
  document.getElementById('rtg_width').value = String(j.config.rtg_width || 0);
  document.getElementById('rtg_height').value = String(j.config.rtg_height || 0);

  document.getElementById('pi_ahi').checked = !!j.config.pi_ahi;
  document.getElementById('pi_ahi_device').value = j.config.pi_ahi_device || '';
  document.getElementById('pi_ahi_samplerate').value = String(j.config.pi_ahi_samplerate || 0);

  document.getElementById('a314').checked = !!j.config.a314;
  document.getElementById('a314_conf').value = j.config.a314_conf || '';

  document.getElementById('cdtv').checked = !!j.config.cdtv;
  document.getElementById('pi_net').checked = !!j.config.pi_net;
  document.getElementById('move_slow_to_chip').checked = !!j.config.move_slow_to_chip;
  document.getElementById('swap_df0_df').value = String(j.config.swap_df0_df || 0);
  document.getElementById('physical_z2_first').checked = !!j.config.physical_z2_first;
  document.getElementById('kick13').checked = !!j.config.kick13;
  document.getElementById('no_pistorm_dev').checked = !!j.config.no_pistorm_dev;
  document.getElementById('rtc').value = j.config.enable_rtc_emulation === null ? '' : String(j.config.enable_rtc_emulation);

  document.getElementById('kbd_enabled').checked = !!(j.config.keyboard && j.config.keyboard.enabled);
  document.getElementById('kbd_key').value = (j.config.keyboard && j.config.keyboard.key) || 'k';
  document.getElementById('kbd_grab').checked = !!(j.config.keyboard && j.config.keyboard.grab);
  document.getElementById('kbd_autoconnect').checked = !!(j.config.keyboard && j.config.keyboard.autoconnect);
  document.getElementById('kbfile').value = j.config.kbfile || '';

  document.getElementById('mouse_enabled').checked = !!(j.config.mouse && j.config.mouse.enabled);
  document.getElementById('mouse_file').value = (j.config.mouse && j.config.mouse.file) || '';
  document.getElementById('mouse_key').value = (j.config.mouse && j.config.mouse.key) || 'm';
  document.getElementById('mouse_autoconnect').checked = !!(j.config.mouse && j.config.mouse.autoconnect);

  document.getElementById('piscsi').checked = !!j.config.piscsi_enable;
  buildPiscsiRows(j.hdfs, j.config.piscsi || {});
  togglePiscsi();
}

async function loadCfg() {
  const name = document.getElementById('cfgfile').value;
  const j = await apiPost('/api/config/select', {name});
  if (j.error) {
    log(j.error);
    return;
  }
  applyCfg(j);
  log('config loaded');
}

async function saveCfg() {
  const rtcRaw = document.getElementById('rtc').value;
  const cfg = {
    cpu: document.getElementById('cpu').value,
    platform: document.getElementById('platform').value,
    loopcycles: parseInt(document.getElementById('loopcycles').value || '300', 10),
    kickstart: document.getElementById('kick').value,
    cpu_slot_mb: parseInt(document.getElementById('cpu_slot_mb').value || '0', 10),
    z2_mb: parseInt(document.getElementById('z2').value, 10),
    z3_mb: parseInt(document.getElementById('z3').value, 10),
    chip_ram: document.getElementById('chip').checked,

    rtg: document.getElementById('rtg').checked,
    rtg_dpms: document.getElementById('rtg_dpms').checked,
    rtg_width: parseInt(document.getElementById('rtg_width').value || '0', 10),
    rtg_height: parseInt(document.getElementById('rtg_height').value || '0', 10),

    pi_ahi: document.getElementById('pi_ahi').checked,
    pi_ahi_device: document.getElementById('pi_ahi_device').value,
    pi_ahi_samplerate: parseInt(document.getElementById('pi_ahi_samplerate').value || '0', 10),

    a314: document.getElementById('a314').checked,
    a314_conf: document.getElementById('a314_conf').value,

    cdtv: document.getElementById('cdtv').checked,
    pi_net: document.getElementById('pi_net').checked,
    move_slow_to_chip: document.getElementById('move_slow_to_chip').checked,
    swap_df0_df: parseInt(document.getElementById('swap_df0_df').value || '0', 10),
    physical_z2_first: document.getElementById('physical_z2_first').checked,
    kick13: document.getElementById('kick13').checked,
    no_pistorm_dev: document.getElementById('no_pistorm_dev').checked,
    enable_rtc_emulation: rtcRaw === '' ? null : parseInt(rtcRaw, 10),

    keyboard: {
      enabled: document.getElementById('kbd_enabled').checked,
      key: document.getElementById('kbd_key').value || 'k',
      grab: document.getElementById('kbd_grab').checked,
      autoconnect: document.getElementById('kbd_autoconnect').checked
    },
    kbfile: document.getElementById('kbfile').value,

    mouse: {
      enabled: document.getElementById('mouse_enabled').checked,
      file: document.getElementById('mouse_file').value || '/dev/input/mice',
      key: document.getElementById('mouse_key').value || 'm',
      autoconnect: document.getElementById('mouse_autoconnect').checked
    },

    piscsi_enable: document.getElementById('piscsi').checked,
    piscsi: {}
  };
  for (let i = 0; i < 7; i++) {
    cfg.piscsi[String(i)] = document.getElementById(`piscsi_${i}`).value;
  }
  const j = await apiPost('/api/config', cfg);
  log(j.response || j.error || '(no response)');
}

async function createCfg() {
  const name = document.getElementById('newCfgName').value.trim();
  const base = document.getElementById('cfgfile').value;
  if (!name) {
    log('name required');
    return;
  }
  const j = await apiPost('/api/config/create', {name, base});
  log(j.response || j.error || '(no response)');
  await loadCfgList();
}

async function activateCfg() {
  const name = document.getElementById('cfgfile').value;
  const j = await apiPost('/api/config/activate', {name});
  log(j.response || j.error || '(no response)');
  await loadCfgList();
}

loadCfgList().then(loadCfg);
document.getElementById('piscsi').addEventListener('change', togglePiscsi);
document.getElementById('loopcycles').addEventListener('input', updateLoopcyclesLabel);
</script>
</body>
</html>
"""


class Handler(BaseHTTPRequestHandler):
    server_version = "adfmanager/1.0"

    def _json(self, obj, code=200):
        b = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def _text(self, body: str, code=200, ctype="text/html; charset=utf-8"):
        b = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    @property
    def cfg(self):
        return self.server.cfg  # type: ignore[attr-defined]

    def do_GET(self):
        u = urlparse(self.path)
        if u.path == "/":
            return self._text(INDEX_HTML)

        if u.path == "/config":
            return self._text(CONFIG_HTML)

        if u.path == "/adf.css":
            css_path = Path(self.cfg["css"])
            if not css_path.exists():
                return self._json({"error": "css not found"}, code=404)
            return self._text(css_path.read_text(errors="replace"), ctype="text/css; charset=utf-8")

        if u.path == "/api/list":
            adf_dir = Path(self.cfg["dir"])
            items = []
            for it in list_images(adf_dir):
                items.append(
                    {
                        **it,
                        "hsize": human_size(it["size"]),
                    }
                )
            return self._json({"items": items})

        if u.path == "/api/status":
            status = get_status(self.cfg["host"], self.cfg["port"])
            if not status:
                return self._json({"error": "status unavailable"}, code=502)
            return self._json(status)

        if u.path == "/api/configs":
            cfg_files = list_cfg_files(self.cfg["cfg_dir"])
            selected = os.path.basename(self.cfg["cfg"])
            active = os.path.basename(self.cfg["default_cfg"])
            return self._json({"items": cfg_files, "selected": selected, "active": active})

        if u.path == "/api/config":
            cfg = parse_default_cfg(self.cfg["cfg"])
            kickstarts = list_dir_files(self.cfg["kick_dir"], {".rom", ".bin"})
            hdfs = list_dir_files(self.cfg["hdf_dir"], {".hdf"})
            return self._json(
                {
                    "config": cfg,
                    "kickstarts": kickstarts,
                    "hdfs": hdfs,
                    "cfg_file": os.path.basename(self.cfg["cfg"]),
                }
            )

        return self._json({"error": "not found"}, code=404)

    def do_POST(self):
        u = urlparse(self.path)
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode("utf-8") if length else ""
        json_body = None
        if "application/json" in self.headers.get("Content-Type", ""):
            try:
                json_body = json.loads(body) if body else {}
            except json.JSONDecodeError:
                json_body = None
        q = parse_qs(u.query)
        form = parse_qs(body) if body else {}

        def param(name, default=""):
            if json_body is not None and name in json_body:
                val = json_body[name]
                if isinstance(val, (str, int, float)):
                    return str(val)
            if name in form and form[name]:
                return form[name][0]
            if name in q and q[name]:
                return q[name][0]
            return default

        if u.path == "/api/eject":
            unit = int(param("unit", "0"))
            resp = send_cmd(self.cfg["host"], self.cfg["port"], f"eject {unit}")
            return self._json({"response": resp})

        if u.path == "/api/insert":
            unit_raw = param("unit", "auto")
            rw = param("rw", "0") == "1"
            path = param("path", "")
            # path arrives URI-encoded from JS
            path = socket.getfqdn() and path  # noop; keep lint calm
            path = bytes(path, "utf-8").decode("utf-8")
            # we already encoded in JS; parse_qs decodes %xx automatically
            p = Path(path)
            if not p.exists():
                return self._json({"error": f"not found: {p}"}, code=400)
            status = get_status(self.cfg["host"], self.cfg["port"])
            preferred = int(unit_raw) if unit_raw.isdigit() else None
            unit = pick_unit(preferred, status)
            if unit is None:
                return self._json({"error": "no free unit"}, code=409)
            rw_flag = "-rw " if rw else ""
            resp = send_cmd(self.cfg["host"], self.cfg["port"], f"insert {unit} {rw_flag}{p}")
            return self._json({"response": resp, "unit": unit})

        if u.path == "/api/create":
            name = param("name", "").strip()
            volume = param("volume", "BLANK").strip() or "BLANK"
            if not name:
                return self._json({"error": "name required"}, code=400)
            dest = Path(self.cfg["dir"]) / name
            ok, msg = create_blank_adf(dest, volume=volume, force=False)
            if not ok:
                return self._json({"error": msg}, code=400)
            return self._json({"response": msg})

        if u.path == "/api/clone":
            src = param("src", "").strip()
            dest = param("dest", "").strip()
            if not src or not dest:
                return self._json({"error": "src and dest required"}, code=400)
            src_path = Path(src)
            if not src_path.is_absolute():
                src_path = Path(self.cfg["dir"]) / src
            dest_path = Path(self.cfg["dir"]) / dest
            ok, msg = clone_image(src_path, dest_path, force=False)
            if not ok:
                return self._json({"error": msg}, code=400)
            return self._json({"response": msg})

        if u.path == "/api/config/select":
            name = param("name", "").strip()
            safe = safe_cfg_name(name)
            if not safe:
                return self._json({"error": "invalid config name"}, code=400)
            cfg_path = Path(self.cfg["cfg_dir"]) / safe
            if not cfg_path.exists():
                return self._json({"error": f"not found: {cfg_path}"}, code=404)
            self.cfg["cfg"] = str(cfg_path)
            cfg = parse_default_cfg(self.cfg["cfg"])
            kickstarts = list_dir_files(self.cfg["kick_dir"], {".rom", ".bin"})
            hdfs = list_dir_files(self.cfg["hdf_dir"], {".hdf"})
            return self._json(
                {
                    "config": cfg,
                    "kickstarts": kickstarts,
                    "hdfs": hdfs,
                    "cfg_file": safe,
                }
            )

        if u.path == "/api/config/create":
            name = param("name", "").strip()
            base = param("base", "").strip() or os.path.basename(self.cfg["cfg"])
            safe = safe_cfg_name(name)
            safe_base = safe_cfg_name(base)
            if not safe:
                return self._json({"error": "invalid config name"}, code=400)
            if not safe_base:
                return self._json({"error": "invalid base config"}, code=400)
            dest = Path(self.cfg["cfg_dir"]) / safe
            src = Path(self.cfg["cfg_dir"]) / safe_base
            if dest.exists():
                return self._json({"error": f"exists: {dest}"}, code=400)
            if src.exists():
                shutil.copy2(src, dest)
            else:
                dest.write_text("")
            return self._json({"response": f"created {dest.name}"})

        if u.path == "/api/config/activate":
            name = param("name", "").strip()
            safe = safe_cfg_name(name)
            if not safe:
                return self._json({"error": "invalid config name"}, code=400)
            src = Path(self.cfg["cfg_dir"]) / safe
            if not src.exists():
                return self._json({"error": f"not found: {src}"}, code=404)
            dest = Path(self.cfg["default_cfg"])
            if src.resolve() != dest.resolve():
                shutil.copy2(src, dest)
            return self._json({"response": f"activated {safe}"})

        if u.path == "/api/config":
            if json_body is not None:
                data = json_body
            else:
                data = {k: v[0] for k, v in form.items()}

            kick = data.get("kickstart") or None
            if kick and not os.path.isabs(kick):
                kick = os.path.join(self.cfg["kick_dir"], kick)

            piscsi_paths = {}
            for unit, val in (data.get("piscsi", {}) or {}).items():
                if not val:
                    piscsi_paths[unit] = ""
                    continue
                if not os.path.isabs(val):
                    val = os.path.join(self.cfg["hdf_dir"], val)
                piscsi_paths[unit] = val

            def to_bool(val) -> bool:
                if isinstance(val, bool):
                    return val
                return str(val).lower() in {"1", "true", "yes", "on"}

            def to_int(val, default=0) -> int:
                try:
                    return int(val)
                except (TypeError, ValueError):
                    return default

            keyboard = data.get("keyboard", {}) or {}
            mouse = data.get("mouse", {}) or {}
            rtc_val = data.get("enable_rtc_emulation")
            rtc = None
            if rtc_val not in (None, ""):
                try:
                    rtc = int(rtc_val)
                except ValueError:
                    rtc = None

            new_cfg = {
                "cpu": data.get("cpu") or None,
                "kickstart": kick,
                "cpu_slot_mb": to_int(data.get("cpu_slot_mb", 0)),
                "z2_mb": to_int(data.get("z2_mb", 0)),
                "z3_mb": to_int(data.get("z3_mb", 0)),
                "chip_ram": to_bool(data.get("chip_ram")),
                "loopcycles": to_int(data.get("loopcycles", 0)),
                "platform": data.get("platform") or "amiga",
                "rtg": to_bool(data.get("rtg")),
                "rtg_dpms": to_bool(data.get("rtg_dpms")),
                "rtg_width": to_int(data.get("rtg_width", 0)),
                "rtg_height": to_int(data.get("rtg_height", 0)),
                "pi_ahi": to_bool(data.get("pi_ahi")),
                "pi_ahi_device": data.get("pi_ahi_device") or "",
                "pi_ahi_samplerate": to_int(data.get("pi_ahi_samplerate", 0)),
                "cdtv": to_bool(data.get("cdtv")),
                "enable_rtc_emulation": rtc,
                "pi_net": to_bool(data.get("pi_net")),
                "a314": True,
                "a314_conf": data.get("a314_conf") or DEFAULT_A314_CONF,
                "move_slow_to_chip": to_bool(data.get("move_slow_to_chip")),
                "swap_df0_df": to_int(data.get("swap_df0_df", 0)),
                "physical_z2_first": to_bool(data.get("physical_z2_first")),
                "kick13": to_bool(data.get("kick13")),
                "no_pistorm_dev": to_bool(data.get("no_pistorm_dev")),
                "keyboard": {
                    "enabled": to_bool(keyboard.get("enabled")),
                    "key": keyboard.get("key") or "k",
                    "grab": to_bool(keyboard.get("grab")),
                    "autoconnect": to_bool(keyboard.get("autoconnect")),
                },
                "kbfile": data.get("kbfile") or "",
                "mouse": {
                    "enabled": to_bool(mouse.get("enabled")),
                    "file": mouse.get("file") or "/dev/input/mice",
                    "key": mouse.get("key") or "m",
                    "autoconnect": to_bool(mouse.get("autoconnect")),
                },
                "piscsi_enable": to_bool(data.get("piscsi_enable")),
                "piscsi": piscsi_paths,
            }

            if not new_cfg["piscsi_enable"]:
                new_cfg["piscsi"] = {str(i): "" for i in range(7)}

            ok, msg = update_default_cfg(self.cfg["cfg"], new_cfg)
            if not ok:
                return self._json({"error": msg}, code=400)
            return self._json({"response": msg})

        return self._json({"error": "not found"}, code=404)

    def log_message(self, fmt, *args):
        # quieter
        return


def cmd_web(args) -> int:
    host = args.web_host
    port = args.web_port
    httpd = ThreadingHTTPServer((host, port), Handler)
    httpd.cfg = {  # type: ignore[attr-defined]
        "host": args.host,
        "port": args.port,
        "dir": args.dir,
        "cfg": args.cfg,
        "cfg_dir": args.cfg_dir,
        "default_cfg": args.cfg,
        "kick_dir": args.kick_dir,
        "hdf_dir": args.hdf_dir,
        "css": args.css,
    }
    print(f"ADF Manager web UI: http://{host}:{port}/")
    print(f"Controls disk service at: {args.host}:{args.port}")
    httpd.serve_forever()
    return 0


def build_parser():
    p = argparse.ArgumentParser(description="Manage A314 disk service images (ADF/HDF).")
    p.add_argument("--dir", default=DEFAULT_ADF_DIR, help=f"image directory (default {DEFAULT_ADF_DIR})")
    p.add_argument("--host", default=DEFAULT_HOST, help="disk control host (default 127.0.0.1)")
    p.add_argument("--port", type=int, default=DEFAULT_PORT, help="disk control port (default 23890)")
    p.add_argument("--cfg", default=DEFAULT_CFG_PATH, help=f"config file (default {DEFAULT_CFG_PATH})")
    p.add_argument("--cfg-dir", default=DEFAULT_CFG_DIR, help=f"config directory (default {DEFAULT_CFG_DIR})")
    p.add_argument("--kick-dir", default=DEFAULT_KICK_DIR, help=f"kickstart dir (default {DEFAULT_KICK_DIR})")
    p.add_argument("--hdf-dir", default=DEFAULT_HDF_DIR, help=f"hdf dir (default {DEFAULT_HDF_DIR})")
    p.add_argument("--css", default=DEFAULT_CSS_PATH, help=f"css file (default {DEFAULT_CSS_PATH})")
    sub = p.add_subparsers(dest="cmd", required=True)

    sp = sub.add_parser("list", help="list .adf/.hdf images")
    sp.set_defaults(fn=cmd_list)

    sp = sub.add_parser("eject", help="eject image from unit")
    sp.add_argument("unit", type=int, choices=[0, 1, 2, 3])
    sp.set_defaults(fn=cmd_eject)

    sp = sub.add_parser("insert", help="insert image into unit")
    sp.add_argument("unit", type=int, choices=[0, 1, 2, 3])
    sp.add_argument("image", help="path or filename (relative to --dir)")
    sp.add_argument("--rw", action="store_true", help="make image writable")
    sp.add_argument("--auto", action="store_true", help="pick next free unit if occupied")
    sp.set_defaults(fn=cmd_insert)

    sp = sub.add_parser("status", help="show disk service status")
    sp.set_defaults(fn=cmd_status)

    sp = sub.add_parser("create", help="create blank ADF")
    sp.add_argument("name", help="filename for new image")
    sp.add_argument("--volume", default="BLANK", help="volume name")
    sp.add_argument("--force", action="store_true", help="overwrite existing")
    sp.set_defaults(fn=cmd_create)

    sp = sub.add_parser("clone", help="clone image")
    sp.add_argument("src", help="source image")
    sp.add_argument("dest", help="destination filename")
    sp.add_argument("--force", action="store_true", help="overwrite existing")
    sp.set_defaults(fn=cmd_clone)

    sp = sub.add_parser("web", help="run a simple web UI")
    sp.add_argument("--web-host", default="0.0.0.0", help="bind host (default 0.0.0.0)")
    sp.add_argument("--web-port", type=int, default=8088, help="bind port (default 8088)")
    sp.set_defaults(fn=cmd_web)

    return p


def main(argv: list[str]) -> int:
    args = build_parser().parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
