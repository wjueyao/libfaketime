# Optional fail-open timestamp recording

Based on upstream v0.9.13 (`86b37fde2fed7336ea2d0c17928e3015a55d9b4a`).
The fork adds opt-in fail-open SAVE and program registration. The timestamp
record format is unchanged.

`recording` is the maintained branch. Upstream changes are merged here and
tested before consumers update their pinned commit. Builds must not follow
the moving branch tip automatically.

Set `FAKETIME_SAVE_FILE` as usual, with `FAKETIME_SAVE_FAIL_OPEN=1` and
`FAKETIME_SAVE_MAX_BYTES` (16 through 67108864 bytes). The caller chooses the
budget; the library has no application-specific default. Without the opt-in,
upstream error reporting and exit behavior remain in effect.

In this mode, a SAVE file open/write/lock/unlock error or budget exhaustion
stops recording and lets the time call return. SAVE preserves the caller's
`errno`. A shared `save_errors` flag latches to 1 so later processes using the
same shared state stop writing. Consumers must reject a recording with this
flag set: a truncated sequence can still contain valid, whole timestamps.

This needs one additional shared-memory field and layout version 2. Build the
wrapper and library together; do not mix this layout with upstream v0.9.13's
layout version 1. No separate manifest or ELF compatibility marker is added.

## Scope and tests

On controlled Linux, setting all of `FAKETIME_PROGRAM_HELPER`,
`FAKETIME_PROGRAM_CONFIG`, and `FAKETIME_PROGRAM_SESSION` opts into program
registration. The trusted static helper is invoked once per library
initialization as `__faketime env-init CONFIG PID SESSION`; it supplies NUL
delimited environment assignments for the program's SAVE/LOAD stream. No
shell evaluation is performed. Missing, failing, or timed-out registration
keeps real time and writes an `unavailable` diagnostic next to the config.
The helper deadline is two seconds. Without these variables the registration
path is inactive. This does not identify logical work across reordered threads
or fork-without-exec; the caller must keep those execution conditions stable.

Internal clock reads honor `FAKETIME_DONT_FAKE_MONOTONIC` before SAVE/LOAD,
including wait helpers which bypass the public clock wrapper.

This is not isolation from arbitrary library faults. Dynamic loader errors,
shared-memory/lock initialization failures, blocking filesystem operations,
and process termination remain outside the SAVE policy. Recording still adds
I/O and synchronization overhead. The validated target is Linux LE64 with the
default flock backend; other platforms are not claimed by these tests.

Build and run in an isolated Linux environment:

```sh
make -C src all
make -C test shm_layout_test
./test/shm_layout_test
python3 test/save_fail_open_test.py -v
```

The fault-injection helpers are test-only and are never installed. Tests cover
normal SAVE/LOAD, open and write failures, SAVE lock/unlock failures, capacity
and malformed limits, cross-process failure latching, business exit status,
and preservation of the default non-opt-in behavior.

The timestamp record is unchanged (two big-endian 64-bit integers). The shared
flag is a boolean latch, not an error counter or event log. There is no retry,
recovery worker, file rotation, or new application configuration format.
