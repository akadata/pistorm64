#!/usr/bin/env python3
import os, re, html
from pathlib import Path

ROOT = Path("Hardware/ADCD_2.1")
OUT  = ROOT / "_txt"

tag_re = re.compile(r"<[^>]+>")
ws_re  = re.compile(r"\s+")

for p in ROOT.rglob("*.html"):
    rel = p.relative_to(ROOT)
    out = OUT / (str(rel) + ".txt")
    out.parent.mkdir(parents=True, exist_ok=True)

    data = p.read_text(errors="ignore")
    data = html.unescape(data)           # handles &nbsp; &amp; etc broadly
    data = tag_re.sub(" ", data)         # crude but fast
    data = ws_re.sub(" ", data).strip()

    out.write_text(data)

