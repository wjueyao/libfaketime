# Optional recording core on libfaketime v0.9.13

Release baseline: upstream v0.9.13, commit
`86b37fde2fed7336ea2d0c17928e3015a55d9b4a`.
`recording` is the maintenance branch; consumers pin a reviewed commit, never
its moving tip. Post-release upstream changes are intentionally excluded from
this release-based patch set. Upgrade the release baseline as a separate review.

## Default build

Without extra build flags the library keeps the release's shared layout version
1 and its clock, SAVE/LOAD and error paths. `test/release_parity.sh` compares the
preprocessed library translation unit with a separately verified release tree,
both with plain flags and with the default feature flags. The ordinary upstream
build/test matrix runs without recording extensions.

## Optional native recording mechanisms

Build both library and wrapper with `FAKETIME_COMPILE_CFLAGS=-DFAKETIME_RECORDING`.
This explicitly selects shared layout version 2 (one extra `save_errors` latch).
Never mix binaries or shared state from the two build profiles.

The optional mechanisms are limited to:

- `FAKETIME_SAVE_FAIL_OPEN=1`: SAVE open/write/lock/unlock errors latch failure
  in shared state and preserve business time calls and their errno. All processes
  using that state stop writing. Consumers must reject the incomplete recording.
- `FAKETIME_SAVE_MAX_BYTES`: an unsigned decimal byte budget of at least one
  timestamp. The caller owns its application limit; the library enforces the
  supplied capacity while holding the existing write lock. No 64 MiB policy is
  embedded here. No rotation, retry, recovery worker or matching is added.
- Internal wait-clock calls honor `FAKETIME_DONT_FAKE_MONOTONIC` before SAVE/LOAD.
- `FAKETIME_SHARED_FILE`: map the controller-created file instead of the named
  shared-memory object, using the same layout validation and clock lock. This
  supports consumers on platforms without filesystem access to POSIX shm.

These checks must execute inside the native clock path; an external controller
cannot reliably reconstruct a failed write, preserve caller errno or intercept
an internal monotonic clock read afterwards. Timestamp bytes remain two
big-endian 64-bit integers. Binary identity and layout compatibility are still
consumer responsibilities; this is not compatibility with old recorded hashes.

Loader faults, shared-memory/lock initialization failures, arbitrary filesystem
stalls and process termination are not covered by SAVE fail-open.

## Consumer-owned initialization

An optional compile-time `FAKETIME_PROGRAM_INIT_HEADER` names a header defining
`FAKETIME_PROGRAM_INIT()` and `FAKETIME_PROGRAM_CLEANUP()`. The library calls init
after resolving real functions and before shared-state/SAVE/LOAD initialization,
and cleanup after its normal shared-state cleanup. With no header, no consumer
code is compiled or called. The header shares the translation unit and may use
`ft_real_fstat`; it is a source-build seam, not a stable binary plugin ABI.

MindPowers owns its header, helper command, binding format, PID/argv collection,
activity leases, unavailable diagnostics and failure policy in
`internal/faketime/native/`. They are not maintained in this fork. Identity,
matching, occurrences, archive limits and result classification remain in its
Go controller. An image build explicitly supplies a hash-verified header and
keeps its corresponding source alongside the resulting library. No runtime
compiler or new preload library is required.

## Validation

```sh
sh test/release_parity.sh /path/to/verified/v0.9.13
sh test/clean_build.sh
make -C src clean
make -C src -j2 FAKETIME_COMPILE_CFLAGS=-DFAKETIME_RECORDING all
python3 test/save_fail_open_test.py -v
```

The fault-injection tests target Linux LE64/flock. They cover SAVE/LOAD, failed
open/write/lock/unlock, whole-record truncation, cross-process latching, malformed
and caller-selected budgets, business exit status and legacy non-opt-in behavior.
Consumer registration/replay tests belong to the consumer repository and must
run against the exact combined image build. Default CI alone does not validate
the optional header or a platform-specific consumer configuration.

The fail-open mechanism and internal monotonic opt-out are candidates for
upstream submission, not claimed accepted upstream. The file-backed shared state
and initialization seam remain optional downstream mechanisms until reviewed by
the community. There is no second copy of upstream clock algorithms here.
