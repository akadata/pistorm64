# Janus IPC Network System

The Janus IPC Network System enables the Amiga to connect to remote processors and services over IP networks. This system allows for distributed computing where the Amiga can offload tasks to remote systems such as the MIPS benchmark client running on Homer (ArchLinux).

## Architecture

The system consists of three main components:

1. **Amiga-side client** (`src/janus/amiga/mips-benchmark/janus-mips.c`)
   - Runs on the Amiga and initiates benchmark requests
   - Communicates with the Pi via Janus IPC ring buffer
   - Accepts command-line parameters like `--ip` for target server

2. **Pi-side network bridge** (`src/janus/network-handler.c`)
   - Runs on the Raspberry Pi as part of the Janus daemon
   - Receives requests from the Amiga via shared memory
   - Handles actual network communication with remote servers
   - Sends results back to the Amiga

3. **Remote client** (`src/janus/client/mips-benchmark/janus-mips-client.c`)
   - Runs on Homer (ArchLinux) or other target systems
   - Listens for connections and executes benchmark tasks
   - Returns results to the Pi-side bridge

## Components

### Amiga-Side Application
- Located in `src/janus/amiga/mips-benchmark/`
- Cross-compiled for Amiga using m68k-amigaos-gcc
- Uses Janus IPC to communicate with the Pi
- Command-line usage: `janus-mips --ip 172.16.0.2`

### Pi-Side Network Bridge
- Located in `src/janus/network-handler.c`
- Integrates with the existing `janusd` daemon
- Handles network communication on behalf of the Amiga
- Uses standard Linux networking APIs

### Remote Client (Homer)
- Located in `src/janus/client/mips-benchmark/`
- Runs on ArchLinux or other compatible systems
- Listens on TCP port 8888 by default
- Executes MIPS benchmark tasks when requested

## Usage

### On Homer (ArchLinux):
```bash
cd ~/pistorm64/src/janus/client/mips-benchmark
make
./janus-mips-client --port 8888
```

### On Amiga:
```bash
# After the full system is operational
janus-mips --ip 172.16.0.2 --port 8888 --iterations 1000000
```

## Network Protocol

The system uses a simple text-based protocol:
- Request: `MIPS_BENCH:<iterations>`
- Response: `MIPS_RESULT:<ops_per_second>:iterations:<count>`

## Integration with Existing System

The Janus IPC system integrates seamlessly with the existing PiStorm infrastructure:
- Uses shared memory ring buffers for Amiga-Pi communication
- Maintains low-latency communication via doorbell mechanism
- Preserves existing functionality while adding network capabilities

## Performance Testing

The system enables performance testing across different network conditions:
1. WiFi network testing
2. Gigabit Ethernet testing
3. Direct connection testing

This allows measuring the impact of network latency and bandwidth on distributed computing tasks.