# RNG / SSL / AES Zorro Device Design

This page collects design notes for Amiga Zorro devices backed by host cryptography.

## Goals

- **Z2/Z3 RNG card** backed by host entropy (hardware RNG/AES when available).
- **Z2/Z3 SSL/AES accelerator** backed by OpenSSL/EVP on the host.
- Optional **remote crypto engines** using L2/MAC addressing (no IP/TCP/UDP).

## Host crypto backends

### x86_64
- AES-NI for AES-128/192/256
- SHA extensions where present

### arm64 (Pi)
- ARMv8 AES/SHA extensions
- SoC RNG when available

### Fallback
- Software EVP paths when hardware acceleration is not available

## Zorro device register maps

### RNG card (minimal)
- `0x00` **DATA32** (R): read random 32-bit value
- `0x04` **STATUS** (R):
  - bit0 = DATA_READY
  - bit1 = ENTROPY_OK
- `0x08` **CTRL** (W):
  - bit0 = CLEAR FIFO / reset

### AES/SSL card (initial sketch)
- **Command FIFO** (op codes for encrypt/decrypt/hash)
- **Key slots** (host-only, opaque to Amiga if desired)
- **Data FIFO** in/out
- **STATUS** / **ERROR** registers

## Amiga-side software model

- Simple libraries:
  - `pirng.library`
  - `pissa.library` (AES/SSL)
- Device drivers:
  - `z2rng.device`
  - `z2aes.device`

## Remote crypto engines (L2)

- MAC-based addressing (no IP stack dependency)
- Simple frame formats for RNG/AES requests
- Host is a relay between Amiga Zorro card and remote engine

## Security / trust model

- Entropy source is host-defined
- Keys can be host-only (opaque) or Amiga-loaded
- Optional policy to restrict off-box use

## Test plan

### Local host
- RNG output stability and performance
- AES/SSL regression tests vs OpenSSL reference

### Remote L2
- Latency/throughput tests
- Key handling and error propagation
n
## Notes

This is a working design space. Keep evolving as z3bus stabilizes and device registration matures.
