# Debugging Guide for PiStorm64

## A314 Memory Access Issue

### Problem Description
The A314 filesystem service crashes on Pi4 64-bit systems with a segmentation fault when mounting drives from the Amiga side. This occurs due to endianness issues in the socket protocol between Python clients and the C++ emulator, causing extremely large memory read requests (e.g., 0x10000000 bytes) that lead to allocation failures and SIGSEGV.

### Root Cause
Mixed endianness handling in the A314 socket protocol:
- Python clients were using native endianness (`=IIB`) for struct packing/unpacking
- This caused address/length fields to be interpreted incorrectly on ARM64 systems
- Values like `length = 0x00000010` (16 bytes) were byte-swapped to `0x10000000` (~256MB)

### Solution Applied
1. **Python Client Updates**: Changed all struct.pack/unpack calls from `'=IIB'` to `'<IIB'` for consistent little-endian handling
2. **C++ Server Hardening**: Added proper bounds checking and little-endian conversion using `le32toh()`/`htole32()`
3. **Memory Safety**: Added maximum limits (1MB) for memory read/write operations
4. **Defensive Programming**: Added connection closing on invalid requests

### Debug Build Target

#### Building with Debug Symbols
To build a version with debug symbols and no optimizations:

```bash
make clean
make PLATFORM=PI4_64BIT_DEBUG
```

This creates an emulator binary with:
- Debug symbols (`-g3`)
- No optimizations (`-O0`)
- Frame pointers preserved (`-fno-omit-frame-pointer`)
- Bounds checking enabled

#### Reproducing the Issue
1. Start the emulator:
   ```bash
   sudo ./emulator
   ```

2. On the Amiga side, mount a drive:
   ```amiga
   mount pi0:
   ```

3. The system should now handle the mount request without crashing

#### Debugging with GDB
For deeper investigation:

```bash
gdb ./emulator
(gdb) set environment LD_LIBRARY_PATH .
(gdb) run
```

Or attach to a running process:
```bash
gdb ./emulator <pid>
```

#### Key Functions to Examine
- `handle_msg_read_mem_req()` - Main handler for memory read requests
- `create_and_send_msg()` - Message construction and sending
- `handle_bytes_from_client()` - Socket data reception

#### Logging
The system logs warnings when invalid memory requests are detected:
- Invalid payload sizes
- Oversized read/write requests
- Requests outside mapped regions

### Verification Commands
Check that debug symbols are present:
```bash
readelf -S ./emulator | grep -E '\.(debug|zdebug)_'
file ./emulator
```

Expected: Debug sections exist and file is not stripped.