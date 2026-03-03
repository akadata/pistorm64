# Bluetooth Config Mirror

This directory mirrors the live BlueZ config under `/etc/bluetooth` so pairing changes are tracked in git.

## Files mirrored
- `main.conf`
- `input.conf`
- `network.conf`

## Sync workflow
1. Edit files in this repo copy first.
2. Copy into system location.
3. Restart Bluetooth service.
4. Verify behavior with `bluetoothctl`.

## Apply repo copy to system
```bash
sudo cp /home/smalley/pistorm64/etc/bluetooth/main.conf /etc/bluetooth/main.conf
sudo cp /home/smalley/pistorm64/etc/bluetooth/input.conf /etc/bluetooth/input.conf
sudo cp /home/smalley/pistorm64/etc/bluetooth/network.conf /etc/bluetooth/network.conf
sudo systemctl restart bluetooth
```

## Refresh repo copy from system
```bash
cp /etc/bluetooth/main.conf /home/smalley/pistorm64/etc/bluetooth/main.conf
cp /etc/bluetooth/input.conf /home/smalley/pistorm64/etc/bluetooth/input.conf
cp /etc/bluetooth/network.conf /home/smalley/pistorm64/etc/bluetooth/network.conf
```

## Verification checklist
```bash
systemctl status bluetooth --no-pager
bluetoothctl show
bluetoothctl pairable on
bluetoothctl discoverable on
```

## Pairing-mode knobs in `main.conf` (enable only when needed)
- `AlwaysPairable = true`
- `PairableTimeout = 0`
- `DiscoverableTimeout = 0`
- `FastConnectable = true`
- `Privacy = device` or `network`

Recommended approach:
- Use permissive pairing values only during onboarding/setup mode.
- Revert to tighter values after provisioning completes.
