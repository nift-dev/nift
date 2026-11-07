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

### 2.1 Legacy behaviour the contract does not follow (PENDING-OWNER: approve as differences)

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

Also observed, not changed by the repair (the VM already differed and is correct):
3. legacy evaluates an index key **twice** in a declaration's initialiser (`r := b[g()]` calls `g`
   twice); the VM evaluates it once (semantics.md: operands once each);
4. legacy fails (`remove: index out of range`) when the argument drops the receiver's last other
   reference; the VM keeps the retained receiver alive.

### 2.2 Same root cause outside H-A (STOP: not fixed, PENDING-OWNER)

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

Diff (production sources, `git diff fc987ec1 C1 -- src`): 7 files, +88/-11, of which about 35
executable lines; the rest are comments and the two mutant guards. No VM, protocol or runtime
change. A captured loop binding still writes its (retained) receiver back after the mutation
(unchanged; such programs whose argument rebinds the loop binding are refused,
`for-capture-vanishing`).

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
  verification**; kept in this history after resolution. Its two PENDING-OWNER legacy
  differences (2.1) and the related operator/compound finding (2.2, not fixed).
- **M-3** `for-capture-vanishing` / `for-missing-write`: deferred precision limitation (safely
  refused before any effect, not previously admitted: 17f7964c refuses them too). Fixtures in
  tree, run by `test-runtime2-b5`: `tests/runtime2/b5/precision/m3_nested_loop_shared.f` (the
  review's `probes3/d05`, legacy `[[1,7],[2]]`) and `m3_closure_array.f` (`probes/c02`, legacy
  `[[1,9,8],[2,9]]`); their goldens pin the refusal, so a future admission is a visible change.

## 9. Gate results (binary 9e9f4466, GCC 13.3; local, not certification)

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
