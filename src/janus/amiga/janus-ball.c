// SPDX-License-Identifier: MIT

#include <exec/memory.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/rastport.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define MIN_WIDTH 160
#define MIN_HEIGHT 128
#define BALL_RADIUS 28

static int isqrt(int value) {
  if (value <= 0) {
    return 0;
  }
  int res = 0;
  int bit = 1 << 30;
  while (bit > value) {
    bit >>= 2;
  }
  while (bit != 0) {
    if (value >= res + bit) {
      value -= res + bit;
      res = (res >> 1) + bit;
    } else {
      res >>= 1;
    }
    bit >>= 2;
  }
  return res;
}

static void fill_circle(struct RastPort* rp, int cx, int cy, int radius) {
  if (radius <= 0) {
    return;
  }
  for (int dy = -radius; dy <= radius; dy++) {
    int sq = radius * radius - dy * dy;
    if (sq < 0) {
      continue;
    }
    int span = isqrt(sq);
    RectFill(rp, cx - span, cy + dy, cx + span, cy + dy);
  }
}

static BOOL confirm_exit(struct Window* window) {
  struct EasyStruct request = {
      .es_StructSize = sizeof(struct EasyStruct),
      .es_Flags = 0,
      .es_Title = "Janus Ball",
      .es_TextFormat = "Stop the bouncing ball demo?",
      .es_GadgetFormat = "Stop|Cancel",
  };
  return EasyRequestArgs(window, &request, NULL, NULL);
}

static void draw_checkers(struct RastPort* rp, UWORD top, UWORD bottom, UWORD width, UBYTE tile,
                          UBYTE pen1, UBYTE pen2) {
  int row = top / tile;
  for (UWORD y = top; y < bottom; y += tile, row ^= 1) {
    int parity = row & 1;
    for (UWORD x = 0; x < width; x += tile, parity ^= 1) {
      UBYTE pen = parity ? pen1 : pen2;
      SetAPen(rp, pen);
      RectFill(rp, x, y, MIN(x + tile - 1, width - 1), MIN(y + tile - 1, bottom - 1));
    }
  }
}

static void draw_ball(struct RastPort* rp, int x, int y) {
  SetDrMd(rp, JAM1);
  SetAPen(rp, 5);
  fill_circle(rp, x, y, BALL_RADIUS);
  SetAPen(rp, 6);
  fill_circle(rp, x - BALL_RADIUS / 4, y - BALL_RADIUS / 4, BALL_RADIUS / 2);
}

int main(void) {
  struct Library* IntuitionBase = NULL;
  struct Library* GfxBase = NULL;
  struct Screen* screen = NULL;
  struct Window* window = NULL;
  struct IntuiMessage* msg = NULL;
  int width = 320;
  int height = 200;
  int ball_x = width / 2;
  int ball_y = height / 2;
  int vel_x = 4;
  int vel_y = 3;
  BOOL running = TRUE;
  int rc = 20;

  IntuitionBase = OpenLibrary("intuition.library", 0);
  GfxBase = OpenLibrary("graphics.library", 0);
  if (IntuitionBase == NULL || GfxBase == NULL) {
    PutStr("janus-ball: failed to open libraries.\n");
    goto done;
  }

  screen = LockPubScreen(NULL);
  if (screen == NULL) {
    PutStr("janus-ball: could not lock public screen.\n");
    goto done;
  }

  width = MIN(width, screen->Width);
  height = MIN(height, screen->Height);

  window = OpenWindowTags(NULL, WA_CustomScreen, screen, WA_Left, 0, WA_Top, 0, WA_Width, width,
                          WA_Height, height, WA_IDCMP,
                          IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE | IDCMP_MOUSEBUTTONS, WA_Flags,
                          WFLG_SIZEGADGET | WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                              WFLG_SMART_REFRESH | WFLG_ACTIVATE,
                          WA_MinWidth, MIN_WIDTH, WA_MinHeight, MIN_HEIGHT, WA_Title,
                          (ULONG)"Janus Ball", TAG_END);
  UnlockPubScreen(NULL, screen);
  screen = NULL;

  if (window == NULL) {
    PutStr("janus-ball: could not open window.\n");
    goto done;
  }

  struct RastPort* rp = window->RPort;

  while (running) {
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, width - 1, height - 1);

    SetDrMd(rp, JAM1);
    draw_checkers(rp, 0, height / 3, width, 24, 1, 2);
    draw_checkers(rp, height / 3, height, width, 32, 3, 4);

    draw_ball(rp, ball_x, ball_y);

    WaitTOF();

    ball_x += vel_x;
    ball_y += vel_y;
    if (ball_x - BALL_RADIUS < 0) {
      ball_x = BALL_RADIUS;
      vel_x = -vel_x;
    } else if (ball_x + BALL_RADIUS >= width) {
      ball_x = width - BALL_RADIUS;
      vel_x = -vel_x;
    }
    if (ball_y - BALL_RADIUS < height / 6) {
      ball_y = height / 6 + BALL_RADIUS;
      vel_y = -vel_y;
    } else if (ball_y + BALL_RADIUS >= height) {
      ball_y = height - BALL_RADIUS;
      vel_y = -vel_y;
    }

    while ((msg = (struct IntuiMessage*)GetMsg(window->UserPort)) != NULL) {
      if (msg->Class == IDCMP_CLOSEWINDOW) {
        if (confirm_exit(window)) {
          running = FALSE;
          rc = 0;
        }
      } else if (msg->Class == IDCMP_NEWSIZE) {
        width = window->Width;
        height = window->Height;
        ball_x = MIN(MAX(ball_x, BALL_RADIUS), width - BALL_RADIUS);
        ball_y = MIN(MAX(ball_y, BALL_RADIUS + height / 6), height - BALL_RADIUS);
      }
      ReplyMsg((struct Message*)msg);
    }
  }

done:
  if (window) {
    CloseWindow(window);
  }
  if (screen) {
    UnlockPubScreen(NULL, screen);
  }
  if (GfxBase) {
    CloseLibrary(GfxBase);
  }
  if (IntuitionBase) {
    CloseLibrary(IntuitionBase);
  }
  return rc;
}
