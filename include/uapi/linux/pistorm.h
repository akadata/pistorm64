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
    __u32 status;  /* output status; see PISTORM_BUSOP_ST_* */
};

struct pistorm_pins {
    __u32 gplev0;
    __u32 gplev1;
};

/* busop flags */
#define PISTORM_BUSOP_F_STATUS 0x0001 /* operate on PiStorm status register */
#define PISTORM_BUSOP_ST_BERR  0x0001 /* bus error observed */

/* Newer per-op struct with FC + status */
struct pistorm_busop_v2 {
    __u8   op;      /* 0 = read, 1 = write */
    __u8   width;   /* 1, 2, 4 bytes */
    __u8   fc;      /* 0-7 function code */
    __u8   flags;   /* reserved */

    __u32  addr;    /* 32-bit bus address */
    __u32  value;   /* IN: write data, OUT: read data */

    __s32  status;  /* OUT: 0 OK, <0 errno, >0 bus status (PISTORM_BUSOP_ST_*) */
};

struct pistorm_run_batch {
    __u32 count;    /* number of ops in ops[] */
    __u32 flags;    /* batching hints */
    __u64 ops_ptr;  /* userspace pointer to pistorm_busop_v2[] */
};

/* run_batch hints (optional) */
#define PISTORM_RUN_BATCH_F_HINT_64    0x00000001
#define PISTORM_RUN_BATCH_F_HINT_128   0x00000002
#define PISTORM_RUN_BATCH_F_HINT_256   0x00000004
#define PISTORM_RUN_BATCH_F_HINT_512   0x00000008
#define PISTORM_RUN_BATCH_F_HINT_1024  0x00000010

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

/* Asynchronous queue interface */
#define PISTORM_IOC_QUEUE_ENQUEUE  _IOW(PISTORM_IOC_MAGIC, 0x12, struct pistorm_busop)
#define PISTORM_IOC_QUEUE_FLUSH    _IO(PISTORM_IOC_MAGIC, 0x13)

struct pistorm_queue_stats {
    __u64 enqueued;
    __u64 drained;
    __u32 max_depth;
    __u32 current_depth;
    __u32 reserved;
};
#define PISTORM_IOC_QUEUE_STATS    _IOR(PISTORM_IOC_MAGIC, 0x14, struct pistorm_queue_stats)

/* New v2 batch interface */
#define PISTORM_IOC_RUN_BATCH      _IOWR(PISTORM_IOC_MAGIC, 0x15, struct pistorm_run_batch)
