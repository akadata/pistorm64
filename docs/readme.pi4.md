
## How to load on Pi 4 with tuning

Default (src=5 div=6):

```sh
sudo insmod pistorm.ko
```

Try a slower GPCLK if the CPLD/bus wiring is marginal (example div=12):

```sh
sudo insmod pistorm.ko gpclk_div=12
```

Try different source if needed (example src=6):

```sh
sudo insmod pistorm.ko gpclk_src=6 gpclk_div=12
```

---

## Note on 32‑bit vs 16‑bit transfers

Your kernel module **never performs a single 32‑bit GPIO transfer**. Even in `PISTORM_W32`, it executes **two 16‑bit bus operations** (`ps_read16`/`ps_write16` twice). That matches the PiStorm bus protocol and the CPLD’s 16‑bit data path.

---

## Next sanity checks on Pi 4

After building and loading:

```sh
dmesg | tail -120
ls -l /dev/pistorm
```

