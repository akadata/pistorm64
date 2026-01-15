#!/bin/bash
# Script to properly coordinate emulator and dumpdisk operations

echo "Starting coordinated disk dump..."

# Clean up any existing emulator
sudo pkill -f emulator 2>/dev/null || true
sleep 3

# Start emulator in background
sudo timeout 120 /home/smalley/pistorm/emulator > /tmp/coordinated_emu.log 2>&1 &
EMU_PID=$!

echo "Emulator started with PID: $EMU_PID"

# Wait for emulator to fully initialize
echo "Waiting for emulator to initialize..."
sleep 25

# Check if emulator is still running
if kill -0 $EMU_PID 2>/dev/null; then
    echo "Emulator is running, proceeding with dump..."
    
    # Create directory for track dumps
    mkdir -p /tmp/disk_tracks
    
    # Try to dump track 0
    echo "Dumping track 0..."
    if sudo timeout 30 /home/smalley/pistorm/build/dumpdisk --out /tmp/disk_tracks/track_00.raw --drive 0 --tracks 1 --sides 1; then
        echo "Successfully dumped track 0"
        ls -la /tmp/disk_tracks/track_00.raw
    else
        echo "Failed to dump track 0"
    fi
    
    # Kill emulator when done
    sudo pkill -f emulator 2>/dev/null || true
    echo "Emulator stopped."
else
    echo "Emulator failed to start properly"
fi

echo "Coordinated dump completed."