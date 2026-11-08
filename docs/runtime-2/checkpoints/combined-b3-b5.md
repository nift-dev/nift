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

### 2.2 Same root cause outside H-A (STOP: not fixed; RELEASE BLOCKER, owner 2026-11-07 22:13 D2, bounded investigation to be briefed separately; fixed by C30, section 27)

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
- **test-rt-memory-c1 out-of-memory abort** (C37, section 30): under `ulimit -v` nift aborts
  (SIGABRT, glibc TLS destructor registration) instead of failing cleanly; nondeterministic;
  pre-existing (weekend/r2, B3, B4).
- **B4b `script_return_kernel_error`** (C36, section 30): `a := []; return a.pop()` refused
  `type:script-return` since B4b (B3 and weekend/r2 run it legacy-equal); cp14b is FAILED-NEW
  against weekend/r2 until fixed or decided.
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
        gcc-15-base=$V libstdc++6=$V libgcc-s1=$V \
        gcc-15-x86-64-linux-gnu=$V g++-15-x86-64-linux-gnu=$V cpp-15-x86-64-linux-gnu=$V
    apt-mark hold gcc-15 g++-15 cpp-15 libstdc++-15-dev libgcc-15-dev gcc-15-base libstdc++6 libgcc-s1 \
        gcc-15-x86-64-linux-gnu g++-15-x86-64-linux-gnu cpp-15-x86-64-linux-gnu

(Review of a977376e: the arch-named `*-15-x86-64-linux-gnu` packages own the real compiler
binaries, `/usr/bin/x86_64-linux-gnu-g++-15` and `-gcc-15`, which the preflight requires `dpkg -S`
to show at this version, C21.)

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

## 19. Confirmation of C20 (965d2a2a): CONFIRMED-WITH-CONDITIONS; fixes (C21)

Harness only; `src/` and the binary (`91a49efa…`) unchanged.

**C1 (fixed): applicability.** A regression baseline is not applicable only when make says its own
Makefile has no rule for the gate itself (`No rule to make target '<gate>'.`, with `  Stop.`
without `-k`, nothing after the name). A missing prerequisite (`…, needed by '<gate>'`) and a
harness-blocked baseline log (comparator SKIP, missing oracle, missing tool) are applicable and
differing: they give no evidence and never support FAILED-BASELINE. So LEGACY-ONLY happens only
when every regression baseline genuinely lacks the target. The v4.12.0 baseline uses the same
target test. Guard toy controls: bd (the review's toy11: equal on 17f7964c, weekend/r2 with a
missing prerequisite → FAILED-NEW), be (toy11 r3: weekend/r2 harness-blocked → FAILED-NEW), bf
(every regression baseline blocked or unbuildable, v4.12.0 equal → FAILED-NEW, not LEGACY-ONLY).

**C2 (fixed): diagnostic list.** Added: an uncaught C++ exception (`terminate called after
throwing an instance of '…'` and its `what():` line), sanitizer WARNING reports (ThreadSanitizer,
AddressSanitizer), and exception names that are dotted or bare (`^\w+(\.\w+)*(Error|Exception)\b`,
with or without a message or a Traceback: `subprocess.CalledProcessError: …`,
`json.decoder.JSONDecodeError: …`, `MemoryError`); UBSan `runtime error:` was already matched.
The selftest now inserts 14 diagnostics at both positions on all 13 gates (26 positions each);
454 checks. Cross-run equality rechecked: unchanged.

**Release-audit items fixed.**
- The compilers are pinned themselves: the real path of `$CXX` and `$CC` must be an ELF binary (a
  wrapper script is refused) owned by a `g++-15` / `gcc-15` package at the profile's version
  (15.2.0-16ubuntu1), from `dpkg -S` and `dpkg-query`. Their sha256 is recorded in every result's
  environment (`compiler_sha256`, `cc_sha256`).
- Every variable a Makefile conditional tests (OS, LIBNIFT_C_EXPORTS, NIFT_DRY, …) is refused
  from the environment like the `?=` variables; `OS=Windows_NT` would switch to static libstdc++,
  `.exe` and other link flags. The link line's variables (`$(CXX) $(CXXFLAGS) $(LDFLAGS)
  $(LDLIBS) $(TARGET)`) as the campaign's make sees them must equal make's own derivation.
- test-sha256's own count is its unit steps' `sha256 unit: N checks, M failures` lines (the
  builtins step prints `fail=1`, a flag).
Guard cases: wrapper script, other compiler package, OS=Windows_NT, link line differs or cannot be
derived.

**C22 (found while freezing C21).** Reading the Makefile rewrites the libffi toolchain signatures
and the object configuration signatures when their content changes. My own C21 probe ran the link
derivation with `OS=Windows_NT`, which rewrote `.build/libffi/*.nift-toolchain-signature` (back to
the same Linux text on the next read, but newer than the libffi stamp), so `make -q nift` reported
the tree out of date. I restored their mtimes to the stamp's (`touch -r`; the content is the
current toolchain's, unchanged), and `make -q nift` = 0 with the binary still `91a49efa…`. Fix:
every preflight derivation now reads the Makefile under `make -n` (the Makefile's own NIFT_DRY
mode, which only marks stale signatures instead of writing them) and parses the echoed command.
Guard: with `OS=Windows_NT` the derivation returns `nift.exe` and the 8 signature files keep
their mtimes.

## 20. test-runtime2-cp01-simplify flakiness: root cause and test-only fix (C23)

**Reproduced (old harness, `ONLY=basic` of ordinary.py, 5-6 runs each):** the candidate failed 6 of
6, fc987ec1 4 of 5, 17f7964c 4 of 5, v4.12.0 3 of 5, weekend/r2 2 of 5. The signatures vary from
run to run: `S3b ... rebuilt ['index', 'p0', 'p1'] expected ['index', 'p1']`, `S4a create
unreferenced file: rebuilt ['p1']` (or `['index', 'p1']`), `S4e edit removed dependency (no
rebuild): rebuilt ['p0']`, `S8e no-op after mode switches rebuilt [N, 0, 0, 0, 0]`, and settle
`after-edit-all:[ 20 1 0 ... ]`. Every failure is an extra rebuild; no run produced stale output
(the clean-build oracle and status checks never failed). Logs: `scratchpad/combined-fix/cp01/`.

**Root cause: a test assumption, not runtime nondeterminism.** Probe (`cp01/probe.py`, 30 runs: 4
showed it): the page-info written by the build that follows an edit carries exactly the edit's
mtime (`info - dep = 0.000 ms`). Timestamps here come from a coarse 4 ms clock (multigrain
timestamps: a stat'ed file gets a fine stamp and later writes in the same tick get that same
value), and a nift build of a few pages finishes inside one tick. The v4.12 modified rule (and
hybrid's) rebuilds when a dependency is not older than its page-info, so the next build rebuilds
that page once more: the documented conservative same-tick behaviour. The harness's 50 ms sleep
before each edit only separates the edit from the PREVIOUS build, never from the next one; settle
edits all pages and builds at once by design. No src/ change is needed or made.

**Fix (test-only, no sleeps, timeouts or retries added):** `tests/runtime2/cp01/ticks.py`
sequences an edit: after writing, it reads the filesystem clock (the mtime of a probe file it
writes, T) and stamps the edited file T - 1 ns; every later write, including the next build's
page-info, is stamped at least T because the clock never goes backwards, so the edit is strictly
older than the build whatever the scheduling. ordinary.py's `edit()` and settle.sh (after editing
all 20 pages, then still checking the edits are newer than the initial page-info) use it. The
edits stay real content changes. The pre-existing 50 ms sleep before an edit (separation from
the previous build, checked fail-closed by the harness) is unchanged.

**Regression test** (`tests/runtime2/cp01/tick_order_selftest.py`, run by cp01_simplify.sh): it
forces the race deterministically with `CP01_WORST_CASE_TICK=1`, which stamps every page-info a
build writes with the earliest legal time (the fastest possible build). With the old edits
(`CP01_EDIT=unsequenced`) the original failures appear every time (S3a/S3b/S4a/S4e/S8e
`rebuilt [10, 0, 0, 0, 0]`, settle `[20, >=1, 0, 0]`); with sequenced edits there are none and
settle is `[20, 0, 0, 0]`.

**Determinism (fixed harness, full gate on the candidate):** 20 consecutive runs, 3 under
`taskset -c 0`, 1 under `nice -n 19` with two CPU-bound processes on both cores, 2 with one
CPU-bound process, 2 with `make -j2`, and 2 pairs run concurrently: 32 of 32 PASS. The fixed
`ONLY=basic` scenarios pass 5 of 5 on each of weekend/r2, v4.12.0, 17f7964c and fc987ec1.

## 21. test-sha256 flakiness: same cause, real content changes, no clock dependence (C24)

**Reproduced:** `[hybrid] up to date after rebuild` failed in 1 of 6 candidate runs and 2 of 5
fc987ec1 runs (§17.1), and `[modified] up to date after rebuild: 1 of 1 tracked file needs
rebuilding` failed in every run on every binary. Cause: the CP01 mechanism (§20). `sleep 1.1`
before the edit only separated it from the previous build; `printf 'v2' > data/x.bin; nift
build` then wrote the page-info in the edit's timestamp tick, so the modified rule (always, the
build being fast) and the hybrid rule (when it is fast) still saw the dependency as not older.

**Fix (test-only):** the test no longer waits for the clock. Every input written before a build
is sequenced with `tests/runtime2/cp01/ticks.py`: `--age DIR` first moves every file of the
project (and of the external directory) 10 s into the past, all by the same amount, so their
order and each page-info's relation to its dependencies are unchanged, and the edited files are
then stamped T - 1 ns (T: the filesystem clock now): strictly newer than everything the previous
build wrote, strictly older than everything the next build writes. All four `sleep 1.1` are gone.
The edits are real content changes (v1→v2, e1→e2, a new maybe.bin, t1→t2, o1→o2); the hash-mode
`touch` without a content change is sequenced the same way (newer mtime, same content).

**Regression test** (in the test itself, modified and hybrid): it forces the fastest build by
stamping the page-info with the earliest legal time. An unsequenced edit (the old test) leaves
the page "needs rebuilding" every time; a sequenced edit is up to date and built.

**Determinism:** 20 consecutive `make test-sha256` runs, 3 under `taskset -c 0`, 3 with two
CPU-bound processes, 2 run concurrently with a second copy: no `[mode]` line fails in any run,
and the failure fingerprints of all 28 make runs are equal (the 21 remaining FAIL lines are the
deterministic VM refusals: bytes(), open_bytes, builtin arity, fn-name-reserved).

## 22. test-runtime2-scanner-parity flakiness: three causes, test-only fixes (C25)

Before: two runs on one tree differed (§17.1): v_preempt, v_thread_order, v_sleep_ties
(tests/runtime2/cp22/vm-only), socket_proof.f, and for-s2/t27. Investigated separately:

- **Thread/task ordering (cp22 VM-only fixtures).** These fixtures define the VM's
  deterministic scheduling (thread order, FIFO wake-up, no preemption, timer ties), and
  test-runtime2-cp22 runs them with `--mode vm-only` against their recorded `.vm.*`
  expectations: ordering IS part of the VM contract and stays asserted there,
  deterministically. Against the legacy reference (real threads) their order and even their
  results (`v_preempt`: true or false) vary by design, so a legacy-parity comparison has no
  stable answer. The scanner now counts and names them (`7 cp22 VM-only fixtures left to
  test-runtime2-cp22 --mode vm-only`) instead of comparing them. This is scoped by the fixture
  directory's documented contract, not by a gate name.
- **The socket probe** already binds port 0 and connects to the port `getsockname` returns
  (it never picks a port itself), so nothing assumes a port stays free; it prints that
  allocated port, so the reference's output differs every run. It is listed in
  `tests/runtime2/scanner_parity_masks.tsv` (with the reason) and compared and reported with
  digits masked. The candidate still fails it (a VM refusal), with a deterministic report.
- **for-s2/t27 and others: job messages.** When a reference run is killed by `timeout`, bash
  prints `line N: PID Killed ...` asynchronously, inside a later script's FAIL output, with a
  pid. The runs' own shell messages now go to a file; the exit status (124/137) is still
  compared. Reported diffs are the masked ones when the reference is nondeterministic.

No timeout was lengthened and nothing is retried. **Regression test**
(`tests/runtime2/scanner_parity_selftest.sh`, run first by the gate): fake candidate and
reference binaries reproduce the three causes (random thread order, a random port, a reference
that kills itself); with the old behaviours (`NIFT_PARITY_UNFIXED=1`, only in the restricted
self-test mode `NIFT_PARITY_SCRIPTS`, which always fails as a certification run) runs differ;
fixed, 6 runs are byte-identical, no job message leaks, both real differences are reported and
the VM-only fixture is counted.

**Determinism:** three full gate runs on the candidate against the v4.12.0 reference (two plain,
one under `taskset -c 0` with a CPU-bound process): equal failure fingerprints (1517 failing
scripts and sites each; before the fix, two plain runs differed).

Also in C25: test-sha256's forced-race block compares `status` output without a pipeline
(test-integrity reported the pipe; findings are back to 57).

## 23. Item 2: the three stopped gates get extractors; rt-diff classification (C26)

Harness and tests only; `src/` and the binary (`91a49efa…`) unchanged. STOPPED is now empty and all
16 expected-baseline gates have extractors and selftest rows.

**Runner-level diagnostics, the same on every tree** (`certify.runner_env`, from the runner's own
checkout; the trees' scripts are unchanged, so baseline logs are produced the same way):
- `scripts/certify_errtrace.bash` through `BASH_ENV` (test-v44-language-foundation): when a command
  makes a bash script exit under `set -e`, it prints `CERTIFY-ERR <script>:<line> rc=<status>
  cmd=<command>`; nothing for failures the script handles. The silent failure is
  `v44_ast_expression_smoke.sh:52 rc=1` (the `while(9007199254740993 == 9007199254740992)` probe;
  category from the status: exit, signal:N, or timeout for 124). Recipe lines run on their own
  (2, 4, 5, 8-14, 16 fail) each have a fingerprint: CERTIFY-ERR cases, the differential corpus's
  FAIL cases, or the step's refusal (step and source position).
- `scripts/certify_site/sitecustomize.py` through `PYTHONPATH` (test-rt-vm): vm_parity's
  `shutil.rmtree` of its work directory first copies rt-diff's report to `<log>.kept` (outside the
  deleted directory) and prints `CERTIFY-RTDIFF <entry> <channel> <first difference>` per
  unapproved difference and `CERTIFY-RTDIFF-SUMMARY differing=N` (the gate's own count). vm_parity's
  category totals are not compared. vm_modules (`probe:export_collision`, `d041:export_clash`) is
  an ordinary FAIL extractor.

**test-runtime2-host-deps: the approved read_limit.py case-ID table** (`READ_LIMIT_TABLE`, 27
entries, one per `check()` call). A FAIL line of read_limit.py must match exactly one entry; the
case is the entry and its name arguments, the exit code in the name is its status; durations, RSS
figures and the cut stderr detail are not compared. A line matching none or several is UNMAPPED
and blocks: its fingerprint never equals anything, itself included. Guard: a selftest control
and a toy (`um`: identical unmapped line on candidate and regression baseline → FAILED-NEW). All 33
real failures map.

**host-deps was flaky too (found here).** `host_deps.py` (`[modified] page(x).metadata edit: not up
to date after building`) has the CP01 cause: it rewrites inputs within the same second on purpose,
and in the mtime-based modes the following build could share the edit's tick. In modified and
hybrid modes each step's edits are now sequenced with `ticks.py` (project and external directory
aged as a whole, edits stamped just before the clock); hash mode keeps the raw same-second edit
(its point). A forced fastest-build check per mtime mode reproduces the old failure with an
unsequenced edit and passes with a sequenced one.

**Cross-binary equality** (this tree's harness, candidate vs fc987ec1 binary): language-foundation
(1 case) and host-deps (33 cases) equal. (The rt-diff cross-binary claim first written here was
wrong; see §23.1, corrected in §25.)

### 23.1 rt-diff's unapproved differences: 30, not 14 (CORRECTED in C28, see §25)

Count: rt-diff's own summary says `differing: 30, approved: 16` (817 entries): 30 entries differ
without an approval; 16 others are approved. The earlier "14" was 30 - 16, a misreading.

The classification first written here ("identical on fc987ec1, 17f7964c and weekend/r2 … none is
introduced by B3-B5 … historical incompatibilities") was WRONG and is withdrawn: rt-diff runs each
entry with a minimal environment, so `NIFT_VM_BIN` never reached `tools/rt-vm/nift-vm`, whose
B side therefore always ran this checkout's `./nift`; every "per-binary" run compared that
binary's legacy side with the candidate's VM. Found by the review of 4ec1aa80 (F1). The
corrected evidence and classification:

All 30 entries, each binary through this tree's (fixed) vm_parity, legacy side `NIFT_VM=0` and VM
side `NIFT_VM=1` of the same binary: the candidate and fc987ec1 show the same 30 differences
(identical first differences, every channel); 17f7964c and weekend/r2 refuse all 30 on the VM
(reference-guard / aliasing / declare-const) and fall back to legacy, so they are legacy-equal. The
differences were introduced by B5 (fc987ec1) and are unchanged by the H-A fix. **All 30 are
approved B5 changes; none is a historical incompatibility and none is a candidate defect.**

| # | Entry | First difference: legacy / VM | fc987ec1 VM | 17f7964c VM | weekend/r2 VM | Cause | Class |
|---|---|---|---|---|---|---|---|
| 1 | `gen/g00002/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 4}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 2 | `gen/g00008/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 16}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 3 | `gen/g00024/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 2}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 4 | `gen/g00028/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 4}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 5 | `gen/g00030/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 21}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 6 | `gen/g00034/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 4}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 7 | `gen/g00045/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 10}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 8 | `gen/g00060/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 10}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 9 | `gen/g00071/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 31}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 10 | `gen/g00073/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 15}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 11 | `gen/g00101/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 8}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 12 | `gen/g00112/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 3}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 13 | `gen/g00114/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 2}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 14 | `gen/g00118/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 2}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 15 | `gen/g00125/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 2}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 16 | `gen/g00131/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 16}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 17 | `gen/g00133/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 22}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 18 | `gen/g00139/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 8}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 19 | `gen/g00159/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 3}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 20 | `gen/g00169/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 8}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 21 | `gen/g00170/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 3}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 22 | `gen/g00181/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 2}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 23 | `gen/g00189/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 12}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 24 | `gen/g00190/main.f` | `{"a": "[1,2]", "b": "[1,2,99]", "line": 32}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 25 | `semantics/copy_at_call.f` | `{"a": "[1]", "b": "[1,9]", "line": 2}` | same difference | legacy-equal (refused: fallback:reference-guard:argument) | legacy-equal (refused: fallback:reference-guard:argument) | B5 shared references: a callee's push to an argument is visible to the caller (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 26 | `semantics/copy_at_return.f` | `{"a": "[1]", "b": "[1,5]", "line": 1}` | same difference | legacy-equal (refused: fallback:aliasing) | legacy-equal (refused: fallback:aliasing) | B5 shared references: a returned array stays shared with the caller's value (D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |
| 27 | `smoke/location_method_receiver/002-missing/missing.f` | `{"a": [1], "b": [0]}` | same difference | legacy-equal (refused: fallback:aliasing) | legacy-equal (refused: fallback:aliasing) | B5 shared references: an extracted element stays valid after its source is cleared (compatibility.md; expected-changes.tsv class i), so `b.push(2)` runs | approved B5 change |
| 28 | `smoke/v44_element_assignment_smoke/003-const/const.f` | `{"a": [1], "b": [0]}` | same difference | legacy-equal (refused: fallback:declare-const) | legacy-equal (refused: fallback:declare-const) | `const` is binding-only (owner ruling Q2 option B, b5-refsem.md section 1): `const a := [1, 2]; a[0] = 9` runs | approved B5 change |
| 29 | `smoke/v44_root_path_reference_smoke/001-missing/missing.f` | `{"a": "error: $ROOT/missing.f:4:7: reference target no longer exists: b", "b": "error: $RO` | same difference | legacy-equal (refused: fallback:aliasing) | legacy-equal (refused: fallback:aliasing) | B5 shared references: the extracted element survives `a.pop()` (`print(b.stringify())` gives `[2]`); `print(b)` of an array then gives legacy's own print error, which legacy also gives for any array (`x := [2]; print(x)`) | approved B5 change |
| 30 | `vm-corpus/cp11/arrays.f` | `{"a": "4", "b": "5", "line": 7}` | same difference | legacy-equal (refused: fallback:reference-guard:shared) | legacy-equal (refused: fallback:reference-guard:shared) | B5 shared references: `grow(a)` pushes to the caller's array, `5/4 becomes 5/5` (weekend-refsem-a section 11, D-NEXT-refsem-b, b5-refsem.md section 1) | approved B5 change |

#29 in detail: `a := [[1], [2]]; b := a[1]; a.pop(); print(b)`. Legacy: `reference target no
longer exists: b`. Under B5's shared references `b` keeps `[2]` (`print(b.stringify())` prints `[2]`
on the candidate and fc987ec1); `print(b)` of an array then fails with `print: value is not
directly renderable`, which is legacy's own error for printing any array (`x := [2]; print(x)`
gives it on legacy and VM alike). So the approved B5 behaviour (the element survives) followed by
an unchanged, legacy-equal print rule: not a defect.

None has an rt-diff approval policy, which is why vm_parity fails on the candidate and fc987ec1
(17f7964c and weekend/r2 refuse them instead). Encoding them as rt-diff approvals is an owner
decision; until then test-rt-vm is FAILED-NEW against 17f7964c and weekend/r2. Data:
`scratchpad/combined-fix/rtvm2/` (kept reports, per binary).

## 24. Review of C21-C25 (a977376e): ACCEPT-WITH-CONDITIONS; follow-ups (C27)

Root cause, mutants and regression tests confirmed, and the cp22 VM-only exclusion. Follow-ups:
- **Port mask narrowed.** socket_proof.f is listed with mask `port`: only `port <digits>` is
  replaced (`port <PORT>`), every other digit is compared; the selftest checks the masked line.
- **No sleep left in ordinary.py's tick handling.** The reviewer confirmed the 50 ms pause was
  load-bearing (it separated an edit from the previous build). `edit()` now ages the whole site
  first (`ticks.age`, order kept) and then sequences the edit; the generated sites are sequenced
  before their first build; S8c's config switch is sequenced like an edit; the other tick sleeps
  are gone. tick_order_selftest still passes (and still reproduces the old failures unsequenced); the gate passed 6 of 6 runs after the change (4 plain, 1 `taskset -c 0`, 1 with a CPU-bound process).
  The only `time.sleep` left is the interrupt scenario's kill delay (it must interrupt a build).
- **GCC and loader redirection refused.** The preflight refuses COMPILER_PATH, GCC_EXEC_PREFIX,
  LIBRARY_PATH, CPATH, C_INCLUDE_PATH, CPLUS_INCLUDE_PATH, OBJC(PLUS)_INCLUDE_PATH,
  GCC_COMPARE_DEBUG, DEPENDENCIES_OUTPUT, SUNPRO_DEPENDENCIES, LD_LIBRARY_PATH, LD_PRELOAD and
  LD_AUDIT in the environment (guard cases for three). §14.1's hold list adds the arch-named
  `gcc-15/g++-15/cpp-15-x86-64-linux-gnu` packages.
- **Record correction (C22, §19).** The libffi signature rewrite was caused by environment probes
  that read the Makefile: my `OS=Windows_NT` probe and also the reviewer's `LIBFFI_CFLAGS` and
  `CFLAGS` probes. Since C22 every preflight derivation reads the Makefile under `make -n`, and the
  tree is verified clean (`make -q nift` = 0, binary `91a49efa…`).

**Release audit (record only; the owner closed diagnostic-list growth):** the scanner self-test's
unfixed mode restores the three old behaviours together and does not show each cause on its own;
three rare diagnostics are not in the shared list: a bare `KeyboardInterrupt`, `SystemExit: N`
and `terminate called without an active exception`.

## 25. Review of C26-C27 (4ec1aa80): REJECT; fixes (C28)

Harness and tests only; `src/` and the binary (`91a49efa…`) unchanged.

**F1 (fixed): rt-diff's VM side and the evidence.** `vm_parity.py` now writes its own B-side
wrapper (`NIFT_VM=1`, then the `--nift` binary) as it already did for the legacy side, and
`cp25/inventory.py` does the same for both its sides: rt-diff's minimal environment had dropped
`NIFT_VM_BIN`, so `tools/rt-vm/nift-vm` always ran the checkout's `./nift`. No gate's evidence was
affected (every gate passes the checkout's own binary; test-rt-vm-sanitize runs vm_parity without
`--rt-diff`); only my cross-binary evidence of §23.1 was, now redone and corrected there (30
approved B5 changes, introduced by fc987ec1; 17f7964c and weekend/r2 refuse them; #28 is Q2's
const-is-binding-only; #29 is the surviving element plus legacy's own print rule).

**F2 (fixed): host-deps table cases diagnose what is printed under them.** For a read_limit.py
case the line's own detail and every line under it are compared, narrowly normalised, with every
shared-list diagnostic in them. Nine `check()` calls print a fixed-size tail of the output whose
start moves with the length of the temporary paths in it, so `certify.run_target` now gives every
run of every tree (candidate, reruns, baselines, unprivileged copy) a TMPDIR of the same length
(`nift-certify-<7-digit pid>/<16 hex>`): the tails are cut at the same offset everywhere. Shown:
three runs with different equal-length TMPDIRs and one with the fc987ec1 binary give equal
fingerprints (32 cases). Selftest: an internal failure, a Segmentation fault, a VM refusal and a
Traceback under `FAIL open one byte over…`, and a replaced detail, each change the fingerprint.
Also found: `read_limit.py`'s `page build: up to date afterwards` had the CP01 same-tick flake
(failed in 2 of 4 runs); its three edits are now sequenced with `ticks.py` (passed 3 of 3 after).
Corrected in C29: its regression coverage was only indirect (host_deps.py's forced race uses the
same helper and rule); read_limit.py now has its own direct forced-race check (an unsequenced edit
needs a rebuild, a sequenced one is up to date, after the fastest build).

**F3 (done): checks hidden by an abort are reported (D3, required, "aborted by earlier failure").**
`certify_fingerprints.not_executed` lists them from the failing logs (the gate's and each failing
recipe line's), on the candidate and every baseline alike; certify adds them to the gate's
not-executed assertions. Named checks come from `ABORT_CASES` (line, name), hidden when after the
line the script stopped at (CERTIFY-ERR, or a Python traceback's module-level frame); a script
without named checks reports "every check after line N". test-v46-relative-imports now also gets
the errtrace hook so its stop line is known. The abort-hidden checks on the candidate:

| Gate | Script (stops at) | Checks never run |
| --- | --- | --- |
| test-v44-language-foundation | v44_ast_expression_smoke.sh (line 52) | big-int equality (its check), large-value compound, live binding, precedence |
| | v44_element_assignment_smoke.sh (line 25, recipe line run alone) | array element assignment, object member assignment, nested grid assignment, compound element assignment, element read-after-write, out-of-range element assignment, const array element assignment |
| | v44_root_path_corruption_reproducers.sh (line 20) | 1 destructuring then parent growth (its check), 2 nested extraction then growth, 3 array of struct handles, 4 map of struct handles, 5 @for over a mutated map, 6 @for over a mutated array, CP34 escaped reference, CP34 loop-scope reference, CP34 heavy reallocation |
| | v44_cp2_variadic_functions, v44_struct_variadic_methods, v44_cp3_variadic_lambdas, v44_cp4_spread, v44_cp5_generalized_native, v44_cp6_cp7_glob_filesystem, v44_cp9_map_markup smokes (`#!/bin/sh`, `set -eu`: stop at a VM refusal; sh has no BASH_ENV hook, so no stop line) | every check after the failing command |
| | v44_ast_differential_corpus.sh | none hidden (it runs every case and fails at its end; `ABORT_CASES` marks it complete) |
| test-v46-relative-imports | v44_relative_import_ownership_smoke.sh (line 69) | every check after line 69 (the script names none) |
| | v46_import_worker_ownership_smoke.sh (recipe line run alone; no stop line printed) | every check after its failing command |
| test-v44-packages | package_transaction_smoke.py (line 330) | every check after line 330 (unnamed asserts: child-update provenance and the rest) |
| test-runtime2-cp03-simplify | cp03_missing_tracer.sh (explicit exit at line 9) | every check after its failing command (the same case's remaining checks) |

None of them was run another way; accepting fail-fast scripts is the owner's decision. Shown end
to end: certify on test-v44-language-foundation and test-v46-relative-imports lists these 29 as
REQUIRED not-executed assertions (`scratchpad/combined-fix/item2/certify2.log`).

**Release-audit items fixed.** The BASH_ENV hook no longer sets errtrace (a script's own ERR
trap no longer fires inside its functions; the selftest checks three trap/errexit shapes keep their
exit status and output under the hook). Corrected in C29: without errtrace, errexit inside a
function exits silently (no CERTIFY-ERR line); that case falls back conservatively (no fingerprint,
so FAILED-NEW, and "every check after its failing command" not executed). The failing commands
seen so far are top-level ones and are reported. The sitecustomize hook catches every `Exception`.


## 26. Confirmation of C28 (c8817214): CONFIRMED-WITH-CONDITIONS; conditions (C29)

- **C1, host-deps cut tails.** An equal-length TMPDIR aligns the cut offset but not the characters
  there: a cut inside a path leaves a fragment with the mkdtemp suffix or the per-log hash and no
  leading `/tmp/`. The first token of each table case's own detail (the one a cut can split) is
  now dropped. Selftest: the review's two read_limit.py runs (equal-length TMPDIRs differing in one
  character) give equal fingerprints.
- **C2, D3 strict default.** A REQUIRED not-executed assertion now fails a `--certify` campaign
  whatever its gate's class, FAILED-BASELINE included (`CAMPAIGN FAILED: required assertions not
  executed in: …`). Guard control `ab`: a FAILED-BASELINE gate whose script aborted. The owner may
  later accept named hidden checks. Consequence to note: test-test-integrity's
  `[pipefail-missing] … "$skip"` finding is read as a required skip (R5-6 noise), so that gate, if
  FAILED-BASELINE, also fails a campaign until its labelling is fixed or accepted.
- **C3.** The two record claims are corrected (§25): errexit inside a function exits silently
  (conservative fallback); read_limit.py has a direct forced-race check now.

## 27. D2: operand evaluation order outside receivers (C30)

Owner rulings 2026-11-08 03:27/03:29 on the bounded investigation (`scratchpad/d2/D2-REPORT.md`):
operands are evaluated once, left to right; binary operators and compound assignment stay
separate. Production change; new binary `f2fed67d211eebd8b511c41fc63f01342e70a501f3475306b4ebba38a442419a`
(GCC 13.3 here; the previous candidate binary was `91a49efa…`).

**Family A (compiler_expr.cpp).** `binary()` and the fused `false_jump` comparison call
`retain_receiver(left, left < mark, node.b)` between the operands: a left operand held in its
binding's own register is copied to a temporary when the right operand can rebind it
(`may_rebind`, unchanged and not narrowed for built-ins). Constants (`kConstantBit`) and
temporaries are never copied.

**Family B (new compiler_compound.cpp, `compound_snapshot`).** Taken by `compound_op` only when
the rhs exists, is not the `AddI` immediate and `may_rebind(rhs)`; otherwise the old code runs
byte for byte (the in-place `s += t`). Steps:
1. snapshot `t`: own register `Move t, x`; global `CheckGlobal` (loop code) then
   `GetGlobalChecked`; cell, upvalue or session binding `read_binding`;
2. the rhs (it may rebind `x`; `t` is unaffected);
3. the operation on `t` at the statement's position (`AddAssign`/`Sub`/`Mul`/`Div`/`Mod`), the
   same `context:assignment-error` refusal;
4. the unchanged binding-type check, then the write-back: `CheckType t` + `Move x, t`, a plain
   `SetGlobal` (no take flag: nothing was taken), or `store_binding` (cells check in the store);
5. `LoadNull t`.
The captured operand (`t`), the binding as the rhs left it, and the write-back stay separate: a
fatal failure in 3 or 4 writes nothing, so the binding keeps what the rhs left (no rollback, no
partial commit). An rhs that fails before returning leaves its own rebinding (fixtures b5, b9).

*String fast path (2b).* With only the steps above, a string `s += f(i)` copies `s` on every
append (the snapshot shares it): 20,000 appends cost 390.4 M Ir against 17.0 M before
(quadratic). So for `+=` with string kinds on an own register or a non-cell global, after the rhs
`JmpNotEq t, x` checks whether the binding still equals the snapshot; if so `t` is dropped and the
old in-place path runs on the binding (`AddAssign x, v`, or `global_take`/`global_put`), which
gives the same result because the operands are equal; otherwise the snapshot path runs.
`compare.cpp`: identical string objects compare equal without reading them (keeps the check O(1)
on the common path). Cells and sessions keep the snapshot path: they were never in place (a
400,000-step session or cell `s += …` loop is quadratic before and after D2, 3.9 s / 9.4 s on
both 91a49efa and f2fed67d; pre-existing, not changed here).

**Family C (compiler_structs.cpp).** `field_update` reads the receiver and the field
(`FieldGet`) before the rhs, then computes, `CheckField`, `FieldSet`. A receiver-rebinding rhs
stays refused (`struct-assign`, fixture c3).

**Not changed (out of scope, per the rulings):** compound on index or field-key targets (refused;
the generator checks that), `may_rebind` for built-ins, `[x, g()]` (already left to right,
fixture e1), `const_write` (rhs first, then the const read: a const cannot be rebound), and no
other expression lowering (none was needed).

**Pre-existing, reported, not changed (reworded in C34, review of 3b513751 L1):** on the in-place
path (and on the unchanged-binding path 2b, which runs it) a string-append memory fault is
detected after the append (`memory_ok` after `add_assign_slow`, vm.cpp), so the binding already
holds the appended text when the fault is raised; on the snapshot path the fault comes before
the write-back, so the binding holds what the rhs left. The two paths would therefore leave
different binding states if anything ran after the fault. That nothing can is **unproven**: in
every trigger found (scripts; sessions with a plain `s += w` and a rebinding `s += k()`,
`NIFT_VM_HEAP_BUDGET` 2-5 MB) the budget stop exits 70 and ends the run, so no later statement
reads the binding. (Sessions never take the in-place path: a session binding is a cell.) The two
session triggers are pinned in `d2_append.py` (exit 70, no later statement runs); release audit
item 27.5 (L1).

### 27.1 Legacy differences introduced (approved) and changed admitted outputs

| Fixture | Legacy | 91a49efa | Now | Status |
| --- | --- | --- | --- | --- |
| d2/b7 loop compound, `fn` callee (`x += g()`, `t += kf()`) | `24`, `Za` | `24`, `Za` | `10`, `aa` | APPROVED difference (owner 03:27/03:29). Consequence (L4): an overflow on the target read first is now a VM error where legacy succeeds (`x := 9223372036854775807`, `fn(g()) { x = 5; return 1 }`, `x += g()` in a loop: VM overflow at the statement, exit 1; legacy `6`) |
| d2/b7 loop string `+=`, lambda callee | `Z` (append lost) | `Za` | `aa` | APPROVED difference |
| d2/b3 overflow after a rebinding rhs | fatal overflow 4:7 | `6 6 5` exit 0 | legacy's error | changed admitted output, legacy-equal (approved) |
| d2/b6 `/=` on an int binding | binding-type error 5:1 | `before 5` | legacy's error | changed admitted output, legacy-equal (approved) |
| d2/b5 session faults | `20`, overflow, `5`, `Z`, `24` | `20`, `6`, `Z`, `120` | legacy-equal | changed admitted output |
| d2/b8 r05 (lambda call argument in loops) | `6 20 6` | `6 6 6` | `6 6 6` | recorded only (compatibility.md), no change |

Recorded in `compatibility.md` (D2 section) and `evidence/b5/expected-changes.tsv` (7 rows).
Error texts and positions: every error row of the D2 matrix (22) gives legacy's error line exactly.

### 27.2 Tests

- `tests/runtime2/b5/d2/` (17 fixtures; in test-runtime2-b5 on the production and guard-off
  builds and in the evalorder sanitizer lane): a1-a4 family A, b1-b9 family B (contexts, session,
  faults, `/=`, approved loop differences, r05, an rhs that rebinds then throws), c1-c3 family C,
  e1 `[x, g()]`.
- `tests/runtime2/b5/gen/gen_operators.py` + `operators.tsv` (in test-runtime2-b5-evalorder):
  40 operations x callee (lambda, `fn`, inline assignment) x context (top, loop, fn global,
  lambda global, fn cell, captured, loop binding, session, fn local): 880 programs, 85 refused,
  795 admitted (the floor), 0 bad; a model (left to right; compound = read, rhs, write back; int
  `/=` = legacy's error); legacy cross-check with error lines compared; known legacy classes
  loop-compound 8, loop-args 3, no-field-op 20; index compounds must stay refused. Negative
  controls: a missing tree and a truncated table exit 2 and write nothing. The pre-D2 binary
  fails it (381 bad).
- `tests/runtime2/b5/d2_append.py` (in test-runtime2-b5): 1,200,000 `s += piece(i)` at top level,
  in a `fn` and in a lambda on a global: about 0.1-0.2 s; the copying variant (mutant d8) times out
  at 60 s.
- Mutants (`mutants.sh`, `-DNIFT_D2_MUTANT=n`), all caught: d1 binary without the retain, d2 the
  fused jump without it, d3 compound target read after the rhs, d4 an own register's snapshot is
  the register (local targets read late), d5 number compound through the restore form (caught by
  d2/b5: the overflow would roll back), d6 field_update's old order, d7 the in-place append
  without the unchanged-binding check, d8 no unchanged-binding path (caught by d2_append). M14
  (`may_rebind` false) now also fails the d2 fixtures. Two candidate mutants were found
  equivalent and not used: dropping `LoadNull t` (one extra copy at most: the next snapshot
  overwrites `t`) and writing back before the binding-type check (a failed check is fatal and the
  session forms that could show it are refused, `session:binding-shape-contract`).

### 27.3 Cost (callgrind Ir, deterministic; 91a49efa -> f2fed67d)

| Program (loop of N) | N | before | after | change |
| --- | --- | --- | --- | --- |
| `s += "x"` (no call: old path) | 20,000 | 10,218,635 | 10,218,842 | 0 |
| `s += piece(i)` (`fn`, unchanged binding: fast path) | 2,000 / 20,000 | 4,401,014 / 16,983,388 | 4,943,475 / 22,385,836 | +270 Ir per append (linear) |
| `s += piece(i)` without the fast path (mutant d8) | 20,000 | 16,983,388 | 389,924,846 | quadratic |
| `x += f(i)` (numbers) | 20,000 | 12,935,552 | 14,995,941 | +103 Ir per step |
| `y = x + f(i)` (family A retain) | 20,000 | 16,794,506 | 17,574,765 | +39 Ir per step |

Programs: `scratchpad/combined-fix/d2run/cost/`.

### 27.4 Gate results (binary f2fed67d, GCC 13.3; local, not certification)

Separate `make -k` runs, logs `scratchpad/combined-fix/d2gates/` (results.txt). The runs after C31
and C32 are on the same binary (those commits change no `src/`).

| Class | Gates |
| --- | --- |
| PASSED | test-runtime2-b5 (140 production / 145 guard-off fixtures incl. d2; sort-refusals 564; d2-append 3); test-runtime2-b5-evalorder (evalorder 840/830 admitted, 0 bad; operators 880/795 admitted, 0 bad; both negative controls); test-runtime2-b5-generators (gen1 20,274, gen_struct 1,911, 0 bad); test-runtime2-b5-mutants (1, 3, 4, 6, 7, 8, 11-15 and d1-d8, all caught); test-rt-lint; test-runtime2-certification-guard (after C32); test-runtime2-b4b-gen-alias (12,600, 5,532 on the VM, 0 bad); test-runtime2-b4b; test-runtime2-guardoff; test-runtime2-cp12c-probes; test-runtime2-cp16-engine-parity; test-runtime2-rt-diff; every test-rt-vm prerequisite except cp14b (b3, b3-mutants, b4, b4-mutants, refsem(-mutants), collections(-mutants), cp22, coro-module, cp17, d040, capture-origins, for-s2, closures-s1, safety(-mutants), vm-default, args-cmd, packages, b5-evalorder, b5-snapshot-memory, ...); test-rt-vm's own recipe up to vm_modules, with **vm_parity --rt-diff now PASS on its own merits** (817 entries, 0 differing, 46 approved: C31), vm_adversarial 39,612 programs 0 non-approved; the 7 recipe lines after vm_modules run one by one: all PASS |
| FAILED-BASELINE (as before) | test-runtime2-cp14b (`script_return_kernel_error`, known pre-existing); vm_modules in test-rt-vm's recipe (2 cases `import-scope:collision`); test-test-integrity (57 findings, unchanged); test-runtime2-rt-diff-selftest (its FAIL set is unchanged; the new D-NEXT cases pass) |
| FAILED-NEW | none |
| Not run here | ASan/UBSan (test-runtime2-b5-evalorder-sanitize now includes d2 and gen_operators), TSan, the GCC 15.2 comparator lane, scanner-parity (needs NIFT_REF): the Linode lane |

### 27.5 Review of 3b513751: ACCEPT-WITH-CONDITIONS; C34 and the release audit

Review: `scratchpad/review-ha/REVIEW-D2.md`. C34 changes no `src/` (binary still f2fed67d).
- **C1 (fixed): every hidden-check row is Category A.** `certify_hidden_checks.tsv` used B for
  v46_import_worker_ownership_smoke.sh (its header asserts worker isolation: concurrency safety),
  the package and module-import smokes, cp03 (incremental builds), cp10 (fs authority) and the
  perf-scaling smoke; with the owner's release requirements in the A definition (package imports
  on the VM, incremental builds, fs-root enforcement, superlinear hard stops, the website-build
  rule) and "when unsure, A", the remaining B rows were rechecked too: variadic functions and
  lambdas (rest arrays, captures), struct variadic methods (dispatch), spread (array sharing),
  generalized natives (dispatch), glob/filesystem (fs-root), structured wildcards (object
  pick/omit: B5 copies), map markup (captures; website-build rule). **No B row remains.** The
  B branch of the verdict stays guard-tested with a synthetic row; toy ab is now A (NOT
  RELEASE-QUALIFYING).
- **L4 (fixed):** the loop-compound approval text (compatibility.md, expected-changes.tsv, 27.1,
  gen_operators.py) states the consequence: an overflow on the target read first, with a `fn`
  callee in a loop, is now a VM error where legacy succeeds.
- **L1 (reworded, 27):** "unobservable" is now "unproven", with the differing states of the two
  paths; the two cheap session triggers are pinned in d2_append.py.

**Release audit (recorded, not fixed):**
- L1: in-place append memory fault: the binding holds the appended text when the fault is raised
  (fast path) vs the rhs's value (snapshot path); proven unobservable only for the pinned triggers.
- L2: mutant d4 is caught only through an exit-70 internal failure (its `LoadNull t` nulls the
  binding), not by a value assertion.
- L3: rt-diff's `D-NEXT-<name>` decision check is format only; it does not check that the
  decision record exists in decisions.md.
- L5: removing read_limit.py's `sequenced()` call sites is caught only flakily (the race).
- L6: a session `z /= k()` is refused (`session:binding-shape-contract`), as on 91a49efa
  (pre-existing).
- L7: C29 drops the first token of a host-deps table case's own detail (minor loss of
  information, accepted).

## 28. Owner decisions 2026-11-08 06:46: rt-diff approvals (C31), campaign verdicts (C32)

**1. The 30 rt-diff B5 differences are APPROVED (C31).** `tools/rt-diff/policies/b5-refsem-vm.json`
holds 62 exact rules in the existing format (entry id, source sha256, channel, exact old -> new,
count), one per differing channel of the 30 entries of section 23.1 (27 stdout+combined, two
exit+stderr+combined, one stderr+combined). Each rule's justification states "APPROVED owner
2026-11-08 06:46: an approved B5 semantic difference, not legacy parity", the cause (shared
references 26; extracted-element lifetime 1; const binding-only 1; the #29 rendering case 1;
a returned array stays shared 1), the legacy and VM text and its row in section 23.1; `test`
names the corpus file, the generator (`tools/rt-diff/gen.py`, seed 1) or the smoke script the
entry is extracted from. `decision` is `D-NEXT-refsem-b`: rt-diff's rule check accepted only
numbered rows (`D-009#6`), so it now also accepts an unnumbered decision record
(`D-NEXT-<name>`; rt-diff.md; selftest cases accept `D-NEXT-refsem-b`, refuse `D-NEXT-` and
`D-NEXT-refsem-b#x`). vm_parity passes the file and confirms every B5 approval by the entry's own
VM statistics (one VM execution, no legacy execution). On the D2 binary: rt-diff 817 entries,
0 differing, 46 approved, 0 unused policies; b5-refsem approvals 30, all confirmed. The rules were
generated from rt-diff's own run_one/compare (`scratchpad/combined-fix/rtdiff-approve/build_policy.py`),
each checked with `policy_matches` before it was written.

**2. Checks hidden by an early abort: PARTIAL certification (C32; replaces C29's C2 default).**
`certify.py` gives a campaign verdict with its own exit status: CERTIFIED 0, FAILED 1, PARTIAL 4
(exit 3 is gone; what it covered is FAILED). PARTIAL only when nothing failed (no FAILED-NEW, no
comparator skip, no NOT-RUN/SKIP gate, no gate blocked by anything other than unexecuted required
checks, no FAILED-BASELINE under --strict) and the only gaps are listed REQUIRED checks that did
not execute. The summary lists each with its category; any Category A gap prints
`NOT RELEASE-QUALIFYING`; PARTIAL prints "PARTIAL is not a pass". The categories are the reviewed
table `scripts/certify_hidden_checks.tsv` (51 rows: every named abort-hidden check, and a `*` row
for every script of the four ABORT_CASES gates); a check without a row reads as A. Category A:
the expression-smoke checks, the element-assignment checks, the root-path reproducers and the
constant-fold smoke; B (as first written): the v4.4 variadic/spread/native/glob/wildcard/markup/
filesystem-authority smokes, the perf-scaling smoke, the module-import smokes, the package smokes
and the CP03 tracer cases. **Superseded by C34 (27.5): every row is A.**
Guard: toys ab (B hidden, FAILED-BASELINE: PARTIAL, exit 4, no NOT RELEASE-QUALIFYING), ac (A
hidden listed, NOT RELEASE-QUALIFYING), ad (hidden + FAILED-NEW: FAILED, exit 1), s1 (PARTIAL,
exit 4), an 11-case verdict table, and every named ABORT_CASES check must have a row.

**3. Category A hidden checks extracted (stopped at the stop condition; not committed).** The 21
Category A checks an abort hides in the expression, element-assignment and root-path scripts were
extracted verbatim (program and assertion) and run alone under legacy, the default engine and
`NIFT_VM=require` (`scratchpad/combined-fix/item3/extract_run.py`, `results.json`). Legacy: 21
pass. VM: 10 pass (large-value compound, live binding, precedence, out-of-range element store,
root 2, 5, 6, CP34 loop scope, CP34 heavy reallocation); 10 are refused before any effect
(big-int equality `prepared-unpreparable`, the five element checks `index-target`, root 1
`declare-pattern`, root 3 and 4 `context:struct-new`, CP34 escaped reference `index-target`); one
fails its original assertion: `const a := [1, 2]; a[0] = 9` runs without an error under B5's
binding-only `const` (owner ruling Q2 option B, rt-diff #28), where the original asserts an error.
Reported to the owner: how a refusal and this approved difference count.

## 29. Certification campaign plan (planning only; nothing provisioned)

Candidate: the frozen tip of `cand/b3-b5-combined` (C34 `98b140a8` or later records), binary
`f2fed67d…` locally (the Linode binary is rebuilt with GCC 15.2 and has its own sha256, recorded
by the campaign). One instance, the two-stage flow of 14.2. All estimates below are planning
numbers, not measurements of this plan; their evidence is stated in 29.3.

### 29.1 Gate inventory (`certify.py --list`: 404 required gates, 212 parallel, 192 serial)

| Class | Gates |
| --- | --- |
| Must pass (PASSED) | the other 326 gates (the plan minus the two rows below), including every B3-B5 and D2 gate: test-runtime2-b3/-b4/-b5(+mutants, generators, evalorder with gen_operators), b4b(+gen-alias, work, fixpoint, mutants), refsem(+mutants), collections, cp22, coro-module, guardoff, certification-guard, test-build-boundary(-nondestructive), test-build-reuse, test-serve (IPv6 checked by the preflight) |
| Expected FAILED-BASELINE (fingerprinted, `EXPECTED_BASELINE`, 17 gates since C36; each must match every applicable regression baseline, else FAILED-NEW) | `test-runtime2-cp14b` (C36; expected FAILED-NEW against weekend/r2 unless the fresh 0b5079fc build refuses `script_return_kernel_error` too, section 30), `test-v44-shell-restricted`, `test-v46-relative-imports`, `test-runtime2-cp03-simplify`, `test-sha256`, `test-runtime2-host-deps`, `test-runtime2-cp01-simplify`, `test-cp19-s1-dispatch`, `test-rt-parser`, `test-runtime2-rt-diff-selftest`, `test-test-integrity`, `test-v44-packages`, `test-v44-language-foundation`, `cp25r-inventory`, `test-runtime2-scanner-parity`, `test-rt-vm`, `test-rt-host-mutants`. Notes: test-rt-vm's vm_parity now passes on its own (C31), so its fingerprint is vm_modules (`import-scope:collision`, 2 cases) plus whatever its prerequisite cp14b contributes; test-runtime2-cp01-simplify was made deterministic in C23 and may now PASS (better than baseline is not a failure) |
| Linode-only lanes (never run in this container; must pass there unless named above) | `test-engine-bindings`, `test-binding-warnings`, `test-rt-builtins-sanitize`, `test-rt-value-sanitize`, `test-cp19-session-cli`, `test-cp19-session-sanitize`, `test-cp19-proof-sanitize`, `test-cp19-admission-sanitize`, `test-cp19-host-sanitize`, `test-runtime2-cp23-sanitize`, `test-runtime2-cp23-tsan`, `test-v48-map-set-scaling`, `test-runtime2-cp14b`, `test-runtime2-cp14b-sort`, `test-runtime2-cp18-sanitize`, `go-binding`, `csharp-binding`, `node-binding`, `python-binding`, `test-load-sensitive`, `test-runtime2-f6-sanitize`, `test-rt-handles-sanitize`, `test-rt-handles-tsan`, `test-runtime2-cp17-sanitize`, `test-rt-source-map-sanitize`, `test-rt-source-map-tsan`, `test-rt-error-oom-sanitize`, `test-runtime2-b5-evalorder-sanitize`, `test-runtime2-guardoff-sanitize`, `test-cp19-expression-sanitize`, `test-runtime2-cp14b-sanitize`, `test-runtime2-cp22-sanitize`, `test-runtime2-cp22-tsan`, `test-runtime2-coro-module-sanitize`, `test-runtime2-coro-module-tsan`, `test-runtime2-collections-sanitize`, `test-runtime2-collections-tsan`, `test-runtime2-refsem-sanitize`, `test-runtime2-refsem-tsan`, `test-runtime2-b3-sanitize`, `test-runtime2-b4-tsan`, `test-runtime2-cp14b-mutants`, `test-cp18-bytes-sanitize`, `test-cp18-bytes-tsan`, `test-cp19-proof-cli`, `test-cp20-launch-bench`, `test-cp20-r1-flake`, `test-cp21-prompt-latency`, `test-cp23-shared-unit-sanitize`, `test-rt-host-tsan`, `test-rt-parser-asan`, `test-runtime2-cp14b-design-mutants`, `test-runtime2-for-s2-bench`, `test-runtime2-for-s2-sanitize`, `test-runtime2-literals-scaling`, `test-go-binding`, `test-csharp-binding`, `test-node-binding`, `test-python-binding`, `test-rt-host-sanitize`, `test-cp19-r1-sanitize`, `test-rt-vm-sanitize` (sanitizer, TSan, the four bindings and their wrappers, the GCC 15.2 comparator gates, the CP19 oracle gate, the performance lane) |
| Expected PARTIAL items (unexecuted REQUIRED checks; all Category A since C34, so the verdict is at best PARTIAL, NOT RELEASE-QUALIFYING) | test-v44-language-foundation: v44_ast_expression_smoke.sh (big-int equality, large-value compound, live binding, precedence), v44_element_assignment_smoke.sh (7 named checks), v44_root_path_corruption_reproducers.sh (9 named checks), and every check after the failing command of the v44 cp2-cp9 smokes; test-v46-relative-imports: both ownership smokes after their failing command; test-v44-packages: package_transaction_smoke.py after line 330; test-runtime2-cp03-simplify: cp03_missing_tracer.sh after its failing command. **Pending the owner (item 3, 28):** of the 21 extracted Category A checks, 10 pass on the VM, 10 are refused on the VM before any effect and one (`const` element store) runs under B5 where the original asserts an error; the extraction is not committed, so until the owner decides they stay unexecuted A checks |

(C37) The 28 other locally failing planned gates of section 30 are pre-registered, for triage
only, in `docs/runtime-2/evidence/combined-b3-b5/prelaunch-known-failures.tsv`; the runner does
not read it. As planned the campaign ends FAILED (pre-launch review B1): launching needs, for
each, an extractor, a fix, a `--drop` with an owner decision, or the owner's acceptance that this
run is diagnostic.

Other PARTIAL/BLOCKED risks, each checked or provided by the plan: the certification guard's
historical control needs commit `0d853db4` in the clone (transfer the whole branch history, not a
shallow bundle); test-sha256's root-only check needs the `tester` re-run; test-runtime2-cp14b: on
EXPECTED_BASELINE since C36 (section 30).

### 29.2 Run order on one instance

| # | Stage | Dedicated 4 CPU / 8 GB | Shared 4 vCPU / 8 GB |
| --- | --- | ---: | ---: |
| 0 | Provision (Ubuntu 26.04 if offered, else 24.04 + resolute chroot), SSH key pin, clone/bundle transfer | 10 min | 10 min |
| 1 | Environment preflight: pinned GCC 15.2.0-16ubuntu1 (14.1), Python 3.12 + dev, Go 1.26 tarball, Node 22, .NET 10, strace/time/util-linux, `tester` user, sysctl; the frozen tip and tree checked by `test`; `certify.py --toolchain-preflight` (compiler identity, profile sha256s, tool versions, host) before any build (C38); **delete the instance on any PREFLIGHT problem**; the full `--list` preflight follows the builds in stage 3 | 35 min | 45 min |
| 2 | v4.12.0 oracle at `ce8cd36a` (`make -j4 nift`; also the v4.12.0 baseline tree) | 8 min | 12 min |
| 3 | Regression baselines 17f7964c and weekend/r2 `0b5079fc` (`make -j4 nift` each, under `set -e`; their gate objects are built by certify when a gate is compared); then `certify.py --list` with every flag: it checks the oracle and both baselines (commit, clean, `make -q nift`, executable, built after the campaign start, sha256) and exits 2 on any PREFLIGHT line (C37); and the stage-3 check: `script_return_kernel_error` on the fresh 0b5079fc build, recorded | 15 min | 20 min |
| 4 | Candidate build: `make -j4 nift` plus the prebuild of test objects | 20 min | 25 min |
| 5a | Campaign, parallel phase (212 gates, `--jobs 4`) | 1.0 h | 1.4 h |
| 5b | Campaign, serial phase (192 gates): test-rt-vm subtree ~0.5 h; sanitizer lanes 22+7 and TSan 9+1 incl. their object sets ~4.5 h; test-build-reuse ~1.1 h; build-boundary pair ~0.5 h; cp17 handle allocation 0.5 h (budget 1.5 h); bindings ~0.3 h; perf lane ~0.6 h; other serial gates and mutants ~1.6 h | 9.6 h | 12.5 h |
| 5c | Classification: failing gates re-run alone (~20; test-rt-vm again), the `tester` re-runs, then each failing expected-baseline gate on 17f7964c, weekend/r2 and v4.12.0 where applicable (test-rt-vm twice) | 2.5 h | 3.3 h |
| 6 | Collect artefacts (tar, checksums, copy off) | 10 min | 15 min |
| 7 | Delete the instance (after the artefacts are verified locally) | 5 min | 5 min |
| | **Total** | **about 14.5 h (range 12-18 h)** | **about 19 h (range 15-24 h)** |

### 29.3 Evidence and scale factors

- Linode, dedicated 4 CPU (`b89299b5`, weekend-vm-default.md): prebuild 1,078 s, full test-rt-vm
  1,484 s; here (2 vCPU container) test-rt-vm with its prerequisites took 2,120 s (d2gates):
  **dedicated-4 = 0.70 x local** for build-and-test gates that use `-j`.
- CP14 (certification.md): summed target time 95 min locally (shared 2 vCPU) vs 90 min on
  dedicated-4: **serial single-process gates ~0.95 x local**; a shared 4-vCPU plan is taken as
  **1.15 x dedicated per core** with parallel efficiency 2.4 (dedicated 3.2) for the parallel phase.
- test-build-reuse 4,031 s on Linode (tooling.md); test-build-boundary-nondestructive 1,552-1,704 s
  (certification.md); bindings 3.4-3.6 min each (certification.md).
- ASan object set: 494 objects in 7 min 45 s at `-j2` (tooling.md), about 4 min at `-j4`, for each
  of about 7 sanitizer configurations (san, san-lifetime, san-destroy-local, tsan, pic, cp18/cp19/
  cp23 variants); sanitizer runs taken as 2.5 x the production gate (test-rt-vm-sanitize alone
  about 1 h). This lane is the largest uncertainty: no local measurement; it alone can move the
  total by +-2 h.
- Local durations of 56 planned gates (+45 test-rt-vm prerequisites) from this round's logs
  (`scratchpad/combined-fix/plan-durations.json`); the other gates from the cls sample (median
  21 s, mean 58 s per gate): 20 s per parallel gate, 45 s per serial gate, 3 min per mutant gate.
- Generators inside the campaign: gen_alias (12,600 programs, 28 s here), gen_evalorder (840),
  gen_operators (880, 3 s), gen1/gen_struct (47 s), each in its gate; included above.
- The expected-baseline gates on three baseline trees: test-rt-vm on 17f7964c and weekend/r2
  (~30 min each, building their test objects first), the other 15 at 1-12 min each where their
  Makefile has the target (19 table of applicability).

### 29.4 Disk and RAM peak

- Disk: candidate tree with production and test objects ~2 GB (1.9 GB here), sanitizer and TSan
  object sets ~1.5 GB per configuration (tooling.md: 578 MB per ASan directory for the value unit
  alone) ~10 GB, three baseline trees ~2-3 GB each once their gates have built ~8 GB, toolchains
  (GCC 15, Go, .NET, Node, resolute chroot if used) ~4 GB, logs and kept reports ~1 GB, transient
  generator and mutant work directories ~3 GB: **peak about 28 GB**; the preflight requires 40 GB
  free; take the plan's 160 GB disk.
- RAM: read_limit.py's default-limit check needs MemAvailable >= 4,096 MiB (preflight: MemTotal >=
  7,600 MiB); the peak is a parallel phase with ASan gates (each ASan process ~2-3x RSS) at
  `--jobs 4`: **about 6-7 GB**. On 8 GB use `--jobs 3` if the first ASan gate shows swap; no swap
  file (a swapping run distorts the load-sensitive gates).

### 29.5 Commands (as root on the instance)

    # stage 0-1
    set -euo pipefail
    useradd -m -s /bin/bash tester
    sysctl -w vm.mmap_rnd_bits=28
    export CXX=g++-15 CC=gcc-15 NIFT_CP14B_REQUIRE_COMPARATOR=1    # after the 14.1 install and holds
    export NIFT_CAMPAIGN_START=$(date +%s)                          # before the oracle build
    TIP=<frozen tip, full hash>; TREE=<frozen tree, full hash>      # from the coordinator's freeze report
    git clone <bundle> /work/nift && cd /work/nift && git checkout --detach "$TIP"
    test "$(git rev-parse HEAD)" = "$TIP" && test "$(git rev-parse 'HEAD^{tree}')" = "$TREE"   # automatic, not by eye
    test -z "$(git status --porcelain)"
    # stage 1: toolchain and host only, before any build (C38): a wrong toolchain fails in minutes
    python3 scripts/certify.py --toolchain-preflight > /work/stage1-toolchain.txt 2>&1 \
        || { cat /work/stage1-toolchain.txt; echo 'toolchain PREFLIGHT failed: delete the instance'; exit 2; }
    # stage 2-3 (set -e: any failed build stops here)
    git worktree add --detach ../nift-v4.12.0 ce8cd36a92ecfdc8b15b00df41a6c3bb9a31c0eb
    git worktree add --detach ../nift-17f7964c 17f7964cb4f5890740658bd90af2d411b2f7d8ce
    git worktree add --detach ../nift-0b5079fc 0b5079fc6874f998bfe82bf3a7e86e2485724477
    for t in ../nift-v4.12.0 ../nift-17f7964c ../nift-0b5079fc; do
        make -C "$t" -j4 nift; make -q -C "$t" nift; test -x "$t/nift"; sha256sum "$t/nift"
    done | tee /work/stage3-builds.txt
    ../nift-v4.12.0/nift --version                                  # Nift v4.12.0
    make -j4 nift
    FLAGS=(--legacy-oracle "$PWD/../nift-v4.12.0/nift" --campaign-start "$NIFT_CAMPAIGN_START"
           --baseline ../nift-v4.12.0
           --regression-baseline 17f7964c=../nift-17f7964c --regression-baseline weekend/r2=../nift-0b5079fc)
    python3 scripts/certify.py --list "${FLAGS[@]}" > /work/stage1-preflight.txt 2>&1 \
        || { echo "PREFLIGHT failed: delete the instance"; exit 2; }        # exit 2 on any PREFLIGHT line
    if grep -q '^PREFLIGHT' /work/stage1-preflight.txt; then echo 'PREFLIGHT line: delete the instance'; exit 2; fi
    # the stage-3 check (record it; on a fresh 0b5079fc build)
    printf 'a:=[]; return a.pop()\n' > /tmp/srk.f
    for t in ../nift-17f7964c ../nift-0b5079fc; do
        echo "== $t"; NIFT_VM=require NIFT_PREVALIDATE=off "$t/nift" /tmp/srk.f || true
    done 2>&1 | tee /work/stage3-script-return.txt
    # stage 4-5 (not under set -e: the verdict is the exit status)
    set +e
    python3 scripts/certify.py --certify --jobs 4 --unprivileged-user tester "${FLAGS[@]}" \
        > /work/campaign.out 2>&1; echo "exit $?" >> /work/campaign.out   # 0 CERTIFIED, 4 PARTIAL, 1 FAILED, 2 preflight

### 29.6 Artefacts to bring back

The whole `.build/certify/` directory (C37), which holds summary.json and summary.txt (verdict,
classes, `not_executed_required` with categories, `certified_binary`, environment), every per-gate
log with its `.line<N>.log`, `.regression-<name>.log`, baseline and `.unprivileged.log`
companions, `_preflight.json` (with the oracle's and both baselines' facts and sha256s),
`_build-nift.log`, `_prebuild.log` and the kept rt-diff reports (`<log>.kept`); the
fingerprints as summary.json records them (`fingerprint_diff`, `regression`,
`whole_log_identical`); the failing gates' harness JSON outputs (every `--output`/`--report` file
of a failing gate, e.g. `.build/cp14b/evidence/*.json`: `find .build -name '*.json' -newer
/work/stage1-preflight.txt` collected whole); `/work/stage1-preflight.txt`,
`/work/stage1-toolchain.txt`, `/work/stage3-builds.txt`, `/work/stage3-script-return.txt`, `/work/campaign.out`; `sha256sum` of `nift`, the oracle and both
baseline binaries; `dpkg -l` of the toolchain packages, `uname -a`, `/etc/os-release`,
`nproc`, `free -m`, `df -h`; and `git rev-parse` of the four trees. One tarball with a sha256
manifest, verified locally before the instance is deleted.

## 30. C36: test-runtime2-cp14b on EXPECTED_BASELINE; the known local failures outside it

Harness only (no `src/`; binary f2fed67d).

**Extractor.** cp14b's harnesses print `FAIL <name>` only (the evidence goes to their `--output`
JSON). Like vm_parity's rt-diff report (section 23), the runner adds the detail on every tree
alike: `certify_site/sitecustomize.py`, loaded through `PYTHONPATH` with `CERTIFY_CP14B_DETAIL=1`
(Spec `cp14b_detail`), reads the JSON functional.py/effects.py/origins.py wrote at exit and prints
`CERTIFY-CP14B <script>:<name> vm exit=… outcome=… reason=… err=<first error line> legacy exit=…`
per failing case and `CERTIFY-CP14B-SUMMARY <script> failed=N` (the independent count, from the
JSON); the harness's output and status are unchanged (selftest: a fake harness with and without
the hook). Case = step, id, category, status and that detail. Table row (real log
`tests/runtime2/certify-fingerprints/test-runtime2-cp14b.txt`): new (a second case fails), diag
(another refusal reason), subst (another case, same count), count, wording; plus the outside-
diagnostic insertions. Fingerprint selftest: 610 checks over 17 gates.

**Is `script_return_kernel_error` pre-existing on the baselines? Only on 17f7964c, not on
weekend/r2.** `a := []; return a.pop()` (legacy: `return: pop: array is empty`, exit 1), run on
the binaries at hand with `NIFT_VM=require`, and functional.py of each tree run on its own binary
(`scratchpad/combined-fix/cp14b/`):

| Tree | Binary result | Its functional.py |
| --- | --- | --- |
| weekend/r2 `0b5079fc` (cand-vmdefault) | runs on the VM, legacy-equal | PASS (failed=0) |
| integ-argscmd, integ-coromod, integ-refsem-a, cand-refsem-a | runs, legacy-equal | (not run) |
| B3 `4c7d0b40` (cand-b3) | runs, legacy-equal (B3 10 fixed the first N1 regression) | (not run) |
| B4b `17f7964c` (cand-b4) | refused `type:script-return` | FAIL, the same CERTIFY-CP14B line |
| fc987ec1, 91a49efa, f2fed67d | refused `type:script-return` | FAIL (b5-refsem.md 10.6; here) |

So it was introduced between B3 `4c7d0b40` and B4b `17f7964c` (64 commits of B4/B4b; most likely
B4b's per-allocation-site element types, not bisected: no intermediate binary here, and building
one does not fit the disk). On Linode the candidate's fingerprint will equal 17f7964c's but not
weekend/r2's (there the gate passes, or fails only for other reasons), so under the all-baselines
rule **cp14b will be FAILED-NEW**, correctly: it is a B3-B5-era admission regression (a program
weekend/r2 ran legacy-equal is refused before any effect), not a pre-existing failure. **Owner:
the release-blocker audit of b5-refsem.md 10.6 ("a precision regression of an admitted form"),
introduced by B4b; added to section 8's list as "B4b: `a := []; return a.pop()` refused
`type:script-return` (cp14b `script_return_kernel_error`)".** If cp14b passes on GCC 15.2 it is
simply PASSED; with the extractor its failure is at least named exactly.

**Other planned gates that fail locally and are not on EXPECTED_BASELINE** (B5 fix-up list,
b5-refsem.md 11.10, rerun here on this tip, `scratchpad/combined-fix/known/`): without an
extractor each is FAILED-NEW on Linode (no fingerprint never matches).

| Gate | Now | 17f7964c (B5 fix-up `c17-*`, or here) | weekend/r2 (here: this tree's harness, r2 binary) | Cause |
| --- | --- | --- | --- | --- |
| test-runtime2-dynamic-reads | **PASS** | FAIL | - | passes on this tip (cause of the change not investigated) |
| test-runtime2-guardoff, test-runtime2-cp12c-probes | PASS | - | - | fixed earlier (11.11, C7) |
| test-cp15-numeric-repair | FAIL | FAIL | FAIL (same refusal) | VM-default: `number-literal` refused (pre-existing, VM-default) |
| test-cp17-bytes, test-cp19-bytes, test-cp20-bytes | FAIL | FAIL | cp19-bytes FAIL, same refusal (others same cause) | VM-default: bytes/FFI built-ins refused |
| test-cp18-bytes | FAIL | FAIL | not checked | `opaque:legacy-runtime-error` refused |
| test-runtime2-process-capture | FAIL | FAIL | FAIL, same line | `cmd()` refused (VM-default) |
| test-runtime2-cp16-legacy | FAIL | FAIL | FAIL, same line | legacy routing expectations under VM-default |
| test-runtime2-diagnostics | FAIL (31) | FAIL (31) | FAIL (31) | default-engine diagnostics goldens |
| test-runtime2-vm-corpus | FAIL (124 lines) | FAIL (155) | FAIL (155) | default-environment goldens; the candidate's failures are a strict subset, so an equal-fingerprint rule would still call it FAILED-NEW |
| test-runtime2-prevalidate-gate | FAIL | FAIL | not checked | legacy channel expectations ("silent"/"ok") |
| test-rt-builtins, test-rt-case-conversion, test-rt-host-random-budget, test-rt-host-sources-list | FAIL | FAIL | not checked | unit link / source-list defects (ld errors) |
| test-rt-builtins-probe | FAIL | FAIL | not checked | context probe expectations (`context:object-length`) |
| test-rt-memory-c1 | FAIL (nondeterministic: 15 of 24 cells here) | FAIL | FAIL (pre-launch review: reproduced on the weekend/r2, B3 and B4 binaries at 400 and 800 MB) | **corrected in C37: a pre-existing product abort on out-of-memory**, not the environment: under `ulimit -v` nift aborts (SIGABRT, exit 134, glibc "failed to register TLS destructor: out of memory") instead of failing cleanly; nondeterministic (glibc 2.39+), so Linode hits it too and no fingerprint can match reliably. **Release blocker** (runtime safety), pre-existing, not a candidate regression |
| test-runtime2-cp13b-mutants, test-runtime2-literals-mutants, -literals-f1-negative, -literals-decimal-negative, -module-host-order-mutant | FAIL | not run there (B5 fix-up: harness failure) | not checked | mutant builds rejected by the production build guard (tooling.md 3) |
| test-runtime2-cp17-handles, test-runtime2-cp17-routing-mutants | FAIL | FAIL (routing build) | not checked | stale routing mutant (ParserScript.cpp no longer compiles with it) |
| test-cp19-r1, test-cp19-shared-eval, test-cp19-expression-development | FAIL | FAIL | not checked | stale eval harness expectations (refusal note wording, function tag) |
| test-cp19-admission-mutants (+ -switch) | FAIL | FAIL (other cases) | not checked | **B5-changed** (case `loop` control: E-U6-1, PENDING-OWNER, b5-refsem.md 11.4); m10 survives |

28 gates (plus cp14b) would reach Linode as FAILED-NEW. Each needs an extractor on
EXPECTED_BASELINE (and must then match both regression baselines), a fix, or an owner decision
before launch; cp19-admission-mutants and cp14b are B3-B5-era and cannot become FAILED-BASELINE.

## 31. Pre-launch review of 0f327a20: CONFIRMED-WITH-CONDITIONS (C37)

Review: `scratchpad/review-ha/PRELAUNCH.md`. Harness and docs only (no `src/`; binary f2fed67d).
- **B2 (fixed).** `--list` and the campaign preflight check, before any gate runs, the
  `--legacy-oracle` (as before, plus executable) and every `--regression-baseline NAME=TREE`:
  the checkout is at the named commit (`REGRESSION_COMMITS`: 17f7964c, weekend/r2 = 0b5079fc; or
  a NAME that is a commit-hash prefix) and clean, `make -q nift` is 0, `nift` is executable and was
  built after the campaign start, and its sha256 is recorded (`_preflight.json`
  `regression_baselines`). A problem is a PREFLIGHT line; a full campaign exits 2, and `--list`
  of a full plan now exits 2 too. Guard: 11 new preflight cases (each field, a bogus baseline
  path through the real fact collector, a stale baseline tree: a git checkout whose `nift` is
  older than its source and than the campaign start). Seen here: a nonexistent 17f7964c path and
  the local weekend/r2 tree (`make -q nift` 1: its binary predates 8 tooling commits) are both
  reported. 29.5: stage 3 builds the oracle and both baselines under `set -e`, checks each with
  `make -q` and `test -x`, records sha256s, then runs `--list` with every flag and stops on any
  PREFLIGHT line, then records `script_return_kernel_error` on the fresh 0b5079fc build.
- **B1 (owner).** As planned the campaign ends FAILED (29.1, 30); recorded in 29.1, not decided here.
- **Artefacts (29.6):** the whole `.build/certify/` (incl. `_build-nift.log`, `_prebuild.log`) and
  the failing gates' harness JSON. Pre-registered known failures for triage only:
  `docs/runtime-2/evidence/combined-b3-b5/prelaunch-known-failures.tsv` (cp14b and the 28 gates of
  30, each with its suspected cause, baseline evidence and owner); the runner never reads it (guard
  check), so it changes no classification or verdict.
- **Docs:** 29.1 (cp14b on EXPECTED_BASELINE); the L4 consequence in the b7 fixture header;
  test-rt-memory-c1 reclassified in 30 as a pre-existing product abort on out-of-memory and added
  to the release blockers (8). The review's vm-corpus note (3 of the 124 blocks are B5-approved
  differences) is in the known-failure list.

## 32. PRELAUNCH-R2 on 60eadc65: CONFIRMED-WITH-CONDITIONS (C38)

Review: `scratchpad/review-ha/PRELAUNCH-R2.md`. Harness and docs only (no `src/`; binary f2fed67d).
- **C1 (fixed): distinct binaries.** Each regression baseline's `nift` must differ from the
  candidate, the oracle and the other baseline by sha256 and by real path, and must not be a
  symlink out of its own tree; two baselines may not share a tree. (The oracle already had its
  checks against the candidate.) Guard: symlinked to the candidate (sha, path and symlink lines),
  a copy of the candidate, the oracle's binary, the same binary under both names, a symlink out of
  the tree, two names on one tree, and a real tree whose `nift` symlinks to another binary through
  the real fact collector.
- **C2 (fixed): the required baselines.** A full campaign (and `--list` of a full plan) needs
  exactly `17f7964c` and `weekend/r2`, once each, on distinct trees; none, one, a duplicate name or
  a missing name is a PREFLIGHT problem (guard: each).
- **Low items.** The baseline tree's untracked/ignored files under `src/`, `include/` and its
  signature's `-I` directories are refused like the candidate's. Baseline git commands run with
  `GIT_OPTIONAL_LOCKS=0` (read-only). `certify.py --toolchain-preflight` runs the toolchain and
  host checks only (no build, oracle or baselines needed; `toolchain_problems`, table-tested) and
  exits 2 on a problem; 29.5 runs it in stage 1 before the stage-3 builds, and stage 0's tip and
  tree check is an automatic `test` against the frozen hashes. B1 stays the owner's decision.

## 33. Linode finding: the v4.12.0 oracle can never pass plain `make -q nift` (C39)

Found by the coordinator on instance 110453277 (setup passed in full, `--toolchain-preflight: ok`;
the oracle built; then builds stopped with `FAILED (23): oracle v4.12.0: make -q nift is not 0 after
the build`; instance deleted; spend so far US$8.43). Cause, verified there: v4.12.0's Makefile
(ce8cd36a line 117) has `$(LIBFFI_SIGNATURE): FORCE`: the libffi toolchain signature is re-made on
every make (and rewritten only when its content changes), so `make -q nift` in a v4.12.0 tree is
never 0 (`make --debug=b -q`: "Must remake target 'FORCE'", then the signature). 17f7964c, 0b5079fc
and the candidate compute the signature with `$(shell)` (Makefile lines 138-147) and pass plain
`make -q`. So the C37/C38 oracle requirement could not be met by the frozen reference.

**Fix (harness only; the oracle only; baselines and candidate keep plain `make -q nift`).**
`certify.oracle_freshness(tree)`, all dry (nothing is written):
- the signature's path is the oracle tree's own `LIBFFI_SIGNATURE`, read from its `make -pq`
  database, and the rule must be the FORCE'd one (otherwise the plain `make -q nift` is used);
- `make -q -o <signature> nift` must be 0: everything else is current, the signature alone
  treated as old;
- the signature file must exist and equal what the tree's own recipe writes for the current
  toolchain: the recipe's `printf` line, taken verbatim from `make -n <signature>` and run with only
  its `> "$@.tmp"` redirect removed. This is the recipe's own computation (CC, CXX, the libffi
  target, CFLAGS, both compilers' `--version` lines) on this host now, so `-o` cannot hide a
  toolchain change: a signature from another compiler or flags no longer matches, and a missing one
  fails.
Every other oracle check is unchanged (commit ce8cd36a, clean, executable, built after the campaign
start, sha256, distinct from the candidate and the baselines). Guard (a v4.12.0-style toy tree with
the FORCE'd signature rule, built by its own make): plain `make -q` is not 0 there (the finding);
the fresh tree passes; a stale binary fails; a signature written by another toolchain fails; a
missing signature fails; a tree without the FORCE'd rule keeps the plain `make -q`. Locally, the
real ce8cd36a tree `bench-v412` reports its signature missing for this host's toolchain (that tree
was not built with it), as intended.

The campaign driver's builds phase (`scratchpad/linode/cb35/job.sh`) checks the oracle the same way
(`certify.oracle_freshness`, exit 23 on failure) and keeps plain `make -q` for the two baselines and
the candidate.
