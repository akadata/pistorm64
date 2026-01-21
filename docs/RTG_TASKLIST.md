# RTG Setup Task List for PiStorm64

## Current Status
The PiStorm64 emulator is running with RTG enabled and properly configured, but RTG functionality is not yet operational on the Amiga side.

## Task List

### Priority 1: Immediate Actions
- [ ] Confirm vc4-kms-v3d overlay is DISABLED in `/boot/firmware/config.txt` (required for PiGFX RTG)
- [ ] Verify framebuffer settings are correct in config.txt
- [ ] Check that Picasso96 is installed on the Amiga system
- [ ] Reboot Pi if config.txt was modified

### Priority 2: PiGFX Installation
- [ ] Run the PiGFX installer from `PDH1:data/a314-shared/rtg/PiGFX Install/`
- [ ] Point installer to original Picasso96 installation folder
- [ ] Verify card files are installed to `DH99:ENVARC/Picasso96/PiGFX/`
- [ ] Ensure monitor file is installed to `DH99:MONITORFILES/PiGFX_MONITOR`

### Priority 3: Amiga Configuration
- [ ] Add assignment to `S:Startup-Sequence`: `assign Pigfx: DH99:ENVARC/Picasso96/PiGFX`
- [ ] Run Picasso96 Setup and select PiGFX as graphics card
- [ ] Configure appropriate screen modes for your display

### Priority 4: Testing and Verification
- [ ] Open Picasso96 Setup and verify PiGFX card is detected
- [ ] Check available screen modes in Preferences > Screen Mode
- [ ] Test RTG functionality with a graphics application
- [ ] Verify RTG screen opens properly with correct resolution

### Priority 5: Performance Tuning
- [ ] Test various screen modes to find optimal performance
- [ ] Verify 1280x720 resolution works properly
- [ ] Check that DPMS power management is functioning

## Dependencies
- Task 1 must be completed before Task 2
- Picasso96 must be installed before running the PiGFX installer
- DRM devices must be available before RTG can function

## Expected Outcomes
Once all tasks are completed:
- RTG will be fully functional on the Amiga side
- Picasso96 will recognize the PiGFX card
- New screen modes will be available
- Graphics applications will be able to utilize RTG acceleration

## Success Criteria
- [ ] Picasso96 detects PiGFX card
- [ ] RTG screen opens successfully
- [ ] Graphics performance is improved over chipset graphics
- [ ] Screen modes are available in Preferences

## Troubleshooting Notes
- If RTG doesn't appear in Picasso96, check card file locations
- If screen modes are unavailable, verify monitor file installation
- If display doesn't work, confirm DRM overlay is active