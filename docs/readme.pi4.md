
## How to load on Pi 4 with tuning

Known-good default:

```sh
sudo insmod pistorm.ko gpclk_src=5 gpclk_div=6
```

If you are debugging a marginal board, only adjust the divider first:

```sh
sudo insmod pistorm.ko gpclk_src=5 gpclk_div=12
```

Do not change `gpclk_src` away from `5` for normal operation.

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
