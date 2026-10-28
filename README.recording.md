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

These checks must execute inside the native clock path; an external controller
cannot reliably reconstruct a failed write, preserve caller errno or intercept
an internal monotonic clock read afterwards. Timestamp bytes remain two
big-endian 64-bit integers. Binary identity and layout compatibility are still
consumer responsibilities; this is not compatibility with old recorded hashes.

Loader faults, shared-memory/lock initialization failures, arbitrary filesystem
stalls and process termination are not covered by SAVE fail-open.

## Optional Linux program registration

With `FAKETIME_RECORDING` enabled on Linux, `src/program_init.h` invokes the
trusted static helper once at program initialization, before shared state and
SAVE/LOAD initialization. Clock reads do not invoke the helper. There is no
consumer header injection interface, source export, Darwin identity protocol,
file-backed shared-memory alternative or second preload library.

The controller owns matching, identity validation, sequence selection, byte
budgets, final audit and result classification. The native code only performs
bounded helper invocation, applies its NUL-delimited environment response, and
holds the attempt activity lease across fork/exec until process exit.

`FAKETIME_PROGRAM_BINDING_V1` optionally names a regular file containing four
nonempty NUL-terminated fields: absolute helper path, absolute config path,
native session ID and absolute activity path (less than 16384 bytes total).
This takes precedence over legacy helper/config/session/active variables.
An empty or removed legacy variable cannot silently replace replay time.
Missing/malformed binding or failed registration exits 125 before business code.
Without a binding, online registration preserves its fail-open policy and marks
`unavailable` beside the config. This is not adversarial isolation from removing
preload/binding, editing files or closing inherited descriptors.

The helper is invoked as `__faketime env-init -- CONFIG PID SESSION` and has a
two-second deadline. This retains the existing controller protocol; no new
matching engine, clock format or application-level scheduling lives in the library.

## Validation

```sh
sh test/release_parity.sh /path/to/verified/v0.9.13
sh test/clean_build.sh
make -C src clean
make -C src -j2 FAKETIME_COMPILE_CFLAGS=-DFAKETIME_RECORDING all
python3 test/save_fail_open_test.py -v
sh test/replay_binding.sh
```

The fault-injection tests target Linux LE64/flock. They cover SAVE/LOAD, failed
open/write/lock/unlock, whole-record truncation, cross-process latching, malformed
and caller-selected budgets, business exit status and legacy non-opt-in behavior.
Native registration regression tests live here. Full CLI record/pack/replay
integration must also run against the exact combined image build. Default CI
alone does not validate a platform-specific recording configuration.

The fail-open mechanism and internal monotonic opt-out are candidates for
upstream submission, not claimed accepted upstream. Linux program registration remains a downstream requirement until reviewed by
the community. There is no second copy of upstream clock algorithms here.

## Release gate

PR commits are candidate validation inputs, not releases. Consumers must wait
for the maintained recording library to be released, pin the released source
and artifacts, and repeat combined CLI verification before production adoption.
