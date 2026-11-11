# Output-order and standard-stream data-loss repair (C139; production checkpoint)

Owner authority:
- D-072 #1 (2026-11-11 18:00): the `print` / `cat` output order is a P1 release blocker; invariant: "output
  operations targeting the same logical stream preserve program execution order"; no global unbuffering; exact-byte
  legacy comparisons; an effective mutant restoring the defect; no re-baselining.
- D-073 #2 and #3 (2026-11-11 22:23): the narrow worker refusal of standard-stream handles, and the bounded design
  ("approximately 120-160 production lines and 450-550 test lines").
- D-077 #5 (2026-11-12): item 1 of the pre-certification production order.

Design: the read-only design C (review-ha DESIGN-OUTPUT-ORDER.md), with the reviewer's notes in
INVESTIGATE-SAVE-SPECIAL-TARGETS.md. `save()`'s replacement of special targets (F-save) is not changed here: it is
the `save()` checkpoint (D-077 #5 item 3); this checkpoint never calls `save()` on a standard stream, in code or tests.

## 1. What is implemented

- **`cat`.** The direct `fwrite`/`fflush` is gone. The bytes go to the VM output sink exactly where a `print` would put
  them (`natives::emit_program_output`, `Vm::native_emit`): the exact bytes, NULs included, then the usual
  `flush_if_needed`, so the 64 KiB flush threshold, the parallel-output publication of a worker's output and the
  end-of-run write apply unchanged. The read path (read limit, budget, D-028 observation, trace, errors, the return
  value) is untouched. Without a VM (unit tests) the bytes still go to stdout.
- **Publication.** `Vm::native_publish`: the pending VM output through the runner's flush callback, then
  `std::cout.flush()`. A worker never publishes there (it cannot hold a std-stream handle).
- **Identity** (`natives::std_stream_target`, src/rt/natives/host_std_stream.cpp): POSIX `stat(path)` (following links)
  against `fstat(1)` and `fstat(2)` by `(st_dev, st_ino)`; the actual destination, not a filesystem: `/dev/fd/1`,
  `/proc/self/fd/1`, a user symlink, and the redirect target under its own name all match; another file on the same
  filesystem does not. The path is never opened (a FIFO cannot block), and the check runs before the open truncates or
  reads. Windows: `CON` and `CONOUT$`, or `GetFileInformationByHandle` (volume serial and file index) against
  `GetStdHandle(STD_OUTPUT_HANDLE / STD_ERROR_HANDLE)` on a handle opened with no access rights. **Windows is coded
  and CI-verified only; legacy's Windows behaviour has not been measured.**
- **Boundaries**, only for a handle whose path is a std stream (a flag recorded at open):
  - before `ofstream(path)` (which truncates) and before `file(path).open(mode)` (which reads the target);
  - before each stream `write` / `write_line`, `flush` and `close`, and before `file().save()`;
  - before a whole-file read of the stdout or stderr object (`open`, `open_bytes`, `cat`, `sha256_file`,
    `file().open("r")`).
- **End of run.** A std-stream output stream still open at the end is not flushed by the handle cleanup: it is held
  (`hold_std_stream`) and released by the script runner after the VM output and the error message
  (`release_std_stream_tail`), the order legacy has at process exit. Other handles keep today's cleanup.
- **Workers** (D-073 #2): in a `thread()` task or an `async()` coroutine task, opening a std-stream handle
  (`ofstream`, `file().open`) is refused before anything is opened or truncated, with a catchable error:
  `ofstream: a thread() or async() task cannot open the process's standard output or error` (`stream.open_failed`;
  `open: ...` with `io.open_failed`). Worker `print`, `cat` and ordinary file I/O are unaffected. Recorded as the
  approved difference `difference:worker-std-stream-refused`.
- **Not done (by design):** no global unbuffering, no per-print flush, no flush for unrelated files, no destructive
  path replacement, no change to `save()`.

**Size:** 158 added src lines that are not blank or comments (202 with comments), 13 removed (two of them the
`fwrite`/`fflush`, the rest compressions of neighbouring lines to keep two files within the 400-line limit). Files:
host_std_stream.{h,cpp} (new: identity, the open gate, cat's emission, the end-of-run tail), builtin.h (Context
hooks), vm.h / vm_builtin.cpp / vm_parallel_output.cpp (VM wiring), host_fs_internal.h and host_fs_read.cpp (cat and
reads), host_file_handles.cpp, host_stream_handles.cpp and host_file_ops.cpp (boundaries), file_resource.h (flags),
script_runner.cpp (the tail release). No concurrency machinery.

## 2. Tests and controls

tests/runtime2/output_order.py (`make test-runtime2-output-order`, in test-rt-vm): 46 cases. Each runs on legacy and
on the VM (`NIFT_VM=require`) with stdout a pipe, the scratch file `o.txt` (`>`), `o.txt` holding `PRE\n` (`>>`), and
under `2>&1`, and compares the exact bytes of stdout (or `o.txt`), stderr, the exit status and every scratch file.
- cat order: print/cat/print; two and three cats; cat in a function in a loop; cat through an imported module; cat
  after more than 64 KiB of print; binary data with a NUL; a missing file caught; a runtime error after cat and after
  prints.
- handles, for `/dev/fd/1`, `/proc/self/fd/1`, a scratch symlink to `/dev/fd/1`, and the redirect target by name:
  open only (truncation), write and close, write without close (closed at the end), write / flush / print / write /
  close, a 16 KiB write, and a runtime error after a write; `/dev/fd/2` under `2>&1`.
- reads of the program's own output: `open`, `cat` and `file().open("r")` of the redirect target.
- an ordinary file (unchanged).
- workers: print and cat in a thread and in an async function (legacy-equal); an ordinary file in a worker
  (legacy-equal); and four VM oracles for the refusal (thread, async, the redirect target by name, which must not be
  truncated, and `file().open`).

No test opens `/dev/stdout` or calls `save()` on a standard stream; the device paths are opened only inside the
child, whose descriptors are the harness's scratch file or pipe.

Mutants (`output_order.py --mutants`, built by sanctioned_mutant.py): 3 of 3 caught.

| Mutant | Caught by |
| --- | --- |
| `NIFT_OUTPUT_ORDER_MUTANT=1`: cat's direct `fwrite`/`fflush` restored | `print-cat-print`, `cat-module`, `cat-binary` (and 8 more) |
| `=2`: the identity check always false | `write-close-fd`, `write-flush-fd`, `read-own-open`, `read-own-cat` (and 28 more) |
| `=3`: no worker refusal | the four worker oracles |

## 3. Gates

nift sha256 `cb820a494cb3a77f22853fe9923645988b9570b08035d60e87849d07e4e64cb6`; every gate ran on it with `/dev/stdout` a symlink to /proc/self/fd/1 (recorded per run).

| Gate | Result |
| --- | --- |
| `make test-rt-vm` (one uninterrupted run, no `-k`) | PASS, its whole recipe included (output-order 46/46 and its 3 mutants, vm_inventory, cp12b_inventory selftest last) |
| test-runtime2-output-order | PASS: 46/46; mutants 3 of 3 caught |
| test-runtime2-safety, test-runtime2-safety-mutants | PASS |
| test-rt-host | PASS: host-process-unit 10002 checks, 0 failures |
| test-rt-host-mutants | PASS: cp17 handle mutants 46, 0 survived |
| test-runtime2-cp16-n1 | PASS |
| test-rt-vm-mutants | PASS: 12 mutants, 12 rejected |
| test-rt-builtins-guards (callgrind) | PASS: 35 workloads, 0 guard failures; no re-baselining |
| test-rt-lint, test-test-integrity | PASS |
| test-runtime2-host-deps | **FAILED** (exit 2): the same 24 known refusals, identical to the C129 run after normalising paths and durations (open release-gate findings, D-075 #1 and D-077 #3) |
| fresh inventory collect + `report --check` | exit 3 (expected): G1 PASS, 137 reasons seen: approved 14, deferred-named 29, implement 32, implemented-awaiting-review 0, implemented-accepted 3, PENDING-OWNER 59, retired 0; blocked by the 59 PENDING-OWNER reasons and by `defect:print-cat-output-order`, now implemented-awaiting-review |

**Changed tests:** no existing test expectation changed. New: tests/runtime2/output_order.py and the Makefile target
test-runtime2-output-order (in test-rt-vm). Records: tsv `defect:print-cat-output-order` implement ->
implemented-awaiting-review; new row `difference:worker-std-stream-refused` (approved, D-073 #2); known-limitations.md
P1 entry marked implemented, awaiting review.
