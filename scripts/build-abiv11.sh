#!/bin/bash
# Build the test programs for ABIv11 x86_64 (the library itself is compiled
# into each program from src/; see docs/usage.md).
set -euo pipefail
. "$(dirname "$0")/env.sh"
: "${ZUNEKIT_TOOLCHAIN_ABIV11:?set in local.env}" "${ZUNEKIT_SDK_ABIV11:?set in local.env}"
R=$ZUNEKIT_ROOT
B=$R/build/abiv11-x86_64
CC="$ZUNEKIT_TOOLCHAIN_ABIV11/x86_64-aros-gcc"
mkdir -p "$B"
"$CC" --sysroot="$ZUNEKIT_SDK_ABIV11" -O2 -Wall -Wno-pointer-sign -I"$R/src" \
  -o "$B/zk-table-test" "$R/tests/zk_table_test.c" "$R/src/zk_table.c"
"$CC" --sysroot="$ZUNEKIT_SDK_ABIV11" -O2 -Wall -Wno-pointer-sign -I"$R/src" \
  -o "$B/zk-steps-test" "$R/tests/zk_steps_test.c" "$R/src/zk_steps.c"
"$CC" --sysroot="$ZUNEKIT_SDK_ABIV11" -O2 -Wall -Wno-pointer-sign -I"$R/src" \
  -o "$B/zk-drop-test" "$R/tests/zk_drop_test.c" "$R/src/zk_drop.c" "$R/src/zk_icon.c"
for p in zk-table-test zk-steps-test zk-drop-test; do
  undef=$("$ZUNEKIT_TOOLCHAIN_ABIV11/x86_64-aros-nm" -u "$B/$p" | grep -v ' w ' || true)
  if [ -n "$undef" ]; then echo "unresolved symbols in $p:"; echo "$undef"; exit 1; fi
done
ls -l "$B/zk-table-test" "$B/zk-steps-test" "$B/zk-drop-test"
