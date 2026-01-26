/*
 * amigabw.c - sequential file read/write benchmark (AmigaOS)
 *
 * READ:  amigabw READ  <path> <bufsize>
 * WRITE: amigabw WRITE <path> <bufsize> <mbytes>
 *
 * Timing:
 *  - Uses timer.device TR_GETSYSTIME (always available) => microsecond resolution
 *  - No TimerBase struct peeking required.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/timer.h>

#include <dos/dos.h>
#include <dos/rdargs.h>

#include <proto/exec.h>
#include <proto/dos.h>

#include <string.h>
#include <stdio.h>

#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

static struct MsgPort *TimerPort;
static struct timerequest *TimerIO;

static int timer_init(void) {
    TimerPort = CreateMsgPort();
    if (!TimerPort) return 0;

    TimerIO = (struct timerequest *)CreateIORequest(TimerPort, sizeof(*TimerIO));
    if (!TimerIO) return 0;

    /* TIMERNAME is "timer.device" in NDK */
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)TimerIO, 0) != 0)
        return 0;

    return 1;
}

static void timer_done(void) {
    if (TimerIO) {
        CloseDevice((struct IORequest *)TimerIO);
        DeleteIORequest((struct IORequest *)TimerIO);
        TimerIO = NULL;
    }
    if (TimerPort) {
        DeleteMsgPort(TimerPort);
        TimerPort = NULL;
    }
}

/* return time in microseconds since some epoch (monotonic enough for benchmarking) */
static ULONG now_us(void) {
    struct timeval tv;

    TimerIO->tr_node.io_Command = TR_GETSYSTIME;
    DoIO((struct IORequest *)TimerIO);

    tv = TimerIO->tr_time; /* timeval: seconds + micros */
    return (ULONG)(tv.tv_secs * 1000000UL + tv.tv_micro);
}

static void print_rate(const char *label, ULONG bytes, ULONG dt_us) {
    ULONG kb = bytes / 1024UL;
    ULONG kbps = (dt_us != 0) ? (ULONG)((kb * 1000000UL) / dt_us) : 0;

    ULONG secs = dt_us / 1000000UL;
    ULONG ms   = (dt_us % 1000000UL) / 1000UL;

    printf("%s: %lu bytes in %lu.%03lu s => %lu KB/s\n",
           label,
           (unsigned long)bytes,
           (unsigned long)secs,
           (unsigned long)ms,
           (unsigned long)kbps);
}

static LONG do_read(const char *path, ULONG bufsize, ULONG memflags) {
    printf("Opening: %s\n", path);
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    printf("Open returned: %ld (IoErr=%ld)\n", (long)fh, (long)IoErr());

    if (!fh) {
        printf("Open failed: %s (IoErr=%ld)\n", path, (long)IoErr());
        return RETURN_FAIL;
    }

    UBYTE *buf = (UBYTE *)AllocMem(bufsize, memflags);
    if (!buf) {
        printf("AllocMem failed (bufsize=%lu, flags=$%lx)\n",
               (unsigned long)bufsize, (unsigned long)memflags);
        Close(fh);
        return RETURN_FAIL;
    }

    ULONG t0 = now_us();
    ULONG total = 0;
    ULONG reads = 0;

    for (;;) {
        LONG n = Read(fh, buf, bufsize);

        if (reads == 0)
            printf("First Read returned: %ld (IoErr=%ld)\n", (long)n, (long)IoErr());

        if (n < 0) {
            printf("Read error (IoErr=%ld)\n", (long)IoErr());
            break;
        }
        if (n == 0) break;

        total += (ULONG)n;
        reads++;

        if ((reads & 0x3F) == 0)  /* every 64 reads */
            printf("Progress: %lu KB\n", (unsigned long)(total / 1024UL));

	if ((total & ((1024UL*1024UL)-1)) == 0)  /* every 1 MiB */
            printf("Progress: %lu KB\n", (unsigned long)(total / 1024UL));

    }

    ULONG t1 = now_us();

    Close(fh);
    FreeMem(buf, bufsize);

    print_rate("READ", total, (t1 - t0));
    return RETURN_OK;
}

static LONG do_write(const char *path, ULONG bufsize, ULONG mbytes, ULONG memflags) {
    BPTR fh = Open((STRPTR)path, MODE_NEWFILE);
    if (!fh) {
        printf("Open failed: %s (IoErr=%ld)\n", path, (long)IoErr());
        return RETURN_FAIL;
    }

    UBYTE *buf = (UBYTE *)AllocMem(bufsize, memflags);
    if (!buf) {
        printf("AllocMem failed (bufsize=%lu, flags=$%lx)\n",
               (unsigned long)bufsize, (unsigned long)memflags);
        Close(fh);
        return RETURN_FAIL;
    }

    for (ULONG i = 0; i < bufsize; i++) buf[i] = (UBYTE)(i & 0xFF);

    ULONG target = mbytes * 1024UL * 1024UL;
    ULONG written = 0;

    ULONG t0 = now_us();

    while (written < target) {
        ULONG chunk = MIN(bufsize, (target - written));
        LONG n = Write(fh, buf, chunk);
        if (n < 0) {
            printf("Write error (IoErr=%ld)\n", (long)IoErr());
            break;
        }
        written += (ULONG)n;
        if ((ULONG)n != chunk) break;
    }

    Flush(fh);

    ULONG t1 = now_us();

    Close(fh);
    FreeMem(buf, bufsize);

    print_rate("WRITE", written, (t1 - t0));
    return RETURN_OK;
}

int main(void) {
    struct RDArgs *rda;
    LONG rc = RETURN_FAIL;

    STRPTR mode = NULL;
    STRPTR path = NULL;
    LONG bufsize = 0;
    LONG mbytes = 0;

    LONG args[4] = {0,0,0,0};
    rda = ReadArgs((STRPTR)"MODE/A,PATH/A,BUFSIZE/N/A,MBYTES/N", args, NULL);
    if (!rda) {
        printf("Args error (IoErr=%ld)\n", (long)IoErr());
        return RETURN_FAIL;
    }

    mode = (STRPTR)args[0];
    path = (STRPTR)args[1];
    bufsize = *(LONG *)args[2];
    if (args[3]) mbytes = *(LONG *)args[3];

    if (!timer_init()) {
        printf("timer.device init failed\n");
        FreeArgs(rda);
        return RETURN_FAIL;
    }

    printf("Buffer=%ld bytes\n", (long)bufsize);

    printf("== MEMF_PUBLIC ==\n");
    if (strcasecmp((const char *)mode, "READ") == 0) {
        rc = do_read((const char *)path, (ULONG)bufsize, MEMF_PUBLIC);
    } else {
        rc = do_write((const char *)path, (ULONG)bufsize, (ULONG)mbytes, MEMF_PUBLIC);
    }

    printf("== MEMF_CHIP ==\n");
    if (strcasecmp((const char *)mode, "READ") == 0) {
        (void)do_read((const char *)path, (ULONG)bufsize, MEMF_CHIP);
    } else {
        (void)do_write((const char *)path, (ULONG)bufsize, (ULONG)mbytes, MEMF_CHIP);
    }

    timer_done();
    FreeArgs(rda);
    return rc;
}

