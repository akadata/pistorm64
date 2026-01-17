# PiStorm Kernel Module

## Loading with Pi 4 GPCLK Tuning

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
