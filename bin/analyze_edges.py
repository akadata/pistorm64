#!/usr/bin/env python3
import zipfile
from collections import Counter

path="/tmp/floppy_debug_capture.srzip"
edges=[0]*8
prev=None
offset=0

with zipfile.ZipFile(path,'r') as z:
    names=sorted([n for n in z.namelist() if n.startswith('logic-1-')], key=lambda x:int(x.split('-')[-1]))
    for name in names:
        buf=z.read(name)
        start=1 if prev is None else 0
        if prev is None: 
            prev=buf[0]
        for b in buf[start:]:
            diff=prev^b
            if diff:
                for bit in range(8):
                    if diff>>bit & 1:
                        edges[bit]+=1
            prev=b

print("edge counts per bit D0..D7:", edges)