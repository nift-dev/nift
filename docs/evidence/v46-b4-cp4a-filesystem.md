# v4.6 Batch 4 CP4a filesystem / FileValue recoverable failures

Status: complete. Converts only the approved filesystem and FileValue backend
failures to recoverable operational Errors. No stream, JSON, or schema
conversion here (CP4b/CP4c).

## Converted producers and codes

Each conversion identifies its code directly at the failing branch via
`Parser::fail_recoverable(code, message, error)`; no code or disposition is
inferred from message text.

| Producer | Failed operation | Code |
| --- | --- | --- |
| `cd` | `fs::current_path` backend failure | `io.change_directory_failed` |
| `mkdir` / `make_dir` | `fs::create_directories` failure | `io.create_failed` |
| `touch` | output stream cannot open target | `io.create_failed` |
| `rm` / `remove` | `fs::remove` backend failure (with error) | `io.remove_failed` |
| `cp` / `copy` | `fs::copy_file` failure | `io.copy_failed` |
| `mv` / `move` | `fs::rename` failure | `io.move_failed` |
| `cat`, `open` | input stream cannot open path | `io.open_failed` |
| `open_bytes` | input stream cannot open path | `io.open_failed` |
| `open_bytes` | backend read failure after successful open | `io.read_failed` |
| `ls` | directory iteration failure / unreadable directory | `io.directory_read_failed` |
| `FileValue.open` | missing file for `r`/`rw`, or unreadable path | `io.open_failed` |
| `FileValue.save` | parent missing / temp create / temp write | `io.write_failed` |
| `FileValue.save` | final atomic replacement failure | `io.atomic_replace_failed` |
| `FileValue.copy` / `move` / `remove` | backend `fs::copy_file` / `rename` / `remove` failure | `io.copy_failed` / `io.move_failed` / `io.remove_failed` |

### `FileValue.save` classification note

`save()` failures are classified by the actual failed operation per the CP2
registry. "Temporary-file write remains `io.write_failed`" (registry note under
`io.atomic_replace_failed`) is the governing rule: a missing parent that blocks
temp creation, a temp file that cannot be created, and a temp write failure are
all part of the temporary-file write step and therefore `io.write_failed`. Only
the final atomic replacement of the real destination is
`io.atomic_replace_failed`. No new code was invented; the compatibility
messages are preserved exactly.

## Origin behavior

`Parser::fail_recoverable` builds an immutable built-in Error carrying the
exact registry code/category and an empty origin, and stages the matching
`active_diagnostic_`. The existing `fail()` projection path fills the defining
source origin into the diagnostic; CP4a also enriches `active_recoverable_` in
`fail()` with that same origin so a caught Error observes the same
source/line/column as its Diagnostic. Rethrow and nested propagation keep the
first defining origin.

## Fatal and unchanged

The following remain fatal (no conversion) and are verified by the wall to
bypass `catch`:

- wrong argument/arity/type (`open(42)`, `mkdir(42)`, `ls(42)`, `cd(42)`, `cp(1,2)`)
- invalid FileValue mode (`open: mode must be r, w, a or rw`)
- lifecycle misuse (`open: file is already open`, dirty `close`, read/write
  direction misuse)
- directory passed where a file is required (`open`/`cat`/FileValue `open`
  "path is a directory")
- `rm` of a directory without recursion
- `copy` destination/glob validation errors
- path escape and filesystem-root authority denial (`checked_path`,
  `nift_fs_root_allowed`, project-root containment are untouched)
- identity-space exhaustion and interpreter invariants

`exists()` and `pwd` remain structured results. Removing a nonexistent path
(`fs::remove` returning false without an error) remains a silent no-op.

## FileValue failed-save semantics

Preserved exactly, verified by the wall:

- a backend save failure is catchable (`io.write_failed`) where the design
  approves it;
- the FileValue remains open and dirty after a caught save failure
  (`modified()` stays true), so retry remains possible;
- `revert()` still clears dirty state;
- the on-disk content is unchanged by a failed save;
- catching does not implicitly save, revert, or destroy buffered changes;
- an in-process repair (creating the missing parent) followed by `save()`
  applies the buffered content exactly once, with no double rollback;
- `replace_file_atomic` removes its temporary file on failure (POSIX and
  Windows), so no `.nift-tmp-*` leakage.

## Wall

```sh
make test-v46-b4-cp4a
```

runs the immutable Batch 4A CP3 gate and the pre-CP4 repair wall, then the
focused CP4a wall. The CP4a wall covers: catchable codes with
code/category/message/origin assertions; uncaught compatibility (nonzero exit,
preserved message, no Error serialization); prepared/legacy parity; a
no-fallback/no-re-execution guard; fatal-stays-fatal for programmer errors;
FileValue failed-save retry semantics; and both standalone filesystem helpers
and FileValue methods.

The existing v4.3 FileValue wall, CP19 bytes I/O wall, pre-CP4 wall, and CP3
wall all remain green on the CP4a commit.