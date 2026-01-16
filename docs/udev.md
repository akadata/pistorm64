# PiStorm udev setup

To run the emulator without sudo (kmod backend), add a udev rule that makes `/dev/pistorm` writable by a `pistorm` group.

## Rule

We ship `etc/udev/99-pistorm.rules`:

```
KERNEL=="pistorm", GROUP="pistorm", MODE="0660"
```

## Install

`make install` copies the rule to `/etc/udev/rules.d/99-pistorm.rules` and reloads udev. You can also install manually:

```sh
sudo groupadd -f pistorm
sudo install -D -m 644 udev/99-pistorm.rules /etc/udev/rules.d/99-pistorm.rules
sudo udevadm control --reload
sudo udevadm trigger --subsystem-match=misc --attr-match=dev=10:262
```

Then add users:

```sh
sudo usermod -aG pistorm <username>
```

Re-login or `newgrp pistorm` to pick up the group. Only one emulator instance should control the hardware at a time.
