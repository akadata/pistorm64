#!/usr/bin/env bash
# pimodplay_ffmpeg_convert.sh
# Convert MP3/FLAC/WAV/etc to Amiga-friendly PCM for Paula-style 8-bit playback.
# 
# Outputs:
#   - WAV (PCM_U8) for easy testing on the Pi / emu side
#   - RAW 8-bit unsigned PCM (headerless) for ultra-simple players
# 
# Why unsigned?
#   The classic Amiga audio DMA expects 8-bit unsigned PCM samples (0..255).
#   Many tracker samples are stored signed 8-bit, but the hardware is unsigned.
#   If your playback code expects signed 8-bit instead, set SAMPLE_FMT=s8 below.
#
# Usage:
#   ./pimodplay_ffmpeg_convert.sh input.mp3
#   ./pimodplay_ffmpeg_convert.sh input.mp3 22050
#   ./pimodplay_ffmpeg_convert.sh input.mp3 27928 stereo
#
# Notes:
#   - Default rate is 27928 Hz (common-ish “Amiga-ish” target for decent quality)
#   - Default is mono (Paula has 4 mono channels; mono source simplifies mixing)
#   - The filter chain normalizes + applies a gentle limiter to avoid clipping.

set -euo pipefail

IN=${1:-}
RATE=${2:-27928}          # try 11025, 22050, 27928, 44100
CHMODE=${3:-mono}         # mono or stereo

if [[ -z "${IN}" || ! -f "${IN}" ]]; then
  echo "Usage: $0 <input.(mp3|wav|flac|...)> [rate] [mono|stereo]" >&2
  exit 2
fi

case "${CHMODE}" in
  mono)   CH=1 ;;
  stereo) CH=2 ;;
  *) echo "CHMODE must be mono or stereo" >&2; exit 2 ;;
 esac

# Choose sample format:
#   u8  = unsigned 8-bit PCM (Amiga DMA friendly)
#   s8  = signed 8-bit PCM (use if your code expects signed)
SAMPLE_FMT=u8

# Output base name
BASE=$(basename -- "${IN}")
BASE=${BASE%.*}

OUT_WAV="${BASE}_${RATE}Hz_${CHMODE}_${SAMPLE_FMT}.wav"
OUT_RAW="${BASE}_${RATE}Hz_${CHMODE}_${SAMPLE_FMT}.raw"
OUT_INFO="${BASE}_${RATE}Hz_${CHMODE}_${SAMPLE_FMT}.txt"

# Audio processing:
#   - highpass: remove DC/rumble
#   - acompressor: gentle dynamic control
#   - loudnorm: normalise to a sane listening level
#   - alimiter: avoid clipping after 8-bit quantisation
#
# You can tweak these if you prefer “hotter” or “softer” output.
AFILTER="highpass=f=20,acompressor=threshold=-18dB:ratio=3:attack=5:release=80,\
         loudnorm=I=-16:LRA=11:TP=-1.5,\
         alimiter=limit=0.95"

# 1) WAV output (headered) for easy checking
ffmpeg -hide_banner -y -i "${IN}" \
  -vn -ar "${RATE}" -ac "${CH}" \
  -af "${AFILTER}" \
  -c:a pcm_${SAMPLE_FMT} "${OUT_WAV}"

# 2) RAW output (headerless)
# For RAW we force the exact sample fmt and container.
ffmpeg -hide_banner -y -i "${OUT_WAV}" \
  -vn -f ${SAMPLE_FMT} -acodec pcm_${SAMPLE_FMT} \
  "${OUT_RAW}"

# 3) Emit a little metadata file so you don’t forget what you made
{
  echo "input:    ${IN}"
  echo "rate:     ${RATE} Hz"
  echo "channels: ${CH} (${CHMODE})"
  echo "format:   PCM_${SAMPLE_FMT} (8-bit ${SAMPLE_FMT})"
  echo "wav:      ${OUT_WAV}"
  echo "raw:      ${OUT_RAW}"
  echo
  echo "If your player expects signed 8-bit, edit SAMPLE_FMT=s8 and re-run."
  echo "If audio is too quiet/loud: adjust loudnorm I= and/or add volume=1.2 at the end of AFILTER."
} > "${OUT_INFO}"

echo "Wrote: ${OUT_WAV}"
echo "Wrote: ${OUT_RAW}"
echo "Wrote: ${OUT_INFO}"

