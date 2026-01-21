#include "messages.h"

#define LIBRARY_NAME "bsdsocket.library"
#define SERVICE_NAME "bsl"

struct LibRemote;
static ULONG null_func();
static BPTR expunge();
static BPTR close(struct LibRemote *lib __asm("a6"));
static ULONG send_request(struct LibRemote *lib __asm("a6"), UBYTE *write_buf, ULONG write_length);

#define LVO_COUNT 50

static ULONG func_0(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), ULONG d1 __asm("d1"), ULONG d2 __asm("d2"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)d1;
    *(ULONG *)&write_buf[10] = (ULONG)d2;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 0;
    return send_request(lib, write_buf, 14);
}

static ULONG func_1(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), void *a0 __asm("a0"), ULONG d1 __asm("d1"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)a0;
    *(ULONG *)&write_buf[10] = (ULONG)d1;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 1;
    return send_request(lib, write_buf, 14);
}

static ULONG func_2(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), ULONG d1 __asm("d1"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)d1;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 2;
    return send_request(lib, write_buf, 10);
}

static ULONG func_3(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), void *a0 __asm("a0"), void *a1 __asm("a1"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)a0;
    *(ULONG *)&write_buf[10] = (ULONG)a1;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 3;
    return send_request(lib, write_buf, 14);
}

static ULONG func_4(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), void *a0 __asm("a0"), ULONG d1 __asm("d1"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)a0;
    *(ULONG *)&write_buf[10] = (ULONG)d1;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 4;
    return send_request(lib, write_buf, 14);
}

static ULONG func_5(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), void *a0 __asm("a0"), ULONG d1 __asm("d1"), ULONG d2 __asm("d2"), void *a1 __asm("a1"), ULONG d3 __asm("d3"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)a0;
    *(ULONG *)&write_buf[10] = (ULONG)d1;
    *(ULONG *)&write_buf[14] = (ULONG)d2;
    *(ULONG *)&write_buf[18] = (ULONG)a1;
    *(ULONG *)&write_buf[22] = (ULONG)d3;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 5;
    return send_request(lib, write_buf, 26);
}

static ULONG func_6(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), void *a0 __asm("a0"), ULONG d1 __asm("d1"), ULONG d2 __asm("d2"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)a0;
    *(ULONG *)&write_buf[10] = (ULONG)d1;
    *(ULONG *)&write_buf[14] = (ULONG)d2;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 6;
    return send_request(lib, write_buf, 18);
}

static ULONG func_7(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), void *a0 __asm("a0"), ULONG d1 __asm("d1"), ULONG d2 __asm("d2"), void *a1 __asm("a1"), void *a2 __asm("a2"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)a0;
    *(ULONG *)&write_buf[10] = (ULONG)d1;
    *(ULONG *)&write_buf[14] = (ULONG)d2;
    *(ULONG *)&write_buf[18] = (ULONG)a1;
    *(ULONG *)&write_buf[22] = (ULONG)a2;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 7;
    return send_request(lib, write_buf, 26);
}

static ULONG func_8(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), void *a0 __asm("a0"), ULONG d1 __asm("d1"), ULONG d2 __asm("d2"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)a0;
    *(ULONG *)&write_buf[10] = (ULONG)d1;
    *(ULONG *)&write_buf[14] = (ULONG)d2;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 8;
    return send_request(lib, write_buf, 18);
}

static ULONG func_9(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), ULONG d1 __asm("d1"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)d1;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 9;
    return send_request(lib, write_buf, 10);
}

static ULONG func_10(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), ULONG d1 __asm("d1"), ULONG d2 __asm("d2"), void *a0 __asm("a0"), ULONG d3 __asm("d3"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)d1;
    *(ULONG *)&write_buf[10] = (ULONG)d2;
    *(ULONG *)&write_buf[14] = (ULONG)a0;
    *(ULONG *)&write_buf[18] = (ULONG)d3;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 10;
    return send_request(lib, write_buf, 22);
}

static ULONG func_11(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), ULONG d1 __asm("d1"), ULONG d2 __asm("d2"), void *a0 __asm("a0"), void *a1 __asm("a1"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)d1;
    *(ULONG *)&write_buf[10] = (ULONG)d2;
    *(ULONG *)&write_buf[14] = (ULONG)a0;
    *(ULONG *)&write_buf[18] = (ULONG)a1;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 11;
    return send_request(lib, write_buf, 22);
}

static ULONG func_12(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), void *a0 __asm("a0"), void *a1 __asm("a1"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)a0;
    *(ULONG *)&write_buf[10] = (ULONG)a1;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 12;
    return send_request(lib, write_buf, 14);
}

static ULONG func_13(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), void *a0 __asm("a0"), void *a1 __asm("a1"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)a0;
    *(ULONG *)&write_buf[10] = (ULONG)a1;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 13;
    return send_request(lib, write_buf, 14);
}

static ULONG func_14(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), ULONG d1 __asm("d1"), void *a0 __asm("a0"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)d1;
    *(ULONG *)&write_buf[10] = (ULONG)a0;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 14;
    return send_request(lib, write_buf, 14);
}

static ULONG func_15(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 15;
    return send_request(lib, write_buf, 6);
}

static ULONG func_16(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), void *a0 __asm("a0"), void *a1 __asm("a1"), void *a2 __asm("a2"), void *a3 __asm("a3"), ULONG d1 __asm("d1"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)a0;
    *(ULONG *)&write_buf[10] = (ULONG)a1;
    *(ULONG *)&write_buf[14] = (ULONG)a2;
    *(ULONG *)&write_buf[18] = (ULONG)a3;
    *(ULONG *)&write_buf[22] = (ULONG)d1;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 16;
    return send_request(lib, write_buf, 26);
}

static ULONG func_17(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), ULONG d1 __asm("d1"), ULONG d2 __asm("d2"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)d1;
    *(ULONG *)&write_buf[10] = (ULONG)d2;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 17;
    return send_request(lib, write_buf, 14);
}

static ULONG func_18(struct LibRemote *lib __asm("a6"))
{
    UBYTE write_buf[256];
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 18;
    return send_request(lib, write_buf, 2);
}

static ULONG func_19(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), ULONG d1 __asm("d1"), ULONG d2 __asm("d2"), ULONG d3 __asm("d3"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)d1;
    *(ULONG *)&write_buf[10] = (ULONG)d2;
    *(ULONG *)&write_buf[14] = (ULONG)d3;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 19;
    return send_request(lib, write_buf, 18);
}

static ULONG func_20(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), ULONG d1 __asm("d1"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)d1;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 20;
    return send_request(lib, write_buf, 10);
}

static ULONG func_21(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), ULONG d1 __asm("d1"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)d1;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 21;
    return send_request(lib, write_buf, 10);
}

static ULONG func_22(struct LibRemote *lib __asm("a6"))
{
    UBYTE write_buf[256];
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 22;
    return send_request(lib, write_buf, 2);
}

static ULONG func_23(struct LibRemote *lib __asm("a6"), void *a0 __asm("a0"), ULONG d0 __asm("d0"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)a0;
    *(ULONG *)&write_buf[6] = (ULONG)d0;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 23;
    return send_request(lib, write_buf, 10);
}

static ULONG func_24(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 24;
    return send_request(lib, write_buf, 6);
}

static ULONG func_25(struct LibRemote *lib __asm("a6"), void *a0 __asm("a0"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)a0;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 25;
    return send_request(lib, write_buf, 6);
}

static ULONG func_26(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 26;
    return send_request(lib, write_buf, 6);
}

static ULONG func_27(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 27;
    return send_request(lib, write_buf, 6);
}

static ULONG func_28(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), ULONG d1 __asm("d1"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)d1;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 28;
    return send_request(lib, write_buf, 10);
}

static ULONG func_29(struct LibRemote *lib __asm("a6"), void *a0 __asm("a0"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)a0;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 29;
    return send_request(lib, write_buf, 6);
}

static ULONG func_30(struct LibRemote *lib __asm("a6"), void *a0 __asm("a0"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)a0;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 30;
    return send_request(lib, write_buf, 6);
}

static ULONG func_31(struct LibRemote *lib __asm("a6"), void *a0 __asm("a0"), ULONG d0 __asm("d0"), ULONG d1 __asm("d1"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)a0;
    *(ULONG *)&write_buf[6] = (ULONG)d0;
    *(ULONG *)&write_buf[10] = (ULONG)d1;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 31;
    return send_request(lib, write_buf, 14);
}

static ULONG func_32(struct LibRemote *lib __asm("a6"), void *a0 __asm("a0"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)a0;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 32;
    return send_request(lib, write_buf, 6);
}

static ULONG func_33(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), ULONG d1 __asm("d1"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)d1;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 33;
    return send_request(lib, write_buf, 10);
}

static ULONG func_34(struct LibRemote *lib __asm("a6"), void *a0 __asm("a0"), void *a1 __asm("a1"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)a0;
    *(ULONG *)&write_buf[6] = (ULONG)a1;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 34;
    return send_request(lib, write_buf, 10);
}

static ULONG func_35(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), void *a0 __asm("a0"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)a0;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 35;
    return send_request(lib, write_buf, 10);
}

static ULONG func_36(struct LibRemote *lib __asm("a6"), void *a0 __asm("a0"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)a0;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 36;
    return send_request(lib, write_buf, 6);
}

static ULONG func_37(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 37;
    return send_request(lib, write_buf, 6);
}

static ULONG func_38(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), void *a0 __asm("a0"), void *a1 __asm("a1"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)a0;
    *(ULONG *)&write_buf[10] = (ULONG)a1;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 38;
    return send_request(lib, write_buf, 14);
}

static ULONG func_39(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), ULONG d1 __asm("d1"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)d1;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 39;
    return send_request(lib, write_buf, 10);
}

static ULONG func_40(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), void *a0 __asm("a0"), ULONG d1 __asm("d1"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)a0;
    *(ULONG *)&write_buf[10] = (ULONG)d1;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 40;
    return send_request(lib, write_buf, 14);
}

static ULONG func_41(struct LibRemote *lib __asm("a6"), ULONG d0 __asm("d0"), void *a0 __asm("a0"), ULONG d1 __asm("d1"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)d0;
    *(ULONG *)&write_buf[6] = (ULONG)a0;
    *(ULONG *)&write_buf[10] = (ULONG)d1;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 41;
    return send_request(lib, write_buf, 14);
}

static ULONG func_42(struct LibRemote *lib __asm("a6"), void *a0 __asm("a0"), ULONG d0 __asm("d0"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)a0;
    *(ULONG *)&write_buf[6] = (ULONG)d0;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 42;
    return send_request(lib, write_buf, 10);
}

static ULONG func_43(struct LibRemote *lib __asm("a6"))
{
    UBYTE write_buf[256];
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 43;
    return send_request(lib, write_buf, 2);
}

static ULONG func_44(struct LibRemote *lib __asm("a6"), void *a0 __asm("a0"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)a0;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 44;
    return send_request(lib, write_buf, 6);
}

static ULONG func_45(struct LibRemote *lib __asm("a6"), void *a0 __asm("a0"))
{
    UBYTE write_buf[256];
    *(ULONG *)&write_buf[2] = (ULONG)a0;
    write_buf[0] = MSG_OP_REQ;
    write_buf[1] = 45;
    return send_request(lib, write_buf, 6);
}

static ULONG funcs_vector[] =
{
    (ULONG)null_func,
    (ULONG)close,
    (ULONG)expunge,
    (ULONG)null_func,
    (ULONG)func_0,
    (ULONG)func_1,
    (ULONG)func_2,
    (ULONG)func_3,
    (ULONG)func_4,
    (ULONG)func_5,
    (ULONG)func_6,
    (ULONG)func_7,
    (ULONG)func_8,
    (ULONG)func_9,
    (ULONG)func_10,
    (ULONG)func_11,
    (ULONG)func_12,
    (ULONG)func_13,
    (ULONG)func_14,
    (ULONG)func_15,
    (ULONG)func_16,
    (ULONG)func_17,
    (ULONG)func_18,
    (ULONG)func_19,
    (ULONG)func_20,
    (ULONG)func_21,
    (ULONG)func_22,
    (ULONG)func_23,
    (ULONG)func_24,
    (ULONG)func_25,
    (ULONG)func_26,
    (ULONG)func_27,
    (ULONG)func_28,
    (ULONG)func_29,
    (ULONG)func_30,
    (ULONG)func_31,
    (ULONG)func_32,
    (ULONG)func_33,
    (ULONG)func_34,
    (ULONG)func_35,
    (ULONG)func_36,
    (ULONG)func_37,
    (ULONG)func_38,
    (ULONG)func_39,
    (ULONG)func_40,
    (ULONG)func_41,
    (ULONG)func_42,
    (ULONG)func_43,
    (ULONG)func_44,
    (ULONG)func_45,
};

static void fill_lvos(struct LibRemote *lib __asm("a6"))
{
    for (int i = 0; i < LVO_COUNT; i++)
    {
        UBYTE *lvo = (UBYTE *)lib - ((i + 1) * 6);
        *(UWORD *)lvo = 0x4ef9;
        *(ULONG *)&lvo[2] = funcs_vector[i];
    }
}
