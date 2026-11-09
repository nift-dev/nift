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

**Superseded by section 35 (C41):** the oracle is now built by `certify.py --record-legacy-oracle`
from a pristine checkout and passed with `--legacy-oracle-sha256` and `--legacy-oracle-provenance`.

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
  against weekend/r2 until fixed or decided. **Fixed in C44 (B06, section 37)**, with
  `comparator_factory`; awaiting review and the GCC 15.2 confirmation on Linode.
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
  sha256 would close it. **Closed by C41 (section 35):** the oracle's sha256 is pinned right after
  its build and verified before every use.
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
  threat-model limitation** (L1). **C41 (section 35) replaces this for the oracle:** the checkout
  checks stay, the `make -q` and mtime checks are recorded only, and the binary is pinned by the
  sha256 recorded right after its build. Substitution after pinning is now refused.

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
    # C41 (section 35): the oracle under its checksum-pinned provenance contract
    test -z "$(git -C ../nift-v4.12.0 status --porcelain --untracked-files=all --ignored)"; test ! -e ../nift-v4.12.0/.build
    python3 scripts/certify.py --jobs 4 --record-legacy-oracle ../nift-v4.12.0 --legacy-oracle-provenance /work/oracle-provenance.json
    sha256sum ../nift-v4.12.0/nift | tee /work/oracle-sha256 /work/stage3-builds.txt   # the pin, at once
    for t in ../nift-17f7964c ../nift-0b5079fc; do
        make -C "$t" -j4 nift; make -q -C "$t" nift; test -x "$t/nift"; sha256sum "$t/nift"
    done | tee -a /work/stage3-builds.txt
    ../nift-v4.12.0/nift --version                                  # Nift v4.12.0
    make -j4 nift
    FLAGS=(--legacy-oracle "$PWD/../nift-v4.12.0/nift" --campaign-start "$NIFT_CAMPAIGN_START"
           --legacy-oracle-sha256 "$(cut -d' ' -f1 /work/oracle-sha256)" --legacy-oracle-provenance /work/oracle-provenance.json
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

(Owner 2026-11-08 14:01: this instance, like all five failed ones, failed in setup or in the
provenance preflight. It was not a Runtime 2.0 certification failure, because no certification gate
ran. The C39 check is a supporting fact only since C41; see section 35.)

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

## 34. Linode finding 2: a fresh v4.12.0 build deletes its own signature (C40)

(Owner 2026-11-08 14:01: a provenance-preflight failure, not a Runtime 2.0 certification failure;
no certification gate ran. The C40 timestamp proof is a supporting fact only since C41; see
section 35.)

Found by the coordinator on instance 110455151 (setup passed; the oracle built; builds exit 23):
`oracle_freshness` gave `signature_present: False`, `make_q: 0`. Cause (verified in a ce8cd36a
tree): v4.12.0's `scripts/build_vendored_libffi.sh:68` runs `rm -rf "$BUILD"`, deleting
`$(LIBFFI_BUILD)/.nift-toolchain-signature`, which the FORCE'd rule had just written inside that
directory; so after any fresh v4.12.0 build the file never exists (later trees keep it beside the
directory, `$(LIBFFI_BUILD).nift-toolchain-signature`). The C39 content check could therefore never
pass on a freshly built oracle.

**Fix (harness; the oracle only).** If the signature file is present, the C39 content check is
unchanged. If it is absent, the oracle is accepted only when both hold:
- `make -q -o <signature> nift` is 0; and
- a mechanical proof that its whole build happened during this campaign (`fresh_build_facts`,
  dry): every object on nift's link line (the last `-o nift` line of `make -n -B nift`; at least
  one required), nift itself, every file under `.build/`, and every untracked or ignored file of
  the tree (`git ls-files --others`: every build product anywhere) has mtime >= the campaign start.
  Nothing in the tree predates the campaign, so it was built from scratch in this run's
  environment, with this run's CC/CXX (which the toolchain preflight binds).

**Recorded v4.12.0 limitation:** for the oracle, toolchain identity is established by this
in-campaign fresh build, not by its signature file (v4.12.0 deletes it).

Guard (a FORCE'd toy git tree whose libffi step deletes the signature, built by its own make):
the fresh in-campaign build passes (signature absent, accepted by the fresh build, objects >= 1);
one object older than the campaign start fails; the binary older than the campaign start fails; a
stray build product older than the campaign start fails; a present but mismatched signature still
fails (the C39 check).

**Real check, local:** a fresh build of ce8cd36a in a scratch clone (not bench-v412; GCC 13.3 here;
1 min 32 s; deleted afterwards): signature absent, `make -q -o <signature> nift` 0, 45 objects on
the link line, every build product newer than the campaign start: accepted ("fresh in-campaign
build"); with a campaign start after the build it is refused (`.build/libffi/.../.build.log` and
the rest older); plain `make -q nift` there is 1 (the C39 finding).

## 35. Owner decision 2026-11-08 14:01: the oracle's checksum-pinned provenance contract (C41)

**The five failed instances.** Every one stopped in setup or in the oracle's provenance preflight
(sections 33 and 34 are two of them). They were setup and provenance-preflight failures, **not
Runtime 2.0 certification failures**: no certification gate ran on any of them, and none says
anything about the candidate.

**Decision.** v4.12.0 (ce8cd36a) is a **legacy semantic oracle**. It is not held to the later build
system's invariants (libffi toolchain signature, `make -q nift`), and v4.12.0 is not modified. The
strict modern checks are unchanged for the candidate, 17f7964c and weekend/r2. The owner prefers a
checksum-pinned contract to timestamps.

**Contract (harness only; no src/ or Makefile change):**
1. `certify.py --record-legacy-oracle TREE --legacy-oracle-provenance JSON` (`record_legacy_oracle`)
   first checks that TREE is a pristine checkout of exactly ce8cd36a (tree `c128ba47`):
   - HEAD and tree are those;
   - `git status --porcelain --untracked-files=all --ignored` is empty, so there is no tracked
     change, untracked file or ignored build output anywhere;
   - there is no `.build`;
   - the provenance file does not exist yet, because a pin is written once.
   If any check fails it exits 3 and builds nothing.
2. It builds `make -j<N> nift` from scratch in the campaign's environment (`clean_env()`; job.sh
   exports CC=gcc-15 and CXX=g++-15 after the toolchain preflight). It records:
   - the command and cwd;
   - CC/CXX as make resolves them, with each compiler's `--version` line and identity (resolved
     path, sha256, owning package and version: the same `compiler_identity` the toolchain
     preflight uses);
   - CC, CXX, CFLAGS, CXXFLAGS, CPPFLAGS, LDFLAGS and LDLIBS as make reports them (dry
     `make -n --eval`, read before the build);
   - the commit, the tree and the pre-build check;
   - the host;
   - the build log.
3. The build must exit 0 and leave an executable `nift`. Otherwise it exits 1, and the record
   carries build_rc.
4. The sha256 of `nift` is taken immediately after the build. It goes into the provenance file
   (read-only, schema `nift-legacy-oracle-provenance/1`) with the start and end timestamps, the
   `--version` output and, as supporting facts only, the C39/C40 `oracle_freshness` (including
   `make -q -o <signature> nift` and the fresh-build timestamps). job.sh writes `sha256sum` of the
   binary to `/root/out/oracle-sha256` at once.
5. The existing checks remain: distinct from the candidate (sha256 and real path) and from the
   regression baselines; reports v4.12.0; executable; its checkout at ce8cd36a with no tracked
   change.
6. Every certify run given `--legacy-oracle` (`--list`, focused runs and the full campaign) needs
   `--legacy-oracle-sha256 HEX` and `--legacy-oracle-provenance JSON`. It **exits 2** unless all of
   these hold (`oracle_pin_problems`, pure):
   - the pin is a sha256;
   - the record exists and parses;
   - its schema, commit and tree are ce8cd36a's;
   - its pre-build check is ok;
   - build_rc is 0;
   - the command and timestamps are present;
   - the pin equals the recorded sha256;
   - the binary is the recorded path;
   - the recorded CC/CXX identities equal this campaign's verified toolchain;
   - the oracle's current sha256 equals the pin.
   The v4.12.0 baseline tree's nift (`--baseline`, the oracle's own tree) must also equal the pin;
   a full campaign requires it to exist.
   During classification `oracle_recheck` runs again (logged in summary.json `oracle_rechecks`):
   after the gate runs, before each target is classified, after every v4.12.0 baseline run and
   baseline line run, and before each decision. A mismatch stops the campaign with exit 2 and
   writes `.build/certify/_oracle-recheck.json`.
   `make -q -o <signature>` (C39), the signature content (C39), the fresh-build timestamps (C40)
   and the in-campaign mtime (D4) are recorded for the oracle but decide nothing. A campaign start
   is still required when regression baselines are given (their mtime check is unchanged).

**Guard (215 cases; toy git checkouts recorded by `record_legacy_oracle`, each build a different
binary):**
- **Recording:**
  - an untracked file or an existing `.build` before the build is refused (3), and so is a
    checkout that is not ce8cd36a; nothing is written in either case;
  - the pristine toy is recorded (commit, tree, command, rc 0, sha256, both compiler identities,
    make's flags);
  - a second record is refused, because the pin is never rewritten;
  - a failing build exits 1 and its record is refused.
- **Pin and recheck:**
  - the correct pin passes;
  - a different pin is refused;
  - a substituted file is refused;
  - the candidate copied over the oracle is refused, and also counts as the same binary as the
    candidate;
  - a binary rebuilt after pinning is refused, both for the oracle and for the v4.12.0 baseline
    tree's nift;
  - a missing `--legacy-oracle-sha256` is refused;
  - a missing provenance file, or no provenance flag, is refused;
  - a provenance commit or tree that is not ce8cd36a's is refused (the toy itself, and the table);
  - a non-pristine pre-build record is refused;
  - another compiler is refused;
  - a non-hex pin is refused;
  - a baseline tree whose nift is not the pin is refused, and so is a full campaign whose baseline
    tree has no nift.
- **CLI:** `--list --targets …` with an unpinned oracle exits 2, and with the pinned toy exits 2
  (not ce8cd36a).
- **Superseded cases:** the C39/C40 cases now check that their facts are still computed and decide
  nothing.

**Real check, local (a scratch clone of ce8cd36a, not bench-v412; GCC 13.3 here; deleted
afterwards):**
- `--record-legacy-oracle` checked the checkout pristine (empty status, no .build), built it in
  93 s with exit 0, and recorded sha256 `7dd42dce…` (CC cc → x86_64-linux-gnu-gcc-13, CXX g++ →
  x86_64-linux-gnu-g++-13, both with sha256; the flags as make reports them; the supporting
  freshness facts were make_q 0, signature absent, and 45 objects, all fresh).
- `--list --targets test-runtime2-b5 --legacy-oracle … --baseline <tree>`:
  - with the pin and the record: exit 0, no oracle problem (only this host's GCC 13 and tool
    problems);
  - without `--legacy-oracle-sha256`: exit 2;
  - without the record: exit 2;
  - with the candidate's binary copied over the oracle: exit 2. Three problems are reported: the
    sha256 is now `f2fed67d…`, not the pinned `7dd42dce…`; it is the same binary as the candidate;
    and the baseline tree's nift is not the pin;
  - after restoring the binary: exit 0 again.
- A second record (same file) was refused, and one into a new file was refused because the tree is
  not pristine (its .build outputs).
- `oracle_recheck` passed, failed after one byte was appended, and passed again once the binary
  was restored.
- A second `make nift` in a built v4.12.0 tree re-runs the libffi step (its cache exits early) but
  does not relink: the sha256 is unchanged. So the v4.12.0 baseline runs, which use the same tree,
  leave the pinned binary as it is.
- The build is reproducible here: a second fresh clone gave the same `7dd42dce…`.

**Driver (`scratchpad/linode/cb35/job.sh`, builds phase):**
- Before the oracle build it checks the checkout (HEAD ce8cd36a, empty `status --porcelain
  --untracked-files=all --ignored`, no `.build`, no pin or provenance yet); failing that is the
  new exit **27**.
- It then runs `certify.py --record-legacy-oracle` (exit 3 → 27, any other failure → 20), writes
  `/root/out/oracle-sha256` right after it, and checks that pin against the provenance (exit 23).
- `--list` and `--certify` get `--legacy-oracle-sha256 <pin> --legacy-oracle-provenance
  /root/out/oracle-provenance.json` (`pin_flags`, read from that file, never recomputed from the
  binary later).
- The two baselines and the candidate keep `make -q nift`.
- collect also copies the provenance and the pin.

## 36. B21: certification evidence outside every tree, durable per gate (C42)

**Finding (BLOCKERS.md B21, instance 110461664).** The campaign ran 404 of 404 gates: 281 ok, 123
FAILED, certify exit 1. Classification then crashed with `FileNotFoundError` on
`.build/certify/test-build-reuse.log`:
- test-build-reuse's `scripts/build/check-build-reuse.sh:244-248` runs `make clean nift` and
  `make distclean nift` in the checkout.
- `distclean` removes `$(TEST_DIR)` = `.build`, and with it `.build/certify`: every earlier gate
  log, the running gate's own log, the preflight and the build logs.
- The 190 exclusive gates after it ran in a tree that had been distcleaned and rebuilt, and that
  the 3,600 s kill may have left in an intermediate configuration.
- The owner (2026-11-09 16:46) approved B21 as the last harness correction. Harness and tests only;
  no src/ or Makefile change.

**1. Evidence outside every tree.**
- Everything certify writes goes to `--evidence-root/<campaign-id>/` (default
  `/var/tmp/nift-certify`, `scripts/certify_evidence.py`): identity, per-gate logs and records,
  kept rt-diff reports, fingerprint inputs, preflight, build logs, oracle rechecks and summaries.
- `--campaign-id` defaults to UTC time, commit and pid. The private copies of destructive gates go
  to `--work-root/<campaign-id>/` (default `/var/tmp/nift-certify-work`).
- Both are refused (exit 2, every run mode, `--list` included) when they lie inside:
  - the checkout;
  - the oracle's tree;
  - the v4.12.0 baseline;
  - a regression baseline;
  - the unprivileged copies.
  They are also refused when either lies inside the other.
- `.build/certify` is no longer written.

**2. Durable per gate.**
- **First pass.** Each gate's first pass writes `gates/<target>/run.log`, fsynced, and then
  `run.json` (atomic: temp file, fsync, link, directory fsync, written once, read-only).
  `run.json` holds:
  - rc, start, end, seconds and timeout;
  - the cwd and source tree, and whether the run used an isolated copy;
  - the log's sha256 and size;
  - the tree identity before and after: commit, tree, working-tree diff sha256 and nift sha256;
  - the toolchain, the environment, the campaign id and the identity sha256.
- **Classification.** Classification reads these and never writes them. Its own runs (rerun,
  unprivileged, v4.12.0 baseline, regression baselines, recipe lines) go into a new attempt
  directory `gates/<target>/cls-<N>/`, each run with the same kind of record. `class.json` is
  written last.
- **Resume.** `--resume` reopens a campaign:
  - gates with a `run.json` are read, not rerun;
  - an interrupted first pass's log is kept as `run.log.interrupted-<k>`;
  - targets with a completed attempt are read back;
  - an interrupted attempt (no `class.json`) is kept as it is, and classification continues in
    attempt N+1.
- **Interruption.** SIGTERM or SIGINT kills the running gates' process groups (their exact PGIDs,
  tracked by certify) and exits 128+signal.
- **Missing evidence.** A missing or altered first-pass log, or a missing record, makes the gate
  FAILED-NEW with an `EVIDENCE-LOST` reason; classification never crashes on it (B21 acceptance 2).
  An OSError during classification is handled the same way.
- `--no-classify` runs and records the first passes only.

**3. Identities kept apart.**
- `identity.json`, written once, records:
  - the candidate's tree identity;
  - the toolchain (CXX version line, resolved CC/CXX and their sha256s, `__GLIBCXX__`, libstdc++
    sha256);
  - the environment;
  - the configuration (flag and make variables, CC, CXX, the comparator switch, the unprivileged
    user);
  - each regression baseline's and the v4.12.0 baseline's tree identity, built first if missing;
  - the oracle and its pin.
  Its sha256 leaves out the campaign id and the paths.
- **Resume.** A resume under a different identity is refused with exit 2, and each difference is
  printed.
- **Records at classification.**
  - A first-pass record from another campaign or identity, or one that ran on another candidate
    tree or binary, is BLOCKED `IDENTITY-MISMATCH (not classified)`, with the difference given.
  - Each classification run's record must match the identity its role expects (candidate,
    `v4.12.0`, `regression:<name>`); otherwise the gate is BLOCKED in the same way.
  - So a gate that left a different binary behind cannot silently colour the gates after it.
- **Merge.** `--merge` lists each summary's campaign and identity, and refuses summaries of
  different source trees unless `--merge-mixed-identities` is given (that merge is then labelled
  as mixed). The existing binary and libstdc++ refusal is unchanged.
- **Separation.** Recovery reruns and baseline comparisons are separated by attempt directory and
  record role, and campaigns by their directories.

**4. Owner's acceptance test (guard, the real `scripts/certify.py` on a toy git tree whose
`distclean` removes `.build` and nift):**
1. test-ev-a runs; its log and result are stored.
2. `make distclean` removes the checkout's `.build` and nift.
3. Both files still exist, byte for byte (sha256).
4. test-ev-b runs in the resumed campaign; test-ev-a's record is read, not rerun.
5. Classification reads both: test-ev-a is classified from its record.
6. SIGTERM to certify's exact PID during test-ev-b's rerun:
   - every stored record is unchanged;
   - the interrupted attempt `cls-1` is kept;
   - the resumed classification completes in `cls-2` with both gates FAILED-NEW (exit 1).

Negative control: the C41 runner (`git show 2bedfb33:scripts/certify.py`, logs under `.build/certify`)
fails step 3, because distclean deletes its log.

Further guard cases:
- **Refusals (exit 2):**
  - evidence inside the checkout;
  - a reused campaign id without `--resume`;
  - a resume after a source change.
- **Lost evidence:** a deleted `run.log` makes the gate FAILED-NEW `EVIDENCE-LOST`, and the
  campaign completes.
- **Isolation:** a destructive toy `test-build-reuse` runs `make distclean` in its copy. The
  checkout keeps `.build` and nift, `timing.txt` is kept in the evidence, the copy is removed, and
  the cap is 7,200 s.
- **Table cases (19):**
  - root containment;
  - tree and identity comparison;
  - the evidence verdicts;
  - the destructive detector (positives, comments, `make nift … clean tree`, the script behind a
    recipe, an unlisted cleaning gate as a plan problem);
  - the per-gate cap.
- The harness self-test now has 234 cases.

**5. Destructive gates (`DESTRUCTIVE` in scripts/certify.py, recorded):**

| Gate | Why it is destructive | Kept from its copy |
| --- | --- | --- |
| test-build-reuse | `make clean nift` and `make distclean nift` (removes `.build`, the objects and nift) and rebuilds the shared objects under other configurations | `.build/build-reuse/` (timing.txt, make.log), and the gate's TMPDIR (its clean-check logs) |
| test-rt-isolation | plants reproducers into production sources and rebuilds their objects (restored, but a kill mid-run leaves them planted) | none |
| test-rt-sort-forgery | plants the forged-header reproducer into `src/Hooks.cpp` and relinks nift (same) | none |

**How they are isolated.**
- Each runs, on every tree it runs in (candidate first pass and rerun, the unprivileged copy, the
  v4.12.0 and regression baselines, its recipe lines), in a private `cp -a` copy under the work
  root, chowned for the unprivileged user. Its own outputs are copied into the evidence and the
  copy is then removed.
- **Why a copy is the least invasive choice:**
  - the gate's script and the Makefile stay unchanged;
  - it starts from the same built state;
  - no repository metadata changes, unlike `git worktree add`;
  - scheduling the gate last is not enough: classification reruns other gates in the same trees
    afterwards, and a timeout kill leaves a tree half-reconfigured.
- **Cost:** one copy of the tree (about 2 GB here with .build) per run of these three gates.
- **Detection:** `--check-plan` and full campaigns refuse any planned gate whose recipe, or a
  script it runs, contains a `make clean|distclean|pristine` goal, `distclean`, or
  `rm -rf $(TEST_DIR)/.build`, unless it is in `DESTRUCTIVE` or `NOT_DESTRUCTIVE`.
  `NOT_DESTRUCTIVE` holds one reviewed entry: test-runtime2-certification-guard, whose distclean
  acts only on its toy trees.
- On the real plan the detector finds test-build-reuse, through check-build-reuse.sh.
- **Limitation:** a copy of a `git worktree` checkout shares its gitdir. On Linode the trees are
  clones.

**6. The test-build-reuse timeout (B21 item 6): open.**
- A cheap lead: tooling.md records test-build-reuse at **4,031 s** on Linode at e6d21fa4, which
  exceeds certify's 3,600 s per-gate default, so the kill on 110461664 is likely a cap below the
  gate's runtime.
- **Not confirmed.** The campaign's own timing.txt was lost, and that 4,031 s run included failing
  checks.
- `GATE_TIMEOUTS` gives test-build-reuse 7,200 s, and its timing.txt and logs are now kept in the
  evidence, so the next campaign will establish its runtime.

**Driver (`scratchpad/linode/cb35/job.sh`, read fresh).**
- **builds:** writes `/root/out/campaign-id` (`cb35-<campaign start>-<tip8>`). `--list` and the
  campaign get `--evidence-root /var/tmp/nift-certify --work-root /var/tmp/nift-certify-work
  --campaign-id <id>`.
- **campaign:** a re-started campaign phase adds `--resume`, and `campaign.out` is appended to.
- **collect:** copies the evidence directory (`artefacts/evidence/`) instead of `.build/certify`,
  plus the campaign id. It still collects the gates' own harness JSON from `.build`.
- **Rerun:** B21 acceptance 4 requires a full campaign rerun.

## 37. B06: two B4b admission regressions, fixed in the kind inference (C44)

**Owner order (2026-11-09 16:46, item 4).** B06 is a genuine admission regression. The order:
find the earliest commit that causes it; restore legitimate execution through the existing
admission and runtime-kind mechanisms; add regression tests; no broad type-inference redesign.
This is a production change in `src/rt` (two compile-time inference files), bounded as follows.

**Bisect.** The B4b range runs from 4c7d0b40 (B3) to 17f7964c. It was bisected by building nift in
a sparse scratch clone of the repository, one commit at a time, then deleting the clone. The clone
left out `docs/` and the cp14b fixtures; free disk ranged from 498 MB to 816 MB. The comparator's
libstdc++ profile gate (`compiler_collection_sort.cpp:8`) was compiled out in the scratch clone
only, so the comparator case could run on GCC 13.3.

| Program | Earliest bad | Its parent | Result there |
| --- | --- | --- | --- |
| `a := []; return a.pop()` (cp14b `script_return_kernel_error`) | **29d7ef0f** B4b 1 (per-site element types) | 982187d2 (B4b 0, design note only, code = B4a) | refused `type:script-return` / runs legacy-equal |
| `a:=[3,2,1]; n:=0; fn(mk()) { n+=1; return (x,y)=>x<y }; a.sort(mk()); …` (cp14b `comparator_factory`) | **3eb749cc** B4b 8 (review H1: inexact callees widen every function value) | 2a24421f (B4b 7) | refused `type:<` / `[1,2,3]`, `3` |

The other probes in the second bisect also placed the regression at 3eb749cc: 29d7ef0f and
2a24421f were good, and 3eb749cc, 0abc0649, a42035c1 and 17f7964c were bad.

**Root causes.**

1. **Family 1: the empty-literal proof.** B3 10 (1f46b0e6, the N1 rule) kept an empty literal's
   element type as proven empty ("holds nothing"). It relied on the literal's positional fact
   (`tuple` with no items), so `a.pop()` had no kinds and passed the top-level script-return check
   (`compiler.cpp:228`). B4b 1 made every mutator's site store drop the positional fact of the
   receiver's sites (B3 N1 per site). The pop itself made the literal non-positional, so on the
   next fixed-point walk the receiver no longer looked like an empty tuple.
   `collection_result` then turned the empty element type into anything
   (`infer_collection.cpp`), and the return was refused.
   - **Mechanism r2 used:** the B3 N1 rule. The program compiled for the VM, and the VM's `pop`
     raised the run-time error at legacy's position (`1:10: return: pop: array is empty`).
2. **Family 2: callbacks still unknown on the first walk.** B4b 8 (H1) made every call whose
   targets are not exact call `untracked_call()`, which widens every function value to parameters
   of anything. `exact_callee` requires a non-empty `callables` list. A collection method's
   callback reached `call_callables` through `collection_result` / `collection_callables` even
   when its type was still empty, as with a factory's result before the factory's body is walked
   (`a.sort(mk())`, `a.map(mk())`). That transient bottom triggered the permanent widening, so the
   lambda's `x < y` saw anything and was refused.
   - **The direct-call path never did this:** `Inference::call` reaches `call_callables` only with
     callables, and calls `untracked_call()` only for a callee that is any, Unknown or
     Function-kind.
   - **Mechanism r2 used:** no H1 widening. The lambda's parameters took the sort's element kinds
     from the call (`call_callables`), and the program ran on the VM, `[1,2,3]` / `3`. B4b and the
     candidate probe builds refuse it with `type:<`.

**Fix (`src/rt`; 2 files; 2 conditions):**

1. **`infer_collection.cpp`, `collection_result`.** An empty element type also stays "holds
   nothing" when the receiver was read precisely from its allocation sites and nothing was ever
   stored there. The condition: not a session; not `any`; it has an element type; it has sites;
   none of them is `kOpenSite`.
   - An open, exposed or collapsed class reads its element as anything when materialized, so it
     never has an empty element type.
   - This is the same per-site contents soundness every other element read relies on since B4b. It
     restates B3's N1 proof in B4b's terms. It is not a new inference rule.
   - Sessions keep only the B3 positional rule.
2. **`infer_callable.cpp`, `call_callables`.** A callee with no kind, no callables and no untracked
   flag is pending: it has no target yet, so it does not call `untracked_call()`. The fixed point
   re-walks the call when the callee's type grows: the factory's `returns_` grows, and its callers
   are re-walked. At that point the callee is exact or inexact as before.
   - This aligns the collection-callback path with the direct-call path.
   - A callee that is any, Unknown, Function-kind without callables, or an untracked callable
     still widens.
   - The H1 mutant A28 is still caught (`test-runtime2-b4b-mutants`: 20 caught).

Mutant hooks: `NIFT_B06_MUTANT` 1 and 2 restore each refusal (`tests/runtime2/b06/build_mutant.sh`).

**Behaviour after the fix (GCC 13.3 production build; the comparator fixtures also on the probe
build).**
- Every family-1 and family-2 program runs on the VM. Stdout, stderr and exit status equal legacy
  on the same binary, error positions included.
- They equal r2 too where r2 admits them:
  - `a := []; return a.pop()`: `1:10: return: pop: array is empty` on legacy, r2 and the fixed
    build;
  - `a := []; a.pop(); return a.pop()`: `1:10: pop: array is empty`;
  - `a := []; return a.remove(0)`: `1:10: return: remove: index out of range`;
  - comparator_factory: `[1,2,3]` / `3`.
- On production GCC 13.3, comparator_factory is still refused `method:sort` by the libstdc++
  profile gate. cp14b reports it UNVERIFIED locally (as for every comparator case), and it runs on
  the certified GCC 15.2.

**Tests.** `test-runtime2-b06` and `test-runtime2-b06-mutants` are new targets, in the inventory
and in MANDATORY.
- `tests/runtime2/b06/run.py` has 28 fixtures in `tests/runtime2/b06/fixtures/`.
  - **Family 1:** pop, remove, twice, in an expression, compared, after clear, after reverse,
    inside an `if`.
  - **Family-1 controls admitted before as well:** bound, expression, in a function, an
    unknown-kind array parameter, pushed then popped.
  - **Family 2:** sort by factory, factory bound to a variable, map, filter, reduce, find, any,
    sort_by, and a callback error with its position.
  - **Refusals that must stay:** an array pushed then popped and returned; the same through an
    alias, through a callee's store, and with an object; `shift` (method:shift).
- **Mutants.** `tests/runtime2/b06/mutants.sh`: mutant 1 fails 8 checks and mutant 2 fails 10 (the comparator fixtures count on both builds).
  Both are caught.
- **No refusal for safety became admitted.** A compile-only differential (`NIFT_VM=classify`,
  nothing runs) of all 2,490 `tests/**/*.f` programs ≤ 16 KiB compared mutant 1, mutant 2 and
  the fixed production and probe builds: **not one program changes admission**.
  - The programs r2 admits and the fixed build still refuses (26) are all deliberate refusal
    fixtures: b3/refuse N1, b4/refuse context:* and struct-this-binding, and the b4b H1
    unknown-callee cases (`call_unknown_callee`, `thread_unknown_callee`, `escape_export`:
    builtin-receiver). None shares B06's cause.
  - The cp14b failures B4b had beyond these two (`nested_sort_internal`, `nested_sort_escape`,
    `return_comparator_result_error`) were fixed earlier. test-rt-vm-mutants (B07) is a policy gap,
    not an admission difference.

**Gates (local, GCC 13.3).**

| Class | Gates |
| --- | --- |
| PASS | test-runtime2-b06, test-runtime2-b06-mutants (2 caught), test-runtime2-b4b, test-runtime2-b4b-gen-alias, test-runtime2-b4b-fixpoint, test-runtime2-b4b-work (deterministic analysis-work budgets), test-runtime2-b4b-mutants (20 caught, A28 included), test-runtime2-b3, test-runtime2-b4, test-runtime2-b5, test-runtime2-b5-mutants, test-runtime2-b5-generators, test-runtime2-b5-evalorder, test-runtime2-guardoff, gen_operators (880 programs, 795 admitted, floor 795, 0 bad), test-rt-lint |
| PASS, was FAILED-BASELINE | test-runtime2-cp14b: `script_return_kernel_error` now PASS; `comparator_factory` UNVERIFIED (method:sort profile gate on GCC 13.3), like every comparator case |
| Linode (GCC 15.2) | comparator_factory on the production build; cp14b's comparator cases with detail |

**Cost.** No VM execution path changed. Both files are compile-time inference (rt-lint
`cold-file`), so no callgrind run applies. The deterministic analysis-work gate
(test-runtime2-b4b-work) passes unchanged.

## 38. B04: the cp14b comparator expectations under copy-sort-commit (C45)

**Status: expectations corrected, pending verification.** B04 is not downgraded here. The owner
decides that only after independent verification and the Linode GCC 15.2 run.

**Owner approval (2026-11-09 17:31).** Regenerate the cp14b comparator receiver-view expectations
under the approved B5 copy-sort-commit semantics. That is owner ruling **Q7, 2026-11-07**
(compatibility.md row "comparator `sort`": aliases see the original order; a failure leaves the
receiver unchanged; identity is kept). This commit changes tests and expectations only; nothing in
src/ changes. The evidence is triage `ADJ-B04.md` and `work/b04/`.

**The historical evidence is unchanged.** `tests/runtime2/cp14b/fixtures/d4-traces.json.gz` keeps
its original sha256 pins in `SHA256.json` (`03f9f45b…` for the file, `0219807d…` for its content).
It records the superseded in-place semantics.

**The derived expectations.**
- `tests/runtime2/cp14b/q7_expectations.py` generates
  `fixtures/d4-q7-expectations.json.gz` mechanically, for 234 cases. The frozen data is copied as
  is wherever Q7 does not change it:
  - every comparator call's pair (l, r), in order;
  - the call count;
  - the failure flag. The failure is the caught `throw error("stop")`, printed `Estop`; the D4
    programs print no position for it.
- Only two things come from the Q7 model:
  - each call's view of the receiver is the original array, as the D4 program builds it;
  - the final array is the frozen final after a completed sort, and the original array after a
    failed one.
- Pins in `SHA256.json`:
  - the file, `d4-q7-expectations.json.gz` = `4864d3ab4dc65968d3522cc13166856403de1574598ddf795b338a87ec146886`;
  - its content, `d4_q7_uncompressed` = `a5ad90977cbb3f6695ae194b8bcfd246e4a5e7ef237aa92ab1324d673b50e188`.
- `--check` regenerates the content from the pinned frozen traces and requires it to be
  byte-identical, with both pins holding.
- `--self-test` has negative controls: a changed pair, receiver view, final array, call count or
  failure flag is refused.
- `test-runtime2-cp14b-sort` runs both before the replay.
- `sort_replay.py` checks the derived file first (generator identity and pins) and re-asserts per
  case that the pairs, count and flag are the frozen trace's. It then requires each case's actual
  `{trace (l, r, a per call), final, calls, failed}` to equal the derived expectation exactly. No
  field was dropped or loosened.

**The DRAIN control** (`tests/runtime2/cp14b/mutations.py`, the normal control of mutants 1, 2
and 9).
- Under Q7 the VM is pinned to `CATCH`, `[{"k":3},{"k":1},{"k":2}]`, `AFTER` (exit 0, no
  stderr).
- Legacy is pinned to its own recorded output: `CATCH`, `[{"k":1},{"k":3},{"k":2}]`, `AFTER`.
- The legacy difference is recorded as approved under Q7 (owner ruling 2026-11-07).
- On the probe build both sides match.

**Explicit failure tests** (`tests/runtime2/b5/sort/`, run on the probe build by
`test-runtime2-b5`; hand-derived goldens):
- `q7_failure_throw.f`: the comparator throws at its k-th call, for every k the sort makes (caught).
  After each failure the receiver and its aliases are unchanged, element by element: the direct
  alias, the alias in an object field and the alias in an array. A push through the receiver is
  then seen through every alias. VM: 0 bad. Legacy: 106.
- `q7_failure_bad_return.f`: the comparator returns a string at its 3rd call.
- `q7_failure_runtime_error.f`: the comparator body divides by zero at its 3rd call.
- In both fatal cases, every comparator call up to the failure sees the receiver and its aliases in
  the original order, element by element. Legacy shows the partial sort. The fatal error and its
  position equal legacy's: `sort: comparator must return bool` at 10:1, and
  `return: division by zero` at 4:3.
- In nift these two failures are fatal. A `try` does not catch them, and neither does a thread
  join. So "unchanged after the failure" can only be observed through the views up to the failure.
- B5 sort fixtures: 8 programs, all pass on the probe build.

**Case counts (local, GCC 13.3; the authoritative check is Linode with GCC 15.2).**

| Binary | Admitted, equal to the Q7 expectations | Refused `method:sort` | Mismatches |
| --- | --- | --- | --- |
| The adjudication's probe build (pre-B06 tree, `work/b04/nift-guardoff`, the code Linode ran) | 192 | 42 | 0 |
| This tree's probe build (with C44) | 202 | 32 | 0 |
| Production (GCC 13.3) | 0 (all 234 refused by the libstdc++ profile gate) | 234 | 0 |

- **The first row is Linode's split.** The adjudication found 50 exact plus 142 mismatching
  against the frozen traces, and 42 refused; the case IDs match `replay-guardoff.json` 234/234.
  Against the derived expectations, every one of the 192 matches.
- **What changed between the rows.** The 10 cases that moved from refused to admitted are
  `0-{asc,desc,dups,equal,seed}-{0,1}`, comparator sorts of an empty literal. B06's family-1 fix
  (C44) proves the empty element type, so they are now admitted. Their result equals the Q7
  expectation and legacy: no calls, final `[]`. Weekend/r2 refused them too.
- **This is a B06 consequence the B06 report did not name.** The corpus differential found no
  admission change in `.f` files, and these D4 programs are generated.
- **The remaining refusals** (32, or 42 without C44) are record arrays of 17, 31, 32 and 33
  elements. They were refused on r2 too (P2). They still count as failures in the replay.

**Gates.**
- **Pass:** `test-runtime2-cp14b-sort` (Q7 check, self-test, then 234 UNVERIFIED on production
  GCC 13.3), `test-runtime2-cp14b`, `test-runtime2-b5` (the sort fixtures on the probe build),
  `test-rt-lint`, the certification guard, and test-integrity (57 findings, unchanged).
- **`test-runtime2-cp14b-mutants`: still fails locally (pre-existing).**
  - Mutants 1, 2 and 9 are UNVERIFIED (comparator), and 3-8 and 10 are DETECTED.
  - Mutant 11 (`missing-const-guard`) stops at its normal control. Its witness is
    `const_precedence`, an owner-approved B5 difference (Q2 option B), which `functional.py`
    already pins in `APPROVED` but `mutations.py` compares for legacy parity.
  - The HEAD harness fails the same way. On Linode it was masked by the DRAIN failure and will
    surface once DRAIN passes.
  - Left unchanged: it is outside the DRAIN item. The same treatment, the Q2 pin from
    `functional.APPROVED`, is proposed.
- **Linode, to confirm:**
  - `test-runtime2-cp14b-sort` with `sort.json` kept: 202 equal to the Q7 expectations on GCC 15.2
    production, 32 refused;
  - the DRAIN control;
  - comparator_factory.

**Also recorded (B06 review, low).** The newly admitted `fs := []; print([1].map(fs.pop())…)` errors
at 1:25 where legacy errors at 1:17. r2 does the same, and the already admitted `fs[0]` variant has
the same difference. This belongs to the P2 error-position group (with `first`/`last` on an empty
array, section 37).

## 39. Review of C44 (B06): condition C1, the vacuous b4b-mutants suite signal (C46)

**Review (`REVIEW-B06.md`): ACCEPT-WITH-CONDITIONS, no unsound admission.**

**Condition C1.** `tests/runtime2/b4b/mutants.sh` ran `b4b_types.py --quick` without `--guardoff`.
- Since B5 the production build shares references. In that mode the unmutated build fails 64
  checks here (61 on the reviewer's run): `trim: unsupported` against legacy, the B5 shares.
- Every mutant was therefore "caught" by the suite signal alone, which proves nothing.

**Fix (tests only).**
- The suite step runs `--guardoff --quick`, as `test-runtime2-b4b` does. On the unmutated build it
  gives 1,897 checks with 0 failures.
- Every signal first runs on the unmutated build (`signals 0`: the tree's own objects, no
  `NIFT_B4B_MUTANT`): the unit oracle, the suite, the work gate, guard-off and the fixed-point
  gate. If any of them fails there, the gate fails ("a mutant cannot be judged by it").
- Each mutant now reports the signals that caught it.

**Re-check (all 20 mutants; the negative control passes).**

| Mutant | Caught by |
| --- | --- |
| A2 | oracle 48; suite 54; guard-off |
| A19 | oracle 21; suite 15; guard-off |
| A20 | oracle 1 |
| A21 | oracle 8 |
| A22 | oracle 1 |
| A23 | oracle 9; suite 5; guard-off |
| A24 | oracle 2 |
| A25 | suite 8; guard-off |
| A26 | oracle 65; suite 31; work gate 16; guard-off |
| A27 | oracle 56; suite 28; guard-off; fixed-point gate (15) |
| A28 | oracle 6; suite 5; guard-off |
| **A29** | **SURVIVED** |
| A30 | oracle 6; suite 4; work gate 11; guard-off |
| A31 | oracle 65; suite 31; work gate 6; guard-off |
| A32 | oracle 66; suite 19; guard-off; fixed-point gate (19) |
| A34 | oracle 4 |
| A35 | oracle 47; suite 21; guard-off; fixed-point gate (13) |
| A36 | oracle 31; suite 11; guard-off; fixed-point gate (11) |
| A37 | work gate 6 |
| A38 | oracle 11; suite 22; guard-off |

**A29 survives, and it is an equivalent mutant since B5 1 (141b847e).** No fixture can catch it.
- A29 removes `expose(value)` in `Inference::site_store` (`infer_sites.cpp:298-300`): a store into
  an earlier session value that has no site here.
- Since B5 1, that same branch then stores into the session heap,
  `site_store(heap_sited(container), …)` at line 303.
- The heap's site classes are in `exposed_` from the start of every session unit
  (`infer_session.cpp:135-137`: "earlier code reaches every earlier value").
- `site_store` exposes every value stored into an exposed class (`infer_sites.cpp:319`, the M1
  rule "stored later into a container earlier code reaches").
- So the guarded `expose(value)` runs a second time on the same path, and removing it changes no
  inference result.
- The evidence agrees:
  - the unit oracle's M1 cases all pass with A29: `session-exposed`, `-push-exposed`,
    `-index-exposed`, `-member-exposed`, `-closure-exposed`, `-late-store`, `-store-after-call`;
  - session probes built for it were refused identically, or ran identically, on the A29 binary
    and production (store into an earlier array, object or map, then an earlier function's
    mutation through a local);
  - the guard-off generator passes.
- **The test-only fix is not possible.** The gate now fails, on A29 alone, until the owner decides.
  The options are:
  - (a) retire A29 as equivalent, the way B5 retired its mutant 2;
  - (b) move the hook to line 319, the mechanism that carries M1 since B5 (a `src/` change: a new
    `NIFT_B4B_MUTANT` value). The existing M1 oracle cases should catch it.
- The harness is left with A29 in its list, so the result is visible.

**The same vacuity pattern in other mutant gates.** I looked for a suite-failure kill signal with
no unmutated control, and ran each suite on the unmutated build:
- **No unmutated control in the harness, but the suite passes unmutated today (not vacuous now,
  latent):**
  - b3 (`b3_structs.py`: 468 checks);
  - b4 (`b4_dispatch.py --quick`: 6,085);
  - refsem (`refsem_a.py`: 566, production and probe);
  - collections (`weekend_collections.py`: 284);
  - coro-module (`coro_module.py`; it checks only that the binary differs);
  - args-cmd (`args_cmd.py`);
  - packages (`packages.py`);
  - b5 (`run.py`, all 148 programs on the probe build, plus `refsem_a.py`, `gen_operators.py` with
    880 programs, and `d2_append.py`).
- **With an unmutated control:**
  - safety (`base.log`);
  - host mutants ("a passing unmutated baseline first");
  - closures-s1 and for-s2 (the suite runs on the target first);
  - cp22 (the normal sha256);
  - cp14b mutations (a per-witness normal control, plus §38's DRAIN pin);
  - b06, which gets one in this commit.
- **Not individually audited:** the bespoke cp20/cp21 fd/terminal/protocol mutants. They use
  per-mutant expected signatures, not a suite-failure signal.

**Low (record only):** the error-position item is in section 38 (P2 group).

## 40. cp14b-mutants: the approved-difference controls (C47)

**What changed.** Mutant 11 (`missing-const-guard`) has `const_precedence` as its witness. Its
normal control now gets the treatment DRAIN got (section 38): the VM is pinned to the approved B5
output, and legacy to its own recorded output. The approval is owner Q2 option B, `const` is
binding-only (B5).
- `tests/runtime2/cp14b/mutations.py` reads both pins from `functional.APPROVED`, the one place the
  approval is recorded:
  - VM: `ARG`, `9`, exit 0;
  - legacy: `cannot mutate const array: a` at 1:59, exit 1.
- In general, every witness that `functional.APPROVED` pins is now pinned the same way in the
  mutant controls.

**Result: mutant 11 now SURVIVES.** It has had no hook since B5 2 (519b9690, "guard removal"):
- that commit deleted the `NIFT_COLLECTION_MUTANT == 11` branch together with the const-array
  guard, which is the approved Q2B semantics;
- `src/` has no `NIFT_COLLECTION_MUTANT … 11` left, so its build is the unmutated build;
- it was never noticed, because its control failed first: since B5 on every host, and on Linode
  behind DRAIN.

As with A29 (section 39), I propose retiring it with this proof, like B5's mutant 2, pending
review. Until then it stays listed and the gate fails on it alone.

**Other mutants' controls.** Mutants 3-8 and 10 use the witnesses `snapshot_mutation`,
`core_factory`, `predicate_error`, `short_any`, `sort_by_stable`, `group_by_each` and `block_error`.
- All of them pass exact legacy parity on the current build, and all are DETECTED.
- Their hooks exist: hooks 6 and 8 are spelled `!= n`.
- None carries a stale pre-B5 expectation.
- Mutants 1, 2 and 9 use DRAIN, pinned in C45. They are UNVERIFIED on GCC 13.3 (the profile gate)
  and are judged on Linode.

## 41. B05: the for-s2 bench under the B5 snapshot contract (C48)

**Owner approval (2026-11-09 17:31): correct the bench, not the runtime.** Evidence: triage
`ADJ-B05.md`. Tests only; the snapshot semantics are unchanged (B5 D5, b5-refsem.md 1 and 10.9:
every loop over a binding iterates an explicit, budget-checked O(n) entry snapshot). Skipping the
snapshot is deferred.

**Budget derivation** (`tests/runtime2/for_s2_bench.py`, written out in its docstring):
- `snapshot(n) = n × 16 + 64` bytes. This is the copy's storage as the VM charges it: n `Value`s
  of 16 bytes, plus the vector's fixed part.
- The reservation checked before the copy is `n × 16 + 128` (`Vm::reserve_snapshot`,
  `src/rt/vm_iter.cpp`), so a budget stop leaves nothing half built.
- for budget = `base_peak + (N × 16 + 128) + MARGIN`:
  - `base_peak` is the array alone: base.f's measured heap_peak, 16,777,280 bytes;
  - N = 1,000,000;
  - MARGIN = 64 KiB, for the program's own small values. It is the same tolerance the write
    loop's no-copy check already uses.
- Locally that is 16,777,280 + 16,000,128 + 65,536 = **32,842,944**. A second copy (16 MB) does
  not fit, so the budget asserts exactly one snapshot.
- The while loops keep the bench's previous budget and their no-copy check.

**Checks (all pass locally):**

| Check | Expected | Measured |
| --- | --- | --- |
| min_for, arith_for: heap_peak | base + snapshot(1M) = 32,777,344 | 32,777,344 |
| … heap_live at exit (snapshot released) | 16,777,280 | 16,777,280 |
| seq3_for.f: three loops in sequence (no accumulation) | 32,777,344 / live 16,777,280 | same |
| fn_for.f: a looping function called twice (no accumulation) | 32,777,344 / live 16,777,280 | same |
| loop_1k.f: one level over 1,000 | 16,448 + 16,064 = 32,512 | 32,512 |
| nested2_1k.f: two levels over 1,000 (one snapshot each) | 16,448 + 2 × 16,064 = 48,576 | 48,576 |
| nested3_100.f: three levels over 100 | 2,112 + 3 × 1,664 = 7,104 | 7,104 |
| write_for.f | runs on the VM, stdout equal to legacy (`1000000`), peak base + snapshot | yes, 32,777,344 |
| negative: budget base + snapshot/2 = 24,777,312 | the documented heap-budget stop: exit 70, outcome internal-error, reason `memory budget exceeded`, nothing printed | yes |

- **Tolerance** is 256 bytes. The VM charges exact sizes in its own accounting, not through malloc,
  and 256 is below the smallest snapshot checked (1,664 bytes), so a missing nesting level fails.
- **write_for.f.** The bench's stale `GUARDED["write_for.f"] = "reference-guard:lent"` is
  replaced. B5 deleted the Phase A guard, and the loop now runs legacy-equal.
- **The performance objective** is reported as before. It is a wall-clock question for Linode.

**New P2 item (record only; the message is unchanged).** The heap-budget stop is a documented
resource stop: exit 70, "a resource stop" (b0-safety.md §1); memory_c1.sh requires `exit 70, memory
budget exceeded`. The CLI nevertheless renders it as `NIFT_VM internal failure: memory budget
exceeded`, and "internal failure" misleads the user for a budget the user set. This joins the P2
diagnostics group.

**B05 status:** bench corrected. Reclassifying it from P0 to a P2 test defect is the owner's
decision after verification; Linode GCC 15.2 should give byte-identical figures, since the sizes
depend only on `sizeof(Value)`.

## 42. A29 retired as an equivalent mutant (C49)

The independent review of C45/C46 (`REVIEW-C45-C46.md`, item 8) confirmed that A29 is equivalent,
but with a **corrected reason**. Section 39's "re-exposed at line 319" does not apply to the cases
that reach the hooked line. The proof as the review established it:

1. **Only two kinds of session statement reach the hooked branch** (`infer_sites.cpp:298`,
   `session_ && !has_site(container)`). The review's trap build (`__builtin_trap()` there) showed
   that it is reached only by:
   - stores into an earlier **non-aggregate** value (int, bool, string, null, a lambda) through
     `push` or `[i] =`;
   - stores into an earlier **struct** instance (`push`, `[0] =`, `["k"] =`, `.k =` on an
     undeclared field).

   Earlier arrays, objects, nested and returned or aliased arrays carry the heap site through
   `sited()` and never reach it.
2. **For those cases A29 really does skip the exposure.** The recursive
   `site_store(heap_sited(...))` returns at `if (!aggregate) return;`, before line 319.
3. **The skipped exposure is never observable.** Every such statement is refused for its
   receiver's kind alone (`type:push`, `type:element-assignment`, `index-target`,
   `static-error:member-assign` / `struct-no-field`). That holds in production and in A29 alike,
   inside a dead branch too. The review ran 1,056 session programs with identical output on
   production and A29: 8 earlier-value kinds × 3 stores × 4 values × 4 later uses, guarded or not,
   plus 288 with aggregate and struct receivers and receiver-kind-sensitive later uses. Every
   program reaching the line was refused.

**Retirement, following B5 mutant 2's precedent.**
- A29 leaves the default list of `tests/runtime2/b4b/mutants.sh`. A comment there points here.
- The hook stays in `src/rt/infer_sites.cpp`, and section 39 keeps the original analysis.
- `tests/runtime2/b4b/a29_precondition.py` runs in the gate first and **pins the precondition**.
  It covers 48 session statements: each earlier kind (int, bool, string, null, lambda, struct) ×
  each store (push, index, key, member), live and in a dead branch. Every one must be refused for
  its receiver-kind reason, with the statement's own output never printed.
- If any is ever admitted, the gate fails with "restore A29": the line-298 exposure would become
  observable, and A29 must come back.
- `test-runtime2-b4b-mutants` passes: the negative control, the A29 precondition (48 refused), and 19 of 19 mutants caught (each by the same signals as in section 39).

**Low (record only, the review's item 9).** The latent mutant gates without an unmutated control
should get the same `signals 0`-style negative control later: b3, b4, refsem, collections,
coro-module, args-cmd, packages and b5. Their suites pass unmutated today, so none is vacuous now.

## 43. B01: the VM's read limit enforced before any unbounded allocation (C50)

**Symptom (P0).** On the VM, `NIFT_READ_LIMIT` was not enforced as legacy enforces it.
`tests/runtime2/read_limit.py` failed eight cases that are not refusals:
- one byte over the limit read silently: `open`, `file().open`, a FIFO, and `NIFT_READ_LIMIT=1K` / `=4096` (the script went on to `AFTER`);
- `ifstream("/dev/zero").read_line()` and `open()` of a regular file 3 x the limit died with `std::bad_alloc`, exit 70, under the test's address-space limit;
- with `NIFT_READ_LIMIT=1g`, a file of 1 GiB + 1 reported `open: file exceeds the memory budget` instead of the limit error.

**Root cause.** Three defects in the VM read natives. Legacy is correct in each case.
1. **Wrapping check.** `read_whole` (`src/rt/natives/host_fs_stream.h`) reads 64 KiB chunks and
   checked `total > limit - got`. When one chunk is larger than the limit (any limit below
   64 KiB), `limit - got` wraps and the check never fires.
2. **Sized before checked.** `read_source` (`host_fs_internal.h`) charged a regular file's whole
   size to the heap budget, and `read_whole` pre-sized its buffer to it, before any comparison
   with the limit. So a file 3 x the limit was a 3 x allocation (bad_alloc), and the heap budget's
   message won over the limit's.
3. **Growth past the limit.** `read_line` (and `read_all`, `host_stream_handles.cpp`) grew through
   `BudgetedText`'s doubling. The per-character check bounds the *content* at the limit, but the
   *buffer* doubled to about 2 x the limit (a 64 MiB limit reached a 120 MiB buffer plus the old one).

**Fix (bounded, existing mechanisms: `ReadLimit.h`; no heap-budget change).**
- `read_whole`:
  - a regular file larger than the limit is rejected from its size, unread, before any buffer is sized;
  - the chunk check is `got > limit - total` (`total <= limit` holds, so it cannot wrap);
  - an unknown-size buffer (FIFO, device) is capped at the limit and, past half the limit, grows straight to it (`growth_target`'s rule), so it never holds more than 1.5 x the limit while it grows.
- `read_source`: the same size check, before the heap-budget charge.
- `read_line` and `read_all`: the buffer grows by `growth_target`, bounded by the limit (or by a smaller read count).
- Errors keep legacy's text and position (`<op>: <what> exceeds the read limit of N bytes (NIFT_READ_LIMIT)`, fatal, nothing printed after it). The controls compare the VM's exit status, stdout and stderr with `NIFT_VM=0` byte for byte.
- **Short reads.** `read_whole` uses `istream::read`, which stops early only at end of file: the filebuf repeats `read(2)` across short reads, and `total` accumulates over the whole read. `strace` of a paced FIFO shows `read(3, ..., 65536) = 1`, then `= 999`, `= 4096`, `= 3`: each short read counted, and the limit error at the end.

**Production sha256.** `nift` at C50: `864952359673b64ef5e5918c75144bd9d14cb32cc44bc8f7fc68bcdf1eb8a496`
(was `c528e7eb...`). The pre-B01 build, made from 03b31fa2's six read-native objects linked in
production order, reproduces `c528e7eb...` exactly. That build fails 24 of the new controls.

**Permanent negative controls.**
- `tests/runtime2/read_limit_controls.py`: 68 checks, each VM == legacy.
  - **A, 1000-byte limit** (below one chunk): `open`, `file().open`, `ifstream.read_all` and `read_line` succeed at exactly the limit and fail fatally one byte over.
  - **B, non-power-of-two limit (40 MiB + 12345)**, under RLIMIT_AS = baseline + 1.5 x limit + 8 MiB:
    - a regular file of exactly the limit reads;
    - a sparse file 3 x the limit, `/dev/zero` (open, file().open, read_all, read_line), and a FIFO one byte over all fail with the limit error, never exit 70.
  - **C, short reads**: a FIFO writer paced chunk by chunk (1, 999, 4096, 3, 65535, 4097, 17 bytes).
    - Pacing: the writer waits on the pipe's own fill level (FIONREAD) until the reader has drained each chunk. No sleeps.
    - 299999 and 300000 bytes, then close, read whole; 300001, and 4 x the limit, hit the limit error (open, file().open, read_all, read_line).
- `tests/runtime2/read_limit_mutants.sh` (`make test-runtime2-read-limit-mutants`, a prerequisite of `test-runtime2-host-deps`):
  - rebuilds the six TUs that include the read natives with `NIFT_READ_LIMIT_MUTANT=n`;
  - checks that every including TU is in its list;
  - requires the control's objects and binary to be byte-identical to production;
  - requires each mutant's objects to differ from the control's.

  | Mutant | Change | Result |
  | --- | --- | --- |
  | 1 | the pre-B01 read_whole/read_source | rejected (16 failing checks) |
  | 2 | stream growth unbounded (read_line, read_all) | rejected (8: exit 70) |
  | 3 | no per-chunk limit check | rejected (16) |
  | 4 | no read_line limit check | rejected (8) |
  | 5 | no size check before sizing | rejected (4: exit 70 on the 3 x file) |
  | 6 | `readsome`: a short read ends the read | rejected (24) |
  | 7 | the byte count restarts at every read | rejected (16, the paced FIFO among them) |
  | 0 | control | passes |

**Remaining read_limit.py failures (24, all refusals; none is a limit defect).**
- **B27 refusals (23):**
  - `builtin-bytes:open_bytes` (7): exact limit, empty file, one byte over, `/dev/zero`, `/dev/full`, a FIFO writer that never stops, and the default 1 GiB limit;
  - `host-handle-flow` (4): `read_all_bytes`, `read_bytes(huge)`, `read_val`, `read_bytes(limit/2) x 16`;
  - `statement:Command` (1): `>> token`;
  - `callable:run` / `callable:cmd` (8): captures of exactly the limit, one byte over and 128 MiB, `run(yes)`, `head -c 150000000`, `cmd(yes).pipe(cat)`, the descendant kill, and stderr capture;
  - `callable:read` (3): stdin lines.
- **One more refusal, outside the B27 list (1):** "read_line: line of exactly the limit" is refused as `type:length`. The case prints `s.read_line().length()`, and `read_line()` returns `string|null`, so `.length()` is not proven: a sound refusal, unchanged from 03b31fa2. The same boundary (a line of exactly the limit, then the next line) passes on the VM in read_limit_controls.py A.

**Gates (local, GCC 13.3).**

| Class | Gates |
| --- | --- |
| PASS | test-runtime2-certification-guard, test-rt-lint, test-runtime2-read-limit-mutants (7 of 7 rejected, control identical to `nift`), read_limit_controls.py (68), read-limit unit, host_deps.py, file_copy_move_freedom, build_write_ownership, test-runtime2-host-memory (61), test-rt-host's cp17 n2-rebuild mutants, handle mutants (incl. `host_file_handles.cpp` mutants killed), stream-resource guards, file/stream handles |
| test-runtime2-host-deps | every line passes except read_limit.py: the 24 refusals listed above |
| test-rt-vm (`make -k`) | every prerequisite passes (cp14b with its comparator cases UNVERIFIED on GCC 13.3, as before). The own recipe passes through vm_parity (PASS) and vm_host (160 PASS, 0 FAIL, 5 REFUSED), then stops at vm_modules: 2 cases `import-scope:collision`, FAILED-BASELINE (sections 9 and 27) |
| FAILED-BASELINE, sources untouched by C50 | test-rt-handles (`no_handle_strings`: `src/rt/render.cpp:11`); test-rt-handles-guards (handle-bench Ir 2293/2370 against 2288/2369.4; the bench builds only `src/rt/handle*`, `handles/`, `value`, `string`, `heap_account`); test-runtime2-cp17-handles and -routing-mutants (the routing mutant of `src/ParserScript.cpp` no longer compiles: `run_script_on_vm` signature) |
| Not completed here | test-rt-host's cp17-unwind-mutants (stopped by the 7000 s timeout, every mutant killed until then); test-rt-host-mutants (`host_mutants.sh` builds 7 full tree copies: the disk ran out; its recorded fingerprint is FAILED-BASELINE, "host mutant 1: build failed") |
| Legacy unchanged | Legacy reads none of the changed code: only `src/rt/vm_handles.cpp` and the six read-native TUs include it. `NIFT_VM=0 read_limit.py` gives identical output on the C50 binary and on the c528e7eb binary (all pass) |

**Cost.** Per chunk or per growth step, one comparison more. No change per byte.
`read_line` adds one capacity test per character, then the existing append. There is no
wall-clock benchmark (standing rule), and no callgrind gate covers these natives.

## 44. Review of C47-C49 (ACCEPT-WITH-FINDINGS, all Low): cp14b mutant 11 retired; the A29 precondition widened (C52)

Tests and docs only. The production binary is unchanged (`864952359673b64e...`, C50).

### 44.1 cp14b mutant 11 retired as obsolete

**History (kept).**
- Mutant 11 (`missing-const-guard`, `src/rt/compiler_array_mutation.cpp`, witness `const_precedence`) removed the VM's const-array guard. That guard implemented legacy's "cannot mutate const array".
- Owner Q2 option B (`const` is binding-only) made the guard wrong. B5 2 (519b9690) deleted it together with the mutant's `NIFT_COLLECTION_MUTANT == 11` hook.
- The mutant stayed listed. Its control failed first, so its emptiness went unseen until C47 pinned the approved control (section 40). From then on it SURVIVED.

**Proof (the reviewer's, reproduced here).** `compiler_array_mutation.cpp` was compiled with
`mutations.py`'s own command, once with `-DNIFT_COLLECTION_MUTANT=11` and once without. The
preprocessed sources are equal and the objects byte-identical. No `NIFT_COLLECTION_MUTANT` test
against 11 is left in `src/`.

**Retirement, following A29 and B5 mutant 2.**
- Mutant 11 leaves `MUTANTS` in `tests/runtime2/cp14b/mutations.py`. A comment and `RETIRED` record it there.
- The `const_precedence` witness pin stays. Every `functional.APPROVED` pin whose witness no mutant uses now runs as an **approved control on its own**: `const_precedence` and `const_capture`. The VM must equal its approved output and legacy its recorded one, and the program must run on the VM.
- No replacement mutant: the contract's coverage is below.
- `test-runtime2-cp14b-mutants` passes:
  - both approved controls;
  - mutants 3-8 and 10 DETECTED; 1, 2 and 9 (DRAIN) UNVERIFIED on GCC 13.3;
  - design mutants: all DETECTED, 12 UNVERIFIED.

**The binding-only `const` contract and what covers each part.**

| Part | Covered by |
| --- | --- |
| 1. Rebinding a `const` is refused (D-009 #2, D-058) | `tests/runtime2/cp25/const/` through `test-cp25r` (in `test-rt-vm`): assign, compound, increment, decrement, assign before the value, in try, shadowed, unreached, in loop bodies and callees, for int, string, bool, double and null in every context, the VM against legacy; also `cp13b` `s03_binding_readonly`, `vm-corpus/cp04/callee_const` and `semantics/loop_const` |
| 2. Mutating a mutable collection through a `const` binding is allowed (Q2 B) | `cp14b/const_admission.py` Q2B (an element store at top level, in a function, through a lambda, and after a side effect, each pinned to its B5 result) and SHARED (a nested element through the loop binding); `functional.py` exact pins `const_precedence` and `const_capture`, also approved controls in `mutations.py` |
| 3. Immutable runtime values stay protected | **strings and numbers**: new in `const_admission.py` (IMMUTABLE, 7 programs; no existing test pinned this through a const binding). A const string or number "changed" through an alias (`+=`, `*=`), a method (`to_upper`), a function parameter, a lambda, or an array element keeps its value: each change makes a new value, on the VM and equal to legacy. Element stores and `push` on a string are errors in legacy and refusals (or the same errors) on the VM. **`args`** is immutable through every alias (owner Q1): `refsem/approved/args_copy_push` (`cannot mutate const array: b`) and `args_cmd.py`. **Booleans and null** have no in-place operation; rebinding them is part 1 |

The language treats as immutable: strings, numbers (int and double), booleans and null, and `args`
(the one immutable collection, Q1). Arrays, objects, maps, sets, stacks and struct instances are
mutable through every binding, const or not (Q2 B). Handles (mutex, channel, files) keep their identity.

### 44.2 The A29 precondition widened

The review found more earlier-value kinds that reach `infer_sites.cpp:298`: a double, a declared
`fn` used as a value, `mutex()`, `channel(1)`, `set()` and `error("e")`.
- `tests/runtime2/b4b/a29_precondition.py` adds each with the same four stores (push, index, key, member), live and in a dead branch.
- Each is refused for its receiver-kind reason: `type:push` (a set's push is `method:push`), `type:element-assignment`, `index-target`, `static-error:member-assign`.
- Result: 96 session statements (12 kinds x 4 stores x 2), all refused, none run.

### 44.3 Low, recorded only

`tests/runtime2/for_s2_bench.py` still mixes the deterministic memory checks with a wall-clock
ratio that only reports MISS (never fails). Unchanged, as asked.

## 45. Review of B01 (ACCEPT-WITH-FINDINGS): F1, `read(n)` counts the bytes read (C53)

**F1 (parity).** The VM failed `s.read(n)` when n > L (the limit) from the count *asked for*. On a
file of exactly L bytes:
- legacy's `read(1001)` (L = 1000) prints `1000`;
- the VM failed at 2:6 with `read: a read of ... exceeds the read limit`.

c528e7eb behaved the same. The difference predates B01 and is on no approved list.

**Cause.** `stream_read_impl` (`src/rt/natives/host_stream_handles.cpp`) refused
`requested > limit` before reading anything. Legacy (`ReadLimit.h` `read_bounded`, called from
`ParserExpression.cpp`) counts the bytes actually read. It reads at most the room plus one probe
byte, and fails only when that byte exists.

**Fix.**
- `read(n)` reads and buffers at most min(n, L) bytes.
- When n > L and L bytes were read, one byte more is probed, as `read_all` already did. If it exists, the result is the limit error, with legacy's text (`a read of <path>`) and position.
- No allocation is proportional to n. Growth stays bounded by min(n, L) (`growth_target`, section 43), so the peak stays within 1.5 L.

**Controls** (`read_limit_controls.py` D, 7 new checks, 81 in all). Each is VM against legacy on exit, stdout and stderr, unless marked otherwise.
- `read(L+1)` and `read(huge)` on L bytes return L bytes.
- `read(L)` on L+1 bytes returns L bytes.
- `read(L+1)` and `read(huge)` on L+1 bytes fail with the limit error.
- `read(huge)` on `/dev/zero` stops at the limit under RLIMIT_AS = baseline + 1.5 L.
- `read(huge)` on a sparse file of exactly L (40 MiB + 12345) under the same RLIMIT_AS reads whole. This one is VM only: legacy itself dies with `std::bad_alloc` there, because it holds the result twice.

**Mutant 8** (`NIFT_READ_LIMIT_MUTANT=8`, the pre-C53 requested-count check) is rejected by 5 checks.
All 8 read-limit mutants are rejected, and the control is byte-identical to `nift`.

**Recorded only (the review's other findings; unchanged).**
- A regular file that grows after `stat` can transiently hold up to about 2 L. The buffer is pre-sized to the stat size, at most L, and the chunk loop then grows it to the limit before the next chunk fails.
- A limit error inside `return` is reported at 1:18 where legacy reports 1:11. This belongs to the existing error-position P2 group.
- test-rt-handles-guards: the handle-bench Ir counts (cycle 2293 > 2288, counted 2370 > 2369.4) are a new local signature. The bench binaries are byte-identical at 03b31fa2 and at the tip, so this predates B01 and is independent of it. **Flagged for a Linode follow-up.**

**Production sha256.** `d78510a29a95eddcf63768aa4f333b50507b63166f63c6f501fb7b1cc08d4bd5`.

**Gates.**
- test-runtime2-host-deps: every line passes except read_limit.py's 24 refusals (section 43), the same list.
- test-runtime2-read-limit-mutants: 8 of 8 rejected.

**Owner additions to C53 (C54, tests only).** The limit applies to the bytes actually read. These are added to `read_limit_controls.py`, each VM == legacy (93 checks in all):
- **D:** `read(L+1)` and `read(huge)` on a file *shorter* than L return its bytes.
- **C, paced FIFO** (short chunks, released as the reader drains each one; no sleeps):
  - `read(huge)` of exactly L bytes then close returns L bytes;
  - `read(L+1)` of L - 1 bytes then close returns L - 1 bytes;
  - `read(L+1)` of exactly L + 1 bytes is the limit error;
  - `read(huge)` from a writer that never stops (10^12 bytes) is the limit error at L. The read stops there and the writer then sees the reader gone, so nothing runs on indefinitely.
- **Bounded increments:** the read goes through a 64 KiB stack chunk. The running total is compared with the limit before the buffer grows, and the buffer grows by `growth_target`, bounded by min(n, L).
- **Mutant 8:** rejected by 13 checks. All 8 read-limit mutants are rejected.
- **Recorded only (owner):** the reviewer's observation that a file growing during a read can transiently need about 2 L stays a release-audit item. I found no unbounded allocation and no memory-safety defect there.

## 46. B02 (P0): file() buffer mode preconditions, legacy parity (C55)

**Symptom.** test-v410-file-buffer-parity row `open-r`:
`f:=file("data");f.open("r");print(f.modified());f.revert();print(f.modified());f.close();print(f.exists())`.
- Legacy prints `false`, then `1:49: revert: file is not open for writing`, exit 1.
- The VM printed `false`, `false`, then `1:96: exists: handle is closed`.

**Root cause (established).** Legacy's `execute_file_method` (`src/ParserExpression.cpp`) gives
each method one precondition:
- `need_open`: modified, tell, seek, close;
- `need_read` (open, mode r or rw): read, read_all, read_line, eof, cat;
- `need_write` (open, mode w, a or rw): write, write_line, flush, revert, save and the edit methods;
- closed only: copy, move, remove;
- none: path, exists.

These errors are fatal, not catchable. The VM natives (`src/rt/natives/host_file_handles.cpp`) had three divergences:
1. `revert()` checked only `open`, so a read-only buffer was reverted silently (the reported defect).
2. `flush()` also checked only `open`: legacy says `flush: file is not open for writing`.
3. `write_line()` delegated to `write()` and reported `write: file is not open (for writing)` instead of `write_line: ...`.

**Fix.** `revert` and `flush` take legacy's `need_write`, in legacy's order: arguments, then
open, then mode. `write_line` checks its preconditions under its own name before writing.
Nothing changes state before a precondition fails.

**The mode x method table (permanent: `tests/runtime2/file_mode_parity.py`, `make test-runtime2-file-mode-parity`).**
- The setup: 6 states x 20 methods, each in two programs.
  - **plain:** the call, then AFTER, then a cleanup that cannot fail.
  - **try:** the call inside try/catch, then the buffer state it left: `modified()` and, in r and rw, the contents.
- The comparison: VM (require) against legacy (`NIFT_VM=0`), on exit, stdout, stderr and every file's bytes. So an invalid-mode operation that partially changed the buffer or the file would be caught.
- Legacy's precondition errors are fatal, so after a refused operation only the file bytes remain to observe. Those are compared in every cell.
- 240 programs, 0 failures. Legend:
  - `=` the VM equals legacy;
  - `D-039` the approved closed-handle semantics, where the VM's exact result is pinned: `runtime.handle_closed` at the call (catchable), the files untouched, and a repeated `close()` idempotent;
  - `refused` the VM refuses the program before it runs (`host-handle-flow`, B27): copy and move return a file value;
  - `x/y` the plain and try programs differ: in try, the observation after `close()` is D-039.

| state | open | close | read | read(2) | read_all | read_line | eof | write | write_line | flush | revert | save | modified | tell | seek(1) | path | exists | copy | move | remove |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| never | = | = | = | = | = | = | = | = | = | = | = | = | = | = | = | = | = | refused | refused | = |
| r | = | =/D-039 | = | = | = | = | = | = | = | = | = | = | = | = | = | = | = | refused | refused | = |
| w | = | = | = | = | = | = | = | = | = | = | = | = | = | = | = | = | = | refused | refused | = |
| a | = | =/D-039 | = | = | = | = | = | = | = | = | = | = | = | = | = | = | = | refused | refused | = |
| rw | = | =/D-039 | = | = | = | = | = | = | = | = | = | = | = | = | = | = | = | refused | refused | = |
| closed | D-039 | D-039 | D-039 | D-039 | D-039 | D-039 | D-039 | D-039 | D-039 | D-039 | D-039 | D-039 | D-039 | D-039 | D-039 | D-039 | D-039 | refused | refused | D-039 |

**Negative controls (`tests/runtime2/file_mode_mutants.sh`, `make test-runtime2-file-mode-mutants`, a prerequisite of the table's target).**
- The harness rebuilds `host_file_handles.cpp` with `NIFT_FILE_MODE_MUTANT` 1-3:
  1. revert without need_write;
  2. flush without need_write;
  3. write_line reporting as `write`.
- All three are rejected. The control's object and binary are byte-identical to production.

**test-v410-file-buffer-parity.** The `open-r` row now matches exactly (exit, stdout, stderr,
files). The gate itself still fails. It stops at the first differing row, and of its 102 rows 15
pass on the VM. The other 87 need a broader change or a decision, so I did not fix them here:
- **80 refusals:** `opaque:nesting-limit` 39, `host-handle-flow` 26, `opaque:legacy-runtime-error` 8, `builtin-arg-fault-context` 2, and 1 each of `builtin-receiver:path`, `builtin-receiver:write`, `builtin-arity:open`, `builtin-arity:save` and `builtin-lazy-args:replace`. Nothing runs in these.
- **5 D-039 rows:** `open-rw`, `open-w`, `open-a` and `new-empty-write` call `exists()` after `close()`; `reopen-external-edit` calls `open()` after `close()`. Legacy allows both; the VM raises `runtime.handle_closed` (D-039). **For the owner:** `path()` and `exists()` need no open file in legacy, and reopening a closed `file()` buffer is a legacy feature. D-039 covers handles in general; whether it was meant to remove these for `file()` buffers needs confirming.
- **2 rows** (`closed-write-factory-count`, `readonly-write-factory-count`): `write` / `write_line` failing inside a `while` or `for` body. Legacy reports `script failed` (`CLI.cpp`: the prepared loop executor drops the method's message). The VM reports `write: file is not open`, at the same position. Matching it would mean copying a legacy message loss in loop contexts. **Needs a decision.**
- **Seen, not in scope:** at the end of a script the VM prefixes the left-open report with `1:1:` (`error: case.f:1:1: managed file left open: ...`), where legacy prints `error: managed file left open: ...`. This is CP17 cleanup reporting, not a method precondition.

**Production sha256.** `3161f6862597c5c828bf6bf20e99c0332a76263f4a141e66f36bb94fefd46ded`.

## 47. Review of C53-C55 (ACCEPT-WITH-FINDINGS): F-1 fixed, L-1 recorded (C56)

**F-1 (Medium, pre-existing; c528e7eb behaves the same).** `write_line([1])` or `write_line({...})`
reported `write: value is not directly renderable` on the VM. Legacy reports `write_line: ...` at
the same position.
- **Cause:** `file_write_line` delegated the value to `file_write`, which checks renderability under its own name.
- **Fix:** `write_line` checks the value's renderability under its own name, after its preconditions (legacy's order) and before anything is written.
- **Table:** `file_mode_parity.py` gains 8 calls: `write` and `write_line` of an array, an object, a number, a double, a bool and null, in every state. That makes 168 cells x 2 programs, 0 failures.
- **Mutant 4** (`NIFT_FILE_MODE_MUTANT=4`, renderability left to `write`) is rejected by 13 checks. All 4 file-mode mutants are rejected, and the control is byte-identical to `nift`.

**L-1 (recorded, deliberately unchanged).** Legacy's `save` records the build write and runs the
build-write guard (`guard_build_write`, `note_written`) *before* checking `need_write`. So a
`save()` on a read-only or closed buffer is still noted as a build write in legacy. The VM checks
the preconditions first and records nothing for a save that cannot happen. This is a deliberate,
safer difference: no effect is observable in the script, and a build does not see a phantom write.
**Production sha256.** `f6de04a56a92b5e6c710932c2e834cb32e08254c7acecd11bb46b39384ba38e1`.

## 48. B03 (P0): abort on out-of-memory during VM teardown (C57)

**Symptom.** `NIFT_VM=require tests/runtime2/memory/c1_push.f` under `ulimit -v` (400000 to
2097152 KiB) ended in SIGABRT (exit 134) with `Fatal glibc error: failed to register TLS
destructor: out of memory`, where it should exit 70 with the memory error. Here, on 03b31fa2 to
C56 (glibc 2.39), 15 of 24 cells failed on each of two runs.

**Root cause (gdb backtrace of a failing cell).** `abort` came from `__cxa_thread_atexit_impl`.
The call chain was:
- `__cxa_thread_atexit`;
- `HandleTable::in_own_hook()`;
- `HandleTable::shutdown()` / `shutdown_noexcept()`;
- `Vm::~Vm()`.

`t_hook_tables` (`src/rt/handles/table.cpp`, a `thread_local std::vector`) has a destructor, so its
first use registers that destructor with the C++ runtime. glibc 2.39 allocates the registration
and, if the allocation fails, **aborts** (it does not throw). In a VM run the first use is
`in_own_hook()` during the handle-table close/shutdown at the end of the run. After an
out-of-memory stop, memory is still exhausted at that point. `git log -S t_hook_tables` shows the
vector was introduced by **CP17 4fb04376** (2026-10-20, "fix adversarial-review findings R1-R14"),
the suspected earliest bad commit. That commit was not rebuilt here, which would need a full tree
copy. No memory corruption was found. ASan/UBSan and valgrind are clean on the new code; see below.

**Fix (bounded).**
- **`t_hook_tables`** becomes an intrusive list of the `HookScope` objects on the thread's stack, headed by a constant-initialised `thread_local const HookScope*`. It has no destructor and allocates nothing (the vector also allocated when pushed).
- **Every other destructor-bearing `thread_local`** goes through `nift::tls::owned<T>()` (new `src/rt/thread_owned.h`).
  - The object is created on first use with `new`, so exhaustion is an ordinary `std::bad_alloc`, handled as every allocation failure is.
  - It is reached through trivially destructible TLS pointers.
  - At thread exit, one POSIX TSD key destructor destroys the thread's objects newest first, as C++ destroys thread_locals. `pthread_key_create` and `pthread_setspecific` allocate nothing here, and fail softly.
  - The main thread's objects live until the process exits. A thread_local's destructor would have run at `exit()`. (Corrected in section 49: not every one of them only frees memory; the TransferHeapScope root releases retained cycle values.)
  - There is no new allocation at startup: everything is created on first use, as before, and only the failure mode changes (`bad_alloc` instead of an abort).

**Audit of the thread_locals.** All 298 production objects were scanned for `__cxa_thread_atexit`.

| Site | Destructor | First touched | Under OOM? | Action |
| --- | --- | --- | --- | --- |
| `rt/handles/table.cpp` `t_hook_tables` | yes (vector) | `in_own_hook()`, at VM teardown | **yes (the abort)** | intrusive list, no TLS destructor |
| `rt/atom.cpp:51` `AtomTable::current()::table` | yes | only without an active `AtomTableScope` (not reached in the runs traced) | possible (a later first use) | `owned<AtomTable>` |
| `rt/value_cycle.cpp:22` `cycle_check_stats()::stats` | no (trivial) | first cycle check | harmless | none |
| `rt/value_cycle.cpp:207` cycle-walk `Buffers` | yes (vectors) | the first aggregate-into-aggregate store check, mid-run | **yes** | `owned<Buffers>` |
| `rt/transfer_heap.cpp:45` `root_heap` | yes | the first cyclic transfer without a scope (embedding/sessions) | **yes** | `owned<TransferHeapScope>` |
| `HostTrace.h` `context()` / `child_label()` counters | yes (string, map) | only while host tracing (`NIFT_TRACE_HOST`) is on; inline in 13 objects | yes, under tracing | `owned<…>` |
| `FileSystem.cpp:55` / `:202` parent caches | yes (`fs::path`, strings) | the first file write in a thread (builds, `save`) | **yes** | `owned<…>` |
| `ParserExpression.cpp` lambda caches, ffi tag/error (legacy) | yes | the first legacy lambda / ffi call | yes (legacy otherwise reports `error: std::bad_alloc`, exit 1) | `owned<…>` accessors |
| `rt/heap_account.cpp`, `rt/parser.cpp`, `rt/parallel_task.cpp`, `rt/infer_types`, `rt/shared_gc.cpp`, `rt/aggregate_store.h`, `ParserAsyncPool.cpp`, `ParserExpression.cpp` ffi parser pointer | no (scalars, pointers) | various | harmless | none |

After the fix, `nift` imports no `__cxa_thread_atexit`, and no object references it.

**Tests (permanent).**
- **`tests/runtime2/tls_destructors.py`** (`make test-runtime2-tls-destructors`; a prerequisite of `test-rt-memory-c1` and in `make test`). A deterministic check that the binary imports no `__cxa_thread_atexit` and that no object references it; a failure names the object and its TLS symbols.
- **`tests/runtime2/tls_mutants.sh`.** `NIFT_TLS_MUTANT=1` restores the CP17 vector, and the check rejects it, naming `table.o`. The control's object and binary are byte-identical to production. This static check is the deterministic regression test the nondeterministic abort needed.
- **`tests/rt/thread_owned_unit.cpp`**, built with ASan/UBSan and LeakSanitizer. It checks:
  - one instance per thread;
  - destruction newest first;
  - a later object used from an earlier one's destructor;
  - an object created during destruction is destroyed too;
  - no leak after worker threads exit.

  It also passes under valgrind (no definite leaks, no errors).
- **`tests/runtime2/memory_c1.sh`** now treats `Fatal glibc error` as a failure, prints a failing cell's whole stderr, and copies it to `$MEMORY_C1_KEEP` when that is set.

**Results (local, GCC 13.3, glibc 2.39).**
- **test-rt-memory-c1 20 x 24 cells** on the frozen binary: 480 of 480 pass, every one exit 70, no SIGABRT and no `terminate called`.
- **The memory error is still reported** in every cell: `memory budget exceeded` in 5 of 24 cells each run, and `out of memory (std::bad_alloc)` where the address space runs out first (19).
- **gdb (`break __cxa_thread_atexit`):** no TLS destructor is registered in a c1 cell (600000 and 2097152 KiB) or in a normal run.
- **valgrind** (`--leak-check=full`, definite leaks as errors) is clean on a VM script, the same script on legacy, and the threaded VM fixtures `b3/equiv/thread.f` and `thread_methods.f`.
- **Handle units** (table, lifecycle including reentrant close inside a hook, close under allocation failure, stress) all pass.
- The **final Linode confirmation** (24 cells x at least 20 on GCC 15.2) is part of the targeted follow-up.

**Also in C57 (lint).** C56 had taken `src/rt/natives/host_file_handles.cpp` past rt-lint's
400-line one-concern limit, with one line over 120 columns. Both it and `table.cpp` are compacted
without changing behaviour (the file-mode table and mutants pass), and rt-lint passes.

**Gates on C57.** These pass:
- the certification guard and rt-lint;
- test-runtime2-tls-destructors (with its mutant and the sanitized unit);
- test-runtime2-file-mode-parity (168 x 2, with 4 of 4 mutants rejected);
- test-runtime2-read-limit-mutants (8 of 8), with read_limit_controls (93);
- the cp17 file/stream handles, script handle lifecycle and closed-handle catchability tests;
- vm_host (160 pass, 0 fail, 5 refused) and vm_parity.

test-runtime2-host-deps passes every line except read_limit.py's same 24 refusals (section 43). In
test-rt-vm every prerequisite passes, and its own recipe stops only at vm_modules
(`import-scope:collision`, failing before this change too). test-rt-handles fails only on the
`no_handle_strings` problem in `render.cpp`, which also predates this change.

**Production sha256.** `fbdb05fbda30fba17ba0817f6d171dac3a26937055fa4e7681354096d6e9b42a`.

## 49. Review of C56-C58 (ACCEPT-WITH-FINDINGS, two Low): wording corrected (C59)

The review confirmed the root cause and the fix and found no corruption. Legacy output is
unchanged across the 2,528-program corpus. It also agreed with converting every thread_local
rather than fixing `table.cpp` alone. Two Lows:

- **L-1.** Section 48 and `src/rt/thread_owned.h` said none of the main thread's `owned<T>` objects does more than free memory. That is wrong for the `TransferHeapScope` root (`transfer_heap.cpp`):
  - Its destructor releases the retained cycle values (`cut`). For counted handles among them, that runs handle-table cleanup.
  - On the main thread it no longer runs at `exit()`.
  - **In the CLI this has no effect:** every VM's handle tables shut down at VM teardown (`Vm::~Vm`), before the process exits, so no handle is left for that cleanup to act on.
  - **Embedding case:** an embedding that keeps transferred cyclic values holding handles alive on its main thread past its VMs no longer gets that release at exit. The OS reclaims the process's resources, but no handle cleanup hook runs for them.
  - Both texts are corrected; no code changes.
- **L-2.** If the TSD key cannot be created (`pthread_key_create` fails), every worker thread's `owned<T>` objects leak until the process exits: soft (no abort), but unbounded in the number of threads.

## 50. B18: test-test-integrity clean (C60, tests and scripts only)

`scripts/test_integrity_check.py` reported 57 findings on the candidate, and it has no allowlist.
All 57 are now resolved by fixes, so none is recorded as an exception. The gate reports
`test-integrity clean: 680 files scanned, 0 findings`.

**The 14 the coordinator named** (new in the candidate or since B4/B4b):
- `swallowed-returncode` in `b4b/result_identity.py`: the unchecked `rm -rf` cleanup run becomes `shutil.rmtree`.
- `pipefail-missing` in `b3/mutants.sh` (4), `b4/mutants.sh` (6) and `b4b/mutants.sh` (3): `set -u -o pipefail`.
  - In b4b, the checker could not follow the inline `|| { echo ...; exit 2; }` groups inside `signals()`; they become `|| die "..."` (the same echo and exit 2).

**Pre-existing findings, all fixed:**
- **`pipefail-missing`** (pipefail now on). In `check-build-reuse.sh`, `config-id.sh` and `check-config.sh`, the checker lost track of state through brace groups after `||`, a one-line function holding a loop, and a sourced file. Those constructs were restructured: `if ...; then ...; fi`, a multi-line `side()`, and `enable set; builtin set -o pipefail` after the source. Files:
  - `collections`, `refsem` and `safety` `mutants.sh`;
  - `scripts/build/check-build-reuse.sh`, `check-config.sh`, `check-production-deps.sh`, `check-symbol-refs.sh` and `config-id.sh`. `config-id.sh` is sourced by `compile-object.sh`, `write-signature.sh` and `check-config.sh`; none of their pipelines' statuses decides anything.
  - `tests/abi/external_consumer.sh`, `tests/rt/refrep_production.sh` and `tests/rt/sort_forgery/run.sh`.
- **SIGPIPE.** Under pipefail, `producer | grep -q` can turn a found match into a failure: grep exits at the first match and the producer dies of SIGPIPE. Each one whose status decides a result now reads its input from a here-string (`grep -q PAT <<< "$(producer)"`). Sites:
  - `check-build-reuse.sh`, the dry-run check (`! make -n | grep -q`, which could otherwise have passed falsely);
  - `external_consumer.sh` (readelf, nm);
  - `refrep_production.sh` (nm x3, printf x3);
  - `sort_forgery/run.sh` (nm);
  - `check-symbol-refs.sh` (printf).
- **`swallowed-returncode`:**
  - `cp25/inventory.py`: rt-diff exiting with anything other than 0/1 (differences) is a harness failure. **This could mask a failure before**: a crashed rt-diff was read as an inventory.
  - `scripts/build/check-production-tu.py`: a failing `-E -v` probe exits, where an empty search list would make every system header look local. **This could mask a failure.** The `command -v` lookup (a cache key only) is explicit about its fallback.
  - `cp23/default_controls.py`: a false positive. The wrapper source's `\nq=` was read as a variable `nq`; the string is split, with no behaviour change.

**Gates re-run after the change.** The binary relinked identically (`check-config.sh`,
`check-symbol-refs.sh` and `config-id.sh` run in every `nift` link).

| Gate | Result |
| --- | --- |
| test-runtime2-b3/b4/b4b/collections/refsem/safety-mutants | all PASS: no failure newly revealed |
| test-rt-refrep-production, test-c-abi-symbols (external_consumer), test-rt-sort-forgery | PASS |
| test-runtime2-guardoff | FAIL at `guardoff.py` on `cp22/vm-only/v_preempt.f` before reaching `result_identity.py`. That case is timing-dependent: whether an async task is seen done within 5,000 loop iterations; `nift` printed `true` and then `false` on repeated runs. It is unrelated to B18 and recorded as flaky. `result_identity.py` run directly: PASS (61 probes) |
| cp25r-inventory | `collect` (the changed step) passes; `report --check` fails on unclassified reasons (`method:open/close/save/write`), as before (EXPECTED_BASELINE) |
| test-test-integrity, test-rt-lint, certification guard | PASS |
| **Not run here** | test-build-reuse (destructive: clean and distclean of the tree); test-production-deps (needs the sanitizer and TSan libffi builds; disk); `cp23/default_controls.py` (wired to no target; needs the R2 binary) |

## 51. B09: stale unit-test source lists (C61, Makefile and tests only)

**Cause.** The built-in unit tests compiled a hand-kept list: `RT_BUILTINS_SOURCES` (the natives)
plus `RT_BUILTINS_CORE`. After B3-B5 the natives need `copy_graph.cpp` (`shallow_copy`,
`deep_copy`), `shared_state.cpp`, `shared_channel.cpp` (`shared_make_*`, `shared_channel_close`,
`shared_handle_type_name`) and `op_admission.cpp` (`note_dispatch_defect`), so the five gates
failed to link.

**Fix: the lists are derived, not kept.**
- **Unit, differential and case-conversion binaries** (`builtins-unit`, `builtins-diff`, `case-conversion-unit`, `case-conversion-bench`) link the production runtime objects `RT_UNIT_OBJECTS`. That list is derived from the source tree, as `value-unit`'s is, and checked by `check-config.sh`.
  - `test-rt-case-conversion-negative` links the same objects with `string_methods.o` replaced by the candidate or the mutant, each compiled from source the way production compiles it.
  - `test-rt-builtins-sanitize` links the sanitized production object set `RT_OBJECTS_san`, as `test-rt-value-sanitize` does.
- **Isolated binaries** (`builtins-bench`, `host-random-budget`). Their contract: the natives link without the legacy value bridge. They now link the closure computed by the new `scripts/build/link_closure.py` over the production objects:
  - seeds: the isolated natives' objects plus the stubs;
  - candidates: `RT_UNIT_OBJECTS`;
  - excluded: `RuntimeValue.o`, `legacy_bridge.o`, `render.o`.

  A native that needs the bridge, directly or through another object, fails the closure and the message names the dependency chain. A stale list can no longer be the failure.

**Results.**
- **test-rt-case-conversion:** PASS (1679 checks).
- **test-rt-case-conversion-negative:** PASS (candidate 0 failures, mutant 120).
- **builtins-unit links, but 4 checks fail.** These are genuine findings: expectations made stale by the product, not by the test build.
  - `builtin_count() == 120`: the table has 186 entries since CP17 W13, which added the file/stream handle built-ins after the Host built-ins.
  - `copy(struct)`, `copy([1, {"p": struct}])` and `deepcopy({"h": file handle})` expect `Status::Internal`, the review-D2 rule that "a struct or handle anywhere is a compiler defect". Since B3/B5 `copy` of a struct succeeds, and the CLI matches legacy (`struct(P){x:=1}; q:=copy(p); q.x=2` prints `1`, `2` on both).
  - **Not changed here:** they need a decision on the intended contract (update the expectations, or restore the rule).
- **builtins-diff does not compile.** `static_assert(HostSecureRandomBytes + 1 == Count)` ("the Host built-ins must stay last") fails: 120 against 186. This is the same CP17 W13 ordering finding; the differential's pure/Host split needs updating. Not changed here.
- **test-rt-builtins-sanitize:** the ASan/UBSan unit links and runs, with the same 4 stale checks and no sanitizer report. The differential stops on the same `static_assert`. The sanitized objects (631 MB) were deleted afterwards for disk.
- **test-rt-host-random-budget and test-rt-builtins-isolation fail: the isolation contract is broken by production code.** `link_closure.py` shows the chain:
  - `natives/shared_methods.o` and `natives/format_methods.o` need `shared_state.o`;
  - `shared_state.o` reaches `isolate_transfer_decode.o`, then `parallel_task.o`, `isolate_task.o`, `vm_parallel_cleanup.o`, `vm_coro.o`, ... `vm.o`, `vm_session_render.o` and `eval_result.o`;
  - those need `legacy_bridge.o` (`to_legacy`) and `render.o` (`append_rendered`, `callable_marker`).

  Restoring the contract needs a production change: splitting `shared_state` from the transfer and parallel code, or stubbing the shared natives in the isolated build. Per the instructions, **stopped and reported, not changed.**
- **test-rt-builtins** depends on the two isolated gates, so it fails with them. Its own lines beyond the unit and the differential: rt-lint of `src/rt/natives` passes. `context_probe.py` fails against its expectation file; that probe of legacy's context-dependent behaviour is independent of B09 and was not examined further here.

## 52. B07: test-rt-vm-mutants unmutated parity (C62, partial; tools and tests only)

**Reproduced** with `vm_parity.py --gen 100 --seed 3 --rt-diff --rt-diff-gen 50` on `nift`, as the
mutation gate's `[0]` step runs it: rt-diff reports 4 differing entries and 48 unused policies, and
the gate fails.
- `gen/g00013`, `g00021`, `g00047`: the B5 shared-reference difference (a callee's `push(99)` visible to the caller: legacy `[1,2]`, VM `[1,2,99]`). The 62 B5 rules in `b5-refsem-vm.json` are exact per-source rules from gen.py **seed 1**. They do not cover seed 3, and 48 of them approve nothing there.
- `gen/g00046`: the `cp11-recursion-budget-class` approves stdout, stderr and exit. The trace channel stayed a difference: legacy stopped at `rec8(70)` before `thread(tw9, 0)`, and the VM's new context `script/thread#1` was not covered. The rule required legacy's whole trace to be a prefix of the VM's in **global** emission order, and that order is not deterministic with the program's concurrent async tasks.

**Done: D-001#4 extended to the trace after legacy's failure point.** `trace_continues`
(`tools/rt-diff/rt-diff`) keeps the global-prefix rule, and also accepts the extension **per
context**:
- every context of legacy's trace is, event for event, the start of the same context of the VM's;
- a context only the VM has must be a task (`<owner>/async#N`, `<owner>/thread#N`) whose first event the VM emitted after its owner had emitted every event legacy's owner emitted, that is, after legacy's failure point (applied recursively for a task owned by a new task).

`g00046` is now approved. `tests/runtime2/rt_diff_trace_policy_selftest.py` (13 cases, 9 rejected)
adds the negative controls; the approved difference must stay exactly this one:
- a new task context started **before** legacy's failure point is rejected;
- a modified event in one of legacy's task contexts is rejected;
- a legacy task context that is shorter in the VM is rejected;
- tasks interleaving with each legacy context a prefix, and a nested new task after the failure point, are approved.

**Stopped: the B5 class rule needs a production change.**
- A class rule for the B5 shared-reference difference, "confirmed by VM statistics as cp11-recursion-budget-class is", needs a statistic that attributes the output difference to a mutation made visible through a shared reference. The recursion class has `beyond_legacy_depth`; the VM statistics have nothing for sharing (fields: outcome, executions, heap, dispatch defects, ...).
- Any text-only class rule ("legacy and VM differ in an array's printed contents") would approve unrelated differences, which is the weakening the task forbids.
- Options for the owner:
  - **(a)** a production statistic, for example a count of stores into an aggregate reachable from more than one binding, with the class rule requiring it to be non-zero and the rest of the output equal;
  - **(b)** a test-side oracle that reruns the program on the VM with every call argument wrapped in `deepcopy(...)` and requires that run to equal legacy exactly. No production change, but a source transform to maintain;
  - **(c)** exact rules for the gate's seed 3 too, or running the gate's parity step with seed 1. This keeps the per-source approach the task wanted replaced.
- Until then the gate's `[0] parity` still fails: 3 differing entries (g00013, g00021, g00047) and 48 unused B5 policies. The 12-mutant re-run was not done, since the unmutated step does not pass yet.

## 53. B16: cp19 admission mutants, recommendation for the owner (C63, analysis only)

Pending the owner's decision on b5-refsem.md 11.4. **No test is changed.**

`make test-cp19-admission-mutants` fails 2 of 15 cases, and `-switch` fails the same way:
- **loop m3:** the control and m3 are both refused (exit 1);
- **native m10:** m10 survives.

Its m12 and m13 rows pass: m12 is a tripwire, and m13 is caught by `shared-scalar`.
`test-cp19-admission-sanitize` runs `loop admitted`, so its CHECK failures at
`admission_mutants.cpp:108`, `:109` and `:128` are the loop control's refusal (the not-admitted,
no-output and wrong-value checks), not a sanitizer finding. There is no ASan/UBSan report.

**None of m3, m10 or m12 is a preprocessor no-op.** Each mutant's translation unit, preprocessed
with and without its `-DNIFT_SESSION_ADMISSION_MUTANT=n`, differs by its hook lines (2 lines each).
So none can be retired as obsolete the way A29 and cp14b mutant 11 were.

| Mutant | Recommendation |
| --- | --- |
| **m3** (`cell.cpp`: `cell_plain_capture` treats a source-slot capture as not plain) | **Keep, pending D4 (release blocker).** It has two effects: (1) `session_callable.cpp:67`, where a session function upvalue that is a loop source-slot capture goes through `may_call` (more conservative: incomplete callable shape); (2) `session_activation.cpp:32`, where functions reached only through such a capture are not relinked to their recompiled code after a session redefinition (stale code). Both need a session function, reached through a captured loop variable, to be **called** in a later statement, and that is exactly what D4 (`session:current-callable-proof`, the U6 exposure of B5 1) refuses. No admitted shape reaches it today: 11.4's 2,144-session search found none, and the probes here agree. Recommend **approving the loop control's refusal as the B5 D4 refusal**: pin it exactly (reason `session:current-callable-proof`, no effect, no output) as a refusal witness, and mark m3 "unexercised while D4 stands". (F-3, corrected in section 59: "unexercised" does **not** mean harmless. Effect (2) is stale code after a session redefinition, a wrong-result class masked only because D4 refuses the route; it is a release-audit entry, and the witness is a two-sided sentinel that fails when the control is admitted again.) Restore the admitted witness when D4 is resolved, since the original program becomes admitted again. No replacement program is proposed, because any shape that reaches the source-slot path also calls through the D4-refused route. |
| **m10** (`compiler_session_native.cpp`: the session native dispatch tag forced to the string family) | **Masked by B4's dispatch precondition (defence in depth); reclassify as a tripwire with a double-mutant witness.** The session branch emits a flagged `CallNative`. At run time `Vm::builtin` checks the receiver family and `redispatch` selects the sibling native of the actual family (`vm_builtin.cpp`), so the forced tag only changes which branch is entered first; the result is identical. Built here: B4 mutant **A4** alone (no redispatch) still passes `native admitted` (the control behaviour), while **m10 + A4** fails `native admitted` and passes `native internal` (`unexpected operands (compiler defect): built-in contains`). So the hook is live and is masked only by redispatch. Recommended witness: keep `native` as a tripwire row for m10 (m10 must behave as the control), and add the double-mutant row m10+A4, which must show the internal failure. If B4's redispatch is ever removed, m10 becomes observable again and the tripwire trips. |
| **m12** (`session_callable.cpp`: `may_call` always true) | **No change.** Already classified in 13.1, owner-accepted: observationally equivalent on the tested reachable paths (production's `may_call` is already true at every arrival). It is a tripwire row plus the reachable-path probe `test-cp19-m12-reachable`. |
| **m13** (`infer_session_location.cpp`: capture-location inference skipped) | **No change.** Caught by `shared-scalar` (b5-refsem.md 12.1), with `shared` as a tripwire. PASS here. |

**The loop control, specifically.**
- **Program:** `fn(g()){return 1};fn(makec()){caparr:=[[()=>g()]];...};c:=makec();warm:=c(0)`, then `fn(g()){return 3};last:=c(1)`.
- **Refused at statement 2:** `session:current-callable-proof` ("the VM gave no exact position"), before any effect. Legacy prints `3`, `3`.
- **Cause:** the D4 exposure of B5 1 (**141b847e**, "U6, session persistence without stale assumptions"). `infer_session.cpp` marks both session-heap site classes `exposed_` ("earlier code reaches every earlier value"), so a fact-proved historical call opens every exposed class.
- **Status:** one of the release-blocker shapes of b5-refsem.md 11.1.
- **Recommendation:** as for m3, approve the refusal as a pinned witness until D4 is resolved; do not look for an admitted replacement while the U6 exposure stands.

## 54. Owner decision 2026-11-10: `file()` after `close()` keeps legacy behaviour (C64, production)

**Decision.** `close()` closes the active buffer of a `file()` value, not the value itself.
D-039's "every later operation raises runtime.handle_closed" does not apply to `file()` values;
it stands for every other handle kind. The amendment is in `decisions.md` under D-039.

**Legacy's behaviour, taken from legacy** (`NIFT_VM=0`; checked cell by cell by the table below):
- After `close()`, `path()` and `exists()` work, and `open(mode)` reopens the value. On reopen the buffer is re-read: mode r/rw/a reads the file, w starts empty and modified, a starts at the end. The modified flag and the cursor are reset as by a first `open`.
- Every other method reports the ordinary `<method>: file is not open` error: fatal, not catchable, at legacy's position. This is the same text as for a never-opened value; legacy does not distinguish the two, and neither does the VM.
- A second `close()` is `close: file is not open`, not idempotent.
- The free `close(f)` on a `file()` value is `close: expected stream`, never-opened or open.

**Change (bounded to `file()`).**
- `file_close` (`src/rt/natives/host_file_handles.cpp`) clears the buffer state as before, but **no longer closes the handle-table slot**. The value stays live until it is dropped, as before, and cleanup still reports a buffer left open.
- `handle_close` (`host_file_ops.cpp`, the free `close(x)`) raises legacy's `close: expected stream` for a `file()` value.
- Streams, processes, timers and channels are unchanged: D-039 still closes their handles.

**Tests.**
- `tests/runtime2/file_mode_parity.py`:
  - no approved D-039 cells remain;
  - two reopen states (`reopened-r`: written, saved, closed, reopened r; `reopened-rw`) and the free `close(f)` are added;
  - result: 8 states x 29 calls = 232 cells x 2 programs, every one equal to legacy except copy/move, which stay refused (B27).
- **Mutant 5** (`NIFT_FILE_MODE_MUTANT=5`, D-039 for `file()` restored) is rejected by 164 checks. All 5 file-mode mutants are rejected, and the control is byte-identical to `nift`.
- **test-v410-file-buffer-parity:** the 5 rows `open-rw`, `open-w`, `open-a`, `new-empty-write` and `reopen-external-edit` now match the oracle exactly.
- **cp17 tests that pinned D-039 on `file()`** are moved to stream handles, for which D-039 still stands. The `file()` variants are kept as legacy-equal checks.
  - `script_handle_lifecycle.py`: `alias_close` and `stale_after_close` are now legacy-equal errors; new `stream_alias_close` and `stream_stale_after_close` cover D-039.
  - `closed_handle_catchable.py`: decision D on an `ifstream`; all 6 cases pass.
  - `resource_contracts.py`: `closed_read_code` is the legacy error, new `closed_reopen` reopens, new `stream_closed_read_code` is `runtime.handle_closed`.
- **Unchanged and passing:** `file_stream_handles.py`, `handle_flow_regressions.py`, `unwind_handle_release.py` (168 cases) and `vm_host.py` (160).

**Production sha256.** `c88bf749d92215615e47fc487a8a7331f59ae3ce2fcb1198e49ddaf78e52a3f7`.

## 55. Owner decision 2026-11-10: the loop write diagnostic is an approved difference (C65, tests and docs)

**The difference.** When a `file()` `write` or `write_line` fails a precondition (not open, or
open read-only) inside a `while`/`for` body, legacy reports `script failed`: its prepared loop
executor drops the method's message (`CLI.cpp`). The VM reports the method's own error
(`write: file is not open`, `write_line: file is not open for writing`, ...) at the same position.
It is recorded in `docs/runtime-2/compatibility.md` under the approved differences. The approval
covers **only that message text**; it is not general permission to change error handling.

**Pinned against legacy** (`tests/runtime2/file_loop_diagnostic.py`, in
`test-runtime2-file-mode-parity`). There are 2 states (closed, read-only), 2 loops (while, for)
and 2 methods, giving 8 programs and 56 checks. For each program:
- the same exit status (1);
- the same stdout, everything printed before the error;
- the same file bytes;
- nothing extra printed;
- legacy's text exactly `script failed` at the position the VM reports;
- the VM's message exactly `<method>: file is not open[ for writing]`, with no VM tag, compiler diagnostic or handle text;
- the same program with the file open for writing succeeds identically on both engines.

**test-v410-file-buffer-parity.** The two rows `closed-write-factory-count` and
`readonly-write-factory-count` accept the VM's pinned stderr, and only when the oracle's stderr is
legacy's `script failed`. Every other channel still equals the oracle. With `NIFT_VM=0` the oracle
is unchanged: 102 of 102 pass.

**Result on the VM.** Of the 102 rows, 22 pass and 80 fail. All 80 are VM refusals (`not compiled
for the VM`): `opaque:nesting-limit` 39, `host-handle-flow` 26, `opaque:legacy-runtime-error` 8,
`builtin-arg-fault-context` 2, `builtin-receiver:path`, `builtin-receiver:write`,
`builtin-arity:open`, `builtin-arity:save` and `builtin-lazy-args:replace`. No semantic
difference remains. The gate itself still stops at its first refused row.

## 56. B09 repaired test-side: parallel stubs, builtin count, copy contract (C67, tests and docs)

Follows section 51 and the C59–C66 review (ACCEPT-WITH-FINDINGS, classification (b)+(c)). No
production change; `nift` stays c88bf749….

**Architectural note (recorded, no production effect).** The isolated natives `shared_methods.o`
and `format_methods.o` need `shared_state.o`, `shared_cancel.o` and the transfer code. These need
three small functions of `src/rt/parallel_task.cpp`: `ParallelBlock` (constructor and destructor),
`parallel_task_identity` and `notify_parallel_wait_states`. The same translation unit holds the
isolate task runner, which needs the whole VM and, through it, `legacy_bridge` and `render`. The
runtime dependency is intended: a shared wait releases a parallel worker slot, and a transferred
Future carries its task's identity. Only the object-file granularity is accidental. It dates from
CP23 (`1dd496d6`, `e002dbc5`, `a5179308`, `ac848f77`, all before r2 `0b5079fc`), and the hand-kept
link lists hid it until C61. There is no production consequence: the CLI links every object, and no
native reaches `to_legacy` or `render` through these functions. A later production hygiene option,
not done here: move the three functions into a leaf `parallel_task_core.cpp`.

**Repair: four test-only stubs** (`tests/rt/parallel_isolation_stubs.cpp`, linked into
`builtins-bench` and `host-random-budget` through `RT_ISOLATION_CLOSURE`).
- The `ParallelBlock` constructor and destructor behave as production does when no parallel
  runtime runs on the thread (`running_pool == nullptr`): admitted, and the destructor does
  nothing. An isolated binary has no VM, so it never starts a runtime.
- `parallel_task_identity` and `notify_parallel_wait_states` return 0 and do nothing for a null
  record, as production does, and **trap** on a non-null record. Such a record exists only when the
  VM creates one, so the stubs are unreachable in an isolated binary.
- `RT_ISOLATION_FORBIDDEN` now also excludes `parallel_task.o` and `vm.o`. The stubs are therefore
  required, and any new path from a native to the VM or the bridge fails `link_closure.py` and
  names the chain.

**F-2: drift guard** (`make test-rt-parallel-isolation-stubs`, part of `test` and
`test-rt-builtins`; `tests/rt/parallel_isolation_stubs_check.py`).
- **Signature:** every symbol the stubs object defines must be defined with the identical mangled
  name by `parallel_task.o`, and the stubs must define exactly the four functions.
- **Behaviour:** a sha256 of the production definitions (plus the `running_pool` declaration that
  their no-runtime path depends on) is pinned. A change fails until the stubs are reviewed and the
  fingerprint is re-pinned in the same commit.
- **Parity probe:** `tests/rt/parallel_isolation_probe.cpp` runs every `ParallelWaitKind`, nested
  blocks, the default kind, a null identity and a null notify, on the main thread and on a new
  thread. It is linked once with the real objects and once with the stubs, and the two outputs (18
  lines) must be identical.
- **Selftest:** six planted drifts are each rejected: a signature change, a missing stub, a
  destructor that gains behaviour, a changed null-record result, a differing probe line and a
  crashing probe. The unmodified control is accepted.

**Stale unit expectations, updated.**
- `builtins-unit`: `builtin_count() == 186`. The count is the real inventory: 89 CP14/CP15, 31 CP16
  Host, 31 CP17 W13 file/stream handle, 19 CP23 shared-concurrency and 16 D-063 collection
  built-ins. `builtin.cpp` statically asserts that its dispatch and spec tables have `Count`
  entries.
- `builtins-unit`: copy/deepcopy of structs and handles now follow **D-NEXT-refsem #3**
  (decisions.md, owner 2026-11-01; Nift v4.2's contract), which superseded CP15 Review D2's
  "a struct or handle is a compiler defect" (`Status::Internal`):
  - `copy(struct)` is a new outer instance;
  - `copy([1, {"p": struct}])` shares the nested instance;
  - `deepcopy([p, p])` copies `p` once and keeps the two elements shared;
  - `deepcopy({"h": file})` keeps the handle's identity.
- `builtins-unit` also crashed (stack overflow in the test's JSON printer) once those checks passed.
  `aliasing_and_cow` did `array_push(a, a)`. Since B2 (`e30bd6c9`) arrays are shared references,
  so this made a cyclic array through the raw test builder, which does not check for cycles (the VM
  refuses such a store, D-NEXT-refsem #4). The test now pushes an explicit outer copy, which keeps
  the CP14 intent of an argument aliased inside the receiver.
- `builtins-diff`: the "Host built-ins last" `static_assert` is replaced by a real split. Only the
  pure CP14/CP15 block (`[0, HostOpen)`) runs against legacy. Each later section is excluded, with
  its reason and its own suite:
  - CP16 Host: process environment; `vm_host.py` and CP16 engine parity;
  - CP17 W13 handles: VM-only typed natives; cp17 and file-mode parity;
  - CP23 shared: no legacy counterpart; the cp23 suites;
  - D-063 collections: VM-only reference values; the collections suite.

  Five `static_assert`s pin the section boundaries, so a built-in added outside an existing section
  (in particular one appended at the end) does not compile until it is classified. The argument
  generator aborts on a receiver of an excluded section instead of the old unhandled `switch`
  cases.

**Results** (in tree, d9adc4f7 plus this commit):
- `test-rt-parallel-isolation-stubs`: PASS (6 symbols, fingerprint, probe identical over 18 lines;
  selftest 6/6 rejected, control accepted).
- `test-rt-host-random-budget`: PASS, 14 checks.
- `test-rt-builtins-isolation`: PASS.
- `builtins-unit`: PASS, 1380 checks.
- `builtins-diff` against legacy (seed 20261018, 5000 cases): PASS, 4980 checks.
- **`test-rt-builtins-guards` (callgrind, builtins-bench): FAIL, 6 of 35 workloads. Not caused by
  the stubs; reported, not re-pinned.**
  - `obj.merge` costs 22,848 to 18,314,388 Ir/call at n = 100 to 100,000, against guards of 3,055
    to 1,717,389 (+10 %). Its scaling exponent is 0.97 against 0.77.
  - `str.replace` at n = 100 costs 3,356 Ir/call against 2,945.
  - A callgrind profile of `obj.merge` at n = 1000 attributes the cost to `Construction::object_set`
    (35 %), `heap_retally` (23 %), `shallow_object` (12 %), `find_position`, `rebuild_index` and
    vector growth. No parallel or stub symbol appears.
  - The guards were locked at CP15 (2026-10-20). The cost comes from code after that: B2 (`e30bd6c9`,
    arrays and objects as shared references) and tooling 2 (`fa10fc9b`, construction builders).
    The isolated bench did not link before this commit (section 51), so the guards had not run on
    this code. Whether to accept the new costs (re-pin) or treat them as a regression is an owner
    decision.

**Release audit (recorded):** the isolation stubs stand in for production code in two test
binaries; the drift guard above fails if they diverge. The CP23 co-location in `parallel_task.cpp`
has no production effect. The `obj.merge` and `str.replace` instruction counts exceed their CP15
guards (above).

## 57. F-1: context_probe probes legacy (C68, tests only)

`tests/runtime2/builtins/context_probe.py` removed `NIFT_VM` from the environment and so ran the
binary's default engine, which has been the VM since Runtime 2.0. Its "legacy context-dependence
inventory" (D-015) was recording VM refusals and found 7 context-dependent cases against the 19
expected. It now sets `NIFT_VM=0`, since legacy is the oracle by design (`RT_BUILTINS_LEGACY`).
`make test-rt-builtins-probe`: PASS (inventory pinned), 272 cases × 6 contexts, 10
context-dependent; 11 pairs, 9 shape-dependent. With this and section 56, `make test-rt-builtins`
passes as a whole.

## 58. F-4: the C66 slot-reuse check asserts the reuse (C69, tests only)

`tests/rt/host_observer_native.cpp` (C66) checked that a closed `ifstream` handle stays closed after
a new stream is created, but not that the new stream took the old slot. If allocation stopped
reusing the slot, the check would pass vacuously. It now asserts that the new stream has the same
handle id (owner tag and slot index) and a later generation. `host-native-unit`: 37 checks, 0
failures.

## 59. B16: permanent cp19 admission tests for m3 and m10 (C70, tests and docs)

Owner rulings 2026-11-10, following section 53 and the C59–C66 review (F-3).
`make test-cp19-admission-mutants` and `-switch` now pass all 15 cases (they failed 2).

**m3: the loop control is a two-sided D4 sentinel.**
- `admission_mutants.cpp` has a new mode, `loop d4-refused`. After the seed, it pins:
  - the probe is refused with exactly `session:current-callable-proof`;
  - 0 VM and 0 legacy executions;
  - no output, no new binding and an unchanged binding count;
  - `g()` is still the earlier definition (1).
- `admission_mutants.py` runs four rows:
  - control `d4-refused` must pass;
  - control `admitted` must fail;
  - m3 `d4-refused` must pass;
  - m3 `admitted` must fail.

  So m3 must behave as the control while D4 stands. **The row fails as soon as the control is
  admitted again**, with the message "D4 RESOLVED? ... restore the original admitted witness and
  m3's discrimination". Resolving D4 cannot pass silently.
- `test-cp19-admission-sanitize` runs `loop` in `d4-refused` mode, so the CHECK failures at
  `admission_mutants.cpp:108`, `:109` and `:128` (the refused control run as `admitted`) are gone.
- **Release audit (unresolved, NOT harmless):** an admission-precision issue tied to D4, the B5 1
  U6 exposure (141b847e). m3's effect (1) makes a session function upvalue that is a loop
  source-slot capture go through `may_call`, which is more conservative. Its effect (2) means
  functions reached only through such a capture are not relinked after a session redefinition:
  **stale code, a wrong-result class**, masked today only because D4 refuses every route that
  reaches it. When D4 is resolved, m3's discrimination and the original admitted witness must
  come back; the sentinel forces that. Linked to b5-refsem.md 11.1 (the D4 release blockers).

**m10: tripwire plus combined-mutant witness.**
- New binaries (`tests/runtime2/cp19/admission_mutants.mk`):
  - `admission-a4`: `vm_builtin.cpp` built with `-DNIFT_B4_MUTANT=4`, B4's redispatch removed;
  - `admission-m10-a4`: that object plus `admission-m10.o`;
  - `-switch` variants of both, linked with the switch VM.

  `test-cp19-admission-mutants` and `-switch` depend on them.
- Rows for `native`:
  - control: `admitted` passes, `internal` fails;
  - m10 alone: the same as the control (tripwire);
  - A4 alone: the same as the control, so detection comes from m10, not from A4;
  - **m10+A4: `internal` passes**, stopping in "unexpected operands (compiler defect): built-in
    contains", and `admitted` fails.

  The row fails if m10 or A4 alone starts to differ from the control, or if m10+A4 can no longer be
  told apart from it.

**m12, m13: no change.** The reviewer's justification, recorded:
- m12 behaves as the control on `selector`. The reachable-path probe (`test-cp19-m12-reachable`)
  shows the `may_call` call site is reached with production's `may_call` already true. It stays an
  informational tripwire.
- m13 is caught by `shared-scalar`: an internal failure where production refuses before any effect.
  `shared` is its tripwire.
- Both rows already fail if the mutant starts to differ from the control, which would force a
  reclassification.

## 60. B07 option 2: the rt-diff sharing oracle (C71, tests and tools only)

Owner 2026-11-10 06:15: option 2 is approved. Seeds are not aligned, so the mutation gate keeps its
independent seed 3, and no new per-program approvals are added. This follows section 52. No
production change; `nift` stays c88bf749….

**The oracle** (`tools/rt-diff/sharing_oracle.py`; `rt-diff --sharing-oracle RT_PARSE_DUMP`,
used by `vm_parity.py` in both tiers). For a script entry whose differences no rule approves:
- **Rewrite T(P):** every argument of every call to a function the program declares is wrapped
  in `deepcopy(...)` at the call site. It is built on the CP10 parse dump (byte offsets), and
  nothing else changes.
- **deepcopy, not copy.** copy is shallow (D-NEXT-refsem #3): an array or object nested in an
  argument would stay shared, and a callee's mutation through a nested element would remain
  visible. deepcopy gives the callee what legacy's by-value passing gives it. The two are never
  used interchangeably, and the self-test asserts that T contains no `copy(`.
- **It approves only if all five checks hold:**
  1. Legacy(T(P)) == Legacy(P) on every channel.
  2. VM(T(P)) == Legacy(P) on every channel.
  3. VM(P) != VM(T(P)).
  4. No refusal, timeout, crash or abort, exit 70 or internal tag, and each VM run is exactly one
     VM execution with no legacy execution.
  5. Exit and stderr are justified separately: every differing channel must move back under T
     (implied by 2 and 3, asserted anyway), and an exit or stderr difference **alone** (stdout
     equal) is never approved.

  Only the script file itself is left out of the T comparisons, since it differs by construction.
- **Fail closed:** the entry is reported `SHARING-ORACLE UNCLASSIFIED` and stays a difference
  that blocks the gate when:
  - any check fails;
  - rt-parse-dump cannot parse P, or P has an unknown node kind;
  - P declares structs (struct `==` is identity);
  - P calls `same()` or a handle or reference built-in (file, ifstream, ofstream, close, mutex,
    channel, send, recv, cancel, atomic, map, set, stack);
  - a closure captures a parameter of an enclosing function;
  - a declared function is rebound;
  - P has no rewritable call site;
  - any run reports a reference cycle.
- **The limit, recorded here and in the tool's header:** the oracle establishes **attribution**
  (the difference comes from argument sharing), **not correctness**. A VM bug confined to the
  shared-mutation path would be approved. B5's semantic suites (b5, refsem, collections; written
  expectations) remain the correctness oracle and stay in the gate.
- `vm_parity.py` additionally requires each oracle approval to carry its passed checks and to come
  from one VM execution of P with no legacy execution.

**Old rules.**
- The 48 seed-1 exact rules (24 generated programs) were checked against the oracle.
  - **The oracle approves 10 of the 24 programs:** g00024, g00030, g00034, g00073, g00114,
    g00118, g00131, g00139, g00170 and g00181. Their 20 rules are retired, moved to
    `tools/rt-diff/policies/b5-refsem-vm-retired.json` (not loaded), each with its reason.
  - **The other 14 are unclassified (fail closed):**
    - 11 use `file()`: g00002, g00008, g00060, g00071, g00101, g00112, g00125, g00133, g00159,
      g00169, g00190;
    - 3 have a closure capturing parameter `k`: g00028, g00045, g00189.

    Their 28 owner-approved rules (2026-11-08) stay exact. They are **scoped** with
    `"gen_seed": 1`: they apply only to `--gen --seed 1`, and in any other seed they are out of
    scope, neither used nor unused (`out_of_scope_policies` in the report). The source sha256
    still pins each program.
- The 14 rules for the 6 corpus entries (semantics, smoke, vm-corpus) are unchanged; they run in
  every tier.

**Self-test** (`make test-rt-vm-sharing-oracle` under `test-rt-vm`, and
`test-rt-vm-mutants-sharing-oracle` under `test-rt-vm-mutants`; both edges in the certification
guard): `tests/runtime2/sharing_oracle_selftest.py`, PASS, 26 checks.
- **Semantic probes on the real VM:**
  - approved: direct mutation of an argument, mutation through a nested array element and
    through an array nested in an object, two arguments aliasing one array, a function returning
    its argument, and a sharing difference ending in the same runtime error;
  - not approved: a cycle store (no difference: both engines leave the array unchanged);
  - unclassified: a closure capturing an argument, `same()`, a handle (`file()`), a struct.
- **Negative controls**, using `tests/runtime2/sharing_oracle_fake_vm.py`, a fake VM that plants
  one unrelated bug in both VM(P) and VM(T(P)). Each is rejected by the named check:
  - an unrelated stdout change, with and without a sharing difference: check 2;
  - a wrong error position: check 2;
  - a changed exit status only, with and without sharing: check 2;
  - an unexpected refusal: check 4;
  - an abort (SIGABRT): check 4;
  - a rewrite that changes legacy (a runtime error column on a rewritten line): check 1.
- **Existing rt-diff mutants** (`rt_diff_mutant.py` stdout-only, stderr-only and exit-only over
  the real VM): every program where the mutation shows is unclassified, with no oracle approval.
- **Static checks:** the rewrite is exact (several arguments, nested calls), and it fails closed
  without a call site and on a parse error.

**Results** (in tree, production c88bf749…):
- **The gate's unmutated `[0] parity`** (`vm_parity.py --gen 100 --seed 3 --rt-diff
  --rt-diff-gen 50`): **PASS**. rt-diff found 659 entries, **0 differing, 0 unused policies**;
  the oracle approved 3 (g00013, g00021, g00047) and left 0 unclassified; 28 seed-scoped rules
  were out of scope.
- **The functional tier** (`--gen 300 --seed 1 --rt-diff`): PASS. rt-diff found 817 entries, 0
  differing, 0 unused; the oracle approved 10; b5-refsem exact approvals 20.
- **`make test-rt-vm-mutants`: the unmutated run passes every suite, and 11 of 12 mutants are
  rejected. Mutant 5 ("wrong error column") SURVIVES every suite. This is not caused by B07;
  production change needed, stopped and reported.**
  - At CP12 mutant 5 was rejected by seven suites (cp12-engine-selection.md, line 401).
  - Its hook, `MUTATE_ERROR_OFFSET` in `Vm::report_offset` (`vm_report.cpp:60`), no longer
    reaches any printed position. On the `-DNIFT_VM_MUTANTS` build, `NIFT_VM_MUTANT=5` prints the
    same positions as 0 for VM runtime errors (division by zero at 1:19, index out of range at
    3:7 and 5:7; statistics outcome `vm-error`). Mutant 6 changes its message on the same
    program.
  - The same run without the oracle and with the old B5 rules shows no mutant-5 difference
    either. Earlier the gate stopped at `[0] parity` (section 52), which hid this.
  - Restoring mutant 5 means moving the hook to where VM error positions are rendered now, which
    is production code (compiled only under `-DNIFT_VM_MUTANTS`). Alternatively, retire it as
    obsolete with evidence. This is an owner decision.
- **`make test-runtime2-rt-diff-selftest` fails 7 checks, identically with C70's
  `tools/rt-diff/rt-diff` (HEAD) and with this commit's.** The failing checks: interleave
  channel, combined text-match, trace ops touch/env-all/spawn, D-009 policies and timeout
  reporting. So these failures predate B07. They are not in `prelaunch-known-failures.tsv`, and I
  have not looked into them. `rt_diff_trace_policy_selftest.py` (the same target's second step)
  passes on its own (13 cases, 9 rejected).
- Gates on this commit: the guard, rt-lint and test-integrity pass.

## 61. Review of C67–C71, must-fix 1: VM mutant 5 was compiled out (C72, Makefile and lint)

Section 60 called mutant 5 obsolete, and that was **wrong**. `RT_VM_MUTATED` listed only `vm.o`,
`vm_ops.o`, `compiler_call.o` and `prepared_outer.o`. `vm_report.cpp`, which holds
`Vm::report_offset` and with it mutant 5's `MUTATE_ERROR_OFFSET`, was split out of `vm.cpp` in
94873269 and never added to the list. The mutant binary linked the production `vm_report.o`, so
the hook expanded to nothing.

- **Fix (Makefile only).** `vm_report.o` is added to `RT_VM_MUTANT_OBJECTS` and `RT_VM_MUTATED`.
  Production `nift` is unchanged (c88bf749…). On the mutant build, `NIFT_VM_MUTANT=5` now moves
  the column of a VM runtime error (`a[i]` out of range: 3:7 becomes 3:8).
- **Lint** (`scripts/build/check-mutant-lists.py`, run first in `test-rt-vm-mutants`, with its
  selftest). It covers the families that build an explicit list of mutated objects:
  - `NIFT_VM_MUTANTS` (`RT_VM_MUTATED`; hooks are the macros `vm_mutants.h` defines, read from it,
    plus `vm_mutant() ==` and the guard itself);
  - `NIFT_CORO_MUTANT` (`CP22_CORO_SOURCES`, `CORO_MODULE_SOURCES`);
  - `NIFT_CORO_MODULE_MUTANT`;
  - `NIFT_CLOSURE_MUTANT` and `NIFT_FOR_MUTANT` (their per-object rules).

  A source whose code uses a family's hook, directly or through a header whose code does, must be
  on that family's list. Comments, the defining headers, and a header's `#ifndef X / #define X 0`
  default do not count.
  - Before the fix it reported exactly `src/rt/vm_report.cpp`; after the fix it passes.
  - The selftest plants four negative controls (a hook macro, a `vm_mutant()` test, a CORO macro
    and a FOR macro outside their lists) and rejects each; the control passes.

**C72 result (gate run after the commit).** `make test-rt-vm-mutants`: the mutant-list lint and its
selftest pass; the unmutated binary passes every suite; **12 of 12 mutants are rejected**. Mutant 5
is rejected by int-bounds (505), golden (60 of 108), review (14), d018 (15), parity (24),
adversarial (161), former-replay (152) and records.

## 62. Review of C67–C71, S-1: harnesses that used the bare binary as legacy (C73, tests and Makefile)

Since b61784a2 (2026-10-31) an unset `NIFT_VM` runs the VM, and rt-diff passes a minimal
environment, so the bare binary is no longer legacy. The 7 `test-runtime2-rt-diff-selftest`
failures of section 60 all have this one cause.
- **New `$(TEST_DIR)/nift-legacy`:** a `NIFT_VM=0` wrapper around the same binary (Makefile
  rule `LEGACY_NIFT`).
- **Fixed:**
  - `test-runtime2-rt-diff-selftest`: `NIFT_BIN` is now the wrapper. PASS, 87 checks, 0 FAIL
    (before: 7 FAIL).
  - `test-runtime2-prevalidate-gate`, `rules_test.py --legacy`: the wrapper. 119 cases, 0
    failures (before: 36 failures, legacy reading as the VM).
  - `test-runtime2-prevalidate-gate`, `positive_test.py --nift`: the wrapper. Its "legacy" and
    "prevalidating default" runs date from CP13, when the default engine was legacy. 17 programs
    and 346 operator programs, 0 failures (before: 30 failures). The target never got this far
    before, because `rules_test.py` failed first. The whole target now passes.
  - `tests/runtime2/cp22/run.py`: a run without a VM mode (the legacy goldens, `--record`, the
    trace comparison's legacy side) now sets `NIFT_VM=0`. Its `legacy` mode passes 177 cases and
    `trace` passes 3, both identical with the bare binary and the wrapper.
- **Checked and left as they are** (their use is intended, or observation only):
  - `tests/runtime2/prevalidate/gate.py --nift`: legacy observations only ("never the
    criterion"), and its classify step sets `NIFT_VM=classify` itself, which a wrapper would
    override.
  - `tests/site_dx_smoke.sh` (`env -u NIFT_VM`, deliberately checks the default-mode refusal).
  - `tests/runtime2/memory_c1.sh` (`NIFT_VM=require`).
  - `tests/runtime2/refrep/refrep_cli.py` and `tests/runtime2/cp19/cleanup_cli.py` (they test
    the default session).
  - `cp25/inventory.py` (VM against VM).
  - `test-runtime2-rt-diff` (the same binary on both sides; a determinism check).
  - `builtins_diff.cpp` and `vm_parity.py` (already pin `NIFT_VM=0`).
  - `cp19/widening_error_boundary.py` (sets `NIFT_VM=0` for its legacy row).

## 63. Review of C67–C71, O-1 and Lows: the oracle's control rewrite T′ (C74, tests and tools)

**O-1.** T does more than remove sharing: a wrapped argument is no longer a bare argument or an
argument path, so the VM compiles the call through other code (`compiler_call.cpp`:
`argument_path`, alias/escaping handling, `bare_argument`). A VM bug that exists only on those
direct-argument paths disappears under T and would be approved. The reviewer showed this with a
fake VM.
- **Control rewrite T′(P).** The same arguments are wrapped in injected identity functions. Each
  wrapped argument gets its own `fn(rtdiff_identity_K(v)) { return v }`, all declared at the
  start of line 1. One shared identity function was tried first: its return type is the union of
  every argument type, which makes the VM fall back (`type:push`, `type:<=`) on 5 of the 13
  oracle-approved programs. One function per argument stays monomorphic.

  The call then compiles as under T, but nothing is copied and the sharing is preserved. Two new
  checks, both required:
  - **1′:** Legacy(T′(P)) == Legacy(P).
  - **6:** VM(T′(P)) == VM(P) on every channel.

  If either fails, or a T′ run is refused or falls back (check 4 applies to it), the row is
  UNCLASSIFIED.
- **Permanent negative control** `n_direct_path_only_bug`. The fake VM's `direct-path` mode adds a
  stdout line only when the program has neither `deepcopy(` nor the identity wrapper, which is a
  bug on the direct-argument path alone. It is rejected by check 6. Before T′, the same control
  would have passed checks 1 to 5.

**Lows.**
- **Imported modules are scanned.** Every literal `import("...")` is resolved beside the script,
  parsed, and given the same static checks, recursively. An unresolvable or non-literal import,
  or an unparsable module, is UNCLASSIFIED. `Export` is a known node kind.
- **Concurrency built-ins** (thread, async, parallel, spawn) are unsafe. `Await` is no longer a
  known node kind, and an `@fn[async|thread|parallel]` declaration is rejected.
- **A parameter named like a declared function** (shadowing) is rejected.
- **New selftest cases** (36 checks, PASS):
  - a cycle store, `link(a, [a])`, where the VM refuses with "reference cycle" and T would remove
    the cycle: rejected by the cycle trigger;
  - a VM side that hangs past rt-diff's timeout (`n_timeout`, 4 s): check 4;
  - a thread call, an async task, a parameter shadowing a function (static);
  - modules: clean (approved), declaring a struct (rejected), missing.
- **The corpus rules for `vm-corpus/cp11/arrays.f` and `semantics/copy_at_call.f`** stay loaded,
  but they are now approvable by the oracle (run without their rules: both "five checks hold").
  `semantics/copy_at_return.f` is not: it has no call site to rewrite. Retiring them needs the
  owner's approval.
- **Gen rules now pin the program's modules.** A rule's `source_sha256` covered only `main.f`.
  Every `gen_seed` rule now also carries `tree_sha256` (every file of the program's directory,
  modules included), and rt-diff requires both to match; a `gen_seed` rule without it is refused
  at load. Each of the 38 values was computed after checking `main.f` against the pinned
  `source_sha256`.

**Consequence of the thread rule. BLOCKER for the gate's `[0] parity`, stopped and reported.**
- Six programs the oracle approved at C71 call `thread()`: seed-1 g00030, g00114, g00118, g00131
  and g00170, and seed-3 g00047. They are now UNCLASSIFIED.
- **Seed 1:** the 10 owner-approved exact rules of the five seed-1 programs (2026-11-08), retired
  at C71, are restored from `b5-refsem-vm-retired.json`, scoped (`gen_seed` 1, `tree_sha256`).
  These are old rules restored, not new approvals. The retired file keeps the 10 rules of
  g00024, g00034, g00073, g00139 and g00181, which the oracle still approves.
- **Seed 3:** g00047 has no prior rule, and new per-program approvals are not allowed (owner
  2026-11-10 06:15). So `[0] parity` fails again on exactly this one row: rt-diff reports 659
  entries, 1 differing, 0 unused policies; the oracle approves g00013 and g00021.
- The program is `th3 := thread(tw2, 3)`: a task over a declared function with a literal scalar
  argument. A narrower rule could allow `thread(declared_fn, scalar literals...)` when the task
  function's body reaches no aggregate shared with the caller. That needs a sound definition and
  an owner decision; it is not done here.

## 64. Owner 08:57, extra requirements for the batch: permanent controls (C75, tests and Makefile)

- **S-1, legacy really is legacy.** `tests/runtime2/legacy_engine_check.sh LEGACY VM` runs a
  probe that differs between the engines (a callee's `push(99)`: legacy prints `[1,2]`, the VM
  `[1,2,99]`). It fails if the two binaries agree or either deviates.
  - `test-runtime2-rt-diff-selftest` runs it on `nift-legacy` against `nift` (PASS), and then as a
    permanent negative control on the bare `nift` against itself, which must be rejected (it is:
    "both binaries print '[1,2,99]'").
  - `test-runtime2-prevalidate-gate` runs the positive check.
- **O-1:** the direct-path fake-VM control is a permanent selftest case (`n_direct_path_only_bug`,
  section 63).
- **Mutant 5, the objects differ.** `test-rt-vm-mutants` now first requires every
  `RT_VM_MUTATED` object to differ byte-wise from its mutant counterpart in `.build/mutants/`; an
  identical pair means the hooks compiled to nothing. All five differ, `vm_report.o` included.
  In the C72 gate run, mutant 5 was caught by the three suites that pin error positions, golden
  (60 of 108), d018 (15) and review (14), and by int-bounds, parity, adversarial, former-replay
  and records too: 12 of 12 rejected (section 62).

## 65. Owner 08:57, A: merge and merge_deep use the one-step clone (C76, production)

**Change.** `object_methods.cpp`'s `shallow_object`, the first step of `merge` and of each
`merge_deep` level, built the result with one `set` per receiver entry. Each set paid a
`find_position`, a `rebuild_index`, vector growth and a `heap_retally`, which is the
`Construction::object_set` / `heap_retally` profile of section 56. It now starts from
`object_shallow_clone`: the entries and the index are copied and the heap is retallied once.
- **One supporting line in `build.h`, flagged for review.** The clone is a published Value, and
  only a builder may populate a fresh object. So `build::Object` gains
  `static Object shallow_clone(const Value&)` with a private constructor from the fresh clone.
  This is exactly `build::Array::shallow_clone`'s existing form (tooling 2, fa10fc9b), not a new
  authority: the builder owns the fresh clone until `publish()` and holds nothing afterwards.
  `object_methods.cpp` could not do this alone: the alternatives were a builder entry point, or
  `obj_merge` as an `InPlaceKey` owner, which is the construction model's authority list.
- **Production sha256:** `f063f1a968b286c69b406d49d5d6f7978c379e2e69bba43f4d97744cd0a73b54`
  (was c88bf749…).

**Guarantees and their tests.**
- **Fresh outer identity.** Unit: the result is never the receiver's or the argument's object.
  CLI: `same(a.merge(b), a)`, `same(.., b)`, `same(a.merge_deep(b), a)` and `same(a.merge(a), a)`
  are all false. B2-pinned, because legacy's `same()` refuses objects.
- **merge shares nested values; merge_deep merges nested objects into new objects and shares
  the rest** (B2). Unit: identity checks against the former construction, entry for entry. CLI:
  B2-pinned outputs (a push through a merged result's nested array is visible in the receiver).
- **Sources unchanged.** Unit and CLI; legacy-equal for the contents.
- **No builder keeps authority after publish:** the existing builder discipline
  (`test-rt-build`'s B-M1/B-M2 mutants); the clone enters through the builder, never a raw Value.
- **Allocation failure: no partial construction, no leak.** `tests/rt/merge_alloc_fail.cpp`
  (`make test-rt-merge-alloc`, part of `test-rt-builtins`) fails every allocation point of a call
  in turn, with a replaced `operator new`, and checks after each failure:
  - outstanding allocations are back to their value before the call (no leak);
  - the live-heap tally is unchanged;
  - the receiver and argument are unchanged (keys, values, nested refcounts).

  It covers the clone (merge: 4 points; merge_deep: 6; merge of an object with itself: 3) and,
  side by side, a reference copy of the former per-entry construction (16 points). All pass;
  336 checks, 0 failures.
- **Accounting stays correct, nested values included.** After success, the live-heap tally grows
  by exactly the result's footprint. A VM-level comparison against a one-off binary built with
  the former `object_methods.cpp` (deleted afterwards) printed identical output for both CLI
  probes. Heap statistics were equal for the nested case and +64 bytes for the 3-entry case;
  the clone copies the source's index vector, and the tally counts it exactly.
- **No cycle check is skipped.** A fresh outer object cannot be reachable from the values stored
  in it, and the former construction (`build::Object::set`) had no cycle check either; the stored
  values are the receiver's and the argument's, unchanged.
- **CLI regression test** `tests/runtime2/merge_contract.py` (in `test-rt-merge-alloc`). Contents,
  order, sources unchanged, `o.merge(o)`, merge_deep and `{}.merge(a)` must equal legacy exactly;
  the two B2 cases are pinned. It also passes on the former binary.

**Original CP15 callgrind limits for `obj.merge` (not re-baselined): PASS at all four sizes and
the exponent.**

| n | Ir per call | CP15 guard |
| --- | --- | --- |
| 100 | 2,988 | 3,055 |
| 1,000 | 18,038 | 18,105 |
| 10,000 | 292,429 | 292,528 |
| 100,000 | 1,717,322 | 1,717,389 |

The exponent is 0.769 against 0.769. Before this commit `obj.merge` measured 22,848 to 18,314,388
Ir/call. `obj.merge_deep` also passes its guard. After A, the only remaining guard failure is
`str.replace` (B, next commit).
