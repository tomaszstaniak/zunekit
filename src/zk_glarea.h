/* SPDX-License-Identifier: MIT */
/* zk_glarea: an area in a Zune window that the program draws with OpenGL
 * or on the CPU. Which of the two is used is decided when the area is
 * shown, from what the system offers:
 *
 *   - ZK_GLAREA_AUTO (default): OpenGL when a context can be made and its
 *     renderer is not a software rasteriser (softpipe, llvmpipe, swrast,
 *     lavapipe); otherwise the CPU, with the reason kept as text;
 *   - ZK_GLAREA_CPU: the CPU; gl.library is not touched;
 *   - ZK_GLAREA_GPU: OpenGL whatever the renderer is; the CPU, with the
 *     reason, when no context can be made.
 *
 * The program draws in its DrawHook. In OpenGL mode the area's context is
 * current and the viewport is the area; the area swaps buffers after the
 * hook returns. In CPU mode the hook draws into the window's RastPort at
 * (left, top). A program that composes its picture in memory either way
 * (a page, a canvas) calls zk_glarea_present() from the hook: the pixels
 * go to the screen as a texture in OpenGL mode and with WritePixelArray in
 * CPU mode.
 *
 * The OpenGL context is made through GLA on the area's rectangle of the
 * window, when the area is shown, and destroyed when it is hidden (a new
 * size hides and shows it). OpenGL programs need a large stack; 8 MB was
 * used in the tests. Link with -lGL.
 *
 * The area does not animate by itself: call zk_glarea_redraw() when the
 * picture has changed. */
#ifndef ZK_GLAREA_H
#define ZK_GLAREA_H

#include <exec/types.h>
#include <graphics/rastport.h>
#include <intuition/classusr.h>
#include <utility/hooks.h>
#include <utility/tagitem.h>

#ifndef MUIB_AROS
#define MUIB_AROS (MUIB_RSVD | 0x00070000)
#endif
#define MUIB_ZkGLArea (MUIB_AROS | 0x00000F00)

enum zk_glarea_request { ZK_GLAREA_AUTO, ZK_GLAREA_CPU, ZK_GLAREA_GPU };
enum zk_glarea_mode    { ZK_GLAREA_USES_CPU, ZK_GLAREA_USES_GL };

/* [ISG] LONG: enum zk_glarea_request. Setting it on a shown area chooses again. */
#define MUIA_ZkGLArea_Request  (MUIB_ZkGLArea | 0x01)
/* [..GN] LONG: enum zk_glarea_mode, what is used now. Notifies after every
 * choice (the area was shown or the request changed), also when the value
 * stays the same. */
#define MUIA_ZkGLArea_Mode     (MUIB_ZkGLArea | 0x02)
/* [..G] const char *: why this mode ("requested", "hardware OpenGL found",
 * "OpenGL here is a software rasteriser", "no OpenGL context"). */
#define MUIA_ZkGLArea_Reason   (MUIB_ZkGLArea | 0x03)
/* [..G] const char *: "vendor | renderer | version" of the context that was
 * opened for the last choice, "" when none was. */
#define MUIA_ZkGLArea_GLInfo   (MUIB_ZkGLArea | 0x04)
/* [..G] LONG: 1 the renderer is a software rasteriser, 0 it is not,
 * -1 no context was opened. */
#define MUIA_ZkGLArea_Software (MUIB_ZkGLArea | 0x05)
/* [IS.] struct Hook *: the program's drawing, see zk_glarea_draw_msg. */
#define MUIA_ZkGLArea_DrawHook (MUIB_ZkGLArea | 0x06)

/* DrawHook: object = the area, message = this. Return value unused. */
struct zk_glarea_draw_msg
{
    LONG mode;                /* enum zk_glarea_mode */
    LONG width, height;       /* of the area, pixels */
    struct RastPort *rp;      /* CPU mode: the window's RastPort */
    LONG left, top;           /* CPU mode: the area's corner in rp */
};

int zk_glarea_init(void);     /* once; 0 on failure */
void zk_glarea_exit(void);

Object *zk_glarea_new(const struct TagItem *extra);

/* Asks the area to call the DrawHook again. */
void zk_glarea_redraw(Object *area);

/* Only inside the DrawHook: shows a picture of w x h pixels, four bytes
 * R, G, B, A per pixel, rows top to bottom, stride bytes per row, with its
 * corner at the area's corner. In OpenGL mode it is uploaded as a texture
 * and drawn 1:1; in CPU mode it is written with WritePixelArray. Alpha is
 * ignored. */
void zk_glarea_present(Object *area, const UBYTE *rgba, LONG w, LONG h, LONG stride);

/* Processor cores the system reports (processor.resource), at least 1. */
ULONG zk_glarea_cpu_cores(void);

#endif
