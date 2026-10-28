#!/bin/sh
# Compare compiled default source to a separately verified release checkout.
set -eu
release=${1:?usage: release_parity.sh UPSTREAM_V0913_SOURCE}
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for flags in "" "-DFAKE_PTHREAD -DFAKE_STAT -DFAKE_UTIME -DFAKE_SLEEP -DFAKE_TIMERS -DFAKE_INTERNAL_CALLS"; do
  # Intentional word splitting: these are fixed compiler flags, never input.
  ${CC:-cc} -E -P $flags "$root/src/libfaketime.c" -o "$work/fork.i"
  ${CC:-cc} -E -P $flags "$release/src/libfaketime.c" -o "$work/release.i"
  cmp "$work/fork.i" "$work/release.i"
done
printf '%s\n' 'Default libfaketime translation unit matches v0.9.13'
