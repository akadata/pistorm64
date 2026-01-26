# Out-of-tree build wrapper
# Usage:
#   make -C /lib/modules/$(uname -r)/build M=$(PWD) modules

obj-m += pistorm.o

pistorm-y := drivers/misc/pistorm/pistorm-platform.o
