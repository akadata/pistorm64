// SPDX-License-Identifier: MIT

#include "../janus-ipc.h"

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <intuition/intuition.h>
#include <graphics/rastport.h>
#include <utility/tagitem.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <stddef.h>

#define JANUS_PORT_NAME "PiStormJanus"
#define MIN_WIDTH 64
#define MIN_HEIGHT 48

struct JanusMsg {
  struct Message msg;
  UWORD cmd;
  UWORD len;
  APTR payload;
  UWORD status;
};

struct JanusFractalCmd {
  UWORD cmd;
  UWORD len;
  UWORD width;
  UWORD height;
  UWORD stride;
  UWORD max_iter;
  ULONG dst_ptr;
  ULONG status_ptr;
  LONG cx;
  LONG cy;
  ULONG scale;
};

static struct MsgPort* find_janus_port(void) {
  struct MsgPort* port = NULL;
  Forbid();
  port = FindPort((STRPTR)JANUS_PORT_NAME);
  Permit();
  return port;
}

static int send_fractal(struct MsgPort* port, struct MsgPort* reply, volatile UWORD* status,
                        struct JanusFractalCmd* cmd) {
  struct JanusMsg* jmsg = NULL;

  *status = 0;

  jmsg = (struct JanusMsg*)AllocVec(sizeof(*jmsg), MEMF_PUBLIC | MEMF_CLEAR);
  if (jmsg == NULL) {
    return 0;
  }

  jmsg->msg.mn_ReplyPort = reply;
  jmsg->len = (UWORD)sizeof(*cmd);
  jmsg->payload = (APTR)cmd;
  jmsg->status = 0;

  PutMsg(port, (struct Message*)jmsg);
  WaitPort(reply);
  GetMsg(reply);

  if (jmsg->status == 0) {
    FreeVec(jmsg);
    return 0;
  }

  FreeVec(jmsg);
  return 1;
}

static void wait_for_done(volatile UWORD* status) {
  while (*status == 0) {
    Delay(1);
  }
}

static BOOL allocate_buffer(UBYTE** buffer, volatile UWORD** status, UWORD stride, UWORD height) {
  size_t size = 4 + (size_t)stride * height;
  UBYTE* new_buffer = (UBYTE*)AllocVec(size, MEMF_PUBLIC | MEMF_CLEAR);

  if (new_buffer == NULL)
    return FALSE;

  if (*buffer) {
    FreeVec(*buffer);
  }

  *buffer = new_buffer;
  *status = (volatile UWORD*)new_buffer;
  return TRUE;
}

static BOOL confirm_exit(struct Window* window) {
  struct EasyStruct request = {
      .es_StructSize = sizeof(struct EasyStruct),
      .es_Flags = 0,
      .es_Title = "Janus Fractal",
      .es_TextFormat = "Stop the Pi fractal demo?",
      .es_GadgetFormat = "Stop|Cancel",
  };
  return EasyRequestArgs(window, &request, NULL, NULL);
}

int main(void) {
  struct Library* IntuitionBase = NULL;
  struct Library* GfxBase = NULL;
  struct Screen* screen = NULL;
  struct Window* window = NULL;
  struct MsgPort* port = NULL;
  struct MsgPort* reply = NULL;
  struct IntuiMessage* msg = NULL;
  UBYTE* penmap = NULL;
  UBYTE* buffer = NULL;
  volatile UWORD* status = NULL;
  int rc = 20;

  UWORD width = 640;
  UWORD height = 480;
  UWORD stride = 320;
  UWORD max_iter = 1024;

  LONG cx = (LONG)(-1 << 15);
  LONG cy = 0;
  ULONG scale = 0;

  IntuitionBase = OpenLibrary("intuition.library", 0);
  GfxBase = OpenLibrary("graphics.library", 0);
  if (IntuitionBase == NULL || GfxBase == NULL) {
    PutStr("janus-fractal: missing libraries.\n");
    goto done;
  }

  screen = LockPubScreen(NULL);
  if (screen == NULL) {
    PutStr("janus-fractal: failed to lock public screen.\n");
    goto done;
  }

  if (width > screen->Width) {
    width = screen->Width;
  }
  if (height > screen->Height) {
    height = screen->Height;
  }
  stride = width;

  window = OpenWindowTags(NULL, WA_CustomScreen, screen, WA_Left, 0, WA_Top, 0, WA_Width, width,
                          WA_Height, height, WA_IDCMP,
                          IDCMP_CLOSEWINDOW | IDCMP_MOUSEBUTTONS | IDCMP_NEWSIZE, WA_Flags,
                          WFLG_SIZEGADGET | WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                              WFLG_SMART_REFRESH | WFLG_ACTIVATE,
                          WA_MinWidth, MIN_WIDTH, WA_MinHeight, MIN_HEIGHT, WA_Title,
                          (ULONG) "Janus Fractal", TAG_END);
  UnlockPubScreen(NULL, screen);
  screen = NULL;

  if (window == NULL) {
    PutStr("janus-fractal: failed to open window.\n");
    goto done;
  }

  penmap = (UBYTE*)AllocVec(256, MEMF_PUBLIC | MEMF_CLEAR);
  if (penmap == NULL) {
    PutStr("janus-fractal: failed to allocate pen map.\n");
    goto done;
  }

  UWORD depth = window->RPort->BitMap->Depth;
  UBYTE mask = (depth >= 8) ? 0xFF : (UBYTE)((1 << depth) - 1);
  for (UWORD i = 0; i < 256; i++) {
    penmap[i] = (UBYTE)(i & mask);
  }

  if (!allocate_buffer(&buffer, &status, stride, height)) {
    PutStr("janus-fractal: failed to allocate buffer.\n");
    goto done;
  }

  port = find_janus_port();
  if (port == NULL) {
    PutStr("janus-fractal: PiStormJanus port not found.\n");
    goto done;
  }

  reply = CreateMsgPort();
  if (reply == NULL) {
    PutStr("janus-fractal: failed to create reply port.\n");
    goto done;
  }

  scale = (ULONG)((3 << 16) / width);

  for (;;) {
    struct JanusFractalCmd* cmd =
        (struct JanusFractalCmd*)AllocVec(sizeof(*cmd), MEMF_PUBLIC | MEMF_CLEAR);
    if (cmd == NULL) {
      PutStr("janus-fractal: failed to allocate command.\n");
      break;
    }

    cmd->cmd = JANUS_CMD_FRACTAL;
    cmd->len = JANUS_FRACTAL_PAYLOAD_LEN;
    cmd->width = width;
    cmd->height = height;
    cmd->stride = stride;
    cmd->max_iter = max_iter;
    cmd->dst_ptr = (ULONG)(buffer + 4);
    cmd->status_ptr = (ULONG)status;
    cmd->cx = cx;
    cmd->cy = cy;
    cmd->scale = scale;

    if (!send_fractal(port, reply, status, cmd)) {
      PutStr("janus-fractal: failed to send command.\n");
      FreeVec(cmd);
      break;
    }

    wait_for_done(status);
    WritePixelArray8(window->RPort, 0, 0, width - 1, height - 1, buffer + 4, penmap);

    FreeVec(cmd);

    WaitPort(window->UserPort);
    while ((msg = (struct IntuiMessage*)GetMsg(window->UserPort)) != NULL) {
      BOOL do_close = FALSE;
      if (msg->Class == IDCMP_CLOSEWINDOW) {
        if (confirm_exit(window)) {
          rc = 0;
          do_close = TRUE;
        }
      } else if (msg->Class == IDCMP_NEWSIZE) {
        UWORD new_width = window->Width;
        UWORD new_height = window->Height;
        if (new_width < MIN_WIDTH) {
          new_width = MIN_WIDTH;
        }
        if (new_height < MIN_HEIGHT) {
          new_height = MIN_HEIGHT;
        }
        if (new_width != width || new_height != height) {
          width = new_width;
          height = new_height;
          stride = width;
          if (!allocate_buffer(&buffer, &status, stride, height)) {
            PutStr("janus-fractal: failed to allocate buffer.\n");
            goto done;
          }
        }
      } else if (msg->Class == IDCMP_MOUSEBUTTONS) {
        WORD mx = msg->MouseX;
        WORD my = msg->MouseY;
        LONG dx = (LONG)((mx - (WORD)(width / 2)) * (LONG)scale);
        LONG dy = (LONG)((my - (WORD)(height / 2)) * (LONG)scale);
        cx += dx;
        cy += dy;
        if (msg->Code == SELECTDOWN) {
          if (scale > 1) {
            scale >>= 1;
          }
        } else if (msg->Code == MENUDOWN) {
          scale <<= 1;
        }
      }
      ReplyMsg((struct Message*)msg);
      if (do_close) {
        goto done;
      }
    }
  }

done:
  if (screen) {
    UnlockPubScreen(NULL, screen);
    screen = NULL;
  }
  if (reply) {
    DeleteMsgPort(reply);
  }
  if (buffer) {
    FreeVec(buffer);
  }
  if (penmap) {
    FreeVec(penmap);
  }
  if (window) {
    CloseWindow(window);
  }
  if (GfxBase) {
    CloseLibrary(GfxBase);
  }
  if (IntuitionBase) {
    CloseLibrary(IntuitionBase);
  }
  return rc;
}
