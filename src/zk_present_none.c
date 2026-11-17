/* SPDX-License-Identifier: MIT */
/* zk_present on systems without window composition in the SDK (ABIv11,
 * mainline before 2026-10-24): no window is composed. */
#include "zk_present.h"

#include <proto/intuition.h>

int zk_present_can_compose(struct Screen *screen) { (void)screen; return 0; }

struct Window *zk_present_open(int compose, struct TagItem *tags)
{
    (void)compose;
    return OpenWindowTagList(NULL, tags);
}

int zk_present_composed(struct Window *window) { (void)window; return 0; }
