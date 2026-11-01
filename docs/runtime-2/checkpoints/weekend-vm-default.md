# Weekend integration: strict VM default (cand/vm-default)

Status: **integrated into local `weekend/r2` under an explicit owner exception**
(2026-10-31). Not independently accepted. `runtime-2` not advanced; nothing pushed.

- Base: `1512579f` (runtime-2 = origin/runtime-2 = weekend/r2 start).
- Candidate: `cand/vm-default`, `0503062d..71bb0056` (22 commits), frozen at `71bb0056`.
- Merge into `weekend/r2`: merge commit with parents `1512579f`, `71bb0056`; tree identical to `71bb0056`.
- Contract: `tests/runtime2/vm_default/REFUSAL_MATRIX.md`; decision D-061 (startup `exit`).

## Independent review ledger

| Review | Tip | Verdict | Principal findings |
|---|---|---|---|
| 1 | b61784a2 | REJECT | (see candidate history) |
| 2 | b8a5da28 | REJECT | (see candidate history) |
| 3 | f96be6ef | REJECT | (see candidate history) |
| 4 | 50ba8db6 | REJECT | shell-form `\|\|` ran after refusal; earlier-unit location; startup refusal order |
| 5 | 5e2604c8 | REJECT | `$[...]` location ignored refusal unit; tty startup file compiled whole |
| 6 | be1b3d09 | REJECT | tty startup comments run as shell commands; if/else split; value echo; invented `:1:1` |
| 7 | fb330b4c | REJECT | positionless refusal at a 1:1 statement printed as exact `:1:1`; syntax-error interrupted line |
| 8 | 71bb0056 | **REJECT** | see below |

### Review 8 findings (71bb0056)

- **MEDIUM (diagnostic only).** Positionless whole-program refusals still print an
  invented exact location on three paths. Exit status 1 and "no part of it ran" are
  correct; side effects are not misrepresented.
  1. `coro-module`: `compile_unit_impl` marks every `check_coroutine_unit` failure
     positioned, but `compiler_coro.cpp` sets `offset = ast.at(root).begin` for this
     whole-unit refusal (e.g. `main.f:1:1` on a comment line).
  2. `import-name-clash`: `module_link.cpp` refuses at the module root start and
     `Stats::positioned` defaults to true (e.g. `lib.f:1:1`, a comment).
  3. import depth limit: `module_graph.cpp` `refuse(index, 0, ...)` (e.g. `m63.f:1:1`;
     the import is at 2:3).
- **LOW.** D-061 point 2 says non-tty startup is unchanged "as in legacy"; piped legacy
  runs earlier statements before failing, the VM runs none. Only the exit status matches.
- **LOW (cosmetic).** The quoted startup statement can include a trailing comment
  (`exit // bye` -> ``(`exit // bye`)``); still stops the file, prompt usable.
- Confirmed: session-path has-position handling; genuine `:1:1` kept; D-061 behaviour on
  both tty startup paths; piped `exit` refused before effect with exit 1 on both paths;
  the six `src/CLI.cpp` lines of 71bb0056 only build the note string and cannot affect
  execution or session cleanup.

## Owner exception (2026-10-31, "Option 1: merge now, fix after")

An owner-authorised exception to the Review 8 REJECT for local weekend integration,
not a retrospective independent acceptance. The strict VM default is functionally
ready to build on.

Review 8 correction (reviewer, after its report): the typed tty REPL path with a
`limit:too many registers` line was not exercised directly (a 200 KB pty line timed
out); persistent tty sessions are covered by vm_default.py §18g.

Open obligations, **release-blocking** for public Runtime 2.0 (not blocking weekend
integration or feature work):

1. Positionless `coro-module` refusals must not report an exact location.
2. Positionless `import-name-clash` refusals must not report an exact location.
3. Positionless import-depth-limit refusals must not report an exact location.
4. Correct D-061's piped-startup legacy-parity wording.
5. Trailing comment in the quoted statement, only if the fix stays small.

These go in a separate narrow candidate on `weekend/r2` using the explicit
has-position contract (never inferred from offsets), with tests distinguishing a
genuine `:1:1` from an unavailable position, a full `test-rt-vm` on its exact tip, and
focused independent review before integration.

## Evidence

- Full `test-rt-vm` passed on `56434245` (3255 s). Not rerun on `71bb0056`; Review 8
  judged the remaining `src/CLI.cpp` change unable to affect execution or cleanup.
- On `71bb0056` (binary confirmed built from the tip): `make nift`,
  test-runtime2-vm-default, cp17-tripwire, cp22, cp19-functional, cp19 session tests
  (24/24), cp25r, cp23, test-cp23-cp19-combined, test-site-dx, test-content,
  test-template-optional, test-template-variables, test-pagination,
  test-markup-json-directives, test-v410-build-pipeline: all exit 0.
- Mutation controls: shell classifier, whole-file compile, double execution, echo,
  1:1 fallback, inferred position, offset default, syntax-error note line: each fails
  the VM-default suite.
- Known limits recorded in `REFUSAL_MATRIX.md` (column drift after quotes in `$[...]`,
  cautious non-tty wording, bare expressions refused, piped `exit` reason ids differ).

## Closure of the Review 8 obligations (2026-11-01)

Obligations 1–4 closed by `cand/diag-positions` `7558346e`, integrated into
`weekend/r2` as merge `8127a515` (independent review ACCEPT; Linode certification:
every gate and full test-rt-vm exit 0; evidence on the NUC under
`.claude-review/linode-jobs/diagpos-7558346e/`). Item 5 (trailing comment in the quoted
startup statement) deferred: needs a quote-aware scanner; recorded in REFUSAL_MATRIX.md.

Tracked from that review (not release-blocking unless noted):
- MEDIUM: `NIFT_VM_STATS` / classify-all rows record 1:1 for positionless refusals (no
  has-position field). Pre-existing; telemetry, not the user diagnostic.
- LOW: several declarations of a clashing name in one module report the last, not the first.
- LOW: whole-line flake checks would miss a leak glued to other output on one line.

## args/cmd integration (2026-11-01)

`cand/args-cmd` `de6e6dde` (independent re-review ACCEPT; two earlier REJECTs fixed) integrated
via `integ/args-cmd`: merge `cbb3de00` (conflicts in `module_link.cpp`/`script_runner.cpp`
resolved; `loop_jump` moved verbatim to `compiler_loops.cpp` for rt-lint), follow-up `9e2f7c84`
(struct/enum-only mention refused at the name: closes the MEDIUM invented-position finding),
`89a4e5dd` (vm-corpus EXPECT: `fallback_args.f` now runs on the VM), `2dbcde8e` (vm_subset
unresolved-name probes use a genuinely unbound name). `weekend/r2` fast-forwarded to `2dbcde8e`.
- Merge-resolution review: ACCEPT (args/cmd behaviour = `de6e6dde`; diag-positions behaviour =
  `8bd59d9f`; flag discipline; byte-identical move; EXPECT legitimate).
- Linode on `2dbcde8e`: make nift, args-cmd, vm-default, full test-rt-vm exit 0 (4177 s), no OOM.
  The earlier `89a4e5dd` run failed only the stale vm_subset probe; every other gate passed there.
- Static audit of stale negative fixtures using args/cmd: none remaining.
- Open (LOW, docs): stale `compiler.cpp` line numbers in four `classification.tsv` rows; stale
  implementation notes in `weekend-args-cmd.md`; write-before-shadow generic wording;
  a module with several `args` mentions may report a later one (real position; fixed by the
  packages ordering rule).

## coro-module integration (2026-11-01)

`cand/coro-module` integrated via `integ/coro-module`: merge `9bc18099` of `9d8f90c6`
(parents `9b2f40a6`, `9d8f90c6`), follow-ups `f6716908` (cooperative refusal claims no
position: test), `32f4797d` (classification location), `b89299b5` (vm_default position witness
moved to the cooperative model; the parallel default now runs that unit). `weekend/r2`
fast-forwarded to `b89299b5`.
- Candidate evidence (identities kept distinct): Review 3 ACCEPT and full Linode test-rt-vm on
  `c30b3138`; inherited CP22 mutant-13 witness fix and cp22-mutants 1-25 on `9d8f90c6`.
- Combined pre-flight: 1,757 fixtures classified against `2dbcde8e`; all 103 outcome changes are
  coro-module's own fixtures.
- Linode on `b89299b5` (first run with the 4-core prebuild): every gate exit 0, cp22-mutants 25/25,
  coro-module and args-cmd mutants, session CLI x2, prebuild 1078 s + full test-rt-vm 1484 s
  (vs ~4177 s serial before), no OOM. Note: a packages job ran for under a minute during this
  run's test-rt-vm before being stopped and re-queued; all results passed.
- Owner ruling: competing capability refusals keep compiler-stage precedence (link-time refusals
  before coroutine admission); deterministic, truthful positions, no effects.
- Open (LOW): `private-async` can point at an unrelated call when a module async function is used
  as a value (real position; whole-unit approximation).

## packages integration (2026-11-01)

`cand/packages` integrated by fast-forwarding `weekend/r2` from `8b3cd02f` to `21360bf8`
(tree `84e83c4a`); `cand/packages` already contained `8b3cd02f`, so the certified tree is the
integrated tree.
- Review history (kept as recorded): Review 1 REJECT (absolute/relative provenance); Review 2
  ACCEPT on `2f37ed36` with two MEDIUM diagnostic-order findings; narrow review of `7f05c811`
  REJECT (F1/F2 order, F3 wrong binding name, F4 docs). Owner policy change (2026-11-01):
  legacy error order and text are not requirements; any genuine error by a documented,
  deterministic compiler-stage order. `21360bf8` removes the order-only machinery and fixes F3;
  revised-contract assessment ACCEPT on `21360bf8`. Interrupted F1/F2 work kept on
  `wip/packages-f1f2` (`1ffd5d99`) as evidence only.
- Linode on `21360bf8`: every gate exit 0, including packages (all three lanes, nift-packages
  present), packages mutants 16/16, args-cmd, coro-module, vm-default, cp22, cp23, cp19 and the
  session CLI x2, prebuild 1089 s + full test-rt-vm 1453 s, no OOM. Earlier `2f37ed36` and
  `7f05c811` runs are diagnostic only.
- Owner rulings on the open findings: C1 (a real import-name collision reported as the
  capability refusal `import-collision`) must be fixed before release; C2 (`export(<non-name>)`
  in code that never runs is a program error) accepted and documented, with no control-flow
  machinery; C3 (an opaque region legacy treats as a runtime error reported as unsupported)
  reviewed before release; C4 (module-initialisation `args` read) remains an approved
  difference; the `genuine()` test helper is improved when the harness is next touched.
- The ACCEPT and certification establish that this candidate meets its agreed checkpoint
  contract and passes its gates; they are not a claim that Runtime 2.0 as a whole is correct.

## Reference semantics Phase A integration (2026-11-01)

`weekend/r2` fast-forwarded from `9ee77096` to `90ab2ef4` (tree `0ab6e16e`), the certified tip
of `integ/refsem-a`. Four identities, kept distinct:
1. **Accepted production source:** `73b327e5` (tree `480a8f5c`). Reviews 1-4 REJECT (kept),
   Review 5 ACCEPT (two LOW, recorded for Phase B). Its standalone Linode run
   (`refsem-73b327e5`) FAILED cp19-functional, cp19-session-cli x2 and test-rt-vm (cp14b
   `splice_throw`); it is historical evidence, not a pass.
2. **Test corrections (no `src/` changes):** `fdcdd0ed`, `104f83cb` on `cand/refsem-a` (CP19
   `unrelated-callback-shadow`; cp14b `splice_throw` per the F2 splice ruling; design mutant 11
   redefined as remove-first; guarded cp14b witnesses preserved); on `integ/refsem-a`
   `5d553e2b` (four args-cmd cases, owner Option 2, guard frozen), `c3704a38` (five review
   LOWs), `2a1c228a` (mutant 5 suspended; REJECTED by independent review, F1: mutant 5 is
   witnessable by classification), `90ab2ef4` (mutant 5 reinstated with the `args.push(args)`
   witness; coordinator-verified on both builds, not a new independent ACCEPT). Preserved
   originals and legacy expectations: `tests/runtime2/refsem/pending/`.
3. **Integration:** merges `afaf3920` (73b327e5) and `0779c1df` (104f83cb) into `9ee77096`;
   follow-ups `99a19d31` (coro-module D-063 renumbered D-064; combined fixtures) and `542f0a65`
   (lint, comments only). Merge-resolution review ACCEPT on `542f0a65` (five LOW, fixed).
4. **Certification:** Linode `integ-refsem-90ab2ef4` on the exact tree `0ab6e16e`: all 43 gates
   exit 0, including refsem/collections/for-s2 sanitize and tsan lanes, every mutant lane
   (args-cmd mutants 1,2,3,5,6 caught; cp14b design 1-11 detected, 12 unverified comparator
   profile), packages (three lanes), coro-module, cp14b, cp19 + session CLI x2, cp22, cp23,
   vm-default, prebuild 1032 s + full test-rt-vm 1592 s; no OOM. The `2a1c228a` run was stopped
   (tree superseded) and is diagnostic only.
- These establish that the candidate meets its agreed checkpoint contract; they are not a claim
  that Runtime 2.0 as a whole is correct.
- Pre-release safety blockers (pre-existing on `9ee77096`, not Phase A): presenting
  `mutex()`/`channel()` values exits 70 after output; `for (a : args) { a.push(1) }` exits 70
  after output. Both must be fixed (correct execution or pre-execution refusal) before release.
- Phase B requirements recorded: remove the reference guard and every pending over-refusal
  (incl. nift-packages/http `capabilities()`, iteration over `args`); privacy-aware struct
  rendering; one validate-commit-bookkeeping mutation protocol; operation-level admission;
  settle the `args` immutability contract (owner preference: the injected `args` array is
  immutable through every alias, not just a non-reassignable binding) and re-witness args-cmd
  mutant 5 through the real const-mutation error.
