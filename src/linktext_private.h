/* SPDX-License-Identifier: MIT */
#ifndef LINKTEXT_PRIVATE_H
#define LINKTEXT_PRIVATE_H

#include <libraries/mui.h>
#include "linktext_core.h"

/* The scroller's position in pixels from the text's top: set by the
   scroller's notification only (lines are not all of one height). */
#define LT_SCROLL_PX (MUIB_Linktext | 0x7f)

struct Linktext_DATA
{
    lt_content *content;
    lt_line *lines;
    ULONG nlines;
    LONG laid_w;                 /* column the lines are for, -1: to do */
    LONG first;                  /* line at the top                    */
    LONG lineh, base;            /* line height, baseline in a line    */
    LONG avg;                    /* average character width, normal font */
    ULONG max_columns;
    ULONG vis;                   /* whole lines at the last show or draw */
    struct TextFont *fixed;      /* between Setup and Cleanup          */
    Object *fixed_obj;           /* what holds `fixed` open            */
    struct TextFont *large;      /* the big font, NULL: not larger     */
    Object *large_obj;
    struct TextFont *large_own;  /* opened here when Big is not larger  */
    BOOL utf8;                   /* MUIA_Linktext_UTF8                 */
    CONST_STRPTR *fallbacks;     /* MUIA_Linktext_FallbackFonts        */
    struct zt_font *zf[3];       /* UTF-8: normal, fixed, large; Setup..Cleanup */
    LONG largeh, large_base;     /* a line with Large text             */
    UWORD dim_pen;               /* MPEN_* for Span_Dim                */
    ULONG empty8;                /* empty paragraph, eighths of a line  */
    ULONG rgb[4];                /* text, dim, accent, shade: MUIV_Linktext_RGB or 0 */
    LONG pen[4];                 /* obtained for them, -1 none         */
    struct { ULONG rgb; LONG pen; } bg[8];   /* block backgrounds obtained */
    ULONG nbg;
    BOOL setup;                  /* between Setup and Cleanup          */
    LONG dirty_first, dirty_last;

    struct Hook *hook;
    Object *scroller;
    ULONG want_lines;
    BOOL fix_height;

    BOOL shown, active, motion;
    ULONG sel_anchor, sel_caret; /* equal: no selection                */
    long focus;                  /* link the keyboard is on, -1 none   */

    BOOL pressed, dragging;
    long press_link;
    ULONG press_caret;
    LONG press_x, press_y;

    struct MUI_EventHandlerNode ehn, ehn_move;

    Object *menu, *item_open, *item_copy, *item_text;
    lt_content *menu_content;
    long menu_link;
    char *help;
};

#endif
