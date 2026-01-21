# PIMODPLAY - Direct Audio Playback to Amiga Paula Chip

## Overview

PIMODPLAY is a groundbreaking tool that enables direct audio playback from the Raspberry Pi to the Amiga's Paula custom chip without running the emulator. This represents the first demonstration of non-emulation code communicating directly with Amiga 500 OCS/ECS hardware through the PiStorm interface.

The code is located in `src/platforms/amiga/registers/pimodplay.c` alongside other custom chip register interfaces.

## Building PIMODPLAY

First, build the pimodplay executable:

```bash
./build_pimodplay.sh
```

## Audio File Preparation

### Using Sample Music (Madonna - Holiday)

For testing, you can use Madonna's "Holiday" which is freely available from Archive.org:
[https://archive.org/details/the-holiday-collection-cd-ep-compilation-europe-sire-9362-40099-2-warner-bros.-r](https://archive.org/details/the-holiday-collection-cd-ep-compilation-europe-sire-9362-40099-2-warner-bros.-r)

### Converting Audio Files

Once you have your audio file (MP3, MP4, WAV, etc.), use the conversion script:

```bash
./tools/pimodplay_ffmpeg_convert.sh <input_file> <sample_rate> <channels>
```

**Example:**
```bash
./tools/pimodplay_ffmpeg_convert.sh ../Madonna_-Holiday.mp4 27928 stereo
```

This will generate three files:
- `<filename>_<rate>_<channels>_u8.wav` - Converted WAV file
- `<filename>_<rate>_<channels>_u8.raw` - Raw audio data
- `<filename>_<rate>_<channels>_u8.txt` - Conversion information

### Supported Formats

The conversion script prepares audio in the correct format for Amiga Paula chip:
- Sample rates: 27928Hz (recommended for PAL Amiga)
- Channels: mono or stereo
- Bit depth: 8-bit unsigned

## Playing Audio

### Using Converted Files

Play the converted audio file directly to the Amiga:

```bash
sudo ./pimodplay --wav <converted_wav_file> --stereo --vol 64 --stream-chunk-bytes 131070 --buffers 3 --pal
```

**Example:**
```bash
sudo ./pimodplay --wav ../Madonna_-Holiday_27928Hz_stereo_u8.wav --stereo --vol 64 --stream-chunk-bytes 131070 --buffers 3 --pal
```

### Demo Mode

Even without audio files, pimodplay includes a built-in demo:

```bash
sudo ./pimodplay --saints
```

This plays "Oh When the Saints" in mono - our first successful attempt at direct Amiga audio output before implementing WAV/raw file support.

## Command Line Options

### Raw Sample Playback (AUD0 DMA)
- `--raw <file>`: 8-bit unsigned sample data
- `--wav <file>`: WAV PCM mono/stereo (8/16-bit)
- `--addr <hex>`: Chip RAM load address (default 0x00080000)
- `--period <val>`: Paula period (default 200)
- `--rate <hz>`: Sample rate (overrides --period)
- `--vol <0-64>`: Volume (default 64)
- `--seconds <n>`: Playback duration (default 5)
- `--stream`: Stream long samples in chunks
- `--chunk-bytes <n>`: Stream chunk size (default 131070 bytes)
- `--buffers <n>`: Stream buffers in Chip RAM (default 2)
- `--u8`: Raw input is unsigned 8-bit (default for --raw)
- `--s8`: Raw input is signed 8-bit
- `--stereo`: Raw/WAV is stereo interleaved (AUD0/AUD1)
- `--mono`: Force mono (downmix if WAV is stereo)
- `--lpf <hz>`: Apply low-pass filter (Hz)

### Built-in Tune
- `--saints`: Play "When the Saints" on AUD0
- `--tempo <bpm>`: Tune tempo (default 180)
- `--gate <0.0-1.0>`: Note gate ratio (default 0.70)

### MOD Playback (Basic)
- `--mod <file>`: ProTracker MOD (4-channel, basic effects)

**Note:** MOD file playback is currently a placeholder feature. MOD playback is not yet implemented and is To Be Completed (TBC). This feature will be added as soon as development resources allow.

### Timing
- `--pal` (default): 50 Hz tick base
- `--ntsc`: 60 Hz tick base

### Control
- `--stop`: Stop audio DMA and mute

## Important Notes

### Troubleshooting

- **No Audio:** If no audio plays, try resetting or power cycling your Amiga 500
- **Power Sequence:** If the Pi is powered from the Amiga, always shut down the Pi first before turning off the Amiga
- **PAL/NTSC:** Use `--pal` flag for PAL Amiga systems, omit for NTSC

### Current Limitations

- This is a work-in-progress demonstration
- Best tested on PAL Amiga systems
- Audio quality depends on sample rate and Amiga hardware configuration

## Technical Details

PIMODPLAY communicates directly with the Amiga's Paula custom chip through custom register interfaces in the PiStorm system. Rather than emulating the entire Amiga system, it sends pre-formatted audio data directly to the hardware registers, bypassing the emulator entirely.

The audio data is buffered and streamed in chunks to maintain consistent playback without interruption.

## Files Location

The source code and related files are located at:
```
src/platforms/amiga/registers/
├── pimodplay.c        # Main pimodplay source code
├── paula.h            # Paula chip register definitions
├── agnus.h            # Agnus chip register definitions
├── denise.h           # Denise chip register definitions
├── blitter.h          # Blitter chip register definitions
├── cia.h              # CIA chip register definitions
├── amiga_custom_chips.h # Common custom chip definitions
```

## Known Issues

### Audio Channel Problem
Currently, pimodplay has limitations in how it utilizes the Amiga's four audio channels. Analysis of the code reveals:

- For raw mono playback (`--raw` or `--wav` without `--stereo`), pimodplay uses only **Channel 0** (AUD0)
- For stereo streaming (`--wav` with `--stereo`), pimodplay uses **Channels 0 and 1** (AUD0 and AUD1)
- The streaming stereo implementation assigns left channel to AUD0 and right channel to AUD1
- Channels 2 and 3 (AUD2 and AUD3) are not currently utilized in typical playback

According to Amiga hardware specifications, the correct stereo assignment should be:
- **Left Output:** Channels 0 and 3 (AUD0 and AUD3)
- **Right Output:** Channels 1 and 2 (AUD1 and AUD2)

This means the current stereo implementation is using AUD0/AUD1 instead of the optimal AUD0/AUD3 and AUD1/AUD2 combination, which could affect the audio output characteristics.

**Audio Channel Registers:**
- **Channel 0:** AUD0LCH (0x0A0), AUD0LCL (0x0A2), AUD0LEN (0x0A4), AUD0PER (0x0A6), AUD0VOL (0x0A8), AUD0DAT (0x0AA)
- **Channel 1:** AUD1LCH (0x0B0), AUD1LCL (0x0B2), AUD1LEN (0x0B4), AUD1PER (0x0B6), AUD1VOL (0x0B8), AUD1DAT (0x0BA)
- **Channel 2:** AUD2LCH (0x0C0), AUD2LCL (0x0C2), AUD2LEN (0x0C4), AUD2PER (0x0C6), AUD2VOL (0x0C8), AUD2DAT (0x0CA)
- **Channel 3:** AUD3LCH (0x0D0), AUD3LCL (0x0D2), AUD3LEN (0x0D4), AUD3PER (0x0D6), AUD3VOL (0x0D8), AUD3DAT (0x0DA)

**How Amiga Stereo Works:**
- **Four Channels:** The Paula chip has four hardware audio channels (0, 1, 2, 3) for 8-bit sound.
- **Left/Right Assignment:**
  - Left: Channels 0 and 3 feed the left audio jack.
  - Right: Channels 1 and 2 feed the right audio jack.
- **Independent Control:** Each channel has its own 6-bit volume control (0-64) and sample rate settings, allowing for rich, separated sound.
- **Stereo Output:** The hardware mixes these four channels into two distinct outputs (left and right) via the Amiga's RCA jacks.

For optimal stereo output, pimodplay should be updated to properly utilize all four channels according to the correct left/right assignment.

## Status

This is a very much work-in-progress demo, but it does work reliably. It represents a significant advancement in PiStorm technology by enabling direct hardware communication without emulation overhead. However, the audio channel implementation needs refinement to properly utilize all four Paula audio channels for correct stereo output.

