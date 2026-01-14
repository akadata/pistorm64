#!/usr/bin/env python3
"""
Recursively replace stale `.html` references with `.html.txt` in the ADCD _txt tree.
Idempotent: leaves existing `.html.txt` intact.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent / "Hardware" / "ADCD_2.1" / "_txt"


def main() -> int:
    if not ROOT.is_dir():
        print(f"Missing _txt directory: {ROOT}", file=sys.stderr)
        return 1

    html_re = re.compile(r"(?P<prefix>[^\\s\"']+?)(?P<stem>node\\d+\\.html)(?P<suffix>\\b)")
    files = sorted(ROOT.rglob("*.txt"))
    changed = 0
    for path in files:
        text = path.read_text(errors="ignore")
        if ".html" not in text:
            continue
        new_text = text
        # Replace any nodeXX.html with nodeXX.html.txt when not already .txt
        new_text = re.sub(r"node(\\d+)\\.html(?!\\.txt)", r"node\\1.html.txt", new_text)
        if new_text != text:
            path.write_text(new_text)
            changed += 1
    print(f"Processed {len(files)} files; updated {changed}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
