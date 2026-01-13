#!/usr/bin/env bash
set -euo pipefail

# Convert audio files into Amiga-friendly PCM:
# - mono
# - 8-bit signed (s8)
# - selectable sample rate
# Outputs: .raw + .wav + .txt sidecar

RATE="${RATE:-22050}"          # try 11025, 22050, 44100
GAIN_DB="${GAIN_DB:-0}"        # e.g. -3, 0, +3
MODE="${MODE:-fast}"           # fast | norm
OUTDIR="${OUTDIR:-out}"

mkdir -p "$OUTDIR"

need() { command -v "$1" >/dev/null 2>&1 || { echo "Missing: $1"; exit 1; }; }
need ffmpeg
need ffprobe

# Stereo -> mono mixer (keeps levels sane)
MONO_MIX="pan=mono|c0=0.5*c0+0.5*c1"

# FAST: very quick resample + optional gain
AF_FAST="${MONO_MIX},aresample=${RATE},volume=${GAIN_DB}dB"

# NORM: adds a gentle compressor (faster than loudnorm/dynaudnorm), then resample
# If you *really* want loudnorm, it will be slower.
AF_NORM="${MONO_MIX},acompressor=threshold=-18dB:ratio=3:attack=5:release=50:makeup=6,aresample=${RATE},volume=${GAIN_DB}dB"

AFILTER="$AF_FAST"
if [[ "$MODE" == "norm" ]]; then
  AFILTER="$AF_NORM"
fi

for inpath in "$@"; do
  [[ -f "$inpath" ]] || { echo "Not a file: $inpath"; continue; }

  base="$(basename "$inpath")"
  stem="${base%.*}"
  raw="$OUTDIR/${stem}_${RATE}_s8_mono.raw"
  wav="$OUTDIR/${stem}_${RATE}_s8_mono.wav"
  txt="$OUTDIR/${stem}_${RATE}_s8_mono.txt"

  echo "==> $inpath"
  echo "    mode=$MODE rate=$RATE gain=${GAIN_DB}dB"

  # RAW (headerless signed 8-bit)
  ffmpeg -hide_banner -nostdin -y \
    -i "$inpath" \
    -vn \
    -af "$AFILTER" \
    -ac 1 -ar "$RATE" -sample_fmt s8 \
    -f s8 "$raw"

  # WAV (PCM signed 8-bit)
  ffmpeg -hide_banner -nostdin -y \
    -i "$inpath" \
    -vn \
    -af "$AFILTER" \
    -ac 1 -ar "$RATE" -c:a pcm_s8 \
    "$wav"

  # Sidecar info
  bytes="$(wc -c < "$raw" | tr -d ' ')"
  secs="$(ffprobe -v error -show_entries format=duration -of default=nk=1:nw=1 "$wav" | awk '{printf "%.3f",$1}')"

  cat > "$txt" <<EOF
source=$inpath
mode=$MODE
rate_hz=$RATE
format=pcm_s8_mono
raw_bytes=$bytes
duration_s=$secs
gain_db=$GAIN_DB
EOF

  echo "    -> $raw"
  echo "    -> $wav"
  echo "    -> $txt"
done

