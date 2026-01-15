# NETWORK_PIPE.md - Socket Pipe Communication Between Pi Zero W2 (amiga) and Host (homer)

## Overview
This document describes the network socket pipe setup that enables coordinated communication between the Pi Zero W2 (running the PiStorm emulator) and the host machine (homer). This setup allows for synchronized operations, marker logging, and coordinated captures between the two systems.

## Important Note: System Architecture Change
**The Pi4 is now offline and NFS network storage is unavailable.** All operations now use the socket-based communication system described in this document. This replaces the previous NFS-based file sharing approach.

## Architecture
The system uses a TCP-based socket pipe to enable communication between:
- **homer**: Host machine (Intel i9) - runs sigrok captures and marker logging
- **amiga**: Pi Zero W2 - runs the PiStorm emulator and register tools

## Prerequisites
- SSH access between homer and amiga
- socat installed on both systems
- netcat (nc) installed on both systems
- Network connectivity between both systems

## Installation Requirements

### On homer (host machine):
```bash
# Install socat
sudo pacman -S socat  # Arch-based
# OR
sudo apt-get install socat  # Debian/Ubuntu

# Install moreutils for timestamping
sudo pacman -S moreutils  # Arch-based
# OR
sudo apt-get install moreutils  # Debian/Ubuntu
```

### On amiga (Pi Zero W2):
```bash
# Install socat
sudo apt-get update
sudo apt-get install socat netcat-openbsd
```

## Setup Process

### 1. Establish SSH Connection
Ensure SSH access from homer to amiga:
```bash
# Add to ~/.ssh/config on homer
Host amiga
    HostName <pi_zero_ip_address>
    Port 9022
    User smalley

# Test connection
ssh amiga
```

### 2. Create Directories
On homer, create necessary directories:
```bash
mkdir -p captures ipc
```

### 3. Socket Pipe Implementation

#### Option 1: Direct TCP Connection (Recommended)
Instead of using SSH reverse tunnel, use direct TCP connection:

**On homer (listener)**:
```bash
# Create persistent marker receiver
mkdir -p ipc
socat -d -d TCP-LISTEN:9009,reuseaddr,fork \
  SYSTEM:'ts "[%Y-%m-%d %H:%M:%S]" | tee -a ipc/amiga_markers.log'
```

**On amiga (sender)**:
```bash
# Send markers to homer
echo "MARKER_NAME $(date +%s.%N)" | socat - TCP:<homer_ip_address>:9009
```

#### Option 2: SSH Reverse Tunnel (Alternative)
If direct connection is not possible:

**Step A: On homer, run the listener**:
```bash
mkdir -p ipc
rm -f ipc/amiga.sock
socat -d -d UNIX-LISTEN:ipc/amiga.sock,fork \
  SYSTEM:'ts "[%Y-%m-%d %H:%M:%S]" | tee -a ipc/amiga_markers.log'
```

**Step B: On amiga, create reverse tunnel**:
```bash
ssh -N -R 9009:ipc/amiga.sock <homer_ip_address>
```

**Step C: On amiga, send markers**:
```bash
echo "MARKER_NAME $(date +%s.%N)" | socat - TCP:127.0.0.1:9009
```

## Migration from Pi4/NFS
With the Pi4 offline and NFS unavailable, the socket-based communication system replaces the previous NFS-based file sharing approach:
- All file operations now happen locally on each system
- Socket communication enables coordinated operations without shared storage
- Capture files are stored locally and accessed via coordinated operations
- Marker logs are collected via the socket system
- No more dependency on network-attached storage

## Benefits of This Approach
- Reduced dependency on network storage
- Faster local file operations
- More reliable communication between systems
- Better isolation of operations
- No NFS mount issues or network storage failures

## Helper Scripts

### 1. homer: start_receiver.sh
```bash
#!/usr/bin/env bash
set -euo pipefail
mkdir -p ipc
rm -f ipc/amiga.sock
exec socat -d -d UNIX-LISTEN:ipc/amiga.sock,fork \
  SYSTEM:'ts "[%Y-%m-%d %H:%M:%S]" | tee -a ipc/amiga_markers.log'
```

### 2. amiga: start_tunnel.sh
```bash
#!/usr/bin/env bash
set -euo pipefail
exec ssh -N -R 9009:ipc/amiga.sock <homer_ip_address>
```

### 3. amiga: mark.sh
```bash
#!/usr/bin/env bash
set -euo pipefail
msg="${*:-MARK}"
echo "$msg $(date +%s.%N)" | socat - TCP:127.0.0.1:9009
```

### 4. Direct Connection Version (Recommended)
**homer/listen_markers.sh**:
```bash
#!/usr/bin/env bash
set -euo pipefail
mkdir -p ipc
OUT_TAG="${1:-$(date +%Y%m%d_%H%M%S)}"
LOG="ipc/markers_${OUT_TAG}.log"
exec socat TCP-LISTEN:9009,reuseaddr,fork \
  EXEC:"tee -a '$LOG'"
```

**amiga/send_marker.sh**:
```bash
#!/usr/bin/env bash
set -euo pipefail
MSG="${*:-MARKER}"
echo "$MSG $(date +%s.%N)" | nc <homer_ip_address> 9009
```

## Coordinated Capture Workflow

### 1. Basic Orchestration Script
```bash
#!/usr/bin/env bash
# orchestrate_run.sh
set -euo pipefail

OUT_TAG="${1:-run_$(date +%Y%m%d_%H%M%S)}"
CAP="captures/${OUT_TAG}.srzip"
LOG="ipc/${OUT_TAG}_markers.log"

mkdir -p captures ipc

# 1) Start marker listener (background)
socat TCP-LISTEN:9009,reuseaddr,fork \
  EXEC:'tee -a ipc/'${OUT_TAG}'_markers.log' &
LISTEN_PID=$!

# 2) Start sigrok capture (background)
sudo sigrok-cli -d fx2lafw:conn=1.27 -c samplerate=24MHz --time 3000 \
  --channels D0,D1,D2,D3,D4,D5,D6,D7 \
  -O srzip -o "$CAP" &
CAP_PID=$!

# 3) Send capture armed marker
sleep 2  # Give capture time to start
amiga 'echo "CAPTURE_ARM '"$OUT_TAG"' $(date +%s.%N)" | nc <homer_ip_address> 9009' || true

# 4) Run emulator or register tool
amiga 'echo "EMU_START '"$OUT_TAG"' $(date +%s.%N)" | nc <homer_ip_address> 9009; timeout 60s sudo ./emulator; echo "EMU_STOP '"$OUT_TAG"' $(date +%s.%N)" | nc <homer_ip_address> 9009' || true

# Wait for capture to complete
sleep 5
kill "$CAP_PID" 2>/dev/null || true
kill "$LISTEN_PID" 2>/dev/null || true

echo "Capture: $CAP"
echo "Markers: ipc/${OUT_TAG}_markers.log"
```

### 2. Floppy Disk Reading Orchestration
```bash
#!/usr/bin/env bash
# orchestrate_floppy_test.sh
set -euo pipefail

OUT_TAG="${1:-floppy_test_$(date +%Y%m%d_%H%M%S)}"
CAP="captures/${OUT_TAG}.srzip"
LOG="ipc/${OUT_TAG}_markers.log"

mkdir -p captures ipc

# 1) Marker listener (background)
socat TCP-LISTEN:9009,reuseaddr,fork EXEC:'tee -a ipc/'${OUT_TAG}'_markers.log' &
LISTEN_PID=$!

# 2) Start sigrok capture (background)
sudo sigrok-cli -d fx2lafw:conn=1.27 -c samplerate=24MHz --time 3000 \
  --channels D0,D1,D2,D3,D4,D5,D6,D7 \
  -O srzip -o "$CAP" &
CAP_PID=$!

# 3) Marker: capture armed
sleep 2
amiga 'echo "CAPTURE_ARM '"$OUT_TAG"' $(date +%s.%N)" | nc <homer_ip_address> 9009' || true

# 4) Stop emulator if running
amiga 'sudo pkill -f emulator 2>/dev/null || true; sleep 2'

# 5) Run dumpdisk test with markers
amiga 'echo "START_DUMPDISK '"$OUT_TAG"' $(date +%s.%N)" | nc <homer_ip_address> 9009; timeout 30s sudo ./build/dumpdisk --out /tmp/dump_test.raw --drive 0 --tracks 1 --sides 1; echo "END_DUMPDISK '"$OUT_TAG"' $(date +%s.%N)" | nc <homer_ip_address> 9009' || true

# Wait for capture to complete
sleep 5
kill "$CAP_PID" 2>/dev/null || true
kill "$LISTEN_PID" 2>/dev/null || true

echo "Capture: $CAP"
echo "Markers: ipc/${OUT_TAG}_markers.log"
```

## Usage Examples

### 1. Basic Marker Communication
```bash
# On homer (start listener)
socat TCP-LISTEN:9009,reuseaddr,fork EXEC:'tee -a /tmp/markers.log'

# On amiga (send markers)
echo "MOTOR_ON $(date +%s.%N)" | nc <homer_ip> 9009
echo "SEEK_T0 $(date +%s.%N)" | nc <homer_ip> 9009
echo "DMA_START $(date +%s.%N)" | nc <homer_ip> 9009
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

## Troubleshooting

### 1. Connection Issues
- Check if port 9009 is available on both systems
- Verify network connectivity: `ping <target_ip>`
- Check if socat/nc is installed: `which socat` and `which nc`

### 2. Permission Issues
- Ensure sudo access for sigrok-cli and emulator
- Check file permissions in ipc/

### 3. Process Management
- Kill lingering processes: `sudo pkill -f socat`
- Check running processes: `ps aux | grep socat`

### 4. Firewall Issues
- Ensure port 9009 is not blocked
- For SSH tunnels, ensure SSH port (usually 22 or 9022) is accessible

## Benefits of This Setup

1. **Synchronized Operations**: Precise timing between host and Pi operations
2. **Coordinated Captures**: Align sigrok captures with specific emulator events
3. **Marker Logging**: Detailed logging of operations for analysis
4. **Remote Control**: Trigger operations on remote systems
5. **Persistent Communication**: Reliable TCP-based communication channel

## Security Considerations

1. **Port Security**: Port 9009 should only be accessible to trusted systems
2. **SSH Keys**: Use SSH key authentication instead of passwords
3. **Network Isolation**: Use isolated network segment if possible
4. **Access Control**: Limit access to marker log files

## Performance Notes

1. **Latency**: Network latency may affect precise timing requirements
2. **Throughput**: Limited by network speed and processing capacity
3. **Buffering**: socat and netcat provide buffering for bursty traffic
4. **Reliability**: TCP provides reliable delivery of markers

## Troubleshooting

### 1. Connection Issues
- Check if port 9009 is available on both systems
- Verify network connectivity: `ping <target_ip>`
- Check if socat/nc is installed: `which socat` and `which nc`

### 2. Permission Issues
- Ensure sudo access for sigrok-cli and emulator
- Check file permissions in ipc/

### 3. Process Management
- Kill lingering processes: `sudo pkill -f socat`
- Check running processes: `ps aux | grep socat`

### 4. Firewall Issues
- Ensure port 9009 is not blocked
- For SSH tunnels, ensure SSH port (usually 22 or 9022) is accessible

## Benefits of This Setup

1. **Synchronized Operations**: Precise timing between host and Pi operations
2. **Coordinated Captures**: Align sigrok captures with specific emulator events
3. **Marker Logging**: Detailed logging of operations for analysis
4. **Remote Control**: Trigger operations on remote systems
5. **Persistent Communication**: Reliable TCP-based communication channel

## Security Considerations

1. **Port Security**: Port 9009 should only be accessible to trusted systems
2. **SSH Keys**: Use SSH key authentication instead of passwords
3. **Network Isolation**: Use isolated network segment if possible
4. **Access Control**: Limit access to marker log files

## Performance Notes

1. **Latency**: Network latency may affect precise timing requirements
2. **Throughput**: Limited by network speed and processing capacity
3. **Buffering**: socat and netcat provide buffering for bursty traffic
4. **Reliability**: TCP provides reliable delivery of markers