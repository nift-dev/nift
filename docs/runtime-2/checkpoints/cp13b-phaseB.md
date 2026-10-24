# CP13B Phase B — exceptions on the VM

**Base:** `6718459944830005cb466456b5141af985ee07e7`, tree
`6ba8a984a4ea537972d64a2de00e8f2941f76462`.
**Authoritative interface:** `.claude-linode-runs/deepseek/cp13b-interface-freeze.md`
(with owner amendments A1/A2). This checkpoint records the implementation
against that freeze; it is not a competing interface authority.

## Implemented

- `SourceMap` (`src/rt/source_map.{h,cpp}`) owns copies of the entry and every
  linked module source. Position tables are built once with `std::call_once`;
  lookups are read-only and thread-safe. A successful run never calls `at()`.
  The map borrows the immutable `LinkedUnit` only for module lookup and must
  not outlive it; the owned source copies make caller buffers releasable.
- `Vm::report_offset` is the single CP12C location rule used by uncaught
  faults and catchable Host/native Errors. Throw origins use the throw
  statement offset (D5), not `report_offset`.
- `Fault.error` and the five frozen opcodes: `TryBegin`, `TryEnd`, `Throw`,
  `ErrorNew`, `ErrorJson`.
- `VmResult.error_origin`, `origin_source`, `origin_line`, `origin_column`;
  `NIFT_VM_STATS` gains the boolean `error_origin` key.
- Handler state lives beside `frames_` in the `Vm` (same stack, keyed by
  frame depth), with `TryEnd` on normal, return, break and continue exits;
  unwinding restores frames, registers, base/proto/K/pc and `closure_`.
- Catchable Host/native errors use the existing `host::catchable` and
  `natives::recoverable_code` predicates; no third classification table.
- Error construction/members/stringify/prettify/highlight and `print(Error)`;
  legacy's `Error values cannot be rendered as text` expression-statement path
  stays a whole-unit fallback.
- Try/catch scopes participate in closure capture; a catch binding is
  body-scoped and readonly, including when captured.
- A1 is implemented by owned source copies plus concurrent first-use and
  post-initialisation tests. A2 is a documented CP22 contract: a future keeps
  the raised Error value and `await` re-raises it unchanged; await-site data
  is separate and never overwrites Error provenance or equality.

## Validation

Focused gates: Phase A legacy/current/future 80/80; CP13B self-tests,
adversarial 24/24, E1–E8 mutants (including E2 checked and E7 ASan), 1M-catch
stress, diagnostics 89 baseline + changes 34, closures 52, capture 82,
SourceMap unit/TSan/ASan, both dispatch builds (`-DNIFT_RT_VM_SWITCH`
included), `test-rt-vm`, Host/CP16 N1 and read-limit tests, `rt-diff` 820
entries, warnings, lint and test integrity. See the evidence package for the
exact logs and identities.

## R1 focused corrections

Codex's independent review found three bounded defects; R1 fixes only these:

- `ErrorJson` now renders a null receiver as `null`, covering nullable
  `Error.cause` for `stringify`/`prettify`/`highlight` while keeping the
  Error-only contract for other kinds.
- `SourceMap` builds all module tables in local storage and publishes them by
  move only after the whole build succeeds; a failed `call_once` initialization
  can retry without duplicated or shifted tables.
- `make_system_error` and `clone_error_with_origin` hold each `ErrorObject` in
  a `unique_ptr` until all throwing initialization and heap accounting are
  complete, then transfer ownership through `Value::adopt`. OOM remains fatal.

Regressions: `tests/runtime2/cp13b/nullable_error_presentation.py`,
`tests/rt/source_map_retry_unit.cpp` and `tests/rt/error_oom_unit.cpp`
(including ASan/LSan/UBSan and concurrent retry readers).

## Outstanding

CP22 must supply the integration test that an awaited Error equals the
original and reports its original origin. Whichever of CP17 W13 or CP13B
lands second adds the abandoned-frame handle-release regression. Shared-file
reconciliation is required with CP22 (`vm.*`, compiler/infer) and CP17
(`vm.*`, compiler). No Phase B production change owns CP20/CP21 lifecycle
code.
