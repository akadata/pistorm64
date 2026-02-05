#!/usr/bin/env bash
set -euo pipefail

# Usage: ./pimodplay.sh <file.wav|file.raw>
# Optional overrides:
#   VOL=64 STREAM=1 STEREO=1 CHUNK=65536 BUFFERS=3 ADDR=0x00060000 SECONDS=0
#   PAL=1 (if your tool supports it; otherwise ignored)

f="${1:-}"

if [[ -z "$f" ]]; then
  echo "Usage: $0 <file.wav|file.raw>"
  exit 2
fi

# If user typed a file that doesn't exist, try to suggest something nearby.
suggest_file() {
  local in="$1"
  local dir base
  dir="$(dirname -- "$in")"
  base="$(basename -- "$in")"
  [[ -d "$dir" ]] || dir="."

  # 1) case-insensitive exact-ish match
  local cand
  cand="$(ls -1 "$dir" 2>/dev/null | awk -v b="$base" 'tolower($0)==tolower(b){print $0; exit}')"
  if [[ -n "${cand:-}" ]]; then
    echo "$dir/$cand"
    return 0
  fi

  # 2) cheap Levenshtein-ish: use python if available, else skip
  if command -v python3 >/dev/null 2>&1; then
    python3 - "$dir" "$base" <<'PY'
import os, sys

dirp, target = sys.argv[1], sys.argv[2]

def lev(a,b):
    # small DP, fine for filenames
    n,m=len(a),len(b)
    dp=list(range(m+1))
    for i,ch in enumerate(a,1):
        prev, dp[0] = dp[0], i
        for j,ch2 in enumerate(b,1):
            cur=dp[j]
            cost=0 if ch==ch2 else 1
            dp[j]=min(dp[j]+1, dp[j-1]+1, prev+cost)
            prev=cur
    return dp[m]

best=None
for name in os.listdir(dirp):
    d=lev(name.lower(), target.lower())
    if best is None or d < best[0]:
        best=(d,name)

if best and best[0] <= 3:  # threshold
    print(os.path.join(dirp, best[1]))
PY
    return 0
  fi
  return 1
}

if [[ ! -f "$f" ]]; then
  if s="$(suggest_file "$f")" && [[ -n "${s:-}" ]] && [[ -f "$s" ]]; then
    echo "File not found: $f"
    echo "Did you mean: $s ?"
    f="$s"
  else
    echo "File not found: $f"
    exit 1
  fi
fi

ext="${f##*.}"
ext_lc="$(printf "%s" "$ext" | tr '[:upper:]' '[:lower:]')"

VOL="${VOL:-64}"
STREAM="${STREAM:-1}"
STEREO="${STEREO:-1}"
CHUNK="${CHUNK:-65536}"     # 64k is safer than 131070
BUFFERS="${BUFFERS:-3}"
ADDR="${ADDR:-0x00060000}"
SECONDS="${SECONDS:-0}"

args=()
if [[ "$ext_lc" == "wav" ]]; then
  args+=(--wav "$f")
elif [[ "$ext_lc" == "raw" ]]; then
  args+=(--raw "$f")
else
  echo "Unsupported file type: .$ext"
  echo "Expected .wav or .raw"
  exit 3
fi

if [[ "$STEREO" == "1" ]]; then args+=(--stereo); fi
args+=(--vol "$VOL")
if [[ "$STREAM" == "1" ]]; then args+=(--stream); fi
args+=(--chunk-bytes "$CHUNK" --buffers "$BUFFERS" --addr "$ADDR")

# Only add --seconds if non-zero
if [[ "$SECONDS" != "0" ]]; then
  args+=(--seconds "$SECONDS")
fi

echo "Time to make some music..."
echo "sudo ./pimodplay ${args[*]}"
sudo ./pimodplay "${args[@]}"

