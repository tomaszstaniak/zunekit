/* SPDX-License-Identifier: MIT */
/* zk_present on systems whose SDK has the compositor's window tags. */
#include "zk_present.h"

#include <graphics/clip.h>
#include <graphics/layers.h>
#include <proto/intuition.h>

/* SA_SourceAlphaSupport is the public question; the call behind it is
 * private to layers.library and may move, so it is not called here. */
int zk_present_can_compose(struct Screen *screen)
{
    IPTR alpha = 0;
    if (!screen) return 0;
    if (!GetAttr(SA_SourceAlphaSupport, (Object *)screen, &alpha)) return 0;
    return alpha != 0;
}

struct Window *zk_present_open(int compose, struct TagItem *tags)
{
    struct TagItem extra[] = {
        { WA_AlphaSurface, TRUE },
        { TAG_MORE, (IPTR)tags },
    };
    return OpenWindowTagList(NULL, compose ? extra : tags);
}

int zk_present_composed(struct Window *window)
{
    return window && window->WLayer && (window->WLayer->Flags & LAYERCOMPOSITED) != 0;
}
