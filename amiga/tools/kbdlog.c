// SPDX-License-Identifier: MIT
// Simple Amiga-side keyboard logger using keyboard.device (raw key events).
#include <exec/types.h>
#include <exec/exec.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <devices/keyboard.h>
#include <devices/inputevent.h>
#include <dos/dos.h>
#include <stdio.h>
#include <stdlib.h>

#include <clib/exec_protos.h>
#include <clib/dos_protos.h>

static const char *event_class_name(UWORD cls) {
  switch (cls) {
  case IECLASS_RAWKEY:
    return "RAWKEY";
  case IECLASS_RAWMOUSE:
    return "RAWMOUSE";
#ifdef IECLASS_NEWMOUSE
  case IECLASS_NEWMOUSE:
    return "NEWMOUSE";
#endif
  case IECLASS_POINTERPOS:
    return "POINTERPOS";
  case IECLASS_TIMER:
    return "TIMER";
  default:
    return "OTHER";
  }
}

int main(int argc, char **argv) {
  volatile UBYTE *reset_base = NULL;
  if (argc >= 2) {
    reset_base = (volatile UBYTE *)strtoul(argv[1], NULL, 0);
  }
  struct MsgPort *port = CreateMsgPort();
  if (!port) {
    printf("kbdlog: failed to create MsgPort\n");
    return 5;
  }

  struct IOStdReq *io = (struct IOStdReq *)CreateIORequest(port, sizeof(struct IOStdReq));
  if (!io) {
    printf("kbdlog: failed to create IORequest\n");
    DeleteMsgPort(port);
    return 5;
  }

  if (OpenDevice((CONST_STRPTR)"keyboard.device", 0, (struct IORequest *)io, 0) != 0) {
    printf("kbdlog: failed to open keyboard.device\n");
    DeleteIORequest((struct IORequest *)io);
    DeleteMsgPort(port);
    return 5;
  }

  if (reset_base) {
    printf("kbdlog: base=0x%08lX (Help x5 triggers reset write)\n",
           (unsigned long)reset_base);
  }
  printf("kbdlog: waiting for raw key events (Ctrl+C to quit)\n");

  UBYTE help_count = 0;
  for (;;) {
    struct InputEvent events[8];
    ULONG max_len = sizeof(events);

    io->io_Command = KBD_READEVENT;
    io->io_Data = (APTR)events;
    io->io_Length = max_len;

    DoIO((struct IORequest *)io);

    ULONG count = io->io_Actual / sizeof(struct InputEvent);
    for (ULONG i = 0; i < count; i++) {
      struct InputEvent *ev = &events[i];
      UWORD code = ev->ie_Code;
      UBYTE key = (UBYTE)(code & ~IECODE_UP_PREFIX);
      int is_up = (code & IECODE_UP_PREFIX) != 0;
      printf("key: code=0x%02X %s class=%s qual=0x%04X\n",
             key, is_up ? "up" : "down",
             event_class_name(ev->ie_Class), ev->ie_Qualifier);

      if (ev->ie_Class == IECLASS_RAWKEY && !is_up) {
        if (key == 0x5F) {
          help_count++;
          printf("key: HELP press count=%u qual=0x%04X\n", help_count, ev->ie_Qualifier);
          if (help_count >= 5) {
            if (reset_base) {
              reset_base[0x00] = 0x01;
              printf("key: HELP reset request written to base\n");
            } else {
              printf("key: HELP reset request (no base provided)\n");
            }
            help_count = 0;
          }
        } else {
          help_count = 0;
        }
      }
    }

    if (SetSignal(0, 0) & SIGBREAKF_CTRL_C) {
      break;
    }
  }

  CloseDevice((struct IORequest *)io);
  DeleteIORequest((struct IORequest *)io);
  DeleteMsgPort(port);
  return 0;
}
