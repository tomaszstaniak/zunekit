#!/bin/bash
# Build the programs on the shared text layer for one target:
#   scripts/build-linktext.sh v1|v11 [outdir]
# zk-linktext-test, zk-utf8demo (Linktext's UTF-8 mode) and zk-editdemo
# (zk_edit), zk-listdemo (zk_list). They share the text layer (zk_text, zk_text_aros, utf8proc,
# HarfBuzz) as outdir/libzktext.a from build-textlib.sh.
# With TEST=1 the Linktext build can fail allocations on request.
set -euo pipefail
. "$(dirname "$0")/env.sh"
R=$ZUNEKIT_ROOT
case ${1:?v1 or v11} in
  v1)  TC=${ZUNEKIT_TOOLCHAIN_V1:-/Volumes/arosv1/toolchain-mainline}; SDK=${ZUNEKIT_SDK_V1:?set in local.env}; B=${2:-$R/build/v1} ;;
  v11) TC=$ZUNEKIT_TOOLCHAIN_ABIV11; SDK=$ZUNEKIT_SDK_ABIV11; B=${2:-$R/build/abiv11-x86_64} ;;
esac
mkdir -p "$B"
# the shared text layer, as a program that compiles the classes in uses it
ZK_TC=$TC ZK_SDK=$SDK "$R/scripts/build-textlib.sh" "$1" "$B" >/dev/null
DEF=-DLINKTEXT_EMBEDDED
[ -n "${TEST:-}" ] && DEF="$DEF -DLINKTEXT_TEST"
CFLAGS="-std=gnu99 -O2 -Wall -Wextra -Werror=implicit-function-declaration -Wno-unused-parameter -Wno-pointer-sign"
INC="-I$SDK/include -I$R/src"
TEXT="$B/libzktext.a"
LIBS="-L$SDK/lib -lfreetype2 -lm"
LT="$R/src/zk_linktext.c $R/src/linktext_class.c $R/src/linktext_core.c"
build() {
  out=$1; shift
  "$TC/x86_64-aros-gcc" --sysroot="$SDK" $CFLAGS $DEF $INC -o "$B/$out" "$@" $TEXT $LIBS
  undef=$("$TC/x86_64-aros-nm" -u "$B/$out" | grep -v ' w ' || true)
  if [ -n "$undef" ]; then echo "unresolved symbols in $out:"; echo "$undef"; exit 1; fi
}
build zk-linktext-test "$R/tests/zk_linktext_test.c" $LT
build zk-utf8demo "$R/tests/zk_utf8demo.c" $LT
build zk-editdemo "$R/tests/zk_editdemo.c" "$R/src/zk_edit.c" "$R/src/zk_edit_core.c"
build zk-listdemo "$R/tests/zk_listdemo.c" "$R/src/zk_list.c" "$R/src/zk_list_core.c"
"$TC/x86_64-aros-gcc" --version | head -1
ls -l "$B/zk-linktext-test" "$B/zk-utf8demo" "$B/zk-editdemo" "$B/zk-listdemo"
