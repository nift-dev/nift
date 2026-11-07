# Combined B3-B5 candidate: H-A repair and certification wiring

Candidate base: `cand/b3-b5-combined` at `fc987ec1` (tree `9e94a182`): weekend/r2 `0b5079fc`, then
B3, B4a/B4b (`17f7964c`) and B5. The independent combined review
(`scratchpad/review-combined/REVIEW-COMBINED.md`) returned READY-WITH-CONDITIONS with H-A (receiver
evaluation order), H-B and H-C (certification inputs), M-1 to M-3. The owner's 18:00 decisions
(2026-11-07): D1 fix H-A before certification; D2 fix the harness; D3 an explicit v4.12.0 oracle;
D4 release-blocker additions; D5 M-3 deferred. This record covers exactly those repairs. The
B3, B4, B4b and B5 records are unchanged.

Commits on top of `fc987ec1`: C1 (H-A repair and tests), C2 (harness C1-C3), C3 (oracle
provenance in the preflight), C4 (recipe lines run on their own after a failure), C5 (scanner
parity's reference binary), C6 (this record, compatibility.md, certification.md). Nothing pushed
or tagged.

## 1. H-A: the defect

For a binding held in the main frame (every top-level binding: functions and lambdas reach it as a
global), a session binding, a top-level binding used inside a function or lambda, and some
captured cells, the compiler read the receiver **after** evaluating the arguments (or, for an
index read, used the binding's own register, which a call can overwrite). An argument that rebinds
the receiver's name therefore redirected the operation to the new value:

`b := [1]; c := b; g := () => { b = [7]; return 4 }; b.push(g())` gave `b = [7,4]`, `c = [1]`
(legacy: `b = [7]`, `c = [1,4]`).

Sites: `compiler_array_mutation.cpp` (push, insert, remove, splice as expressions), the statement
`push` in `compiler_stmt_expr.cpp` (late re-read after the operand), `compiler_keyed.cpp` (map
set/remove, set add/remove, stack push: arguments evaluated first by design), the element store in
`compiler_assign.cpp` (receiver re-read after the value) and the index read in
`compiler_call.cpp` (the binding's own register used across the key). Pre-existing since at least
weekend/r2 for unaliased programs; B5 admitted the aliased forms.

## 2. The ordering contract, per operation

Established by running each form on legacy (`NIFT_VM=0`, the same legacy engine as v4.12.0), the
accepted B4b `17f7964c` and `fc987ec1`, in every context (top level, a while/for body, a function's
captured local, a top-level binding used in a function and in a lambda, a session), with a lambda
and a `fn` callee. Probes and logs: `scratchpad/combined-fix/p1`..`p9`, `p*-before.txt`,
`p*-after.txt`. Receiver, index and value order were checked separately.

| Operation | Receiver vs operands (legacy canonical evaluator) | Order among operands (legacy) | fc987ec1 | After the repair |
| --- | --- | --- | --- | --- |
| `b.push(v)` (statement and expression) | receiver first (lambda or `fn` callee; top level, function, lambda, session) | - | receiver late except a function's captured local (statement form) | receiver first, retained |
| `b.insert(i, v)` | receiver first | `i`, then `v` | late | first |
| `b.remove(i)` | receiver first | - | late | first |
| `b.splice(s, n[, r])` | receiver first | left to right; one commit (F2) | late (3-argument form with a call refused, `method:splice`) | first |
| `m.set(k, v)`, `m.remove(k)`, `s.add(v)`, `s.remove(v)`, `st.push(v)` | receiver first | `k`, then `v` | late everywhere | first |
| `b[i] = v` (array and object element store) | receiver first with a lambda callee, in sessions and function cells; **late when `v` calls a `fn`**, and in loops (legacy inconsistency, section 2.1) | **`v`, then `i`**, in every legacy context (`b[i] = g()` where `g` changes `i` stores at the new `i`) | receiver late (main frame, session, global); `i` before `v` where `i` is loaded (global, cell, session) | receiver first; `v` then `i` |
| `b[k]`, `o[k]`, `b[k].m()` reads | receiver first everywhere (loops too) | - | late for main-frame bindings | first |
| nested `b[i][j]` read | the inner container before `i` | `i`, then `j` | late on `b[g()][j]` at top level | first |
| compound `b[i] += v`, `b[i]++` | legacy: receiver first (lambda), late (`fn`) | - | refused (`compound-target`) | unchanged (refused) |
| comparator `b.sort(c)` | receiver first | - | refused here (`method:sort`, toolchain gate); the probe build reads the receiver at `CollectionSort` entry and refuses an effectful callback | unchanged |
| struct `p.x = g()`, `p.m(g())` | legacy: error / receiver first | - | refused (`struct-assign`) | unchanged |
| nested receivers `a[0].push(x)` | B6 (approved scope, not implemented) | - | refused | unchanged |
| session statements | as the operations above | | late | first |

Atomicity: a failed container mutation leaves the receiver unchanged (bounds, refused cycle,
throwing argument); an argument's side effects (here: the rebinding) remain. Checked by
`atomic_*` fixtures (catchable cycle errors, and fatal bounds errors observed in sessions).

### 2.1 Legacy behaviour the contract does not follow (APPROVED as differences, owner 2026-11-07 22:13 D1; recorded in evidence/b5/expected-changes.tsv as class iii; not legacy parity)

Legacy is not consistent, so no single order matches every legacy result. The repair follows
legacy's canonical evaluator and the approved reference semantics (plan section 3/5.3: the
receiver and every operand are evaluated before the mutation; semantics.md: operands left to
right; owner 18:00: "receiver evaluated and RETAINED ... before arguments/index expressions").
These legacy results are therefore no longer reproduced; each is a previously admitted,
legacy-equal program whose output changes:

1. **Element store with a `fn` callee.** `b := [1, 2]; fn(g()) { b = [7, 8]; return 4 }; b[0] = g()`:
   legacy `[4,8]` (it resolves `b` after a value that calls a `fn`, but before one that calls a
   lambda: e04 gives `[7,8]`, `[4,2]`); fc987ec1, 17f7964c, B3 and weekend/r2 gave `[4,8]`; the
   repair gives `[7,8]`. Fixture `evalorder/legacy_fn_store.f`.
2. **Legacy's prepared loop evaluator.** In loop code legacy reads the receiver late for `push`,
   map `set`, set `add` and element stores (`[7,4]`, `[1]`), and **loses the mutation** for
   `insert`, `remove`, `splice` and stack `push` (neither array changes). fc987ec1 matched the
   late reads by accident and mismatched the rest; the repair uses the canonical order in loops.
   Previously admitted, legacy-equal and now different (no alias kept): loop `push`, `m.set`,
   `s.add`, element store, with either callee (`scratchpad/combined-fix/p9.json`). Fixture
   `evalorder/legacy_loop_order.f`.

3. **A loop declaration's index read** (review of 0d853db4 M1): `r := b[g()]` in loop code with
   `g` rebinding `b`: legacy 8 and 17f7964c 8, now 2. Fixture `evalorder/legacy_loop_decl_read.f`.

Also observed, not changed by the repair (the VM already differed and is correct):
4. legacy evaluates an index key **twice** in a declaration's initialiser (`r := b[g()]` calls `g`
   twice); the VM evaluates it once (semantics.md: operands once each);
5. legacy fails (`remove: index out of range`) when the argument drops the receiver's last other
   reference; the VM keeps the retained receiver alive.

### 2.2 Same root cause outside H-A (STOP: not fixed; RELEASE BLOCKER, owner 2026-11-07 22:13 D2, bounded investigation to be briefed separately)

The same main-frame-register pattern affects operators, which are not receiver operations:
`x := 1; fn(g()) { x = 10; return 1 }; print(x + g())` prints 11 (legacy 2; semantics.md requires
left to right), also `-`, `==`, string `+`. And compound assignment to a scalar binding reads the
target **after** its right-hand side in every context, including function cells
(`x += g()`: 11, legacy 2; `compiler_assign.cpp` compound_op, "after the right-hand side, which
may read it"). Pre-existing on 17f7964c. Not fixed here: binary operators are outside H-A's
receiver scope, and retaining a compound target's value before the right-hand side would defeat
the in-place `s += t` append (B2 global_take, linear string building) and needs a new mechanism.
Probes: `scratchpad/combined-fix/p8/b*.f`, `c*.f`. Owner decision: scope and approach.

## 3. The fix

The receiver is evaluated first and its value is held in a register (a counted reference, so its
lifetime covers the operation) before any argument, index or value is evaluated. Saving the
variable's location and reloading it later is not used anywhere:

- `compiler_array_mutation.cpp`, `compiler_keyed.cpp`: the receiver's read into `state+Source` (a
  register copy) moves before the arguments.
- `compiler_stmt_expr.cpp` (statement `push`), `compiler_assign.cpp` (element store): a global or
  session receiver is read before the operand (the late re-read is gone); a receiver in its
  binding's own register is copied to a temporary when the operand can rebind it
  (`retain_receiver`). The element store evaluates its value, then its index.
- `compiler_call.cpp` (index read): the same copy when the key can rebind the container.
- `compiler_literal_expr.cpp`: `may_rebind(operand)` (a call, assignment, compound assignment,
  increment or await anywhere in the operand) and `retain_receiver`.

Diff (production sources, `git diff fc987ec1 C1 -- src`): 7 files, +88/-12 (corrected; the first
version of this record said -11), of which about 35 executable lines; the rest are comments and
the two mutant guards. No VM, protocol or runtime change.

**Correction (review of 0d853db4, H1).** This record first said that a captured loop binding
still writes its retained receiver back and that programs whose argument rebinds the loop binding
are refused (`for-capture-vanishing`). That was false: the refusal concerns a loop source that
the body may shrink, and a literal source is admitted. The write-back undid the argument's
rebinding (section 10).

Cost (deterministic, callgrind Ir, no wall clock): a top-level loop of 200k `b.push(f(i))`, 100k
`m.set(j, f(j))` and 200k `s += b[f(k)]`: 510,963,703 -> 532,963,843 (+4.3%: one `Move` and its
reference count per operation whose operand calls); the same loops with plain operands
(`b.push(i)`, `b[0] = i`, `s += b[k]`): 217,252,957 -> 217,253,226 (unchanged). Keyed operations
only reorder (no added instruction).

Binary: `nift` sha256 `ac283072f622c473b117d76d4d2e4397606743a8804bee5c0fa1565145ac05b7` (fc987ec1,
rebuilt here identically) -> **`9e9f446631cf9f313ac615e2aa711a692fc45873bc75b9db876d737655ed0f00`**
(GCC 13.3.0, `__GLIBCXX__` 20240904; the Linode GCC 15.2 hash will differ).

## 4. Tests

- `tests/runtime2/b5/evalorder/` (42 fixtures, run by `test-runtime2-b5` on the production and the
  guard-off build): the review's reproducers e01-e16; every operation with a rebinding argument
  (`op_*`); value-before-index (`order_*`, script and session); alias mutation; atomicity
  (`atomic_*`: refused cycles, throwing arguments, bounds errors observed in sessions); lifetime
  (`life_*`: the argument rebinds the name and the alias, so the retained receiver is the only
  reference; the removed or read element is the original's, `same()` with the element kept
  elsewhere); functions and closures (`fn_*`: a function's captured locals were already right for
  the statement push and wrong for the others; a top-level binding used in a function or lambda;
  a captured parameter); sessions (`e16`, `sess_ops`, `life_session`, `atomic_*_range`); the two
  PENDING-OWNER legacy differences.
- Comparison (`scratchpad/combined-fix/fixtures-3way.txt`): every fixture is legacy-equal except
  the approved Phase B differences (cycle refusal, argument sharing, extraction identity) and
  the two PENDING-OWNER rows; fc987ec1 differs from the golden on 33 of 42; 17f7964c refuses most
  (the guard) and differs on every admitted rebinding case (e01, e02, e05, e11, e14, e15,
  `op_map_key`, `op_nested_read`, `op_stack_push`, `order_store_value_index*`).
- Evaluation-order family (`tests/runtime2/b5/gen/gen_evalorder.py`, inputs
  `tests/runtime2/b5/gen/evalorder.tsv`, target `test-runtime2-b5-evalorder`, part of
  `test-rt-vm`): 14 operations x 5 argument behaviours (none, rebind, alias, drop, throw) x 2
  callees x 6 contexts = 770 programs, each compared with the contract model; legacy must agree
  with the model outside the four named classes of 2.1 (that checks the model against the
  oracle). Repair: PASS, 760 admitted (floor 760), 0 bad; legacy differs only in loop 67,
  fn-store 8, decl-twice 9, lost 3. fc987ec1: 277 bad; 17f7964c: 127 bad of 312 admitted.
  Negative controls (missing tree, truncated table): exit 2, nothing written.
- Mutants (`tests/runtime2/b5/mutants.sh`, `test-runtime2-b5-mutants`): **13** restores the late
  receiver reads (array, keyed, statement push, element store, and index before value): caught,
  34 failing fixtures, generator 269 bad; **14** never copies a binding's own register
  (top-level forms read late): caught, 16 failing fixtures, generator 24 bad. All B5 mutants
  caught.
- ASan: **not run locally; deferred to the Linode sanitizer lane.** The sanitizer build of `nift`
  was started and stopped at 112 of 303 objects (541 MB; the projection exceeded the 1.2 GB left);
  its output was deleted. New target `test-runtime2-b5-evalorder-sanitize` (a prerequisite of
  `test-rt-vm-sanitize`, mandatory in the certification plan) runs the evalorder fixtures
  (`life_*`, `atomic_*`, all others) and the generator (its `drop` family is the lifetime case) on
  the ASan/UBSan build with `halt_on_error`; `tests/runtime2/b5/run.py` and `genrun.py` now pass the
  sanitizer options through and treat a sanitizer report as an internal failure.

## 5. Gates rerun after the compiler change

Logs: `scratchpad/combined-fix/gates/` (`results.txt`: rc, seconds, HEAD, `git status` hash before
and after, free disk). Results are in section 9.

## 6. Certification harness (C1-C3)

`scripts/certify.py` and `tests/runtime2/certification_guard.sh`:

- **C1 inventory.** The plan is `certify-targets` plus the Runtime 2.0 inventory derived from the
  Make database (the B5 reconciliation's naming rule; 248 targets now: the 244, `test-cp19-m12-reachable`,
  and the three new gates), minus two named non-gates (the S1 `--prepare` maintenance targets).
  32 owner-named B3-B5 and combined gates are MANDATORY. `certify.py --check-plan` fails when an
  inventory target is neither planned nor a named non-gate, a non-gate is stale, or a mandatory
  gate is missing. Negative control in the guard: `--drop` of each of `test-runtime2-b5`,
  `-b4b-fixpoint`, `test-cp19-proof-cli`, `test-runtime2-diagnostics` fails preparation. 49 of
  the review's 51 unreached targets are planned; the other two are the non-gates.
- **C2 no masking.** Umbrellas are expanded: each gate prerequisite is its own entry and the
  umbrella's own recipe runs last with its gate prerequisites marked old (`make -k T -o P ...`);
  every entry runs with `make -k`. Entries that were not `certify-targets` leaves run serially
  (they ran serially inside their umbrella before; isolation not audited). Plan: 404 entries
  (212 parallel, 192 serial). Report classes PASSED / FAILED-NEW / FAILED-BASELINE / BLOCKED /
  NOT-RUN; only PASSED is a pass; FLAKY counts as FAILED-NEW. Baselines are kept apart: v4.12.0
  (`--baseline`, legacy compatibility), regression baselines (`--regression-baseline
  17f7964c=TREE --regression-baseline weekend/r2=TREE`; identical = same failing step and the same
  normalised FAIL lines), and the approved differences (the B5 expected-output table, checked by
  the gates, not by the runner). Exit 1 on FAILED-NEW or a comparator SKIP, 3 on BLOCKED/NOT-RUN,
  0 only when every required gate is PASSED or FAILED-BASELINE.
- **C3 comparator and oracle preflight.** Before anything else is built, after `make nift`: GCC
  15.2 (not clang), `__GLIBCXX__ == 20260321`, a comparator sort runs on the production VM,
  `NIFT_CP14B_REQUIRE_COMPARATOR=1`, and the v4.12.0 legacy oracle (`--legacy-oracle` or
  `CP19_LEGACY_ORACLE`; it must report v4.12.0 AND be the `nift` of a clean checkout at
  `ce8cd36a` whose `make -q nift` is current: the version string alone does not identify it,
  since Runtime 2.0 builds still report v4.12.0). A full campaign with any problem exits 2 before
  running a gate. A gate log with `SKIP UNVERIFIED COMPARATOR COVERAGE` is BLOCKED and fails the
  campaign; a log showing a missing oracle is BLOCKED. Table tests (32 cases) in the guard.
- **gen_alias.** `test-runtime2-b4b-gen-alias`: all 12,600 programs on the production build,
  `--min-vm 5532`; negative controls (a stub that exits 70, a stub that refuses everything) must
  fail.
- **CP19 oracle.** Build on the instance from the frozen tag (section 7).

## 7. Building the v4.12.0 legacy oracle

Tag `v4.12.0` (annotated) = commit `ce8cd36a92ecfdc8b15b00df41a6c3bb9a31c0eb`. On the instance:

    git -C <repo> rev-parse 'v4.12.0^{commit}'         # must print ce8cd36a92ec...
    git -C <repo> worktree add --detach ../nift-v4.12.0 ce8cd36a92ecfdc8b15b00df41a6c3bb9a31c0eb
    make -C ../nift-v4.12.0 -j"$(nproc)" nift
    ../nift-v4.12.0/nift --version                       # Nift v4.12.0
    sha256sum ../nift-v4.12.0/nift                       # record in the campaign evidence
    python3 scripts/certify.py --legacy-oracle "$PWD/../nift-v4.12.0/nift" \
        --baseline ../nift-v4.12.0 \
        --regression-baseline 17f7964c=../nift-17f7964c --regression-baseline weekend/r2=../nift-0b5079fc

The same tree serves as the v4.12.0 baseline. Without the oracle, `test-cp19-proof-cli` exits 2
and the campaign preflight fails.

## 8. Release-blocker list (updated)

Carried (B5 close, 2026-11-07): D4 four session regressions (shared-callable, field, element,
sibling-effects) with the group audit, and m3; D2 comparator-sort portability; F4
comparator-lambda error positions; m10; pre-existing failures (the 37 inventory FAILs and the
`make test` components failing identically on weekend/r2, B3, 17f7964c, fc987ec1);
cp01-simplify FLAKY; `--json` function-tag leakage; Python binding source list; macOS/Windows
export lists and cross-platform GitHub Actions qualification; CHECK5 F1-F4; `atomic.store`
text; CP16 `fs_symlink` ordering; gen1 pre-existing differences. H-1 production exit 70: fixed
(confirmed by the combined review).

Added (owner 2026-11-07 18:00 D4/D5):
- **B6**, existing approved scope only (plan section 6, owner Q6): nested-receiver mutation
  (`a[0].push(x)`, `o["k"].push(x)`, `m.get(k).push(x)`) and fresh array/object struct-field
  initialisers (`struct(C){ l := [] }`); both refused on fc987ec1 and here.
- **B4b `glob16` admission limitation** (compatibility.md: `glob16`/`glob100` refused
  `builtin-receiver:trim`, admitted by B4a).
- **VM-default carried items**, each on its own: (a) binding builds outside the Makefile
  compile production sources without the production guard; (b) raw object-layout access
  (`heap_as<...>()->items`) is enforced by lint only; (c) the remaining deliberate-source routes
  of tooling.md's layer table.
- **H-A**: the defect being fixed before certification; **fixed here pending independent
  verification** (the first repair was REJECTED for H1, section 10); kept in this history after
  resolution. Its legacy differences in 2.1 (items 1-3) are APPROVED (owner 2026-11-07 22:13 D1).
- **Operand order of top-level bindings outside receivers** (review of 0d853db4 M2; 2.2): `x + g()`,
  `x == g()`, `s + k()`, `x += g()` (every context), `[x, g()]`. RELEASE BLOCKER (owner 2026-11-07
  22:13 D2); not started in this round; a bounded investigation will be briefed separately.
- **M-3** `for-capture-vanishing` / `for-missing-write`: deferred precision limitation (safely
  refused before any effect, not previously admitted: 17f7964c refuses them too). Fixtures in
  tree, run by `test-runtime2-b5`: `tests/runtime2/b5/precision/m3_nested_loop_shared.f` (the
  review's `probes3/d05`, legacy `[[1,7],[2]]`) and `m3_closure_array.f` (`probes/c02`, legacy
  `[[1,9,8],[2,9]]`); their goldens pin the refusal, so a future admission is a visible change.

## 9. Gate results at 0d853db4 (binary 9e9f4466, GCC 13.3; local, not certification)

Run one at a time with `make -k`; `git status` unchanged by every gate.

| Gate | Result |
| --- | --- |
| test-runtime2-b5 (production + guard-off; 117 / 122 fixtures incl. evalorder, precision; sort-refusals 564) | PASS |
| test-runtime2-b5-mutants (1, 3, 4, 6, 7, 8, 11, 12, 13, 14) | PASS, all caught |
| test-runtime2-b5-generators, test-runtime2-b5-evalorder (770; negative controls) | PASS |
| test-runtime2-b4b-gen-alias (12,600 programs, 5,532 on the VM, 0 bad; negative controls) | PASS |
| test-runtime2-b4b, -b4b-work, test-rt-b4b-types-unit, -b4b-fixpoint, -b4b-mutants | PASS |
| test-runtime2-guardoff, test-runtime2-cp12c-probes, test-runtime2-cp23, test-runtime2-rt-diff, test-runtime2-cp16-engine-parity, test-rt-lint, test-runtime2-certification-guard | PASS |
| test-runtime2-rt-diff-selftest | FAIL, identical FAIL lines on the fc987ec1 binary (FAILED-BASELINE) |
| test-runtime2-scanner-parity | BLOCKED here: needs NIFT_REF (the campaign passes the v4.12.0 oracle) |
| `make -k test-rt-vm` (32.6 min) | every prerequisite after `test-runtime2-cp14b` ran: only cp14b failed (`script_return_kernel_error`, known pre-existing); b3, b3-mutants, b4, b4-mutants, refsem(-mutants), collections(-mutants), cp22, coro-module, cp17, d040, capture-origins, for-s2, closures-s1, safety(-mutants), vm-default, args-cmd, packages, b5-evalorder, b5-snapshot-memory all PASS |
| test-rt-vm own recipe (`make -k test-rt-vm -o <its 45 gate prerequisites>`) | stops at vm_parity `--rt-diff`: 1 failure, "30 differing, 16 approved", identical on fc987ec1 (FAILED-BASELINE). The 28 later lines run one by one: all PASS (vm_adversarial seed 1 count 600, vm_subset(_gen), vm_builtins, vm_records_coverage, vm_engine_selection, vm_former_replay, vm_review_cp12, ...) except vm_modules (2 cases `import-scope:collision`), identical on fc987ec1 (FAILED-BASELINE) |
| `scripts/certify.py --check-plan`; `--drop` negative controls; `--list` dry run (404 entries) | PASS; drops detected; plan listed with preflight |
| `scripts/certify.py` full campaign here | exit 2 at preflight before any gate (GCC 13.3, `__GLIBCXX__` 20240904, comparator refused, switch unset, no oracle): the C3 negative control |

Sanitizer, TSan, wall-clock, cross-platform and comparator (GCC 15.2) lanes: Linode, including the
new `test-runtime2-b5-evalorder-sanitize`.

## 10. Independent review of 0d853db4: REJECT; repair round (C7-C9)

Review: `scratchpad/review-ha/REVIEW-HA.md` (repros in `review-ha/repro/`). Verdict REJECT with
two High findings; the core receiver-first change was confirmed sound for every other receiver
kind.

### H1 (fixed, C7): a captured loop binding's write-back undid the argument's rebinding

`for (b : [[1],[2]]) { g := () => { b = [7]; return 4 }; b.push(g()); print(b.stringify()) }`
printed `[1,4] [2,4]` (legacy and 17f7964c `[7,4] [7,4]`; contract `[7] [7]`: the retained
element gets the push, the rebinding stays). Cause: after reading the receiver first, `put()`
(`compiler_array_mutation.cpp`) stored the retained receiver back into the loop binding's cell;
the element store (`compiler_assign.cpp`) and the statement push's `location_source` path
(`compiler_stmt_expr.cpp`) had the same write-back (the element store was wrong before H-A too:
17f7964c `[5,2]`, legacy and contract `[7,8]`).

Fix: no write-back when an operand `may_rebind` (the array was mutated in place; writing back
the same reference was a no-op otherwise, and that unchanged path is kept). Production delta in
C7: 3 files, +17/-6, about 6 executable lines (the rest is the M15 guard and comments). Results:
push `[7] [7]`, insert/remove `[7,8] [7,8]` (legacy-equal), store `[7,8]` (legacy-equal), the
expression form in a function and the session form as the contract; keyed operations were
already correct (no write-back).

Tests: fixtures `evalorder/loopbind_{push,insert_remove,store,fn_expr}.f`, `loopbind_session.in`;
generator context `loopbind` (70 more programs, 840 in all, 830 admitted, 0 bad); mutant **15**
(the reviewer's mE: a captured loop binding read late again) in `b5/mutants.sh`, caught (section
10.4). Binary: **`91a49efa43715613ff96c6aae31b0844ff4b192e323addd3e64b1b6e677fefcf`**.

Section 2's contract table, row push/insert/remove/store, applies to captured loop bindings too;
legacy's prepared loop evaluator differs there as recorded in 2.1 item 2.

### H2 (fixed, C8): the runner could launder a new failure into FAILED-BASELINE

- (a) The per-line results of a failing multi-line recipe now decide the class: a baseline
  matches only if every recipe line that fails on its own on the candidate fails identically
  (same command, same failure identity) when the same lines run on that baseline; otherwise the
  gate is FAILED-NEW. The reason text is appended to, never overwritten.
- (b) Failure identity (`failure_identity`): the failing step, the exit statuses and signals
  (make's `Error N`, signal texts), and the normalised FAIL lines; when neither log has a FAIL
  line, their normalised last lines. Two logs with nothing to compare never match.
- (c) The v4.12.0 baseline decision (`decide()`) also requires that identity
  (`base_identical`); the same step alone is FAIL.
- Guard: toy-Makefile negative controls run `scripts/certify.py` on candidate and baseline toy
  trees: (a) a later line failing only on the candidate, (b) a crash against an assertion with no
  FAIL lines, (c) different FAIL lines at the same v4.12.0 step: each FAILED-NEW (exit 1); a
  positive control (identical failure) is FAILED-BASELINE (exit 0). The 0d853db4 `certify.py`
  fails control (a) (FAILED-BASELINE, exit 0). Table cases: 37 harness, 31 classification.

### M1 (recorded; fixture and compatibility row; APPROVED 2026-11-07 22:13, owner D1.3)

`r := b[g()]` in loop code (`g` rebinds `b`): legacy 8 and 17f7964c 8 (legacy evaluates the key
twice; 17f7964c read late), now 2 (operands once, receiver first). A previously admitted program
whose output changes: added to 2.1 as item 3 (`evalorder/legacy_loop_decl_read.f`;
compatibility.md). The generator now cross-checks loop programs against legacy where legacy's
prepared evaluator is meaningful (no argument effect: must agree, and does); the other loop
programs are reported under named classes: `loop` (rebind/drop/throw), `loop-twice` (legacy
evaluates an alias-mutating argument twice for insert, splice, stack push), `loop-expr` (a loop
declaration: `r := b.push(g())` gives legacy "cannot mutate const array", `r := b[g()]` evaluates
the key twice).

### M2 (recorded as an open release blocker, not fixed; owner scope decision)

Operand order of top-level (main-frame) bindings outside receivers: `x + g()` (11, legacy 2),
`x == g()` (false, legacy true), `s + k()` (`Zb`, legacy `ab`), compound `x += g()` in every
context including function cells and loops (11, legacy 2), and `[x, g()]` (`[10,1]` on the VM
and on legacy, against semantics.md's left-to-right rule). Silent wrong output in admitted
programs under the VM default; identical on 17f7964c (not a regression). Added to section 8 as a
release blocker pending the owner's scope decision (2.2).

### L1-L4

- L1 (recorded): the oracle provenance check (commit, clean checkout, `make -q nift`) catches
  accidental misuse but not a deliberate copy of a newer binary over a clean `ce8cd36a` checkout
  (`make -q` compares timestamps; the binary is untracked). Recording and checking the oracle's
  sha256 would close it.
- L2 (recorded, known harness limitation pending a decision): a gate that exits 0 after printing
  a SKIP other than the known patterns counts as PASSED (e.g. `tests/runtime2/read_limit.py`
  prints `SKIP default 1 GiB limit` on hosts with less than 4 GiB available, which includes small
  Linodes). Partially skipped gates are therefore not distinguished from passed ones.
- L3 (recorded): `run_lines` runs `make -n` output through bash without the target-specific
  exported variables (e.g. `PYTHONDONTWRITEBYTECODE`), and its line numbers include prerequisite
  build commands; it affects the line diagnostics and, since C8, the line comparison (both trees
  are run the same way).
- L4 (fixed, C7): the diff count (+88/-12); the generator's truncated-table negative control now
  reads the table from `--tree`.

### 10.4 Gates rerun (binary 91a49efa)

Separate `make -k` runs (`scratchpad/combined-fix/gates/results2.txt`); the tree changed during
the run only by this record's edits.

| Class | Gates |
| --- | --- |
| PASSED | test-rt-lint; test-runtime2-b5-evalorder (840 programs, 830 admitted, 0 bad; negative controls); test-runtime2-b5 (123 production / 128 guard-off fixtures; sort-refusals 564); test-runtime2-b5-mutants (1, 3, 4, 6, 7, 8, 11, 12, 13, 14, 15 all caught: M13 38 failing checks, M14 22, M15 5); test-runtime2-b5-generators; test-runtime2-certification-guard (plan check, drop controls, 37 + 31 table cases, four toy controls); test-runtime2-b4b-gen-alias; test-runtime2-b4b; test-runtime2-refsem; test-runtime2-cp22; test-runtime2-capture-origins; test-runtime2-cp12c-probes; test-runtime2-b5-snapshot-memory |
| FAILED-NEW | none |
| FAILED-BASELINE, BLOCKED, NOT-RUN | none among the rerun gates (section 9's results for the others stand: the C7 change touches only the loop-binding and `location_source` write-backs) |

M15 on the generator: 12 bad (caught). The 0d853db4 `certify.py` fails toy control (a).

## 11. Confirmation round on c73a7692: CONFIRMED-WITH-CONDITIONS; cleanup (C10-C12)

Review: `scratchpad/review-ha/CONFIRM-HA-R2.md`. H1, H2(a)-(c), M1, M2 and L1-L4 confirmed; one
condition (F1) and three cleanups. No production change in this round: the binary stays
`91a49efa43715613ff96c6aae31b0844ff4b192e323addd3e64b1b6e677fefcf`.

- **F1 (fixed, C10).** `failure_identity` looked only at the FAIL lines when there were any, so
  `FAIL suite` with `bad case: case-NEW` matched `FAIL suite` with `bad case: case-old`, and a
  vm_parity run with `31 differing` would have matched the baseline's `30 differing` (its FAIL line
  is only `FAIL vm parity: 1 failure(s)`). The identity now always includes the normalised last 15
  lines (detail and summary lines) as well as the FAIL lines, all through one `normalise()`:
  checkout roots, `/tmp` paths, timestamps, clock times, durations (`123ms`, `"execute_us":44.2`),
  `pid N` and addresses; counts are kept. Guard controls: the reviewer's detail toy (regression and
  v4.12.0 paths) and a vm_parity-style count change are FAILED-NEW; a control whose only
  differences are a pid, a temporary path, durations and a timestamp is FAILED-BASELINE.
  Stability of the known baseline-failing gates under the new identity: section 11.1.
- **F2 (fixed, C10).** The guard runs the 0d853db4 `certify.py` (`git show
  0d853db4:scripts/certify.py`) on control (a): it reports FAILED-BASELINE, exit 0 (laundered),
  where the tree's runner reports FAILED-NEW. Where that commit is absent from the repository the
  control prints NOT RUN and points to this record.
- **F3 (recorded).** The `compiler_stmt_expr.cpp` write-back guard (`&& !may_rebind(arg)` on the
  `location_source` path; loop bindings go through `array_mutation`) is believed unreachable. A
  scratch build of the reviewer's mutant mG (the guard removed; `src/` unchanged) gives the same
  output as production on every probe tried: a named iterable pushed at top level with a
  rebinding argument before and after the loop, inside the loop, in a function, with an
  assignment-expression argument, and with a closure over the loop variable
  (`scratchpad/combined-fix/f3/{a..f}.f`); the reviewer's `r2/o01_locsrc.f` and `o02_locsrc_top.f`
  are refused (`for-capture-vanishing`). The evalorder and precision fixtures (50) and the
  generator (840, 0 bad) also pass on mG. The guard is kept as harmless defence; no fixture
  reaches it.
- **F4 (fixed, C12).** compatibility.md's loop-binding row now says 17f7964c printed the
  legacy-equal `[7,4]` (a previously admitted program whose output changes); the dangling fragment
  in section 8 is removed; `results2.txt` recorded `head=0d853db4` because the gates ran on the
  uncommitted C7-C9 working tree (same binary); this round's results file records the head of the
  commit the gates ran on.

### 11.1 Identity stability of known baseline-failing gates

Known baseline-failing gates (each identical on 17f7964c/fc987ec1, section 9 and b5-refsem.md
11.10): test-runtime2-cp14b; test-runtime2-rt-diff-selftest; test-rt-vm's own recipe lines
vm_parity `--rt-diff` and vm_modules; cp25r-inventory, test-cp15-numeric-repair, test-cp17-bytes,
test-cp18-bytes, test-runtime2-diagnostics, test-runtime2-prevalidate-gate,
test-runtime2-literals-{mutants,decimal-negative,f1-negative}, test-rt-builtins-probe,
test-rt-memory-c1, test-cp19-expression-development, the cp19 admission-mutants pair and the
remaining B5-inventory FAILs (37 in all), and the `make test` components of the review
(v42_struct_private_smoke and four others).

Each gate run twice on the same binary (91a49efa), identities compared with the new
`failure_identity` (`scratchpad/combined-fix/stab/`):

| Gate | Result |
| --- | --- |
| vm_parity `--seed 1 --gen 300 --rt-diff` (test-rt-vm own recipe) | identical (1 FAIL line, `30 differing, 16 approved` in the tail) |
| vm_modules `--gen 100 --seed 1` | identical (3 FAIL lines) |
| test-runtime2-cp14b | identical (`Error 1`, 1 FAIL line; the `real/user/sys` lines differ and are excluded/normalised) |
| test-runtime2-rt-diff-selftest (about 10 min each) | identical (`Error 1`, 8 FAIL lines) |

The other known baseline failures were not rerun twice here (time); they get the same identity
mechanism on Linode, where a varying value that is not normalised shows as FAILED-NEW (the
conservative direction) rather than as a pass. Normalisation of varying values is a guard control
(`test-rt-vary`: pid, `/tmp` path, durations, timestamp differ; FAILED-BASELINE).

## 12. Owner decisions 2026-11-07 22:13 (D1-D4), folded into the F1 round (C11)

- **D1, APPROVED: three evaluation-order differences from legacy**, recorded in
  `docs/runtime-2/evidence/b5/expected-changes.tsv` (three class iii rows: legacy output, candidate
  output, cause and order, permanent test) and compatibility.md (status APPROVED 2026-11-07 22:13;
  none described as legacy parity): (1) the element store whose value rebinds the receiver
  (`evalorder/legacy_fn_store.f`); (2) receiver rebinding in loop bodies, captured loop bindings
  included (`legacy_loop_order.f`, `loopbind_*`: `[7] [7]` against legacy `[7,4] [7,4]`); (3) `b[g()]`
  in a loop declaration, key evaluated once (`legacy_loop_decl_read.f`: 2 against legacy 8). The
  fixtures' headers say APPROVED.
- **D2: the ordinary-operator late read is a RELEASE BLOCKER** (2.2, section 8). Not started in
  this round.
- **D3, assertion classes** (`scripts/certify.py`): every gate's assertions are PASS, PARTIAL,
  SKIP, FAIL or BLOCKED. Skip markers are found mechanically (`SKIP`/`SKIPPED` at a line start,
  `skipping`, `N ... skipped` with N > 0); `KNOWN_CONDITIONAL` lists the conditional checks the
  tree prints, with each requirement and whether it is required on the Linux campaign host
  (other-platform checks are not: their CI lanes); an unknown skip is required. PARTIAL (a
  required assertion skipped while others ran) maps to BLOCKED, SKIP (only skips) to NOT-RUN, FAIL
  to FAILED-NEW/FAILED-BASELINE, BLOCKED to BLOCKED, PASS to PASSED. The summary lists every
  required assertion that did not execute, with its requirement. In certification mode
  (`--certify`, implied by a full campaign) any planned gate that is PARTIAL or SKIP fails the
  campaign (exit 1). Guard: 14 table cases and toy controls (PARTIAL with the read_limit RAM skip,
  SKIP, an other-platform skip that stays PASSED). Local logs: the only required skips are the
  comparator ones (GCC 13.3 here).
- **D4, oracle provenance, no extra hardening.** The preflight checks: the oracle's checkout is
  at `ce8cd36a` (tag v4.12.0), clean, and `make -q nift` current; the binary was built during the
  campaign (`--campaign-start EPOCH` / `NIFT_CAMPAIGN_START`, required; the oracle's mtime must be
  later); and the oracle and the candidate are distinct (different sha256 and real path). The
  facts record the oracle's sha256, real path and build command
  (`git worktree add --detach <tree> ce8cd36a92ecfdc8b15b00df41a6c3bb9a31c0eb && make -C <tree>
  -j"$(nproc)" nift`) in `.build/certify/_preflight.json` and summary.json. Deliberate substitution
  (copying another binary over the oracle's checkout after the build) is an **accepted
  threat-model limitation** (L1).

### 12.1 Linode instance minimums (every mandatory assertion executes)

Superseded by section 13.2 (confirmation round 3 completed the list).

## 13. Confirmation round 3 on 35a5fda4: CONFIRMED-WITH-CONDITIONS; harness completion (C13)

Review: `scratchpad/review-ha/CONFIRM-HA-R3.md` (toys `review-ha/toy4/`, `toy5/`, copied into the
guard). No production change: binary `91a49efa…`.

- **R3-1 (skip detection).** `SKIP_MARKERS` are now case-insensitive `skip`/`skipped`/`skipping`
  (not a hyphenated identifier such as `skipped-float`, not `--skip-million`), upper-case
  `NOT RUN`, `unverified`, `not verified`, TAP `# SKIP` and pytest progress lines with an `s`;
  explicit zero counts (`0 skipped`, `skipped: 0`) are not skips. Upper-case `NOT RUN` only: the
  tree's PASS lines say a refused statement "did not run" / "was not run on the legacy engine".
  Inventory: `tests/runtime2/skip_phrases.tsv` lists every tracked line (Makefile, `*.mk`,
  tests, scripts, tools) that prints skip / not-run / unverified wording: 159 lines, 143
  detected by the markers, 16 justified (FAIL lines of failing gates, identifiers, comments,
  counters), each with its reason. The guard rescans the tree and fails on an unlisted line, a
  stale entry, or a detected line the markers no longer catch (with a negative control). The
  F2 historical control prints `SKIP …` when 0d853db4 is absent, so the guard gate is PARTIAL
  (BLOCKED), never PASSED, there. Limitation: the scan is per line; a message whose wording sits
  on a continuation line without a print keyword is not seen (none known).
- **R3-2 (identity scope).** The identity now uses the whole normalised output of the failing
  step (from the last line that names the step before make's error to the end), or the whole
  log without an identified step; each per-line run is a whole log. The reviewer's
  `test-rt-tail` toy is a guard control (FAILED-NEW on both baseline paths). Stability, recomputed
  on the `stab/` double runs: vm_parity, vm_modules, cp14b (137 lines of step output) and
  rt-diff-selftest (90 lines) are each identical between their two runs.
- **R3-3 (addresses).** Only pointer contexts normalise: `at `/`address `/`ptr=` or a sanitizer
  frame (`#N 0x…`) with 8+ hex digits, or a pointer-width value of 12+ digits;
  `expected 0xdeadbeef got 0xdeadbeee` is FAILED-NEW (guard toy). Durations no longer include a
  bare `s` after an integer or `min`.
- **R3-4 (silent gates).** A gate that exits 0 without output of its own (not the runner header,
  make's own messages, or the recipe commands make echoes) is SKIP (NOT-RUN), unless it is in
  `SILENT_BY_DESIGN` with a reason (empty today: no planned gate is known to be silent; an entry
  needs a reason). Guard toy: `@true` is NOT-RUN and fails the campaign under `--certify`.
- **R3-5.** The other-platform entries of `KNOWN_CONDITIONAL` are anchored to the nine exact
  messages the tree prints; anything else mentioning Windows/macOS is required (guard case).
- **R3-6.** The preflight now checks the host before any gate (section 13.2).
- **R3-7.** "Built from its checkout" requires the checkout's Makefile, and `make -q nift` runs there.
- **D3 reporting.** The summary lists every assertion that did not execute, each marked
  REQUIRED or not-required with its requirement (guard control).

### 13.2 Linode instance minimums and toolchain plan (checked by the preflight)

| Requirement | Minimum | Checked | For |
| --- | --- | --- | --- |
| RAM | 8 GB plan (MemTotal >= 7600 MiB) | yes | read_limit.py's default-limit check needs MemAvailable >= 4096 MiB |
| Disk | 50 GB free | yes | 1.7 GB worktree; about 1.5 GB per sanitizer object set (ASan/UBSan, TSan) and their variants (guard-off, cp18, cp19, cp23); three baseline trees; a GCC snapshot build (5-7 GB build tree, about 1.2 GB installed) |
| Cores | 2 (4 recommended) | no | duration only |
| GCC 15.2 with `__GLIBCXX__` 20260321 | the GCC 15 branch snapshot of 2026-03-21 (`15.2.1 20260321`; FreeBSD ships it as `gcc15-devel 15.2.1.s20260321`) | yes | comparator sort |
| Python | 3.12+, with headers (Ubuntu `python3-dev`) | yes | harnesses, test-python-binding |
| Go | 1.26+ (bindings/go/go.mod) with pkg-config | yes | test-go-binding (`go test -race`) |
| Node | 22 (>= 20) and npm | yes | test-node-binding |
| .NET SDK | 10 (`net10.0`) | yes | test-csharp-binding |
| git, strace, GNU time (`/usr/bin/time`), util-linux (`setpriv`, `script`), cc, make, coreutils | any current | yes | conditional checks in KNOWN_CONDITIONAL; test-progress-pty |
| PTY (`/dev/ptmx`), loopback TCP | available | yes | test-progress-pty, v4.4 PTY smokes; test-serve |
| `/dev/shm` | a separate tmpfs | yes | cross-device move cases |
| Kernel | `vm.mmap_rnd_bits` <= 28 (`sysctl -w vm.mmap_rnd_bits=28`) | yes | the TSan gates |
| User | an unprivileged `tester` | (runner) | root-only classification |

Ubuntu 24.04 packages: `build-essential pkg-config git strace time util-linux python3 python3-dev
python3-venv golang-go` (only 1.22 in 24.04: install Go 1.26 from go.dev/dl instead), Node 22 from
NodeSource or nodejs.org, `dotnet-sdk-10.0` from Microsoft's package feed.

**GCC 15.2 with `__GLIBCXX__` 20260321.** That value is the DATESTAMP of the GCC 15 branch
snapshot `15-20260321`; no LTS image (Ubuntu 24.04, Debian 12/13) ships it, and rolling
distributions have since moved past it. The tree's evidence (cp14b-d4-validation.md) names the
profile but not the image; the exact compiler identity is in the owner workspace
(`.claude-linode-runs/codex/cp14b/implementation/d4/profile.json`). Plan: build from the
snapshot on the instance:

    curl -O https://gcc.gnu.org/pub/gcc/snapshots/15-20260321/gcc-15-20260321.tar.xz
    tar xf gcc-15-20260321.tar.xz && cat gcc-15-20260321/gcc/DATESTAMP   # must print 20260321
    (cd gcc-15-20260321 && ./contrib/download_prerequisites)
    mkdir gcc-build && cd gcc-build && ../gcc-15-20260321/configure --prefix=/opt/gcc-15-20260321 \
        --enable-languages=c,c++ --disable-multilib --disable-bootstrap && make -j"$(nproc)" && make install
    export PATH=/opt/gcc-15-20260321/bin:$PATH LD_LIBRARY_PATH=/opt/gcc-15-20260321/lib64

Estimate: 30-60 min on 4 cores without bootstrap (2 cores: about twice), 5-7 GB build tree
(deleted after install), about 1.2 GB installed. Then `echo __GLIBCXX__ | g++ -E -include
cstddef -` must print 20260321 (the preflight checks it). Not tried here (disk).
