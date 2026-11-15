/* SPDX-License-Identifier: MIT */
/* zk-glarea-test [AUTO|CPU|GPU] [TOUR]
 *
 * A window with a zk_glarea showing a moving picture that the program
 * computes in memory and hands to zk_glarea_present(), so the same pixels
 * go through the OpenGL path (texture) or the CPU path (WritePixelArray).
 * Under it: a cycle gadget for the request, and what the area reports
 * (mode and reason, the OpenGL texts, processor cores, frames per second).
 * The same lines are printed to the Shell. TOUR changes the request by
 * itself every 6 seconds (GPU, AUTO, CPU), for unattended runs.
 *
 * Run with a large stack (Stack 8000000): OpenGL needs it. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <libraries/mui.h>
#include <clib/alib_protos.h>
#include <aros/asmcall.h>
#include "zk_glarea.h"

static Object *area, *txt_mode, *txt_gl, *txt_frame, *cy_req;
static UBYTE *buf;
static LONG bw, bh;
static float sintab[1024];
static double t0, last_report;
static ULONG frames;
static int tour, tour_step = -1;

static double now(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

static float tsin(float x)
{
    return sintab[(int)(x * (1024.0f / 6.2831853f)) & 1023];
}

static void compose(LONG w, LONG h, float t)
{
    LONG x, y;

    if (!buf || bw != w || bh != h) {
        free(buf);
        buf = malloc((size_t)w * h * 4);
        bw = w;
        bh = h;
    }
    if (!buf)
        return;
    for (y = 0; y < h; y++) {
        UBYTE *p = buf + (size_t)y * w * 4;

        for (x = 0; x < w; x++) {
            float v = tsin(x * 0.031f + t) + tsin(y * 0.043f - t * 1.3f) + tsin((x + y) * 0.021f + t * 0.7f);
            float a = v * 1.5707963f + 100.0f;

            *p++ = (UBYTE)(127.5f + 127.5f * tsin(a));
            *p++ = (UBYTE)(127.5f + 127.5f * tsin(a + 2.094f));
            *p++ = (UBYTE)(127.5f + 127.5f * tsin(a + 4.188f));
            *p++ = 255;
        }
    }
    /* A white frame and a diagonal: both paths must show them in the same
     * place (top left corner at the area's corner, rows top to bottom). */
    for (x = 0; x < w; x++) {
        memset(buf + (size_t)x * 4, 255, 4);
        memset(buf + ((size_t)(h - 1) * w + x) * 4, 255, 4);
    }
    for (y = 0; y < h; y++) {
        memset(buf + (size_t)y * w * 4, 255, 4);
        memset(buf + ((size_t)y * w + w - 1) * 4, 255, 4);
        if (y < w)
            memset(buf + ((size_t)y * w + y) * 4, 255, 4);
    }
}

AROS_UFH3(static IPTR, draw_func,
          AROS_UFHA(struct Hook *, hook, A0),
          AROS_UFHA(Object *, obj, A2),
          AROS_UFHA(struct zk_glarea_draw_msg *, m, A1))
{
    AROS_USERFUNC_INIT
    (void)hook;
    compose(m->width, m->height, (float)(now() - t0));
    if (buf)
        zk_glarea_present(obj, buf, m->width, m->height, m->width * 4);
    frames++;
    return 0;
    AROS_USERFUNC_EXIT
}

AROS_UFH3(static IPTR, mode_func,
          AROS_UFHA(struct Hook *, hook, A0),
          AROS_UFHA(Object *, obj, A2),
          AROS_UFHA(APTR, unused, A1))
{
    AROS_USERFUNC_INIT
    static char line[200], gl[220];
    static const char *const req[] = { "AUTO", "CPU", "GPU" };
    IPTR r = 0, mode = 0, sw = 0;
    const char *reason = "", *info = "";

    (void)hook; (void)unused;
    get(obj, MUIA_ZkGLArea_Request, &r);
    get(obj, MUIA_ZkGLArea_Mode, &mode);
    get(obj, MUIA_ZkGLArea_Reason, &reason);
    get(obj, MUIA_ZkGLArea_GLInfo, &info);
    get(obj, MUIA_ZkGLArea_Software, &sw);
    if ((LONG)mode < 0)
        return 0;
    snprintf(line, sizeof(line), "Request %s -> %s (%s)", req[r], mode == ZK_GLAREA_USES_GL ? "OpenGL" : "CPU", reason);
    snprintf(gl, sizeof(gl), "%s%s", info[0] ? info : "OpenGL: not opened",
             (LONG)sw == 1 ? "  [software]" : (LONG)sw == 0 ? "  [hardware]" : "");
    set(txt_mode, MUIA_Text_Contents, (IPTR)line);
    set(txt_gl, MUIA_Text_Contents, (IPTR)gl);
    printf("%s\n%s\n", line, gl);
    frames = 0;
    last_report = now();
    return 0;
    AROS_USERFUNC_EXIT
}

int main(int argc, char **argv)
{
    static const char *const requests[] = { "Auto", "CPU", "OpenGL", NULL };
    static struct Hook draw_hook, mode_hook;
    static char sysline[120], frameline[120];
    Object *app, *win;
    LONG request = ZK_GLAREA_AUTO;
    ULONG sigs = 0, cores = zk_glarea_cpu_cores();
    int i;

    for (i = 1; i < argc; i++) {
        if (!stricmp(argv[i], "CPU")) request = ZK_GLAREA_CPU;
        else if (!stricmp(argv[i], "GPU")) request = ZK_GLAREA_GPU;
        else if (!stricmp(argv[i], "TOUR")) tour = 1;
    }
    for (i = 0; i < 1024; i++)
        sintab[i] = sinf(i * (6.2831853f / 1024.0f));
    draw_hook.h_Entry = (HOOKFUNC)draw_func;
    mode_hook.h_Entry = (HOOKFUNC)mode_func;
    snprintf(sysline, sizeof(sysline), "Processor cores: %lu", (unsigned long)cores);
    printf("%s\n", sysline);

    if (!zk_glarea_init()) {
        printf("no class\n");
        return 20;
    }
    {
        struct TagItem tags[] = {
            { MUIA_ZkGLArea_Request, request },
            { MUIA_ZkGLArea_DrawHook, (IPTR)&draw_hook },
            { TAG_DONE, 0 } };

        area = zk_glarea_new(tags);
    }
    app = ApplicationObject,
        MUIA_Application_Title, (IPTR)"zk-glarea-test",
        MUIA_Application_Base, (IPTR)"ZKGLAREATEST",
        SubWindow, (IPTR)(win = WindowObject,
            MUIA_Window_Title, (IPTR)"zk_glarea test",
            MUIA_Window_ID, MAKE_ID('Z', 'G', 'L', 'A'),
            WindowContents, (IPTR)VGroup,
                Child, (IPTR)area,
                Child, (IPTR)HGroup,
                    Child, (IPTR)Label((IPTR)"Renderer"),
                    Child, (IPTR)(cy_req = CycleObject, MUIA_Cycle_Entries, (IPTR)requests,
                                           MUIA_Cycle_Active, request, End),
                End,
                Child, (IPTR)(txt_mode = TextObject, TextFrame, MUIA_Background, MUII_TextBack, End),
                Child, (IPTR)(txt_gl = TextObject, TextFrame, MUIA_Background, MUII_TextBack, End),
                Child, (IPTR)TextObject, TextFrame, MUIA_Background, MUII_TextBack,
                             MUIA_Text_Contents, (IPTR)sysline, End,
                Child, (IPTR)(txt_frame = TextObject, TextFrame, MUIA_Background, MUII_TextBack, End),
            End,
        End),
    End;
    if (!app || !area) {
        printf("no application\n");
        if (app) MUI_DisposeObject(app);
        zk_glarea_exit();
        return 20;
    }
    DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (IPTR)app, 2,
             MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
    DoMethod(cy_req, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (IPTR)area, 3,
             MUIM_Set, MUIA_ZkGLArea_Request, MUIV_TriggerValue);
    DoMethod(area, MUIM_Notify, MUIA_ZkGLArea_Mode, MUIV_EveryTime, (IPTR)area, 2,
             MUIM_CallHook, (IPTR)&mode_hook);

    t0 = last_report = now();
    set(win, MUIA_Window_Open, TRUE);
    while (DoMethod(app, MUIM_Application_NewInput, (IPTR)&sigs) != (IPTR)MUIV_Application_ReturnID_Quit) {
        double t = now();

        if (tour) {
            static const LONG steps[3] = { 2, 0, 1 };      /* cycle entries: OpenGL, Auto, CPU */
            int step = (int)((t - t0) / 6.0) % 3;

            if (step != tour_step) {
                tour_step = step;
                set(cy_req, MUIA_Cycle_Active, steps[step]);
            }
        }
        zk_glarea_redraw(area);
        if (t - last_report >= 1.0) {
            snprintf(frameline, sizeof(frameline), "%ldx%ld  %.1f frames/s", (long)bw, (long)bh,
                     frames / (t - last_report));
            set(txt_frame, MUIA_Text_Contents, (IPTR)frameline);
            printf("%s\n", frameline);
            frames = 0;
            last_report = t;
        }
        /* The picture moves all the time: poll instead of waiting. */
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            break;
        Delay(1);
    }
    set(win, MUIA_Window_Open, FALSE);
    MUI_DisposeObject(app);
    zk_glarea_exit();
    free(buf);
    return 0;
}
