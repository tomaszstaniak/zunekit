#!/bin/bash
# Build zk-linktext-test for one target: scripts/build-linktext.sh v1|v11 [outdir]
# With TEST=1 the build can fail allocations on request.
set -euo pipefail
. "$(dirname "$0")/env.sh"
R=$ZUNEKIT_ROOT
case ${1:?v1 or v11} in
  v1)  TC=${ZUNEKIT_TOOLCHAIN_V1:-/Volumes/arosv1/toolchain-mainline}; SDK=${ZUNEKIT_SDK_V1:?set in local.env}; B=${2:-$R/build/v1} ;;
  v11) TC=$ZUNEKIT_TOOLCHAIN_ABIV11; SDK=$ZUNEKIT_SDK_ABIV11; B=${2:-$R/build/abiv11-x86_64} ;;
esac
mkdir -p "$B"
DEF=-DLINKTEXT_EMBEDDED
[ -n "${TEST:-}" ] && DEF="$DEF -DLINKTEXT_TEST"
"$TC/x86_64-aros-gcc" --sysroot="$SDK" -I"$SDK/include" -O2 -Wall -Wextra -Wno-unused-parameter -Wno-pointer-sign $DEF -I"$R/src" \
  -o "$B/zk-linktext-test" "$R/tests/zk_linktext_test.c" "$R/src/zk_linktext.c" "$R/src/linktext_class.c" "$R/src/linktext_core.c"
undef=$("$TC/x86_64-aros-nm" -u "$B/zk-linktext-test" | grep -v ' w ' || true)
if [ -n "$undef" ]; then echo "unresolved symbols:"; echo "$undef"; exit 1; fi
ls -l "$B/zk-linktext-test"
