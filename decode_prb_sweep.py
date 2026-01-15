#!/usr/bin/env python3
"""
Decode a PRB sweep capture (srzip) and report edge counts/times per channel.
Assumes PRB bits 7..0 were toggled sequentially during the capture.
"""
import sys
import zipfile

def main(path: str) -> None:
    sr = 24_000_000
    edges = [[] for _ in range(8)]
    prev = None
    offset = 0
    with zipfile.ZipFile(path, "r") as z:
        names = sorted(
            [n for n in z.namelist() if n.startswith("logic-1-")],
            key=lambda x: int(x.split("-")[-1]),
        )
        for name in names:
            buf = z.read(name)
            start = 1 if prev is None else 0
            if prev is None:
                prev = buf[0]
            for i, b in enumerate(buf[start:], start):
                diff = prev ^ b
                if diff:
                    for bit in range(8):
                        if diff & (1 << bit):
                            edges[bit].append(offset + i)
                prev = b
            offset += len(buf)

    if not any(edges):
        print("No edges found.")
        return

    for ch, e in enumerate(edges):
        if e:
            print(
                f"ch{ch}: {len(e)} edges, first {e[0]/sr:.6f}s"
                f" last {e[-1]/sr:.6f}s"
            )

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: decode_prb_sweep.py <capture.srzip>")
        sys.exit(1)
    main(sys.argv[1])
