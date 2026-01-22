// SPDX-License-Identifier: MIT

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <exec/execbase.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/expansion.h>
#include <clib/expansion_protos.h>

#include "../../platforms/amiga/pistorm-dev/pistorm-dev-enums.h"

#define JANUS_RING_SIZE 4096
static char janus_port_name[] = "PiStormJanus";

#define WRITESHORT(base, cmd, val) *(volatile UWORD*)((base) + (cmd)) = (val)
#define WRITELONG(base, cmd, val) *(volatile ULONG*)((base) + (cmd)) = (val)
#define READSHORT(base, cmd, var) (var) = *(volatile UWORD*)((base) + (cmd))

struct janus_ring {
  volatile UWORD write_idx;
  volatile UWORD read_idx;
  UWORD size;
  UWORD flags;
  UBYTE data[1];
};

struct JanusMsg {
  struct Message msg;
  UWORD cmd;
  UWORD len;
  APTR payload;
  UWORD status;
};

static ULONG find_pistorm_base(void) {
  ULONG base = 0xFFFFFFFF;
  struct ExpansionBase* expansion =
      (struct ExpansionBase*)OpenLibrary((STRPTR) "expansion.library", 0L);

  if (expansion != NULL) {
    struct ConfigDev* cd = NULL;
    cd = (struct ConfigDev*)FindConfigDev(cd, 2011, 0x6B);
    if (cd != NULL) {
      base = (ULONG)cd->cd_BoardAddr;
    }
    CloseLibrary((struct Library*)expansion);
  }
  return base;
}

static void janus_kick_pi(ULONG base, ULONG seq) {
  if (base == 0xFFFFFFFF) {
    return;
  }
  WRITELONG(base, PI_DBG_VAL1, seq);
}

static UWORD janus_register_ring(ULONG base, struct janus_ring* ring, UWORD size, UWORD flags) {
  UWORD result = 0;

  if (base == 0xFFFFFFFF) {
    return 0;
  }

  WRITELONG(base, PI_PTR1, (ULONG)ring);
  WRITESHORT(base, PI_WORD1, size);
  WRITESHORT(base, PI_WORD2, flags);
  WRITESHORT(base, PI_CMD_JANUS_INIT, 1);

  READSHORT(base, PI_CMDRESULT, result);
  return result;
}

static void ring_init(struct janus_ring* ring, UWORD size) {
  ring->write_idx = 0;
  ring->read_idx = 0;
  ring->size = size;
  ring->flags = 0;
}

static UWORD ring_used(const struct janus_ring* ring) {
  if (ring->write_idx >= ring->read_idx) {
    return (UWORD)(ring->write_idx - ring->read_idx);
  }
  return (UWORD)(ring->size - (ring->read_idx - ring->write_idx));
}

static UWORD ring_free(const struct janus_ring* ring) {
  return (UWORD)(ring->size - ring_used(ring) - 1);
}

static UWORD ring_write(struct janus_ring* ring, const UBYTE* src, UWORD len) {
  UWORD free_space = ring_free(ring);
  UWORD write_idx = ring->write_idx;
  UWORD first;

  if (len == 0 || len > free_space) {
    return 0;
  }

  first = (UWORD)(ring->size - write_idx);
  if (first > len) {
    first = len;
  }

  CopyMem((APTR)src, (APTR)(ring->data + write_idx), first);
  if (len > first) {
    CopyMem((APTR)(src + first), (APTR)ring->data, (UWORD)(len - first));
  }

  ring->write_idx = (UWORD)((write_idx + len) % ring->size);
  return len;
}

int main(void) {
  struct MsgPort* port = NULL;
  struct JanusMsg* jmsg = NULL;
  struct janus_ring* ring = NULL;
  ULONG base = 0xFFFFFFFF;
  ULONG seq = 1;
  UWORD init_res = 0;

  base = find_pistorm_base();

  port = CreateMsgPort();
  if (port == NULL) {
    PutStr((STRPTR)"janusd: failed to create message port.\n");
    return 20;
  }

  port->mp_Node.ln_Name = janus_port_name;
  AddPort(port);

  ring = (struct janus_ring*)AllocVec(sizeof(*ring) + JANUS_RING_SIZE - 1,
                                      MEMF_PUBLIC | MEMF_CLEAR);
  if (ring == NULL) {
    PutStr((STRPTR)"janusd: failed to allocate ring buffer.\n");
    RemPort(port);
    DeleteMsgPort(port);
    return 20;
  }

  ring_init(ring, JANUS_RING_SIZE);

  init_res = janus_register_ring(base, ring, JANUS_RING_SIZE, 0);
  if (init_res != PI_RES_OK) {
    PutStr((STRPTR)"janusd: failed to register ring with Pi side.\n");
  }

  PutStr((STRPTR)"janusd: ready.\n");

  for (;;) {
    WaitPort(port);
    while ((jmsg = (struct JanusMsg*)GetMsg(port)) != NULL) {
      if (jmsg->payload != NULL && jmsg->len > 0) {
        jmsg->status = ring_write(ring, (const UBYTE*)jmsg->payload, jmsg->len);
        if (jmsg->status != 0) {
          janus_kick_pi(base, seq++);
        }
      } else {
        jmsg->status = 0;
      }

      ReplyMsg((struct Message*)jmsg);
    }
  }
}
