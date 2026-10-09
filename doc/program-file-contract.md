# Optional Darwin program file contract

The injected library owns process identity and native time interception. The
controller owns source selection, attempt files and final evaluation. This
protocol does not change timestamp bytes, clock cursors or clock locks. Programs
without the optional variables retain the legacy registration protocol.

## Registration

`FAKETIME_PROGRAM_ACTIVE` opts Darwin into self-reported identity. The library calls:

```
helper __faketime env-init-v2 -- config pid session executable cwd argv0 argv1 ...
```

Identity comes from the registering process, including the actual Python
Framework launcher versus Python.app image. Arguments are never reconstructed
from shell text. The controller validates paths and bounds before matching. PID
is diagnostic, not a request to inspect another process. Helper output remains
NUL-delimited allowed environment assignments. Registration errors retain the
bounded fail-open business behavior and mark evaluation unavailable. Inherited
SAVE/LOAD/SHARED/SHARED_FILE values are cleared before any failure exit.

## State file

`FAKETIME_SHARED_FILE` selects an already-created regular file in place of the
POSIX shared-memory object. Its pinned header/version/size and clock-lock identity
in `FAKETIME_SHARED` are unchanged. The controller creates and deletes the file;
the library maps it and updates the original clock cursor and counters. Without
the optional variable, the original shared-memory path is retained. Darwin online
recording is not enabled by this protocol.

## Activity and completion

The controller creates an empty private `active` file. Each injected program
holds a nonblocking shared file lock from registration through its lifetime.
The descriptor survives fork and exec, including exec of the helper. Its number
is passed in `FAKETIME_PROGRAM_ACTIVE_FD`. A new constructor acquires a new lock
before closing a verified same-inode inherited descriptor. Destructors close,
never unlock, so a forked child's reference remains protected. The OS releases
the last reference on normal exit, `_exit` or SIGKILL.

The helper also holds its own shared lock while registering and writing state,
and checks that the activity file is empty. Final evaluation takes an exclusive,
nonblocking lock, writes `closed` while holding it, then reads registrations and
state. Cleanup uses the same exclusion. New programs reject locked or closed
attempts instead of joining completed state.

This depends on descriptor inheritance. It does not cover descendants that
actively close inherited descriptors and continue without injection, nor audit
work deliberately started after evaluation has ended. It is not an adversarial
process isolation boundary or a new concurrent-fork clock guarantee.
