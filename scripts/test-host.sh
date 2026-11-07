#!/bin/bash
# Build and run the host tests of the components' pure logic (no AROS
# needed): scripts/test-host.sh. Uses cc with AddressSanitizer and UBSan.
set -euo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
B=$R/build/host
mkdir -p "$B"
CC=${CC:-cc}
F="-std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -I$R/src"
$CC $F -o "$B/zk-core-test" "$R/tests/host/zk_core_test.c" "$R/src/zk_imageview_core.c" \
  "$R/src/zk_dragout_core.c"
$CC $F -o "$B/lt-test" "$R/tests/host/linktext_core_test.c" "$R/src/linktext_core.c"
"$B/zk-core-test"
"$B/lt-test"
# zk_text: FreeType from pkg-config (the system's freetype2.library on AROS)
FT=$(pkg-config --cflags --libs freetype2)
$CC $F -I"$R/third_party/utf8proc" -DUTF8PROC_STATIC -o "$B/zt-test" "$R/tests/host/zk_text_test.c" "$R/src/zk_text.c" \
  "$R/third_party/utf8proc/utf8proc.c" $FT
"$B/zt-test" "$R/tests/fixtures/fonts"
