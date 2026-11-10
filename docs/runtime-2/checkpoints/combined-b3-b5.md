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
  - `test-runtime2-rt-diff` (the same binary on both sides; a determinism check). Note (review of
    C72–C78, Low): since b61784a2 both sides run the VM, so this check has been VM against VM, not
    legacy against legacy. It still checks the determinism of the default engine; no legacy
    determinism check runs there.
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

## 66. Owner 08:57, B: the str.replace guard re-baselined (C77, tests only)

- **Reason.** 32e402b2 (C3) made `str.replace` build into `BudgetedText`: a heap-budget check
  before the result buffer is reserved, and before every larger growth. That check is required
  memory safety, it stays, and it costs instructions per call.
- **Old baseline (CP15, Ir/call):** 484, 2,945, 20,881, 209,316 and 2,081,931 at n = 1, 100,
  1,000, 10,000 and 100,000; exponent 0.998.
- **Measured** with the established workload (`tests/rt/builtins_bench.cpp`, `str.replace` over
  `builtins_guards.measure`, callgrind), 3 runs, identical to the instruction, before and after
  section 65: 581, 3,356, 23,002, 228,537 and 2,272,273; exponent 0.998. That is +97 Ir at n=1
  and +9 to +14 % from n=100.
- **New guard:** the measured values. The tolerance is unchanged and the same as every other
  guard (+10 % + 50 Ir, exponent slack 0.15); nothing generous was added.
- **Result:** `make test-rt-builtins-guards`, 35 workloads, 0 guard failures (after section 65 and
  this commit).
- **Negative control** (`tests/rt/replace_budget_negative.sh`, `make
  test-rt-replace-budget-negative`, a prerequisite of `test-rt-builtins-guards` and part of
  `test`). It builds `tests/rt/native_budget.cpp` against the real `string_methods.o`, which
  passes (105 checks), and against a copy whose `str_replace` builds into an unchecked
  `std::string`. The mutant must fail on native_budget checks, and it does: 20 CHECK failures (for
  example `live <= limit + kSlack`), after which it runs away and is killed. So removing the
  check cannot pass as an optimisation.
- **Approving a cost required for memory safety does not license future regressions:** the new
  values are the guard, at the same tolerance as every other.

## 67. Owner 08:57, C: construction requires cycle-free input (C78, docs and a test)

- **Contract.** The construction primitives (`Construction` in `src/rt/build.h`, reached
  through `build::Array`, `ArrayBulk`, `Object` and `Struct`, the `InPlaceKey` holders and the
  storage tests' accessor) and natives' `ArrayBuilder` do **not** check for reference cycles.
  Their callers must pass cycle-free input. A fresh value is unreachable from what is stored in
  it, so every builder use in production satisfies this; section 65's merge clone is one such
  use. The test helper that broke it (`builtins_unit`'s `array_push(a, a)`, section 56) went
  through the storage tests' accessor.
- **Recorded in the header comments** of `build.h` ("Cycle-free input") and `natives/support.h`
  (`ArrayBuilder`). Comments only: `nift` is byte-identical (f063f1a9…).
- **If an embedding API ever exposes these functions**, their cycle-safety contract must be
  reconsidered first. That is the one unguarded route in the review of C67–C71 (section 2).
- **The debug assertion (P0-L1) is deferred,** per the owner; not added.
- **Public operations refuse cycles: permanent test** `tests/runtime2/cycle_refusal_routes.py` (in
  `test-runtime2-refsem`). It covers the reviewer's refused routes that had no fixture of their
  own: `a.push(a)`, `a[0] = a`, `b := a; b[1] = a`, through an object (`a.push({"k": a})`,
  `inner := o["k"]; inner.push(o)`), mutual arrays, `insert`, `splice`, a nested literal, a
  function parameter and a closure-captured binding.
  - Each must run on the VM in one VM execution, print `runtime.reference_cycle` (catchable) and
    leave the receiver's length unchanged.
  - Legacy, which copies (an approved difference), is pinned as an observation.
  - 11 routes, 0 failures.
  - The existing refsem fixtures keep covering maps, stacks, cells, structs and closure-cycle
    witnesses (`refsem/approved/*cycle*`).

## 68. Review of C72–C78, T-1: clone routes in the cycle tests, and a copy_summary negative control (C79, tests)

- **The gap.** A clone must carry its source's "may hold identity" summary (`copy_summary`):
  the cycle check skips a value whose summary says it cannot hold identity. The reviewer's mutant,
  `object_shallow_clone` without `copy_summary`, stored a real cycle
  (`arr := [1]; o := {"a": arr}; m := o.merge({"b": 2}); arr.push(m)`) and passed every merge
  test. Production refuses it with `runtime.reference_cycle`.
- **`cycle_refusal_routes.py` gains 8 clone and copy routes** (19 routes in total), each refused
  on the VM before the store, with legacy (copies) pinned:
  - `merge-result` and `merge-deep-result` (`object_shallow_clone`);
  - `copy-map`, `deepcopy-map` and `copy-stack` (`collection_shallow_clone` via `copy_graph`);
  - `copy-object` and `copy-array`;
  - `loop-snapshot`.
- **Negative control** `tests/runtime2/clone_summary_negative.sh` (`make
  test-runtime2-clone-summary-negative`, a prerequisite of `test-runtime2-refsem-mutants`). For
  each clone site it links a temporary `nift` from a copy of the source with that one
  `copy_summary` call removed (test-side only; production source and binary unchanged,
  f063f1a9…), and `cycle_refusal_routes.py` must fail on it. The unmodified binary passes first.
  - `object_shallow_clone`: **caught** (merge-result, merge-deep-result).
  - `collection_shallow_clone`: **caught** (copy-map, copy-stack).
  - `array_shallow_clone`: survives, as expected and reported. Its clones are only internal
    snapshots that no script can store (the for-loop source in `vm_iter.cpp`, the sort keys in
    `vm_array_mutation.cpp`, the collection source in `collection_helpers.cpp`).
- **Every `copy_summary` site, checked:**
  - `object_shallow_clone`, `collection_shallow_clone`: covered and caught;
  - `array_shallow_clone`: internal only, listed;
  - `array_write`: exists only in the `NIFT_REFSEM_MUTANT == 24` build.

  `copy_graph`'s deep copy fills its copies through the in-place key's construction primitives,
  which mark the summary per stored value (`aggregate_store.h:171`), with no `copy_summary`. The `deepcopy-map` route covers it, and refsem's
  `cycle_*` fixtures cover the rest.

## 69. Review of C72–C78, L-1: the mutant-list lint covers the per-number families (C80, build lint)

`scripts/build/check-mutant-lists.py` (section 61) covered 5 explicit-list families. It now also
checks the **per-number** form: mutant N is built by compiling specific sources with
`-DMACRO=N`. Every source whose code tests `MACRO == N` (or `!=`; comments excluded) must be
compiled into mutant N's build. A header hook counts when a compiled source includes it directly.
Every hook number must have a build; a number can be exempted only as retired, with its record,
and none is today.

**Extractors (read from the real wiring, not a copied list):**
- **`make`:** `define` templates compiled with `-DMACRO=$(1)`, their `$(call ...,N,stem)` and
  `$(foreach ...)` instances, and explicit `x.o: src/...cpp` rules whose recipe passes
  `-DMACRO=N`. Sources: the Makefile and every `tests/**/*.mk`.
- **`shell`:** one-line compiles `-DMACRO=N ... -c src/x.cpp` in test scripts.
- **`b4`:** the `[N]=src/x.cpp` table of `tests/runtime2/b4/mutants.sh`.
- **`cp14b`:** the `MUTANTS` rows of `tests/runtime2/cp14b/mutations.py`.

**Families checked, all clean:**

| Family | Wiring | Numbers |
| --- | --- | --- |
| cp19 admission and proof (`NIFT_SESSION_ADMISSION_MUTANT`) | `admission_mutants.mk` (1–13), `proof.mk` (14–26; M17 compiles two sources) | 1–26 |
| cp19 resource (`NIFT_SESSION_RESOURCE_MUTANT`) | `resources.mk` | 1–6 |
| cp19 job (`NIFT_SESSION_JOB_MUTANT`) | `jobs.mk` | 1, 2 |
| cp19 session (`NIFT_SESSION_MUTANT`) | Makefile | 1–7 |
| COLLECTION | cp14b `mutations.py` (11 retired: its hook is gone) | 1–10 |
| B4 | `b4/mutants.sh`, plus A4 in `admission_mutants.mk` and A9 in `safety/mutants.sh` | 1, 3–7, 9–18 |

**Not checked, with the reason:**
- **EXC:** `cp13b/mutant_build.py` builds a whole clean tree with the flag, so every source is
  compiled into every mutant by construction.
- **Each of these families wires its numbers in its own script** (shell loops and maps of varied
  forms): REFSEM (`refsem/mutants.sh`, `rt/isolation/run.sh`), HOST (`host_mutants.sh`), B4B
  (`b4b/mutants.sh`), B5 and D2 (`b5/mutants.sh`), READ_LIMIT, PACKAGES, B3, COLLECTIONS, SAFETY,
  FILE_MODE, ARGSCMD, TLS, B06, INTERP (`literals/*negative_control.sh`).
  - The generic extractors read none of them: they report every hook as "no build", which is an
    extractor gap, not a finding.
  - Covering each needs a parser for its script, which was not done here.
  - Each family's own harness requires its mutants to be caught, so a compiled-out hook shows
    there as a surviving mutant, though without naming the cause.
  - Addendum (review of C79–C82, C80-L1; section 79): REFSEM is also wired in the Makefile, not
    only in its scripts. `RT_REFREP_MUTANTS` (`test-rt-refrep-mutants`) builds 21 and 24–31, and
    `stale-vm-r19` builds 19, by compiling aggregate.cpp and aggregate_collection.cpp, the units that
    include `aggregate_store.h`, with `-DNIFT_REFSEM_MUTANT=19`. `refsem/mutants.sh` builds 1–16,
    21–23 and 31, and `rt/isolation/run.sh` uses 26 as an isolation-guard plant.
- **Makefile rule families the generic extractor already reads, with no finding** (not added to
  the gate): R1, KIND, CP21_D2, CP21_D3, INVENTORY, INVENTORY_HOST.
- **Not readable by the generic extractor:**
  - CP20, CP21 and MUTATION: Makefile pattern rules on `$*`.
  - SORT: a shell loop in a Makefile recipe.
  - RT_BUILDER: header hooks compiled into a test translation unit.
  - CP21_D1: a truthiness hook, plus `== 0` as the "no mutant" case.
  - SHELL_DISPATCH and CP19_EVAL: named, not numbered, macros.

**Selftest:** three new planted per-number cases.
- Control: a hook compiled into its build is accepted.
- A hook `== 1` in a source mutant 1's build does not compile: rejected.
- A hook number with no build: rejected.

Together with the four explicit-list controls, all pass.

## 70. Owner 10:10: the g00047 one-row exception (C82, tests and docs)

**Decision.** Owner 2026-11-10 10:10 (review REVIEW-C72-C78.md): `gen/g00047/main.f` of the
mutation gate's parity corpus (`rt-diff --gen 50 --seed 3`) is approved as an explicit B5
semantic difference, by one exact row.
- It is not a general exception for threaded programs and adds no thread logic to the oracle.
- The oracle's fail-closed rules are unchanged: it still reports g00047 UNCLASSIFIED
  (`identity-sensitive, handle or concurrency built-in thread()`).

**Evidence from the program's source** (generated by `tools/rt-diff/gen.py --seed 3`, index 47):
```
     3	fn(tw2(n)) { return is_dir(".") }
     4	th3 := thread(tw2, 3)
     5	print(th3.join())
    ...
    14	fn(mut6(xs)) { xs.push(99); return xs.length() }
    15	a7 := [1, 2]
    16	print(mut6(a7))
    17	print(a7.stringify())
```
- The thread starts at line 4 and is joined at line 5. `join()` blocks until the task has
  finished, so it completes before line 6 runs.
- The array `a7` is created at line 15, after the join.
- The task function `tw2` receives only the scalar `3` and calls `is_dir(".")`; it captures nothing.
- The only difference is line 6 of stdout, `print(a7.stringify())`: legacy `[1,2]`, VM
  `[1,2,99]`, because `mut6`'s `push(99)` reaches the caller's array (B5). rt-diff reports stdout
  and combined only; exit, stderr and trace are identical.
- The program has no module files.

**The rules** (`tools/rt-diff/policies/b5-refsem-vm.json`): `b5-gen-seed3_g00047_main-stdout`
and `-combined`, in the form of the restored historical rules.
- `decision: D-NEXT-refsem-b`, `gen_seed: 3`, entry `gen/g00047/main.f`.
- `source_sha256` 02f3ce30790fb7206f8edb68ebd44a121bd555b27d1662312f2b29f335e80738 (main.f).
- `tree_sha256` cfe7504cbf1eca367a56f9796dbb981fdbf68690a798bb07c067d2d02bce5e95 (every file of
  the program's directory).
- old `"[1,2]\n"`, new `"[1,2,99]\n"`, count 1: exactly that line.
- The justification cites the owner decision, the review and the evidence above.
- There is no exit or stderr rule, so both must stay identical. Any other output change,
  diagnostic, refusal or crash is a difference.

**Expiry** is by the pins: a changed `main.f` or any added or changed file in the directory stops
both rules from matching, and they are then reported unused, which fails the gate.

**Controls** (`tests/runtime2/g00047_exception_controls.py`, `make test-rt-vm-g00047-exception`, a
prerequisite of `test-rt-vm-mutants` with a guard edge). g00047 is generated afresh and run alone,
with exactly these two rules loaded. All pass:
- **Positive:** approved by both rules (stdout and combined), nothing differing, no unused rule.
- **Without the rules:** the oracle reports it UNCLASSIFIED (`thread()`).
- **(a) An unrelated extra stdout line** (fake VM): rejected (stdout, combined).
- **(b) An extra stderr line:** rejected (stderr, combined). **A changed exit status:** rejected
  (exit).
- **(c) A one-byte source change that keeps the difference** (`index 47` becomes `index 4X` in the
  comment): the rules stop matching; differing, rules unused.
- **(d) An extra module-input file** in the program's directory: the rules stop matching. The
  program has no modules, so this is the directory pin, `tree_sha256`.
- **(e) The same rules under `--seed 1`:** out of scope (`out_of_scope_policies`), neither used
  nor unused, no approval.

Recorded in `docs/runtime-2/compatibility.md` (the B5 shared-references table) as an
owner-approved B5 semantic difference.

**Acceptance (in tree, production f063f1a9…, a freshly rebuilt `nift-mutants`, `make -q` = 0):**
- **The gate's `[0] parity`:** PASS. rt-diff: 659 entries, **0 differing, 0 unused policies**;
  the oracle approves g00013 and g00021; 38 seed-scoped rules out of scope.
- **The seed-1 tier:** PASS. 817 entries, 0 differing, 0 unused; oracle 5; 2 seed-scoped rules
  (the g00047 pair) out of scope.
- **`make test-rt-vm-mutants`:** PASS. The mutant-list lint and the object check, the oracle
  selftest (36) and the g00047 controls run first. The unmutated binary passes every suite, and
  12 of 12 mutants are rejected.
- The guard, rt-lint and test-integrity pass.

## 71. Repair batch (owner 2026-11-10 12:21), B08: the host source list is generated (C83, build lists)

- **Cause** (triage BLOCKERS.md B08). `bindings/python/build.sh` compiles the sources named in
  `src/rt/host-sources.txt`, a hand-kept file. The Makefile's `RT_HOST_LINK_SOURCES` uses a
  wildcard over `src/rt/handles/*.cpp`, and the file lacked `handle_box.cpp` and
  `table_teardown.cpp` (the latter defines `HandleTable::shutdown_noexcept`), so the extension
  failed to load. `test-rt-host-sources-list` already compared the file with the wildcard; it was
  failing, not missing.
- **Fix.** New `make update-rt-host-sources` rewrites the list from the same
  `RT_HOST_LIST_EXPECTED` the check uses, keeping the header comment and sorting the paths. The
  file is regenerated: 40 sources, the two above added. The check's failure message now names the
  regeneration command, so the list cannot drift silently.
- **Results.** `test-rt-host-sources-list` OK. `python-binding` builds
  `nift/_nift.cpython-313-x86_64-linux-gnu.so`. `test-python-binding`: 28 tests OK, and
  `import nift` works. This was Python 3.13 locally; the Python 3.14 import is for Linode.
- No production change: `nift` is f063f1a9….

## 72. Repair batch, B11: Pool workers take their state explicitly (C84, harness)

- **Cause** (triage BLOCKERS.md B11). Python 3.14 starts Pool workers with forkserver by default.
  A forkserver or spawn worker imports the module afresh and does not inherit module globals set
  in `main()`. `tests/runtime2/b4b/gen_alias.py` set `ARGS` in `main()` and its workers read it,
  so every worker saw `ARGS = None`. Reproduced locally on Python 3.13 by forcing spawn:
  `AttributeError: 'NoneType' object has no attribute 'nift'`.
- **Fix** (no switch to fork). gen_alias.py passes `ARGS` through `initializer=_init,
  initargs=(ARGS,)`.
- **Audit of every Pool user.** These five are all of them:
  - `b4b/fixpoint.py`, `b4b/gen_alias.py`, `b5/gen/gate.py`, `b5/gen/gen_evalorder.py` and
    `b5/gen/gen_operators.py`.
  - The other four already pass their state through an initializer.
  - gate.py's job functions come from normally imported modules (gen1, gen_struct), so they pickle
    under every start method.
  - Each of the five now takes its Pool from `multiprocessing.get_context(NIFT_MP_START_METHOD or
    default)`, so the checks below can force a start method. Unset, it is the interpreter's default.
- **Results** (Python 3.13.x locally; forkserver and spawn forced through `NIFT_MP_START_METHOD`):
  - gen_alias `--sample 60`: PASS under fork, forkserver and spawn (60 programs, 26 by the VM,
    0 bad).
  - gate.py (gen_struct 1911 programs, 0 bad), gen_evalorder.py (840, 0 bad) and gen_operators.py
    (880, 0 bad): PASS under both spawn and forkserver.
  - fixpoint.py: `--self-test` PASS under spawn. Its pool path (`run_sources`) ran 3 programs
    under spawn and under forkserver, 0 crashed.
  - `make test-runtime2-b4b-gen-alias` (default start method): 12600 programs, 5532 by the VM,
    0 bad, and both negative controls PASS.
  - `make test-runtime2-guardoff` with the default start method:
    - every content check passed: b4b-types 6597 checks, gen_alias 1500 programs (0 bad), the
      safety fuzz 12512 programs, the guard-off cross-check 2245 scripts (0 failures), result
      identity;
    - its closing repo-state check failed only on my own concurrent, uncommitted edits to other
      files during the run (the B10 and B14 work below), not on anything the gate wrote.
- **For Linode.** `test-runtime2-guardoff-sanitize` needs the full sanitizer object set (about
  800 MB), more than this machine's free disk. It and the Python 3.14 default-start-method runs
  of the three gates are for Linode.
- No production change: `nift` is f063f1a9….

## 73. Repair batch, B10: the cp14b harnesses use the configured toolchain (C85, harness)

- **Cause** (triage BLOCKERS.md B10). The four cp14b harnesses that compile or link
  (`build_sanitizer.py`, `build_drain.py`, `mutations.py`, `design_mutations.py`) had three faults:
  - They called `g++` by name, whatever `CXX` was. The Makefile already passed
    `NIFT_CP14B_CXX="$(CXX)"`, but only the comparator profile read it.
  - They linked a hard-coded `.build/libffi/x86_64-linux-gnu-cc/install/lib/libffi.a`. The real
    path is `$(LIBFFI_A)`, which depends on `$(CC)`, so it differs on Linode.
  - They took their objects from `rglob('*.o')` or from a hand-kept source list. The sanitizer's
    list lacked `src/PackageTransaction.cpp`, hence the undefined
    `PackageTransaction::PackageTransaction`.
  - A failed link also hid the linker's stderr (`check=True, capture_output=True`).
- **Fix.** The new `tests/runtime2/build_toolchain.py` reads one Makefile variable,
  `HARNESS_TOOLCHAIN_ENV`, which sets:
  - `NIFT_TC_CXX`, `_GUARD` (`$(PRODUCTION_GUARD)`), `_CPPFLAGS`, `_CXXFLAGS`, `_LDFLAGS` and
    `_LIBFFI_A`;
  - `_CLI_OBJECTS` (`$(CLI_OBJECTS)`, nift's own link set);
  - `_RT_SOURCES` (`$(RT_VALUE_SOURCES) src/RuntimeValue.cpp`).

  A harness started outside make stops and names the missing variable. Every compile and link goes
  through `run()`, which on failure prints the command and the tool's stdout and stderr.
- **What each harness now does:**
  - mutations.py compiles each named mutant with nift's exact compile line plus the mutant define,
    into the non-production `.build/cp14b/mutants`. It requires the object to differ from
    `src/rt/<stem>.o` (the C75 check) and links nift's object list.
  - design_mutations.py compiles its drivers as test units (no guard). It compiles mutant sources
    as production units, with `-iquote src/rt` so a copied source still finds its `"..."` headers
    (never `-I src/rt`).
  - build_drain.py links nift's objects without `src/nift.o`.
  - build_sanitizer.py compiles `$(RT_VALUE_SOURCES)` plus RuntimeValue.cpp, 255 units, which
    include PackageTransaction.cpp and JsonFile.cpp. Production units get the guard and the
    harness does not. It links `$(LIBFFI_A)`. Its sanitizer flags are unchanged.
  - resources.py reports the configured compiler's version.
- **Results** (GCC 13 locally):
  - `make test-runtime2-cp14b-mutants`, both tiers:
    - named mutants: 3–8 and 10 DETECTED; 1, 2 and 9 UNVERIFIED, the comparator cases this
      profile refuses (D2);
    - design mutants: 1–5 and 7–11 DETECTED, 6 RETIRED, 12 UNVERIFIED.
  - `make test-runtime2-cp14b-sanitize`: the sanitizer harnesses build (they had never linked:
    255 units), and the drain and allocation-failure witnesses run. Every functional, sort replay,
    origins, copy and const check under ASan/UBSan PASSes or is a comparator SKIP of the same kind.
    The 580 MB build directory was removed afterwards.
  - A deliberately failing link (`src/rt/vm.o` alone) prints the command and the linker's
    `undefined reference` lines.
  - Comparator certification still needs the GCC 15.2 run on Linode (D2).
- No production change: `nift` is f063f1a9….

## 74. Repair batch, B14: the isolation negatives' diagnostic pattern is quote-agnostic (C86, tests)

- **Cause** (triage BLOCKERS.md B14). `tests/rt/isolation/run.sh` required the text
  `poisoned "StorageTestAccess"`, which is how GCC 13 quotes the name. GCC 15 prints
  `src/Hooks.cpp:118:29: error: attempt to use poisoned 'StorageTestAccess'`, so seven negatives
  that did fail as intended were reported as failing for the wrong reason.
- **Fix.** The pattern is now
  `poisoned [^[:alnum:]_ ]{0,3}StorageTestAccess([^[:alnum:]_]|$)`. It accepts any quote: `"`,
  `'`, or the 3-byte `‘…’` of a UTF-8 locale, although the script runs with `LC_ALL=C`. The
  name must still end there.
- **A self-check of the pattern** runs first in the script, on recorded text:
  - It must match the GCC 13 line, the GCC 15 line above and the UTF-8 form.
  - It must not match `poisoned 'StorageTestAccessor'` or `'StorageTestAccess' was not declared`.
  - Negative control: the old pattern fails this self-check, missing both the GCC 15 and the
    UTF-8 lines.
- **Results.** `make test-rt-isolation` on GCC 13: 31 PASS, including the 9 poison cases (for
  example `n2a-annotated-definition: ... poisoned "StorageTestAccess"`). Afterwards `make -q nift`
  returns 0. The GCC 15 run is for Linode; the recorded GCC 15 line above is covered by the
  self-check.
- No production change: `nift` is f063f1a9….

## 75. Repair batch, B15: mutants build in a sanctioned non-production directory (C87, harness)

- **Cause** (triage BLOCKERS.md B15). Nine gates built their mutants by running `make nift` with
  mutant `CXXFLAGS`, in an archive of HEAD or a clean worktree. Each such build compiled every
  object with `compile-object.sh --production`, whose guard (tooling.md 3) correctly refuses a
  test-only switch. Example: `NIFT_EXC_MUTANT` on `src/ParserHost.o`. Some mutant patches had
  also gone stale. Every one of those builds was also a full tree copy plus a full build.
- **The sanctioned build, `tests/runtime2/sanctioned_mutant.py`:**
  - It compiles only the units the mutant changes:
    - for a define, every source of the object set that names the macro, or whose recorded
      dependencies (`.d`) include a header naming it;
    - for a patch or text edit, the patched files.
  - Each uses nift's own compile line (`$(CXX) $(PRODUCTION_GUARD) $(CPPFLAGS) $(CXXFLAGS)`, from
    `HARNESS_TOOLCHAIN_ENV`, C85) and goes into `.build/mutant-nift/<name>`, a non-production
    directory.
  - It links them with nift's other objects. Source identity is otherwise production: the working
    tree's own sources, not an archive of HEAD.
  - The C75 check: each mutant object is compared with the production object. An identical one is
    not linked and is reported. If none differs, the mutant is dead and the build fails. Compiling
    an unchanged copy of a source gives a byte-identical object (checked), so the comparison is
    meaningful for patched copies too.
  - A patch or edit that no longer applies is a harness failure (a stale mutant), never a detection.
  - Three configurations:
    - `default` (nift);
    - `pic` (`$(C_ABI_PIC)`, adds `-fPIC`);
    - `san` (`$(SAN_OBJECTS)` with the sanitizer compile and link lines, `HARNESS_SAN_TOOLCHAIN_ENV`).
  - `--unit TEST.cpp` links a unit test instead of nift, with the object set named by
    `--objects-env`.
  - Every caller deletes its mutant directory afterwards. `provenance.json` records the sources,
    patches, edits, objects and hashes.
- **The nine gates:**
  - **test-runtime2-module-host-order-mutant** (R1): PASS, 2 mutant objects; module_host_order
    fails in 8 cases.
  - **test-runtime2-literals-mutants**: 4/4 PASS. **test-runtime2-literals-f1-negative**: PASS
    (15 F1 failures).
  - **test-runtime2-literals-decimal-negative**: PASS (6 silent divergences).
    - The old mutant deleted a `v.is(Kind::Decimal)` line from `infer.cpp`. That code moved to
      `infer_number.cpp` (dcdea681) as an `if` plus its `return`.
    - The mutant is now `tests/runtime2/literals/mutants/decimal-infer.patch`, which removes that
      branch.
  - **test-runtime2-cp13b-mutants** (EXC): E1–E8 and the checked E2 build and are detected (9 of
    10 checks), and stress and lifecycle PASS.
    - E7 under ASan is built from `$(SAN_OBJECTS)` (`--config san`); the target now depends on
      `$(SAN_TARGET)`.
    - The full sanitizer object set (about 800 MB) does not fit this machine, so the E7 ASan
      check is **for Linode**.
  - **test-runtime2-cp17-routing-mutants** and **test-runtime2-cp17-handles**: PASS. The routing
    patches called `run_script_on_vm(source, path, exit)`, a stale signature (the
    `ParserScript.cpp:342` error). They now pass the command and arguments
    (`std::string(), std::vector<std::string>()`), and the mutant reaches the VM selector in all
    four kinds.
  - **test-cp23-mutants**: PASS, 10 real mutations, 120 controls rejected.
    - Its "build failure output-publication" on Linode was the hard-coded `g++` and libffi path
      (as in B10). It now uses `build_toolchain`, compiles each mutated copy under its source's
      own basename with `-iquote`, requires the object to differ from production, and deletes the
      mutant binary and object once recorded.
  - **test-rt-host-mutants**, the host part (`host_mutants.sh`): 13 behavioural mutants (1–9 and
    11–14), the M10 formula control and N2 15/16 all PASS.
    - It used 16 full builds before; each is now a sanctioned build.
    - Unit mutants 4, 15 and 16 link `$(RT_UNIT_OBJECTS)`. Mutant 13 links the host-random-budget
      closure, computed as its rule computes it; stub objects without a source are linked unchanged.
    - The N2 rebuild-decision witness keeps its existing private rule; its products are removed.
    - The cp17 parts of test-rt-host-mutants are the routing gate above, the handle and N2 gates
      (unchanged), and the unwind gate (B22, section 76).
- No production change: `nift` is f063f1a9….

## 76. Repair batch, B22: cp17-unwind-mutants finishes in about 200 s (C88, harness)

- **Why it never completed** (triage BLOCKERS.md B22): too slow, not a deadlock or a
  non-terminating mutant.
  - The harness copied the working tree per mutant, about 576 MB with `.build/libffi`, `rt` and
    `pic`: 277 MB of tree, 245 MB rt and 28 MB pic.
  - Its ignore list kept only `libffi`, `rt` and `pic` from `.build`, so the copy had no
    `.build/obj-config`. The Makefile writes the object signature when it is read, so the first
    `make` in each copy created `obj-config/default.flags` with a fresh mtime. Every object
    depends on that file.
  - Evidence: `make -n -W .build/obj-config/default.flags nift` lists 298 compiles, and
    `make -n -W .build/obj-config/pic.flags …persistent` lists 299.
  - So each mutant recompiled nift's objects (19 `vm` and 3 `unit` mutants, whose unit links
    `$(RT_UNIT_OBJECTS)`) or the pic set (2 `persist`), under 3600 s inner timeouts: hours in total. Locally, a single copy is more than half the free disk.
  - Per-mutant evidence from the new run (below): every mutant builds in 1.3 to 4.6 s.
    - Each witness run finishes in at most 1.7 s, except the five oom `HANG` and `CHECK` mutants.
    - Those print their witness line and stop at the unit's own 20 s hang watchdog (exit 3).
    - None runs to the cap.
- **Fix:**
  - Each mutant is a sanctioned build (section 75, `sanctioned_mutant.build` with an exact text
    edit): the mutated unit compiled with its production line and linked with the production
    objects. That is nift for `vm`, `$(RT_UNIT_OBJECTS)` with the unit test for `unit`, and
    `$(C_ABI_PIC)` with the persistent-allocation program for `persist`.
  - The oom witness compiles `$(RT_HANDLE_SOURCES)` as its rule does: the control objects once,
    then only the mutated unit per mutant, which must differ from the control object.
  - The C75 comparison replaces the old mtime and sha provenance. The production artefacts are
    the controls.
  - Progress lines carry elapsed time, the mutant number and the build and witness seconds.
  - `--cap` (default 300 s) bounds each witness run.
- **Timeouts.** A timeout alone is never a kill. A run that reaches the cap counts as a kill only
  if the expected witness text was already printed: the restored defect is itself a hang after
  the CHECK line. Otherwise it is a harness failure.
- **Stale mutants.** Five mutants named `src/rt/compiler.cpp`, but their while-loop sites moved to
  `compiler_loops.cpp` (a43472ed). Their text is unchanged; the path is corrected.
- **Results.** `make test-runtime2-cp17-unwind-mutants`: PASS, 32 mutants, 0 failures, 201 s.
  - The dead-binary control passes, and so does every control kind.
  - Every mutant is killed by its named witness.
  - The certification cap is 3600 s.
- No production change: `nift` is f063f1a9….

## 77. Repair batch, B17: the six unclassified cp25r reasons are classified (C89, inventory)

- **Cause** (triage BLOCKERS.md B17). `inventory.py report --check` failed on six reasons with no
  classification row: `coro-blocking-host:file`, `coro-blocking-host:rm`, `method:open`,
  `method:write`, `method:save` and `method:close`. Reproduced on the local inventory
  (`.build/cp25r-inventory`, collected from this candidate).
- **Where they come from.** All six come from one unit, `rt-diff-corpus/async/fileops/main.f`,
  and only in classify-all:
  - In an async function, `f := file(name); f.open("w"); f.write(name); f.save(); f.close()`, then
    `exists`/`rm`/`exists`.
  - classify-all compiles with the default CompileLimits (`parallel_tasks=false`), where the
    cooperative-only rule A1 refuses host built-ins in task code
    (`compiler_coro_admit.cpp:138`). That gives `coro-blocking-host:file` and
    `coro-blocking-host:rm`.
  - Its `f` is then Null-kind (detail `Null->method:open`, and so on), and each method call is
    refused at `compiler_call.cpp:204`, consequentially.
  - In execute mode, and in a real run (`NIFT_VM=1`), the unit runs on the VM (`vm 1`) with legacy's
    output, `a.txt:true:false` and `b.txt:true:false` (legacy `NIFT_VM=0`: the same).
- **Classification.** Class H, family `F12-classify-all-switches`: the existing approved category of
  the same unit kind's `coro-blocking-host:exists` row ("tooling: ... classify-all compiles with
  default CompileLimits ... Fix inventory tool to apply the run-time switches"). The approval
  column is that row's: `pending (tooling artefact: not a real-run fallback reason)`. No reason
  needed a new category, so none is flagged PENDING-OWNER (B27).
- **`--check` names pending approvals.** It now prints one line naming every seen reason whose G2
  approval is still pending (`pending ...` or `PENDING-OWNER ...`), with the PENDING-OWNER ones
  separately. A G1 pass is therefore never read as G2 approval. It does not fail on them: G2 is
  the owner's.
- **Results.** `report --check` on the local inventory now exits 0 (it exited 1 with the six
  `unclassified reason` lines). It prints
  `G2 approval pending for 135 of 157 reason(s)`, with no PENDING-OWNER rows.
  - A fresh `make cp25r-inventory` (collect plus check) is the Linode run.
  - The classification's own `raised_at` for `coro-blocking-host:exists` still says
    `compiler_coro_admit.cpp:128`; the site is now line 138. That pre-existing row was left as it
    was; the new rows name 138.
- No production change: `nift` is f063f1a9….

## 78. Repair batch, B29: the handle-string lint's one exact exception (C90, lint)

- **The finding** (triage BLOCKERS.md B29): `src/rt/render.cpp:11`,
  `return std::string("\x1fnift:callable:") + (function.is_lambda`.
- **What it is:** an intentional exception, not a legacy handle string.
  - It is `rt::callable_marker`, legacy's internal callable tag.
  - It is used only for `nift eval --json` of a function value (`eval_result.cpp`). That output
    is kept as it was by review L1 (b4-dispatch.md), and is carried for an owner decision.
  - It names no handle: CP17's typed handles (files, commands, streams) are what the lint
    guards.
  - It was added on 2026-10-27 (61136086), after the lint (f561e341, 2026-10-19), and the gate
    has failed since then.
- **The allowlist:**
  - `tests/runtime2/cp17/no_handle_strings.py` gains `ALLOWED`. It has one entry, keyed by the
    repository path and the line's exact stripped text, with its reason.
  - The general pattern and the comment rule are unchanged.
  - An entry that matches no line in a scanned root is itself a problem, so it cannot outlive
    its line or move to another.
  - Each accepted line is printed with its reason.
- **Self-test:** 11 cases. The 8 existing ones, plus three for the allowlist:
  - the pinned line is accepted in its file;
  - the same line in another file is flagged;
  - a changed line in the allowlisted file is flagged.
- **Results:**
  - `no_handle_strings.py src/rt`: 0 problems, with one `allowed` line.
  - A planted `"\x1fnift:file:"` in another file is still flagged.
  - `make test-rt-handles` PASS.
- No production change: `nift` is f063f1a9….

## 79. Repair batch: the Lows of the review of C79–C82 (C91, tests, comments and record)

- **C79-L1: the array clone's "internal snapshot, never stored" rule is enforced structurally.**
  - **Comments.** Each `array_shallow_clone` caller now says so in a trailing comment on the call
    line, so no line moves. The three callers are `vm_array_mutation.cpp:75`,
    `collection_helpers.cpp:177` and `vm_iter.cpp:52`; the declaration's comment in `aggregate.h:50`
    is changed too. `make nift` after the edits gives the same binary, `nift` f063f1a9…, and
    `test-rt-lint` passes (line length included).
  - **The negative control requires survival.** `clone_summary_negative.sh` now fails when the
    `array_shallow_clone` mutant (its `copy_summary` call removed) is **caught**: a route catching
    it would mean a script can reach and store a snapshot. The other two clone mutants must still be
    caught.
  - Each mutant object must differ from the production object (C80-L2).
  - Result: control PASS, 21 routes; `object_shallow_clone` and `collection_shallow_clone` caught;
    `array_shallow_clone` SURVIVED, as required; negative-control PASS.
- **C79-L2: deepcopy routes for arrays and objects.**
  - `cycle_refusal_routes.py` gains `deepcopy-array`
    (`arr := [[1]]; c := deepcopy(arr); x := c[0]; x.push(c)`) and `deepcopy-object` (the same
    with `{"k": [1]}`).
  - These reach copy_graph's per-value marking (`note_store`, through
    `Construction::array_push` / `object_set`).
  - Both are refused on the VM with `runtime.reference_cycle`, the length unchanged. Legacy's
    copy prints `2` (pinned).
  - 21 routes, 0 failures.
  - No copy_graph-specific mutant was built for them: the routes were added as the review asked.
- **C80-L1:** section 69 records REFSEM's Makefile wiring:
  - `RT_REFREP_MUTANTS` (`test-rt-refrep-mutants`): 21 and 24–31;
  - `stale-vm-r19`: 19, through aggregate.cpp and aggregate_collection.cpp.

  The addendum sits beside the family list.
- **C80-L2: the C75 check on the script-wired families.**
  - Each mutant object is compared with its production object:
    - the six per-number scripts that compile `$src` into `$mobj` with `-DNIFT_*_MUTANT=$n` (`b3`,
      `b4`, `b4b`, `refsem`, `collections`, `safety`);
    - the Makefile's `test-rt-refrep-mutants` loop;
    - the clone-summary negative control;
    - every sanctioned build (section 75).
  - An identical object stops the family with exit 2 ("a compiled-out hook").
  - These scripts compile without `PRODUCTION_GUARD`. The guard only defines a macro, so that
    makes no difference to the bytes: `vm_ops`, `aggregate` and `collection_helpers` compiled
    without it are byte-identical to production. The comparison is therefore meaningful.
  - Results: `test-rt-refrep-mutants` 9 of 9 caught; `test-runtime2-collections-mutants` PASS.
  - The other four families' full runs are long and were not repeated here. Their edit is the same
    line, which can only add a failure; they run in certification.
- Production source: comments only. `nift` is f063f1a9… before and after.

## 80. Repair batch, B28: stale legacy-path expectations re-derived for the VM default (C92, tests)

**Method** (owner-approved B28):
- No golden file was regenerated; every recorded oracle is kept as it was.
- An expectation recorded from legacy is now checked on the **legacy channel** (`NIFT_VM=0`). Before
  the VM default (0503062d), an unset `NIFT_VM` meant legacy.
- Where a gate also checks the VM default, only an approved difference is accepted, and it is pinned
  exactly. Every other difference still fails.
- The frozen originals are the HEAD versions; their sha256 are recorded below.

**Results, per gate** (local, GCC 13). The originals were at 51f15127.

| Gate | Cause | Correction | Result | Original sha256 (first 16 hex) |
| --- | --- | --- | --- | --- |
| test-v410-object-member-guard, -callback-overlay-guard, -glob-relative-guard, -glob-prefix-guard, test-v48-callback-guard, test-v49-glob-key-guard | They count legacy internals (object-member scans, callback overlays and the prepared cache, glob paths and keys). Under the VM default their programs never reach those paths, so the counters are 0. | `NIFT_VM=0` in the six recipes (Makefile comment at `OBJECT_LOOKUP_GUARD`). Each fails without it and passes with it. | 6 PASS | Makefile recipes |
| test-v410-native-dispatch-parity | The 15 recorded results are legacy's. Under the VM default, 5 are refused before execution. | Exact on the legacy channel. Under the VM default, each case must equal the recorded result or be a refusal before execution: exit 1, no stdout, and the refusal's own lines. | PASS: 10 equal, 5 refused | `568024dcddc42199` |
| test-cp19-shared-eval | `NIFT_VM=require` refusals used to name `NIFT_VM=require`. The VM-default wording (0503062d) gives the refusal and "no part of it ran". | Assert exit 1, no stdout, `not compiled for the VM (`, and the note. | PASS | eval_entry.py `3310af1d4d5b7d48` |
| test-cp19-expression-development; test-cp19-r1 (its review matrix) | A function value: legacy prints its internal callable tag; the VM gives `eval: opaque runtime value is not serializable` (exit 2). This is the approved B4 behaviour (b4-dispatch.md 6, owner: no internal tag). | Accepted only where legacy's output holds the callable tag and the VM's is exactly that error, with the same effects (marker). | PASS (165 observations; 135-observation matrix) | expressions_complete.py `5082c6ab4897a13a`, check_review_matrix.py `86eb03f06ad23438` |
| test-rt-parser, test-rt-parser-asan | Since a5179308 (CP23), rt parses the removed call form `await(...)` as a call of `await`, and the compiler refuses it with legacy's message. The unit expected `Opaque [legacy-runtime-error]`. The corpus's legacy runs and the negative gate's legacy oracle ran the VM default. | The unit expects the call shape. The paren oracle and the negative gate run legacy (`NIFT_VM=0`). The negative gate accepts a regular `await(...)` only when the VM default refuses it before running, with that message. | test-rt-parser PASS (unit; negative gate 0 failures; paren oracle 0 divergent); test-rt-parser-asan PASS | parser_unit.cpp `6bf018d25a957e1c`, rt_parser_corpus.py `ed209468bb440232`, rt_parser_negative.py `5407ec38d1a5fa00` |
| test-runtime2-diagnostics | The baseline corpus is "what legacy prints today", but it ran the VM default (31 FAIL). | Without `--vm`, the runner sets `NIFT_VM=0`. | baseline 89 cases, 0 fail; changes/old 34, 0 fail; changes/new 34, 0 fail; codes_check 0 problems | run.sh `5b7e1ac692e57c0f` |
| test-runtime2-vm-corpus | The goldens are legacy's (124 FAIL lines under the VM default). | `NIFT_VM=0`. `NIFT_VM_CORPUS_EXPECT=r2` still runs the default engine against the intended Runtime 2.0 goldens. | PASS, 180 scripts | vm_corpus.sh `7b3cb3cfad6bb2ae` |
| test-runtime2-prevalidate-gate | | fixed in C73 | | |
| test-rt-builtins-probe | | fixed in C68 | | |

- The approved differences used are exactly two: the VM-default refusal before execution, and a
  function value having no shown form. No unapproved behavioural difference turned up in these
  gates.
- No production change: `nift` is f063f1a9….

## 81. Owner 13:26, item 1: the module-collision expectation (C93, tests)

- `tests/runtime2/vm_modules.py` still expected the pre-B4 reason `import-collision` for the d041
  fixture `export_clash` and for the probe `export_collision`.
- Since 6f6a790d (B4 7) the VM gives the approved B4 C1 behaviour (compatibility.md; coordinator
  2026-11-06), `import-scope:collision`: legacy's exact error line, rejected before running.
  - Legacy prints `m runs`, then `import: export collides with existing binding: mf`, exit 1.
  - The VM default prints the same error line and the note that no part of the program ran.
- **Change.** Both expectations are now `import-scope:collision`. For that outcome the check also
  runs the VM default and requires:
  - no VM or legacy execution in its statistics;
  - no stdout and legacy's exit status;
  - legacy's `error:` line as the first stderr line.
- **Result.** `vm_modules.py --gen 100`: PASS, 60 programs plus 100 random module programs, all on
  the VM.
- vm_modules.py original sha256 `03e631e5db9b94a4…`. Production unchanged: f063f1a9….

## 82. Owner 13:26, item 2: the relative-import fixtures use `fn(...)` (C94, tests)

- **What was wrong.** `test-v46-relative-imports` failed on plain `@fn(...)` declarations in its
  fixtures (`fn-flags`), so the fixtures never reached what they test. They were written in a
  syntax D-047 #1 and D-048 #1 make invalid in script context.
- **Migration** (D-048 #4).
  - Every plain `@fn(` that opens a declaration in a script-context fixture is now `fn(`: 23 in
    `v44_relative_import_ownership_smoke.sh` and 9 in `v46_import_worker_ownership_smoke.sh`.
  - Flagged forms (`@fn[async]`) are not plain `@fn` and are unchanged.
  - The two helpers imported from a template (`templates/…/helper.f`, template context) keep their
    `@fn`.
  - Behaviour before and after: both smokes PASS on legacy (`NIFT_VM=0`) before and after.
- **Legacy is not changed:** the owner declined the legacy-side rejection. Legacy accepting plain
  `@fn` in a script is therefore recorded as an explicit legacy/VM difference in
  docs/runtime-2/known-limitations.md (section 87). The smoke pins it at its start:
  - the VM default refuses `@fn(v()) { return 3 }` with `fn-flags` before running anything;
  - `NIFT_VM=0` prints `3`.
- **Status after the migration.** Under the VM default the smokes now reach their import checks.
  - The v44 smoke stops at a package entry's delayed import inside a function (`import-nested`).
    That is a named deferral (D-065, section 87).
  - The v46 smoke stops at `coro-module-init-task` (D-064 #2, keep refused).
  - So test-v46-relative-imports still fails, now on those two refusals rather than on fixture
    syntax.
- Original sha256: v44 smoke `596c91c6038e1958…`, v46 smoke `a1e44df7ce6fb4b2…`. Production
  unchanged: f063f1a9….

## 83. Owner 13:26, item 3: the const element-assignment check (C95, tests)

- `v44_element_assignment_smoke.sh` required `const a := [1, 2]; a[0] = 9` to fail. The approved
  binding-only `const` (owner Q2 option B) allows the element write.
- **Change.** The check now requires the write to succeed and print `9`. A new check requires
  rebinding the const (`a = [3]`) to fail.
  - Under the VM default the write prints `9`, and the rebinding is refused (`declare-const`).
  - Legacy keeps its old `const` (`cannot assign to const binding: a`). That is the approved
    difference, so this check is for the default engine.
- The smoke was also restructured for item 4 (section 84). Original sha256 `2d572fbd91df889e…`.

## 84. Owner 13:26, item 4: nine smokes no longer abort silently (C96, tests)

- Each of these smokes died at `out=$(…)` (or a bare `[[ … ]]`/`grep`) under `set -e` when a run
  was refused, so the failing check had no FAIL line and every later check was hidden. Each failing
  check now prints a FAIL line naming it.
- **Report every check, exit non-zero at the end:**
  - `v44_ast_expression_smoke.sh` (`run_script` returns `exit N: <first line>`);
  - `v44_element_assignment_smoke.sh` (one program per check);
  - `v44_root_path_corruption_reproducers.sh` (and its PASS line now comes last);
  - `v46_output_smoke.sh`.
- **Name the check, exit non-zero at it:** `v48_bytes_index_parity.sh`, `v48_comparison_parity.sh`,
  `v48_callable_parity.sh`, `v48_map_set_index_smoke.sh` and `v48_prepared_collection_parity.sh`.
  Their `ok()` used to die on a refused run before comparing.
- **What they report now under the VM default** (each passes on legacy, `NIFT_VM=0`, except as
  noted in section 83):
  - ast: `big-int equality` (`prepared-unpreparable`);
  - element: object member, nested grid and compound element assignment (`index-target`,
    `index-assign-target`, `compound-target`);
  - root path: `destruct` (`declare-pattern`), `structarr`, `mapstruct` (`context:struct-new`) and
    `escaped-ref` (`index-target`);
  - v46 output: all four checks (`callable:err`);
  - v48: bytes (`builtin-bytes:bytes`), callable (`prepared-decl-type`), map-set (`callable:sorted_map`
    after the item 5 refusals), prepared collection (`postfix-spelling`).

  All of these are owner production items or inventory rows (section 86).
- Original sha256:

  | Smoke | sha256 (first 16 hex) |
  | --- | --- |
  | v44 ast | `2a87b033088c4e23` |
  | v44 element | `2d572fbd91df889e` |
  | v44 root path | `87312bde8c2a98c9` |
  | v48 bytes | `fc04ef8bcdfa80b0` |
  | v48 comparison | `fbc364119bb87257` |
  | v48 callable | `eb970f17e5ae970d` |
  | v48 map-set | `8b78c4f431751a1e` |
  | v48 prepared collection | `823e1b8890f310a1` |
  | v46 output | `0c476605fa6859b6` |

## 85. Owner 13:26, item 5: gates expect the four already-approved refusals (C97, tests)

- **Helpers.** `tests/runtime2/approved_refusal.py` and `approved_refusal.sh` accept only the
  approved reasons:
  - `fn-name-reserved` (D-009 #3);
  - `free-name` (owner ruling 2026-10-29);
  - `fn-flags` (D-047 #1 / D-048 #1);
  - `collection-mutable-key` (D-NEXT-refsem #6).
- **What they require under the VM default:**
  - exactly that reason, `not compiled for the VM (<reason>)`;
  - exit 1, no stdout, and the "no part of it ran" note;
  - a statistics row with that reason and no VM or legacy execution, so nothing ran before it.
- The recorded oracle of each case is still checked, on the legacy channel (`NIFT_VM=0`).
- **The gates** (from GATES.tsv):

  | Gate | Approved refusal | Cases |
  | --- | --- | --- |
  | test-v48-map-set-index | `collection-mutable-key` | its two marked/reference key cases. The gate still fails afterwards, on `callable:sorted_map`/`sorted_set` (not approved; inventory row PENDING-OWNER). |
  | test-v410-filesystem-recipe-parity | `fn-name-reserved` | the seven `stat` rows (files must be untouched by the refused run). The gate still fails afterwards, on `move`/`copy` rows (`builtin-lazy-args:move`, `builtin-arity:copy`, …) and on the pure-string rows (`postfix-spelling`); none is approved. |
  | test-v43-language (`tests/script_import_syntax.sh`) | `fn-name-reserved` in a session | `fn(import(x))`: the session refuses that first statement before running it ("this statement was not executed"), and legacy keeps the original check. |
  | test-v410-sort-factory-parity | `free-name` | `dynamic-capture-dependency` |
  | test-v45-concurrency, test-v46-relative-imports | `fn-flags` | the plain `@fn` was fixture syntax (section 82). The relative-import smoke pins the refusal itself. test-v45-concurrency's other scripts pass. |

- **Found, not changed (stopped on these items, for the owner):**
  - **Unapproved behavioural difference** in test-v410-sort-factory-parity, case
    `named-precedence`: `fn(x()) { return 8 }; f := x => x; print(type(f(3)))`. Legacy prints
    `function` (the named function wins over the lambda parameter), the VM prints `int` (the
    parameter). No approval was found. The gate stops there.
  - **Unapproved refusal** in `tests/script_import_syntax.sh` (test-v43-language). A malformed
    bare import (`import(` with no `)`) is refused as `opaque:template-text`, where legacy reports
    `import has no matching ')'` at the import (`malformed.f:2:5`). The check that follows aborts
    there.

## 86. Owner 13:26, item 6: the CP25-R inventory has a row and a status for every seen reason (C98, inventory)

- **Missing rows.**
  - Sources searched for refusal reasons seen on the release gates:
    - the candidate's Linode gate logs (`not compiled for the VM (…)` and session refusals, 79
      distinct reasons);
    - the B25–B27 scope list;
    - today's local reruns.
  - Of the 95 reasons seen, **30 had no row** (exact, or a `prefix:*` family row). The scope's 11
    are all among them.
  - All 30 are added. Each row's legacy column names what the gate log shows, or says it was not
    recorded there. Its evidence column names the gate.
- **A `status` column on every row.** The four values:
  - `approved`: an owner or coordinator ruling cited in `approval`;
  - `deferred-named`: D-065, section 87;
  - `implement`: approved for implementation before release, from the owner's production list of
    13:26;
  - `PENDING-OWNER`.
- **How existing rows were mapped:**
  - the five named deferrals → `deferred-named`;
  - the four refusals of section 85 → `approved`;
  - the production list → `implement`. Those reasons: index-target, index-assign-target,
    compound-target, type:element-assignment, struct-field-init, context:struct-new,
    prepared-unpreparable, declare-pattern, the bytes reasons, warn, err, postfix-spelling,
    path-in-call-argument, binding-name-reserved, import-collision, read and number-literal.
  - an `approval` citing a ruling (not "pending", "release approval pending" or "implementation
    pending") → `approved`;
  - everything else → `PENDING-OWNER`. A row never implies approval: an earlier `pending`
    approval, a class H tooling row, and "owner ruling … release approval pending" all count as
    PENDING-OWNER.
- **Totals:** 241 rows. approved 54, deferred-named 17, implement 24, PENDING-OWNER 146.
- **`inventory.py report --check`** reports G1 and the release state separately.
  - It fails (exit 1) on an unclassified reason, an unknown class, or a status outside the four.
  - Otherwise it prints `G1 PASS` with the count per status, the `deferred-named` and `implement`
    reasons, and `BLOCKED: N PENDING-OWNER reason(s)` with their names.
  - It then exits 3 while any seen reason is PENDING-OWNER. A G1 pass is never read as a release
    pass.
  - On the local inventory (`.build/cp25r-inventory`, collected from this candidate): G1 PASS with
    157 reasons seen (approved 11, deferred-named 17, implement 19, PENDING-OWNER 110), exit 3.
  - The 30 new rows are mostly from other gates, so they are not all in this inventory's 157.
  - `make cp25r-inventory` (collect plus check) is the Linode run.
- **Newly found, PENDING-OWNER (25):**
  - binding-shadows-fn;
  - builtin-arity:copy, builtin-arity:move, builtin-lazy-args:move;
  - builtin-arity:sha256, builtin-arity:sha256_file;
  - builtin-lazy-args:setenv, builtin-lazy-args:substr, builtin-lazy-args:validate,
    builtin-receiver:substr;
  - callable:assert, callable:len, callable:nosuch, method:nosuch;
  - callable:module_path, callable:sorted_map, callable:sorted_set;
  - context:number-to-int, context:object-length;
  - for-capture-vanishing, host-handle-flow;
  - method:sort (the comparator profile; seen on the Linode GCC 15.2 run);
  - nesting-limit, text-type-name, type:>.
- **Newly found, implement (5):** builtin-bytes:secure_random_bytes, callable:secure_random_bytes,
  number-literal, prepared-unpreparable, type:element-assignment.
- The full PENDING-OWNER list (146, the 25 above included) is the `status` column of
  `tests/runtime2/cp25/classification.tsv`.

## 87. Owner 13:26, item 7: the named deferrals are recorded and gated (C99, docs and tests)

- **decisions.md D-065** (2026-11-10) records the five deferrals the owner approved by name:
  - delayed imports inside functions;
  - `run()` and `cmd()`, keeping the earlier release-scope decision, which had no decisions.md
    entry until now;
  - VM-native FFI;
  - VM-native timers;
  - `project_root()`, with D-053.

  Each is stated as a VM limitation: legacy (`NIFT_VM=0`) keeps running it, and FFI keeps working
  on legacy and non-VM paths.
- **`docs/runtime-2/known-limitations.md`** (new) covers:
  - how a refusal behaves;
  - the named deferrals, with their reasons and what still runs them;
  - the four approved refusals, including plain `@fn`: the explicit legacy/VM difference (legacy
    accepts it; the owner declined the legacy-side rejection);
  - the approved semantic differences, which it summarises and links to compatibility.md as the
    authority;
  - the inventory statuses.
- **The CP25-R inventory:** 17 rows have status `deferred-named` (section 86):
  - import-nested;
  - callable:run, callable:cmd, method:run, method:pipe;
  - callable:ffi_open, ffi_call, ffi_callback, ffi_close;
  - callable:timer and the timer methods;
  - callable:project_root.
- **The gate:** `make test-runtime2-named-deferrals` (`tests/runtime2/named_deferrals.py`, on the
  `test-rt-vm` edge). For each construct it requires, under the VM default:
  - exit 1, no stdout, `not compiled for the VM (<reason>)` and the "no part of it ran" note;
  - statistics with that reason and no VM or legacy execution (no silent fallback);
  - the same result on a second run (deterministic).

  It also requires the legacy channel to run the construct. Result: PASS, 6 constructs.
- Every deferred construct is currently refused cleanly. None needed reporting.

## 88. Review of C83–C99, P1-2 and L-1: no inventory row is approved without an approval (C100, inventory and docs)

- **The review's finding** (REVIEW-C83-C99, P1-2). Four rows had status `approved` although their
  citations do not approve the refusal. known-limitations.md repeated one of them as an "approved
  refusal".
- **Now PENDING-OWNER, with the reason in the `approval` cell (the old text is kept after "Was:"):**
  - **`fn-name-reserved`.**
    - D-009 #3 decides that lexical bindings and user functions take precedence over built-ins:
      the opposite of the refusal. The refusal therefore defers D-009 #3; it does not implement it.
    - It is also not the owner's 13:26 "reserved-name check". The B25–B27 scope analysis that the
      13:26 list answers names that item `binding-name-reserved` ("small (raise at point)",
      implement), already status `implement`. It lists `fn-name-reserved` separately, as the D-009
      #3 case.
    - So the row is PENDING-OWNER, not `implement`.
  - **`free-name`:** "release approval pending". There is no free-name ruling text in decisions.md;
    D-009 #8 covers only free-name warnings.
  - **`fn-flags`:** D-047 #1 and D-048 #1 approve rejecting plain `@fn` in both engines. Legacy keeps
    accepting it (the owner declined the legacy side), so the VM-only refusal is not what was
    approved.
  - **`unresolved-name:legacy-binding`:** its remaining members are pending.
- **L-1: a `retired` status.** The six resolved or no-longer-emitted rows (type:print, aliasing,
  reference-guard:*, struct-no-method, type:for-source, coro-module) are now `retired`, so they no
  longer count as approved.
  - `inventory.py` accepts `retired`.
  - `--check` blocks (exit 3) if a retired reason is seen again.
- **New rows (P1-1):** `callable:to_string` and `callable:unknown`, both PENDING-OWNER. They are
  seen in test-v410-native-dispatch-parity and had no row.
- **docs/runtime-2/known-limitations.md:**
  - "Approved refusals" now lists only `collection-mutable-key` (D-NEXT-refsem #6, keys D6).
  - A new section, "Refusals awaiting an owner ruling", gives the three others, each with why it is
    not approved.
- **Status counts:**

  | Status | Rows |
  | --- | --- |
  | approved | 44 |
  | deferred-named | 17 |
  | implement | 24 |
  | PENDING-OWNER | 152 |
  | retired | 6 |

  The total is 243 rows.
  - On the local inventory: G1 PASS, 157 seen (approved 8, deferred-named 17, implement 19,
    PENDING-OWNER 113, retired 0), so `--check` exits 3.
- No production change: `nift` is f063f1a9….

## 89. Review of C83–C99, P1-1, P2-1 and P2-2: a refusal is accepted only when pinned and approved (C101, tests)

- **The shared rule** (`tests/runtime2/approved_refusal.py` and `approved_refusal.sh`). A gate may
  count a VM-default refusal as expected only if all of these hold:
  - the gate pins that case's reason itself; it is never read from the VM's output;
  - the reason's CP25-R status is `approved` or `deferred-named`. The status is read from
    classification.tsv (exact row, else the `prefix:*` family row), never hard-coded;
  - the program is refused with exactly that reason before anything runs: exit 1, no stdout, the
    note, and statistics with 0 VM and 0 legacy executions;
  - the legacy channel (`NIFT_VM=0`) still gives the recorded result.

  A PENDING-OWNER, implement or retired reason, a reason with no row, an unpinned refused case, or a
  different reason fails, naming the case and the reason. The hard-coded `APPROVED` table is gone.
- **P1-1:**
  - `tests/v410_native_dispatch_parity.py` pins five refusals:
    - `callable:len` (two cases);
    - `callable:to_string`;
    - `callable:unknown`;
    - `static-error:arity`.

    The last four reasons are PENDING-OWNER (rows added in C100), so the gate now **fails on all five
    cases**. That is expected until the owner rules; it was not softened.
  - `tests/runtime2/cp19/eval_entry.py` pins the seven `NIFT_VM=require` units:
    - `unresolved-name`;
    - `opaque:legacy-runtime-error` (two);
    - `builtin-bytes:bytes` (three);
    - `callable:timer`.

    `callable:timer` is deferred-named. The others are PENDING-OWNER or implement, so
    test-cp19-shared-eval now **fails on six units**, naming each one.
  - `tests/script_import_syntax.sh` (test-v43-language) checks fn-name-reserved's status the same
    way, and now fails: the status is PENDING-OWNER (C100).
- **P2-1:**
  - `tests/v410_filesystem_recipe_parity.py` pins `fn-name-reserved` for its seven stat cases.
  - `tests/v410_sort_factory_parity.py` pins `free-name` for `dynamic-capture-dependency`.
  - Any other refused case fails, whatever its reason. Both gates now fail on these pinned reasons,
    which are PENDING-OWNER since C100.
- **P2-2: the legacy oracle is mandatory.**
  - The shell helper fails without its sixth argument (the legacy stdout), and requires exit 0 with
    exactly that output.
  - `check()` runs the legacy channel itself. It requires a recorded `(exit, stdout[, stderr])` or a
    judging function, and fails without one.
- **Negative controls** (`make test-runtime2-approved-refusal-selftest`, on the test-rt-vm edge,
  `tests/runtime2/approved_refusal_selftest.sh`):
  - Shell, 6 controls:
    - an approved reason with an oracle passes;
    - a missing oracle, a wrong oracle, a PENDING-OWNER reason (`callable:len`), a reason with no
      row, and another reason than the refusal all fail.
  - Python, 11 controls:
    - the positive control and a pinned accepted reason pass;
    - a PENDING-OWNER reason, an unknown reason, an implement reason, a missing oracle, a wrong
      oracle, a wrong pinned reason and a program that runs all fail;
    - a case with no pin refused with an **approved** reason (`collection-mutable-key`) fails. This
      is the control for a running case becoming refused;
    - a case refused with an approved reason other than its pin fails.

  PASS.
- **Remaining callers:**
  - `v48_map_set_index_smoke.sh` still accepts its two `collection-mutable-key` cases (approved,
    D-NEXT-refsem #6), with legacy exact.
  - The relative-import smoke now fails at its `fn-flags` pin (PENDING-OWNER).
- No production change: `nift` is f063f1a9….

## 90. Review of C83–C99, the remaining Lows (C102, tests and docs)

(L-1, the `retired` status, is in section 88.)

- **L-3: the allowlist is pinned by line number as well.** `no_handle_strings.py` now keys its one
  exception by path, line number and exact stripped text: `src/rt/render.cpp:11`.
  - The self-test has 12 cases. The new one: the identical line at another line of the same file is
    flagged.
  - A line that moves becomes an unmatched entry, which is itself a problem.
  - `src/rt`: 0 problems, one `allowed src/rt/render.cpp:11` line. `make test-rt-handles` PASS.
- **L-4: the witness's process group is killed on the cap.** `unwind_mutants.py` `execute()` starts
  each witness in its own session (`start_new_session=True`). On a timeout it sends `os.killpg`
  SIGKILL to that group only; the group id is the child's own pid, a group the harness started.
  Nothing is found by name, and pkill and killall are not used.
  - Control: a child that starts a 300 s grandchild, run with a 2 s cap, returns 124 with its
    output, and the grandchild is gone.
  - `make test-runtime2-cp17-unwind-mutants`: PASS, 32 mutants, 0 failures, 197 s.
- **L-2: named_deferrals is Linux-only, and says so.** The libc lookup was not made portable. On a
  non-Linux platform the script prints `SKIP named deferrals: Linux only …` and exits 0. Its
  docstring and known-limitations.md say the gate is certified on the Linux host only.
  `make test-runtime2-named-deferrals`: PASS, 6 constructs.
  - Both new SKIP lines are in `tests/runtime2/skip_phrases.tsv`: the printed one as `detected`, the
    docstring line as `justified`. The certification guard's phrase inventory requires that.
- No production change: `nift` is f063f1a9….

## 91. Review of C100–C102, P1-1 and P2-2: filesystem-recipe and sort-factory report every failure (C103, tests and inventory)

- **Collect, then exit.** `tests/v410_filesystem_recipe_parity.py` and `tests/v410_sort_factory_parity.py`
  no longer stop at their first failure. Each failing case prints a `FAIL <case>:` line with its
  reason and status, or its difference. Each gate then lists them all and exits 1.
- **Pins.** Every refused case is pinned to its actual reason:
  - **filesystem**:
    - `fn-name-reserved` ×7;
    - `builtin-lazy-args:move` ×13, `builtin-arity:move` ×2;
    - `builtin-arity:copy` ×13, `spread` ×1;
    - pure cases: `postfix-spelling` ×49, `context:assignment-error`, `builtin-arity:split`,
      `builtin-arity:last`, `unresolved-name`, `opaque:legacy-runtime-error`.
  - **sort-factory**:
    - `free-name`, `for-capture-vanishing` ×2, `callable:timer`, `import-name-clash`,
      `method:sort_by`, `type:member`.

  A pin is still accepted only by its inventory status, so each case keeps failing on it.
  `unused-timer-observable` (`callable:timer`, deferred-named) is now accepted, with legacy exact.
- **New inventory rows, all PENDING-OWNER:**
  - `builtin-arity:split`: legacy `split: expected one delimiter`;
  - `builtin-arity:last`: legacy `last: expected no arguments`;
  - `import-name-clash`: it also appears in vm_modules' `module_private` probe.

  Status counts, 246 rows:

  | Status | Rows |
  | --- | --- |
  | approved | 44 |
  | deferred-named | 17 |
  | implement | 24 |
  | PENDING-OWNER | 155 |
  | retired | 6 |
- **`tests/runtime2/cp25/pending_differences.tsv`** (new) lists each unapproved behavioural
  difference, with its gate, case, program, VM and legacy output, and status PENDING-OWNER:
  - `copy-wrong-arity`: VM `1`, legacy `1`, `2` (legacy's loop path evaluates the argument twice);
  - `named-precedence`: VM `int`, legacy `function`.

  The gates read it only to label the case. A listed case still fails. known-limitations.md
  references it under "Refusals awaiting an owner ruling".
- **filesystem-recipe**: FAIL, 91 cases.

  | Group | Cases | Reason | Status |
  | --- | --- | --- | --- |
  | stat-prepared, stat-compatibility, stat-wrong-arity, stat-wrong-type, stat-first-error, named-stat-prepared, named-stat-compatibility | 7 | `fn-name-reserved` | PENDING-OWNER |
  | move-order, move-argument-mutates-source, move-first-source-error, move-first-destination-error, move-cwd-boundary, move-unmatched-source, move-array-source, move-bad-source-array, move-second-destination-error, move-second-source-error, move-loop-location, move-index-probe-order, move-root-path-replacement | 13 | `builtin-lazy-args:move` | PENDING-OWNER |
  | move-wrong-arity, move-spread | 2 | `builtin-arity:move` | PENDING-OWNER |
  | copy-order, copy-argument-mutates-source, copy-first-source-error, copy-first-destination-error, copy-cwd-boundary, copy-unmatched-source, copy-array-source, copy-bad-source-array, copy-second-destination-error, copy-second-source-error, copy-loop-location, copy-index-probe-order, copy-root-path-replacement | 13 | `builtin-arity:copy` | PENDING-OWNER |
  | copy-spread | 1 | `spread` | PENDING-OWNER |
  | copy-wrong-arity | 1 | the unapproved difference above | PENDING-OWNER |
  | pure #0–47 and #57 | 49 | `postfix-spelling` | implement |
  | pure #48 | 1 | `context:assignment-error` | PENDING-OWNER |
  | pure #52 | 1 | `builtin-arity:split` | PENDING-OWNER |
  | pure #53 | 1 | `builtin-arity:last` | PENDING-OWNER |
  | pure #54 | 1 | `unresolved-name` | PENDING-OWNER |
  | pure #56 | 1 | `opaque:legacy-runtime-error` | PENDING-OWNER |
- **sort-factory**: FAIL, 7 cases.

  | Case | Reason | Status |
  | --- | --- | --- |
  | dynamic-capture-dependency | `free-name` | PENDING-OWNER |
  | named-precedence | the unapproved difference above | PENDING-OWNER |
  | root-path-growth | `for-capture-vanishing` | PENDING-OWNER |
  | location-parent-growth-sort | `for-capture-vanishing` | PENDING-OWNER |
  | module-ownership | `import-name-clash` | PENDING-OWNER |
  | nested-returned-factory | `method:sort_by` | PENDING-OWNER |
  | module-definition-error | `type:member` | PENDING-OWNER |
- No production change: `nift` is f063f1a9….

## 92. Review of C100–C102, P2-1 and the Lows: no smoke aborts silently; error oracles; the killpg limit (C104, tests and inventory)

- **Rule applied.** Every touched smoke reports each failing check on a `FAIL <check>:` line. A
  refused run is reported with its reason and inventory status. The smoke goes on and exits
  non-zero at the end. A VM-default refusal is accepted only for a check that pins it through
  `approved_refusal`: status approved or deferred-named, legacy oracle mandatory.
- **`tests/v44_relative_import_ownership_smoke.sh`.**
  - It has new `vm_ok` and `vm_err` checks.
  - The package checks pin `import-nested` (deferred-named, D-065 1) and are accepted, with legacy
    exact. These are:
    - the package ownership output;
    - child export leak;
    - traversal;
    - symlink escape.

    The last three have legacy error oracles: exit 1, no stdout, and their message.
  - Result: FAIL, 1 case. `plain @fn in a script` is refused `fn-flags` (PENDING-OWNER). The direct,
    missing-sibling, cycle and template checks pass.
- **`tests/script_import_syntax.sh`.**
  - It has named `ok` and `err` checks. The REPL status check no longer exits early.
  - The malformed import pins `opaque:template-text`. The four non-string imports pin
    `import-dynamic`: same-line, comment-line, bare and `@import` (the old position-only greps hid
    that).
  - Result: FAIL, 6 cases.

    | Case | Reason | Status |
    | --- | --- | --- |
    | REPL fn named import | `fn-name-reserved` | PENDING-OWNER |
    | bare malformed import | `opaque:template-text` | PENDING-OWNER |
    | same-line non-string import | `import-dynamic` | PENDING-OWNER |
    | same-line comment import | `import-dynamic` | PENDING-OWNER |
    | non-string bare import | `import-dynamic` | PENDING-OWNER |
    | non-string `@import` | `import-dynamic` | PENDING-OWNER |
- **Sweep of the smokes C96–C101 touched:**
  - The five v48 smokes no longer stop at their first failure:
    - `ok` and `err` record the failure;
    - a refused run is named with its reason and status;
    - the silent template-site `build` now has a FAIL line.
  - `tests/v46_import_worker_ownership_smoke.sh` (C94) gets named checks. Its refusals are pinned:
    - `coro-module-init-task` (approved, D-064 #2);
    - `coro-module-capture` (approved, D-064 #3);
    - `import-nested` ×3 (deferred-named).
  - The v44 ast, element and root-path smokes and v46 output were already collecting; they are
    unchanged.
  - The legacy channel (`NIFT_VM=0`) passes every v48 smoke.
  - Results, each FAIL:

    | Smoke | Cases | Failing cases (reason, status) |
    | --- | --- | --- |
    | v48 bytes-index | 13 | every case, incl. 2 error checks (`builtin-bytes:bytes`, implement) |
    | v48 comparison | 11 | 3 `builtin-bytes:bytes` (implement); 2 `binding-name-reserved` (implement); 4 `opaque:irregular-legacy-parse` (PENDING-OWNER); 2 `type:<` error checks (PENDING-OWNER) |
    | v48 callable | 2 | `prepared-decl-type`; `static-error:arity` error check (both PENDING-OWNER) |
    | v48 map-set | 3 | 2 `callable:sorted_map`, 1 `callable:sorted_set` (PENDING-OWNER) |
    | v48 prepared-collection | 4 | 2 `postfix-spelling` (implement); 2 error checks, `builtin-arity:contains` and `builtin-arity:set` (no row before this commit, now PENDING-OWNER) |
    | v46 worker-ownership | 2 | `compound-target` (implement); module-private async, refused `import-collision` (implement) |
- **New inventory rows:** `builtin-arity:contains` and `builtin-arity:set`, both PENDING-OWNER.
  Counts, 248 rows:

  | Status | Rows |
  | --- | --- |
  | approved | 44 |
  | deferred-named | 17 |
  | implement | 24 |
  | PENDING-OWNER | 157 |
  | retired | 6 |
- **L-1: the shell helper takes an error oracle.**
  `approved_refusal BIN NAME REASON DECISION FILE LEGACY-STDOUT [LEGACY-EXIT [LEGACY-STDERR]]`.
  - Legacy's stdout must match exactly, its exit must equal LEGACY-EXIT (default 0), and its stderr
    must contain LEGACY-STDERR, or be empty when none is given.
  - Four new self-test controls:
    - an error oracle passes;
    - a wrong legacy exit fails;
    - missing stderr text fails;
    - a legacy error without an error oracle fails.

    That makes 10 shell controls plus 11 Python: PASS.
  - The helper now resolves its Python path absolutely, so a caller may `cd` first.
- **L-2: the killpg limit is documented, not closed.** `unwind_mutants.execute()` records it:
  - A descendant that calls `setsid()` leaves the witness's process group and is not reached.
  - No witness starts one: the unit and persist witnesses are single processes, and the vm
    witness's programs start no process.
  - A survivor cannot change a verdict: no kill is counted without the witness text.

  `make test-runtime2-cp17-unwind-mutants`: PASS, 32 mutants, 202 s.
- No production change: `nift` is f063f1a9….

## 93. Review of cf833f5a, Low: the drain after the cap is bounded (C105, tests)

- **The finding.** In `unwind_mutants.execute()`, the `communicate()` that ran after the
  process-group kill had no timeout. A descendant that had left the group (one that called
  `setsid()`) and held the pipes open could therefore hang the harness.
- **Fix.** That drain now has its own bound (`ESCAPE_DRAIN_SECONDS`, 10 s). If it times out:
  - the harness closes both pipes and reaps the child;
  - `execute()` returns the timed-out value `ESCAPED`;
  - the mutant loop reports `FAIL <mutant>: descendant escaped the witness's process group …
    (a harness error, never a kill)`;
  - a control that escapes fails as well.
- **Control.** A child that starts `setsid sleep 30` and waits, run with a 2 s cap and a 2 s drain,
  returns `ESCAPED` after 4.0 s, and its output so far is kept. The control then killed the escaped
  sleep by its exact PID. A child whose grandchild stays in the group still returns `True` (a
  normal timeout).
- **Result.** `make test-runtime2-cp17-unwind-mutants`: PASS, 32 mutants.
- No production change: `nift` is f063f1a9….

## 94. Production checkpoint 1, the assignment family: inventory and scope (ruled: option C, section 95)

No production change: `nift` is f063f1a9…. Probes: `scratchpad/cp1/` (VM default and
`NIFT_VM=0`, this binary).

- **Refusal reasons covering the family** (all status `implement`, owner 13:26):
  - `index-target` (compiler_rules.cpp `element_target_ok`): a key that is not a plain name or an
    unsigned integer literal, so `o["k"] = v` and `o["new"] = v`;
  - `index-assign-target` (compiler_assign.cpp, Index branch): a receiver that is not a plain
    name, so `grid[1][0] = 7`, `o["k"][1] = 9`, `g[r][1] = …` in loops;
  - `type:element-assignment` (same branch): a receiver not proved an array, or a key not proved a
    number, so `o[k] = v` with a variable key;
  - `compound-target` (compiler_assign.cpp `compound_assign`): any non-name target, so
    `a[0] += 10`, `o["k"] += 10`, `g[1][0] += 7`, and the same for `-= *= /= %=`.
- **In scope (proposed):**
  - element store on an array or object receiver, with a key that is a plain name, an unsigned
    integer literal, or a string literal without `.`;
  - the same at nesting depth 2 and 3, arrays and objects mixed (`g[i][j]`, `o["k"][1]`,
    `o["a"]["b"]`), at top level, in functions, closures, loops, and sessions;
  - compound `+= -= *= /= %=` on those targets.
- **Out of scope (unchanged, still refused):**
  - `o.k = v` and `o.k += v`: legacy's error "member assignment requires a struct instance"
    (`static-error:member-assign`, PENDING-OWNER);
  - a target whose text contains `.` (`o["a.b"] = 1`, `a[o.k] = 1`, `a[1.0] = 1`): legacy reads
    it as a member path and fails (the `index-target` F1 half; the row stays, narrowed);
  - computed keys (`a[i + 1][0] = v`) and increments on elements (`a[0]++`, postfix spelling);
  - struct field targets (struct rules), struct defaults, loop admission of struct
    construction, big integers, destructuring, bytes, imported-name rebinding.
- **Legacy behaviour observed** (all of it is what the VM would have to match, or differ from by
  an approved rule):

  | Program | Legacy |
  | --- | --- |
  | `o["k"] = 5`, `o["new"] = 1` | `{"k":5}`, `{"new":1}` |
  | `grid[1][0] = 7`, `o["k"][1] = 9` | `[[1,2],[7,4]]`, `{"k":[1,9]}` |
  | `a[0] += 10; a[1] -= 1`, `o["k"] += 10`, `g[1][0] += 7` | `[11,1]`, `{"k":11}`, `[[1,2],[10,4]]` |
  | `a[i] = f()`, `f` sets `i = 1` | `[1,5]`: value, then index (VM today: the same) |
  | `g[i][i] = f()`, `f` sets `i = 1` | `[[1,2],[3,5]]`: value, then both indices |
  | `o[k] = f()`, `f` sets `k = "b"` | `{"a":1,"b":5}`: value, then key |
  | `a[i] += f()`, `f` sets `i = 1` (`a = [1,2]`) | `[1,6]`: reads `a[0]`, stores at `a[1]` |
  | `g[5][0] = 7` | `1:15: index out of range: 5` |
  | `g[0][0] = 7` on `[1,2]` | `assignment index does not address an array or object: 0` |
  | `o["b"]["c"] = 7`, missing `b` | `object has no member: b` |
  | `a["x"] = 7` / `o[0] = 7` | `assignment index does not address an array or object: "x"` / `: 0` |
  | `a[0] += "x"` / `a[0] += 1` on `["s"]` | `arithmetic operators require numeric operands` |
  | `a[3] += 1` on `[1]` | `JSON array index 3 is out of range in 'a[3]'` (the read's text) |
  | `o["a"] %= 2` on `1.5` | `modulo requires integer-valued operands` |
  | `a[0] = a`, `g[0][0] = g`, `o["k"] = o` | accepted silently (a copy); VM: `runtime.reference_cycle` |
  | `for (e : arr) { e["k"] = 9 }` | `arr` unchanged (VM, B5: the element changes, as `e[0] = 7` today) |

- **Blocking question (evaluation order).** The checkpoint brief requires the key or index to be
  captured before the right-hand side, and not legacy's value-before-key order. The recorded
  contract says the opposite for the already-admitted form:
  - section 2's table and compatibility.md ("an element store evaluates its value before its
    index", `b[i] = g()` row: legacy-equal);
  - the `index-target` inventory row ("implement with that order", value before key);
  - the shipped VM: `a[i] = f()` gives `[1,5]`, legacy-equal, pinned by `b5/evalorder`.

  Three ways forward; the owner chooses:
  - **A.** Key first everywhere, `a[i] = v` included. One order, D2's left to right. This changes
    an admitted legacy-equal output: the `b5/evalorder` value-before-index fixtures become
    approved differences.
  - **B.** Key first only for the new forms. `a[i] = g()` and `a[i][j] = g()` would then use
    different orders.
  - **C.** Legacy's order for the new stores (value, then every key), matching section 2. Compound
    targets still cannot follow legacy (it reads at the old index and writes at the new one), so
    they would use D2's left to right.

## 95. Production checkpoint 1: the assignment family on the VM (C107)

**Ruling.** Owner 2026-11-10 16:35: option C of section 94. The brief's "capture the key before
the value" line is withdrawn (the 13:26 "value-before-key quirk" wording was wrong); H-A and D2 are
the contracts:
- a **store** (`a[i] = v`, `o["k"] = v`, `g[i][j] = v`): the receiver, then the value, then the
  keys in their order, then validate and commit. `a[i] = v` itself is unchanged (its code is not
  touched; `b5/evalorder` passes unchanged);
- a **compound update** (`a[i] op= v`, `o["k"] op= v`, nested): resolve the target and read its
  value before the right-hand side, then the rhs, then compute and commit, never looking the
  target up again. Legacy's old-key read / new-key write is the approved difference.

**Production change** (src/: 4 new files, 7 edited):
- `compiler_element_path.{h,cpp}` (new, `ElementPath`, a friend of `UnitCompiler`):
  - `form`: an Index target whose receiver is not a plain name, or whose receiver is proved an
    object, goes here; every other `name[key] = v` keeps the existing code;
  - admission: a plain-name root, at most 3 levels, each key a plain name, an unsigned integer
    literal or a string literal without `.`, brackets, quotes, backslash, braces, `$` or control
    characters (legacy reads other texts as member paths or fails: `o["a]"]` is "invalid
    assignment target", `o["a.b"]` a member-assignment error); each level's receiver proved an
    array with a number key or an object with a string key (`type:element-assignment`
    otherwise); for an update, number operands or two strings for `+=`
    (`type:compound-assignment` otherwise);
  - the root is read exactly as the existing store reads it (globals, cells, upvalues, loop
    bindings, sessions; a const scalar refused, a const array written, Q2 B) and retained when the
    rhs can rebind it; a captured loop binding commits its cached cell the same way;
  - a store: value, then per level the key and a `GetIndex` marked `Note::assign_path` (legacy's
    assignment wording), then the final key and `SetIndex`;
  - an update: per level the key (retained when the rhs can rebind it), the intermediate
    `GetIndex` and the element's `GetIndex` (legacy's read wording, the whole target text), then
    the rhs, the operation (`AddAssign`/`Sub`/`Mul`/`Div`/`Mod`, as `compound_op`) and `SetIndex`
    into the container read first at the key read first.
- `vm_element_path.{h,cpp}` (new):
  - `path_step`: an in-range element or a present member; otherwise `index out of range: <key
    text>` (integral), `assignment index does not address an array or object: <key text>`
    (negative or fractional), `object has no member: <key>`;
  - `set_member`: `mutation::object_put` (immutable, cycle, commit); a refused cycle is the
    catchable `runtime.reference_cycle`, as every store route.
- `vm_report.cpp`: a `GetIndex` that fails and whose note is an assignment path re-reports through
  `path_step` (the success path is unchanged); `vm_ops.cpp`: `set_index`'s existing kind-failure
  branch hands an object receiver to `set_member` (the array path is unchanged). `proto.h` (the note
  flag), `codegen.h` (`mark_assign_path`), `compiler_assign.cpp` (the two routes),
  `compiler_expr.h` (the friend), `compiler_coro.cpp` (CP22's future-flow admission keeps refusing
  a future stored through a nested path; it admitted only `name[i] = f()` before).
- Atomicity: every check (kinds at compile time; range, member, index form, arithmetic, cycle at
  run time) precedes the single commit, so a failed store or update writes nothing. A heap-budget
  stop after the commit is the existing exit-70 stop (nothing later runs).
- Not changed: the inference (it already modelled these stores, B4b), the session contracts, the
  struct, loop-snapshot and comparator-sort paths.

**Approved differences relied on** (compatibility.md, "The assignment family"; each pinned in
`tests/runtime2/assign_family.py`):
- D2 + owner 16:35: `a[i] += f()` where `f` changes `i`: legacy `[1,6]`, VM `[6,2]` (lambda, `fn`,
  nested, object key, loop);
- H-A D1.1 (store) and D2 (update): a `fn` callee that rebinds the receiver: legacy writes the new
  receiver, the VM the receiver read first;
- B5 (Q3 list): aliases, arguments and loop variables see the write;
- B5 cycle refusal: `g[0][0] = g`, `o["k"] = o`, `o["a"]["b"] = o`, `o["n"] = [o]`;
- Q2 B: element writes through a const array binding;
- D-018c: the canonical wording of a failing path store in loop code (`invalid index target` in
  legacy).

**Tests.**
- `tests/runtime2/assign_family.py` (`make test-runtime2-assign-family`): 75 cases on the VM and
  legacy, every failure collected. 46 legacy-equal; 29 pinned (each difference with its ruling,
  each refusal with legacy's output). Covered:
  - the three forms;
  - every compound operator on array, member and nested targets, and string `+=`;
  - order and counts (value then keys; the rhs once; an update's key, receiver and element before
    the rhs);
  - aliases: two names, an extracted inner array, an object alias, arguments, loop variables, a
    function global, closures;
  - Q2 B;
  - failures with legacy's message and position, in scripts, and in sessions with the targets
    printed unchanged afterwards: out of range, a missing intermediate, a negative or fractional
    index, division and modulo, overflow, a modulo of a fraction;
  - loop wording;
  - cycles through each store form, caught, nothing written;
  - a heap-budget stop on object stores (exit 70, nothing after it) with its no-budget control;
  - futures;
  - the refusals that stay.
- `tests/runtime2/assign_family_mutants.py` (`make test-runtime2-assign-family-mutants`): 13
  mutants, each an exact edit built by `sanctioned_mutant.py`, each caught by the named case:
  - store keys before value;
  - update rhs first (the capture left out), final key re-read, key not retained;
  - receiver not retained;
  - partial commit before the operation;
  - intermediate step left out;
  - step read wording;
  - step copies its container;
  - an object store that commits a refused cycle;
  - the object `SetIndex` route removed;
  - the missing-member wording;
  - futures admitted through a path.
- Updated expectations, each now the VM's result for an admitted form:
  - `b5/q3/r04_object_alias` (`{"k":2}`, B5) and seven `b5/u6` session goldens (two run with B5
    results; five now refuse `type:element-assignment` instead of `index-assign-target`, the
    session heap's kinds);
  - `b4b/codegen/object_element_store_expr.f` (renamed from `refused_…`: runs, legacy-equal);
  - `cp18/cow_semantics.py` (object-index, object-alias: 2/2, B5);
  - `vm_adversarial_gen3` (compound element targets compile and equal legacy);
  - `vm_subset`, `vm_subset_gen` and `vm_unit` (a computed-key compound and `o.k += 1` stay
    refused);
  - `b5/gen/gen_operators.py` (c_index, c_okey modelled; 10 `fn`-callee rows in the approved class
    `index-compound`);
  - `v44_root_path_corruption_reproducers.sh` escaped-ref (B5 Extraction: `1`, `2` on the VM,
    legacy's error pinned).
- New: VM corpus `cp11/assign_family.f` (vm) and `assign_family_error.f` (vm-error), with legacy
  goldens (they also reach vm_parity's rt-diff). Sharing oracle probes:
  - a member store and a nested update in a callee: approved;
  - an alias store (no call): not approved, nothing to rewrite;
  - the D2 order: not approved, check 2;
  - a member cycle through an argument: not approved, the reference cycle.

**Inventory** (`tests/runtime2/cp25/classification.tsv`):
- `index-assign-target`, `compound-target` and `type:element-assignment`: retired, citing C107.
  The first two were seen on the local release inventory before this commit and are not after
  it; `type:element-assignment` was seen in neither.
- `index-target`: narrowed and still `implement` (the computed and dotted keys:
  `fallback_index_assign_call`, `review3_f1_member_index`).
- Counts, 248 rows: approved 44, deferred-named 17, implement 21, PENDING-OWNER 157, retired 9.
  Local `inventory.py collect` + `report --check`: G1 PASS, 155 reasons seen, exit 3 (PENDING-OWNER,
  unchanged).

- `tests/runtime2/cycle_refusal_routes.py`: 3 new routes (a nested element store, an object
  member store, a nested member store): refused before the store, as the other 21.

**Cost** (callgrind Ir, deterministic): the existing store and read paths are unchanged in code;
a 100,000-iteration loop of `a[j] = n` and `s = s + a[j] + o["k"]` costs 191,138,565 Ir against
191,038,591 for a build with the original `vm_report.cpp`/`vm_ops.cpp` dispatch (+0.05 %). An
earlier draft that tested the note on every `GetIndex` and the receiver kind on every `SetIndex`
cost +0.68 % and was replaced.

**Known limits** (known-limitations.md):
- the kind inference merges a plain member store's value into every member of that object, so a
  later path through another member is refused;
- the other residual refusals of section 94.

## 96. Freeze of checkpoint 1 (C108, tests and docs)

The freeze gates found four things. Each was fixed test-side; there is no production change, and
`nift` is unchanged from C107.

- **`b4b_types.py` mutators check.** `vm_element_path.cpp` calls the mutation protocol. Its one
  use is SetIndex's `object_put`, which the inference already models (the Assign statement's
  `site_store`). It is added to the classified set.
- **Seed-1 tier (`vm_parity --rt-diff`), two items.**
  - (a) The extracted smoke `v44_root_path_corruption_reproducers/005-escaped` now runs with B5's
    Extraction result. That is an approved difference: owner 2026-11-07 15:25, compatibility.md
    "extracted array elements stay valid". It was not attributable by the sharing oracle, since
    the program has no call. It gets four exact rules (stdout, stderr, exit, combined) in
    `b5-refsem-vm.json`. Two things changed to make that possible:
    - the reproducer prints a first line, so that legacy's stdout is non-empty, which an exact
      rule needs;
    - the smoke pins both engines.
  - (b) Three exact rules for `v44_element_assignment_smoke/003-const` had been unused since C95.
    C95 rewrote that smoke without heredocs, so rt-diff no longer extracts the entry. The rules
    are moved to `b5-refsem-vm-retired.json` with the reason. The const element write stays
    pinned by the smoke and by `assign_family.py`.
- **Certification guard phrase inventory.** C107's mutant name `store-step-skipped` read as a
  skip message once the file was tracked: C107 fails the guard on that one line. It is renamed
  `store-step-omitted`; the guard passes at C108.
- **decisions.md D-066** records the owner's 16:35 ruling.

**Gate results** (local; GCC 13.3; `nift` 8195f1b3…):

| Gate | Result |
| --- | --- |
| certification guard, test-rt-lint, test-test-integrity | PASS (every commit) |
| `make test-rt-vm`: every prerequisite target | PASS (56 min) |
| `make test-rt-vm`: its own recipe, rerun after (a) and (b) | PASS: vm-unit 40,349 checks; seed-1 tier rt-diff 816 entries, 0 differing, 46 approved, 0 unused; vm_adversarial 39,612 programs, 0 non-approved; vm_subset, vm_subset_gen, vm_builtins and the other 22 commands |
| `make test-rt-vm-mutants` | PASS: `[0] parity` (gen 100 seed 3) and every suite pass unmutated; 12 of 12 mutants rejected; sharing-oracle selftest and the g00047 controls pass |
| `make test-rt-vm-sharing-oracle` | PASS, 0 of 41 checks failed (5 new probes) |
| `make test-runtime2-assign-family` / `-mutants` | PASS, 75 cases / 13 of 13 mutants caught |
| `make test-runtime2-b5` (production and guard-off) / `-b5-mutants` | PASS / all caught |
| cycle refusal routes | PASS, 24 routes |
| clone-summary negative | PASS (2 of 3 caught, as recorded in section 68) |
| `make test-rt-builtins-guards` (callgrind) | PASS, 35 workloads, 0 guard failures; not re-baselined |
| cp18 cow_semantics on `libnift_c.so` | PASS, 8 |
| b5 generator gate, D2 operator generator, refsem-a, b4b-types | PASS |

**Smokes.**
- `v44_element_assignment_smoke.sh` passes.
- `v44_root_path_corruption_reproducers.sh`: escaped-ref passes. The smoke still fails on
  `declare-pattern` and `context:struct-new`, families outside this checkpoint.
- `v46_import_worker_ownership_smoke.sh`: its `compound-target` case is gone. It still fails on
  `import-collision`.
- `v46_b4_cp7_worker_hardening.sh` stops at `import-nested` (D-065), as before.

**For Linode:**
- the sanitizer builds (ASan/UBSan nift and the san mutant configuration);
- TSan;
- the certification instance;
- `make cp25r-inventory` on the release toolchain;
- the GCC 15.2 comparator-sort profile.

## 97. Review of C105-C108 (ACCEPT-WITH-REPAIRS): repairs (C109, tests, docs, inventory)

No production change. `src/` is unchanged since 28a6368b, and `nift` is still 8195f1b3….

- **P2-1. D2 differences that were not pinned before.** Each is now an exact case in
  `assign_family.py`, with both engines pinned. Each has a row in compatibility.md and a sentence
  in known-limitations.md, citing D2 and the owner's 16:35 ruling.
  - The right-hand side replaces an intermediate container. The VM updates the container it read
    first; legacy updates the new one. This holds for every callee, so the earlier "lambda callee
    legacy-equal" wording is corrected: it is true only when the rhs rebinds the receiver.

    | Program | VM | Legacy |
    | --- | --- | --- |
    | `g[0][1] += f()`, where `f` (a `fn` or a lambda) sets `g[0] = [100,200]` | `g` `[[100,200],[3,4]]`, `row` `[1,7]` | `[[100,7],[3,4]]`, `[100,7]` |
    | `o["m"]["x"][0] += f()`, where `f` replaces `o["m"]` (`fn` and lambda) | `{"m":{"x":[50,60]}}` | `{"m":{"x":[6,60]}}` |

  - An inline assignment in the rhs rebinds the key. The VM uses the key it read first.

    | Program | VM | Legacy |
    | --- | --- | --- |
    | `a[i] += (i = 1)` | `[2,2]` | `[1,2]` |
    | `g[i][i] += (i = 1) + 10` | `[[12,2],[3,4]]` | `[[1,2],[3,12]]` |

  - These are owner items to confirm under D2.
- **P2-2. Forms that legacy rejects stay refused.** Each case pins the refusal reason, with
  legacy's error as the oracle:
  - `o := {"a": 1}; o[0] = 5` is refused with `type:element-assignment`. Legacy: `assignment index
    does not address an array or object: 0`.
  - `a[0] -= "y"`, `*=`, `/=` and `%=` on `["x"]` are refused with `type:compound-assignment`.
    Legacy: `arithmetic operators require numeric operands`.

  Two new sanctioned mutants each drop one of these checks. `sanctioned_mutant.py` fails the
  build when a mutant object is identical to production (the C75 check), so each built mutant
  differs from production. Both are caught by the case that names them:
  - `object-number-key-admitted` (an object level accepts any key kind) is caught by
    `refused_object_number_key`; the VM exits 70;
  - `string-compound-any-op` (two strings accepted for every operator) is caught by
    `refused_string_compound_sub`, and by 8 cases in all.

  `assign_family.py` now has 86 cases (46 legacy-equal, 40 pinned). The assignment-family mutants
  are 15 of 15 caught.
- **L-1.** `gen_operators` labels a row `index-compound` only when the callee is `fn`. The row
  count is pinned at 10.
- **L-2. Inventory.**
  - `index-assign-target` and `compound-target` go back to `implement`, narrowed:
    - `index-assign-target` remains for a fourth level (`g[0][0][0][0] = 5`, which legacy runs)
      and for a receiver that is not a plain name (`o.k[0] = 2`);
    - `compound-target` remains for `o.k += 1`.
  - `type:element-assignment` stays retired. Its row now notes the five b5/u6 session goldens that
    emit it. `tests/runtime2/b5` is not an inventory corpus. A fresh local `inventory.py collect`
    shows no sighting, and `--check` reports retired 0 seen.
  - Counts, 248 rows: approved 44, deferred-named 17, implement 23, PENDING-OWNER 157, retired 7.

## 98. Owner ruling 20:11: D2 coverage, the escaped-ref invariant, the inference limit (C110, tests and docs)

No production change. `src/` is unchanged since 28a6368b; `nift` is 8195f1b3….

**1. Approved differences.** Both compound differences of section 97 are approved under D2 + owner
16:35 Option C + owner 20:11 confirmation. They introduce no new rule:
- the right-hand side replaces an intermediate container: the VM writes to the target it captured
  first and never resolves the path again;
- the right-hand side changes an index: `a[i] += (i = 1)` gives `[2,2]` on the VM and `[1,2]` on
  legacy.

compatibility.md and known-limitations.md cite that ruling. These are no longer owner items.

**2. Coverage.**
- `assign_family.py` gains eight `d2_*` cases, 94 cases in all. Each pins the exact output of both
  engines.
- Each case shows:
  - **The rhs runs once:** `cnt` is 1 afterwards.
  - **The pre-rhs element is used:** the rhs first writes 50 (or 40) into the old element, and the
    result is computed from the value read before the rhs. In the array forms `row` ends as `[1,7]`
    and `a` as `[2,2]`. In the object form `kept` ends as `[6,60]`.
  - **No re-resolution:** the replacement container and the element at the new index are untouched;
    the captured container is the one written.
  - **Unrelated effects stay:** `other` becomes `[9]`.
- The forms covered: container replacement with a `fn` callee, with a lambda, and in the object
  form; index rebinding through `a[i]` and `g[i][i]`.
- Three failing variants run as sessions, so execution continues after the error and the targets
  can be printed:
  - `/=` by zero after the rhs replaced `g[0]`;
  - `%=` by zero after it replaced `o["m"]`;
  - an overflow after it rebound `i`.

  In each, the captured target is unchanged (`row` `[1,2]`, `kept` `[1,60]`, `a` unchanged), while
  the rhs's own effects stay (`cnt` 1, `other` `[9]`, the replacement container, `i`).
- Every case fails on exit 70, a signal, compiler-defect or internal text, or a nonzero
  dispatch-defect count.
- **Mutant `compound-path-reresolved`:** a sanctioned edit of `compiler_element_path.cpp` that
  re-reads the root, the intermediate steps and the final key after the rhs (legacy's model). It
  is caught by 18 cases: all five successful `d2_*` cases and every `order_compound_*` case. The
  three failing variants cannot tell it apart, because the commit fails before any write either
  way.
- Assignment-family mutants: 16 of 16 caught.

**3. The escaped-ref reproducer.**
- The `print("start")` line added in C108 keeps the reproducer's original invariant: memory safety,
  and exact pins on both engines.
- The smoke now also runs the original program without the print, written with `printf` so that
  rt-diff extracts it only once. It requires:
  - the VM output to equal the pinned output after its first line (`1`, `2`, exit 0, empty stderr);
  - legacy to fail with the same exit and `reference target no longer exists: c[0]` at the
    original line, 6:7.
- Both runs use the smoke's sanitizer settings. The receiver-rebinding cases (H-A/D2) and
  escaped-ref (B5) are confirmed by the owner.

**4. Inventory.**
- `index-assign-target` and `compound-target` stay restored to `implement`, narrowed (section 97).
- The refusal of `o["k"] = 5` followed by `o["m"]["x"] = 1` is documented as a checkpoint-1
  limitation of the incomplete kind inference, not a permanent one. That is in known-limitations.md
  and in the `type:element-assignment` row (still retired), so the rescan picks it up.
- No inference change.
- Counts, 248 rows: approved 44, deferred-named 17, implement 23, PENDING-OWNER 157, retired 7.

## 99. Post-checkpoint-1 inventory rescan (C111; repaired in C112-C116; owner rulings D-067 applied in C117 and D-068 in C122; reviews applied in C119-C121; slot 1 in C123-C124)

This section mirrors `docs/runtime-2/checkpoints/rescan-post-cp1.md`; subsection numbers follow that file.

Base: frozen checkpoint-1 candidate `0c66cad6` (owner 21:30: CHECKPOINT 1 ACCEPTED). C111 (`73886c2c`) was the
rescan; its independent review came back NOT CONFIRMED (no P1; classifications, evidence and gate counts wrong).
C112 repaired it; its confirmation review was again NOT CONFIRMED (a session-only retirement, rows missing for
run-time-built reason names), and C113 repaired that; C114 makes the last repairs and adds the decision shape
(section 1); C115 applies owner rulings 00:08 and 00:29 and the C114 confirmation review. nift sha256 `8195f1b34f91d018e336451153aa55bf0e05d590f9acfcd0dffa566ba35834e4` is unchanged and
`git diff 0c66cad6 -- src/` is empty. Only `tests/runtime2/cp25/classification.tsv` and the docs change. Nothing
is approved or deferred, and no rt-diff or pending rule is touched. **Every proposed disposition is a PROPOSAL;
the owner decides the families.**

### 1. How to read the 535 (decision shape; the 505 of C113 plus later rows)

Row count: C113 had 505 PENDING-OWNER + 23 implement = 528; C114 added `type:for-source` (restored) and
`coro-future-flow:Coalesce` (new): 530; C115 adds `fn-redeclared`, `too-many-functions`, `limit:invocation-arguments`,
`session:opaque` and `difference:import-depth` (535); C116 un-retires `free-name:legacy-binding`, `coro-collection-callback`,
`coro-main-cell`: 515 PENDING-OWNER + 23 implement = 538 rows in all. Every one carries exactly one outcome class at the start of its tsv disposition
(`outcome:i`, `outcome:ii-A`/`ii-B`/`ii-C`, `outcome:iii`, `outcome:D`, `outcome:E`), so this split can be recomputed from the tsv:

- **(i)** the VM refuses a program legacy runs to completion: a real capability gap;
- **(ii)** legacy also errors on the executed path; only the message, the position, the earlier output or the exit
  differ (because the VM refuses before running anything);
- **(iii)** session-only;
- **(iv)** the VM accepts where legacy errors: one row (`difference:import-depth`), plus documented differences;
- **D** derivative; **E** test or inventory artefact (E1/E2).

Method: the class comes from the row's recorded programs (tsv legacy column, C111-C114 probes, the rows.json evidence
runs), counting only a program on which the VM refuses with the row's own reason, and never a session's or an
uncalled body's legacy exit. Rows without such a program were probed in C114 (36 programs). This independent split
agrees with the confirmation review's approximate one; the differences are listed under the table.

| Family | (i) | (ii) | (iii) | (iv) | D | E | rows |
| --- | --- | --- | --- | --- | --- | --- | --- |
| builtin-arity | 5 | 123 | 0 | 0 | 0 | 1 | 129 |
| method (undefined or wrong-kind method) | 2 | 41 | 0 | 0 | 29 | 7 | 79 |
| builtin-receiver | 0 | 77 | 1 | 0 | 0 | 1 | 79 |
| callable (unbound callee) | 17 | 20 | 0 | 0 | 0 | 2 | 39 |
| coroutines and coro-future-flow | 12 | 8 | 0 | 0 | 0 | 9 | 29 |
| module-host-op (host op in a program with imports) | 27 | 0 | 0 | 0 | 0 | 0 | 27 |
| syntax forms | 19 | 5 | 0 | 0 | 0 | 0 | 24 |
| type:<op> and type checks | 18 | 0 | 0 | 0 | 2 | 0 | 20 |
| static checks, const, interpolation | 3 | 14 | 0 | 0 | 0 | 0 | 17 |
| structs | 12 | 3 | 1 | 0 | 1 | 0 | 17 |
| context and prepared code | 11 | 3 | 0 | 0 | 0 | 0 | 14 |
| built-in argument rules | 11 | 1 | 0 | 0 | 0 | 0 | 12 |
| other (singles) | 7 | 1 | 1 | 0 | 0 | 2 | 11 |
| dynamic scope and free names | 4 | 2 | 0 | 0 | 4 | 0 | 10 |
| expression and statement forms | 7 | 1 | 0 | 0 | 2 | 0 | 10 |
| exports and imports | 4 | 3 | 0 | 1 | 0 | 0 | 8 |
| bytes | 4 | 0 | 0 | 0 | 1 | 0 | 5 |
| variadics, spread, destructuring | 4 | 0 | 0 | 0 | 0 | 0 | 4 |
| capacity limits | 4 | 0 | 0 | 0 | 0 | 0 | 4 |
| **total** | **171** | **302** | **3** | **1** | **39** | **22** | **538** |

Changes in C119 (review of C117/C118, P1-2): 19 kind-based rows move from (ii) to (i), as **mixed** rows: the 16
`type:<op>` rows, `interpolation:non-scalar`, `coro-future-flow:ForBinding` and `coro-future-flow:Throw`. Each refuses
both a single-kind operand (legacy errors: still ii-B) and an inferred union (legacy runs it when the run-time kind fits:
a capability gap); both programs are in each row's evidence (dead-code-admission.md section 1.1). The table above shows
the C119 counts: (i) 171, (ii) 302. The C115-C118 counts in the rest of this file (152 and 321) are historical.

Changes in C115 (confirmation review P2-1, verified): 14 rows moved from (ii) to (i), because legacy runs the refused
construct and the earlier probe's legacy error came from something downstream: `builtin-arity:entries`/`values`,
`builtin-lazy-args:slice`, `builtin-default:get`, `builtin-literal-arg:validate`, `context:modulo-assignment`,
`context:number-to-int` (C114 was wrong: `x := 3.5` then `y := x * 2` / `y.to_int()` in a loop prints 7 in legacy),
`param-shadows-fn`, `struct-name`, `struct-nested`, `struct-redeclared`, `struct-member-clash` (legacy fails only when the
struct is used); and, from the C115 corpus measurement, `prepared-decl-type` (call-locals-2000/4000/8000, official-bfs
complete in legacy) and `opaque:legacy-runtime-error` (b4.f, s.f, names.f complete in legacy). This gives 149 rather
than the review's 147; with the three new reachable rows, (i) is 152. Every remaining (ii) probe was re-run with the
refused construct's result consumed by a plain assignment (`zz := X`, then `print("ok")`): legacy still errors at the
refused construct in every one (288 programs; the other rows' construct is not a printed expression and was checked
by position). `builtin-arity:contains`/`set` stay (ii); `keys`, `copy`, `move` are (i).

**(iv), the VM accepts where legacy errors:** `difference:import-depth` (an import chain 33 to 63 modules deep runs on the VM;
legacy fails at depth 33; C115 probe). Documented differences without rows: async self-recursion (CP23
qualification `qualified:legacy-await-chain-defect`, deep_chain.f); a struct method named like a built-in, under D-009 #3;
the B5 aliasing approvals (D-NEXT-refsem-b, Q3); B3's explicit `.stringify()` on a struct with its own method.

#### Governing policy for class (ii): early refusal (owner ruling 00:29)

Rejecting a program before execution whenever the VM can determine that it will fail is the established Runtime 2.0
design (D-013), not a new policy. Earlier error timing, and the absence of legacy's earlier output or side effects, are
approved consequences. Case B stays B; case C covers only internal failures and genuinely different failure categories.
The ruling applies only where the program contains an error that legacy also raises **at the same construct**; valid
constructs the VM refuses stay capability gaps (class (i)); early detection does not excuse a wrong position; internal
failures are not covered. The (ii) rows are tagged `outcome:ii-B, policy:early-refusal(D-013, owner 00:29)` and are
closed under this policy as admission-timing findings, without individual approvals.

**Why they stay PENDING-OWNER.** The policy covers programs that will fail. Every (ii) refusal also fires in code that
never runs, where legacy completes the program (below). Until the owner rules on that dead-code question (section
13.1), a (ii) row is not fully covered, so no status changes; nothing is approved.

| Case | Definition | Rows |
| --- | --- | --- |
| ii-A | both engines reject before any side effect; only the wording differs | 0 |
| ii-B | both reject the same error, the VM earlier (approved timing, 00:29) | 302 (321 before C119) |
| ii-C | internal failure or a genuinely different failure category | 0 (no exit 70, no defect text, exit 1 on both engines in every probe) |

| Contract | Rows | Meets the 00:29 rule? |
| --- | --- | --- |
| B-same-position | 290 (304 before C119) | yes for executed errors (wording and timing differ, as approved); the dead-code question remains |
| B-different-position | 12 (17 before C119) | only where the VM position is accepted: 7 marked "fix: take legacy's position" (implement, D-067 #1); 5 "VM more precise", approved in C119 after the position review (section 19); the other 5 verified positions are on rows that C119 moved to (i) mixed |

**Dead code (C115 measurement).** Each (ii) row's failing statements were wrapped in `if (false) { ... }` (declarations
kept at top level), then `print("n")`: legacy completes every such program. The refusal still fires in 316 of the 321
rows (313 with the generic wrapper, `jump-outside-loop`, `fn-called-before-declaration` and `opaque:struct-body` with a
targeted probe): a static check. It does not fire for `coro-module-before-import` (a flow-sensitive check, effectively
run-time ordering); `import-dynamic` in a dead block is refused as `import-nested` instead; for `import-limit`,
`import-opaque:struct-body` and `import-opaque:legacy-runtime-error` the failing code is module initialisation, which
always runs. **Corpus impact:** of the 94 corpus units (inventory corpora) whose first reason was a C114 (ii) reason, 7
complete in legacy, all refused with `prepared-decl-type` or `opaque:legacy-runtime-error`, which C115 moves to (i); for
the remaining 321 (ii) rows, 0 corpus programs contain such dead code.

**Reading it.** The real gaps (171 rows since C119, class (i); 152 before) fall into the families below; the 302 (ii) rows are governed by
the early-refusal policy, subject to the dead-code and position questions (section 13.1).

#### 1.1 Class (i) families ranked by ordinary-scripting impact

| # | Family | (i) rows | Gates (cases) | Representative | Legacy / VM | PROPOSAL |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Host operations in any program with an import | 27: `module-host-op:arch`, `module-host-op:cat`, `module-host-op:cd`, `module-host-op:cp`, `module-host-op:env`, `module-host-op:epoch`, `module-host-op:exists`, `module-host-op:file`, ... | none of the re-run gates | `import("./m.f")` / `print(pwd())` | legacy prints the directory; VM `module-host-op:pwd` | root cause and the reviewer's bounded repair: section 11 (PROPOSAL); raise site script_runner.cpp:325-339 with module_host_op.h:29 (`first_module_host_op`) |
| 2 | Postfix spellings | 1: `postfix-spelling` | test-v410-filesystem-recipe-parity 49, test-v48-prepared-collection-parity 2 | `m.set(k,v+1)` style spellings (filesystem-recipe) | legacy runs; VM `postfix-spelling` | admit the legacy spellings (implement row) |
| 3 | Nested and redeclared function declarations | 2: `fn-redeclared`, `statement:FnDecl` | none of the re-run gates | `fn(f()) { fn(g()) { return 1 } return g() }` / `print(f())` | legacy prints 1; VM `statement:FnDecl` | compile nested `fn` declarations (in functions, loops, ifs) as local function bindings |
| 4 | Safe navigation `?.` / `?[]` | 2: `expression:SafeIndex`, `expression:SafeMember` | none of the re-run gates | `o := {"a": 1}` / `print(o?.a)` | legacy prints 1; VM `expression:SafeMember` | compile SafeMember/SafeIndex as a null-checked member/index |
| 5 | Irregular legacy parses | 2: `import-opaque:irregular-legacy-parse`, `opaque:irregular-legacy-parse` | test-v48-comparison-parity 4 | `if({"a": 1} == {"a": 1}) { print("yes") }` | legacy prints yes; VM `opaque:irregular-legacy-parse` | structure the `if(` and `await x ?? y` spellings in the parser |
| 6 | `export(...)` in an entry file | 1: `statement:Export` | none of the re-run gates | `fn(twice(n)) { return n * 2 }` / `export(twice)` / `print(twice(2))` | legacy prints 4; VM `statement:Export` | compile `export` in an entry unit as a no-op |
| 7 | Structs | 12: `context:struct-new`, `export-struct`, `struct-after-statement`, `struct-assign`, `struct-field-init`, `struct-member-clash`, `struct-name`, `struct-nested`, ... | v44_root_path_corruption_reproducers 2 | `x := P()` / `x = P()` / `x.v = 3` / `print(x.v)` | legacy prints 3; VM `struct-assign` | the reviewer's grouping and CP2/CP2b split: section 12 (PROPOSAL) |
| 8 | Const objects | 1: `declare-const` | none of the re-run gates | `const o := {"k": 1}` / `print(o["k"])` | legacy prints 1; VM `declare-const` | admit const aggregates (binding-only const, B5 Q2) |
| 9 | Interpolation of unproven kinds | 1: `interpolation:unknown-type` | none of the re-run gates | `fn(f(x)) { return "v=$[x]" }` / `print(f(1))` / `print(f("s"))` | legacy prints v=1, v=s; VM `interpolation:unknown-type` | a run-time-kind render path |
| 10 | Variadics, spread, destructuring | 4: `declare-pattern`, `fn-param`, `lambda-variadic`, `spread` | v44_root_path_corruption_reproducers 1, test-v410-filesystem-recipe-parity 1 | `fn(sum(...xs)) {...}` / `print(sum(1, 2, 3))` | legacy prints 6; VM `fn-param` | one variadic/spread checkpoint, destructuring next to it |
| 11 | `a[0]++` and compound assignment as a value | 3: `coro-future-flow:Assign`, `expression:assignment-target`, `incdec-target` | none of the re-run gates | `a := [1]` / `print(a[0] += 5)` | legacy prints 6; VM `expression:assignment-target` | the owner-excluded checkpoint-1 forms, scheduled together (section 13.1 item 4) |
| 12 | Lazy built-in argument order | 7: `builtin-lazy-args:move`, `builtin-lazy-args:range`, `builtin-lazy-args:replace`, `builtin-lazy-args:setenv`, `builtin-lazy-args:slice`, `builtin-lazy-args:substr`, `builtin-lazy-args:validate` | test-v410-filesystem-recipe-parity 13, test-cp19-s1-dispatch 1 | `s := "abc"` / `t := "b"` / `print(s.replace("a", t + "x"))` | legacy prints bxbc; VM `builtin-lazy-args:replace` | one rule: evaluate built-in arguments in legacy's (lazy) order |
| 13 | Futures in ordinary expressions | 8: `coro-future-flow:Assign`, `coro-future-flow:Binary`, `coro-future-flow:Call`, `coro-future-flow:Coalesce`, `coro-future-flow:ExprStmt`, `coro-future-flow:Logical`, `coro-future-flow:Property`, `coro-future-flow:String` | none of the re-run gates | `f := af(1) ?? af(2)` / `print(await f)` | legacy prints 1; VM `coro-future-flow:Coalesce` | admit futures as ordinary values in these parent positions |
| 14 | Loop-context built-ins (`.length()`, `empty()`) | 2: `context:collection-empty`, `context:object-length` | none of the re-run gates | `m := map()` / a while loop with `print(m.empty())` | legacy prints false; VM `context:collection-empty` | implement legacy's loop-context results |

The other 80 class (i) rows (each a smaller family; gates and programs in their tsv rows): `alias-argument`, `binding-name-reserved`, `binding-shadows-fn`, `builtin-arity:copy`, `builtin-arity:entries`, `builtin-arity:keys`, `builtin-arity:move`, `builtin-arity:values`, `builtin-bytes:bytes`, `builtin-bytes:encode`, `builtin-bytes:open_bytes`, `builtin-bytes:secure_random_bytes`, `builtin-default:get`, `builtin-literal-arg:validate`, `builtin-presentation-chain`, `callable:build`, `callable:build_all`, `callable:build_names`, `callable:err`, `callable:minify`, `callable:module_path`, `callable:prique`, `callable:queue`, `callable:read`, `callable:secure_random_bytes`, `callable:sorted_map`, `callable:sorted_set`, `callable:status`, `callable:track`, `callable:tracked`, `callable:untrack`, `callable:warn`, `comparison-chain`, `context:modulo-assignment`, `context:negative-zero`, `context:number-to-int`, `context:parameter-type`, `context:string-append-type`, `coro-async-value`, `fn-flags`, `fn-name-reserved`, `for-capture-vanishing`, `free-name`, `free-name:dynamic-caller`, `host-literal-arg`, `import-collision`, `import-name-clash`, `import-opaque:nesting-limit`, `import-opaque:template-text`, `import-reinitialised`, `index-target`, `jump-not-last`, `limit:atoms`, `limit:invocation-arguments`, `limit:jump distance exceeds 16 bits`, `limit:too many registers`, `method:sort`, `method:sort_by`, `module-bare-return`, `nesting-limit`, `object-key`, `opaque:legacy-runtime-error`, `opaque:nesting-limit`, `opaque:template-text`, `param-name-reserved`, `param-shadows-fn`, `path-in-call-argument`, `path-in-call-argument:imported`, `postfix-receiver:member`, `prefix-not-precedence`, `prepared-decl-type`, `prepared-fn-unprepared`, `prepared-unpreparable`, `shebang-statement`, `statement:Command`, `string-literal`, `too-many-functions`, `type:element-assignment`, `type:for-source`, `unresolved-name`.

**(iii)**: `builtin-receiver:stringify` and `presentation-struct-method:*` (sessions only).

**Rows whose recorded evidence fails in legacy although the capability is a gap** (confirmation review): `declare-const`,
`struct-value`, `struct-field-init` and `module-host-op:*` now carry a program legacy runs (C112/C114 evidence; for
`module-host-op:stat/ls/cd/mkdir/make_dir/env/sleep/remove` the C113 programs failed only on the argument or on
printing the value; C114 replaced them). `builtin-arity:copy`/`move` cite the recorded oracle cases.

#### 1.3 Completeness boundary (owner 00:08)

Inventory identity: `0c66cad6` `src/` (unchanged at C115), nift sha256 `8195f1b34f91d018e336451153aa55bf0e05d590f9acfcd0dffa566ba35834e4`,
tsv revision = the C116 commit (tests/runtime2/cp25/classification.tsv, 607 rows).
The inventory is complete when it covers:

| Criterion | Coverage at C114 |
| --- | --- |
| every statically named VM refusal reason | C115 statement-level scan of 15 emit idioms in `src/rt` (sites / reason literals): `fail(` 595/271, `refuse(` 42/34, `reject(` 10/9, `add(` 120/7, `kinds_within(` 12/10, `write_refusal(` 7/6, `record_entry`/`record_linked(` 5/2, `reason =` 85/24, `reason_ =` 4/2, `why =` 12/3, `refusal =` 8/7, `stop =` 20/9, `return "..."` 328/21, `return {..., "..."}` 2/2, concatenation `"prefix:" +` 42/44: 255 distinct literals. Each has a row or a section 9 disposition. C115 found 13 the C112 literal scan missed: `fn-redeclared`, `too-many-functions`, `limit:invocation-arguments`, `session:opaque` (rows, reachable), `fn-shape`, `statement:Try`, `session:function-assignment`, `session:capture-contract` (rows, retired at creation; no context emits them, classify-all included), `free-name:legacy-binding` (D), `coro-collection-callback`, `coro-main-cell` (E2/F12; un-retired in C116 because classify-all emits the first two), and the non-reason strings `lambda:`/`named:`/`nift:` (render text), `opaque-limit`/`round-limit`/`unmaskable:` (inventory stop markers) |
| every run-time-generated family | 17 concatenated-name families (section 9.2), enumerated from their emit sites and probed with 1010 generated programs plus the C114 spellings; every reached suffix has an exact row or is absorbed by a stated family row (`method:*`, `callable:*`, `type:*` absorbs nothing producible) |
| every reachable refusal in the approved test corpus | fresh `inventory.py collect` (vm-corpus, semantics, rt-diff-corpus, conformance, cp410 probes, smoke heredocs): 155 reasons seen, G1 PASS (all classified), no retired reason seen |
| the contexts | script, module import, `nift -i` session, function called from a loop, uncalled function and lambda bodies, and classify-all (the inventory's own mode, C116), for every retired row; the first five for every unreachable claim (sections 9.1, 9.3, 9.4, 9.8) |
| async expressions | 25 coro-future-flow spellings (section 9.2; `:Binary`, `:Coalesce` found by them), coroutine-module forms, thread values |

After confirmation the inventory freezes for this campaign; later findings are tracked as separate issues.

#### 1.2 Method

- **Collections.** A fresh `inventory.py collect` at the tip (C112): 155 reasons seen. The C111 tip, pre-checkpoint-1
  mutant and 28a6368b comparisons stand (C111 section 1 findings are unchanged: only `compound-target`,
  `index-assign-target` and `index-target` moved because of C107).
- **F12 fix measured.** A scratch `sanctioned_mutant` build of the two-line inventory fix (section 10) was collected
  and compared with the tip, then deleted.
- **Every reclassification and evidence program was run** on the tip binary, VM default and `NIFT_VM=0`. The exact
  outputs below come from those runs (`NIFT_VM_STATS` for the reason).
- **Gates were re-run** (section 4); every count comes from those logs, one row per failing case.
- **D rows**: every D row was re-checked: either the reviewer's sample (review L-2) or a C112 root-free probe
  (section 3.5).
- **Static scan**: every reason literal in `src/rt` without a tsv row was triaged (section 9).
- **C113 four-way probe rule.** Before a row stays retired or a literal is called unreachable or not reached, it is
  probed in a script, a module import, a `nift -i` session and a function called from a loop (section 9.1, 9.3).
- **C113 dynamic families.** Every reason family whose name is built at run time was enumerated from its emit site
  and probed (1010 generated programs; section 9.2). A fresh C113 collect gives the same 155 seen reasons.

### 2. Summary

| Class | C111 | C112 | C113 | C114 | C115 | C116 | Meaning |
| --- | --- | --- | --- | --- | --- | --- | --- |
| E1 | 2 | 2 | 2 | 2 | 2 | 2 | eliminated for its evidence by checkpoint 1 (C107) |
| E2 | 19 | 18 (+2 from N) | 18 (+1 from N) | 18 (+1 from N) | 18 (+1 from N) | 18 (+3 outside the 180) | 16 F12, `method:join`, `method:increment` (row deleted in C113); `callable:prit`; C116: `coro-collection-callback`, `coro-main-cell` (F12) |
| A | 4 | 3 | 3 | 3 | 3 | 3 | covered by an approved ruling, not yet that behaviour |
| Q | 0 | 1 | 1 | 1 | 1 | 1 | owner question: `fn-flags` |
| D | 42 | 39 | 39 | 38 | 38 | 38 (+1 outside) | derivative (C116: `free-name:legacy-binding`), with a named root |
| G | 113 | 117 (+2) | 117 (+8) | 118 (+9) | 118 (+9) | 118 (+9) | genuine remaining capability |
| **classified** | **180** | **180** | **180** | **180** | **180** | **180** | = 157 PENDING-OWNER + 23 implement at 0c66cad6 |
| N | 45 | 58 | 340 | 341 | 346 | 346 | + `fn-redeclared`, `too-many-functions`, `limit:invocation-arguments`, `session:opaque`, `difference:import-depth` (C115) |

Rows by class at C116: E1 2, E2 20, A 3, Q 1, D 39, G 127, N 346 = 538 (= PENDING-OWNER 515 + implement 23). Row count
history: 528 at C113; +2 in C114 (`type:for-source` restored, `coro-future-flow:Coalesce` new) = 530; +5 in C115 = 535;
+3 in C116 (un-retired). Four C115 rows remain retired at creation (no context emits them, classify-all included).

| Status | 0c66cad6 | C111 | C112 | C113 | C114 | C115 | C116 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| approved | 44 | 44 | 44 | 44 | 44 | 44 | 44 |
| deferred-named | 17 | 17 | 17 | 17 | 17 | 17 | 17 |
| implement | 23 | 23 | 23 | 23 | 23 | 23 | 23 |
| PENDING-OWNER | 157 | 200 | 217 | 505 | 507 | 512 | 515 |
| retired | 7 | 9 | 9 | 5 | 4 | 11 | 8 |
| rows | 248 | 293 | 310 | 594 | 595 | 607 | 607 |

**C117 (owner rulings D-067):** approved 46, deferred-named 36, implement 76, PENDING-OWNER 443, retired 8; 609 rows
(+2 family rows `builtin-arity:*`, `builtin-receiver:*`). Section 18 lists the changes.

Inventory identity: `0c66cad6` `src/`, nift sha256 `8195f1b3...`, tsv revision = the C117 commit (C118 adds dead-code tags only).

#### 2.1 Per-row classes (C115)

- **E1 (2)**
  - assign (2): `compound-target`, `index-assign-target`
- **E2 (20)**
  - f12 (18): `builtin-arity:join`, `builtin-receiver:join`, `callable:thread`, `coro-blocking-host:exists`, `coro-blocking-host:file`, `coro-blocking-host:rm`, `coro-collection-callback`, `coro-main-cell`, `coro-observe:done`, `coro-observe:print`, `coro-observe:status`, `coro-struct`, `method:close`, `method:done`, `method:open`, `method:save`, `method:status`, `method:write`
  - f12+D (1): `method:join`
  - test-artefact (1): `callable:prit`
- **A (3)**
  - approved (3): `alias-argument`, `context:assignment-error`, `fn-name-reserved`
- **Q (1)**
  - owner-question (1): `fn-flags`
- **D (39)**
  - derivative (38): `assign-free-name:undeclared`, `builtin-bytes:decode`, `free-name:undeclared`, `method:array_value`, `method:await`, `method:call`, `method:call_helper`, `method:call_step`, `method:catch`, `method:command`, `method:count`, `method:decode`, `method:encode`, `method:error`, `method:filter`, `method:fixed`, `method:group_by`, `method:helper`, `method:hidden`, `method:m`, `method:map`, `method:public_helper`, `method:push`, `method:q`, `method:read`, `method:secret`, `method:stringify`, `method:sum`, `method:throw`, `method:to_global`, `method:to_string`, `method:trim`, `statement:Import`, `statement:TemplateStmt`, `struct-construction`, `type:index`, `type:push`, `unresolved-name:legacy-binding`
  - inventory refinement (1): `free-name:legacy-binding`
- **G (127)**
  - aliasing (1): `path-in-call-argument`
  - builtins (19): `builtin-arity:copy`, `builtin-arity:move`, `builtin-bytes:bytes`, `builtin-bytes:encode`, `builtin-bytes:open_bytes`, `builtin-bytes:secure_random_bytes`, `builtin-lazy-args:move`, `builtin-lazy-args:setenv`, `builtin-lazy-args:substr`, `builtin-lazy-args:validate`, `callable:err`, `callable:minify`, `callable:module_path`, `callable:read`, `callable:secure_random_bytes`, `callable:sorted_map`, `callable:sorted_set`, `callable:warn`, `statement:Command`
  - context (8): `context:assignment-type`, `context:number-to-int`, `context:object-length`, `jump-not-last`, `prepared-decl-type`, `prepared-fn-unprepared`, `prepared-unpreparable`, `string-arg-type`
  - coro (2): `coro-future-flow:Member`, `for-capture-vanishing`
  - cp24 (9): `callable:build`, `callable:build_all`, `callable:build_names`, `callable:inject`, `callable:status`, `callable:track`, `callable:tracked`, `callable:untrack`, `opaque:template-text`
  - dynscope (2): `free-name`, `free-name:dynamic-caller`
  - exports (8): `export-struct`, `import-collision`, `import-dynamic`, `import-name-clash`, `module-bare-return`, `module-host-op:cat`, `module-host-op:exists`, `statement:Export`
  - infer (4): `interpolation:unknown-type`, `method:sort_by`, `type:element-assignment`, `type:for-source`
  - portability (1): `method:sort`
  - static (34): `builtin-arity:contains`, `builtin-arity:last`, `builtin-arity:set`, `builtin-arity:sha256`, `builtin-arity:sha256_file`, `builtin-arity:split`, `builtin-receiver:stringify`, `builtin-receiver:substr`, `builtin-receiver:to_string`, `coro-async-value`, `coro-module-before-import`, `coro-module-private-async`, `declare-const`, `error-result-expression`, `host-handle-flow`, `jump-outside-loop`, `non-transferable`, `number-literal`, `opaque:legacy-runtime-error`, `opaque:struct-body`, `static-error:arity`, `static-error:member-assign`, `static-error:redeclared`, `static-error:struct-arity`, `struct-private`, `type:+`, `type:<`, `type:>`, `type:compound-assignment`, `type:increment`, `type:length`, `type:member`, `type:script-return`, `type:unary`
  - structs (6): `context:struct-new`, `presentation-struct-method:*`, `struct-after-statement`, `struct-field-init`, `struct-no-method`, `struct-value`
  - syntax (12): `binding-name-reserved`, `coro-future-flow:Assign`, `expression-statement`, `expression:assignment-target`, `incdec-target`, `index-target`, `nesting-limit`, `opaque:irregular-legacy-parse`, `param-name-reserved`, `postfix-spelling`, `shebang-statement`, `text-type-name`
  - undef (17): `assign-free-name`, `binding-shadows-fn`, `callable:*`, `callable:assert`, `callable:exit`, `callable:len`, `callable:nope`, `callable:nosuch`, `callable:to_string`, `callable:undefined_fn`, `callable:undefined_thing`, `callable:unknown`, `fn-called-before-declaration`, `method:*`, `method:nope`, `method:nosuch`, `unresolved-name`
  - variadic (4): `declare-pattern`, `fn-param`, `lambda-variadic`, `spread`
- **N (346)**
<details><summary>N rows by family</summary>

  - builtins (5): `builtin-lazy-args:range`, `builtin-lazy-args:replace`, `builtin-literal-arg:validate`, `builtin-presentation-chain`, `host-literal-arg`
  - context (7): `context:collection-empty`, `context:modulo-assignment`, `context:negative-zero`, `context:parameter-type`, `context:string-append-type`, `context:struct-field`, `for-missing-write`
  - dynamic: C115 (5): `difference:import-depth`, `fn-redeclared`, `limit:invocation-arguments`, `session:opaque`, `too-many-functions`
  - dynamic: builtin-arity (116): `builtin-arity:arch`, `builtin-arity:atomic<bool>`, `builtin-arity:atomic<int>`, `builtin-arity:attr_escape`, `builtin-arity:cancel`, `builtin-arity:cat`, `builtin-arity:cd`, `builtin-arity:ceil`, `builtin-arity:channel`, `builtin-arity:chunk`, `builtin-arity:close`, `builtin-arity:cp`, `builtin-arity:deepcopy`, `builtin-arity:drop`, `builtin-arity:empty`, `builtin-arity:ends_with`, `builtin-arity:entries`, `builtin-arity:env`, `builtin-arity:eof`, `builtin-arity:epoch`, `builtin-arity:exists`, `builtin-arity:first`, `builtin-arity:flatten`, `builtin-arity:floor`, `builtin-arity:flush`, `builtin-arity:from_entries`, `builtin-arity:get`, `builtin-arity:getenv`, `builtin-arity:hardware_concurrency`, `builtin-arity:has`, `builtin-arity:highlight`, `builtin-arity:html_escape`, `builtin-arity:indexOf`, `builtin-arity:index_of`, `builtin-arity:is_array`, `builtin-arity:is_bool`, `builtin-arity:is_bytes`, `builtin-arity:is_collection`, `builtin-arity:is_dir`, `builtin-arity:is_enum`, `builtin-arity:is_file`, `builtin-arity:is_float`, `builtin-arity:is_function`, `builtin-arity:is_int`, `builtin-arity:is_null`, `builtin-arity:is_number`, `builtin-arity:is_object`, `builtin-arity:is_string`, `builtin-arity:is_struct`, `builtin-arity:last_index_of`, `builtin-arity:length`, `builtin-arity:lock`, `builtin-arity:locked`, `builtin-arity:ls`, `builtin-arity:make_dir`, `builtin-arity:map`, `builtin-arity:max`, `builtin-arity:merge`, `builtin-arity:merge_deep`, `builtin-arity:min`, `builtin-arity:mkdir`, `builtin-arity:modified`, `builtin-arity:mutex`, `builtin-arity:mv`, `builtin-arity:omit`, `builtin-arity:open`, `builtin-arity:os`, `builtin-arity:path`, `builtin-arity:pick`, `builtin-arity:platform`, `builtin-arity:prettify`, `builtin-arity:pwd`, `builtin-arity:range`, `builtin-arity:read`, `builtin-arity:read_all`, `builtin-arity:read_line`, `builtin-arity:recv`, `builtin-arity:remove`, `builtin-arity:replace`, `builtin-arity:revert`, `builtin-arity:rm`, `builtin-arity:round`, `builtin-arity:same`, `builtin-arity:save`, `builtin-arity:seek`, `builtin-arity:send`, `builtin-arity:setenv`, `builtin-arity:size`, `builtin-arity:sleep`, `builtin-arity:slice`, `builtin-arity:stack`, `builtin-arity:starts_with`, `builtin-arity:stat`, `builtin-arity:substr`, `builtin-arity:sum`, `builtin-arity:take`, `builtin-arity:tell`, `builtin-arity:to_double`, `builtin-arity:to_int`, `builtin-arity:to_lower`, `builtin-arity:to_string`, `builtin-arity:to_upper`, `builtin-arity:touch`, `builtin-arity:trim_end`, `builtin-arity:trim_start`, `builtin-arity:try_lock`, `builtin-arity:type`, `builtin-arity:unique`, `builtin-arity:unlock`, `builtin-arity:unsetenv`, `builtin-arity:url_encode`, `builtin-arity:validate`, `builtin-arity:values`, `builtin-arity:which`, `builtin-arity:write`, `builtin-arity:write_line`
  - dynamic: builtin-default (1): `builtin-default:get`
  - dynamic: builtin-lazy-args (1): `builtin-lazy-args:slice`
  - dynamic: builtin-receiver (68): `builtin-receiver:abs`, `builtin-receiver:ceil`, `builtin-receiver:chunk`, `builtin-receiver:close`, `builtin-receiver:compare_exchange`, `builtin-receiver:contains`, `builtin-receiver:copy`, `builtin-receiver:decode`, `builtin-receiver:drop`, `builtin-receiver:encode`, `builtin-receiver:ends_with`, `builtin-receiver:entries`, `builtin-receiver:eof`, `builtin-receiver:exchange`, `builtin-receiver:exists`, `builtin-receiver:fetch_add`, `builtin-receiver:fetch_sub`, `builtin-receiver:flatten`, `builtin-receiver:floor`, `builtin-receiver:flush`, `builtin-receiver:from_entries`, `builtin-receiver:front`, `builtin-receiver:get`, `builtin-receiver:has`, `builtin-receiver:indexOf`, `builtin-receiver:last`, `builtin-receiver:last_index_of`, `builtin-receiver:load`, `builtin-receiver:lock`, `builtin-receiver:locked`, `builtin-receiver:max`, `builtin-receiver:merge`, `builtin-receiver:merge_deep`, `builtin-receiver:min`, `builtin-receiver:modified`, `builtin-receiver:move`, `builtin-receiver:omit`, `builtin-receiver:open`, `builtin-receiver:path`, `builtin-receiver:pick`, `builtin-receiver:read`, `builtin-receiver:read_all`, `builtin-receiver:read_line`, `builtin-receiver:remove`, `builtin-receiver:replace`, `builtin-receiver:revert`, `builtin-receiver:round`, `builtin-receiver:save`, `builtin-receiver:seek`, `builtin-receiver:set`, `builtin-receiver:slice`, `builtin-receiver:split`, `builtin-receiver:starts_with`, `builtin-receiver:store`, `builtin-receiver:take`, `builtin-receiver:tell`, `builtin-receiver:to_int`, `builtin-receiver:to_lower`, `builtin-receiver:top`, `builtin-receiver:trim`, `builtin-receiver:trim_end`, `builtin-receiver:trim_start`, `builtin-receiver:try_lock`, `builtin-receiver:unique`, `builtin-receiver:unlock`, `builtin-receiver:values`, `builtin-receiver:write`, `builtin-receiver:write_line`
  - dynamic: callable (10): `callable:build_repair`, `callable:ffi_buffer`, `callable:ffi_bytes`, `callable:ffi_sizeof`, `callable:ffi_snapshot_bytes`, `callable:ffi_struct`, `callable:package_path`, `callable:page`, `callable:prique`, `callable:queue`
  - dynamic: coro-future-flow (10): `coro-future-flow:Binary`, `coro-future-flow:Coalesce`, `coro-future-flow:CompoundAssign`, `coro-future-flow:ExprStmt`, `coro-future-flow:ForBinding`, `coro-future-flow:IncDec`, `coro-future-flow:Logical`, `coro-future-flow:Property`, `coro-future-flow:String`, `coro-future-flow:Throw`
  - dynamic: expression (2): `expression:SafeIndex`, `expression:SafeMember`
  - dynamic: four-way (6): `compound-operator`, `import-opaque:legacy-runtime-error`, `limit:atoms`, `limit:jump distance exceeds 16 bits`, `limit:too many registers`, `path-in-call-argument:imported`
  - dynamic: import-opaque (4): `import-opaque:irregular-legacy-parse`, `import-opaque:nesting-limit`, `import-opaque:struct-body`, `import-opaque:template-text`
  - dynamic: message (2): `async(callable, ...) was removed; declare an async function/lambda and call it directly`, `await(...) was removed; use 'await future' or 'await async_fn(args)'`
  - dynamic: method (37): `method:all`, `method:any`, `method:append`, `method:cat`, `method:clear`, `method:count_by`, `method:cwd`, `method:env`, `method:file`, `method:find`, `method:find_index`, `method:group_by_each`, `method:index_by`, `method:insert`, `method:insert_after`, `method:insert_before`, `method:max_by`, `method:min_by`, `method:mv`, `method:partition`, `method:pop`, `method:prepend`, `method:read_all_bytes`, `method:read_bytes`, `method:read_val`, `method:reduce`, `method:reset`, `method:reverse`, `method:running`, `method:splice`, `method:stat`, `method:stderr`, `method:stdin`, `method:stdout`, `method:unique_by`, `method:write_bytes`, `method:write_val`
  - dynamic: module-host-op (19): `module-host-op:arch`, `module-host-op:cd`, `module-host-op:env`, `module-host-op:epoch`, `module-host-op:hardware_concurrency`, `module-host-op:ls`, `module-host-op:make_dir`, `module-host-op:mkdir`, `module-host-op:open`, `module-host-op:os`, `module-host-op:platform`, `module-host-op:pwd`, `module-host-op:setenv`, `module-host-op:sha256_file`, `module-host-op:sleep`, `module-host-op:stat`, `module-host-op:touch`, `module-host-op:unsetenv`, `module-host-op:which`
  - dynamic: statement (1): `statement:FnDecl`
  - exports (8): `import-limit`, `import-reinitialised`, `module-host-op:cp`, `module-host-op:file`, `module-host-op:getenv`, `module-host-op:is_dir`, `module-host-op:is_file`, `module-host-op:remove`
  - infer (4): `builtin-arg-fault-context`, `interpolation:non-scalar`, `postfix-receiver:member`, `type:for-source-union`
  - static (25): `assign-catch-binding`, `await-outside-coroutine-unit`, `builtin-arity:abs`, `builtin-arity:keys`, `builtin-arity:stringify`, `builtin-arity:trim`, `builtin-receiver:empty`, `builtin-receiver:first`, `builtin-receiver:index_of`, `builtin-receiver:keys`, `builtin-receiver:sum`, `builtin-receiver:to_double`, `builtin-receiver:to_upper`, `coro-future-flow:Call`, `coro-future-flow:method`, `coro-thread-callee`, `method:replace_once`, `push-receiver`, `static-error:push-arity`, `type:%`, `type:*`, `type:-`, `type:/`, `type:<=`, `type:>=`
  - structs (7): `struct-assign`, `struct-member-clash`, `struct-name`, `struct-nested`, `struct-no-field`, `struct-redeclared`, `struct-shadows-fn`
  - syntax (6): `comparison-chain`, `object-key`, `opaque:nesting-limit`, `param-shadows-fn`, `prefix-not-precedence`, `string-literal`
  - undef (2): `callable:stringify`, `import-scope`

</details>


Root of each D row: section 3.5.

### 3. E1, E2, A, Q and D

#### 3.1 E1

`compound-target` and `index-assign-target`: their evidence runs since C107; on the tip corpus each is seen only as a
consequential reason. Residual forms (`o.k += 1`; a fourth level, `o.k[0] = v`) keep them implement.

#### 3.2 E2

- **F12 (16 rows).** classify-all compiles with default `CompileLimits` (`refusal_inventory.cpp:163`, no
  `cooperative_switches()`), script runs with the switches (`script_runner.cpp:310-311`). Measured with the fix built:
  all 16 are no longer seen. They stay PENDING-OWNER until the fix lands (retiring them now would trip `retired
  reason seen again`).
- **`method:join` (E2+D).** 40 classify-all sightings at the tip; 9 non-consequential remain with the fix, in
  `v44_automation_smoke` auto.f and auto2.f (`r.affected.join`, `tracked().join`, `status().join`: derivative of the
  CP24 rows `callable:build`, `callable:tracked`, `callable:status`) and `v44_cp3_variadic_lambdas_smoke` ok.f
  (`mapped.join` after `lambda-variadic`). A plain `xs.join(",")` and `.join` on a lambda's result run on the VM. Not
  retirable even with the fix.
- **`method:increment` (row deleted in C113).** Its evidence copy.f runs, but `x := 5` / `x.increment()` still emits the
  reason in all four contexts (section 9.3). inventory.py matches an exact row before `<prefix>:*` (`row_for`, line 194),
  so the retired row, not `method:*`, would have matched a sighting ("retired reason seen again"), contrary to C112's
  text. Retired means "resolved or not emitted any more" (status header, review L-1); an emitted reason cannot keep that
  status, so the row is deleted and the `method:*` family row (PENDING-OWNER) classifies it. No corpus program emits it
  (fresh C113 collect).
- **`callable:prit`**: a typo mutation inside test-rt-vm (an undefined callable); a test artefact, kept PENDING-OWNER
  like every undefined callable.
- **`builtin-receiver:stringify` (restored to PENDING-OWNER, G sessions, C113).** C112 retired it after probing scripts
  and a struct session only. `nift -i` with `f := (v) => v.stringify()` / `print(f({"a": 1}))` refuses with it; legacy
  prints `{"a":1}`; the same lambda runs in a script, a module and a loop-called function (section 9.3).
- **C111's retirement of `presentation-struct-method:*` was wrong**; it is restored as G (section 5.7).

#### 3.3 A (3 rows)

| Row | Ruling | Why still PENDING-OWNER |
| --- | --- | --- |
| `context:assignment-error` | D-009 #1 (owner 2026-10-17): one top-level wording in loops | the refusal defers implementing it. Gates: filesystem-recipe 1 case |
| `fn-name-reserved` | D-009 #3: user functions take precedence | the refusal defers that change. Gates: filesystem-recipe 7, sha256 4, script_import_syntax 1 |
| `alias-argument` | B5 shared references (D-NEXT-refsem-b, Q3 list) | aliasing is the approved semantics; the refusal predates B5 |

#### 3.4 Q: owner question (1 row)

`fn-flags`: D-047 #1 / D-048 #1 approve rejecting plain `@fn` before execution in **both** engines. The legacy side
was declined, and `known-limitations.md` itself says the VM-only refusal is not what was approved. Representative:
`@fn(v()) { return 3 }` / `print(v())`: legacy prints `3`, exit 0; VM `fn-flags`, exit 1. **Question:** does D-048's
rejection-before-execution cover a VM-only refusal now that the legacy change is declined?

#### 3.5 D (38 rows since C114): roots and the root-free check

| Rows | Root | Check |
| --- | --- | --- |
| `method:array_value`, `method:command`, `method:count`, `method:fixed`, `method:to_global` | fn-param / fn-flags (variadic struct methods, v44_struct_variadic_methods ok.f) | C112: the same struct with fixed parameters (and a parameter not named `args`, which is `param-name-reserved`) runs, legacy-equal |
| `method:stringify` | fn-param / builtin-arity:copy | review L-2 |
| `method:call` | fn-param (non-array-spread.f) | C112: `fn(call(xs))` with `a.call(x)` runs, legacy-equal |
| `method:hidden` | fn-param, then struct-private (private.f calls a private method from outside; legacy errors `private struct method`) | C112: without the variadic it refuses with `struct-private` (legacy's error), legal private use runs; its own reason never surfaces |
| `method:read`, `method:helper`, `method:call_helper` | struct-field-init | review L-2 |
| `method:call_step`, `method:public_helper` | struct-field-init (a lambda field in internal.f) | C112: the vault without lambda fields runs (10/11/2, legacy-equal); `public_helper` is itself a lambda field, so its only form is the root: a struct with just that field refuses with `struct-field-init` |
| `type:push` | struct-field-init / path-in-call-argument | review L-2 |
| `method:catch`, `method:error`, `method:throw` | fn-name-reserved (global `error`, `catch`, `throw` functions) and opaque:legacy-runtime-error (`throw (1)`) in names.f | C112: the struct with methods named error/catch/throw alone runs (14/15/16, legacy-equal) |
| `method:filter`, `method:group_by`, `method:map` | callable:inject | review L-2 |
| `method:m` | struct-after-statement | review L-2 |
| `method:push` | path-in-call-argument / struct-field-init | review L-2 |
| `method:q` | opaque:irregular-legacy-parse (`@struct`) then struct-field-init (a lambda field) | C112: without `@` it refuses with `struct-field-init`; as a plain method it runs, legacy-equal |
| `method:secret` | struct-private | review L-2 |
| `method:sum` | static-error:struct-arity | review L-2; C112: a struct method `sum` runs |
| `method:to_string` | callable:run / fn-flags / builtin-receiver:to_string | review L-2; C112: `x.to_string()` runs |
| `method:trim` | callable:run (deferred-named; `r.stdout.trim()`) | C112: `s.trim()` on a string runs, legacy-equal |
| `type:index` | prepared-decl-type / callable:inject | review L-2 |
| `method:await` | coro-future-flow:Member | review L-2 |
| `statement:Import`, `statement:TemplateStmt` | opaque:template-text (an `@script` template) | C112: the same import and the same try/catch in a plain script run, legacy-equal |
| `assign-free-name:undeclared` | assign-free-name (inventory refinement of it, refusal_inventory.cpp:69-71) | review L-2; C112: the root-free program reports `assign-free-name` |
| `free-name:undeclared` | free-name (inventory refinement, globals.cpp:179 via refusal_inventory.cpp) | C112: top level and loop variants both report `free-name`, never the sub-name |
| `unresolved-name:legacy-binding` | unresolved-name (inventory refinement for legacy special names, refusal_inventory.cpp:65-69); consequential only | C112: `print(content)` reports `unresolved-name`; tip sightings are all consequential |
| `text-type-name` | binding-name-reserved / fn-name-reserved (consequential only, gate logs) | **C114: not derivative**: `x := await` inside a lambda body emits it on its own, so it moved to G (section 9.4); C112 had probed `type(x) == int` only |
| `struct-construction` | context:struct-new | review L-2 |
| `builtin-bytes:decode`, `method:decode`, `method:encode` | builtin-bytes:encode | review L-2 |

Three C111 D rows failed this check and are now G: `struct-value` (section 5.7), `callable:undefined_thing` (5.2),
`coro-module-private-async` (5.1).

### 4. Gate re-runs (P2-4)

Each gate was run at the tip; every `FAIL` case line was read once (listing repeats and summary lines dropped).
Where a line carries no reason (one program's several failing checks), it inherits the reason of the program's
first line (marked *inherited*). Fail-fast gates stop at their first failing case and report 1.

#### test-v410-native-dispatch-parity: 5 failing case(s) (gate reports `5 of 15 case(s)`)

| Reason | Cases |
| --- | --- |
| `callable:len` | 2 |
| `callable:to_string` | 1 |
| `callable:unknown` | 1 |
| `static-error:arity` | 1 |
| **total** | **5** |

#### eval_entry: 6 failing case(s) (cp19 `eval_entry.py` (threaded run); gate reports `6 refusal(s) not accepted`)

| Reason | Cases |
| --- | --- |
| `builtin-bytes:bytes` | 3 |
| `opaque:legacy-runtime-error` | 2 |
| `unresolved-name` | 1 |
| **total** | **6** |

#### test-v410-filesystem-recipe-parity: 91 failing case(s) (gate reports `91 failing case(s)`; C111's counts were doubled (each case is listed twice in the log))

| Reason | Cases |
| --- | --- |
| `postfix-spelling` | 49 |
| `builtin-lazy-args:move` | 13 |
| `builtin-arity:copy` | 13 |
| `fn-name-reserved` | 7 |
| `builtin-arity:move` | 2 |
| pending difference (rt-diff/pending, not a refusal) | 1 |
| `spread` | 1 |
| `context:assignment-error` | 1 |
| `builtin-arity:split` | 1 |
| `builtin-arity:last` | 1 |
| `unresolved-name` | 1 |
| `opaque:legacy-runtime-error` | 1 |
| **total** | **91** |

<details><summary>Per case</summary>

- FAIL stat-prepared: refused with fn-name-reserved, whose inventory status is PENDING-OWNER (only app → `fn-name-reserved`
- FAIL stat-compatibility: refused with fn-name-reserved, whose inventory status is PENDING-OWNER (onl → `fn-name-reserved`
- FAIL stat-wrong-arity: refused with fn-name-reserved, whose inventory status is PENDING-OWNER (only  → `fn-name-reserved`
- FAIL stat-wrong-type: refused with fn-name-reserved, whose inventory status is PENDING-OWNER (only a → `fn-name-reserved`
- FAIL stat-first-error: refused with fn-name-reserved, whose inventory status is PENDING-OWNER (only  → `fn-name-reserved`
- FAIL named-stat-prepared: refused with fn-name-reserved, whose inventory status is PENDING-OWNER (on → `fn-name-reserved`
- FAIL named-stat-compatibility: refused with fn-name-reserved, whose inventory status is PENDING-OWNE → `fn-name-reserved`
- FAIL move-order: refused with builtin-lazy-args:move, whose inventory status is PENDING-OWNER (only  → `builtin-lazy-args:move`
- FAIL move-argument-mutates-source: refused with builtin-lazy-args:move, whose inventory status is PE → `builtin-lazy-args:move`
- FAIL move-first-source-error: refused with builtin-lazy-args:move, whose inventory status is PENDING → `builtin-lazy-args:move`
- FAIL move-first-destination-error: refused with builtin-lazy-args:move, whose inventory status is PE → `builtin-lazy-args:move`
- FAIL move-wrong-arity: refused with builtin-arity:move, whose inventory status is PENDING-OWNER (onl → `builtin-arity:move`
- FAIL move-cwd-boundary: refused with builtin-lazy-args:move, whose inventory status is PENDING-OWNER → `builtin-lazy-args:move`
- FAIL copy-order: refused with builtin-arity:copy, whose inventory status is PENDING-OWNER (only appr → `builtin-arity:copy`
- FAIL copy-argument-mutates-source: refused with builtin-arity:copy, whose inventory status is PENDIN → `builtin-arity:copy`
- FAIL copy-first-source-error: refused with builtin-arity:copy, whose inventory status is PENDING-OWN → `builtin-arity:copy`
- FAIL copy-first-destination-error: refused with builtin-arity:copy, whose inventory status is PENDIN → `builtin-arity:copy`
- FAIL copy-wrong-arity: VM {'exit': 0, 'stdout': '1\n', 'stderr': '', 'files': {'b': 'B', 'a': 'A', ' → `pending difference (rt-diff/pending, not a refusal)`
- FAIL copy-cwd-boundary: refused with builtin-arity:copy, whose inventory status is PENDING-OWNER (on → `builtin-arity:copy`
- FAIL move-unmatched-source: refused with builtin-lazy-args:move, whose inventory status is PENDING-O → `builtin-lazy-args:move`
- FAIL move-array-source: refused with builtin-lazy-args:move, whose inventory status is PENDING-OWNER → `builtin-lazy-args:move`
- FAIL move-bad-source-array: refused with builtin-lazy-args:move, whose inventory status is PENDING-O → `builtin-lazy-args:move`
- FAIL move-second-destination-error: refused with builtin-lazy-args:move, whose inventory status is P → `builtin-lazy-args:move`
- FAIL move-second-source-error: refused with builtin-lazy-args:move, whose inventory status is PENDIN → `builtin-lazy-args:move`
- FAIL move-spread: refused with builtin-arity:move, whose inventory status is PENDING-OWNER (only app → `builtin-arity:move`
- FAIL copy-unmatched-source: refused with builtin-arity:copy, whose inventory status is PENDING-OWNER → `builtin-arity:copy`
- FAIL copy-array-source: refused with builtin-arity:copy, whose inventory status is PENDING-OWNER (on → `builtin-arity:copy`
- FAIL copy-bad-source-array: refused with builtin-arity:copy, whose inventory status is PENDING-OWNER → `builtin-arity:copy`
- FAIL copy-second-destination-error: refused with builtin-arity:copy, whose inventory status is PENDI → `builtin-arity:copy`
- FAIL copy-second-source-error: refused with builtin-arity:copy, whose inventory status is PENDING-OW → `builtin-arity:copy`
- FAIL copy-spread: refused with spread, whose inventory status is PENDING-OWNER (only approved or def → `spread`
- FAIL move-loop-location: refused with builtin-lazy-args:move, whose inventory status is PENDING-OWNE → `builtin-lazy-args:move`
- FAIL move-index-probe-order: refused with builtin-lazy-args:move, whose inventory status is PENDING- → `builtin-lazy-args:move`
- FAIL move-root-path-replacement: refused with builtin-lazy-args:move, whose inventory status is PEND → `builtin-lazy-args:move`
- FAIL copy-loop-location: refused with builtin-arity:copy, whose inventory status is PENDING-OWNER (o → `builtin-arity:copy`
- FAIL copy-index-probe-order: refused with builtin-arity:copy, whose inventory status is PENDING-OWNE → `builtin-arity:copy`
- FAIL copy-root-path-replacement: refused with builtin-arity:copy, whose inventory status is PENDING- → `builtin-arity:copy`
- FAIL pure#0: refused with postfix-spelling, whose inventory status is implement (only approved or de → `postfix-spelling`
- FAIL pure#1: refused with postfix-spelling, whose inventory status is implement (only approved or de → `postfix-spelling`
- FAIL pure#2: refused with postfix-spelling, whose inventory status is implement (only approved or de → `postfix-spelling`
- FAIL pure#3: refused with postfix-spelling, whose inventory status is implement (only approved or de → `postfix-spelling`
- FAIL pure#4: refused with postfix-spelling, whose inventory status is implement (only approved or de → `postfix-spelling`
- FAIL pure#5: refused with postfix-spelling, whose inventory status is implement (only approved or de → `postfix-spelling`
- FAIL pure#6: refused with postfix-spelling, whose inventory status is implement (only approved or de → `postfix-spelling`
- FAIL pure#7: refused with postfix-spelling, whose inventory status is implement (only approved or de → `postfix-spelling`
- FAIL pure#8: refused with postfix-spelling, whose inventory status is implement (only approved or de → `postfix-spelling`
- FAIL pure#9: refused with postfix-spelling, whose inventory status is implement (only approved or de → `postfix-spelling`
- FAIL pure#10: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#11: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#12: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#13: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#14: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#15: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#16: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#17: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#18: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#19: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#20: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#21: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#22: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#23: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#24: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#25: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#26: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#27: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#28: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#29: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#30: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#31: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#32: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#33: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#34: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#35: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#36: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#37: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#38: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#39: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#40: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#41: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#42: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#43: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#44: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#45: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#46: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#47: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#48: refused with context:assignment-error, whose inventory status is PENDING-OWNER (only a → `context:assignment-error`
- FAIL pure#52: refused with builtin-arity:split, whose inventory status is PENDING-OWNER (only approv → `builtin-arity:split`
- FAIL pure#53: refused with builtin-arity:last, whose inventory status is PENDING-OWNER (only approve → `builtin-arity:last`
- FAIL pure#54: refused with unresolved-name, whose inventory status is PENDING-OWNER (only approved o → `unresolved-name`
- FAIL pure#56: refused with opaque:legacy-runtime-error, whose inventory status is PENDING-OWNER (onl → `opaque:legacy-runtime-error`
- FAIL pure#57: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`

</details>

#### test-v410-filesystem-recipe-guard: 1 failing case(s)

| Reason | Cases |
| --- | --- |
| `builtin-arity:copy` | 1 |
| **total** | **1** |

#### test-v410-sort-factory-parity: 7 failing case(s) (gate reports `7 failing case(s)`)

| Reason | Cases |
| --- | --- |
| `for-capture-vanishing` | 2 |
| `free-name` | 1 |
| pending difference (rt-diff/pending, not a refusal) | 1 |
| `import-name-clash` | 1 |
| `method:sort_by` | 1 |
| `type:member` | 1 |
| **total** | **7** |

<details><summary>Per case</summary>

- FAIL dynamic-capture-dependency: refused with free-name, whose inventory status is PENDING-OWNER (on → `free-name`
- FAIL named-precedence: VM (0, 'int\n', '') != recorded (0, 'function\n', '') (unapproved VM/legacy d → `pending difference (rt-diff/pending, not a refusal)`
- FAIL root-path-growth: refused with for-capture-vanishing, whose inventory status is PENDING-OWNER ( → `for-capture-vanishing`
- FAIL module-ownership: refused with import-name-clash, whose inventory status is PENDING-OWNER (only → `import-name-clash`
- FAIL nested-returned-factory: refused with method:sort_by, whose inventory status is PENDING-OWNER ( → `method:sort_by`
- FAIL location-parent-growth-sort: refused with for-capture-vanishing, whose inventory status is PEND → `for-capture-vanishing`
- FAIL module-definition-error: refused with type:member, whose inventory status is PENDING-OWNER (onl → `type:member`

</details>

#### script_import_syntax: 6 failing case(s)

| Reason | Cases |
| --- | --- |
| `import-dynamic` | 4 |
| `fn-name-reserved` | 1 |
| `opaque:template-text` | 1 |
| **total** | **6** |

#### v44_root_path_corruption_reproducers: 3 failing case(s)

| Reason | Cases |
| --- | --- |
| `context:struct-new` | 2 |
| `declare-pattern` | 1 |
| **total** | **3** |

#### v44_ast_expression_smoke: 1 failing case(s)

| Reason | Cases |
| --- | --- |
| `prepared-unpreparable` | 1 |
| **total** | **1** |

#### v44_ast_constant_fold_smoke: 0 failing case(s) (passes)

No failing case.

#### v44_element_assignment_smoke: 0 failing case(s) (passes (checkpoint 1))

No failing case.

#### v46_output_smoke: 4 failing case(s)

| Reason | Cases |
| --- | --- |
| `callable:err` | 4 |
| **total** | **4** |

#### test-v46-secure-random-cli: 1 failing case(s)

| Reason | Cases |
| --- | --- |
| `builtin-bytes:secure_random_bytes` | 1 |
| **total** | **1** |

#### test-v48-bytes-index-parity: 13 failing case(s)

| Reason | Cases |
| --- | --- |
| `builtin-bytes:bytes` | 13 |
| **total** | **13** |

#### test-v48-comparison-parity: 11 failing case(s)

| Reason | Cases |
| --- | --- |
| `opaque:irregular-legacy-parse` | 4 |
| `builtin-bytes:bytes` | 3 |
| `binding-name-reserved` | 2 |
| `type:<` | 2 |
| **total** | **11** |

<details><summary>Per case</summary>

- FAIL ordinary bytes equality: exit 1, refused with builtin-bytes:bytes (inventory status implement) → `builtin-bytes:bytes`
- FAIL direct if object ==: exit 1, refused with opaque:irregular-legacy-parse (inventory status PENDI → `opaque:irregular-legacy-parse`
- FAIL direct if object !=: exit 1, refused with opaque:irregular-legacy-parse (inventory status PENDI → `opaque:irregular-legacy-parse`
- FAIL direct if nested ==: exit 1, refused with opaque:irregular-legacy-parse (inventory status PENDI → `opaque:irregular-legacy-parse`
- FAIL direct if bytes ==: exit 1, refused with builtin-bytes:bytes (inventory status implement) → `builtin-bytes:bytes`
- FAIL direct if bytes !=: exit 1, refused with builtin-bytes:bytes (inventory status implement) → `builtin-bytes:bytes`
- FAIL precomputed vs direct parity: exit 1, refused with binding-name-reserved (inventory status impl → `binding-name-reserved`
- FAIL precomputed condition then direct nested: exit 1, refused with binding-name-reserved (inventory → `binding-name-reserved`
- FAIL ordinary array ordering rejected: stderr lacks [ordering comparisons require two numbers or two → `type:<`
- FAIL direct if array ordering rejected: stderr lacks [ordering comparisons require two numbers or tw → `type:<`
- FAIL direct if object ordering rejected: stderr lacks [ordering comparisons require two numbers or t → `opaque:irregular-legacy-parse`

</details>

#### test-v48-callable-parity: 2 failing case(s)

| Reason | Cases |
| --- | --- |
| `prepared-decl-type` | 1 |
| `static-error:arity` | 1 |
| **total** | **2** |

#### test-v48-map-set-index: 3 failing case(s)

| Reason | Cases |
| --- | --- |
| `callable:sorted_map` | 2 |
| `callable:sorted_set` | 1 |
| **total** | **3** |

#### test-v48-prepared-collection-parity: 4 failing case(s)

| Reason | Cases |
| --- | --- |
| `postfix-spelling` | 2 |
| `builtin-arity:contains` | 1 |
| `builtin-arity:set` | 1 |
| **total** | **4** |

#### test-v48-identifier-parity: 1 failing case(s)

| Reason | Cases |
| --- | --- |
| `binding-shadows-fn` | 1 |
| **total** | **1** |

#### test-v49-selector-parity: 1 failing case(s)

| Reason | Cases |
| --- | --- |
| `for-capture-vanishing` | 1 |
| **total** | **1** |

#### test-sha256: 21 failing case(s) (21 FAIL lines; the gate's own counter prints `68 passed, fail=1` (a flag, not a count))

| Reason | Cases |
| --- | --- |
| `builtin-bytes:bytes` *(inherited)* | 10 |
| `fn-name-reserved` *(inherited)* | 3 |
| `builtin-bytes:bytes` | 2 |
| `builtin-bytes:open_bytes` *(inherited)* | 2 |
| `builtin-bytes:open_bytes` | 1 |
| `builtin-arity:sha256` | 1 |
| `builtin-arity:sha256_file` | 1 |
| `fn-name-reserved` | 1 |
| **total** | **21** |

#### test-parser-hardening: 1 failing case(s)

| Reason | Cases |
| --- | --- |
| `nesting-limit` | 1 |
| **total** | **1** |

#### test-cp15-numeric-repair: 1 failing case(s)

| Reason | Cases |
| --- | --- |
| `number-literal` | 1 |
| **total** | **1** |

#### test-sort-number-compact: 1 failing case(s)

| Reason | Cases |
| --- | --- |
| `builtin-receiver:substr` | 1 |
| **total** | **1** |

#### test-cp19-s1-dispatch: 9 failing case(s) (gate's FAILED list has 9 names)

| Reason | Cases |
| --- | --- |
| `callable:run` | 6 |
| `callable:run` *(inherited)* | 2 |
| `builtin-lazy-args:setenv` | 1 |
| **total** | **9** |

<details><summary>Per case</summary>

- FAIL user function side effect once: marker='' stderr='error: <stdin>:3:3: NIFT_VM session refused:  → `callable:run`
- FAIL failed user function status non-zero → `callable:run (inherited: same program, no reason on its line)`
- FAIL $[...] evaluated once: bump='' stderr='error: <stdin>:3:3: NIFT_VM session refused: callable:ru → `callable:run`
- FAIL $[...] result dispatched → `callable:run (inherited: same program, no reason on its line)`
- FAIL executed // interpolation once: error: <stdin>:3:3: NIFT_VM session refused: callable:run: the  → `callable:run`
- FAIL executed pipeline interpolation once: error: <stdin>:3:3: NIFT_VM session refused: callable:run → `callable:run`
- FAIL interpolation left-to-right: error: <stdin>:2:16: NIFT_VM session refused: callable:run: the ca → `callable:run`
- FAIL interpolation cannot reclassify: error: <stdin>:3:18: NIFT_VM session refused: builtin-lazy-arg → `builtin-lazy-args:setenv`
- FAIL syntax validates before interpolation: error: <stdin>:3:3: NIFT_VM session refused: callable:ru → `callable:run`

</details>

#### test-runtime2-cp03-simplify: 1 failing case(s)

| Reason | Cases |
| --- | --- |
| `callable:module_path` | 1 |
| **total** | **1** |

#### test-runtime2-host-deps: 24 failing case(s) (24 FAIL lines; no gate total printed)

| Reason | Cases |
| --- | --- |
| `builtin-bytes:open_bytes` | 6 |
| `callable:run` | 6 |
| `host-handle-flow` | 4 |
| `callable:read` | 3 |
| `callable:cmd` | 2 |
| `type:length` | 1 |
| `statement:Command` | 1 |
| `callable:run` *(inherited)* | 1 |
| **total** | **24** |

Notes:
- filesystem-recipe: `postfix-spelling` (49 of 91) is the largest share; the `copy-wrong-arity` and sort-factory
  `named-precedence` cases are recorded pending differences (`pending_differences.tsv`), not refusals.
- `opaque:irregular-legacy-parse` in v48-comparison is ordinary code: `if({"a": 1} == {"a": 1}) { print("yes") }`
  (legacy prints `yes`; the VM refuses). It is in the syntax family (5.4).
- `callable:run` and `callable:cmd` (cp19-s1, host-deps) are deferred-named rows.

### 5. G and N families

Common to every family:
- **VM refusal form**: stderr `error: <file>:<l>:<c>: not compiled for the VM (<reason>): <text> is not supported by
  the Runtime 2.0 VM yet` (sessions: `NIFT_VM session refused: <reason>: ...`), exit 1, nothing executed.
- **Corpus** = the tip collection's full/consequential/fallback counts summed over the family's rows.
- **Affected gates** = section 4, summed over the family's rows.
- **Ordinary scripting**: the repository has no `examples/` directory; frequencies are over the 2539 `.f` test files
  and the extracted smoke heredocs where measured.
- **Approval**: none of these rows is approved; rulings cited are reasons, not approvals, unless stated.

#### 5.1 Static errors raised at the wrong point (G 34, N 25)

- **Rows**: G `builtin-arity:contains`, `builtin-arity:last`, `builtin-arity:set`, `builtin-arity:sha256`, `builtin-arity:sha256_file`, `builtin-arity:split`, `builtin-receiver:stringify`, `builtin-receiver:substr`, `builtin-receiver:to_string`, `coro-async-value`, `coro-module-before-import`, `coro-module-private-async`, `declare-const`, `error-result-expression`, `host-handle-flow`, `jump-outside-loop`, `non-transferable`, `number-literal`, `opaque:legacy-runtime-error`, `opaque:struct-body`, `static-error:arity`, `static-error:member-assign`, `static-error:redeclared`, `static-error:struct-arity`, `struct-private`, `type:+`, `type:<`, `type:>`, `type:compound-assignment`, `type:increment`, `type:length`, `type:member`, `type:script-return`, `type:unary`.
  N `assign-catch-binding`, `await-outside-coroutine-unit`, `builtin-arity:abs`, `builtin-arity:keys`, `builtin-arity:stringify`, `builtin-arity:trim`, `builtin-receiver:empty`, `builtin-receiver:first`, `builtin-receiver:index_of`, `builtin-receiver:keys`, `builtin-receiver:sum`, `builtin-receiver:to_double`, `builtin-receiver:to_upper`, `coro-future-flow:Call`, `coro-future-flow:method`, `coro-thread-callee`, `method:replace_once`, `push-receiver`, `static-error:push-arity`, `type:%`, `type:*`, `type:-`, `type:/`, `type:<=`, `type:>=`.
- **Root cause**: the compiler can prove that legacy fails here, or the construct is outside the admitted shapes, and refuses before execution instead of raising legacy's error at the point legacy raises it.
- **Raise sites**: `builtin-arity:*` compiler_keyed.cpp:105,196 and compiler_native_args.cpp:24; `builtin-receiver:*` compiler_dispatch.cpp:41; `type:<op>` compiler_expr.cpp:165,170; `static-error:arity` compiler_call.cpp:52; `declare-const` compiler_assign.cpp:47,151,205,278,315,331, compiler_element_path.cpp:86; `number-literal` compiler_literal_expr.cpp:58; `host-handle-flow` compiler_handles.cpp:190; `non-transferable` compiler_handles.cpp:195; `coro-async-value` compiler_coro.cpp:158; `coro-module-before-import` compiler_coro_module.cpp:32; `coro-module-private-async` compiler_coro_module.cpp:31; `static-error:push-arity` compiler_stmt_expr.cpp:27; `push-receiver` compiler_stmt_expr.cpp:22; `await-outside-coroutine-unit` compiler_coro.cpp:80; `assign-catch-binding` compiler_assign.cpp:332; `coro-future-flow:method` compiler_coro.cpp:68; `coro-thread-callee` compiler_thread.cpp:23.
- **Representative (a legacy error with prior output)**:

```
print("a")
print(sha256("a", "b"))
```

- Legacy (`NIFT_VM=0`): exit 1, stdout `a`, stderr `error: main.f:2:7: sha256: expected one string or bytes value`.
- VM: exit 1, stdout empty, stderr `error: main.f:2:7: not compiled for the VM (builtin-arity:sha256): 'sha256()' with this number of arguments is not supported by the Runtime 2.0 VM yet` (reason `builtin-arity:sha256`).

- **Representative (a program legacy runs: const object)**:

```
const o := {"k": 1}
print(o["k"])
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `1`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:1: not compiled for the VM (declare-const): this use of a const binding is not supported by the Runtime 2.0 VM yet` (reason `declare-const`).

- **Representative (a program legacy runs: smoke v45_adversarial_runtime/007-churn)**:

```
@fn[async](id(x)) { return x }
i := 0
while(i < 50) {
  t := thread(id, i); t.join()
  f := id(i); r := await f
  i += 1
}
```

- Legacy (`NIFT_VM=0`): exit 0, stdout empty, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:4:15: not compiled for the VM (coro-async-value): this coroutine or async usage is not supported by the Runtime 2.0 VM yet` (reason `coro-async-value`).

- **Representative (N, legacy accepts an extra argument)**:

```
o := {"a": 1}
k := o.keys(1)
print(k.size())
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `1`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:2:6: not compiled for the VM (builtin-arity:keys): 'keys()' with this number of arguments is not supported by the Runtime 2.0 VM yet` (reason `builtin-arity:keys`).

- **Affected gates**: eval_entry: `opaque:legacy-runtime-error` 2; test-cp15-numeric-repair: `number-literal` 1; test-runtime2-host-deps: `host-handle-flow` 4, `type:length` 1; test-sha256: `builtin-arity:sha256` 1, `builtin-arity:sha256_file` 1; test-sort-number-compact: `builtin-receiver:substr` 1; test-v410-filesystem-recipe-parity: `builtin-arity:last` 1, `builtin-arity:split` 1, `opaque:legacy-runtime-error` 1; test-v410-native-dispatch-parity: `static-error:arity` 1; test-v410-sort-factory-parity: `type:member` 1; test-v48-callable-parity: `static-error:arity` 1; test-v48-comparison-parity: `type:<` 2; test-v48-prepared-collection-parity: `builtin-arity:contains` 1, `builtin-arity:set` 1.
- **Corpus** (tip, full/consequential/fallback): 47/44/42.
- **Approval**: none. `declare-const` cites D-009 #2/D-058 for the scalar form only.
- **Ordinary-scripting impact**: **not only already-failing programs** (C111 was wrong): `declare-const` refuses const objects/arrays that legacy runs (`const o := {"k": 1}` prints 1), `coro-async-value` refuses the churn smoke that legacy runs (exit 0), and `builtin-arity:keys` refuses a call legacy accepts. The rest reach only programs that fail in legacy, but the refusal is pre-execution, so legacy's prior output (e.g. `a` above) is lost.
- **PROPOSAL**: one generic "raise legacy's error at the point" mechanism (the C25 `type:print` pattern) for the legacy-error rows; implement const aggregates, the async-value flow and legacy-accepted arities as capability work.

#### 5.2 Undefined names and calls (G 18, N 2)

- **Rows**: G `assign-free-name`, `binding-shadows-fn`, `callable:*`, `callable:assert`, `callable:exit`, `callable:len`, `callable:nope`, `callable:nosuch`, `callable:to_string`, `callable:undefined_fn`, `callable:undefined_thing`, `callable:unknown`, `fn-called-before-declaration`, `method:*`, `method:nope`, `method:nosuch`, `unresolved-name`, `method:*`.
  N `callable:stringify`, `import-scope`.
- **Root cause**: an undefined callable, method or name is refused at compile time instead of raising legacy's error when reached.
- **Raise sites**: `callable:<name>` compiler_thread.cpp:22 (fallback for every unbound callee); `method:<name>` compiler_call.cpp:204 (now covered by the `method:*` family row); `assign-free-name` compiler_assign.cpp:204; `fn-called-before-declaration` resolver.cpp:128; `binding-shadows-fn` compiler_assign.cpp:17; `import-scope` module_link.cpp:356.
- **Representative**:

```
print("a")
nope(1)
```

- Legacy (`NIFT_VM=0`): exit 1, stdout `a`, stderr `error: main.f:2:1: undefined callable: nope`.
- VM: exit 1, stdout empty, stderr `error: main.f:2:1: not compiled for the VM (callable:nope): the call 'nope()' is not supported by the Runtime 2.0 VM yet` (reason `callable:nope`).

- **Representative (C112 G, root-free)**:

```
try { undefined_thing() } catch (e) { print("c") }
```

- Legacy (`NIFT_VM=0`): exit 1, stdout empty, stderr `error: main.f:1:7: undefined callable: undefined_thing`.
- VM: exit 1, stdout empty, stderr `error: main.f:1:7: not compiled for the VM (callable:undefined_thing): the call 'undefined_thing()' is not supported by the Runtime 2.0 VM yet` (reason `callable:undefined_thing`).

- **Representative (N)**:

`m.nft`:

```
fn(helper(x)) {
  return x * 3
}
fn(api(x)) {
  return helper(x)
}
export(api)
```

`main.f`:

```
import("m.nft")
print(helper(4))
```

- Legacy (`NIFT_VM=0`): exit 1, stdout empty, stderr `error: main.f:2:7: undefined callable: helper`.
- VM: exit 1, stdout empty, stderr `error: main.f:2:7: not compiled for the VM (import-scope): this form of import is not supported by the Runtime 2.0 VM yet` (reason `import-scope`).

- **Affected gates**: eval_entry: `unresolved-name` 1; test-v410-filesystem-recipe-parity: `unresolved-name` 1; test-v410-native-dispatch-parity: `callable:len` 2, `callable:to_string` 1, `callable:unknown` 1; test-v48-identifier-parity: `binding-shadows-fn` 1.
- **Corpus** (tip, full/consequential/fallback): 13/27/12.
- **Approval**: `assign-free-name`, `callable:nope`, `callable:exit`, `method:nope` cite "owner ruling 2026-10-29: keep refused pending dynamic-resolution semantics"; review P1-2 found no decision text that approves it for release.
- **Ordinary-scripting impact**: low for correct scripts (legacy fails too), but prior output is lost; `binding-shadows-fn` (identifier gate) and `fn-called-before-declaration` can be reached by ordinary code that legacy runs in some orders.
- **PROPOSAL**: decide with dynamic scope (section 8): if names stay statically resolved, raise `undefined callable: <n>` / `unknown value or malformed expression` at the point.

#### 5.3 Context-dependent legacy behaviour (G 8, N 7)

- **Rows**: G `context:assignment-type`, `context:number-to-int`, `context:object-length`, `jump-not-last`, `prepared-decl-type`, `prepared-fn-unprepared`, `prepared-unpreparable`, `string-arg-type`.
  N `context:collection-empty`, `context:modulo-assignment`, `context:negative-zero`, `context:parameter-type`, `context:string-append-type`, `context:struct-field`, `for-missing-write`.
- **Root cause**: legacy behaviour depends on the calling context (loop or prepared code, declared binding types), so the VM refuses loop-reachable or prepared forms.
- **Raise sites**: `context:number-to-int` compiler_call.cpp:196; `context:object-length` compiler_call.cpp:237; `prepared-decl-type` prepared.cpp:248; `prepared-fn-unprepared` prepared.cpp:330; `string-arg-type` prepared.cpp:370; `context:modulo-assignment` compiler_assign.cpp:295; `context:string-append-type` compiler_assign.cpp:300; `context:negative-zero` compiler_expr.cpp:240,318; `context:parameter-type` compiler_call.cpp:109, compiler_structs.cpp:370; `for-missing-write` compiler_loops_step.cpp:92; `context:struct-field` compiler_assign.cpp:203.
- **Representative**:

```
fn(n(o)) { return o.length() }
i := 0
while (i < 1) { print(n({"k": 1})); i += 1 }
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `1`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:19: not compiled for the VM (context:object-length): a context-dependent legacy behaviour (object-length) is not supported by the Runtime 2.0 VM yet` (reason `context:object-length`).

- **Representative (N, legacy runs)**:

```
s := "a" + 0.5
i := 0
while (i < 1) { s += "b"; i += 1 }
print(s)
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `a0.5b`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:3:17: not compiled for the VM (context:string-append-type): a context-dependent legacy behaviour (string-append-type) is not supported by the Runtime 2.0 VM yet` (reason `context:string-append-type`).

- **Affected gates**: test-v48-callable-parity: `prepared-decl-type` 1; v44_ast_expression_smoke: `prepared-unpreparable` 1.
- **Corpus** (tip, full/consequential/fallback): 14/10/13.
- **Approval**: D-018, D-014 and D-015 are cited as "keep refused" rationale, not release approval.
- **Ordinary-scripting impact**: moderate: `.length()` on objects, `to_int` and string appends in loops are ordinary.
- **PROPOSAL**: owner rule D-015 "consistent declarations everywhere", or implement both legacy contexts.

#### 5.4 Syntax and statement forms (G 12, N 6)

- **Rows**: G `binding-name-reserved`, `coro-future-flow:Assign`, `expression-statement`, `expression:assignment-target`, `incdec-target`, `index-target`, `nesting-limit`, `opaque:irregular-legacy-parse`, `param-name-reserved`, `postfix-spelling`, `shebang-statement`, `text-type-name`.
  N `comparison-chain`, `object-key`, `opaque:nesting-limit`, `param-shadows-fn`, `prefix-not-precedence`, `string-literal`.
- **Root cause**: statement and expression forms legacy accepts that the VM parser or compiler does not admit.
- **Raise sites**: `expression-statement` compiler_stmt_expr.cpp:84; `shebang-statement` compiler.cpp:139; `postfix-spelling` compiler_rules.cpp:41; `index-target` compiler_rules.cpp:332, compiler_element_path.cpp:67,69; `incdec-target` compiler_assign.cpp:312,328; `param-name-reserved` resolver.cpp:77, compiler_closure.cpp:81; `nesting-limit` compiler.cpp:259; `opaque:nesting-limit` expr_parser.cpp:255,307,341, expr_postfix.cpp:98, statement_parser.cpp:50; `opaque:irregular-legacy-parse` (parser opaque marking); `comparison-chain` compiler_rules.cpp:309; `prefix-not-precedence` compiler_rules.cpp:298; `string-literal` compiler_literal_expr.cpp:62; `object-key` compiler_expr.cpp:344; `param-shadows-fn` compiler_unit.cpp:286; `binding-name-reserved` resolver.
- **Representative (ordinary code, v48-comparison)**:

```
if({"a": 1} == {"a": 1}) { print("yes") }
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `yes`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:4: not compiled for the VM (opaque:irregular-legacy-parse): source the VM parser does not structure (irregular-legacy-parse) is not supported by the Runtime 2.0 VM yet` (reason `opaque:irregular-legacy-parse`).

- **Representative**:

```
print("a")
#!/usr/bin/env nift
print("b")
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `a\nb`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:2:1: not compiled for the VM (shebang-statement): a shebang line after the first line is not supported by the Runtime 2.0 VM yet` (reason `shebang-statement`).

- **Representative (owner-excluded checkpoint-1 form)**:

```
a := [1]
a[0]++
print(a[0])
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `2`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:2:1: not compiled for the VM (incdec-target): this increment or decrement target is not supported by the Runtime 2.0 VM yet` (reason `incdec-target`).

- **Representative (N)**:

```
print(1 != 1 == 1)
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `false`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:7: not compiled for the VM (comparison-chain): a chained comparison is not supported by the Runtime 2.0 VM yet` (reason `comparison-chain`).

- **Affected gates**: test-parser-hardening: `nesting-limit` 1; test-v410-filesystem-recipe-parity: `postfix-spelling` 49; test-v48-comparison-parity: `opaque:irregular-legacy-parse` 4, `binding-name-reserved` 2; test-v48-prepared-collection-parity: `postfix-spelling` 2.
- **Corpus** (tip, full/consequential/fallback): 17/41/14.
- **Approval**: none. `incdec-target` (`a[0]++`) is the form the owner excluded from checkpoint 1 (16:35), pinned since C107 by `assign_family.py` `refused_increment`; it is not a new finding.
- **Ordinary-scripting impact**: high: `postfix-spelling` alone is 49 of filesystem-recipe's 91 failures, and `if({...} == {...})` (irregular parse) and `a[0]++` are ordinary.
- **PROPOSAL**: compile `#!` and bare scalars as no-ops; lower the comparison and prefix rules to legacy's precedence; admit the irregular `if(` spelling and postfix spellings; `a[0]++` is for the owner to schedule (it was excluded from checkpoint 1).

#### 5.5 Missing built-ins and host surfaces (G 19, N 5)

- **Rows**: G `builtin-arity:copy`, `builtin-arity:move`, `builtin-bytes:bytes`, `builtin-bytes:encode`, `builtin-bytes:open_bytes`, `builtin-bytes:secure_random_bytes`, `builtin-lazy-args:move`, `builtin-lazy-args:setenv`, `builtin-lazy-args:substr`, `builtin-lazy-args:validate`, `callable:err`, `callable:minify`, `callable:module_path`, `callable:read`, `callable:secure_random_bytes`, `callable:sorted_map`, `callable:sorted_set`, `callable:warn`, `statement:Command`.
  N `builtin-lazy-args:range`, `builtin-lazy-args:replace`, `builtin-literal-arg:validate`, `builtin-presentation-chain`, `host-literal-arg`.
- **Root cause**: the native exists but its compiler admission, lazy-argument order or receiver kind is missing; bytes have no kind bit (CP15 W8).
- **Raise sites**: `builtin-lazy-args:*` compiler_native_args.cpp:34; `host-literal-arg` compiler_native_args.cpp:27; `builtin-arg-fault-context` compiler_native_args.cpp:42; `builtin-presentation-chain` compiler_call.cpp:225; `builtin-bytes:*` compiler_native_args.cpp:9; `builtin-literal-arg:validate` compiler_native_args.cpp:16; `callable:*` compiler_thread.cpp:22; `statement:Command` compiler.cpp:156.
- **Representative**:

```
s := sorted_set()
s.add(2)
s.add(1)
print(s.size())
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `2`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:6: not compiled for the VM (callable:sorted_set): the call 'sorted_set()' is not supported by the Runtime 2.0 VM yet` (reason `callable:sorted_set`).

- **Representative (N, legacy runs)**:

```
fn(f()) { print("f")
return 3 }
r := range(0, f())
print(r.size())
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `f\n3`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:3:15: not compiled for the VM (builtin-lazy-args:range): lazily evaluated arguments of 'range()' is not supported by the Runtime 2.0 VM yet` (reason `builtin-lazy-args:range`).

- **Representative (N, legacy runs)**:

```
n := 1
f := file("a.txt")
f.open("w")
f.write("v=$[n]")
f.save()
f.close()
cat("a.txt")
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `v=$[n]`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:4:9: not compiled for the VM (host-literal-arg): a literal argument to a host operation is not supported by the Runtime 2.0 VM yet` (reason `host-literal-arg`).

- **Affected gates**: eval_entry: `builtin-bytes:bytes` 3; test-cp19-s1-dispatch: `builtin-lazy-args:setenv` 1; test-runtime2-cp03-simplify: `callable:module_path` 1; test-runtime2-host-deps: `builtin-bytes:open_bytes` 6, `callable:read` 3, `statement:Command` 1; test-sha256: `builtin-bytes:bytes` 12, `builtin-bytes:open_bytes` 3; test-v410-filesystem-recipe-guard: `builtin-arity:copy` 1; test-v410-filesystem-recipe-parity: `builtin-arity:copy` 13, `builtin-lazy-args:move` 13, `builtin-arity:move` 2; test-v46-secure-random-cli: `builtin-bytes:secure_random_bytes` 1; test-v48-bytes-index-parity: `builtin-bytes:bytes` 13; test-v48-comparison-parity: `builtin-bytes:bytes` 3; test-v48-map-set-index: `callable:sorted_map` 2, `callable:sorted_set` 1; v46_output_smoke: `callable:err` 4.
- **Corpus** (tip, full/consequential/fallback): 17/1/15.
- **Approval**: none; `callable:err`, `read`, `warn` and the bytes rows are implement.
- **Ordinary-scripting impact**: high for host scripting: bytes (`builtin-bytes:bytes` 13 in bytes-index, 12 in sha256, 3 in comparison, 3 in eval_entry), filesystem moves and copies (`builtin-lazy-args:move` 13, `builtin-arity:copy` 14), `err()` (all 4 v46-output failures).
- **PROPOSAL**: implement; treat lazy argument order as one general rule, not per built-in.

#### 5.6 CP24 project APIs (G 9, N 0)

- **Rows**: G `callable:build`, `callable:build_all`, `callable:build_names`, `callable:inject`, `callable:status`, `callable:track`, `callable:tracked`, `callable:untrack`, `opaque:template-text`.
- **Root cause**: the project and build APIs are not bound on the VM.
- **Raise sites**: compiler_thread.cpp:22 (`callable:*`); `opaque:template-text` (parser).
- **Representative**:

```
print(tracked())
```

- Legacy (`NIFT_VM=0`): exit 1, stdout empty, stderr `error: main.f:1:7: tracked: not a Nift project (no .nift/config.json found from /tmp/claude-0/-home-claude/5262fcab-c28a-58db-987c-2c4b624387c1/scratchpad/rescan/fam/F-cp24)`.
- VM: exit 1, stdout empty, stderr `error: main.f:1:7: not compiled for the VM (callable:tracked): the call 'tracked()' is not supported by the Runtime 2.0 VM yet` (reason `callable:tracked`).

- **Affected gates**: script_import_syntax: `opaque:template-text` 1.
- **Corpus** (tip, full/consequential/fallback): 39/0/31.
- **Approval**: none.
- **Ordinary-scripting impact**: project scripts only, but `callable:inject` (24/0/24) also masks `map`, `filter`, `group_by`, `type:index` and `method:join` (auto.f, auto2.f).
- **PROPOSAL**: a CP24-on-VM checkpoint.

#### 5.7 Structs (G 6, N 7)

- **Rows**: G `context:struct-new`, `presentation-struct-method:*`, `struct-after-statement`, `struct-field-init`, `struct-no-method`, `struct-value`.
  N `struct-assign`, `struct-member-clash`, `struct-name`, `struct-nested`, `struct-no-field`, `struct-redeclared`, `struct-shadows-fn`.
- **Root cause**: struct features outside the admitted subset: construction and defaults in loops, non-constant defaults, declarations after statements, reassigning struct bindings, instances as values, and presentation choice in sessions.
- **Raise sites**: `context:struct-new` compiler_structs.cpp:278; `struct-field-init` structs.cpp:109; `struct-after-statement` structs.cpp:88; `struct-value` compiler_assign.cpp:140, compiler_structs.cpp:64; `struct-assign` compiler_assign.cpp:209; `struct-shadows-fn` structs.cpp:93; `struct-member-clash` structs.cpp:103; `struct-name` structs.cpp:91; `struct-nested` structs.cpp:78; `struct-no-field` compiler_structs.cpp:71; `struct-redeclared` structs.cpp:92; `presentation-struct-method:*` compiler_presentation.cpp.
- **Representative (non-constant default, legacy runs)**:

```
struct(S) {
  xs := [1]
  fn(add(v)) { xs.push(v) }
}
s := S()
s.add(2)
print(s.xs.stringify())
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `[1,2]`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:2:3: not compiled for the VM (struct-field-init): this struct feature is not supported by the Runtime 2.0 VM yet` (reason `struct-field-init`).

- **Representative (C112 G, root-free)**:

```
struct(P) {
  v := 1
}
struct(Q) {
  v := 2
}
fn(pick(b)) {
  if (b) { return P() }
  return Q()
}
x := pick(true)
x.v = 9
print(x.v)
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `9`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:12:1: not compiled for the VM (struct-value): this struct feature is not supported by the Runtime 2.0 VM yet` (reason `struct-value`).

- **Representative (N, legacy runs)**:

```
struct(P) {
  v := 1
}
x := P()
x = P()
x.v = 3
print(x.v)
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `3`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:5:1: not compiled for the VM (struct-assign): this struct feature is not supported by the Runtime 2.0 VM yet` (reason `struct-assign`).

- **Representative (restored G, session)**:

session (`nift -i`) input:

```
struct(P) {
  v := 1
  fn(stringify()) { return "P!" }
}
xs := [P()]
print(xs[0].stringify())
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `P{v:1}`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: <stdin>:6:7: NIFT_VM session refused: presentation-struct-method:stringify: '.stringify()' on a value that may be an instance of a struct declaring its own 'stringify()' method (choosing the method or the built-in at run ti` (reason `presentation-struct-method:stringify`).

- **Affected gates**: v44_root_path_corruption_reproducers: `context:struct-new` 2.
- **Corpus** (tip, full/consequential/fallback): 13/2/11.
- **Approval**: `presentation-struct-method:*`: the B3 ruling (2026-11-05) approves that an explicit `.stringify()` calls the struct's own method (so the VM may differ from legacy's `P{v:1}`); B4 implemented the run-time choice for scripts and modules only, so the session refusal is the gap. The others: none.
- **Ordinary-scripting impact**: high: `x = P()` (`struct-assign`), returning either of two structs (`struct-value`), array defaults with push methods (`struct-field-init`) and structs declared after a `print` (`struct-after-statement`) are ordinary. The escaped-ref smoke fails on `context:struct-new` (section 15).
- **PROPOSAL**: checkpoint 2 takes `context:struct-new` and `struct-construction`; see the scope PROPOSAL in section 10 for the rest. The legacy-error rows (`struct-no-field` and the declaration checks) fit the 5.1 raise-at-point mechanism.

#### 5.8 Coroutine and loop-capture flow (G 2, N 0)

- **Rows**: G `coro-future-flow:Member`, `for-capture-vanishing`.
- **Root cause**: a for-loop body that captures the element while the source may shrink, and `.await()` method spelling on a future.
- **Raise sites**: `for-capture-vanishing` compiler_loops_step.cpp:90; `coro-future-flow:Member` compiler_coro.cpp:36,68.
- **Representative**:

```
a := [[1]]
for(row : a) { f := x => row[0]; a.push([9]); print(f(0)) }
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `1`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:2:1: not compiled for the VM (for-capture-vanishing): this for-loop form is not supported by the Runtime 2.0 VM yet` (reason `for-capture-vanishing`).

- **Affected gates**: test-v410-sort-factory-parity: `for-capture-vanishing` 2; test-v49-selector-parity: `for-capture-vanishing` 1.
- **Corpus** (tip, full/consequential/fallback): 1/0/1.
- **Approval**: none.
- **Ordinary-scripting impact**: moderate: capturing the loop element in a lambda while appending to the source is uncommon but appears in sort-factory (2) and v49-selector (1).
- **PROPOSAL**: implement the vanishing-element capture (copy the element into the closure as legacy does); raise legacy's error for `.await()`.

#### 5.9 Kind inference (G 4, N 4)

- **Rows**: G `interpolation:unknown-type`, `method:sort_by`, `type:element-assignment`, `type:for-source`.
  N `builtin-arg-fault-context`, `interpolation:non-scalar`, `postfix-receiver:member`, `type:for-source-union`.
- **Root cause**: the compiler cannot prove a value's kind where legacy dispatches at run time.
- **Raise sites**: `interpolation:unknown-type` compiler_interp.cpp:274; `interpolation:non-scalar` compiler_interp.cpp:275; `method:sort_by` compiler_call.cpp:204; `postfix-receiver:member` compiler_rules.cpp:53; `builtin-arg-fault-context` compiler_native_args.cpp:42.
- **Representative**:

```
fn(f(x)) { return "v=$[x]" }
print(f(1))
print(f("s"))
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `v=1\nv=s`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:24: not compiled for the VM (interpolation:unknown-type): this string interpolation (unknown-type) is not supported by the Runtime 2.0 VM yet` (reason `interpolation:unknown-type`).

- **Representative (N, legacy errors)**:

```
fn(f(x)) { return x }
v := f([1])
print("v=$[v]")
```

- Legacy (`NIFT_VM=0`): exit 1, stdout empty, stderr `error: main.f:3:1: print: parameter expression must resolve to a scalar value`.
- VM: exit 1, stdout empty, stderr `error: main.f:3:12: not compiled for the VM (interpolation:non-scalar): this string interpolation (non-scalar) is not supported by the Runtime 2.0 VM yet` (reason `interpolation:non-scalar`).

- **Affected gates**: test-v410-sort-factory-parity: `method:sort_by` 1.
- **Corpus** (tip, full/consequential/fallback): 2/0/0.
- **Approval**: none.
- **Ordinary-scripting impact**: high: interpolating a parameter of mixed kind is ordinary.
- **PROPOSAL**: a run-time-kind render path for interpolation; dynamic dispatch for `sort_by` on returned factories.

#### 5.10 Portability (G 1, N 0)

- **Rows**: G `method:sort`.
- **Root cause**: comparator sorts are not certified across toolchains.
- **Raise sites**: `method:sort` compiler_call.cpp:204.
- **Representative**:

```
xs := [3, 1, 2]
xs.sort((a, b) => a < b)
print(xs.stringify())
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `[1,2,3]`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:2:1: not compiled for the VM (method:sort): the method call '.sort()' is not supported by the Runtime 2.0 VM yet` (reason `method:sort`).

- **Affected gates**: none of the gates re-run in section 4.
- **Corpus** (tip, full/consequential/fallback): 0/0/0.
- **Approval**: "owner 2026-10-25 comparator profile" is cited but the row is not approved.
- **Ordinary-scripting impact**: moderate: comparator sorts are ordinary.
- **PROPOSAL**: verify on GCC 15.2 on Linode (section 16), then rule.

#### 5.11 Aliasing (G 1, N 0)

- **Rows**: G `path-in-call-argument`.
- **Root cause**: an element path passed as a call argument is not admitted.
- **Raise sites**: `path-in-call-argument` compiler_call.cpp:278 (and the `:imported` variant).
- **Representative**:

```
o := {"k": 0}
a := [5]
fn(id(x)) { return x }
print(a[id(o.k)])
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `5`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:4:12: not compiled for the VM (path-in-call-argument): an element path as a call argument is not supported by the Runtime 2.0 VM yet` (reason `path-in-call-argument`).

- **Affected gates**: none of the gates re-run in section 4.
- **Corpus** (tip, full/consequential/fallback): 2/0/2.
- **Approval**: none; the row is implement.
- **Ordinary-scripting impact**: moderate: `f(o.k)` inside an index is ordinary; it also masks `method:push`.
- **PROPOSAL**: implement under B5's shared-reference rules.

#### 5.12 CP24 / modules: exports and imports (G 8, N 8)

- **Rows**: G `export-struct`, `import-collision`, `import-dynamic`, `import-name-clash`, `module-bare-return`, `module-host-op:cat`, `module-host-op:exists`, `statement:Export`.
  N `import-limit`, `import-reinitialised`, `module-host-op:cp`, `module-host-op:file`, `module-host-op:getenv`, `module-host-op:is_dir`, `module-host-op:is_file`, `module-host-op:remove`.
- **Root cause**: see section 7.
- **Raise sites**: see section 7.
- **Representative**:

```
fn(twice(n)) { return n * 2 }
export(twice)
print(twice(2))
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `4`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:2:1: not compiled for the VM (statement:Export): an export statement in this position is not supported by the Runtime 2.0 VM yet` (reason `statement:Export`).

- **Affected gates**: script_import_syntax: `import-dynamic` 4; test-v410-sort-factory-parity: `import-name-clash` 1.
- **Corpus** (tip, full/consequential/fallback): 56/0/43.
- **Approval**: see section 7.
- **Ordinary-scripting impact**: see section 7.
- **PROPOSAL**: see section 7.

#### 5.13 Variadics, spread and destructuring (G 4, N 0)

- **Rows**: G `declare-pattern`, `fn-param`, `lambda-variadic`, `spread`.
- **Root cause**: see section 6.
- **Raise sites**: see section 6.
- **Representative**:

```
fn(sum(...xs)) { t := 0; for (x : xs) { t += x }; return t }
print(sum(1, 2, 3))
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `6`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:11: not compiled for the VM (fn-param): this function declaration form is not supported by the Runtime 2.0 VM yet` (reason `fn-param`).

- **Affected gates**: test-v410-filesystem-recipe-parity: `spread` 1; v44_root_path_corruption_reproducers: `declare-pattern` 1.
- **Corpus** (tip, full/consequential/fallback): 10/1/6.
- **Approval**: see section 6.
- **Ordinary-scripting impact**: see section 6.
- **PROPOSAL**: see section 6.

#### 5.14 Dynamic scope (G 2, N 0)

- **Rows**: G `free-name`, `free-name:dynamic-caller`.
- **Root cause**: see section 8.
- **Raise sites**: see section 8.
- **Representative**:

```
fn(read_hidden()) { return hidden }
fn(mk(hidden)) { return read_hidden() }
print(mk(7))
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `7`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:28: not compiled for the VM (free-name): a name that is not declared in this scope is not supported by the Runtime 2.0 VM yet` (reason `free-name`).

- **Affected gates**: test-v410-sort-factory-parity: `free-name` 1.
- **Corpus** (tip, full/consequential/fallback): 1/0/2.
- **Approval**: see section 8.
- **Ordinary-scripting impact**: see section 8.
- **PROPOSAL**: see section 8.

### 6. Variadics, spread and destructuring

- **Raise sites**: `fn-param` resolver.cpp:70; `lambda-variadic` compiler_closure.cpp:80; `spread` compiler_call.cpp:87,160;
  `declare-pattern` compiler_assign.cpp:48.

```
fn(sum(...xs)) { t := 0; for (x : xs) { t += x }; return t }
print(sum(1, 2, 3))
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `6`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:11: not compiled for the VM (fn-param): this function declaration form is not supported by the Runtime 2.0 VM yet` (reason `fn-param`).

```
fn(add(a, b)) { return a + b }
xs := [1, 2]
print(add(...xs))
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `3`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:3:11: not compiled for the VM (spread): the spread operator is not supported by the Runtime 2.0 VM yet` (reason `spread`).

```
f := (...xs) => xs.length()
print(f(1, 2))
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `2`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:10: not compiled for the VM (lambda-variadic): a variadic lambda is not supported by the Runtime 2.0 VM yet` (reason `lambda-variadic`).

```
[x, y] := [1, 2]
print(x + y)
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `3`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:1: not compiled for the VM (declare-pattern): this declaration pattern is not supported by the Runtime 2.0 VM yet` (reason `declare-pattern`).

- **Affected gates**: test-v410-filesystem-recipe-parity: `spread` 1; v44_root_path_corruption_reproducers: `declare-pattern` 1.
- **Corpus**: 10/1/6. `fn-param` masks 8 `method:*` rows (section 3.5) and `method:join` (ok.f).
- **Frequency**: `...name` in 21 test `.f` files, destructuring declarations in 5.
- **Approval**: none; `declare-pattern` is implement. **Release gate**: the escaped-ref smoke's `destruct` fails on it.
- **PROPOSAL**: one variadic/spread checkpoint (rest parameters, call spread, lambda rest), destructuring next to it.

### 7. Exports and modules

- **Rows**: G `export-struct`, `import-collision`, `import-dynamic`, `import-name-clash`, `module-bare-return`, `module-host-op:cat`, `module-host-op:exists`, `statement:Export`; N `import-limit`, `import-reinitialised`, `module-host-op:cp`, `module-host-op:file`, `module-host-op:getenv`, `module-host-op:is_dir`, `module-host-op:is_file`, `module-host-op:remove`.
- **Root cause**: `export(...)` in a directly run unit is not compiled; module initialisation with host operations, struct
  exports, name clashes, dynamic imports, repeated initialisation and deep import chains are refused.
- **Raise sites**: `statement:Export` compiler.cpp:156; `export-struct` module_graph.cpp:350; `import-name-clash`
  module_link.cpp:338; `module-bare-return` module_link.cpp:304; `module-host-op:*` module_host_op.h:29; `import-dynamic`
  (import resolver); `import-reinitialised` module_graph.cpp:271; `import-limit` module_graph.cpp:275.

```
fn(twice(n)) { return n * 2 }
export(twice)
print(twice(2))
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `4`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:2:1: not compiled for the VM (statement:Export): an export statement in this position is not supported by the Runtime 2.0 VM yet` (reason `statement:Export`).

`main.f`:

```
import("./m.f")
p := P()
print(p.v)
```

`m.f`:

```
struct(P) { v := 3 }
export(P)
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `3`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: m.f:2:1: not compiled for the VM (export-struct): exporting a struct from a module is not supported by the Runtime 2.0 VM yet` (reason `export-struct`).

`m.nft`:

```
x := 1
export(x)
```

`m2.nft`:

```
x := 2
export(x)
```

`main.f`:

```
cp("m2.nft", "m.nft")
import("m.nft")
print(x)
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `2`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:1: not compiled for the VM (module-host-op:cp): the host operation 'cp()' in a program with imports is not supported by the Runtime 2.0 VM yet` (reason `module-host-op:cp`).

`m.f`:

```
print("init")
k := 1
export(k)
```

`a.f`:

```
import("./m.f")
fn(fa()) { return k }
export(fa)
```

`main.f`:

```
import("./m.f")
import("./a.f")
print(k)
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `init\ninit\n1`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: a.f:1:1: not compiled for the VM (import-reinitialised): this form of import is not supported by the Runtime 2.0 VM yet` (reason `import-reinitialised`).

- **Affected gates**: script_import_syntax: `import-dynamic` 4; test-v410-sort-factory-parity: `import-name-clash` 1.
- **Corpus**: 56/0/43; `statement:Export` (45/0/33) is the largest single G reason. `export(` appears in 197 test `.f` files.
- **Approval**: `import-dynamic` cites D-037 (owner 2026-10-20: rejected before execution) with "G2 release approval
  pending", so it is G. `import-reinitialised`: D-041 #2 allows this refusal temporarily, only if the engines land
  separately and with a recorded removal condition; no such condition is recorded (owner question).
- **Impact**: high; a library file that is also run directly, and diamond imports (legacy initialises twice), are ordinary.
- **PROPOSAL**: compile `export(...)` in an entry unit as a no-op; admit module-init host operations in order (pinned by
  module_host_order); the owner states whether D-037 is the G2 approval for `import-dynamic` and records a removal
  condition (or not) for `import-reinitialised`.

### 8. Dynamic scope

```
fn(read_hidden()) { return hidden }
fn(mk(hidden)) { return read_hidden() }
print(mk(7))
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `7`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:28: not compiled for the VM (free-name): a name that is not declared in this scope is not supported by the Runtime 2.0 VM yet` (reason `free-name`).

- **Rows**: `free-name`, `free-name:dynamic-caller` (G); `free-name:undeclared` (D, inventory refinement).
- **Raise site**: globals.cpp:170-186 (plan) and the compiler's name resolution.
- **Affected gates**: test-v410-sort-factory-parity: `free-name` 1.
- **Corpus**: 1/0/2. **Approval**: none (2026-10-29 "keep refused" is cited; P1-2 found no decision text).
- **PROPOSAL**: rule legacy dynamic scope out of the release contract (class X), or keep it as a blocker; this also decides 5.2.

### 9. Reason literals in src/rt without a tsv row (P2-6)

A scan of `fail`/`refuse`/`add` calls and `reason =` assignments in `src/rt` finds 221 reason literals; 108 had no tsv
row at C111 (the review's scan found 83; this scan is wider and includes non-compile codes). Each has one disposition:

| Disposition | Count |
| --- | --- |
| session-only | 30 |
| N row added | 21 |
| not a compile refusal | 14 |
| not reached | 11 |
| unreachable | 10 |
| parser-shadowed | 5 |
| consequential-only | 3 |
| inventory refinement of `free-name` | 3 |
| eval-entry only | 2 |
| classify-all/session only | 2 |
| resolver-shadowed | 2 |
| unreachable as a refusal | 2 |
| unreachable in practice | 2 |
| dynamic family | 1 |
| **total** | **108** |

<details><summary>All 108</summary>

| Literal | Disposition |
| --- | --- |
| `analysis-limit:kinds` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) [C113 four-way probe: section 9.1] |
| `assign-catch-binding` | N row added (reachable; program in its evidence) |
| `assign-target` | consequential-only (tip corpus: consequential sightings only; probes give another reason first) |
| `await-shape` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) [C113 four-way probe: section 9.1] |
| `binary-operator` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) [C113 four-way probe: section 9.1] |
| `binding-call-reserved` | resolver-shadowed: binding-name-reserved fires first; legacy errors |
| `builtin-literal-arg:validate` | N row added (reachable; program in its evidence) |
| `builtin-plain-arg:` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found [C113 four-way probe: section 9.1] |
| `cleanup-incomplete` | not a compile refusal: an isolate-transfer, handle or process error code |
| `compound-operator` | N row added (C113: reached by the four-way probe, section 9.1) |
| `context-end` | not a compile refusal: an isolate-transfer, handle or process error code |
| `context:collection-empty` | N row added (C113: reached by the four-way probe, section 9.1) |
| `context:struct-field` | N row added (reachable; program in its evidence) |
| `coro-async-recursion` | classify-all/session only: needs parallel_tasks off (F12 default limits) or a session unit (refused as session:coroutine first) |
| `coro-capture` | classify-all/session only: needs parallel_tasks off (F12 default limits) or a session unit (refused as session:coroutine first) |
| `coro-future-flow:method` | N row added (reachable; program in its evidence) |
| `coro-future-flow:type` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found [C113 four-way probe: section 9.1] |
| `coro-observe:epoch` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found [C113 four-way probe: section 9.1] |
| `coro-task-loop` | consequential-only (tip corpus: consequential sightings only; probes give another reason first) |
| `coro-task-native:` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found [C113 four-way probe: section 9.1] |
| `coro-task-op:` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found [C113 four-way probe: section 9.1] |
| `coro-task-recursion` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found [C113 four-way probe: section 9.1] |
| `coro-thread-callee` | N row added (reachable; program in its evidence) |
| `entry-out-of-range` | not a compile refusal: an isolate-transfer, handle or process error code |
| `eval:error-aggregate-presentation` | eval-entry only (cp19 evaluated expressions, not script or session units) |
| `eval:quoted-callback` | eval-entry only (cp19 evaluated expressions, not script or session units) |
| `execution-error` | not a compile refusal: an isolate-transfer, handle or process error code |
| `expression-nesting` | parser-shadowed: the parser's opaque:nesting-limit fires first (row added in C111) |
| `expression:` | dynamic family (section 9.2): rows added for SafeMember, SafeIndex, assignment-target |
| `expression:assignment-target` | N row added (C113: reached by the four-way probe, section 9.1) |
| `file-capture` | not a compile refusal: an isolate-transfer, handle or process error code |
| `free-name:before-declaration` | inventory refinement of `free-name` (globals plan names the sub-case; the runner reports free-name, which has a row) |
| `free-name:dynamic-shadow` | inventory refinement of `free-name` (globals plan names the sub-case; the runner reports free-name, which has a row) |
| `free-name:not-one-top-level-binding` | inventory refinement of `free-name` (globals plan names the sub-case; the runner reports free-name, which has a row) |
| `import-limit` | N row added (reachable; program in its evidence) |
| `import-link` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) [C113 four-way probe: section 9.1] |
| `import-outside-project` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found [C113 four-way probe: section 9.1] |
| `import-parse-error` | not reached: a module parse error is reported as module-error:syntax (row exists), legacy-equal message [C113 four-way probe: section 9.1] |
| `import-reinitialised` | N row added (reachable; program in its evidence) |
| `interpolation:internal` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) [C113 four-way probe: section 9.1] |
| `interpolation:non-scalar` | N row added (reachable; program in its evidence) |
| `invalid-graph` | not a compile refusal: an isolate-transfer, handle or process error code |
| `invalid-input` | not a compile refusal: an isolate-transfer, handle or process error code |
| `lambda-capture` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) [C113 four-way probe: section 9.1] |
| `lambda-param` | parser-shadowed: opaque:legacy-runtime-error first; legacy errors (malformed parameter list, duplicate key, invalid signature) |
| `lambda-shape` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) [C113 four-way probe: section 9.1] |
| `limit:arguments` | unreachable in practice: capacity limit (0x7fff arguments, 0xffff atoms or lambdas) [C113 four-way probe: section 9.1] |
| `limit:atoms` | N row added (C113: reached by the four-way probe, section 9.1) |
| `limit:lambdas` | unreachable in practice: capacity limit (0x7fff arguments, 0xffff atoms or lambdas) [C113 four-way probe: section 9.1] |
| `limit:session-bindings` | session-only (the session compiler's contract checks) |
| `limit:session-protos` | session-only (the session compiler's contract checks) |
| `logical-operator` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) [C113 four-way probe: section 9.1] |
| `method:splice` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found [C113 four-way probe: section 9.1] |
| `object-duplicate-key` | parser-shadowed: opaque:legacy-runtime-error first; legacy errors (malformed parameter list, duplicate key, invalid signature) |
| `param-duplicate` | parser-shadowed: opaque:legacy-runtime-error first; legacy errors (malformed parameter list, duplicate key, invalid signature) |
| `param-type-unpredicted` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found [C113 four-way probe: section 9.1] |
| `parse-error` | not a compile refusal: the runner's stop reason for a parse error (legacy-identical diagnostic, CP10) |
| `path-in-call-argument:imported` | N row added (C113: reached by the four-way probe, section 9.1) |
| `path-in-index` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found [C113 four-way probe: section 9.1] |
| `pipe-capture` | not a compile refusal: an isolate-transfer, handle or process error code |
| `postfix-receiver` | consequential-only (tip corpus: consequential sightings only; probes give another reason first) |
| `proto-unavailable` | not a compile refusal: an isolate-transfer, handle or process error code |
| `push-receiver` | N row added (reachable; program in its evidence) |
| `refused-future` | not a compile refusal: an isolate-transfer, handle or process error code |
| `refused-handle` | not a compile refusal: an isolate-transfer, handle or process error code |
| `session:active-historical-call` | session-only (the session compiler's contract checks) |
| `session:binding-contract` | session-only (the session compiler's contract checks) |
| `session:binding-shape-contract` | session-only (the session compiler's contract checks) |
| `session:binding-struct-contract` | session-only (the session compiler's contract checks) |
| `session:call-alias-contract` | session-only (the session compiler's contract checks) |
| `session:call-kind-contract` | session-only (the session compiler's contract checks) |
| `session:call-signature` | session-only (the session compiler's contract checks) |
| `session:callable-flow` | session-only (the session compiler's contract checks) |
| `session:callable-rebind` | session-only (the session compiler's contract checks) |
| `session:capture-layout` | session-only (the session compiler's contract checks) |
| `session:captured-struct` | session-only (the session compiler's contract checks) |
| `session:coroutine` | session-only (the session compiler's contract checks) |
| `session:current-callable-proof` | session-only (the session compiler's contract checks) |
| `session:dynamic-shadow` | session-only (the session compiler's contract checks) |
| `session:element-contract` | session-only (the session compiler's contract checks) |
| `session:lambda-assignment-spelling` | session-only (the session compiler's contract checks) |
| `session:legacy-method-precedence` | session-only (the session compiler's contract checks) |
| `session:parse` | session-only (the session compiler's contract checks) |
| `session:redeclared` | session-only (the session compiler's contract checks) |
| `session:redefinition-callable-flow` | session-only (the session compiler's contract checks) |
| `session:redefinition-order` | session-only (the session compiler's contract checks) |
| `session:reserved-binding-contract` | session-only (the session compiler's contract checks) |
| `session:shadow-or-redeclare` | session-only (the session compiler's contract checks) |
| `session:struct-callable-flow` | session-only (the session compiler's contract checks) |
| `session:struct-dynamic-shadow` | session-only (the session compiler's contract checks) |
| `session:struct-name-conflict` | session-only (the session compiler's contract checks) |
| `session:struct-redeclared` | session-only (the session compiler's contract checks) |
| `session:widening-callable-flow` | session-only (the session compiler's contract checks) |
| `struct-assign` | N row added (reachable; program in its evidence) |
| `struct-member-clash` | N row added (reachable; program in its evidence) |
| `struct-method` | parser-shadowed: opaque:struct-body first (legacy errors 'struct fields require name := initializer') |
| `struct-name` | N row added (reachable; program in its evidence) |
| `struct-nested` | N row added (reachable; program in its evidence) |
| `struct-no-field` | N row added (reachable; program in its evidence) |
| `struct-redeclared` | N row added (reachable; program in its evidence) |
| `struct-shadows-fn` | N row added (reachable; program in its evidence) |
| `struct-this-binding` | resolver-shadowed: binding-name-reserved fires first; legacy errors |
| `throw-expression` | unreachable as a refusal: the parse error comes first and is legacy-identical [C113 four-way probe: section 9.1] |
| `trailing-comma` | unreachable as a refusal: the parse error comes first and is legacy-identical [C113 four-way probe: section 9.1] |
| `unary-operator` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) [C113 four-way probe: section 9.1] |
| `unknown-effects` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) [C113 four-way probe: section 9.1] |
| `unsupported-constant` | not a compile refusal: an isolate-transfer, handle or process error code |
| `unsupported-value` | not a compile refusal: an isolate-transfer, handle or process error code |

</details>

Probe notes for the "not reached" rows: `a.empty()` in a loop-called function, an async function returning epoch(),
a non-suspending loop, print and recursion in tasks, `splice` with a sorted argument, a reassigned parameter, an
index inside a call argument, and an import outside a project root all ran or gave another reason. **Observation**
(not a refusal): an async function awaiting its own recursive call runs on the VM (prints 0) where legacy errors
`future: return: await: expected future`; flagged for the owner (section 13).

The N evidence of every C111 N row is now a minimal program with its exact VM reason and legacy output (in the tsv).

#### 9.1 C113 four-way probes of every unreachable or not-reached disposition

Rule (confirmation review P2-A): a literal is called unreachable or not reached only after a probe in a script, a
module import (the program as the imported module), a `nift -i` session and a function called from a loop. Session
legacy exits are 0 by design (a session reports and continues). Cells: VM outcome; legacy exit.

| Literal | Probe | script | module | session | loop-called fn | C113 disposition |
| --- | --- | --- | --- | --- | --- | --- |
| `analysis-limit:kinds` | `x := 1 / i := 0 / while (i < 3) { x = [x]; i += 1 } / print(x.stringify())` | `context:assignment-error`; legacy rc=1 | `context:assignment-error`; legacy rc=1 | `session:binding-shape-contract`; legacy rc=0 | `context:assignment-error`; legacy rc=1 | not reached (other reasons first) |
| `await-shape` | `x := await / print(1)` | `unresolved-name`; legacy rc=1 | `unresolved-name`; legacy rc=1 | `unresolved-name`; legacy rc=0 | `prepared-decl-type`; legacy rc=1 | not reached (a bare `await` is a name) |
| `binary-operator` | `print(1 ^ 2)` | `opaque:legacy-runtime-error`; legacy rc=1 | `import-opaque:legacy-runtime-error`; legacy rc=1 | `session:opaque`; legacy rc=0 | `opaque:legacy-runtime-error`; legacy rc=1 | parser-shadowed (`^` is opaque) |
| `compound-operator` | `x := 6 / x &= 3 / print(x)` | `compound-operator`; legacy rc=1 | `compound-operator`; legacy rc=1 | runs; legacy rc=0 | `compound-operator`; legacy rc=1 | **reached: N row added** (`&=`, `\|=`, `^=`) |
| `interpolation:internal` | `x := "" / print("$[x]") / print("")` | runs; legacy rc=0 | runs; legacy rc=0 | `interpolation:unknown-type`; legacy rc=0 | runs; legacy rc=0 | not reached (an empty string is a constant) |
| `lambda-capture` | `i := 0 / while (i < 1) { j := i / f := () => j / print(f()) / i += 1 }` | runs; legacy rc=0 | runs; legacy rc=0 | runs; legacy rc=0 | `prepared-decl-type`; legacy rc=0 | not reached (defensive arm) |
| `lambda-shape` | `f := () => / print(1)` | `opaque:legacy-runtime-error`; legacy rc=0 | `import-opaque:legacy-runtime-error`; legacy rc=0 | `session:opaque`; legacy rc=0 | `opaque:legacy-runtime-error`; legacy rc=0 | parser-shadowed (`opaque:legacy-runtime-error`; legacy runs: row exists) |
| `logical-operator` | `print(null ?? 1) / print(1 and 2)` | `opaque:legacy-runtime-error`; legacy rc=1 | `import-opaque:legacy-runtime-error`; legacy rc=1 | `session:opaque`; legacy rc=0 | `opaque:legacy-runtime-error`; legacy rc=1 | parser-shadowed |
| `unary-operator` | `print(~1)` | `opaque:legacy-runtime-error`; legacy rc=1 | `import-opaque:legacy-runtime-error`; legacy rc=1 | `session:opaque`; legacy rc=0 | `opaque:legacy-runtime-error`; legacy rc=1 | parser-shadowed |
| `unknown-effects` | `fn(f(a)) { a.sort((x, y) => x < y) / return a } / print(f([2, 1]).stringify())` | `method:sort`; legacy rc=0 | `method:sort`; legacy rc=0 | `method:sort`; legacy rc=0 | `method:sort`; legacy rc=0 | shadowed by `method:sort` (row exists) |
| `import-link` | `import("./m.f") / print(x)` | `statement:Command`; legacy rc=0 | `statement:Command`; legacy rc=0 | `statement:Import`; legacy rc=0 | `import-nested`; legacy rc=0 | not reached (invariant: every module parses alone) |
| `throw-expression` | `try { throw } catch(e) { print("c") }` | error: throw requires an Error expression; legacy rc=1 | `module-error:syntax`; legacy rc=1 | `session:parse`; legacy rc=0 | error: throw requires an Error expression; legacy rc=1 | unreachable as a refusal: legacy-identical parse error first |
| `trailing-comma` | `a := [1, 2,] / print(a.size())` | error: expression cannot be empty; legacy rc=1 | `module-error:syntax`; legacy rc=1 | `session:opaque`; legacy rc=0 | error: expression cannot be empty; legacy rc=1 | unreachable as a refusal: legacy-identical parse error first |
| `limit:arguments` | `print(1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,...` | `static-error:arity`; legacy rc=1 | `static-error:arity`; legacy rc=1 | `static-error:arity`; legacy rc=0 | `static-error:arity`; legacy rc=1 | shadowed by `static-error:arity` (33000 arguments) |
| `limit:atoms` | `o := {"k0": 0, "k1": 1, "k2": 2, "k3": 3, "k4": 4, "k5": 5, "k6": 6, "k7": 7, "k8": 8, "k9...` | `limit:atoms`; legacy rc=0 | `limit:atoms`; legacy rc=0 | `limit:atoms`; legacy rc=0 | `limit:atoms`; legacy rc=0 | **reached: N row added** (70000 keys; legacy runs) |
| `limit:lambdas` | `f0 := () => 0 / f1 := () => 1 / f2 := () => 2 / f3 := () => 3 / f4 := () => 4 / f5 := () =...` | timeout (120 s); legacy rc=-9 | timeout (120 s); legacy rc=-9 | `binding-name-reserved`; legacy rc=-9 | timeout (120 s); legacy rc=-9 | not reached: 66000 lambdas time out on both engines; session hits `binding-name-reserved` (`f32`) |
| `limit:too many constants` | `x := ["s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "s12", "s1...` | `limit:too many registers`; legacy rc=0 | `limit:too many registers`; legacy rc=0 | `limit:too many registers`; legacy rc=0 | `limit:too many registers`; legacy rc=0 | shadowed by `limit:too many registers` (row added) |
| `limit:too many registers` | `fn(g()) { return 1 } / print([g(), g(), g(), g(), g(), g(), g(), g(), g(), g(), g(), g(), ...` | `limit:too many registers`; legacy rc=0 | `limit:too many registers`; legacy rc=0 | `limit:too many registers`; legacy rc=0 | `limit:too many registers`; legacy rc=0 | **reached: N row added** (legacy runs) |
| `limit:jump distance exceeds 16 bits` | `x := 0 / if (true) { / x += 1 / x += 1 / x += 1 / x += 1 / x += 1 / x += 1 / x += 1 / x +=...` | runs; legacy rc=0 | runs; legacy rc=0 | `limit:jump distance exceeds 16 bits`; legacy rc=0 | runs; legacy rc=0 | **reached in a session: N row added** (legacy runs) |
| `limit:too many struct types` | `struct(S0) { v := 1 } / struct(S1) { v := 1 } / struct(S2) { v := 1 } / struct(S3) { v := ...` | `type:member`; legacy rc=1 | `type:member`; legacy rc=1 | timeout (120 s); legacy rc=-9 | `struct-nested`; legacy rc=1 | not reached: 65600 structs give `type:member` / `struct-nested`; session times out |
| `coro-future-flow:type` | `fn[async](af(x)) { return x } / x := af(1) / if (true) { x = 1 } / print(await x)` | error: cannot assign int to string binding 'x'; legacy rc=1 | `coro-module-init-task`; legacy rc=1 | `session:coroutine`; legacy rc=0 | error: cannot assign int to string binding 'x'; legacy rc=1 | not reached (legacy-equal type error first) |
| `coro-observe:epoch` | `fn(w()) { return epoch() > 0 } / t := thread(w) / print(t.join())` | runs; legacy rc=0 | `coro-module-init-task`; legacy rc=0 | `session:coroutine`; legacy rc=0 | runs; legacy rc=0 | classify-all only (F12 limits); runs on the runner |
| `coro-task-recursion` | `fn(w(n)) { if (n == 0) { return 0 } / return w(n - 1) } / t := thread(w, 2) / print(t.join...` | runs; legacy rc=0 | `coro-module-init-task`; legacy rc=0 | `session:coroutine`; legacy rc=0 | runs; legacy rc=0 | classify-all only; runs |
| `coro-task-native:` | `fn(w()) { print("x") / return 1 } / t := thread(w) / print(t.join())` | runs; legacy rc=0 | `coro-module-init-task`; legacy rc=0 | `session:coroutine`; legacy rc=0 | runs; legacy rc=0 | classify-all only; runs |
| `coro-task-op:` | `fn(w()) { a := [1] / a.push(2) / return a.size() } / t := thread(w) / print(t.join())` | runs; legacy rc=0 | `coro-module-init-task`; legacy rc=0 | `session:coroutine`; legacy rc=0 | runs; legacy rc=0 | classify-all only; runs |
| `coro-async-recursion` | `fn[async](f(n)) { if (n == 0) { return 0 } / return await f(n - 1) } / print(await f(2))` | runs; legacy rc=1 | `coro-module-init-task`; legacy rc=1 | `session:coroutine`; legacy rc=0 | runs; legacy rc=1 | classify-all only; the VM runs where legacy errors (section 13.2 item 9) |
| `coro-capture` | `k := 2 / f := async (x) => x * k / print(await f(1))` | runs; legacy rc=0 | `coro-module-init-task`; legacy rc=0 | `session:coroutine`; legacy rc=0 | `prepared-decl-type`; legacy rc=0 | classify-all or session only (`session:coroutine` first) |
| `method:splice` | `a := [3, 1] / b := [2] / a.splice(0, 1, b.sort()) / print(a.stringify())` | runs; legacy rc=0 | runs; legacy rc=0 | runs; legacy rc=0 | runs; legacy rc=0 | not reached; runs |
| `param-type-unpredicted` | `fn(f(x)) { if (x > 0) { return x } / return 0 } / print(f(1)) / print(f(2.5))` | runs; legacy rc=0 | runs; legacy rc=0 | runs; legacy rc=0 | runs; legacy rc=0 | not reached; runs |
| `path-in-index` | `o := {"k": [0]} / a := [5] / print(a[o.k[0]])` | `opaque:legacy-runtime-error`; legacy rc=1 | `import-opaque:legacy-runtime-error`; legacy rc=1 | `session:opaque`; legacy rc=0 | `opaque:legacy-runtime-error`; legacy rc=0 | parser-shadowed |
| `path-in-call-argument:imported` | `import("./m.f") / fn(id(x)) { return x } / print(id(t.k[0]))` | `path-in-call-argument:imported`; legacy rc=0 | `path-in-call-argument:imported`; legacy rc=0 | `statement:Import`; legacy rc=0 | `path-in-call-argument:imported`; legacy rc=0 | **reached: N row added** (legacy runs) |
| `builtin-plain-arg:` | `fn(f(x)) { return "a".contains(x) } / print(f("a")) / print(f(1))` | error: contains: argument must be a string; legacy rc=1 | error: import: contains: argument must be a string; legacy rc=1 | runs; legacy rc=0 | error: contains: argument must be a string; legacy rc=1 | not reached: legacy-equal error |
| `import-outside-project` | `import("/dev/null") / print(1)` | `import-error:missing`; legacy rc=1 | `import-error:missing`; legacy rc=1 | `statement:Import`; legacy rc=0 | `import-nested`; legacy rc=1 | not reached: `import-error:missing` (row exists) first |
| `import-parse-error` | `import("./m.f") / print(1)` | `module-error:syntax`; legacy rc=1 | `module-error:syntax`; legacy rc=1 | `statement:Import`; legacy rc=0 | `import-nested`; legacy rc=1 | not reached: `module-error:syntax` (row exists) |
| `opaque:deferred-translation` | `f := (x) => @fn / print(1)` | `opaque:legacy-runtime-error`; legacy rc=0 | `import-opaque:legacy-runtime-error`; legacy rc=0 | `session:opaque`; legacy rc=0 | `opaque:legacy-runtime-error`; legacy rc=0 | not reached: `opaque:legacy-runtime-error` / `opaque:template-text` first |
| `context:collection-empty` | `m := map() / i := 0 / while (i < 1) { /   print(m.empty()) /   i += 1 / }` | `context:collection-empty`; legacy rc=0 | `context:collection-empty`; legacy rc=0 | `context:collection-empty`; legacy rc=0 | `context:collection-empty`; legacy rc=0 | **reached: N row added** (P2-B) |
| `expression:assignment-target` | `a := [1] / print(a[0] += 5)` | `expression:assignment-target`; legacy rc=0 | `expression:assignment-target`; legacy rc=0 | `expression:assignment-target`; legacy rc=0 | `expression:assignment-target`; legacy rc=0 | **reached: row added** (owner-excluded checkpoint-1 form, P2-B) |
| `coro-future-flow:Call (C112 probed as `:type`)` | `fn[async](af(x)) { return x } / fn(w(x)) { return x } / x := af(1) / if (true) { x = threa...` | `coro-future-flow:Call`; legacy rc=0 | `coro-module-init-task`; legacy rc=0 | `session:coroutine`; legacy rc=0 | `coro-future-flow:Call`; legacy rc=0 | **reached: N row added** (P2-B) |

The C112 categories "session-only" (30), "not a compile refusal" (14), "inventory refinement" (3), "eval-entry only" (2)
and the parser- or resolver-shadowed rows (7) are unchanged; the confirmation review re-sampled several of them.

#### 9.2 Reason families built at run time

The literal scan cannot see names built by concatenation. Every such emit site in `src/rt`, its producible suffixes and
the C113 probes (1010 generated programs, VM and legacy, script context; module host operations need imports):

| Family | Emit sites | Producible suffixes (source of the enumeration) | Probed | Rows added |
| --- | --- | --- | --- | --- |
| `type:<op>` | compiler_expr.cpp:165,170,374; literals in compiler_*.cpp | op_name of Add..Mod, Lt..Ge (+ - * / % < <= > >=) and 11 literal names | all | 4 (P2-B) |
| `coro-future-flow:<parent kind>` | compiler_coro.cpp:163 (and :Member, :method, :type literals) | node_kind_name of any parent of a future holder that `allowed()` rejects | 20 parent kinds | 9 + 2 (P2-B) |
| `expression:<kind>` | compiler_expr.cpp:138 | node kinds without an arm in `expr`: SafeMember, SafeIndex, Spread, Property, StringPart, statement kinds | 9 forms | 2 |
| `statement:<kind>` | compiler.cpp:156 | statement kinds without an arm: Block, FnDecl, StructDecl, EnumDecl, Import, Export, Command, TemplateStmt, Pattern | 7 forms | 1 |
| `opaque:<reason>`, `import-opaque:<reason>` | script_runner.cpp:278, module_graph.cpp:326 | the 6 OpaqueReason names | 6 + 5 imported | 4 (import-opaque) + 1 four-way |
| `builtin-arity:<name>` | compiler_native_args.cpp:25, compiler_keyed.cpp:105,196 | every built-in with fixed max (builtin_list.inc, 187 specs): max+1 and min-1 arguments | all specs | 116 |
| `builtin-receiver:<name>` | compiler_dispatch.cpp:41,44, compiler_call.cpp:215 | every String/Array/Object/Number/Map/Set/Stack/Bool method on another kind | all specs | 68 |
| `builtin-lazy-args:<name>`, `builtin-default:get` | compiler_native_args.cpp:33-34 | every may-fail built-in with two or more arguments, second argument a call | all specs | 1 + 1 |
| `builtin-bytes:<name>`, `builtin-plain-arg:<name>`, `builtin-literal-arg:validate` | compiler_native_args.cpp:9,16,20 | bytes producers; unknown-kind arguments; validate | via specs | 0 (existing rows; plain-arg not reached, 9.1) |
| `callable:<name>` | compiler_thread.cpp:22 | any unbound callee: unbounded; enumerated over legacy_names.cpp kNames (225 names) | 225 | 10 exact + `callable:*` family (absorbs 101) |
| `method:<name>` | compiler_call.cpp:204, compiler_keyed.cpp:98,115,188,191, compiler_error.cpp:39, compiler_array_mutation.cpp:9,11, compiler_collection_lambda.cpp:8 | any method name not bound for the receiver kind: unbounded; enumerated over kMethods on string, array and object receivers | all kMethods × 3 | 37 exact; `method:*` covers the rest |
| `module-host-op:<name>` | module_host_op.h:29 | every host built-in (HostOpen..HostSecureRandomBytes, 26) in a program with imports | 26 | 19 |
| `limit:<codegen error>` | compiler.cpp:40 (codegen.cpp:16,48,59,92,116) | too many registers / struct types / constants, register-operand-constant, jump distance | 4 (9.1) | 2 (four-way) |
| removed `async(...)` / `await(...)` forms | compiler_thread.cpp:19-21 (the message is the reason) | 2 | 2 | 2 |
| `coro-blocking-host:<name>`, `coro-observe:*`, `coro-task-*:<x>` | compiler_coro_admit.cpp:131-249 | host built-ins, future ops and opcodes in tasks | 9.1 | 0: classify-all only (F12 limits); `coro-blocking-host:sha256_file` appears consequentially in the C113 collect |
| `presentation-struct-method:<name>`, `collection-*:<name>`, `invocation-assign:<name>` | compiler_presentation.cpp:40, compiler_collection*.cpp, compiler_invocation.cpp:81 | presentation methods; collection methods; invocation names | - | 0: family rows exist |
| refinements (`free-name:*`, `assign-free-name:*`, `unresolved-name:legacy-binding`) | refusal_inventory.cpp:65-80 | inventory sub-names of a runner reason | - | 0 (the runner reports the base reason, which has a row) |

Not reachable (probed, C114 natural spellings included): `coro-future-flow` parents Unary, Array, Declare, If, While,
Return, Index (`f := xs[0]`), Call (`id(af(1))`), Member and a property store run on the VM; Ternary and `await x ?? y`
give `opaque:irregular-legacy-parse`, a spread argument gives `spread`. C113 was wrong that the Coalesce parent is
unreachable: `f := af(1) ?? af(2)` (and `thread(w, 1) ?? 0`) gives `coro-future-flow:Coalesce`, legacy prints 1 (row
added in C114); `g := f == f` makes `:Binary` a program legacy runs (true). `expression:` Spread,
Property, Number, Coalesce and assignment/increment expressions run or give other reasons; `statement:` Block,
EnumDecl, StructDecl in a block, Import in a function, Pattern and `defer` give `opaque:legacy-runtime-error`,
a legacy-identical parse error, `struct-nested`, `import-nested` or `declare-pattern` (rows exist). Every
producible `builtin-*`, `module-host-op` and legacy-name `callable`/`method` suffix the probes reached has an exact row.

**`type:*` absorbed** (before C113, no exact row): `type:/`, `type:<=`, `type:>=`, `type:for-source-union`; all
emitted (P2-B probes), all now have rows. It absorbs no other producible reason: every op_name suffix and every
literal `type:` name has an exact row (`type:for-source` retired, not reached; `type:element-assignment` restored).

**`method:*` absorbs**: before C113 the 37 legacy method names below (all emitted, legacy errors) and `method:increment`
(emitted). The 37 now have exact rows; `method:increment`'s retired row is deleted, so `method:*` absorbs it and any
other identifier used as a method.

**`callable:*` absorbs** the 101 legacy special names that legacy itself reports as undefined callables (emitted, legacy-equal error), and any other undeclared name:

`Handover`, `OLDPWD`, `PWD`, `SHLVL`, `_`, `amplify`, `append`, `atomic`, `azure`, `bg`, `bool`, `break`, `buffer`, `callback_i64`, `catch`, `cloudflare`, `const`, `content`, `continue`, `css`, `cstr`, `defer`, `del`, `else`, `enum`, `eof`, `export`, `f32`, `f64`, `fg`, `firebase`, `first`, `flush`, `fn`, `for`, `highlight`, `html`, `i16`, `i32`, `i64`, `i8`, `if`, `immut`, `import`, `info`, `input`, `insert`, `insert_after`, `insert_before`, `jobs`, `js`, `json`, `jsx`, `lambda`, `loop`, `modified`, `name`, `netlify`, `path`, `prepend`, `prettify`, `project`, `ptr`, `read_all`, `read_all_bytes`, `read_bytes`, `read_line`, `read_val`, `render`, `replace`, `replace_once`, `return`, `revert`, `save`, `script`, `section`, `seek`, `self`, `split`, `struct`, `supabase`, `svg`, `tell`, `this`, `throw`, `title`, `try`, `u16`, `u32`, `u64`, `u8`, `unwatch`, `vercel`, `void`, `wait`, `watch`, `while`, `write`, `write_line`, `write_val`, `xml`

<details><summary>The 276 dynamic-family rows added (reason; legacy on the probe)</summary>

- **builtin-arity** (116): `builtin-arity:arch` (errors), `builtin-arity:atomic<bool>` (errors), `builtin-arity:atomic<int>` (errors), `builtin-arity:attr_escape` (errors), `builtin-arity:cancel` (errors), `builtin-arity:cat` (errors), `builtin-arity:cd` (errors), `builtin-arity:ceil` (errors), `builtin-arity:channel` (errors), `builtin-arity:chunk` (errors), `builtin-arity:close` (errors), `builtin-arity:cp` (errors), `builtin-arity:deepcopy` (errors), `builtin-arity:drop` (errors), `builtin-arity:empty` (errors), `builtin-arity:ends_with` (errors), `builtin-arity:entries` (errors), `builtin-arity:env` (errors), `builtin-arity:eof` (errors), `builtin-arity:epoch` (errors), `builtin-arity:exists` (errors), `builtin-arity:first` (errors), `builtin-arity:flatten` (errors), `builtin-arity:floor` (errors), `builtin-arity:flush` (errors), `builtin-arity:from_entries` (errors), `builtin-arity:get` (errors), `builtin-arity:getenv` (errors), `builtin-arity:hardware_concurrency` (errors), `builtin-arity:has` (errors), `builtin-arity:highlight` (errors), `builtin-arity:html_escape` (errors), `builtin-arity:indexOf` (errors), `builtin-arity:index_of` (errors), `builtin-arity:is_array` (errors), `builtin-arity:is_bool` (errors), `builtin-arity:is_bytes` (errors), `builtin-arity:is_collection` (errors), `builtin-arity:is_dir` (errors), `builtin-arity:is_enum` (errors), `builtin-arity:is_file` (errors), `builtin-arity:is_float` (errors), `builtin-arity:is_function` (errors), `builtin-arity:is_int` (errors), `builtin-arity:is_null` (errors), `builtin-arity:is_number` (errors), `builtin-arity:is_object` (errors), `builtin-arity:is_string` (errors), `builtin-arity:is_struct` (errors), `builtin-arity:last_index_of` (errors), `builtin-arity:length` (errors), `builtin-arity:lock` (errors), `builtin-arity:locked` (errors), `builtin-arity:ls` (errors), `builtin-arity:make_dir` (errors), `builtin-arity:map` (errors), `builtin-arity:max` (errors), `builtin-arity:merge` (errors), `builtin-arity:merge_deep` (errors), `builtin-arity:min` (errors), `builtin-arity:mkdir` (errors), `builtin-arity:modified` (errors), `builtin-arity:mutex` (errors), `builtin-arity:mv` (errors), `builtin-arity:omit` (errors), `builtin-arity:open` (errors), `builtin-arity:os` (errors), `builtin-arity:path` (errors), `builtin-arity:pick` (errors), `builtin-arity:platform` (errors), `builtin-arity:prettify` (errors), `builtin-arity:pwd` (errors), `builtin-arity:range` (errors), `builtin-arity:read` (errors), `builtin-arity:read_all` (errors), `builtin-arity:read_line` (errors), `builtin-arity:recv` (errors), `builtin-arity:remove` (errors), `builtin-arity:replace` (errors), `builtin-arity:revert` (errors), `builtin-arity:rm` (errors), `builtin-arity:round` (errors), `builtin-arity:same` (errors), `builtin-arity:save` (errors), `builtin-arity:seek` (errors), `builtin-arity:send` (errors), `builtin-arity:setenv` (errors), `builtin-arity:size` (errors), `builtin-arity:sleep` (errors), `builtin-arity:slice` (errors), `builtin-arity:stack` (errors), `builtin-arity:starts_with` (errors), `builtin-arity:stat` (errors), `builtin-arity:substr` (errors), `builtin-arity:sum` (errors), `builtin-arity:take` (errors), `builtin-arity:tell` (errors), `builtin-arity:to_double` (errors), `builtin-arity:to_int` (errors), `builtin-arity:to_lower` (errors), `builtin-arity:to_string` (errors), `builtin-arity:to_upper` (errors), `builtin-arity:touch` (errors), `builtin-arity:trim_end` (errors), `builtin-arity:trim_start` (errors), `builtin-arity:try_lock` (errors), `builtin-arity:type` (errors), `builtin-arity:unique` (errors), `builtin-arity:unlock` (errors), `builtin-arity:unsetenv` (errors), `builtin-arity:url_encode` (errors), `builtin-arity:validate` (errors), `builtin-arity:values` (errors), `builtin-arity:which` (errors), `builtin-arity:write` (errors), `builtin-arity:write_line` (errors)
- **builtin-default** (1): `builtin-default:get` (errors)
- **builtin-lazy-args** (1): `builtin-lazy-args:slice` (errors)
- **builtin-receiver** (68): `builtin-receiver:abs` (errors), `builtin-receiver:ceil` (errors), `builtin-receiver:chunk` (errors), `builtin-receiver:close` (errors), `builtin-receiver:compare_exchange` (errors), `builtin-receiver:contains` (errors), `builtin-receiver:copy` (errors), `builtin-receiver:decode` (errors), `builtin-receiver:drop` (errors), `builtin-receiver:encode` (errors), `builtin-receiver:ends_with` (errors), `builtin-receiver:entries` (errors), `builtin-receiver:eof` (errors), `builtin-receiver:exchange` (errors), `builtin-receiver:exists` (errors), `builtin-receiver:fetch_add` (errors), `builtin-receiver:fetch_sub` (errors), `builtin-receiver:flatten` (errors), `builtin-receiver:floor` (errors), `builtin-receiver:flush` (errors), `builtin-receiver:from_entries` (errors), `builtin-receiver:front` (errors), `builtin-receiver:get` (errors), `builtin-receiver:has` (errors), `builtin-receiver:indexOf` (errors), `builtin-receiver:last` (errors), `builtin-receiver:last_index_of` (errors), `builtin-receiver:load` (errors), `builtin-receiver:lock` (errors), `builtin-receiver:locked` (errors), `builtin-receiver:max` (errors), `builtin-receiver:merge` (errors), `builtin-receiver:merge_deep` (errors), `builtin-receiver:min` (errors), `builtin-receiver:modified` (errors), `builtin-receiver:move` (errors), `builtin-receiver:omit` (errors), `builtin-receiver:open` (errors), `builtin-receiver:path` (errors), `builtin-receiver:pick` (errors), `builtin-receiver:read` (errors), `builtin-receiver:read_all` (errors), `builtin-receiver:read_line` (errors), `builtin-receiver:remove` (errors), `builtin-receiver:replace` (errors), `builtin-receiver:revert` (errors), `builtin-receiver:round` (errors), `builtin-receiver:save` (errors), `builtin-receiver:seek` (errors), `builtin-receiver:set` (errors), `builtin-receiver:slice` (errors), `builtin-receiver:split` (errors), `builtin-receiver:starts_with` (errors), `builtin-receiver:store` (errors), `builtin-receiver:take` (errors), `builtin-receiver:tell` (errors), `builtin-receiver:to_int` (errors), `builtin-receiver:to_lower` (errors), `builtin-receiver:top` (errors), `builtin-receiver:trim` (errors), `builtin-receiver:trim_end` (errors), `builtin-receiver:trim_start` (errors), `builtin-receiver:try_lock` (errors), `builtin-receiver:unique` (errors), `builtin-receiver:unlock` (errors), `builtin-receiver:values` (errors), `builtin-receiver:write` (errors), `builtin-receiver:write_line` (errors)
- **callable** (10): `callable:build_repair` (errors), `callable:ffi_buffer` (errors), `callable:ffi_bytes` (errors), `callable:ffi_sizeof` (errors), `callable:ffi_snapshot_bytes` (errors), `callable:ffi_struct` (errors), `callable:package_path` (errors), `callable:page` (errors), `callable:prique` (errors), `callable:queue` (errors)
- **coro-future-flow** (10): `coro-future-flow:Binary` (errors), `coro-future-flow:Coalesce` (runs), `coro-future-flow:CompoundAssign` (errors), `coro-future-flow:ExprStmt` (runs), `coro-future-flow:ForBinding` (errors), `coro-future-flow:IncDec` (errors), `coro-future-flow:Logical` (runs), `coro-future-flow:Property` (runs), `coro-future-flow:String` (runs), `coro-future-flow:Throw` (errors)
- **expression** (2): `expression:SafeIndex` (runs), `expression:SafeMember` (runs)
- **free-name** (1): `free-name:legacy-binding` (errors)
- **import-opaque** (5): `import-opaque:irregular-legacy-parse` (runs), `import-opaque:legacy-runtime-error` (errors), `import-opaque:nesting-limit` (runs), `import-opaque:struct-body` (errors), `import-opaque:template-text` (runs)
- **limit** (4): `limit:atoms` (runs), `limit:invocation-arguments` (runs), `limit:jump distance exceeds 16 bits` (runs), `limit:too many registers` (runs)
- **message** (8): `async(callable, ...) was removed; declare an async function/lambda and call it directly` (errors), `await(...) was removed; use 'await future' or 'await async_fn(args)'` (errors), `compound-operator` (errors), `coro-collection-callback` (errors), `coro-main-cell` (errors), `fn-redeclared` (runs), `fn-shape` (errors), `too-many-functions` (runs)
- **method** (37): `method:all` (errors), `method:any` (errors), `method:append` (errors), `method:cat` (errors), `method:clear` (errors), `method:count_by` (errors), `method:cwd` (errors), `method:env` (errors), `method:file` (errors), `method:find` (errors), `method:find_index` (errors), `method:group_by_each` (errors), `method:index_by` (errors), `method:insert` (errors), `method:insert_after` (errors), `method:insert_before` (errors), `method:max_by` (errors), `method:min_by` (errors), `method:mv` (errors), `method:partition` (errors), `method:pop` (errors), `method:prepend` (errors), `method:read_all_bytes` (errors), `method:read_bytes` (errors), `method:read_val` (errors), `method:reduce` (errors), `method:reset` (errors), `method:reverse` (errors), `method:running` (errors), `method:splice` (errors), `method:stat` (errors), `method:stderr` (errors), `method:stdin` (errors), `method:stdout` (errors), `method:unique_by` (errors), `method:write_bytes` (errors), `method:write_val` (errors)
- **module-host-op** (19): `module-host-op:arch` (runs), `module-host-op:cd` (errors), `module-host-op:env` (errors), `module-host-op:epoch` (runs), `module-host-op:hardware_concurrency` (runs), `module-host-op:ls` (errors), `module-host-op:make_dir` (errors), `module-host-op:mkdir` (errors), `module-host-op:open` (runs), `module-host-op:os` (runs), `module-host-op:platform` (runs), `module-host-op:pwd` (runs), `module-host-op:setenv` (runs), `module-host-op:sha256_file` (runs), `module-host-op:sleep` (errors), `module-host-op:stat` (errors), `module-host-op:touch` (runs), `module-host-op:unsetenv` (runs), `module-host-op:which` (runs)
- **path-in-call-argument** (1): `path-in-call-argument:imported` (runs)
- **session** (3): `session:capture-contract` (errors), `session:function-assignment` (errors), `session:opaque` (errors)
- **statement** (2): `statement:FnDecl` (runs), `statement:Try` (errors)

</details>

#### 9.3 Retired rows under the four-way rule

| Row | Probe | script | module | session | loop-called fn | C113 status |
| --- | --- | --- | --- | --- | --- | --- |
| `type:print` | `fn(each(xs)) { out := []; for(x : xs) { out.push(x * 2) } return out } / print(each([1, 2,...` | error: print: value is not directly renderable; legacy rc=1 | error: import: print: value is not directly renderable; legacy rc=1 | runs; legacy rc=0 | error: print: value is not directly renderable; legacy rc=1 | stays retired (no emit site; the VM raises legacy's error) |
| `aliasing` | `fn(g()) { a := []; a.push(1); return a } / fn(o()) { return {"a": 1} } / g() / o() / a := ...` | runs; legacy rc=0 | runs; legacy rc=0 | runs; legacy rc=0 | `prepared-decl-type`; legacy rc=0 | stays retired (no emit site) |
| `reference-guard:*` | `a := [[1]] / b := a[0] / b.push(2) / a[0].push(3) / print(a.stringify()) / print(b.stringi...` | `opaque:legacy-runtime-error`; legacy rc=1 | `import-opaque:legacy-runtime-error`; legacy rc=1 | `session:opaque`; legacy rc=0 | `opaque:legacy-runtime-error`; legacy rc=1 | stays retired (no emit site) |
| `type:for-source` | `fn(f(x)) { for (e : x) { print(e) } / return 0 } / f([1, 2])` | runs; legacy rc=0 | runs; legacy rc=0 | runs; legacy rc=0 | runs; legacy rc=0 | **restored to PENDING-OWNER, G (C114)**: an uncalled lambda emits it (9.4) |
| `type:for-source (args)` | `for (a : args) { print(a) } / print("n")` | runs; legacy rc=0 | `import-scope:invocation-init`; legacy rc=1 | runs; legacy rc=0 | runs; legacy rc=0 | (same row) |
| `coro-module` | `import("./m.f") / fn[async](af(x)) { return x + k } / print(await af(1))` | `coro-module-capture`; legacy rc=0 | `coro-module-init-task`; legacy rc=0 | `statement:Import`; legacy rc=0 | `coro-module-capture`; legacy rc=0 | stays retired (site exists, not reached) |
| `coro-module (thread)` | `import("./m.f") / fn(w(x)) { return x + k } / t := thread(w, 1) / print(t.join())` | `coro-module-capture`; legacy rc=0 | `coro-module-init-task`; legacy rc=0 | `statement:Import`; legacy rc=0 | `coro-module-capture`; legacy rc=0 | (same row) |
| `method:increment` | `x := 5 / x.increment()` | `method:increment`; legacy rc=1 | `method:increment`; legacy rc=1 | `method:increment`; legacy rc=0 | `method:increment`; legacy rc=1 | **row deleted** (emitted; `method:*` covers it) |
| `struct-no-method` | `struct(S) { v := 1 } / s := S() / print(s.nope())` | error: struct has no method: nope; legacy rc=1 | error: import: struct has no method: nope; legacy rc=1 | runs; legacy rc=0 | `struct-no-method`; legacy rc=1 | **restored to PENDING-OWNER, G** |
| `struct-no-method (string name)` | `struct(S) { v := 1 } / s := S() / print(s.trim())` | `struct-no-method`; legacy rc=1 | `struct-no-method`; legacy rc=1 | `struct-no-method`; legacy rc=0 | `struct-no-method`; legacy rc=1 | (same row) |
| `type:element-assignment (variable key)` | `o := {"a": 1} / k := "a" / o[k] = 2 / print(o["a"])` | runs; legacy rc=0 | runs; legacy rc=0 | runs; legacy rc=0 | runs; legacy rc=0 | (same row) |
| `type:element-assignment (mixed receiver)` | `fn(f(c)) { if (c) { return [1] } / return {"a": 1} } / x := f(true) / x[0] = 5 / print(x.s...` | `type:element-assignment`; legacy rc=0 | `type:element-assignment`; legacy rc=0 | `type:element-assignment`; legacy rc=0 | `type:element-assignment`; legacy rc=0 | **restored to PENDING-OWNER, G** |
| `type:element-assignment (key kinds)` | `fn(k(c)) { if (c) { return 0 } / return "a" } / a := [1] / a[k(true)] = 5 / print(a[0])` | `index-target`; legacy rc=0 | `index-target`; legacy rc=0 | `index-target`; legacy rc=0 | `index-target`; legacy rc=0 | (same row) |
| `builtin-receiver:stringify` | `f := (v) => v.stringify() / print(f({"a": 1}))` | runs; legacy rc=0 | runs; legacy rc=0 | `builtin-receiver:stringify`; legacy rc=0 | runs; legacy rc=0 | **restored to PENDING-OWNER, G (sessions)** |

A fresh C114 collect sees none of the 4 retired reasons and no `method:increment`.

#### 9.4 Fifth context: bodies that are never called (C114)

The confirmation review found `type:for-source` emitted only inside a lambda that is never called. Every retired row and
every unreachable or not-reached claim was re-probed with its program as the body of an uncalled `fn` and an uncalled
lambda (legacy then runs the program, exit 0, since the body never executes).

| Probe | uncalled fn | uncalled lambda |
| --- | --- | --- |
| R type:print | runs | `type:*` |
| R aliasing | runs | runs |
| R reference-guard | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| R coro-module | `coro-module-capture` | `coro-module-capture` |
| R coro-module thread | `coro-module-capture` | `coro-module-capture` |
| R type:for-source | runs | runs |
| U analysis-limit:kinds | `context:assignment-error` | `context:assignment-error` |
| U await-shape | `free-name` | `text-type-name` |
| U binary-operator | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| U compound-operator &= | `compound-operator` | `compound-operator` |
| U compound-operator |= | `compound-operator` | `compound-operator` |
| U compound-operator ^= | `compound-operator` | `compound-operator` |
| U interpolation:internal | runs | runs |
| U lambda-capture | runs | runs |
| U lambda-shape | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| U logical-operator | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| U unary-operator | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| U unknown-effects | `method:sort` | `method:sort` |
| U import-link | `import-nested` | `import-nested` |
| U throw-expression | error: throw requires an Error expression | error: throw requires an Error expression |
| U trailing-comma | error: expression cannot be empty | error: expression cannot be empty |
| NR coro-future-flow:type | runs | runs |
| NR coro-observe:epoch | runs | runs |
| NR coro-task-recursion | runs | runs |
| NR coro-task-native | runs | runs |
| NR coro-task-op | runs | runs |
| NR method:splice | runs | runs |
| NR param-type-unpredicted | runs | runs |
| NR path-in-index | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| NR path-in-call-argument:imported | `path-in-call-argument:imported` | `path-in-call-argument:imported` |
| NR builtin-plain-arg | runs | runs |
| NR import-outside-project | `import-nested` | `import-nested` |
| NR import-parse-error | `import-nested` | `import-nested` |
| NR opaque:deferred-translation | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| NR coro-async-recursion | runs | runs |
| NR coro-capture | runs | runs |
| 92 cff Ternary | `opaque:irregular-legacy-parse` | `opaque:irregular-legacy-parse` |
| 92 expr Spread | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| 92 expr Property | runs | runs |
| 92 expr Coalesce | runs | runs |
| 92 expr Number | runs | runs |
| 92 expr AssignExpr | runs | runs |
| 92 expr IncDecExpr | runs | runs |
| 92 stmt Block | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| 92 stmt Enum | error: enum requires 'Name { members }' | error: enum requires 'Name { members }' |
| 92 stmt StructInIf | `struct-nested` | `struct-nested` |
| 92 stmt ImportInFn | `import-nested` | `import-nested` |
| 92 stmt Pattern | `declare-pattern` | `declare-pattern` |
| 92 stmt Defer | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| 92 opaque deferred-translation | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| 92 opaque deferred-translation2 | `opaque:template-text` | `opaque:template-text` |
| 92 cff Unary | runs | runs |
| 92 cff Array | runs | runs |
| 92 cff Declare | runs | runs |
| 92 cff If | runs | runs |
| 92 cff While | runs | runs |
| 92 cff Return | `statement:FnDecl` | `statement:FnDecl` |
| 92 cff Call | runs | runs |
| 92 cff Member | runs | runs |
| 92 cff Index | `type:index` | `type:index` |
| T text-type-name lambda | `free-name` | `text-type-name` |
| T text-type-name called | `text-type-name` | `text-type-name` |

Results: no retired row's reason is emitted (type:print's uncalled lambda gives `type:*`, a different row). The
reachable reasons stay reachable (`compound-operator`, `path-in-call-argument:imported`). One D row is not derivative:
`text-type-name` is emitted by `x := await` inside a lambda body, called or not (now G, outcome (ii): legacy errors when
the body runs). The `92 cff` rows were re-probed with the async declaration at top level (the first pass nested it).

#### 9.5 Retirement policy (C114; sixth context added in C116)

A row may be retired, or a reason literal called unreachable or not reached, only after its best program has been run
in all five contexts: a script, a module import (the program as the imported module), a `nift -i` session, a
function called from a loop, the body of a function and of a lambda that are never called, and (C116) classify-all, the
inventory's own mode (`NIFT_VM=classify-all`), on the same programs. The results are
recorded in the row's evidence or in section 9. A reason emitted in any context is not retired; an exact retired row
for an emitted reason is deleted when a family row should classify it (inventory.py matches the exact row first).

#### 9.6 Deleted rows (traceability)

`method:increment` was deleted in C113 (it is emitted; `method:*` classifies it). Its last row, kept also as a tsv comment:

- reason `method:increment`; class A; family F13-chain-masking; legacy: copy.f: rc=0 out='1/2/2/' err=''
- disposition: RETIRED by the post-checkpoint-1 rescan (C111; reworded C112, class E2): its own evidence copy.f runs on the VM (1/2/2, legacy-equal). The name is one member of the generic `method:<name>` family (a method not bound for the receiver kind, compiler_call.cpp:204): `x := 5` / `x.increment()` still emits method:increment (legacy 'unknown value or malformed expression: x.increment()'); that family is covered by the `method:*` row (G undef). Was: consequential (chain masking): the receiver's declaration or producer is refused/masked, so its kind is Unknown or the name unresolved; resolves with the root, no independent work (decisions.md CP15B scope correction (1)). Root: copied struct (struct-value)
- approval: pending; evidence: count=1; smoke/v42_struct_private_smoke/013-copy/copy.f; real run NIFT_VM=1 (candidate): fb:struct-value 1; rescan C112 program: `x := 5 / x.increment()` (VM method:increment, exit 1; legacy rc=1 out='' err='error: main.f:2:1: unknown value or malformed expression: x.increment()'); raised at: src/rt/compiler_call.cpp:194 (method not bound for the receiver kind); status: retired

#### 9.7 The 32 rows whose reported position differed (C115 per-row assessment)

| Row | legacy position | VM position | C115 class | Assessment |
| --- | --- | --- | --- | --- |
| `builtin-arity:deepcopy` | main.f:1:16 | main.f:1:7 | (ii) | VM more precise: the arity of the call is wrong, so the call (1:7) is the construct; legacy points at the argument list (1:16) |
| `builtin-arity:entries` | main.f:2:1 | main.f:2:7 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `builtin-arity:values` | main.f:2:1 | main.f:2:7 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `builtin-default:get` | main.f:4:7 | main.f:4:16 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `builtin-lazy-args:slice` | main.f:4:1 | main.f:4:18 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `builtin-literal-arg:validate` | main.f:1:7 | main.f:1:16 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `context:modulo-assignment` | main.f:2:1 | main.f:4:17 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `context:number-to-int` | main.f:1:12 | main.f:1:19 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `context:struct-field` | main.f:4:25 | main.f:4:21 | (ii) | fix: take legacy's position (the read of the implicit field, 4:25), not the assignment (4:21) |
| `coro-future-flow:ForBinding` | main.f:3:1 | main.f:3:10 | (ii) | VM more precise: the iterable is the future; legacy reports the statement |
| `coro-future-flow:Throw` | main.f:3:1 | main.f:3:7 | (ii) | VM more precise: the operand is the future; legacy reports the statement |
| `coro-module-before-import` | main.f:1:22 | main.f:2:6 | (ii) | fix: take legacy's position (the failing call inside the task) |
| `coro-module-private-async` | pa.f:2:13 | pa.f:2:26 | (ii) | VM more precise (column only, same statement) |
| `fn-called-before-declaration` | main.f:2:11 | main.f:3:1 | (ii) | fix: take legacy's position (the call, not the declaration) |
| `for-missing-write` | main.f:2:24 | main.f:2:1 | (ii) | fix: take legacy's position (the write `x = 5`, not the `for`) |
| `import-limit` | m32.f:1:1 | m63.f:1:1 | (ii) | fix: take legacy's position (legacy's depth, m32.f); also a class (iv) difference: chains 33-63 deep run on the VM and fail in legacy (row difference:import-depth) |
| `import-opaque:legacy-runtime-error` | m.f:1:7 | m.f:1:1 | (ii) | fix: take legacy's position (opaque nodes report the statement start, not the failing operand) |
| `interpolation:non-scalar` | main.f:3:1 | main.f:3:12 | (ii) | VM more precise: the interpolation part, not the print |
| `method:nope` | main.f:1:16 | main.f:1:23 | (ii) | VM more precise: the method call, not the return |
| `non-transferable` | main.f:3:6 | main.f:3:21 | (ii) | VM more precise: the non-transferable argument, not the thread call |
| `opaque:legacy-runtime-error` | main.f:1:7 | main.f:1:1 | (i): corpus programs complete in legacy | fix: take legacy's position (the failing operand); row moved to (i) |
| `param-shadows-fn` | main.f:3:1 | main.f:2:6 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `prepared-decl-type` | main.f:8:22 | main.f:2:3 | (i): corpus programs complete in legacy | fix: take legacy's position (the failing write, not the declaration); row moved to (i) |
| `static-error:push-arity` | main.f:3:17 | main.f:1:21 | (ii) | VM more precise: the `push` itself, not the loop's call site |
| `string-arg-type` | main.f:2:3 | main.f:6:9 | (ii) | fix: take legacy's position (the parameter binding) |
| `struct-member-clash` | main.f:3:7 | main.f:2:13 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `struct-name` | main.f:2:6 | main.f:1:1 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `struct-nested` | main.f:2:7 | main.f:1:13 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `struct-redeclared` | main.f:3:7 | main.f:2:1 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `type:for-source-union` | main.f:2:13 | main.f:3:1 | (ii) | probe replaced by the function-parameter form: same position on both engines (main.f:1:12); now B-same-position |
| `type:member` | main.f:2:24 | error-module.f:1:30 | (ii) | VM more precise: the member read in the lambda body, not the call site |
| `type:script-return` | - | main.f:1:1 | (ii) | VM more precise: legacy prints no position |

Remaining in (ii) with a different position: 17 (7 fix, 10 VM more precise).

#### 9.8 Sixth context: classify-all (C116)

Each retired row's probes were run under `NIFT_VM=classify-all` (non-consequential reasons listed; `(c)` = consequential).

| Row | script | module | loop-called fn | uncalled fn | uncalled lambda | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `type:print` | compiled | compiled | compiled | compiled | type:* | stays retired: its own reason is not emitted |
| `aliasing` | compiled | compiled | prepared-decl-type, method:push | compiled | compiled | stays retired: its own reason is not emitted |
| `reference-guard:*` | opaque:legacy-runtime-error | import-opaque:legacy-runtime-error | opaque:legacy-runtime-error | opaque:legacy-runtime-error | opaque:legacy-runtime-error | stays retired: its own reason is not emitted |
| `coro-module` | coro-module-capture | coro-module-init-task, coro-module-private-async | coro-module-capture | coro-module-capture | coro-module-capture | stays retired: its own reason is not emitted |
| `coro-module (thread)` | coro-module-capture, method:join | coro-module-init-task, method:join | coro-module-capture, method:join | coro-module-capture, method:join | coro-module-capture, method:join | stays retired: its own reason is not emitted |
| `fn-shape` | opaque:legacy-runtime-error | import-opaque:legacy-runtime-error | opaque:legacy-runtime-error | opaque:legacy-runtime-error | opaque:legacy-runtime-error | stays retired: its own reason is not emitted |
| `statement:Try` | - | module-error:syntax | - | - | - | stays retired: its own reason is not emitted |
| `session:function-assignment` | assign-free-name | assign-free-name | statement:FnDecl | statement:FnDecl | statement:FnDecl | stays retired: its own reason is not emitted |
| `session:capture-contract` | compiled | compiled | context:assignment-error | compiled | compiled | stays retired: its own reason is not emitted |
| `coro-main-cell` | compiled | coro-module-init-task, coro-module-init-task, coro-module-private-asyn | statement:FnDecl | statement:FnDecl | statement:FnDecl | **un-retired: E2/F12 (consistency)** |
| `coro-collection-callback` | coro-collection-callback, method:map | coro-module-init-task, coro-module-init-task | statement:FnDecl | statement:FnDecl | statement:FnDecl | **un-retired: E2/F12** |
| `free-name:legacy-binding` | free-name:legacy-binding | free-name:legacy-binding | statement:FnDecl | statement:FnDecl | statement:FnDecl | **un-retired: D** |


### 10. Src-side and scope proposals (items 1, 3 and 3a applied in slot 1, section 23; the rest not applied)

1. **F12, inventory limits (corrected; APPLIED in slot 1, section 23).** In `src/rt/refusal_inventory.cpp`, add `#include "rt/compiler_coro_admit.h"`
   and, after `CompileLimits limits;` (line 163), `cooperative_switches(limits);` as `script_runner.cpp:311` does. C112
   built and collected it in a scratch mutant: 16 of the 17 F12 reasons are no longer seen; `method:join` keeps 9
   non-consequential sightings (section 3.2), so it is not retirable. The fix also unmasks `coro-async-value` (+1) and
   `binding-name-reserved` (+1), both of which have rows, and drops one approved `method:get` sighting. It changes
   nift's sha256 (the file is linked into nift).
2. **Nesting names.** Name the parser and compiler limits distinctly (e.g. `parse-nesting-limit`, `compile-nesting-limit`).
3. **`type:*` name collision (APPLIED in slot 1 as `type:mul`, section 23).** The `*` operator's reason `type:*` is also the inventory's family-row syntax, so its row
   acts as the fallback for every unlisted `type:<op>` reason (still PENDING-OWNER, never an approval). Rename the
   emitter's reason (e.g. `type:mul`).
3a. **Reasons that are messages (APPLIED in slot 1 for `async(...)`/`await(...)` as `removed-builtin:async`/`:await`; `non-transferable` not approved, unchanged).** `async(callable, ...) was removed; ...` and `await(...) was removed; ...` (compiler_thread.cpp:19-21)
   are refusals whose reason string is the error text. PROPOSAL: name them (e.g. `removed-async-call`, `removed-await-call`)
   and keep the text as the message; a src change with the `type:*` rename (sha256 changes). C119 (position review
   Low): `non-transferable` is the same case; its emitted reason is the long message ("non-transferable resource: cannot
   transfer ..."); name it (e.g. `thread-arg-non-transferable`) with the others.
4. **Checkpoint-2 scope (review L-3), PROPOSAL for the owner:**
   - CP2 also takes `struct-after-statement`: reachable on its own in ordinary code (`print("a")` then a struct;
     legacy prints `a`, `1`); after CP2's struct-in-loops work such scripts would still be refused; it masks `method:m`.
   - CP2 also takes `struct-value`: root-free reproducer `pick(true)` returning either of two structs, then `x.v = 9`;
     legacy prints 9.
   - CP2 also takes non-constant array defaults (`struct-field-init`): `xs := [1]` with a push method; legacy prints `[1,2]`.
   - CP2 does not take `struct-private`: it mostly reproduces legacy errors (`private struct field: v`), and legal
     private use already runs on the VM.
   - `struct-construction` (derivative of `context:struct-new`) belongs with CP2's construction-in-loops work.
   - Also for the owner: `struct-assign` (N, `x = P()`, legacy prints 3) is the same struct-as-value gap.
   - No implement row was added or removed; the 13:26 implement rows for checkpoints 2-5 stay implement (only the
     E1 narrowing of `compound-target`/`index-assign-target` changed scope).

### 11. Host built-ins in programs with imports: root cause and repair (APPROVED as slot 2, D-068 #2; design as reviewed)

**Owner 2026-11-11 05:41 (D-068 #2):** Option A is approved as sequence slot 2, about 150-220 lines. **The approved
design is the reviewed one, dead-code-admission.md section 5.5 (C120, REVIEW-C119 section 3 and REVIEW-C120):**

- **E:** a leading import block in the entry; only `import(...)` statements precede the last import.
- **M:** call-free, host-free initialisation of every module, checked transitively over the whole graph (diamonds
  included).
  - `import(...)`/`export(...)` are exempt.
  - Non-host, callback-free natives are allowed.
  - "Call" is any call node in init: interpolation, template text, literals, struct construction and callback
    built-ins.
  - A call named like one of the 11 binding-wins built-ins counts as a user call whenever the name is bound anywhere in
    the program.
- **H:** a **permanent explicit classification of every built-in** as host or pure, by a test that fails on any
  unclassified `BuiltinId`. **Unknown effects are refused.**
- **Negative controls** for host effects, callbacks and transitive imports, plus the mutants listed in 5.5.

The text below is the original C114 reviewer proposal, kept for traceability. Where it differs (it allowed call-free
declarations before the imports), the reviewed design governs.

Recorded from the C114 confirmation review, Part 2. **A reviewer finding marked PROPOSAL, not a ruling.** The rescan did
not investigate this family (owner 00:08); it records the raise site and representatives (section 1.1, rank 1).

**Root cause.** `script_runner.cpp:325-339`: after a linked unit compiles, `first_module_host_op` (module_host_op.h:29)
scans every proto of the linked program; any CallNative of a host built-in (`natives::host_builtin(id)`) refuses the
unit with `module-host-op:<name>`. The refusal inventory uses the same scan (refusal_inventory.cpp:226).

- **What it protects: ruling R1 (owner 2026-10-21) on module read order.** The VM links a program with static imports by
  reading every module's source before execution; legacy reads each module when its `import` executes. A host operation
  legacy runs before or between imports could observe or change different files (`cp`/`remove` of a module, `cd`,
  `setenv`), and the host trace order (`NIFT_TRACE_HOST`) would differ. Pinned by `tests/runtime2/module_host_order.py`;
  `module_host_order_mutant.sh` (`-DNIFT_R1_MUTANT`) is its negative control.
- **The refusal is general:** every proto (entry and modules), uncalled functions included
  (`import("./m.f")` / `fn(never()) { return pwd() }` / `print(k)` is refused; legacy prints 1), read-only and
  side-effecting ops alike, and ops placed after every import (`host-after-import` is pinned as refused although the
  legacy order is then identical).
- **B4b and B5 do not depend on it**: only the ordering of module reads against host side effects and trace lines is at
  stake.

**Proposed bounded repair.** Replace "any host op anywhere" with "no host-op execution can precede the last module read
in legacy order". Admit only when both hold:

1. The entry's imports form a leading block: every top-level statement before the last `import` is an `import` or a
   call-free declaration (`fn`/struct declaration, literal or constant binding).
2. No module's initialisation code can run a host op: module top-level statements contain no host CallNative and no
   call (conservatively: no call other than non-host built-ins), transitively through nested imports.

Everything else stays refused with `module-host-op:<name>` (dynamic and nested imports are already refused:
`import-nested`, D-065). Read-only and side-effecting ops are treated alike (a read-only allowance alone would be unsound
for the trace and for `cd`/env-dependent resolution).

| Aspect | Proposal |
| --- | --- |
| Files and size | new `module_host_order.{h,cpp}` with the two checks (about 120-180 lines); `script_runner.cpp` and `refusal_inventory.cpp` call it instead of the blanket scan (one shared implementation); `module_host_op.h` keeps `first_module_host_op` for the message |
| Tests | extend `module_host_order.py`: admitted, with identical trace, tree and output: a host op after all imports (read-only and mutating), in a function called after the imports, inside an imported module's function called after all imports, and in an uncalled function; still refused: host-before-import (top, function, loop), copy-over-module, remove-module, host-in-module-init with a later sibling import, a nested-module function called during init, and `cd` or `setenv` before an import |
| Mutants | prefix check disabled (caught by host-before-import, copy-over-module); module-init check disabled (host-in-module-init); "calls count as safe" in the prefix (host-before-import-fn); keep the R1 mutant |
| Must not change | the linked-unit model and module-read timing; R1's refusal wherever a host op could precede a module read; sessions, eval and the CP22/CP23 coroutine-module rules; no re-baselining |

### 12. Struct refusals by root cause (reviewer finding, PROPOSAL)

Recorded from the C114 confirmation review, Part 3. **A reviewer finding marked PROPOSAL, not a ruling.**

| Group | Rows | Shared root |
| --- | --- | --- |
| A, declaration model | `struct-field-init`, `struct-after-statement`, `struct-nested`, and the declaration-validation rows (`struct-name`, `struct-redeclared`, `struct-shadows-fn`, `struct-member-clash`: move them to use time) | schemas planned up front with constant defaults (structs.cpp:78-109) |
| B, construction in loop context | `context:struct-new` (former `struct-construction`), `context:struct-field` | legacy's L42 loop behaviour (compiler_structs.cpp:278, compiler_assign.cpp:204) |
| C, struct identity tracking | `struct-assign`, `struct-value`, part of `struct-no-method` | single-`struct_id` binding typing (compiler_assign.cpp:140/209/281/318, compiler_structs.cpp:64/206/210) |
| D, sessions | `presentation-struct-method:*` | session dispatch (compiler_presentation.cpp:40) |
| E, legacy errors | `struct-private`, `struct-no-field` | raise-at-point, under the early-refusal policy |

`type:element-assignment` and `type:for-source` belong with the inference work, not with structs.

**Legacy facts (measured by the reviewer).**
- Defaults are evaluated per construction, in order, with fresh values: `struct(S) { id := next() }` gives 1 then 2;
  `xs := [1]` with `push` on one instance gives `[1,2]` and `[1]`. Defaults that reference earlier bindings or functions
  work only when the struct is declared after them, so non-constant defaults and `struct-after-statement` are one root.
- L42: legacy fails member access on structs inside loop bodies ("value has no member"); only construct-and-store in the
  loop works.
- `S().field` directly on a call result errors in legacy ("current JSON value is not an object"); use `a := S(); a.v`.

**Recommendation (PROPOSAL):** CP2 = Group A + Group B limited to construction and storage in loops (about 300-400 lines
of src, about 500 of tests and mutants; must preserve fresh per-construction defaults (extend the R6 refsem mutant to
expression defaults), no shared mutable defaults, initialiser side-effect order, loop-local captures of instances, B5
semantics, bounded scope and no re-baselining).
CP2b = Group C (about 250-350 lines; B4b struct-id soundness, B5 aliasing of instances). Group D with the B4 session
work; Group E with the early-refusal policy. L42 needs an owner ruling: reproduce legacy's loop-context errors, or
approve one meaning everywhere (D-040 value semantics).


### 13. Proposed rulings for the owner

In the owner's order. Items marked settled restate a ruling; every other item is a PROPOSAL or a question.
Provisional priorities (reference only, not rulings): P0 host built-ins with imports; P1 postfix spelling, nested fns,
`?.`/`?[]`, structs, variadics/spread, const objects; P2 export in entry files, irregular comparisons, indexed postfix
and async forms.

#### 13.1 How the early-refusal policy applies

1. **Settled (owner 00:29, D-013):** rejecting before execution a program the VM can determine will fail is the
   established design; earlier timing and absent earlier output are approved; case B stays B, case C is internal
   failures and genuinely different categories only. Governs the 321 class (ii) rows (`policy:early-refusal`), for
   errors legacy raises at the same construct. Valid constructs stay capability gaps; wrong positions are not excused.
2. **Dead code (question).** 316 of the 321 (ii) refusals also fire in code that never runs, in programs legacy
   completes (`fn(f()) { print(1, 2) }` never called; `if (false) { sha256("a", "b") }`; `if (x > 1) { y := "a" - 1 }`
   with x <= 1). Not covered by 00:29, because these programs do not fail. Options, neutrally:
   (a) refusing statically invalid code even when it is unreachable is an approved part of the early-refusal design; or
   (b) only provably reachable errors may refuse; dead-code cases are capability gaps.
   Counts: fires in dead code 316; does not fire 1 (`coro-module-before-import`, flow-sensitive); `import-dynamic` in a dead
   block is refused as `import-nested`; 3 module-initialisation rows always execute. Corpus impact: 0 corpus programs
   for the remaining (ii) rows (the 7 legacy-completing corpus units belonged to rows C115 moved to (i)). Until this is
   ruled, the (ii) rows stay PENDING-OWNER.
3. **The 17 different-position rows (section 9.7):** 7 "fix: take legacy's position" (`context:struct-field`,
   `coro-module-before-import`, `fn-called-before-declaration`, `for-missing-write`, `string-arg-type`,
   `import-opaque:legacy-runtime-error`, `import-limit`); 10 "VM more precise" with a rationale: accept those 10?
   (`prepared-decl-type` and `opaque:legacy-runtime-error` also need legacy's position; they moved to (i).)

#### 13.2 The 152 capability gaps (class (i)), by feature (section 1.1)

4. Host built-ins in any program with an import (27 rows): see 13.4.
5. Postfix spellings; nested and redeclared function declarations; `?.`/`?[]`; const objects; variadics/spread/
   destructuring; irregular comparisons; `export` in entry files: require for the first VM release?
6. Structs (incl. the four declaration-validation rows moved to (i)): see 13.5.
7. The owner-excluded checkpoint-1 assignment forms (`a[0]++`, `print(a[0] += 5)`, `hs[0][0] = f(1)`): schedule together.
8. Interpolation of unproven kinds, lazy built-in argument order (incl. `slice`, `get` default), loop-context built-ins
   (`.length()`, `empty()`, `to_int`, `%=`), futures in ordinary expressions, capacity limits, the remaining singles:
   require, or schedule after the first release? Confirm `type:element-assignment`, `type:for-source` and
   `struct-no-method` belong with the inference and struct work.

#### 13.3 Session-only rows (class (iii), 3)

9. `builtin-receiver:stringify`, `presentation-struct-method:*`, `session:opaque`: schedule with the B4 session work, or defer?

#### 13.4 Host built-ins with imports: diagnosis and repair (section 11, reviewer PROPOSAL)

10. Approve the bounded repair design (leading import block, init code free of host ops and calls; R1 kept wherever a
    host op could precede a module read)?

#### 13.5 Checkpoint-2 scope (section 12, reviewer PROPOSAL; section 10 item 4)

11. CP2 = Group A (declaration model) + Group B (construction and storage in loops); CP2b = Group C (identity)?
12. L42: reproduce legacy's loop-context member errors, or approve one meaning everywhere (D-040)?

#### 13.6 Other items

13. `fn-flags`: does D-048's rejection-before-execution cover a VM-only refusal, with the legacy change declined?
14. `import-dynamic`: is D-037 its G2 release approval?
15. Ratify the CP23 review qualification `qualified:legacy-await-chain-defect` (async self-recursion runs on the VM where
    legacy errors; deep_chain.f, qualified output `1275` pinned in `qualified` at tests/runtime2/cp23/compatibility.py:9).
16. `difference:import-depth` (class (iv)): import chains 33-63 deep run on the VM and fail in legacy; accept, or make the
    VM fail at legacy's depth (which also fixes `import-limit`'s position)?
17. Family rows (PROPOSAL, confirmation review): family rows for the pure (ii) families `builtin-arity:*`,
    `builtin-receiver:*`, `method:*`, `callable:*`, each stating its contract, exact rows only for members that behave
    differently; only after the `type:*` fix; no rows collapsed until the owner decides.
18. Src-side fixes (each changes the sha256): the F12 inventory-limits fix (then retire the 16 F12 rows); rename `type:*`
    (e.g. `type:mul`); name the two message-named reasons (`async(...)`/`await(...)` "was removed").
19. Deferral candidates: dynamic scope (`free-name`, `free-name:dynamic-caller`); `import-reinitialised` (D-041 #2
    removal condition); CP24 project APIs; comparator-sort portability (Linode); family dispositions in sections 1, 5-9.

### 14. Gates at the tip (C116)

| Gate | Result |
| --- | --- |
| `make test-runtime2-certification-guard` | PASS (classification self-test, 31 cases) |
| `make test-rt-lint` | PASS (0 problems in 710 files) |
| `make test-test-integrity` | clean (701 files scanned, 0 findings) |
| fresh `inventory.py collect` + `report --check` (C117 tsv) | **exit 3** |
| `python3 -B tests/runtime2/assign_family.py --nift $PWD/nift` | PASS (94 cases) |
| `tests/runtime2/named_deferrals.py` | PASS (11 constructs, 5 new D-067 cases) |
| `make test-runtime2-import-depth` (new) | PASS |

- **Inventory (C117)**: G1 PASS, 155 reasons seen: approved 9, deferred-named 29, implement 27, PENDING-OWNER 90,
  retired 0. Exit 3 because 90 seen reasons are still PENDING-OWNER (the intended BLOCKED state). No retired reason is seen.
- **Production code**: `git diff 0c66cad6 -- src/` is empty; nift sha256 `8195f1b34f91d018e336451153aa55bf0e05d590f9acfcd0dffa566ba35834e4`.

### 15. Release-gate issue, still outstanding

`tests/v44_root_path_corruption_reproducers.sh` **FAILS** at the tip (re-run in C112): `destruct` (`declare-pattern`),
`structarr` and `mapstruct` (`context:struct-new`). `escaped-ref` passes. The file is not passing; the failures are
recorded on the `declare-pattern` and `context:struct-new` rows.

**Tracked issue (C115 final confirmation, L-2): no gate checks class (iv) differences.** A behaviour where the VM
accepts a program legacy rejects, such as `difference:import-depth` (import chains 33-63 deep), is not an emitted
reason, so `inventory.py --check` cannot see it; only a corpus program in an rt-diff corpus would expose it. Tracked
as a separate issue after the inventory freezes.

### 16. Linode-only items (not run here)

- The execute pass on the release toolchain: `make cp25r-inventory`.
- `method:sort` comparator portability on GCC 15.2 (cp14b-sort's 234 unverified cases).
- Sanitizer runs of the gates in section 4.

### 17. Change logs per review round

#### 17.1 C112 changes per review finding

| Finding | Change |
| --- | --- |
| P2-1 | `presentation-struct-method:*` restored to PENDING-OWNER, class G (session reproducer in its evidence); C111's claims corrected; `method:increment` stays retired with a reworded cause; `method:*` family row added |
| P2-2 | `struct-value`, `callable:undefined_thing` D → G; all 42 D rows re-checked (section 3.5): `coro-module-private-async` also → G; 39 D remain |
| P2-3 | `declare-const`, `param-name-reserved`, `interpolation:unknown-type` evidence replaced; all 12 repaired programs re-run, each emitting its own reason (recorded in the tsv) |
| P2-4 | gates re-run; one table per failing gate (section 4); family gate fields rebuilt from them |
| P2-5 | static impact corrected; struct representative replaced; every family has all owner fields |
| P2-6 | `incdec-target` → G (owner-excluded, pinned since C107); `callable:prit` → E2; `builtin-receiver:stringify` retired; 108 literals triaged; 16 N rows added; N evidence replaced by programs |
| P2-7 | F12 proposal corrected and measured; `method:join` E2+D, not retirable |
| L-1 | `fn-flags` → Q (owner question) |
| L-3 | checkpoint-2 recommendation added as a PROPOSAL |
| new | `type:*` family-syntax collision noted (row and proposal) |

#### 17.2 C113 changes per confirmation-review finding

| Finding | Change |
| --- | --- |
| P2-A | `builtin-receiver:stringify` restored to PENDING-OWNER, G (sessions); four-way probe rule applied to every retired row (9) and every unreachable or not-reached literal: `struct-no-method` and `type:element-assignment` restored too; `compound-operator`, `limit:atoms`, `limit:too many registers`, `limit:jump distance exceeds 16 bits`, `path-in-call-argument:imported`, `import-opaque:legacy-runtime-error` added |
| P2-B | 8 rows added (`type:/`, `type:<=`, `type:>=`, `type:for-source-union`, `expression:assignment-target`, `coro-future-flow:Assign`, `coro-future-flow:Call`, `context:collection-empty`); every run-time-built family enumerated and probed: 270 rows added, `callable:*` family row added, the rest recorded in section 9.2; `type:*` and `method:*` absorption listed |
| L-1 | `method:increment` row deleted (emitted, so not retirable; `method:*` covers it) |
| L-2 | section 13 item 10 cites `qualified:legacy-await-chain-defect` and asks for ratification |
| L-3 | section 13 items 6 and 8 added |
| L-4 | section 3.3 lists `context:assignment-error`'s filesystem-recipe case |

#### 17.3 C114 changes per confirmation-review finding

| Finding | Change |
| --- | --- |
| P2-1 | `type:for-source` restored to PENDING-OWNER, G (infer); uncalled function and lambda bodies added as the fifth probe context; the 4 retired rows and every section 9.1/9.2 claim re-probed (9.4); `text-type-name` D → G; retirement policy written (9.5) |
| P2-2 | `coro-future-flow:Coalesce` row added; every parent re-probed in natural spellings (9.2): `:Binary` has a legacy-running form |
| L-1 | `expression:assignment-target`, `coro-future-flow:Assign` tagged class G in the tsv |
| L-2 | section 13.2 item 9 cites the pinned output `1275` (compatibility.py:9), not a line number |
| L-3 | rename proposal for the two message-named reasons (section 10 item 3a, section 13.2 item 11) |
| L-4 | `statement:FnDecl` covers functions declared in functions, loops and ifs (five-way evidence; section 1.1 rank 3) |
| Item 4 | section 1: outcome classes for all 530 rows, `outcome:` tag in every row, (i) families ranked; evidence fixed for the (i) rows whose program failed in legacy |
| Item 5 | section 13.2 item 10 (PROPOSAL) |
| Owner 00:08 #1 | class (ii) split into ii-A/B/C by a print-before probe of every row: A 0, B 335 (contracts B-same-position 303, B-different-position 32), C 0; tag `outcome:ii-B` with category, position and side effect in every row; the single-mechanism question removed |
| Owner 00:08 #2 | every retired, D, E1 and E2 row carries a TRACE note (reproducer, why it was a refusal, the explanation, why not independent); text dropped by C112/C113 repairs restored as previous-* notes (10 evidence segments, 41 legacy columns, 25 dispositions); the deleted `method:increment` row kept as a tsv comment and in section 9.6 |
| Owner 00:08 #3 | completeness boundary (section 1.3) |
| Owner 00:08 #4 | section 13 arranged under the three headings |

#### 17.4 C115 changes (C114 confirmation review and owner rulings 00:08, 00:29)

| Item | Change |
| --- | --- |
| Owner 00:29 | early-refusal policy recorded in sections 1 and 13.1; 321 (ii) rows tagged `policy:early-refusal(D-013, owner 00:29)`; status unchanged pending the dead-code question |
| 1 (P2-1) | 12 rows (ii) → (i) with the reviewer's programs (verified); all (ii) probes re-run with `zz := X`; 2 more rows → (i) from the corpus measurement (`prepared-decl-type`, `opaque:legacy-runtime-error`); split (i) 152, (ii) 321, (iii) 3, (iv) 1, D 38, E 20 |
| 2 (P2-2) | 15-idiom statement-level rescan (section 1.3): `fn-redeclared`, `too-many-functions`, `limit:invocation-arguments`, `session:opaque` added (reachable); 7 unreached rows added as retired at creation with five-context probes |
| 3 | dead-code measurement per (ii) row and the neutral question (13.1 item 2) |
| 4 | section 9.7 per-row table for the 32; `type:for-source-union` probe replaced (same position); `import-limit` (iv) verified: row `difference:import-depth`, item 16 |
| 5 | sections 11 and 12 (reviewer Parts 2 and 3, PROPOSAL); items 10-12 |
| 6 | section 13 in the owner's order |
| 7 | row counts reconciled (528 → 530 → 535); inventory identity recorded |

#### 17.5 C116 changes (C115 final confirmation)

| Item | Change |
| --- | --- |
| P2-1 | `free-name:legacy-binding` un-retired: D, PENDING-OWNER (classify-all emits it on its own probe) |
| P2-2 | `coro-collection-callback` un-retired: E2/F12, PENDING-OWNER (classify-all, !parallel); `coro-main-cell` given the same E2/F12 treatment |
| Policy | classify-all added as the sixth context of the retirement rule (9.5); the four remaining retired-at-creation rows and the four older retired rows re-checked (9.8) |
| L-1 | TRACE labels on the retired-at-creation rows |
| L-2 | class (iv) gap recorded as a tracked issue (section 15) |
| L-3 | section 12 must-preserve list completed |
| L-4 | the 14 reclassified rows keep their C114 ii-B tags as "Previous outcome" notes |
| L-5 | section 13.1 option (a) stated without the precedent clause |

### 18. Owner rulings 2026-11-11 02:53 (D-067), applied in C117

decisions.md D-067 records the rulings; the inventory applies them (each row cites "D-067 #n (owner 2026-11-11 02:53)").

| D-067 item | Applied to |
| --- | --- |
| #1 positions | the 7 position fixes set to `implement`; the 10 VM-more-precise positions marked approved-pending-review (still PENDING-OWNER); C119: all 10 verified by the review, 5 approved, 5 on mixed rows kept PENDING-OWNER (section 19) |
| #2 deferrals | 19 rows `deferred-named`: `incdec-target`, `expression:assignment-target`, `coro-future-flow:Assign`; `builtin-receiver:stringify`, `presentation-struct-method:*`, `session:opaque`; `free-name`, `free-name:dynamic-caller`; the CP24 rows (`callable:build`, `build_all`, `build_names`, `inject`, `status`, `track`, `tracked`, `untrack`, `build_repair`, `opaque:template-text`; D-053); `import-dynamic` (D-037 cited, not broadened). known-limitations.md "Further deferrals"; named_deferrals.py gains 5 cases |
| #3 `@fn` | `fn-flags` approved; amendment recorded under D-047 and D-048 |
| #4 import depth | `difference:import-depth` approved; kMaxModuleDepth = 64 (module_graph.h:122) documented as a deliberate resource bound of the recursive graph walk (module_graph.cpp:275, introduced with the loader in CP12b f45f52d3); tests/runtime2/import_depth.py (`make test-runtime2-import-depth`) pins depth 33 running, legacy failing at 33, and depth 64 refused |
| #5 not ratified | async self-recursion and `import-reinitialised` untouched (PENDING-OWNER) |
| #6 | sort-comparator portability stays a mandatory Linode item (section 16) |
| #7 required | postfix, nested fn, `fn-redeclared` (document the later-definition-wins behaviour), `?.`/`?[]`, const objects, `export`, bytes, `warn`/`err`/`read`: `implement` (remaining approved features); variadics/spread/destructuring: CP3; structs: CP2 (declaration model, construction in loops) and CP2b (`struct-assign`, `struct-value`, `struct-no-method`) |
| #8 host-import guard | 27 `module-host-op:*` rows `implement`, sequence slot 2 |
| #9 investigate first | irregular comparisons, interpolation, loop-context built-ins, futures in expressions, capacity limits and the remaining (i) singles: direction recorded per row, PENDING-OWNER |
| #10 family grouping | family rows `builtin-arity:*` (123 members), `builtin-receiver:*` (77), `method:*` (40), `callable:*` (10) state their contracts; members keep their rows and evidence, marked "member of <family>"; exceptions unmarked; nothing deleted |
| #11 dead code | Option A withdrawn; the 321 (ii) rows are reclassified in C118 (docs/runtime-2/checkpoints/dead-code-admission.md); revised in C119 (section 19) |

### 19. C119: review of C117/C118 applied (tsv and docs only)

The review (C117 CONFIRMED; C118 dead-code proposal SOUND WITH CHANGES) is applied; docs/runtime-2/checkpoints/dead-code-admission.md is revised.

| Finding | Change |
| --- | --- |
| P1-1 design | design (b): defer only kind-certain sites, through an allow-list; (a) (a run-time-checked op) would implement the union operation, a class (i) capability, not a deferral (dead-code-admission.md 5.1) |
| P1-2 type rows | 19 rows (ii) → (i) mixed, with union and single-kind programs in each row's evidence: (i) 171, (ii) 302; X (ii) 280, plus 19 single-kind sub-cases |
| P2-1 dynamic scope | `callable:*` (family and 10 members), `assign-free-name`, `assign-free-name:undeclared` and `free-name*` (17 rows) marked deferral-excluded, with the probes `h(1)`, `q = 3` and `h.size()` |
| P2-2 order | per-site table of legacy order and message (dead-code-admission.md 5.2); every (ii) X row is marked ALLOW-LISTED (249) or NOT allow-listed (19) or excluded (12); 15 of the 19 mixed rows' single-kind sub-cases allow-listed |
| P2-3 Bottom | a distinct `kKindBottom` bit, the join rule, the trap for `{Bottom}` operands and a mutant (5.3) |
| P2-4 inventory | `deferred:<reason>` records in classify-all; `reason_key` strips the prefix; `--check` applies the row's status; test (5.4) |
| P2-5 host guard | options (a) (the slot-2 repair covers uncalled and untaken host ops; soundness: no host op runs before the last module read) and (b) (accept the gap), for the owner (5.5) |
| P2-6 S rows | legacy accepts all 10 S constructs in dead code (probes in each row's evidence); `jump-outside-loop` marked OWNER RULING NEEDED; `text-type-name` probe fixed (an uncalled lambda) |
| Must-not-change, mutants | extended (5.7): D-013 early refusal on non-allow-listed sites, exit 1 never 70, heap budget, per-unit sessions and eval, async raise rejects the future (CP22 L1/L4) with a test; 13 mutants |
| Positions (D-067 #1) | all 10 pass; approved: `builtin-arity:deepcopy`, `coro-module-private-async`, `method:nope`, `non-transferable`, `static-error:push-arity`; verified but PENDING-OWNER (mixed rows): `coro-future-flow:ForBinding`, `coro-future-flow:Throw`, `interpolation:non-scalar`, `type:member`, `type:script-return` |
| Lows | `which` in the platform list; the corpus base made reproducible and 318/266/298 reconciled (4.1); `number-literal` implement vs S reconciled; `non-transferable` added to section 10 item 3a; `type:script-return` position note (1:8) |
| Not changed | the slot labels of the D-067 #7 required features (put to the owner by the coordinator) |

Statuses: 609 rows, approved 51 (+5), deferred-named 36, implement 76, PENDING-OWNER 438, retired 8. No row was removed.

Gates at C119 (tsv revision of this commit, pinned binary):

| Gate | Result |
| --- | --- |
| `make test-runtime2-certification-guard` | PASS (historical control; classification self-test, 31 cases) |
| `make test-rt-lint` | PASS (0 problems in 710 files) |
| `make test-test-integrity` | clean |
| fresh `inventory.py collect` + `report --check` | **exit 3**: G1 PASS, 155 reasons seen: approved 11, deferred-named 29, implement 27, PENDING-OWNER 88, retired 0 (the intended BLOCKED state) |
| `tests/runtime2/named_deferrals.py --nift $PWD/nift` | PASS (11 constructs, 0 failures) |
| `make test-runtime2-import-depth` | PASS |
| `tests/runtime2/assign_family.py --nift $PWD/nift` | PASS (94 cases) |

Production code: `git diff 0c66cad6 -- src/` is empty; nift sha256 `8195f1b34f91d018e336451153aa55bf0e05d590f9acfcd0dffa566ba35834e4`.

### 20. C120: review of C119 applied (tsv, docs and one pinned test)

The C119 re-review found the plan SOUND WITH CHANGES, with two P1s. The name-certainty option is sound with
conditions, and host guard (a) is sound with small changes. docs/runtime-2/checkpoints/dead-code-admission.md is
revised.

| Finding | Change |
| --- | --- |
| P1-1 sessions | session units never defer a kind- or name-dependent site (argued against routing through the session re-check); session tests (kind and name programs) and the mutant "session deferral frozen"; the C119 "decides independently" wording corrected |
| P1-2 built-in precedence | name certainty for every callee-named site, including the 57 allow-listed builtin-arity free-function members; legacy precedence probed per member in four binding forms: the binding wins for 11 (`cancel`, `channel`, `deepcopy`, `map`, `recv`, `send`, `set`, `sha256`, `sha256_file`, `stack`, `validate`), the built-in wins for 50, and 62 have no free form; recorded per row ("C120 precedence"); the hole is hidden today only by `binding-name-reserved`, `fn-name-reserved` and `param-name-reserved` |
| P2-1 kind certainty | defined per site: the set is non-empty, has no Unknown and no Bottom, and no member is admitted by the operation (Number = Int\|Frac, File/Stream = Handle\|bit) |
| P2-2 Bottom | `kKindBottom = 1u << 24`, distinct from the 12 existing `kinds == 0` uses (audit table); the assertion is scoped to the admission and deferral entry points |
| P2-3 | 5.1(2) qualified by the approved semantic differences (B5 parameter mutation example) |
| Name certainty | extended to `callable:*` (11 rows ALLOW-LISTED under name certainty); scan coverage list (every unit and binding form, nested and conditional fn declarations, struct methods, imports and exports, the embed host registry; sessions excluded); dependency on four whole-program refusals pinned by the new `tests/runtime2/deferral_dependencies.py` (`make test-runtime2-deferral-dependencies`, in `test-rt-vm`); the owner's `if (false) { nope() }` example works under the plan |
| Host guard (a) | restated as the proposed slot-2 design: leading import block; call-free module init with `export`/`import` exempt and non-host, callback-free natives allowed; "call" defined; transitive; a complete host set with a test; tests and mutants; about 150-220 src lines |
| Mutants | five added (session deferral frozen, builtin-arity deferred despite a binding, name scan misses a nested or conditional fn or a struct method, export not exempted, interpolation call missed in init), plus a kind-0 assertion mutant |
| Coverage | X (ii) 260 of 280, X + L-defer 284 of 309; 7 of the 11 refused platform patterns fixed, 8 with host guard (a) |

Statuses unchanged: 609 rows, approved 51, deferred-named 36, implement 76, PENDING-OWNER 438, retired 8.

Gates at C120:

| Gate | Result |
| --- | --- |
| `make test-runtime2-certification-guard` | PASS (31 cases) |
| `make test-rt-lint` | PASS (0 problems in 710 files) |
| `make test-test-integrity` | clean (702 files, 0 findings) |
| fresh `inventory.py collect` + `report --check` | **exit 3**: G1 PASS, 155 reasons seen: approved 11, deferred-named 29, implement 27, PENDING-OWNER 88, retired 0 |
| `tests/runtime2/named_deferrals.py` | PASS (11 constructs) |
| `make test-runtime2-import-depth` | PASS |
| `make test-runtime2-deferral-dependencies` (new) | PASS (5 whole-program refusals) |
| `tests/runtime2/assign_family.py` | PASS (94 cases) |

Production code: `git diff 0c66cad6 -- src/` is empty; nift sha256 `8195f1b34f91d018e336451153aa55bf0e05d590f9acfcd0dffa566ba35834e4`.

### 21. C121: final soundness review of C120 applied (docs only)

REVIEW-C120 found host guard (a), including the print allowance, SOUND, and the dead-code plan SOUND after one
blocking fix. Both C119 P1s are closed.

| Finding | Change |
| --- | --- |
| Blocking: struct fields | the name scan covers every field of every struct in every unit (with struct names and method names); plan test `sf2.f` (legacy prints 7; the VM refuses `struct-field-init` today, latent until CP2); mutant "name scan misses a struct field" |
| Six conditions | recorded for the implementation checkpoint's review (dead-code-admission.md 5.7a): the session gate covers the `nift -i script.f` startup file; binding-wins names in module init count as user calls; `host_set_complete.py` classifies every BuiltinId; more `deferral_dependencies.py` contexts; "the ordinary error status (eval exits 2), never 70"; statement-prefixed and empty-after-prefix messages |
| Note | `host_set_complete.py` is proposed, not committed |

### 22. C122: owner rulings 2026-11-11 05:41 (D-068) applied (tsv, docs and one pinned test)

decisions.md D-068 records the rulings. D-041 #2 gains the `import-reinitialised` amendment.

| D-068 item | Applied to |
| --- | --- |
| #1 dead-code plan, slot 3 | 249 ALLOW-LISTED (ii) rows and 15 single-kind sub-cases marked "sequence slot 3"; statuses unchanged (the reachable case stays the 00:29 early refusal) |
| #2 host guard (a), slot 2 | the 27 `module-host-op:*` rows cite the reviewed design; section 11 updated |
| #3 undefined-call deferral | the 11 `callable:*` rows marked slot 3 under full name certainty; the builtin-arity free forms keep the 11-vs-50 precedence notes |
| #4 sessions | scope boundary recorded (dead-code-admission.md header, 5.7a condition 1) |
| #5 S | the 10 S rows confirmed; `jump-outside-loop` ruled S (C122 probes: a break in a never-running loop is fine) |
| #6 uncertain types | the 19 mixed rows marked: stay (i), never narrowed |
| #7 positions | the 5 mixed rows' certain-type positions approved under D-067 #1, recorded in each disposition and pinned by the new `tests/runtime2/precise_positions.py` (`make test-runtime2-precise-positions`, in `test-rt-vm`); statuses stay PENDING-OWNER |
| #8 slot 7 | `postfix-spelling`, `statement:Export`, `statement:FnDecl`, `fn-redeclared`, `declare-const` relabelled from "remaining approved features" to slot 7 |
| #9 async self-recursion | conditionally approved; safety checkpoint for the task-memory defect; CP23 qualification and its row (tests/runtime2/cp23/CP22-CLASSIFICATION.tsv `deep_chain.f`) unchanged, pending that checkpoint |
| #10 `import-reinitialised` | D-041 #2 amendment; row note; stays PENDING-OWNER |
| #11 slot order | recorded in D-068 |

Gates at C122:

| Gate | Result |
| --- | --- |
| `make test-runtime2-certification-guard` | PASS (31 cases) |
| `make test-rt-lint` | PASS (0 problems in 710 files) |
| `make test-test-integrity` | clean |
| fresh `inventory.py collect` + `report --check` | **exit 3**: G1 PASS, 155 reasons seen: approved 11, deferred-named 29, implement 27, PENDING-OWNER 88, retired 0 |
| `make test-runtime2-precise-positions` (new) | PASS (5 rows) |

Production code: `git diff 0c66cad6 -- src/` is empty; nift sha256 `8195f1b34f91d018e336451153aa55bf0e05d590f9acfcd0dffa566ba35834e4`.

### 23. Slot 1: inventory-infrastructure corrections (C123; production checkpoint)

Owner 2026-11-11 05:41 (D-068 #11): slot 1 is authorised with exactly the approved scope (a), (b) and (c).
Program semantics do not change. The only user-visible change is the reason id in three refusal texts.

**(a) The inventory tool compiles with a real run's settings.**
- `src/rt/refusal_inventory.cpp` includes `rt/compiler_coro_admit.h` and calls `cooperative_switches(limits)` (through
  `run_path_switches`) after the link, as `script_runner.cpp:311` does after its link.
  - The call site is after the link block, not immediately after `CompileLimits limits;`, because
    `cooperative_switches` reads `limits.link`. Under default settings both positions behave the same.
  - The call is in a short helper because rt-lint caps function length at 150 lines.
- **Retired with a TRACE citing slot 1 (18 rows):**
  - the 16 F12 rows: `callable:thread`, `method:done`, `method:status`, `builtin-arity:join`,
    `builtin-receiver:join`, `coro-blocking-host:exists`/`file`/`rm`, `method:open`/`write`/`save`/`close`,
    `coro-observe:done`/`print`/`status`, `coro-struct`;
  - `coro-collection-callback`, which the corrected tool no longer emits on its probe (the reverted mutant does);
  - `coro-main-cell`, emitted only when parallel tasks are off.
  - **Corrected in C124 (slot-1 review, finding 3).** Parallel tasks are also off under the non-contractual test
    switch `NIFT_VM_COOPERATIVE_THREADS=1` (semantics.md:140, evidence only). With that switch the **real runner**
    emits 8 of the retired reasons:
    - `coro-observe:done`, `coro-observe:status`, `coro-observe:print`;
    - `coro-blocking-host:exists` (review probe) and `coro-blocking-host:file` (C124 probe);
    - `coro-struct`, `coro-collection-callback`, `coro-main-cell`.

    With the default settings it emits none of them in any probed context. These rows are retired on that basis,
    pending the owner's confirmation that the switch is not user-facing. If the owner treats it as user-facing, they
    must not be retired. A collect run with the switch set would report "retired reason(s) seen again", which fails
    safe. The C123 claims that "the real runner never refused with it" and that "neither the corrected tool nor the
    runner can emit it" were wrong and are removed from the tsv.
- **`method:join` stays E2+D, PENDING-OWNER.** Its 9 remaining sightings are classified in its row under their real
  families: auto.f and auto2.f are CP24 (`callable:build`/`status`/`project_root`, deferred-named), and ok.f is
  `lambda-variadic` (implement, slot 6).
- **Accounted:**
  - `coro-async-value` +1 (churn.f, unmasked);
  - `binding-name-reserved` +1 (method-module.f, consequential before);
  - `method:get` -1 (mutex.f, an F12 artefact; the row stays approved).
  - Execute-mode fallbacks are unchanged.
- **Negative control:** `NIFT_INVENTORY_LIMITS_MUTANT` (`.build/nift-inventory-limits-mutant`), tested by
  `tests/runtime2/inventory_limits.py` (`make test-runtime2-inventory-limits`, in `test-rt-vm`). The mutant lists the
  15 F12 reasons again on 5 probes, and the classification gate fails with "retired reason(s) seen again" for the 14
  retired ones.

**(b) `type:*` is renamed `type:mul`.**
- The reason is built by `op_type_reason` (src/rt/ast.cpp), used at the three compiler_expr.cpp sites.
- `refusal_text.cpp` maps `type:mul` to the unchanged text "(*)".
- `type:*` is no longer a row. The tsv row is renamed `type:mul`.
- `inventory.py` falls back to a family row only for the four D-067 #10 families (`FAMILIES`). An unlisted
  `type:<op>` reason fails G1, tested by `tests/runtime2/cp25/inventory_families.py`, whose `--mutant` re-adds the
  generic fallback and is caught by `unlisted-type-reason`.
- References updated by substituting only that string:
  - tests/runtime2/cp19/session_refusal_cli.py;
  - tests/runtime2/cp19/widening_error_boundary.py;
  - tests/runtime2/certify-fingerprints/test-rt-vm.txt (3 occurrences).
- No rt-diff rule or golden names it.
- **Left unchanged on purpose:**
  - historical evidence (docs/runtime-2/evidence/**, earlier checkpoint documents and the C111-C122 evidence text in
    the tsv), which records what earlier binaries emitted;
  - `type:*` used as family notation in prose ("type:*, context:*").

**(c) The message-shaped reasons get stable ids:** `removed-builtin:async` and `removed-builtin:await`
(compiler_thread.cpp).
- The tsv rows are renamed, and tests/runtime2/rt_parser_negative.py now checks the VM refusal by the new id. Legacy's
  own message is unchanged.
- `non-transferable` and the nesting-emitter rename are not changed (not approved).

**User-visible change, exact (pinned by `tests/runtime2/reason_ids.py`, `make test-runtime2-reason-ids`).** The
program's directory is shown as `./`:

| Program | Before | After |
| --- | --- | --- |
| `print("a" * 1)` | `error: ./main.f:1:7: not compiled for the VM (type:*): an operation whose operand types the VM cannot prove (*) is not supported by the Runtime 2.0 VM yet` | `error: ./main.f:1:7: not compiled for the VM (type:mul): an operation whose operand types the VM cannot prove (*) is not supported by the Runtime 2.0 VM yet` |
| the same in `nift -i` | `error: <stdin>:1:7: NIFT_VM session refused: type:*: an operation ... (*) is not supported by the Runtime 2.0 VM yet` | `error: <stdin>:1:7: NIFT_VM session refused: type:mul: an operation ... (*) is not supported by the Runtime 2.0 VM yet` |
| `x := async(f)` | `error: ./main.f:2:6: not compiled for the VM (async(callable, ...) was removed; declare an async function/lambda and call it directly): a language feature is not supported by the Runtime 2.0 VM yet` | `error: ./main.f:2:6: not compiled for the VM (removed-builtin:async): a language feature is not supported by the Runtime 2.0 VM yet` |
| `x := await(f())` | `error: ./main.f:2:6: not compiled for the VM (await(...) was removed; use 'await future' or 'await async_fn(args)'): a language feature is not supported by the Runtime 2.0 VM yet` | `error: ./main.f:2:6: not compiled for the VM (removed-builtin:await): a language feature is not supported by the Runtime 2.0 VM yet` |

The note lines, stdout, exit status 1 and legacy's messages are unchanged. NIFT_VM_STATS records the new ids.

**Mutant controls:**

| Control | Mutant | Caught by |
| --- | --- | --- |
| (a) | `-DNIFT_INVENTORY_LIMITS_MUTANT` on refusal_inventory.o | `inventory_limits.py --mutant`: the probes and the classification gate |
| (b) | `-DNIFT_REASON_ID_MUTANT=1` on ast.o (old `type:*`) | `reason_ids.py --mutant type-mul`: `type-mul-script`, `type-mul-session` |
| (b) | the generic family fallback re-added (inventory.py copy) | `inventory_families.py --mutant`: `unlisted-type-reason` |
| (c) | `-DNIFT_REASON_ID_MUTANT=2` on compiler_thread.o | `reason_ids.py --mutant removed-builtin`: `removed-builtin-async`, `removed-builtin-await` |

All changes are compile-time or inventory-only, outside hot paths (no VM opcode, native or runtime function changed).

**Inventory after slot 1:** 609 rows: approved 51, deferred-named 36, implement 76, PENDING-OWNER 420, retired 26 (+18).

Freeze gates (C123, every gate run, failures collected):

| Gate | Result |
| --- | --- |
| `make test-runtime2-certification-guard` | PASS |
| `make test-rt-lint` | PASS (0 problems; boundary 0 in 710 files) |
| `make test-test-integrity` | clean |
| `make -k test-rt-vm` (vm-unit, parity seed 1 with rt-diff, adversarial 39612 programs, the new slot-1 targets, inventory mutant gates) | PASS (rc 0, 61 min) |
| `make -k test-rt-vm-mutants` ([0] parity) | PASS |
| `make test-rt-vm-sharing-oracle` (selftest) | PASS |
| `make test-runtime2-b5`, `make test-runtime2-b5-mutants` | PASS, PASS |
| cycle routes | PASS (24 routes) |
| assign_family | PASS (94 cases) |
| named_deferrals, import_depth, deferral_dependencies | PASS |
| inventory-links, inventory-host-mutant, inventory-limits (+ mutant), reason-ids (+ 2 mutants), inventory families (+ mutant) | PASS |
| fresh `inventory.py collect` + `report --check` | **exit 3** (intended BLOCKED state): G1 PASS, 138 reasons seen: approved 10, deferred-named 29, implement 27, PENDING-OWNER 72, retired 0 |
| `make test-rt-builtins-guards` (callgrind) | PASS: 35 workloads, 0 guard failures |

nift sha256 at the freeze: `78027d9565d719f4ab95ad26d0fed460a795f69958eb0dcca3548f47f9e1b495` (changed from 8195f1b3..., as approved for slot 1).

### 24. C124: slot-1 review evidence fixes (tsv, docs and tests only)

The slot-1 review (REVIEW-C122-C123-SLOT1) verdict is ACCEPT, with evidence fixes. src/ is unchanged since dda4a0c8,
and the nift sha256 is still `78027d9565d719f4ab95ad26d0fed460a795f69958eb0dcca3548f47f9e1b495`. No status was changed.

**Finding 3: the TRACE wording.** The 18 retirement TRACEs are rewritten (section 23, corrected). The false "never"
claims are removed. The switch-emitted rows now say "emitted only under the non-contractual test switch
NIFT_VM_COOPERATIVE_THREADS=1 (semantics.md:140); never with the default settings in any probed context; retired on
that basis, pending the owner's confirmation that the switch is not user-facing".

**Finding 4: per-row probes in six contexts.** Each retired row now records its probe in each context: script, imported
module, session, loop-called function, uncalled function, uncalled lambda, and classify-all. Each context was run three
ways: the VM by default, the VM with `NIFT_VM_COOPERATIVE_THREADS=1`, and legacy. Classify-all was run on the release
binary and on the limits-reverted mutant. The probe record is the session scratch file `rescan/p13/probe18.json`.

| Probe (rows) | Default VM: script / loop fn / uncalled fn / uncalled lambda | Module | Session | With the switch | Classify-all (mutant) |
| --- | --- | --- | --- | --- | --- |
| rt-diff-corpus async/fileops (`coro-blocking-host:exists`/`file`/`rm`, `method:open`/`write`/`save`/`close`) | runs, runs, runs, runs | coro-module-init-task | session:coroutine | coro-blocking-host:file | compiled (the 7 reasons) |
| rt-diff-corpus async/thread (`callable:thread`) | runs x4 | coro-module-init-task | session:coroutine | coro-blocking-host:file | compiled (callable:thread, method:join) |
| future observe (`method:done`, `method:status`, `coro-observe:done`, `coro-observe:status`) | runs x4 | coro-module-init-task | session:coroutine | coro-observe:done | compiled (5 reasons) |
| async print (`coro-observe:print`) | runs x4 | coro-module-init-task | session:coroutine | coro-observe:print | compiled (coro-observe:print) |
| smoke v45_threads handle-transfer (`builtin-arity:join`) | runs x4 | coro-module-init-task | **builtin-arity:join** | (as default) | compiled (builtin-arity:join, ...) |
| thread array join (`builtin-receiver:join`) | runs x4 | coro-module-init-task | session:coroutine | runs | compiled (builtin-receiver:join, ...) |
| struct to async (`coro-struct`) | runs x4 (legacy fails, see below) | coro-module-init-task | session:coroutine | coro-struct | compiled (coro-struct) |
| collection callback (`coro-collection-callback`) | runs x4 | coro-module-init-task | session:coroutine | coro-collection-callback | compiled (coro-collection-callback) |
| loop closure into async (`coro-main-cell`) | runs x4 | coro-module-init-task | session:coroutine | coro-main-cell (script), coro-capture (function contexts) | compiled (coro-main-cell) |

Legacy completes every probe in every context (rc 0), except the struct-to-async probe in the script, module and
loop-called contexts (below).

**Two new findings, for the coordinator:**

1. **`builtin-arity:join` is emitted by the real session runner with the default settings.**
   - Program: `nift -i` with handle-transfer.f, where a thread joins a thread handle passed as a parameter
     (`h.join()`). The VM gives `NIFT_VM session refused: builtin-arity:join` at line 2; legacy runs it.
   - The retirement rule (section 9.5) counts sessions, so this row should not be retired. It belongs back at
     PENDING-OWNER (E2, sessions).
   - The status is left unchanged as instructed; its TRACE says so.
2. **Outside slot 1: a struct instance passed to an async function.**
   - Program: `struct(P) { v := 1 }` / `fn[async](af(p)) { return p.v }` / `print(await af(P()))`.
   - The VM runs it and prints 1. Legacy fails with `async function argument contains a non-transferable resource`.
   - This is a pre-existing class (iv)-style difference with no row. It is recorded here, and nothing was changed.

**Finding 6: session forms pinned.** `tests/runtime2/reason_ids.py` now also pins the session forms of
`removed-builtin:async` and `removed-builtin:await`:
- `error: <stdin>:2:6: NIFT_VM session refused: removed-builtin:async: a language feature is not supported by the
  Runtime 2.0 VM yet`, and the same with `removed-builtin:await`;
- the "before" texts are verified on the mutant build;
- an `await` of an async callee is refused as `session:coroutine` first, so the await case uses a plain callee.

**Finding 8: D-068 completed** with the omitted owner text. The slot-1 scope is item 12; see decisions.md D-068 "Text
completion (C124)".

Gates at C124:

| Gate | Result |
| --- | --- |
| `make test-runtime2-certification-guard` | PASS |
| `make test-rt-lint` | PASS |
| `make test-test-integrity` | clean |
| `make test-runtime2-reason-ids` (with the session forms; both mutants; inventory families and its mutant) | PASS |
| `make test-runtime2-inventory-limits` (and its mutant) | PASS |
| `make test-runtime2-inventory-links` | PASS (10 cases; mutant detected 10/10) |
| fresh `inventory.py collect` + `report --check` | **exit 3**: G1 PASS, 138 reasons seen: approved 10, deferred-named 29, implement 27, PENDING-OWNER 72, retired 0 |
| `make -q` | exit 0 (up to date), before and after |

`git diff dda4a0c8 -- src/` is empty; nift sha256 `78027d9565d719f4ab95ad26d0fed460a795f69958eb0dcca3548f47f9e1b495` (unchanged).
The full test-rt-vm is not re-run (src unchanged).
