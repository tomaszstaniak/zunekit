/* SPDX-License-Identifier: MIT */
#ifndef LINKTEXT_PRIVATE_H
#define LINKTEXT_PRIVATE_H

#include <libraries/mui.h>
#include "linktext_core.h"

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
    UWORD dim_pen;               /* MPEN_* for Span_Dim                */
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
