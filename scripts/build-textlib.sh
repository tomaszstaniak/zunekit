#!/bin/bash
# The shared text layer as one static library for a program that compiles
# zunekit classes into itself (Linktext's UTF-8 mode, zk_edit):
#   scripts/build-textlib.sh v1|v11 outdir
# outdir/libzktext.a holds zk_text, zk_text_aros, utf8proc and HarfBuzz
# (C++11 without exceptions, RTTI or thread-safe statics, so it needs no C++
# runtime and no pthread.library; HB_NO_MT: one task at a time). Link it
# with -lfreetype2 -lm. ZK_TC and ZK_SDK, when set, name the toolchain directory and the SDK
# (a program passes its own, so the library matches it); otherwise they
# come from local.env as for the other scripts. The classes themselves
# (src/zk_edit.c, src/zk_edit_core.c, src/linktext_*.c, src/zk_linktext.c)
# are compiled by the program with -I src.
set -euo pipefail
. "$(dirname "$0")/env.sh"
R=$ZUNEKIT_ROOT
case ${1:?v1 or v11} in
  v1)  TC=${ZK_TC:-${ZUNEKIT_TOOLCHAIN_V1:-/Volumes/arosv1/toolchain-mainline}}; SDK=${ZK_SDK:-${ZUNEKIT_SDK_V1:?set in local.env}} ;;
  v11) TC=${ZK_TC:-$ZUNEKIT_TOOLCHAIN_ABIV11}; SDK=${ZK_SDK:-$ZUNEKIT_SDK_ABIV11} ;;
  *) echo "v1 or v11" >&2; exit 2 ;;
esac
B=${2:?outdir}
mkdir -p "$B/zktext-obj"
O=$B/zktext-obj
"$TC/x86_64-aros-g++" -std=c++11 -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics -DHB_NO_MT -DHB_NO_MMAP -DHB_NO_SETLOCALE --sysroot="$SDK" -I"$SDK/include" \
  -c "$R/third_party/harfbuzz/src/harfbuzz.cc" -o "$O/harfbuzz.o"
CF="-std=gnu99 -O2 -Wall -Wextra -Werror=implicit-function-declaration -Wno-unused-parameter -Wno-pointer-sign"
INC="-I$SDK/include -I$SDK/include/freetype -I$R/src -I$R/third_party/utf8proc -I$R/third_party/harfbuzz/src -DUTF8PROC_STATIC"
for c in src/zk_text.c src/zk_text_aros.c third_party/utf8proc/utf8proc.c; do
  "$TC/x86_64-aros-gcc" --sysroot="$SDK" $CF $INC -c "$R/$c" -o "$O/$(basename "$c" .c).o"
done
rm -f "$B/libzktext.a"
"$TC/x86_64-aros-ar" rcs "$B/libzktext.a" "$O/harfbuzz.o" "$O/zk_text.o" "$O/zk_text_aros.o" "$O/utf8proc.o"
echo "$B/libzktext.a ($(git -C "$R" rev-parse --short HEAD)$(git -C "$R" diff --quiet -- src third_party || echo +changes))"
