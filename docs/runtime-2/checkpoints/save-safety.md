# save() filesystem safety (C142; production checkpoint)

**Status:** implemented locally; pending freeze review and owner acceptance. Owner decisions D-074 #1 (the contracts)
and D-091 #5 (authorised after C141 acceptance); the design note was approved with three answers (both engines
through one shared helper, the legacy change limited to its call site; classification before the dirty check; the
read-only rule) and seven additions.

## The defect (F-save, D-073 #1)

Both engines wrote a temporary file beside the path and renamed it over the path. A symlink was therefore replaced by
a regular file (the link lost, its target unchanged); a FIFO, socket or device node at the path was destroyed and
replaced; a read-only file was replaced silently when run as root; and a path naming the process's own stdout or
stderr (for example `/dev/stdout`, or the file stdout is redirected to) was replaced. Demonstrated in this sandbox on
2026-11-11, when a probe's `save()` turned `/dev/stdout` into a regular file (restored by the coordinator).
Re-checked at 3632979 in scratch: the symlink and 0444 cases on both engines.

## Contract and classification order

`nift::rt::natives::save_target(path, resolved, error)` (src/rt/natives/host_std_stream.{h,cpp}, beside the output
repair's `std_stream_target`, which it reuses). Both engines call it after the host call begins and before the dirty
check, so the result does not depend on whether the buffer changed, and before any temporary file exists. Refusals
are recoverable `io.write_failed` errors with these messages:

| Step | Target | Result |
| --- | --- | --- |
| 1 | `lstat` fails with ENOENT | a new file: the existing parent check and atomic create; any other lstat error: **`save: cannot check the target: <reason>`** (C142a) |
| 2 | the process's stdout or stderr: stat() of the path, following every link, against fstat(1) and fstat(2) (st_dev/st_ino; never opened); before the link is resolved, so a link to a pipe is caught (C142a, L4) | **`save: target is the process's standard output or error`**, including a redirect target under its own name and a link to it |
| 3 | a symlink | followed (`canonical`, the whole chain); dangling or looping: **`save: dangling symlink`**; otherwise every later step uses the resolved target, and the save replaces the target, keeping the link |
| 4 | not a regular file (FIFO, socket, character or block device, directory) | **`save: target is not a regular file`**, from stat only: never opened or modified |
| 5 | owner-write bit clear (every user, root included), or, for a non-root user, `access(W_OK)` fails | **`save: target is read-only`** |
| 6 | a regular writable file, hard-linked or not | the temporary file (created exclusively, `fopen(..., "wbx")`: O_CREAT|O_EXCL, never through an existing name or link) in the resolved target's directory, its mode copied from the target, ownership not changed, no fsync (as before); then the atomic rename over the resolved target. A hard link breaks: the saved name gets a new inode, other names keep the old content |

| 7 (C142b, D-093) | immediately before the rename | **`save: target changed during save`** (the temporary file removed, the original untouched) unless the destination is unchanged: absent if it was absent; otherwise not a symlink, the same file (dev/ino), and still allowed by steps 2-5; and a save through a link: the link still resolves to the same target (`save_target_unchanged`, both engines) |

The temporary file's name and its exclusive creation (`save_temp_suffix`, `open_save_temp`: `fopen "wbx"`, O_CREAT|
O_EXCL, so an existing name, a symlink included, fails it with nothing truncated, written through or created) are
shared by both engines. The legacy engine (src/ParserExpression.cpp, the `save` branch) changes only at its call
site: the classification, the resolved target, the shared temporary file (C142b: before, a non-exclusive
`std::ofstream`, which truncated an existing name and followed a link there; REVIEW-C142 L2 fixed, an owner-approved
legacy behaviour change, D-093) and the revalidation.

**What save() supports, and what it does not (owner, D-093).** Supported: safe refusal of prohibited targets under
ordinary, non-adversarial conditions, and atomic replacement of permitted regular files. Not guaranteed: race-free
operation against concurrent adversarial path replacement.

## Races (bounded, documented; known-limitations.md)

save() is not race-safe. The C142b revalidation (step 7) narrows the check-to-rename window to the few syscalls
between that check and the rename; it does not eliminate the TOCTOU race. The swaps it catches are tested
deterministically with a test-only hook build (below); what remains is not tested.

- Between the revalidation and the rename, the name at the resolved target can still be replaced by someone with
  write access to its directory: rename(2) does not follow its last component, so a symlink, FIFO, device or
  read-only file swapped in then is replaced, never written through or opened. (Before C142b this window ran from the
  classification, before the temporary file was written, to the rename.)
- A directory component of the resolved path swapped by someone with write access to an ancestor sends the save into
  the swapped directory. Closing this needs openat/fstatat/renameat through a directory descriptor (and the same in the
  temporary-file creation): REVIEW-C142 (d)2, deferred by the owner (D-093). It is a filesystem race, not a
  portability issue, and it is serious under elevated privilege: **do not run privileged saves into directories that
  untrusted users control.** The revalidation re-resolves the path too, so it catches such a swap made before it, but
  not one made between it and the rename.
- **Worst case, as root (REVIEW-C142 M2).** Through such a parent or path swap the temporary file is created, and the
  rename replaces whatever entry has the target's name, in a directory that was never validated: a read-only or
  protected regular file, a FIFO, or a device node. For example, an ancestor swapped for a symlink to /dev makes a
  root save create `/dev/<name>.nift-tmp-*` and rename it over `/dev/<name>` (`/dev/null`, say). Neither engine
  writes through a link planted at the temporary name (O_EXCL in both since C142b; before, the legacy engine's
  temporary open followed one, REVIEW-C142 L2). save() is not race-safe.
- **Following symlinks (approved order #2) as root.** A save through a link now replaces the link's target, and the
  temporary file is created in the target's directory. A link planted by another user in a directory that root saves
  into can therefore redirect a root save onto any regular file whose owner-write bit is set (for example
  `/etc/passwd`, 0644). Linux's `protected_symlinks` blocks this only in sticky world-writable directories. Before
  C142 such a save replaced only the link.
- The reviewer's narrowing items (D-093): (d)1, the revalidation just before the rename, and (d)3, the exclusive legacy
  temporary file, are implemented (C142b); (d)2, pinning the parent directory, is deferred.
- Windows: `_wfopen(..., L"wbx")`, `std::filesystem` classification, the output repair's console and handle identity;
  the revalidation there has no dev/ino identity (only the type, writability and link checks). Not compiled anywhere
  yet, unverified until Windows CI.

## Behaviour changes (deliberate; REVIEW-C142 L5 and "differences beyond the corrected contracts")

1. A **clean** save (nothing written since open) of a special file, a read-only file, a std-stream target, a dangling or
   looping link, or a path that cannot be checked is now refused. Before C142 it returned nil and did nothing: the
   classification runs before the dirty check by design (answer (ii)).
2. A save through a symlink needs write access to the **target's** directory (the temporary file is created there),
   not the link's.
3. An existing name at the temporary path fails the save ("cannot create temporary file") instead of being
   truncated or written through: the VM since C142, the legacy engine since C142b (owner-approved, D-093); a failing
   close of the temporary file is now an error in both.
4. A symlink loop is reported as `save: dangling symlink`.
5. An lstat error other than ENOENT (ENOTDIR, EACCES, ELOOP in a directory component, EIO) is refused as
   `save: cannot check the target: <reason>` (C142a, REVIEW-C142 L1); before, it was treated as a new file and failed
   later with a misleading message.
6. A destination that changed between the classification and the rename (a different file, a new symlink, a
   special or read-only file, a file where there was none, a retargeted link) is refused, `save: target changed
   during save`, and the temporary file removed (C142b, D-093).
7. `/dev/stdout` (or any link to the process's stdout or stderr) is refused with the std-stream message even when
   stdout is a pipe: the std-stream check runs on the path, through stat(), before the link is resolved (C142a,
   REVIEW-C142 L4); before, `canonical()` could not resolve `pipe:[...]` and the refusal read `dangling symlink`.

Pre-existing and unchanged (known-limitations.md): `open("w")` reads the existing file, so a FIFO with no writer blocks
at `open` (both engines, 5-second timeout in scratch), before `save()` is reached.

## Tests (`make test-runtime2-save-safety`, in `test-rt-vm`; tests/runtime2/save_safety.py)

Every case runs in both engines (NIFT_VM=require and NIFT_VM=0), each in a fresh temporary directory, with an
address-space and file-size limit and a 20-second timeout. The harness never opens /dev/stdout and checks that it is
still the link to /proc/self/fd/1 before and after. Special files reach `save()` through a file opened on an absent
name and an `mv()` of the fixture onto it, so `open()` never reads them; the device is a `mknod c 1 3` inside the
directory, only stat()ed and removed in a finally block, and reported NOT-RUN (listed in skip_phrases.tsv, never a
pass) where mknod is not permitted.

| Case | Checks |
| --- | --- |
| regular | saved, mode kept (0640), new inode, no temporary file |
| new-file | created |
| hard-link | saved name new content and new inode; the other link keeps the old content |
| symlink | the target saved, the link kept |
| dangling | refused; the link kept, nothing created |
| read-only, read-only-unchanged | 0444, run as root: refused (with and without a write); content, inode, mode kept |
| fifo, socket, device | swapped in after open: refused; still that type |
| symlink-fifo, symlink-device | a link to one swapped in: refused; the node and the link kept |
| stdout, stderr, symlink-stdout | stdout or stderr redirected to a scratch file, saved by name or through a link: refused; the file's inode kept, the program's output intact |
| symlink-chain (C142a, L3) | l1 -> l2 -> target: the target saved, both links kept |
| symlink-loop (L3) | a -> b -> a: refused as dangling; both links kept |
| symlink-read-only (L3) | a link to a 0444 file: refused; the file and the link kept |
| non-root-access (L3) | nift run as nobody; a root-owned 0644 file in a directory anyone may write (not sticky): refused by access(W_OK) (the rename alone would succeed); reported NOT-RUN where the harness cannot run nift as nobody |
| not-a-directory (L1) | `f.txt/x`, f.txt a regular file (ENOTDIR): refused as `cannot check the target: Not a directory`; f.txt kept |
| race-fifo, race-read-only, race-new-inode, race-symlink (C142b, hook builds) | at the pre-rename point the hook moves t.txt aside and puts a FIFO, a 0444 file, a new regular file, or a link to the original there: refused (`target changed during save`); the swapped object and the original unchanged, no temporary file left |
| race-retarget (hook builds) | a save through l.txt -> t.txt; the hook retargets l.txt to u.txt: refused; t.txt, u.txt and the new link unchanged |
| race-appear (hook builds) | a new file; the hook creates it: refused; the created file unchanged |
| occupied-regular, occupied-link, occupied-dangling (hook builds) | the (fixed) temporary name is already a regular file, a link to another file, or a dangling link: refused (`cannot create temporary file`); nothing truncated, written through or created |
| stdout-pipe-link (L4) | stdout a pipe; a scratch link to /proc/self/fd/1 renamed (mv) onto a name opened while absent, never opened: refused with the std-stream message; the link kept |

Negative controls (one per contract, `NIFT_SAVE_MUTANT`, test-only, each built with sanctioned_mutant.py and removed
after its run):

| Mutant | Removes | Caught by |
| --- | --- | --- |
| 1 | the dangling refusal (saved as a new file) | dangling, symlink-loop (both engines) |
| 2 | the std-stream refusal | stdout, stderr, symlink-stdout, stdout-pipe-link (both) |
| 3 | the special-file refusal | fifo, socket, symlink-fifo (both); the device cases too where mknod is permitted |
| 4 | the read-only refusal | read-only, read-only-unchanged, symlink-read-only, non-root-access (both) |
| 5 | symlink resolution (the link itself replaced) | symlink, symlink-chain (both) |
| 6 | classification before any temporary file (one created first) | every VM case (temporary file left) |
| 7 | atomic replacement (the target written in place) | regular (same inode), hard-link (the other link changes) |
| 8 (C142a) | REVIEW-C142 L1 (any lstat error treated as a new file) | not-a-directory (both) |
| 9 (C142b, hook build) | the revalidation | race-fifo, race-read-only, race-new-inode, race-symlink, race-appear (both) |
| 10 (hook build) | the link re-check | race-retarget (both) |
| 11 (hook build) | nothing: the control, correct code with the hooks | must pass every case, the race and occupied cases included |
| 12 (hook build) | the exclusive temporary file (`"wb"`: truncates, follows a link) | occupied-regular, occupied-link, occupied-dangling (both) |

The hooks exist only in builds 9-12 (`NIFT_SAVE_MUTANT` 9 to 12, built with sanctioned_mutant.py): the swap command
from `NIFT_TEST_SAVE_SWAP`, run at the pre-rename point, and a fixed temporary name (`.nift-tmp-fixed`). The
production nift contains neither (its binary has no `NIFT_TEST_SAVE_SWAP` string).

Wiring (C142a, REVIEW-C142 M1): `save-safety` is a suite of the test-rt-vm recipe (`$(SAVE_SAFETY_GATE)`, the cases
then the mutants) and of `RT_VM_SUITES`, not a prerequisite, so its failure cannot stop the other suites (the C140b
G1 defect). scripts/test_integrity_check.py's suite-wiring rule now also reports any test-rt-vm prerequisite outside a
frozen list of the existing ones (RT_VM_PREREQUISITES; a stale entry is reported too), with a negative control in every
run: the same scan on the Makefile plus one added prerequisite must report it. On the C142 Makefile it reports
`test-runtime2-save-safety`.
