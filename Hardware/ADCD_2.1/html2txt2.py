#!/usr/bin/env python3
import re
import html
from pathlib import Path

ROOT = Path(".")          # run from Hardware/ADCD_2.1
OUT  = Path("_txt")

# crude tag stripper (fast)
tag_re = re.compile(r"<[^>]+>")
ws_re  = re.compile(r"\s+")

# optional: pull <title> for context
title_re = re.compile(r"<title[^>]*>(.*?)</title>", re.IGNORECASE | re.DOTALL)

def html_to_text(s: str) -> str:
    s = html.unescape(s)
    # keep some structure hints
    s = s.replace("</h1>", "\n").replace("</h2>", "\n").replace("</h3>", "\n")
    s = s.replace("<br>", "\n").replace("<br/>", "\n").replace("<br />", "\n")
    s = s.replace("</p>", "\n").replace("</li>", "\n")
    s = tag_re.sub(" ", s)
    # collapse whitespace but keep newlines meaningful
    s = re.sub(r"[ \t]+\n", "\n", s)
    s = re.sub(r"\n{3,}", "\n\n", s)
    s = ws_re.sub(" ", s)
    return s.strip()

def get_title(raw: str) -> str:
    m = title_re.search(raw)
    if not m:
        return ""
    t = tag_re.sub(" ", html.unescape(m.group(1)))
    return ws_re.sub(" ", t).strip()

OUT.mkdir(parents=True, exist_ok=True)

for p in ROOT.rglob("*.html"):
    rel = p.relative_to(ROOT)
    out = OUT / (str(rel) + ".txt")
    out.parent.mkdir(parents=True, exist_ok=True)

    raw = p.read_text(errors="ignore")
    title = get_title(raw)
    text = html_to_text(raw)

    # add a strong “source header” so Codex can cite/trace
    header = f"SOURCE: {rel}\n"
    if title:
        header += f"TITLE: {title}\n"
    header += "\n"

    out.write_text(header + text + "\n", encoding="utf-8")

print(f"Wrote txt corpus to: {OUT.resolve()}")
