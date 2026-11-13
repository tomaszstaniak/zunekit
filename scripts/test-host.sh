#!/bin/bash
# Build and run the host tests of the components' pure logic (no AROS
# needed): scripts/test-host.sh. Uses cc with AddressSanitizer and UBSan.
# zk_text needs FreeType (pkg-config freetype2) and builds HarfBuzz from
# third_party/harfbuzz once (one C++11 unit, as on AROS).
set -euo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
B=$R/build/host
mkdir -p "$B"
CC=${CC:-cc}
F="-std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -I$R/src"
FT=$(pkg-config --cflags --libs freetype2)
HB_O="$B/harfbuzz.o"
if [ ! -f "$HB_O" ] || [ "$R/third_party/harfbuzz/src/harfbuzz.cc" -nt "$HB_O" ]; then
  ${CXX:-c++} -std=c++11 -O2 -DHB_NO_MT -DHB_NO_MMAP -DHB_NO_SETLOCALE -c "$R/third_party/harfbuzz/src/harfbuzz.cc" -o "$HB_O"
fi
ZT="-I$R/third_party/utf8proc -I$R/third_party/harfbuzz/src -DUTF8PROC_STATIC"
ZT_SRC="$R/src/zk_text.c $R/third_party/utf8proc/utf8proc.c $HB_O"
CXXLIB=${CXXLIB:--lc++}

$CC $F -o "$B/zk-core-test" "$R/tests/host/zk_core_test.c" "$R/src/zk_imageview_core.c" \
  "$R/src/zk_dragout_core.c"
$CC $F -o "$B/zl-test" "$R/tests/host/zk_list_test.c" "$R/src/zk_list_core.c"
$CC $F $ZT -o "$B/lt-test" "$R/tests/host/linktext_core_test.c" "$R/src/linktext_core.c" $ZT_SRC $FT $CXXLIB
$CC $F $ZT -Dmalloc=tm -Drealloc=tr -Dcalloc=tc $(pkg-config --cflags freetype2) -c "$R/src/zk_text.c" -o "$B/zk_text_f.o"
$CC $F $ZT -o "$B/zt-test" "$R/tests/host/zk_text_test.c" "$B/zk_text_f.o" \
  "$R/third_party/utf8proc/utf8proc.c" "$HB_O" $FT $CXXLIB
$CC $F $ZT -Dmalloc=fm -Drealloc=fr -Dcalloc=fc -c "$R/src/zk_edit_core.c" -o "$B/zk_edit_core.o"
$CC $F $ZT -o "$B/ze-test" "$R/tests/host/zk_edit_test.c" "$B/zk_edit_core.o" $ZT_SRC $FT $CXXLIB
"$B/zk-core-test"
"$B/zl-test"
"$B/lt-test"
"$B/zt-test" "$R/tests/fixtures/fonts"
"$B/ze-test" "$R/tests/fixtures/fonts"
