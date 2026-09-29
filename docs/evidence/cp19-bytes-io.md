# CP19 binary file and stream I/O

Date: 2026-09-29

Status: **complete candidate** at the CP19 implementation tree.

## Contract implemented

- `open_bytes(path)` returns the exact file payload as immutable bytes.
- Input streams expose `read_bytes(count)` and `read_all_bytes()`. Managed files
  expose the same methods using their existing byte cursor and mode rules.
- Output streams and managed files accept bytes through `write(value)` and emit
  the exact payload without encoding, formatting or newline insertion.
- `write_line(bytes)` remains rejected because line output is a text API.
- Existing `open`, `read`, `read_all`, `read_line`, string `write`/`write_line`,
  process capture and value serialization retain their text behavior.
- Reads use bounded 64 KiB chunks, treat EOF as a valid short result, and reject
  non-EOF stream failures. A large requested count does not allocate that count.
- Managed writes retain established overwrite, suffix, extension, cursor,
  dirty, revert, save and close behavior. Atomic save now uses the shared
  cross-platform replacement primitive, including replacement of an existing
  destination on Windows and destination-permission restoration on failure.
- All path inputs retain the existing project and `NIFT_FS_ROOT` confinement.

## Focused evidence

`tests/cp19_bytes_io.sh` certifies:

- Embedded NUL, LF, CR, high bytes and the complete `0..255` octet range.
- Empty files/writes, zero-count reads, short EOF reads, repeated EOF, exact
  counts above JavaScript-safe integer range, rejected `size_t` overflow, and a
  read crossing the 64 KiB chunk boundary.
- Exact stream and managed-file round trips.
- Existing managed destinations opened in `rw` and `w`, shorter replacement,
  suffix preservation, extension, seek/tell, dirty state, save/reopen and
  revert behavior.
- Prepared and deliberately legacy managed raw-write parity.
- Wrong direction, closed stream, invalid count, `write_line(bytes)`, missing
  path and filesystem-root rejection.
- Retained string return/write behavior for the existing text APIs.

Local results:

- `make -j2 test-cp19-bytes`: pass.
- `make -j2 test-cp17-bytes test-cp18-bytes test-engine-bindings`: pass.
- `make -j2 test-cp18-bytes-sanitize`: pass, including CP19 under ASan/UBSan.
- `make -j2 test-cp18-bytes-tsan`: pass, including CP19 under TSan.
- `make -j2 test`: pass, including the registered CP19 target and historical
  text/stream/managed-file compatibility walls.
- `tests/v43_cp112_filevalue_wall_smoke.sh`: pass.
- `git diff --check`: pass.

## Defect review

The first review rejected the candidate because existing-file replacement failed
on MinGW, read failures could be mistaken for EOF, prepared managed writes kept
the old bytes rejection, and edge coverage was insufficient. Repairs moved
managed save to the shared Windows-capable atomic replacement path, made
destination permission changes failure-transactional, added checked chunked
reads, aligned prepared raw writes, and expanded the focused wall. Final review
reported no blocker, high or medium findings.

## Deferred by scope

CP19 adds no mutable byte buffer, FFI conversion, embedding ABI, binding API,
process-byte capture, mmap, zero-copy view or proof-consumer integration. CP20
owns explicit copying bridges to mutable FFI buffers.
