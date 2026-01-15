# PiStorm Socket Communication System

This directory contains all documentation and scripts for the socket-based communication system between the Pi Zero W2 (amiga) and the host machine (homer).

## Purpose
The socket communication system enables coordinated operations between:
- **homer**: Host machine (Intel i9) - runs sigrok captures and marker logging
- **amiga**: Pi Zero W2 - runs the PiStorm emulator and register tools

## Important Note: System Architecture Change
**The Pi4 is now offline and NFS network storage is unavailable.** All operations now use the socket-based communication system described below.

## Components

### Documentation
- `NETWORK_PIPE.md` - Comprehensive documentation of the socket pipe system
- `DISKIO.md` - Disk I/O system documentation
- `KERNEL_MODULE_PROPOSAL.md` - Kernel module implementation proposal

### Scripts
- `listen_markers.sh` - Starts marker listener on homer
- `send_marker.sh` - Sends markers from amiga to homer
- `orchestrate_run.sh` - Coordinated capture orchestration script
- `orchestrate_floppy_test.sh` - Floppy disk reading orchestration script

## Setup Requirements
- SSH access between homer and amiga
- socat installed on both systems
- netcat (nc) installed on both systems
- Network connectivity between both systems

## Usage

### 1. Basic Marker Communication
```bash
# On homer (start listener)
./listen_markers.sh

# On amiga (send markers)
./send_marker.sh "MOTOR_ON"
./send_marker.sh "SEEK_T0"
./send_marker.sh "DMA_START"
```

### 2. Coordinated Capture
```bash
# 1. Start marker listener on homer
./listen_markers.sh capture_session_1

# 2. Start coordinated run
./orchestrate_run.sh session_1

# 3. Results:
# - Capture: captures/session_1.srzip
# - Markers: ipc/session_1_markers.log
```

## Architecture
The system uses a TCP-based socket pipe on port 9009 to enable communication between the two systems, allowing for synchronized operations, marker logging, and coordinated captures.

## Security Considerations
- Port 9009 should only be accessible to trusted systems
- Use SSH key authentication for secure connections
- Consider using isolated network segments for sensitive operations

## Migration from Pi4/NFS
With the Pi4 offline and NFS unavailable:
- All file operations now happen locally on each system
- Socket communication replaces NFS-based file sharing
- Capture files are stored locally and accessed via coordinated operations
- Marker logs are collected via the socket system