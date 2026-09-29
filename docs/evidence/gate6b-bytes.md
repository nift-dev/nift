# Review Gate 6B: generic bytes value semantics

Date: 2026-09-29

Decision: **pass** at `9bc4bc59f2a2868c7969bcf74fc9ada3d35a3016`.

## Reviewed scope

- `RuntimeValue` and public C++ bytes are genuine immutable values with a
  dedicated runtime tag and shared contiguous backing. Jsonic remains strict
  JSON and no marker-string representation is used.
- Construction, length, indexing, safe indexing, copying slices, bytes-only
  concatenation, equality, truthiness, introspection and strict UTF-8 conversion
  are checked. Mutation, ordering, implicit conversion and mixed concatenation
  are rejected.
- Prepared and deliberately forced-legacy callable bodies produce equal results
  for byte construction, operations and UTF-8 conversion. Focused diagnostics
  cover invalid construction, indexing, ordering, concatenation and codecs.
- Direct rendering, interpolation, templates, shell output, `nift eval`,
  `stringify`/`prettify`/`highlight`, command arguments, managed-file and stream
  text writes, and managed-file and stream `write_val`/`read_val` reject direct
  or nested bytes before text or strict-JSON serialization.
- Assignment, public and language copy/deepcopy, prepared and legacy returns,
  closures, nested aggregates, mutex snapshots, threads and async values retain
  immutable backing while aggregate shells remain independently mutable.
- An exact patterned 8 MiB value fanned out to 256 simultaneously live copies
  retains one non-null backing pointer. Pointer identity is the deterministic
  portable amplification guard; RSS thresholds are intentionally not used.

## Review repairs

The first independent review did not find a core implementation defect, but
correctly rejected the evidence as incomplete. Repairs added:

- A dedicated hosted Gate 6B workflow for Linux GCC, Linux Clang, macOS Clang,
  Windows MinGW, ASan/UBSan and TSan.
- Explicit prepared-versus-legacy byte operation and codec parity.
- Focused managed-file value I/O, presentation, and `cmd`/`run` rejection tests.
- ASCII-only test source for the non-ASCII UTF-8 round trip, avoiding MSYS argv
  transcoding while retaining the same `C3 A9 F0 9F 98 80` payload.
- Broad automatic workflow triggers for relevant source, public-header and
  focused-test changes.

The repaired independent review reported no blocker, high or medium finding and
recommended passing Gate 6B.

## Hosted evidence

Gate 6B run: https://github.com/nift-dev/nift/actions/runs/36564268837

- Linux GCC: https://github.com/nift-dev/nift/actions/runs/36564268837/job/109392269536
- Linux Clang: https://github.com/nift-dev/nift/actions/runs/36564268837/job/109392269432
- macOS Clang: https://github.com/nift-dev/nift/actions/runs/36564268837/job/109392269158
- Windows MinGW: https://github.com/nift-dev/nift/actions/runs/36564268837/job/109392269392
- ASan/UBSan: https://github.com/nift-dev/nift/actions/runs/36564268837/job/109392269417
- TSan: https://github.com/nift-dev/nift/actions/runs/36564268837/job/109392269556

All jobs ran the CP17 language semantics and CP18 transfer suites where
applicable and passed at the reviewed SHA.

## Residual risk and scope boundary

TSan is available on Linux only. Shared-backing identity proves that live value
copies do not duplicate the large payload but does not measure allocator
overhead or transient allocations made by explicit concatenation and slicing.

Gate 6B adds no binary I/O, bytes/FFI-buffer bridge, embedding ABI or maintained
binding surface. Those remain owned by CP19-CP22. Review Gate 7 continues to
block all proof-consumer integration.
