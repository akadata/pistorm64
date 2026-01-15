#pragma once
#include <linux/ioctl.h>
#include <linux/types.h>

#define PISTORM_IOC_MAGIC 'p'

enum pistorm_width {
    PISTORM_W8  = 1,
    PISTORM_W16 = 2,
    PISTORM_W32 = 4,
};

struct pistorm_busop {
    __u32 addr;
    __u32 value;   /* for write: input; for read: output */
    __u8  width;   /* 1/2/4 */
    __u8  is_read; /* 1=read, 0=write */
    __u16 flags;   /* see PISTORM_BUSOP_F_* */
};

struct pistorm_pins {
    __u32 gplev0;
    __u32 gplev1;
};

/* busop flags */
#define PISTORM_BUSOP_F_STATUS 0x0001 /* access PiStorm status register */

/* Small control ops */
#define PISTORM_IOC_SETUP          _IO(PISTORM_IOC_MAGIC, 0x00)
#define PISTORM_IOC_RESET_SM       _IO(PISTORM_IOC_MAGIC, 0x01)
#define PISTORM_IOC_PULSE_RESET    _IO(PISTORM_IOC_MAGIC, 0x02)
#define PISTORM_IOC_GET_PINS       _IOR(PISTORM_IOC_MAGIC, 0x03, struct pistorm_pins)

/* Single bus op (slow but simplest first step) */
#define PISTORM_IOC_BUSOP          _IOWR(PISTORM_IOC_MAGIC, 0x10, struct pistorm_busop)

/* Optional later: batched ops to cut syscall overhead */
struct pistorm_batch {
    __u64 ops_ptr;    /* userspace pointer to array of pistorm_busop */
    __u32 ops_count;
    __u32 reserved;
};
#define PISTORM_IOC_BATCH          _IOWR(PISTORM_IOC_MAGIC, 0x11, struct pistorm_batch)
