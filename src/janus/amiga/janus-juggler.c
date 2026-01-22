// SPDX-License-Identifier: MIT

#include <exec/memory.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

static const int sin_table[360] = {
    0,   18,   36,   54,   71,   89,   107,  125,  143,  160,  178,  195,  213,  230,  248,
    265, 282,  299,  316,  333,  350,  367,  384,  400,  416,  433,  449,  465,  481,  496,
    512, 527,  543,  558,  573,  587,  602,  616,  630,  644,  658,  672,  685,  698,  711,
    724, 737,  749,  761,  773,  784,  796,  807,  818,  828,  839,  849,  859,  868,  878,
    887, 896,  904,  912,  920,  928,  935,  943,  949,  956,  962,  968,  974,  979,  984,
    989, 994,  998,  1002, 1005, 1008, 1011, 1014, 1016, 1018, 1020, 1022, 1023, 1023, 1024,
    1024, 1024, 1023, 1023, 1022, 1020, 1018, 1016, 1014, 1011, 1008, 1005, 1002, 998,  994,
    989, 984,  979,  974,  968,  962,  956,  949,  943,  935,  928,  920,  912,  904,  896,
    887, 878,  868,  859,  849,  839,  828,  818,  807,  796,  784,  773,  761,  749,  737,
    724, 711,  698,  685,  672,  658,  644,  630,  616,  602,  587,  573,  558,  543,  527,
    512, 496,  481,  465,  449,  433,  416,  400,  384,  367,  350,  333,  316,  299,  282,
    265, 248,  230,  213,  195,  178,  160,  143,  125,  107,  89,   71,   54,   36,   18,
    0,   -18,  -36,  -54,  -71,  -89,  -107, -125, -143, -160, -178, -195, -213, -230, -248,
    -265, -282, -299, -316, -333, -350, -367, -384, -400, -416, -433, -449, -465, -481, -496,
    -512, -527, -543, -558, -573, -587, -602, -616, -630, -644, -658, -672, -685, -698, -711,
    -724, -737, -749, -761, -773, -784, -796, -807, -818, -828, -839, -849, -859, -868, -878,
    -887, -896, -904, -912, -920, -928, -935, -943, -949, -956, -962, -968, -974, -979, -984,
    -989, -994, -998, -1002, -1005, -1008, -1011, -1014, -1016, -1018, -1020, -1022, -1023,
    -1023, -1024, -1024, -1024, -1023, -1023, -1022, -1020, -1018, -1016, -1014, -1011, -1008,
    -1005, -1002, -998, -994, -989, -984, -979, -974, -968, -962, -956, -949, -943, -935, -928,
    -920, -912, -904, -896, -887, -878, -868, -859, -849, -839, -828, -818, -807, -796, -784,
    -773, -761, -749, -737, -724, -711, -698, -685, -672, -658, -644, -630, -616, -602, -587,
    -573, -558, -543, -527, -512, -496, -481, -465, -449, -433, -416, -400, -384, -367, -350,
    -333, -316, -299, -282, -265, -248, -230, -213, -195, -178, -160, -143, -125, -107, -89,
    -71,  -54,  -36,  -18,
};
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

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN_WIDTH 320
#define MIN_HEIGHT 220

static inline int wrap_index(int value) {
  value %= 360;
  if (value < 0)
    value += 360;
  return value;
}

static inline int sin_lookup(int value) {
  return sin_table[wrap_index(value)];
}

static inline int cos_lookup(int value) {
  return sin_lookup(value + 90);
}
static BOOL confirm_exit(struct Window* window) {
  struct EasyStruct request = {
      .es_StructSize = sizeof(struct EasyStruct),
      .es_Flags = 0,
      .es_Title = "Janus Juggler",
      .es_TextFormat = "Stop the raytraced juggler?",
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

static void draw_juggler(struct RastPort* rp, int center_x, int center_y, int sway_x) {
  SetDrMd(rp, JAM1);
  SetAPen(rp, 4);
  fill_circle(rp, center_x, center_y, 36);
  SetAPen(rp, 5);
  fill_circle(rp, center_x, center_y - 80, 28);

  SetDrMd(rp, JAM2);
  SetAPen(rp, 6);
  Move(rp, center_x, center_y - 10);
  Draw(rp, center_x - 70 + sway_x / 2, center_y - 22);
  Move(rp, center_x, center_y - 10);
  Draw(rp, center_x + 70 + sway_x / 2, center_y - 22);

  Move(rp, center_x - 20, center_y + 70);
  Draw(rp, center_x - 40 + sway_x / 5, center_y + 120);
  Move(rp, center_x + 20, center_y + 70);
  Draw(rp, center_x + 40 + sway_x / 5, center_y + 120);
}

static void draw_ball_shape(struct RastPort* rp, int x, int y) {
  SetDrMd(rp, JAM1);
  SetAPen(rp, 7);
  fill_circle(rp, x, y, 14);
  SetAPen(rp, 6);
  fill_circle(rp, x - 4, y - 4, 6);
}

int main(void) {
  struct Library* IntuitionBase = NULL;
  struct Library* GfxBase = NULL;
  struct Screen* screen = NULL;
  struct Window* window = NULL;
  struct IntuiMessage* msg = NULL;
  int width = 400;
  int height = 260;
  int phase = 0;
  int viewer_dx = 0;
  int viewer_dy = 0;
  BOOL running = TRUE;
  int rc = 20;

  IntuitionBase = OpenLibrary("intuition.library", 0);
  GfxBase = OpenLibrary("graphics.library", 0);
  if (IntuitionBase == NULL || GfxBase == NULL) {
    PutStr("janus-juggler: missing libraries.\n");
    goto done;
  }

  screen = LockPubScreen(NULL);
  if (screen == NULL) {
    PutStr("janus-juggler: failed to lock public screen.\n");
    goto done;
  }

  width = MIN(width, screen->Width);
  height = MIN(height, screen->Height);

  window = OpenWindowTags(NULL, WA_CustomScreen, screen, WA_Left, 0, WA_Top, 0, WA_Width, width,
                          WA_Height, height, WA_IDCMP,
                          IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE | IDCMP_MOUSEMOVE, WA_Flags,
                          WFLG_SIZEGADGET | WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                              WFLG_SMART_REFRESH | WFLG_ACTIVATE,
                          WA_MinWidth, MIN_WIDTH, WA_MinHeight, MIN_HEIGHT, WA_Title,
                          (ULONG)"Janus Juggler", TAG_END);
  UnlockPubScreen(NULL, screen);
  screen = NULL;

  if (window == NULL) {
    PutStr("janus-juggler: could not open window.\n");
    goto done;
  }

  struct RastPort* rp = window->RPort;

  while (running) {
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, width - 1, height - 1);
    SetDrMd(rp, JAM1);
    draw_checkers(rp, 0, height / 2, width, 32, 1, 2);
    draw_checkers(rp, height / 2, height, width, 40, 3, 4);

    int center_x = width / 2 + viewer_dx;
    int center_y = (height * 11) / 20 + viewer_dy;
    draw_juggler(rp, center_x, center_y, viewer_dx);

    int orbit_x = width / 5;
    int orbit_y = height / 4;
    for (int i = 0; i < 3; i++) {
      int ball_phase = wrap_index(phase + i * 60);
      int sinv = sin_lookup(ball_phase);
      int cosv = cos_lookup(ball_phase);
      int dx = (cosv * orbit_x) / 1024;
      int dy = (sinv * orbit_y) / 1024;
      int ball_x = center_x + dx;
      int ball_y = center_y - (dy * 7) / 10 - 40 + viewer_dy / 2 + viewer_dx / 4;
      draw_ball_shape(rp, ball_x, ball_y);
    }

    phase = wrap_index(phase + 4);
    WaitTOF();

    while ((msg = (struct IntuiMessage*)GetMsg(window->UserPort)) != NULL) {
      if (msg->Class == IDCMP_CLOSEWINDOW) {
        if (confirm_exit(window)) {
          running = FALSE;
          rc = 0;
        }
      } else if (msg->Class == IDCMP_NEWSIZE) {
        width = window->Width;
        height = window->Height;
      } else if (msg->Class == IDCMP_MOUSEMOVE) {
        viewer_dx = (msg->MouseX - width / 2) / 6;
        viewer_dy = (msg->MouseY - height / 2) / 8;
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
