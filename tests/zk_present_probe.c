/* zk_present probe: one rounded, half see-through picture with a soft
 * shadow over the desktop, shown the way this system allows.
 *
 *   zk-present-probe [SECONDS] [shaped]
 *
 * Prints what the screen offers and what was done, shows the window for
 * SECONDS (default 5) or until a key or click, and removes it. With
 * "shaped" the window is cut to the picture's outline instead of being
 * blended over a picture of the screen. */
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include "zk_glass_core.h"
#include "zk_present.h"

enum { W = 340, H = 240, PAD = 20, RAD = 16 };

/* Distance from pixel (x,y) to the rounded panel inside the picture,
 * 0 inside, in pixels (whole numbers are enough for a probe). */
static int outside(int x, int y)
{
    int dx = x < PAD + RAD ? PAD + RAD - x : x >= W - PAD - RAD ? x - (W - PAD - RAD - 1) : 0;
    int dy = y < PAD + RAD ? PAD + RAD - y : y >= H - PAD - RAD ? y - (H - PAD - RAD - 1) : 0;
    int d2 = dx * dx + dy * dy, d = 0;
    while ((d + 1) * (d + 1) <= d2) d++;
    return d > RAD ? d - RAD : 0;
}

int main(int argc, char **argv)
{
    LONG seconds = 5;
    int shaped = 0, compose, composed, captured, x, y, ok, i;
    struct Screen *scr;
    struct Window *win;
    struct Region *region = NULL;
    unsigned char *pic, *under, *glass;
    int left, top;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "shaped")) shaped = 1;
        else seconds = atol(argv[i]);
    }
    if (!(scr = LockPubScreen(NULL))) { PutStr((CONST_STRPTR)"no public screen\n"); return RETURN_FAIL; }
    left = (scr->Width - W) / 2;
    top = (scr->Height - H) / 2;
    pic = malloc(W * H * 4); under = malloc(W * H * 4); glass = malloc(W * H * 4);
    if (!pic || !under || !glass) { UnlockPubScreen(NULL, scr); return RETURN_FAIL; }

    compose = zk_present_can_compose(scr);
    captured = zk_glass_capture(scr, left, top, W, H, under);
    Printf((CONST_STRPTR)"can_compose=%ld captured=%ld depth=%ld\n", (IPTR)compose, (IPTR)captured,
           (IPTR)GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH));
    if (captured) { memcpy(glass, under, W * H * 4); zk_glass_blur(glass, W, H, 8); }

    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            unsigned char *p = pic + (y * W + x) * 4;
            int d = outside(x, y);
            if (d == 0) {
                const unsigned char *g = glass + (y * W + x) * 4;
                p[0] = 255;
                /* a dark tint over the blurred desktop, or a plain body */
                p[1] = (unsigned char)(captured ? (28 * 180 + g[1] * 75) / 255 : 28);
                p[2] = (unsigned char)(captured ? (30 * 180 + g[2] * 75) / 255 : 30);
                p[3] = (unsigned char)(captured ? (36 * 180 + g[3] * 75) / 255 : 36);
                if ((x / 20 + y / 20) % 2 == 0 && x > 60 && x < 280 && y > 60 && y < 180) { p[1] = 127; p[2] = 180; p[3] = 255; }
            } else if (d < PAD) {
                p[0] = (unsigned char)(100 * (PAD - d) * (PAD - d) / (PAD * PAD));
                p[1] = p[2] = p[3] = 0;
            } else {
                p[0] = p[1] = p[2] = p[3] = 0;
            }
        }

    if (shaped && !compose) region = zk_present_region(pic, W, H);
    {
        struct TagItem tags[] = {
            { WA_PubScreen, (IPTR)scr }, { WA_Left, left }, { WA_Top, top },
            { WA_InnerWidth, W }, { WA_InnerHeight, H },
            { WA_Borderless, TRUE }, { WA_Activate, TRUE }, { WA_RMBTrap, TRUE },
            { WA_IDCMP, IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS },
            { region ? WA_ShapeRegion : TAG_IGNORE, (IPTR)region },
            { TAG_DONE, 0 }
        };
        win = zk_present_open(compose, tags);
    }
    if (!win) { PutStr((CONST_STRPTR)"no window\n"); UnlockPubScreen(NULL, scr); return RETURN_FAIL; }
    composed = zk_present_composed(win);
    ok = zk_present_put(win, pic, W, H, (captured && !region) ? under : NULL);
    Printf((CONST_STRPTR)"composed=%ld way=%s put=%ld\n", (IPTR)composed,
           (IPTR)(composed ? "composed" : region ? "shaped" : captured ? "pictured" : "plain"), (IPTR)ok);

    {
        ULONG mask = 1UL << win->UserPort->mp_SigBit;
        LONG ticks = seconds * 10;
        int done = 0;
        while (!done && ticks-- > 0) {
            struct IntuiMessage *m;
            Delay(5);
            if (SetSignal(0, 0) & mask)
                while ((m = (struct IntuiMessage *)GetMsg(win->UserPort))) {
                    if (m->Class == IDCMP_MOUSEBUTTONS || (m->Class == IDCMP_RAWKEY && !(m->Code & 0x80))) done = 1;
                    ReplyMsg((struct Message *)m);
                }
            if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) done = 1;
        }
    }
    CloseWindow(win);
    if (region) DisposeRegion(region);
    UnlockPubScreen(NULL, scr);
    free(pic); free(under); free(glass);
    PutStr((CONST_STRPTR)"closed\n");
    return RETURN_OK;
}
