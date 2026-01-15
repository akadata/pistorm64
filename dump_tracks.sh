#!/bin/bash
# Script to dump multiple tracks sequentially

echo "Starting multi-track dump..."

# Define number of tracks to attempt
MAX_TRACKS=10
DRIVE=0

# Make sure emulator is running
sudo pkill -f emulator 2>/dev/null || true
sleep 2
sudo timeout 600 /home/smalley/pistorm/emulator > /tmp/emulator_multi.log 2>&1 &
sleep 10

# Loop through tracks
for track in $(seq 0 $MAX_TRACKS); do
    echo "Attempting to dump track $track..."
    
    # Try to dump the track
    output_file="/tmp/disk_tracks/track_$(printf "%02d" $track).raw"
    result=$(sudo timeout 30 /home/smalley/pistorm/build/dumpdisk --out "$output_file" --drive $DRIVE --tracks 1 --sides 1 2>&1)
    
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
    
    # Small delay between dumps
    sleep 2
done

echo "Multi-track dump completed."