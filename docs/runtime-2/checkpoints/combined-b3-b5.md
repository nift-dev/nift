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
| Disk | 40 GB free (section 14: no toolchain build) | yes | 1.7 GB worktree; about 1.5 GB per sanitizer object set (ASan/UBSan, TSan) and their variants (guard-off, cp18, cp19, cp23); three baseline trees |
| Cores | 2 (4 recommended) | no | duration only |
| GCC 15.2 | the D4 profile: Ubuntu `g++-15 15.2.0-16ubuntu1` with its libstdc++ (section 14) | yes (version, `__GLIBCXX__`, three sha256s) | comparator sort |
| Python | 3.12+, with headers (Ubuntu `python3-dev`) | yes | harnesses, test-python-binding |
| Go | 1.26+ (bindings/go/go.mod) with pkg-config | yes | test-go-binding (`go test -race`) |
| Node | 22 (>= 20) and npm | yes | test-node-binding |
| .NET SDK | 10 (`net10.0`) | yes | test-csharp-binding |
| git, strace, GNU time (`/usr/bin/time`), util-linux (`setpriv`, `script`), cc, make, coreutils | any current | yes | conditional checks in KNOWN_CONDITIONAL; test-progress-pty |
| PTY (`/dev/ptmx`), loopback TCP | available | yes | test-progress-pty, v4.4 PTY smokes; test-serve |
| `/dev/shm` | a separate tmpfs | yes | cross-device move cases |
| Kernel | `vm.mmap_rnd_bits` <= 28 (`sysctl -w vm.mmap_rnd_bits=28`) | yes | the TSan gates |
| User | an unprivileged `tester` | (runner) | root-only classification |

(The toolchain plan of this section is superseded by section 14.)

## 14. The comparator toolchain is Ubuntu's packaged GCC 15.2.0-16ubuntu1 (C14)

The owner's D4 profile (owner workspace `.claude-linode-runs/codex/cp14b/implementation/d4/profile.json`,
read by the coordinator) records the comparator certification toolchain as `g++ (Ubuntu
15.2.0-16ubuntu1) 15.2.0`, `__GLIBCXX__` 20260321, `_GLIBCXX_RELEASE` 15, flags `-std=c++17 -O2
-Wall -Wextra -pedantic -pthread`, scope "Linux/GCC15 libstdc++ full-buffer profile only", and the
sha256 of `/usr/lib/gcc/x86_64-linux-gnu/15/libstdc++.so` and of `bits/stl_algo.h` and
`bits/stl_tempbuf.h`. Those values are now in the tree, `tests/runtime2/cp14b/toolchain-profile.json`
(with that provenance).

- **Preflight.** Besides `__GLIBCXX__` 20260321, the compiler's version string must contain
  `15.2.0-16ubuntu1`, and the libstdc++.so (found with `$CXX -print-file-name=libstdc++.so`, symlink
  resolved) and the two headers (found through the compiler's include trace for `<algorithm>`)
  must have the profile's sha256s. Guard: a mismatch on each field fails (5 table cases).
- **Image.** packages.ubuntu.com lists `gcc-15` **15.2.0-16ubuntu1** for **resolute (Ubuntu 26.04
  LTS)**; stonking (26.10) and tenacious have 15.3.0-4ubuntu1 (checked 2026-11-07). Install on a
  26.04 instance, pinned and held:

      apt-get install gcc-15=15.2.0-16ubuntu1 g++-15=15.2.0-16ubuntu1 cpp-15=15.2.0-16ubuntu1 \
          libstdc++-15-dev=15.2.0-16ubuntu1 libgcc-15-dev=15.2.0-16ubuntu1
      apt-mark hold gcc-15 g++-15 cpp-15 libstdc++-15-dev libgcc-15-dev
      export CXX=g++-15 CC=gcc-15

  If the resolute archive has moved on by then (an SRU of gcc-15), pin the archive to a date with
  snapshot.ubuntu.com (`deb [snapshot=20261107T000000Z] http://archive.ubuntu.com/ubuntu resolute main
  universe`, apt >= 2.7) so exactly 15.2.0-16ubuntu1 resolves. The runtime `libstdc++6` the
  `libstdc++.so` symlink resolves to comes from that archive state too: the preflight's sha256
  check decides, whatever the package names.
- **Linode image: not verified here.** Ubuntu 26.04 was released in April 2026 and other providers
  list it; whether Linode offers `linode/ubuntu26.04` must be checked with the API
  (`linode-cli images list | grep -i 26.04`) before provisioning. Fallback: a 24.04 instance with a
  resolute chroot (`debootstrap resolute /srv/resolute http://archive.ubuntu.com/ubuntu`, or the
  snapshot.ubuntu.com mirror at a pinned date), the campaign run inside it with the pinned packages.
- **Rejected:** building the GCC 15 snapshot 15-20260321 from source (C13's plan): it shares the
  date stamp but is not the profiled Ubuntu libstdc++ build (different files and sha256s).
- **Disk.** No toolchain build: the minimum is back to 40 GB free (preflight `MIN_DISK_GIB`).
- **Go, Node, .NET on 26.04.** Not verified (the packages.ubuntu.com page for resolute's
  `golang-go` could not be fetched from here). Use the upstream installers as on 24.04 unless the
  resolute packages meet the minimums (Go 1.26+, Node 22, .NET SDK 10): go.dev tarball, Node 22 from
  nodejs.org/NodeSource, `dotnet-sdk-10.0` from Microsoft's feed for 26.04 (or the official install
  script). The preflight checks presence; versions are recorded in its facts.
- **No conflict with the cp14b gate.** `tests/runtime2/cp14b/comparator_profile.py` and the
  production gate (`compiler_collection_sort.cpp`) check only `__GLIBCXX__ == 20260321`; the
  preflight is stricter (a different build with the same date stamp passes the gate's check but
  fails the campaign preflight), which is the intended direction. Neither is changed.

## 15. Confirmation round 4 on 5f8630e2: CONFIRMED-WITH-CONDITIONS; fixes (C15-C17)

Review: `scratchpad/review-ha/CONFIRM-HA-R4.md` (toys `toy6/`, `toy7/`, copied into the guard).
Owner additions 23:10 (environment record, two-stage flow) and 23:14 (four outcomes, source-tree
binding). No production change: binary `91a49efa…`.

- **R4-1.** `skipped 0 <noun>` is a zero count. `test-rt-parser`'s `paren oracle: skipped N
  nondeterministic entries` is a by-design KNOWN_CONDITIONAL entry (the entries are
  tools/rt-diff/ignore.txt's). `test-sha256`'s `running as root; unreadable-file check skipped`
  is required when not root; **as root the runner re-runs that gate as the unprivileged `tester`
  user** (the plan provides the user: `--unprivileged-user`, default `tester`); if no
  unprivileged run executes it, the check counts as a required skip. Local classification of
  real gate logs: section 15.1.
- **R4-2.** The identity is the whole normalised log (the step's last mention could be moved by a
  FAIL line naming a test path); the reviewer's `test-rt-paths` toy is a guard control
  (FAILED-NEW). The `stab/` double runs stay identical under it (vm_parity 10 lines, vm_modules
  5, cp14b 138, rt-diff-selftest 90).
- **R4-3 and owner 23:14.** The campaign's `make nift` return code is checked (a failure aborts,
  exit 2). `./nift` must be current (`make -q nift`), not older than the campaign start, and its
  build signature (`.build/obj-config/default.flags`: resolved compiler path and `--version` hash,
  args) must name `$CXX`'s resolved path and version hash and carry the profile flags
  (`-std=c++17 -O2 -Wall -Wextra -pedantic -pthread`); a `CXXFLAGS` environment override that
  differs from the profile fails. summary.json `certified_binary` records the binary sha256, the
  profile, the compiler and its resolved path, the libstdc++ sha256, the build invocation, the
  build signature and the source tree (HEAD, tree, clean flag). Guard: a binary built by another
  compiler with the same `__GLIBCXX__` (the 15-20260321 snapshot) fails. **Limit:** the header
  sha256s are taken from the compiler's own include trace without the build's `-I` paths, so a
  `bits/stl_algo.h` placed under `src/` or `include/` would shadow the system header in the build
  but not in the preflight (none exists in the tree).
- **R4-5.** make's echo of a continued recipe is not the gate's own output (each echoed line is
  removed); the 400-line cap is gone. Guard: `test-rt-cont` is NOT-RUN.
- **R4-6.** Minimum versions: Python 3.12, Go 1.26, Node 22, .NET SDK major 10; the other tools
  need only be present (no minimum stated in 13.2).
- **R4-7.** The phrase scan covers the Makefile, `*.mk`, and the scripts, harnesses and test
  sources (`py sh c cc cpp h js go cs ps1`) of tests/, scripts/, tools/ and bindings/ (162 lines,
  146 detected, 16 justified). Not scanned, with reasons: docs/ (not printed by a gate), Nift
  program fixtures (`*.f`, `*.in`, `*.out`, `*.json`: program data compared with goldens), src/
  (the product's own messages, asserted by the gates that print them). Limit: per-line scan.
- **Owner 23:14 outcomes.** Each gate records `zero-skipped`, `inapplicable-only`,
  `mandatory-skipped` or `not-executed`; a KNOWN_CONDITIONAL entry excuses only its own line
  (guard: the root line plus another skip in the same log is PARTIAL).
- **Owner 23:10 environment.** summary.json and every gate result carry `environment`: hostname,
  kernel, distro, container or host, compiler, libstdc++ sha256, binary sha256.
  `certify.py --merge A.json B.json …` lists each environment and the gates that ran in it.

### 14.1 Toolchain pin (R4-4)

Choice: **install from the snapshot date and pin**. Add to the archive pin of section 14
(`snapshot=20261107T000000Z`, resolute) and install and hold every package of the gcc-15 source
the build and the loader use, at the same version:

    V=15.2.0-16ubuntu1
    apt-get install gcc-15=$V g++-15=$V cpp-15=$V libstdc++-15-dev=$V libgcc-15-dev=$V \
        gcc-15-base=$V libstdc++6=$V libgcc-s1=$V
    apt-mark hold gcc-15 g++-15 cpp-15 libstdc++-15-dev libgcc-15-dev gcc-15-base libstdc++6 libgcc-s1

(packages.ubuntu.com, resolute: `libstdc++6-15-dbg` is built from source gcc-15
15.2.0-16ubuntu1 and depends on `libstdc++6 (>= 15.2.0-16ubuntu1)`.) Whatever apt resolves, the
preflight's sha256 checks decide.

### 14.2 Single-instance two-stage flow (owner 23:10)

1. Provision the intended instance (Ubuntu 26.04 image if Linode offers it, section 14).
2. Stage 1, environment only: install the pinned toolchain and tools, build the v4.12.0 oracle,
   `make nift`, then `python3 scripts/certify.py --list --legacy-oracle … --campaign-start …`
   (the preflight, nothing else). **If the preflight reports any problem, delete the instance
   immediately** (no gate runs on a wrong environment).
3. Stage 2, on the same instance: `python3 scripts/certify.py --certify …` (the full campaign,
   whose own preflight repeats stage 1's checks against the same binary).

If the fallback (a resolute chroot or container on a 24.04 instance) is used, these planned
gates depend on the host and must be judged with that in mind (the summary's `environment.container`
says which ran where): the TSan gates (`*-tsan`: the host kernel's `vm.mmap_rnd_bits` and address
layout); process management and job control (`test-v45-job-control`, `test-cp21-*` terminal and
fd gates, `test-rt-host*`, `test-runtime2-process-capture`, `test-cp20-*`, `test-cp19-host`,
`escapees_subreaper` in the host-process unit: PID namespaces and `PR_SET_CHILD_SUBREAPER`); PTY
gates (`test-progress-pty`, the v4.4 PTY and TTY smokes, `test-cp21-terminal-lease`); networking
(`test-serve`: loopback ports); filesystem gates (`/dev/shm` cross-device moves, symlinks,
`test-filesystem-type`); and privilege gates (setpriv, the unprivileged `tester` re-runs).

### 15.1 Local classification of real gate logs (R4-1)

Estimate: the 343 planned gates that need no sanitizer, GCC 15.2, oracle or binding toolchain
would take about 5-8 hours here (test-rt-vm's 45 prerequisites alone took 33 min), over the 3-hour
budget. So the 44 not-yet-run gates whose recipes print skip / not-run / unverified wording ran
first (`make -k`, non-certify, 42.7 min; logs `scratchpad/combined-fix/cls/`), and the 24 gate
logs of this round's earlier runs were reclassified (`cls/reclass.txt`). The other ~275 feasible
gates were not run here; they are classified on Linode.

The pass found these false readings, now fixed: make's echo of a recipe containing `echo "…
skipped …"` (excluded: `skipped_assertions` drops the echoed recipe lines); product wording
(`status --strict lists unverified pages`), test-case names (`PASS skipped && interpolation`,
`PASS syntax validates skipped segment`) and mutant descriptions (`PASS mutant N (… skipped …)`)
(KNOWN_CONDITIONAL entries, not required); cp14b's `UNVERIFIED <case>` lines (the comparator
requirement). It also found one real conditional check: test-serve's IPv6 case (`SKIPPED - IPv6
::1 not available on this host`), now a required KNOWN_CONDITIONAL entry and a preflight check
(`::1` must bind). And test-test-integrity flagged `scripts/certify.py` (a subprocess result it
could not see used): rewritten, so its findings equal fc987ec1's 57.

**Gates whose class would block a `--certify` campaign:**

| Gate | Here | On Linode |
| --- | --- | --- |
| test-serve | PARTIAL (no IPv6 `::1` in this container) | PASS when the preflight's `::1` check passes |
| test-runtime2-cp14b-sort, test-cp19-session-cli, test-runtime2-cp14b-design-mutants | BLOCKED (comparator SKIP/UNVERIFIED on GCC 13.3) | judged only on the GCC 15.2 profile host |
| test-runtime2-cp14b, test-runtime2-cp14b-mutants | FAIL with comparator UNVERIFIED lines | judged only on the GCC 15.2 profile host |
| test-runtime2-certification-guard | PASS | PARTIAL if the clone lacks 0d853db4 (historical control) |
| test-sha256 | FAIL here (VM refusals, pre-existing) with the root-only skip | its unreadable-file check is re-run as `tester` |

Failing here, expected FAILED-BASELINE only if identical on the named baselines (pre-existing per
b5-refsem.md 11.10 and the combined review; not re-compared here): test-v44-language-foundation
(load-sensitive), test-v44-shell-restricted, test-v46-relative-imports,
test-runtime2-cp03-simplify, test-sha256, test-runtime2-host-deps, test-runtime2-cp01-simplify
(FLAKY on both binaries: FAILED-NEW under the zero-flakiness rule), test-cp19-s1-dispatch,
test-rt-parser, test-test-integrity (57 findings, the same as fc987ec1), test-v44-packages,
test-rt-host-mutants (mutant builds rejected by the production build guard), cp25r-inventory,
test-rt-vm's own recipe (vm_parity `--rt-diff`, vm_modules), test-runtime2-rt-diff-selftest;
test-runtime2-scanner-parity is BLOCKED here (needs NIFT_REF: the campaign passes the oracle).

Judged only on Linode: every sanitizer and TSan gate (including test-runtime2-b5-evalorder-sanitize),
the four binding gates, the performance lane (PERF_LANE, LOAD_SENSITIVE), test-cp19-proof-cli
(oracle), the GCC 15.2 comparator gates, the root-only re-runs as `tester`, and the ~275 feasible
gates not run here.

## 16. Confirmation round 5 on 0193d4ed: CONFIRMED-WITH-CONDITIONS; build binding (C18)

C18 changes only `scripts/certify.py`, the guard and `skip_phrases.tsv`; `src/` and the binary
(`91a49efa…`) are unchanged.

**R5-2 (fixed): the build binding is exact.** The preflight derives the C++ arguments the Makefile
itself gives the objects (`$(OBJ_SIGNATURE_ARGS)` with CXXFLAGS, CPPFLAGS and LDFLAGS unset; the
build output root written as `$TEST_DIR`, as the signature writes it) and refuses:
- a build signature whose arguments are not exactly that list (an extra `-O0` or `-D_GLIBCXX_DEBUG`,
  in any position, is refused as well as a missing flag);
- CPPFLAGS or LDFLAGS set in the environment (CXXFLAGS other than the profile flags was already
  refused);
- tracked changes in the source tree, and untracked or ignored sources under `src/` (the Makefile
  builds `$(wildcard src/rt/*.cpp)`);
- a missing `.build/obj-config/default.flags` (the binding was silently skipped).
Guard cases: exact arguments (accepted), extra `-O0`, extra `-D_GLIBCXX_DEBUG`, CPPFLAGS set,
LDFLAGS set, dirty tracked tree, untracked `src/rt/extra.cpp`, clean tree (accepted), missing
signature.

**R5-3 (fixed): an excusing entry covers its own exact text.** Every KNOWN_CONDITIONAL entry that
excuses a skip is anchored at both ends against the real in-tree line, and a skip marker in any
text a matched entry leaves over makes the line required. Guard controls: `PASS mutant 3 (x):
build skipped (no compiler), not run` (PARTIAL), the real mutant-13 line (PASS), `PASS skipped &&
interpolation; SKIP remaining cases (no tty)` (PARTIAL), the paren-oracle line behind another
skip (PARTIAL).

**R5-5 (fixed): `--merge`** prints `MERGE REFUSED: <field> differs between environments` and exits
1 when `binary_sha256` or `stdlib_sha256` differ; equal binaries on different hosts merge (exit 0).
Guard case on generated summaries.

**Recorded for the release audit, not fixed:**
- R5-4: on `urc == 0` the root re-run credits the line without checking that the unprivileged log
  shows the check; a failing gate (rc ≠ 0, e.g. test-sha256) is never re-run for its root-only
  check and is reported "no unprivileged run available" (wrong wording); with that gate
  FAILED-BASELINE the run can exit 0. `prepare_copy` deletes an existing
  `~tester/nift-certify-copy` without warning.
- R5-6: labelling noise (`UNVERIFIED <anything>` labelled with the comparator requirement; any
  `serve_smoke: N passed, M skipped` labelled IPv6; test-test-integrity's `[pipefail-missing] …
  "$skip"` finding read as a required skip). All stay required; nothing is hidden.
- R5-1b: test-runtime2-cp01-simplify is FLAKY on both binaries, so FAILED-NEW; a campaign exits 1
  while it stays flaky. Needs a fix or an owner decision.

**R5-1a (analysis only, see the round-5 report):** §15.1's "expected FAILED-BASELINE" list is
unverified. Whole-log identity holds on one tree (§11.1) but is not shown to hold across trees;
for test-rt-vm's own recipe it cannot (vm_parity prints the HEAD-only `'approved B5 Q3'`
category; recipe and 9 harness files differ from 17f7964c). For the gates absent from v4.12.0, and
for VM refusals, v4.12.0 is not a usable baseline. No new identity mechanism was built; the options
are with the owner.

## 17. Per-gate failure fingerprints (owner 2026-11-08 01:06, Option A) (C19)

C19 is a harness change only: `scripts/certify_fingerprints.py` (new), `scripts/certify.py`, the
guard, `tests/runtime2/certify_fingerprints_selftest.py` and its real-log fixtures
(`tests/runtime2/certify-fingerprints/*.txt`). No production or runtime change; the binary is
`91a49efa…`.

**Rule.** A failing gate is FAILED-BASELINE only when its failure fingerprint on the candidate
equals its fingerprint on the baseline (v4.12.0 or a regression baseline). Both are computed during
the campaign from the two raw logs by the same extractor; nothing stored is compared. Anything else,
including a log with no extractable failing case or a gate without an extractor, is FAILED-NEW;
there is no fallback to "same step". The old whole-log identity is still computed and reported
(`whole log identical: NAME=True/False`) as a stricter extra signal; it does not decide. Recipe
lines run on their own (H2a) are compared the same way, line by line.

**Fingerprint** (`certify_fingerprints.py`, declarative per-gate SPECS):
- each failing case: (step, case id, category, status, detail). The step is the harness make ran;
  the case id is the name the harness prints; the category is the first of build-refused, refusal,
  crash, signal, timeout, exception that matches the case's lines, else assertion (or the gate's
  fixed category: test-integrity `finding:<kind>`, cp25r `unclassified`, scanner `diff`); the
  status is the exit status the case states; the detail is the refusal code and source position,
  the crash text, the exception, or the harness's own detail line and the lines under it, plus the
  case line's own detail;
- the failure count, from a separate per-gate pattern (a stated count where the harness prints one,
  otherwise the failure lines), compared independently of the case set;
- the gate's own exit (make's `Error N` on the top-level target, or the line's rc).
- Normalisation is narrow: the checkout roots, and absolute paths under /tmp reduced to their base
  name (with line and column). Durations, counts and other values are kept.
- One justified reduction: test-rt-host-mutants records the refused switch, not the refused object
  set, because host_mutants.sh runs `make -j4` without `-k` and which objects are refused before
  make stops depends on scheduling.

**Expected-baseline gates with an extractor (13):** cp25r-inventory, test-cp19-s1-dispatch,
test-rt-host-mutants, test-rt-parser, test-runtime2-cp01-simplify, test-runtime2-cp03-simplify,
test-runtime2-rt-diff-selftest, test-runtime2-scanner-parity, test-sha256, test-test-integrity,
test-v44-packages, test-v44-shell-restricted, test-v46-relative-imports.

**Stopped (no extractor; FAILED-NEW until an adapter is approved), with proposals:**

| Gate | Why its failing cases cannot be identified | Proposed adapter |
| --- | --- | --- |
| test-v44-language-foundation | `tests/v44_ast_expression_smoke.sh` fails with no output (`set -euo pipefail`; the failing command prints nothing) | certify re-runs that recipe line under `bash -x` on both trees and takes the last traced command as the case; or a reviewed test-only change printing the failing step |
| test-rt-vm (own recipe) | vm_parity's `--rt-diff` part runs rt-diff `--quiet` and deletes its report; the log says only `FAIL vm parity: 1 failure(s)` (vm_modules alone is extractable) | certify runs vm_parity with the rt-diff report kept in a certify directory on both trees and fingerprints the unapproved differing entries (id, channel) |
| test-runtime2-host-deps | read_limit.py prints `FAIL <name> <detail>`: names contain colons and exit codes, and the detail is raw stderr cut to a fixed tail whose start moves with the length of TMPDIR (it contains a pid) | match read_limit.py's case names from its `check()` calls (a reviewed name table) and keep only each case's refusal code / exit status |

**Guard.** `certify_fingerprints_selftest.py` (run by test-runtime2-certification-guard; 81 checks)
fails if an expected-baseline gate has no extractor, no table row, or is also stopped. For each
gate, on its real log: positive (equal to itself, and to a copy with another checkout root and other
temporary paths), a new failure alongside the existing ones, a changed diagnostic or category, a
substitution with the same count (each must differ), and a wording-only change outside the
extracted fields (summaries, notes, totals, headers; must stay equal); test-integrity also has a
count-only change. End to end, toy controls n1 (diagnostic changes), n2 (a passing case fails
alongside) and n3 (substitution, same count) are FAILED-NEW under certify; p and v (equal failure,
varying temporary path, pid, time) are FAILED-BASELINE; the earlier controls a, c, d, t, x, tp run
under an expected-baseline gate name and stay FAILED-NEW, e (vm_parity style, no extractor) stays
FAILED-NEW.

### 17.1 Equality shown locally (HEAD binary 91a49efa, fc987ec1 binary ac283072)

Fingerprints computed by the C19 extractor from raw logs: head1 = the round-4 certify logs
(`cls/`), head2 = a second run on this tree, fc = this tree's harness with the fc987ec1 binary
(`make T TARGET=<fc987ec1 binary>`). Only the binary differs between head and fc, which is the
main source of cross-tree change. Whole-log identity often differs where fingerprints are equal:
the echoed command lines name the binary's path, and prerequisite output differs between runs.
The 17f7964c and weekend/r2 comparisons are for Linode.

| Gate | head1 ~ head2 | head ~ fc987ec1 |
| --- | --- | --- |
| test-v46-relative-imports, test-runtime2-cp03-simplify, test-v44-packages, test-v44-shell-restricted, test-cp19-s1-dispatch, cp25r-inventory, test-rt-parser, test-runtime2-rt-diff-selftest | equal | equal |
| test-test-integrity | head2 equals fc987ec1's tree (57 findings each); the round-4 log (58: certify.py's finding, fixed in C17) differs, as it should | equal |
| test-rt-host-mutants | one log only (it builds mutant trees; same tree, not binary-dependent) | n/a |
| test-sha256 | **flaky**: `[hybrid] up to date after rebuild` fails in 1 of 6 HEAD runs and 2 of 5 fc987ec1 runs (`[modified]` fails in all) | differs when the flake hits |
| test-runtime2-cp01-simplify | **flaky** (known, R5-1b): 4 failing cases in one run, 1 in the next | differs |
| test-runtime2-scanner-parity | **not stable on one tree**: cp22 vm-only thread-order scripts (v_preempt, v_thread_order, v_sleep_ties), socket_proof.f (the reference prints a random port) and for-s2/t27 differ between two runs; 1519/1520 failing scripts against the v4.12.0 reference | differs: fc987ec1 also fails 25 b5/evalorder fixtures (the H-A fix) |

So the extractor identifies the cases in all 13, but three of those gates are nondeterministic
themselves (test-sha256, cp01-simplify, scanner-parity): with zero-flakiness they will be
FAILED-NEW on Linode until their nondeterminism is fixed or the owner decides otherwise.
Scanner-parity would need its thread-order and port-printing probes made deterministic or
excluded with a reviewed list; sha256's hybrid/modified freshness check looks like mtime
granularity (not investigated; same class as CP01).

## 18. Review of C18-C19 (3161f52d): ACCEPT-WITH-CONDITIONS; fixes (C20)

Harness only; `src/` and the binary (`91a49efa…`) unchanged.

**F1 (fixed): diagnostics outside a case block.** One shared DIAGNOSTICS list for every gate:
build-refused, refusal (VM refusal code, session refusal), internal-failure (`NIFT_VM internal
failure`), sanitizer (`ERROR: …Sanitizer: kind`, UBSan `runtime error:`), assertion (C/C++
`Assertion … failed`), crash (Segmentation fault, Aborted, Bus error, Floating point exception,
Illegal instruction, Killed, Terminated), timeout, traceback, exception. Every match outside the
case blocks is an entry of its own (step, `<outside>`, category, detail); inside a block every
match of every category is in the case's detail (so a crash inside a refusal case changes it).
Normalisation is unchanged (roots, /tmp paths). test-rt-host-mutants keeps its outside entries as a
set (how many objects are refused depends on -j4). Selftest: on every gate, a traceback, a new
refusal id, an internal failure, a crash, a C++ assertion, a sanitizer report and a timeout,
each inserted before the first case line and after the last block, must change the fingerprint
(182 checks), plus the review's category moves (a crash or internal failure before
`FAIL run-captures-not-tty`, a traceback before `FAIL failed user function status non-zero`).
Cross-run equality (§17.1) rechecked with the C20 extractor: unchanged (equal for the same eight
gates and test-integrity's tree pair; sha256, cp01 and scanner-parity differ as before).

**F2 (fixed; decision 2026-11-08): the regression baselines decide.** A failure is FAILED-BASELINE
only if its fingerprint is equal on EVERY applicable regression baseline (17f7964c, weekend/r2):
`all()`, not `any()`. A baseline is applicable when the gate exists there (no "No rule to make
target") and can run there (its log is not harness-blocked); a named baseline tree that cannot be
built counts as applicable and differing. A gate that passes, or fails differently, on an
applicable baseline is FAILED-NEW, whatever v4.12.0 shows. Only when no regression baseline is
applicable may an equal v4.12.0 fingerprint give FAILED-BASELINE, labelled `LEGACY-ONLY` in the
reason. Each failing gate's reason lists every baseline: `baselines: 17f7964c: applicable,
fingerprint equal; weekend/r2: not applicable: no such target there`. Guard: seven report-class
cases (any vs all, not applicable, v4.12.0 vs a passing regression, legacy-only) and four toy
controls: b9 (the review's toy9: v4.12.0 equal, 17f7964c passes → FAILED-NEW), ba (equal on
17f7964c, different on weekend/r2 → FAILED-NEW), bb (equal on both → FAILED-BASELINE), bc (no
target on the regression tree, v4.12.0 equal → FAILED-BASELINE, LEGACY-ONLY).

**F3 (fixed): make flag variables.** `clean_env()` drops GNUMAKEFLAGS too; the preflight refuses
MAKEFLAGS, MFLAGS, GNUMAKEFLAGS or MAKEOVERRIDES set in the environment, and every `?=` variable of
the Makefile set from the environment (CXX and CC are the campaign's compilers and bound to the
signature; CXXFLAGS only as the profile flags). The C++ flag portion of the signature (all but
`-include FILE` and `-I…`) must equal the profile's flags exactly, checked directly, so a spoofed
derivation that agrees with a spoofed build is still refused. Guard cases for each.

**F4 (fixed).** Untracked or ignored files under `src/` and every `-I` directory of the signature
(include, minifypp, markuppp; the build's own .o/.d/.cfg aside) are refused; a failing
`git ls-files` is refused. The C half of the signature (the cmark objects) must equal the
Makefile's derivation (CFLAGS from the environment is a `?=` variable, refused) and be built by
$CC. A failing `make --eval` derivation is a preflight failure, not a fallback. Guard cases.

**F5 (fixed).** `count` is the gate's own stated count where it prints one (test-v44-shell-
restricted and test-cp19-s1-dispatch: the `FAILED: a, b` names; cp01: `FAILS (N):`; rt-parser:
`N rt parser unit failure(s)`; test-integrity: `findings: N`); the other eight print none, and the
selftest table states why for each (`count_source`). The failure-line count is kept separately
(`lines`). Both are compared.

**Stopped gates** stay stopped, as named FAILED-NEW, pending the owner (an adapter must parse the
unchanged baseline logs, so a test-only change in the candidate cannot help).

**Applicable baselines per gate** (from the Makefiles of 17f7964c, 0b5079fc and ce8cd36a; whether
each can run there is decided by the campaign):

| Gate | 17f7964c | weekend/r2 | v4.12.0 |
| --- | --- | --- | --- |
| test-v44-shell-restricted, test-v46-relative-imports, test-v44-packages, test-test-integrity | target present | target present | target present (not used: regression baselines apply) |
| cp25r-inventory, test-cp19-s1-dispatch, test-rt-host-mutants, test-rt-parser, test-runtime2-cp01-simplify, test-runtime2-cp03-simplify, test-runtime2-rt-diff-selftest, test-runtime2-scanner-parity, test-sha256 | target present | target present | no target |
| test-v44-language-foundation, test-rt-vm, test-runtime2-host-deps (stopped) | target present | target present | language-foundation present; the others absent |
