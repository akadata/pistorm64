import math, struct
sr = 8000
freq = 440
secs = 1.0
n = int(sr*secs)
with open("tone.raw","wb") as f:
    for i in range(n):
        s = math.sin(2*math.pi*freq*i/sr)
        v = int(128 + 127*s)  # unsigned 8-bit
        f.write(struct.pack("B", v))
print("wrote tone.raw", n, "bytes")
