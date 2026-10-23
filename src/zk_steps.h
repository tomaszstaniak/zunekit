/* SPDX-License-Identifier: MIT */
/* zk_steps: a slider over a few named steps (compression levels, speeds),
 * for Zune programs. It is a subclass of the standard Numeric class and adds
 * only drawing, size and the mouse:
 *
 *   - the value is the step's index, 0 to count-1 (MUIA_Numeric_Min is 0,
 *     MUIA_Numeric_Max is count-1), so mouse, cursor keys and the wheel all
 *     choose the same values; keys and wheel are Numeric's own;
 *   - a tick under every step; a click or a drag moves the knob to the
 *     nearest step;
 *   - drawn with the pens of the MUI preferences and sized from the font.
 *
 * Notifications are Numeric's: notify on MUIA_Numeric_Value. Change the
 * number of steps with MUIA_Numeric_Max (count-1). The program shows the
 * step's name itself; the slider has no text of its own. */
#ifndef ZK_STEPS_H
#define ZK_STEPS_H

#include <exec/types.h>
#include <intuition/classusr.h>
#include <utility/tagitem.h>

/* The class; 0 on failure. Call once before creating sliders, zk_steps_exit()
 * after the last one is disposed. */
int zk_steps_init(void);
void zk_steps_exit(void);

/* A slider with count steps (at least 2) at step value. extra (may be NULL)
 * are more tags for the object, such as MUIA_CycleChain or MUIA_ShortHelp.
 * NULL on failure. */
Object *zk_steps_new(ULONG count, ULONG value, const struct TagItem *extra);

/* Test aid: fn (or NULL) gets a line for every press on a slider, with the
 * pointer's x, the geometry and the step chosen. */
void zk_steps_set_trace(void (*fn)(const char *line));

#endif
