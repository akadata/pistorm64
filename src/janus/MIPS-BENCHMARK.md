# MIPS Benchmark Suite for Janus IPC

This document describes how to use the MIPS benchmark suite with the Janus IPC system.

## Overview

The MIPS benchmark suite enables distributed computing where the Amiga can offload MIPS-intensive tasks to remote systems. The system consists of three components:

1. **Amiga-side client** (`janus-mips`) - Runs on the Amiga and initiates benchmark requests
2. **Pi-side network bridge** (`janusd-pi`) - Runs on the Raspberry Pi and handles network communication
3. **Remote client** (`janus-mips-client`) - Runs on target systems (like Homer) to execute benchmarks

## Architecture

```
Amiga (janus-mips) 
    ↓ (Janus IPC)
Pi (janusd-pi) 
    ↓ (Network)
Remote Host (janus-mips-client)
```

## Components

### 1. Remote Client (Homer/Target System)

The remote client runs on the system where the actual computation occurs (e.g., Homer running ArchLinux).

#### Build and Run:
```bash
cd ~/pistorm64/src/janus/client/mips-benchmark
make C=gcc                 # Build with gcc
# or
make C=clang               # Build with clang

# Run the client
./janus-mips-client --port 8888
```

#### Usage:
```bash
./janus-mips-client --port <PORT_NUMBER>
```

The client listens for connections and executes MIPS benchmark tasks when requested.

### 2. Pi-side Network Bridge

The Pi-side daemon acts as a bridge between the Amiga and remote systems.

#### Build and Run:
```bash
cd ~/pistorm64/src/janus
make C=gcc                 # Build with gcc
# or
make C=clang               # Build with clang

# Run the daemon
sudo ./janusd-pi
```

This daemon handles network communication on behalf of the Amiga.

### 3. Amiga-side Client

The Amiga-side client sends benchmark requests through the Janus IPC system.

#### Build:
```bash
# This is built as part of the main emulator build process
# The Amiga-side tool is compiled using the Amiga cross-compiler
cd ~/pistorm64/src/janus/amiga/mips-benchmark
make AMIGA_PREFIX=/opt/amiga
```

#### Usage on Amiga:
```bash
# First, ensure janusd is running on the Amiga
janusd

# Then run the MIPS benchmark client
janus-mips --ip <REMOTE_HOST_IP> --port <PORT> --iterations <COUNT>
```

Example:
```bash
# Run benchmark against Homer on the local network
janus-mips --ip 172.16.0.2 --port 8888 --iterations 1000000
```

## Network Protocol

The system uses a simple text-based protocol:
- Request: `MIPS_BENCH:<iterations>`
- Response: `MIPS_RESULT:<ops_per_second>:iterations:<count>`

## Performance Testing

The system enables performance testing across different network conditions:
1. WiFi network testing
2. Gigabit Ethernet testing
3. Direct connection testing

This allows measuring the impact of network latency and bandwidth on distributed computing tasks.

## Troubleshooting

### Amiga Build Issues
The Amiga-side tool requires the Amiga cross-compilation environment and Amiga development headers. If building fails:
- Ensure the Amiga cross-compiler is installed at `/opt/amiga`
- Verify Amiga development libraries are available
- The tool is typically built as part of the main emulator build process

### Network Connectivity Issues
- Verify the remote client is running and listening on the correct port
- Check firewall settings on both ends
- Ensure the IP address specified is reachable from the Pi

### Janus IPC Communication Issues
- Ensure `janusd` is running on the Amiga before running client applications
- Verify the Pi-side daemon (`janusd-pi`) is running
- Check shared memory ring buffer registration

## Integration with Main Emulator

The Amiga-side tools are integrated into the main emulator build system. When building the main emulator:

```bash
cd ~/pistorm64
make
```

The Janus IPC components are automatically included.

## Deployment

### On the Amiga:
1. Copy the emulator binary and Janus tools
2. Run `janusd` daemon before running any Janus IPC applications
3. Run `janus-mips` with appropriate parameters

### On the Pi:
1. Copy and run `janusd-pi` daemon
2. Ensure network connectivity to remote systems

### On Remote Systems:
1. Deploy `janus-mips-client`
2. Configure firewall rules to allow connections
3. Run the client with appropriate port settings