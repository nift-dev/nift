# CP07 — Process capture and PATH cache

Status: **certified as a leak/resource fix; launch-cost gate deferred, not passed** (owner decision D-011; pipe-based execution is a CP16 objective).
Behaviour-preservation work (no leaked capture files) is done and verified.

## Objective (roadmap)
Cheaper process launch with captured output (gate: ≤ Bash + 15%), no capture
files left in `/tmp`, PATH lookup caching — without behaviour change (D-006,
D-010).

## Pipe attempt rejected
A first implementation (`/home/claude/review/cp07-pipe-attempt/0001-runtime-2-CP07-pipe-based-process-capture-validated-.patch`,
never published) captured through pipes drained by `poll`, handed the pipes to
a detached "sink" process when background descendants kept them open, and
added a Windows PATH cache. It met the launch gate, but the adversarial review
reproduced observable behaviour changes:
1. hang / unbounded memory when a background descendant keeps writing more
   often than the 100 ms idle window (`(while :; do echo hb; sleep 0.05; done) &`);
2. timing-dependent extra output from descendants after the launched process
   exits (output was a superset of the old one, but unstable);
3. children killed by SIGPIPE when Nift dies mid-capture (old: they finish);
4. child stdout/stderr became FIFOs (`S_ISREG`, `lseek`, `> /dev/stdout`
   truncation and `tee /dev/stdout` results all changed);
5. one sink process per held stream, +100 ms per such call, 200 sinks after
   100 calls; 6. earlier failure under low descriptor limits.
Under D-006/D-010 these are not acceptable for interim performance work, so the
design was replaced.

## Implementation (behaviour-preserving)
- POSIX: capture files stay regular files in `/tmp`, but are anonymous. Linux
  opens them with `O_TMPFILE` (never linked); other systems, or filesystems
  without `O_TMPFILE`, use `mkstemp` and unlink immediately. The captures are
  read back through their descriptors with `pread` from offset 0 at the same
  moment as before (after the launched processes are reaped). Writes by
  background descendants afterwards go to the unlinked file and are dropped,
  as before. `O_TMPFILE` was preferred over `memfd_create` so the bytes live on
  the same storage as before (a long-lived background writer fills disk, not
  RAM); `memfd` measured no faster.
- Root cause of the leaked `/tmp/nift-proc-*` files: the named files existed
  for the whole run and were removed only on normal completion, so any Nift
  killed during a capture (timeouts, SIGKILL in tests, crashes) left both
  behind. Anonymous files cannot be leaked.
- Windows: each capture file gets an extra non-inheritable "keeper" handle
  opened with `FILE_FLAG_DELETE_ON_CLOSE`; every other handle shares delete
  access; captures are read through the keepers. The file disappears when the
  last handle closes, even after a crash or while a background descendant
  still holds a write handle (previously `DeleteFileW` failed in that case and
  the file leaked). The name still exists while open (Windows has no anonymous
  files). Not compiled in the campaign environment: CI only.
- PATH cache: not added. POSIX lookup is one `stat` per PATH directory; the
  Windows cache had staleness risks (empty PATH segments, FAT/exFAT and SMB
  directory mtimes) for little gain.
- `scripts/v411_hardening_mutations.py`: the `capture-cleanup` mutant now
  removes the descriptor cleanup on the capture-creation failure path (still
  caught: "fd leak").

## Gate: launch cost — not met
300 × `/bin/true` with captured output, median of 15 interleaved runs, shared
2-vCPU host at load ~15 (wall seconds):

| Binary | wall | ÷ Bash | ÷ Bash `$()` | CPU (user+sys) ÷ Bash `$()` |
| --- | ---: | ---: | ---: | ---: |
| runtime-2 (c5db14d) | 1.074 | 1.55 | 1.27 | 1.02 |
| CP07 (O_TMPFILE) | 1.062 | 1.53 | 1.26 | 0.99 |
| memfd variant | 1.043 | 1.50 | 1.24 | 0.98 |
| pipe attempt | 0.738 | 1.06 | 0.87 | 0.97 |
| Bash `$()` | 0.844 | 1.22 | 1.00 | 1.00 |

CPU cost already equals Bash `$()`; the wall-clock gap is waiting. With a file
the parent can only learn that the child is done from its exit notification;
with a pipe it wakes on hang-up while the child is still tearing down. A
standalone C loop shows the same gap (memfd 0.70–0.88 s vs pipe 0.55–0.72 s
per 300 launches), so it is not Nift overhead. Meeting the gate needs pipes or
a pipe-like completion signal, which changes observable child behaviour —
an owner decision (escalation in `progress.md`). The pre-CP07 binary does not
meet the gate either.

## Tests
- `tests/runtime2/process_capture.sh` (`make test-runtime2-process-capture`,
  in `make test`): regular-file stdout (`stat -L`, `S_ISREG`, `lseek`,
  `/dev/stdout` truncation, `tee`), 4 MB + 2 MB on both streams, NUL bytes,
  exit codes 7/137, background descendant neither blocks nor is captured, the
  child survives Nift being terminated, capture under `ulimit -n 6`, no
  `/tmp/nift-proc-*` files after SIGKILL, nothing in `TMPDIR`. The reference
  binary passes every behaviour check and fails only the leak check (4 files).
- Not changed and identical in both: a background writer that never stops
  (`yes &`) makes both the old and new code chase the growing capture file
  (pre-existing; noted for the VM's Host process API).

## Residual risk
- Windows path compiled and tested only in CI.
- `/proc/self/fd/1` link text differs (`/tmp/#<inode> (deleted)` instead of a
  random `/tmp/nift-proc-XXXXXX`); the old name was random, so nothing could
  depend on it.
