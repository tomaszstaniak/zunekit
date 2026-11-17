/* SPDX-License-Identifier: MIT */
/* stdc.library compatibility for the mainline (ABIv1) widget providers.

   libpthread from SDKs since AROS commit a819b99a registers its thread-exit
   hook with __stdc_set_taskexit() in its init function and clears it in its
   exit function. That function was added in stdc.library 0.52 in a slot that
   was unused before. stdc.library keeps version 0, so the library request
   made for the call (version 0) is met by older systems too, and on
   stdc.library 0.51 and older the call lands in an unused vector: on x86_64
   that ends the provider in a Software Failure while it exits.

   This definition replaces the SDK's weak stub. It forwards to stdc.library
   only when the running library has the function; otherwise there is no hook
   to set, and exit() in a thread behaves as it always did on that system.
   libpthread's own init and exit code still run on every system. */

#if defined(__AROS__) && defined(ZK_HTTPS_STDC_COMPAT)

#include <exec/libraries.h>
#include <libraries/stdc.h>
#include <proto/stdc.h>

#define STDC_TASKEXIT_VERSION 0
#define STDC_TASKEXIT_REVISION 52

/* The SDK stub's strong entry point: loads StdCBase and jumps through the
   function's vector. */
void ____stdc_set_taskexit_StdCBase_wrapper(void (*taskexit)(int returncode));

static int zk_https_stdc_has_taskexit(void)
{
    struct Library *base = (struct Library *)__aros_getbase_StdCBase();

    if (base == NULL)
        return 0;
    if (base->lib_Version != STDC_TASKEXIT_VERSION)
        return base->lib_Version > STDC_TASKEXIT_VERSION;
    return base->lib_Revision >= STDC_TASKEXIT_REVISION;
}

void __stdc_set_taskexit(void (*taskexit)(int returncode))
{
    if (zk_https_stdc_has_taskexit())
        ____stdc_set_taskexit_StdCBase_wrapper(taskexit);
}

#else

/* ISO C forbids an empty translation unit. */
typedef int zk_https_stdc_compat_unused;

#endif
