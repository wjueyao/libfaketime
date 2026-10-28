#!/bin/sh
# Real LOAD-file time, including the two forms that used to bypass registration.
set -eu
lib=$(cd "$(dirname "$0")/../src" && pwd)/libfaketime.so.1
work=$(mktemp -d)
cleanup() {
  if test -f "$work/objects"; then
    while read -r pid; do
      case "$pid" in ''|*[!0-9]*) exit 1;; esac
      rm -f "/dev/shm/faketime_shm_$pid" "/dev/shm/faketime_lock_$pid"
    done < "$work/objects"
  fi
  rm -rf "$work"
}
trap cleanup EXIT
python3 - "$work" <<'PY'
import pathlib, struct, sys
p=pathlib.Path(sys.argv[1])
(p/'clock').write_bytes(struct.pack('>qq',946684800,0)*64)
(p/'binding').write_bytes(str(p/'helper').encode()+b'\0'+str(p/'config').encode()+b'\0session\0'+str(p/'active').encode()+b'\0')
(p/'active').touch()
PY
cc -o "$work/helper" "$(dirname "$0")/replay_binding_helper.c" "$(dirname "$0")/../src/ft_sem.c" -pthread -lrt
for mode in empty unset wrong; do
  case $mode in
    empty) command='FAKETIME_PROGRAM_HELPER= FAKETIME="2030-01-01 00:00:00" date -u +%s';;
    unset) command='env -u FAKETIME_PROGRAM_HELPER -u FAKETIME_PROGRAM_CONFIG FAKETIME="2030-01-01 00:00:00" date -u +%s';;
    wrong) command='FAKETIME_PROGRAM_HELPER=/missing FAKETIME_PROGRAM_CONFIG=/wrong FAKETIME_PROGRAM_SESSION=wrong date -u +%s';;
  esac
  # Source only after shell startup: exactly the SessionStart execution model.
  result=$(LIB="$lib" BINDING="$work/binding" CMD="$command" sh -c 'export LD_PRELOAD="$LIB" FAKETIME_PROGRAM_BINDING_V1="$BINDING"; eval "$CMD"')
  test "$result" = 946684800 || { echo "$mode: wrong replay time $result"; exit 1; }
  test ! -e "$work/unavailable"
done
# Broken bindings must not execute business code with the real clock.
rm "$work/helper"
set +e
LIB="$lib" BINDING="$work/binding" sh -c 'export LD_PRELOAD="$LIB" FAKETIME_PROGRAM_BINDING_V1="$BINDING"; date +%s' > "$work/output"
status=$?
set -e
test "$status" = 125
test ! -s "$work/output"
test -s "$work/unavailable"
# Truncation and a missing binding are also fail-closed, never real-time success.
for mode in truncated missing; do
  if test "$mode" = truncated; then printf '/helper\000/config\000' > "$work/binding"; else rm "$work/binding"; fi
  set +e
  LIB="$lib" BINDING="$work/binding" sh -c 'export LD_PRELOAD="$LIB" FAKETIME_PROGRAM_BINDING_V1="$BINDING"; date +%s' > "$work/output"
  status=$?
  set -e
  test "$status" = 125
  test ! -s "$work/output"
done
echo 'replay binding: PASS' 
