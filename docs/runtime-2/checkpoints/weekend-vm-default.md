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

## B0 pre-release safety integration (2026-11-02)

`weekend/r2` fast-forwarded from `33904a6f` to `12781121` (tree `d37aa27a`), `cand/b0-safety`.
- Scope: no admitted program exits 70 because of a runtime-reachable operand kind. Handle
  presentation and wrong-kind mutator receivers are ordinary runtime errors; three more paths
  (`validate()`, session display, session tail) and, after review, mixed-kind interpolation
  (`scalar_to_string`, string append) fixed. About 100 production lines; no compiler or admission
  change; guard, reference semantics and legacy untouched. Record: `b0-safety.md`.
- Reviews (distinct): REJECT `8a9dc923` (F1 mixed-kind interpolation exit 70, found by the
  reviewer's fuzzer); narrow production ACCEPT `fe2ec7fc`; final ACCEPT `12781121` (docs and fuzz
  runner only).
- Root cause (for B4): inference correct ({int, mutex}); interpolation narrowed to scalars
  (`scalar_only`) before a scalar-only native. B4 owns the structural fix and the invariant
  "no runtime-reachable kind causes an internal failure through dispatch", with a mutation test.
- New permanent gates: `test-runtime2-safety` (1,163 cases + fixed-seed fuzz, 33,512 programs,
  five invariant checks with a selftest; fails 134 on `8a9dc923`) and
  `test-runtime2-safety-mutants` (S1-S7). Known limit: the fuzzer's lost-output check is skipped
  for errors in functions defined above the first print (covered by `safety.py` fixtures).
  `expected.json` is a recorded snapshot, not proof of correctness.
- Linode `b0-12781121` on the exact tree: all 33 gates exit 0 (safety + mutants, sanitizer
  prebuild, refsem + mutants, collections, packages three lanes, args-cmd + mutants, coro-module,
  cp14b + design mutants, cp19 + session CLI x2, cp22, cp23, vm-default, full test-rt-vm), no OOM.
  Website build (`benchmark-10k`, same machine, serial): B0 full build median 1.025 s vs
  `33904a6f` 1.047 s; no-op 0.318 vs 0.318 s; single page 0.308 vs 0.329 s; shared template
  1.238 vs 1.293 s. No regression.
- Rendering ruling (owner): mutex/channel keep the ordinary error; struct rendering is B3
  (required); maps/sets/stacks, functions, atomics, futures assessed in B4, presentation separate
  from serialization, no legacy quirks.

## B1 mutation protocol integration (2026-11-03)

`weekend/r2` fast-forwarded from `d209febd` to `7742a21b` (tree `323992fc`), `cand/b1-perf`.
Distinct evidence, all kept:
1. `cand/b1-mutation` `5d4db7fa`/`8975e8eb`: independent ACCEPT (correctness).
2. Linode `b1-8975e8eb`: all correctness gates pass, but same-machine `bench-rt-vm` showed
   `ops/array_push` 382 -> 504 Ir/op (+32%): PERFORMANCE FAILURE; not integrated.
3. `cand/b1-perf` `7742a21b`: attributed (call structure: out-of-line layers, temporaries,
   eager Env; not validation), fixed without a fast path or second implementation; independent
   ACCEPT (no behaviour change over ~36,800 differential runs; 13 mutants caught).
4. Linode `b1p-7742a21b` on the exact tree: all 40 correctness gates exit 0 (incl. full
   test-rt-vm, refsem/collections sanitize + tsan, all mutant lanes). One extra build-only harness
   gate (`.build/rt/vm-run`) exited 1 on both candidate and baseline (harness slip; the helper was
   built through test-rt-vm and for the baseline through bench-rt-vm); it is recorded as failed,
   not as passing.
   Same-machine VM benchmark vs `d209febd` (callgrind Ir/op, VM): `array_push` 382 -> 384 and
   `array_set` 388 -> 413 (both owner-approved residuals; B2 must reach <= 382 / <= 388);
   push_pop 1440 -> 1422, insert_pop 2462 -> 2421, map_set 1788 -> 1472, set_add 1366 -> 1341,
   stack_push_pop 2240 -> 2123; all other ops, fib and phase counts identical; kernels within
   noise. Website build interleaved x3: full build base 1.039/1.053/1.141 s vs B1
   1.101/1.055/1.051 s; no-op 0.323-0.336 vs 0.322-0.342 s: no regression.
- Carried to B2: session receiver loss on a failed array mutator (mandatory gate), exclusive
  protocol, immutability across transfer, map_set throw-safety, frame-owned sort protection,
  VM-level immutability tests, immutable-before-bounds order, perf recovery. Release diagnostic:
  budget-stop message must not say "compiler defect".

## B2 reference representation integration (2026-11-04)

`weekend/r2` fast-forwarded from `919b5669` to `3c962476` (tree `c12612da`), `cand/b2-refrep`
(record `docs/runtime-2/checkpoints/b2-refrep.md`). Arrays and objects are shared references
under the unchanged Phase A guard; one mutation protocol; immutability follows the object;
frame-owned sort protection; fresh aggregate literals. Distinct evidence, all kept (reports on
the NUC under `.claude-review/b2-reviews/`):
1. `94bfce4b`: independent **REJECT** (`REVIEW.md`). H1/H2: JSON-spelled object literals were
   one shared pool constant (loop output `2 3 4` vs legacy `2 2 2`; `same(f(1), f(2))` true);
   M1 mutation-authority bypasses; M2 sort error classified by message text.
2. `534f9af8` (repair): independent **ACCEPT-WITH-CONDITIONS** (`REREVIEW.md`): C1 sort
   storage still reachable, C2 construction lint missed qualified/indirect calls, C3 session row
   of the literal case table tested nothing.
3. `eba6ca1f`: narrow re-check **CONDITIONS MET** (`RECHECK.md`); new LOWs R1-R3.
4. `3c962476`: narrow re-check of R1/R2 **ACCEPT-WITH-CONDITIONS** (`RECHECK2.md`), not
   relabelled as ACCEPT. Owner retained this tree (2026-11-04) subject to a bounded source audit:
   all four conditions PASS (`B2-SOURCE-AUDIT.md`: no production TU includes a test helper,
   directly or transitively, over 583 compile commands / 1,229 dependency files; no production
   use of any demonstrated bypass; no helper symbols or strings in `nift` or `libnift_c.so`;
   no route without changing source).
5. Linode `b2cert-3c962476` on the exact tree (identity verified; `nift` sha256 `3062f229`
   identical to the local build): 54 gates exit 0, including full `test-rt-vm`, every value,
   refsem, collections, CP18 (now links: `67527994`) and CP19-R1 sanitizer lanes, all five TSan
   lanes, all mutant lanes, session CLI x2; no OOM. Failures, every one reproduced on `919b5669`
   on the same machine with the same failure signature (target-by-target comparison, owner
   method 2026-11-04):
   - `test-cp19-r1` (`eval_entry.py`, stale `NIFT_VM=require` expectation) and
     `test-runtime2-cp18` (`combined_cp14b` reference-guard:inflow; `cow_semantics` struct-copy);
   - `test-rt-builtins` (static assert "Host built-ins must stay last");
   - `test-rt-vm-sanitize`: only `test-runtime2-cp14b-sanitize` fails (link: `PackageTransaction`
     missing from its source list, the CP18 defect class); a `make -k` rerun on the exact tree
     ran every other sub-lane and all pass;
   - `make -k test`: 69 failing targets, all 69 also fail on `919b5669` with the same signature
     after normalising temp paths and Makefile line numbers (66 by full-suite comparison, the
     rest by targeted reruns on the base tree). They are historical tests that still expect the
     legacy interpreter under VM default (refusals such as `callable:run`, `text-type-name`,
     `aliasing`, `struct-field-init`) and two `.build/rt` unit binaries with incomplete source
     lists (`shared_atomic_*`). Release-audit items, not B2 regressions.
   Classification: 0 changes over 589 CP25-R units and 1,824 `tests/runtime2` units vs
   `919b5669` (the CP25 half first failed on a harness slip, a missing `.build/rt-parse-dump`,
   and was rerun).
6. Same-machine performance (callgrind Ir/op, VM) vs `d209febd` / `919b5669`: `array_push`
   382 / 384 -> **368** and `array_set` 388 / 413 -> **380** (targets <= 382 / <= 388 met);
   push_pop 1440 -> 1431, insert_pop 2462 -> 2446, map_set 1788 -> 1498, set_add 1366 -> 1365,
   stack_push_pop 2240 -> 2192. Owner-approved residuals: `array_index` 434 -> 436,
   `object_member` 761 -> 762, `string_concat` 3309 -> 3312. Object literal evaluated in a loop
   1,887 -> 4,291: the cost of correct freshness (the base figure was the defect), accepted.
   Website build (site group, same machine): full_2k 0.291 / 0.292 -> 0.283 s, noop_2k
   0.082 / 0.083 -> 0.082 s, one_page 0.083 -> 0.082 s, shared_partial 0.336 / 0.334 -> 0.335 s:
   no regression. `bench-rt-value`, `bench-rt-value-cycle`, `benchmark-10k` pass.
- Corrections to the B2 record's R1/R2 statements (owner 2026-11-04): byte-copying a `SortKey`
  (`__builtin_bit_cast`, `malloc` + copy; `SortKey` is trivially copyable) is well-defined C++,
  not undefined behaviour; a file declaring its own public `SortKey`/`Protocol::sort_storage`
  links statically without diagnostic. The R2 test-helper restriction is a lint, not a
  compile-time boundary, and RECHECK2 bypassed it (direct include of
  `tests/rt/storage_access.h`; annotation accepted anywhere; `markuppp/`, `minifypp/`, nested
  `tests` dirs and some suffixes unscanned). Accepted only because the audited source contains no
  such use.
- `libnift_c.so` exports about 2,355 internal `nift::rt::` symbols with default visibility
  (pre-existing).
- Carried to the tooling checkpoint (approved, before B3, required before release; separately
  reviewable): build-object reuse; compile-time builder type (construction / publication /
  mutation); compile-time isolation of test helpers, rejecting direct and transitive production
  includes; close the SortKey byte-copy route and the duplicate-declaration route where
  structurally possible; annotations only at authorised locations; scan `markuppp/`,
  `minifypp/`, nested `tests` dirs and all relevant suffixes; the seven R3 lint tokenizer cases;
  explicit public symbol visibility for `libnift_c.so` with an exported-symbol inventory test
  against the embedding ABI.
- Carried to B5/B6: L1 in-place comparator sort exposes intermediate state to aliased reads
  (mandatory B5 test; Q7 must cover reads); L2 `cell.cpp` `replace_path` residual take,
  default Env, partial commit (B5/B6); L3 static `c=100` path validation order (B5).
- Invariant (owner): a compiled constant is a description, not a runtime instance; every
  evaluation of a literal creates fresh aggregates, recursively.
- Linode campaign: instance `110032608` deleted (API-confirmed); evidence copied to the NUC
  `linode-jobs/b2-final/` and verified against `SHA256SUMS`; weekend campaign total US$6.05
  (ceiling US$10).

## Tooling checkpoint integration (2026-11-05)

`weekend/r2` fast-forwarded from `c1f43300` to `2175205e` (tree `ca3a5616`), `cand/tooling`
(record `docs/runtime-2/checkpoints/tooling.md`). Six separately reviewed components, no runtime
semantic change: (1) build-object reuse with per-configuration object sets, compiler-identity
signatures and a link-time configuration check on every link and archive; (6) `libnift_c.so`
exports limited to the documented C ABI (5,527 -> 73 symbols, Linux version script); (3)
test-helper isolation (production marker, poison, production-dependency and test-switch
checks); (4) SortKey hardening plus a link-time restricted-symbol allowlist; (2) compile-time
builders (construction vs publication vs mutation) with an owner-held in-place key tier; (5)
lint hardening (phase-2/3 lexer, pinned annotation manifest, repository-wide boundary scan).
`nift` is byte-identical to `c1f43300`'s rebuild except the documented objects; `nift` sha256
`de382d75`; `include/` unchanged. Reports on the NUC under `.claude-review/` (scratchpad
`review-tooling/`). Distinct evidence, all kept:
1. `df7eb54e`: independent review combined ACCEPT-WITH-CONDITIONS; lint component REJECT
   (F1: renamed construction primitives escaped the lint; a published array was emptied in a
   release build). F2-F13 further findings.
2. `5e156911`: re-review ACCEPT-WITH-CONDITIONS (N1 weak references escaped the symbol check,
   N2-N9). `19395b98`: check CONDITIONS MET with C1-C7 (C1 a regression in the new dependency
   parser). `4e97e34e`: check CONDITIONS MET.
3. Linode `tcert-4e97e34e`: **FAILED** (packages/args-cmd mutant harnesses saw a stale `nift`;
   cp14b design mutant anchor broken by the builder migration; `make -k test` left 59 in-tree
   objects under another configuration). Fixed in `fcb011c3` (re-review ACCEPT-WITH-CONDITIONS
   N1-N4) and `e6d21fa4` (check CONDITIONS MET).
4. Linode `tcert-e6d21fa4` on the exact tree: 59 gates exit 0 (incl. full test-rt-vm with every
   mutant lane, all sanitizer and TSan lanes), `make nift` after `make -k test` relinks the same
   `nift`, isolation and sort-forgery pass, tree unchanged; every `make test` failure matches the
   B2 certification signature (69 targets); known pre-existing failures only (cp19-r1, cp18,
   embed's cp18-switch, rt-builtins, cp14b-sanitize link, bindings: go/dotnet/node absent on the
   host, Python source list). Perf vs `d209febd` / `c1f43300` (callgrind Ir/op): array_push
   382 / 368 -> 367, array_set 388 / 380 -> 381, all mutation workloads at or below `d209febd`,
   the three approved residuals unchanged; site builds within noise; classify 0 changes
   (589 + 1,824). **test-build-reuse FAILED** three checks (define, generated, distclean).
5. `2175205e` (class A, test-harness only: `scripts/build/check-build-reuse.sh` and the record):
   define row used a forbidden test-only define (now a permitted define plus a separate
   negative control), a failed row left configuration B in place (now restored; the generated
   row rebuilds exactly ParserExpression.o), distclean deleted its own logs (now kept outside
   the tree). Independent diff review CHECK5 ACCEPT-WITH-CONDITIONS.
6. Linode `tgt-2175205e`, targeted certification approved by the owner for a class-A diff:
   `e6d21fa4` and `2175205e` built side by side on one machine: 587 production objects,
   `nift` (`de382d75`), `libnift_c.so` (`464c144a`), `libnift_c.a` (`31976548`) and the 73
   exported symbols byte-identical; test-rt-lint, protocol-compile-fail, test-rt-build,
   test-c-abi-symbols, test-production-deps, test-rt-isolation, test-rt-sort-forgery and the
   full test-build-reuse (every check PASS, incl. define-forbidden, generated = 1 object,
   clean and distclean) exit 0; tree unchanged, `make -q nift` 0.
   **Reused evidence:** the item 4 results (59 gates, VM, sanitizers, make test comparison,
   perf, classify) were produced on `e6d21fa4`, not on `2175205e`; they carry over only
   because the production artifacts are proven identical and the diff is test-harness only.
- Record corrections: CHECK5 C1, the tooling.md local-run note cites `build-reuse-out/` from
  the reverted `4c6b2bea`; at `0110c8b8` the logs are `.build/build-reuse`; only the Linode
  run counts. CHECK4 wording: the "make-q-nift gate" is the implementer's local gate script;
  the real checks are the harnesses' trailing `make -q` calls.
- Carried (not fixed, owner 2026-11-05): CHECK5 F1-F4 harness robustness (unchecked mktemp,
  copy failures not counted, unchecked restore build, one-second mtime comparison); remaining
  deliberate-source routes recorded in tooling.md's layer table; raw object-layout access
  (`heap_as<...>()->items`) lint-only; macOS/Windows export lists; binding builds outside the
  Makefile lack the production guard; Python binding source list (release blocker).
- Linode: instances deleted (API-confirmed); evidence on the NUC under `linode-jobs/`
  (`tooling-final/`, `tooling-recert/`, `tooling-targeted/`) verified against SHA256SUMS;
  weekend campaign total US$7.78 (ceiling US$10).
- Next (owner 2026-11-04/05): B3, B4, B5 sequentially with independent reviews, one combined
  certification; no further infrastructure work before B3.
