#!/bin/bash
# Build the test program for ABIv11 x86_64 (the library itself is compiled
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
undef=$("$ZUNEKIT_TOOLCHAIN_ABIV11/x86_64-aros-nm" -u "$B/zk-table-test" | grep -v ' w ' || true)
if [ -n "$undef" ]; then echo "unresolved symbols:"; echo "$undef"; exit 1; fi
ls -l "$B/zk-table-test"
