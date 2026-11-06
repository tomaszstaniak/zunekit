#!/bin/bash
# Build the test programs of zk_toolbar, zk_imageview, zk_dragout and
# zk_table for one target, with warnings as errors:
#   scripts/build-tests.sh v1|v11|pi [outdir]
# AROS_CC and AROS_SDK, when set, choose the compiler and the SDK; otherwise
# they come from local.env (see local.env.example).
set -euo pipefail
. "$(dirname "$0")/env.sh"
R=$ZUNEKIT_ROOT
T=${1:?v1, v11 or pi}
case $T in
  v1)  CC=${AROS_CC:-${ZUNEKIT_TOOLCHAIN_V1:?set in local.env}/x86_64-aros-gcc}
       SDK=${AROS_SDK:-${ZUNEKIT_SDK_V1:?set in local.env}}; B=${2:-$R/build/v1} ;;
  v11) CC=${AROS_CC:-${ZUNEKIT_TOOLCHAIN_ABIV11:?set in local.env}/x86_64-aros-gcc}
       SDK=${AROS_SDK:-${ZUNEKIT_SDK_ABIV11:?set in local.env}}; B=${2:-$R/build/abiv11-x86_64} ;;
  pi)  CC=${AROS_CC:-${ZUNEKIT_CC_PI:?set in local.env}}
       SDK=${AROS_SDK:-${ZUNEKIT_SDK_PI:?set in local.env}}; B=${2:-$R/build/pi-aarch64}
       # The Pi compiler wrapper takes its SDK from RPI_SDK.
       export RPI_SDK=$SDK ;;
  *) echo "unknown target $T (v1, v11 or pi)" >&2; exit 2 ;;
esac
case $CC in
  *-gcc)   NM=${CC%gcc}nm ;;
  *-clang) NM=${CC%clang}nm ;;
  *)       NM=$(dirname "$CC")/nm ;;
esac
CFLAGS="-std=gnu99 -O2 -Wall -Wextra -Werror -Werror=implicit-function-declaration"
mkdir -p "$B"
build() {
  out=$1; shift
  "$CC" --sysroot="$SDK" $CFLAGS -I"$R/src" -o "$B/$out" "$@"
  undef=$("$NM" -u "$B/$out" | grep -v ' w ' || true)
  if [ -n "$undef" ]; then echo "unresolved symbols in $out:"; echo "$undef"; exit 1; fi
}
DRAG="$R/src/zk_dragout.c $R/src/zk_dragout_core.c"
VIEW="$R/src/zk_imageview.c $R/src/zk_imageview_core.c"
build zk-toolbar-test "$R/tests/zk_toolbar_test.c" "$R/src/zk_toolbar.c"
build zk-imageview-test "$R/tests/zk_imageview_test.c" $VIEW
build zk-dragout-test "$R/tests/zk_dragout_test.c" $DRAG $VIEW
# zk_table_test.c is older than these flags and passes plain strings to
# CONST_STRPTR; it alone is compiled without -Wpointer-sign.
"$CC" --sysroot="$SDK" $CFLAGS -Wno-pointer-sign -I"$R/src" -c -o "$B/zk_table_test.o" \
  "$R/tests/zk_table_test.c"
build zk-table-test "$B/zk_table_test.o" "$R/src/zk_table.c" $DRAG
rm -f "$B/zk_table_test.o"
"$CC" --version | head -1
ls -l "$B/zk-toolbar-test" "$B/zk-imageview-test" "$B/zk-dragout-test" "$B/zk-table-test"
