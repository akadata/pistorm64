// SPDX-License-Identifier: MIT

#include <exec/types.h>
#include <exec/ports.h>
#include <exec/memory.h>

#include "../janus-ipc.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

#define JANUS_PORT_NAME "PiStormJanus"

struct JanusMsg {
  struct Message msg;
  UWORD cmd;
  UWORD len;
  APTR payload;
  UWORD status;
};

int main(void) {
  struct MsgPort* port = NULL;
  struct MsgPort* reply = NULL;
  struct JanusMsg* jmsg = NULL;
  const char* text = "janus-client: hello\n";
  UWORD text_len = (UWORD)(strlen(text) + 1);
  UBYTE* packet = NULL;

  Forbid();
  port = FindPort((STRPTR)JANUS_PORT_NAME);
  Permit();

  if (port == NULL) {
    PutStr((STRPTR)"janus-client: PiStormJanus port not found.\n");
    return 20;
  }

  reply = CreateMsgPort();
  if (reply == NULL) {
    PutStr((STRPTR)"janus-client: failed to create reply port.\n");
    return 20;
  }

  jmsg = (struct JanusMsg*)AllocVec(sizeof(*jmsg), MEMF_PUBLIC | MEMF_CLEAR);
  if (jmsg == NULL) {
    PutStr((STRPTR)"janus-client: failed to allocate message.\n");
    DeleteMsgPort(reply);
    return 20;
  }

  packet = (UBYTE*)AllocVec(4 + text_len, MEMF_PUBLIC | MEMF_CLEAR);
  if (packet == NULL) {
    PutStr((STRPTR)"janus-client: failed to allocate packet.\n");
    FreeVec(jmsg);
    DeleteMsgPort(reply);
    return 20;
  }

  ((UWORD*)packet)[0] = JANUS_CMD_TEXT;
  ((UWORD*)packet)[1] = text_len;
  CopyMem((APTR)text, packet + 4, text_len);

  jmsg->msg.mn_ReplyPort = reply;
  jmsg->cmd = 1;
  jmsg->len = (UWORD)(4 + text_len);
  jmsg->payload = (APTR)packet;
  jmsg->status = 0;

  PutMsg(port, (struct Message*)jmsg);
  WaitPort(reply);
  GetMsg(reply);

  if (jmsg->status == 0) {
    PutStr((STRPTR)"janus-client: message not accepted.\n");
  }

  FreeVec(packet);
  FreeVec(jmsg);
  DeleteMsgPort(reply);
  return 0;
}
