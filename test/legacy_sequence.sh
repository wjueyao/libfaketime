#!/bin/sh
# The persistent upstream timestamp format is independent of library builds.
# Optionally also consume bytes emitted by the deployed historical recorder.
set -eu
lib=${LIBFAKETIME_TEST_LIBRARY:-$(cd "$(dirname "$0")/../src" && pwd)/libfaketime.so.1}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/read.c" <<'C'
#include <stdio.h>
#include <time.h>
int main(void) {
  struct timespec t;
  for (int i=0;i<3;i++) {
    if (clock_gettime(CLOCK_REALTIME,&t)) return 1;
    printf("%lld.%09ld\n",(long long)t.tv_sec,t.tv_nsec);
  }
  return 0;
}
C
cc -o "$work/read" "$work/read.c"
# Frozen v1 bytes include backwards time and subsecond precision.
python3 - "$work/sequence" <<'PY'
import struct,sys
open(sys.argv[1],'wb').write(b''.join(struct.pack('>qQ',s,n) for s,n in [(946684800,123456789),(946684799,987654321),(946684802,1)]))
PY
printf '946684800.123456789\n946684799.987654321\n946684802.000000001\n' > "$work/expected"
env LD_PRELOAD="$lib" FAKETIME_LOAD_FILE="$work/sequence" FAKETIME_DONT_FAKE_MONOTONIC=1 "$work/read" > "$work/actual"
python3 - "$work/expected" "$work/actual" <<'PY'
import pathlib,sys
assert pathlib.Path(sys.argv[1]).read_bytes()==pathlib.Path(sys.argv[2]).read_bytes()
PY
if test -n "${LEGACY_LIBFAKETIME:-}"; then
 env LD_PRELOAD="$LEGACY_LIBFAKETIME" FAKETIME_SAVE_FILE="$work/legacy" FAKETIME_DONT_FAKE_MONOTONIC=1 "$work/read" > "$work/recorded"
 cp "$work/legacy" "$work/original"
 env LD_PRELOAD="$lib" FAKETIME_LOAD_FILE="$work/legacy" FAKETIME_DONT_FAKE_MONOTONIC=1 "$work/read" > "$work/replayed"
 python3 - "$work" <<'PY'
import pathlib,sys
p=pathlib.Path(sys.argv[1])
assert (p/'recorded').read_bytes()==(p/'replayed').read_bytes()
assert (p/'original').read_bytes()==(p/'legacy').read_bytes()
PY
fi
printf 'legacy timestamp sequences: PASS\n'
