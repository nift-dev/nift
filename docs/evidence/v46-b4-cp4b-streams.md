# v4.6 Batch 4 CP4b stream recoverable failures and API parity

Status: complete. Converts approved stream backend failures to recoverable
Errors and completes the file-stream lifecycle surface (`open`/`close` method
parity, default construction, `write_bytes`). No filesystem, JSON, or schema
conversion here (CP4a/CP4c).

## Stream API audit vs Strut reference

Nift now matches the Strut stream model for the file-stream surface:

| Feature | Strut | Nift before | Nift now |
| --- | --- | --- | --- |
| `ifstream(path)` / `ofstream(path)` | yes | yes | yes |
| `ifstream()` / `ofstream()` default | yes | no | yes (unopened) |
| `ifs.open(path)` / `ofs.open(path)` | yes | no | yes |
| `ifs.close()` / `ofs.close()` | yes | no (global `close()` only) | yes (same path as global) |
| `close(stream)` global | — | yes | kept (identical semantics) |
| `read_all()` / `read()` / `read_line()` / `read_bytes()` / `read_all_bytes()` / `read_val()` / `eof()` | yes | yes | yes |
| `write()` / `write_line()` / `write_val()` / `flush()` | yes | yes | yes |
| `write_bytes()` | yes | no | yes |
| `>>` / `<<` operators | yes | no (currently unrecognized as Nift syntax and therefore follows Nift's normal external-command fallback) | dedicated CP4b+ checkpoint |
| standard `in` / `out` / `err` streams | yes | no (only `print`/`err` functions) | documented gap; out of CP4b scope |
| `sstream` | yes | no | documented follow-up |

`>>` / `<<` are deliberately **not** part of CP4b. Nift has no multi-character
operator machinery, and before operators exist `s >> x` is not recognized as
Nift syntax, so it follows Nift's normal external-command fallback (which is
intentional and unchanged). Adding the operators needs parser/evaluator work
(multi-char operators, lvalue mutation for `>>`, chaining returns) and is the
dedicated CP4b+ stream-operator checkpoint rather than part of this one.

## One stream state machine

`StreamInstance` now tracks `path`, `open`, and `closed`. Constructors,
`.open()`, `.close()`, and all read/write methods share this state:

```text
unopened (default-constructed) -> .open() allowed; I/O methods are fatal "stream is not open"
open (constructor-with-path or .open()) -> reads/writes allowed; .open() is fatal "stream is already open"
closed (explicit .close()) -> .open() allowed; I/O methods are fatal "stream is closed or invalid"; .close() is fatal "stream already closed"
```

`.close()` and the global `close(stream)` funnel through the same
`Parser::stream_close`; both are fatal on an unopened or already-closed
stream. Explicit `.open()` uses the same authority checks as the constructor
(project-root containment, filesystem-root policy).

## Recoverable codes

| Producer | Code |
| --- | --- |
| `ifstream`/`ofstream` constructor or `.open()` backend open failure | `stream.open_failed` |
| read backend failure (`read`, `read_all`, `read_line`, `read_bytes`, `read_all_bytes`) | `stream.read_failed` |
| write backend failure (`write`, `write_line`, `write_bytes`, `write_val`) | `stream.write_failed` |
| flush backend failure | `stream.flush_failed` |
| explicit close delayed-write failure | `stream.close_failed` |

Detection repairs (CP1 flagged these for CP4):

- `read()` and `read_all()` now check `bad()` like `read_bytes`/`read_all_bytes`
  already did; `read_all` was reimplemented with the chunked `read()` loop so
  backend failure sets `bad()` (the previous `rdbuf()` extraction bypassed the
  stream state and silently returned clean EOF on a directory read).
- Explicit close flushes an output stream before closing and reports a
  delayed write failure as `stream.close_failed`, while still closing.

Semantics kept distinct:

- normal EOF is a stream state (`eof()`, `read_line() == null`), never
  `stream.read_failed`;
- `read_val` conversion/parse failure stays fatal (data-conversion contract,
  not a backend failure);
- wrong direction, closed/unopened misuse, wrong arity/type, non-renderable
  and Error values stay fatal and bypass `catch`;
- `ofs << error(...)` is rejected unless the caller explicitly writes
  `error(...).stringify()`.

Compatibility messages are preserved exactly (`ifstream: cannot open path`,
`ofstream: cannot open path`, `flush: output failure`, etc.).

## Baseline updates

The v4.3 CP78 stream wall's POSIX backend section pinned the pre-CP4b
behavior (directory reads as clean EOF, close not surfacing delayed write
failure). CP4b intentionally changes those two baselines; the wall now asserts
the new detection (`stream.read_failed`, `stream.flush_failed`,
`stream.close_failed`). No other established stream/bytes/FileValue walls
changed.

## Wall

```sh
make test-v46-b4-cp4b
```

runs the immutable Batch 4A gate, the pre-CP4 wall, the CP4a wall, and the
focused CP4b wall. The CP4b wall covers: API parity (constructor-with-path,
default construction + open, `.close()` method, global `close` equivalence,
reopen after close, `write_bytes`); the lifecycle state machine (fatal
misuse); every recoverable stream code with code/category/origin/message
assertions; EOF-is-not-error; fatal-stays-fatal (direction, type, closed,
Error/non-renderable writes); and uncaught compatibility.