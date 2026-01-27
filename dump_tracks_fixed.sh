#!/bin/bash
# Script to dump multiple tracks sequentially with emulator restarts

echo "Starting multi-track dump with emulator restarts..."

# Define number of tracks to attempt
MAX_TRACKS=5
DRIVE=0

# Loop through tracks
for track in $(seq 0 $MAX_TRACKS); do
    echo "Attempting to dump track $track..."
    
    # Kill any existing emulator
    sudo pkill -f emulator 2>/dev/null || true
    sleep 3
    
    # Start emulator
    sudo timeout 60 "$HOME/pistorm/emulator" > /tmp/emulator_track_${track}.log 2>&1 &
    sleep 8  # Give emulator time to initialize
    
    # Try to dump the track
    output_file="/tmp/disk_tracks/track_$(printf "%02d" $track)_fixed.raw"
    result=$(sudo timeout 25 "$HOME/pistorm/build/dumpdisk" --out "$output_file" --drive $DRIVE --tracks 1 --sides 1 2>&1)
    
    # Check if dump was successful by looking for "Track 0 side 0 dumped"
    if echo "$result" | grep -q "Track 0 side 0 dumped"; then
        echo "Successfully dumped track $track"
        # Get file size
        size=$(stat -c%s "$output_file" 2>/dev/null || echo "0")
        echo "Track $track size: $size bytes"
    else
        echo "Failed to dump track $track"
        echo "$result"
        # Remove the failed file if it exists
        rm -f "$output_file" 2>/dev/null
    fi
    
    # Kill emulator for next iteration
    sudo pkill -f emulator 2>/dev/null || true
    sleep 2
done

echo "Multi-track dump completed."