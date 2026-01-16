# Realtime priority (optional)

The emulator sets SCHED_RR priorities for CPU/IO/input threads. Without CAP_SYS_NICE (i.e. when not running under sudo), the kernel will deny those requests and print warnings:

```
[PRIO] RT priority for cpu denied (CAP_SYS_NICE/rtprio limit needed)
```

This is expected unless you grant realtime to your user. To enable RT for the `pistorm` group (example):

1) Ensure the group exists and you are a member (see docs/udev.md).
2) Add a limits.d file:

```sh
sudo tee /etc/security/limits.d/pistorm-rt.conf >/dev/null <<'EOF'
@pistorm - rtprio 95
@pistorm - memlock unlimited
EOF
```

3) Log out/in (or `newgrp pistorm`) so pam_limits applies.

With this in place, the RT priority requests will succeed when running as a user in `pistorm`. If you don’t need RT, you can ignore the warnings; the emulator will still run.
