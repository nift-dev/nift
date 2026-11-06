# Checkpoint B5: guard removal, the reference-semantics switch (D-NEXT-refsem-b)

Status: **IN PROGRESS, NOT FROZEN.** Stopped at a clean commit at the owner's B decision point
(section 8): A (E-U6-1) is a STOP with alternatives; B (`trim`/dispatch, approved 2026-11-07
04:47) is not implemented. The full gate set was deliberately not run (the coordinator requires
it on the final code, after B); a pre-B smoke set was (section 5). Branch `cand/b5-refsem` from
the accepted B4b tip `17f7964c` (tree `ef8c508d`). Commits B5 0 (B4b closure), B5 1 (U6), B5 2
(the switch, Q7, loop snapshots, first conversions), B5 3 (record), B5 4 (lazy loop locations
and selector cells deleted, B2 L2; loop/closure/cp14b/cp18 conversions), B5 5 (cp11, rt units,
mutant retirements, cp14b design mutants, classification, compatibility), B5 6 (this record).
Plan: Phase B plan section 3, 5, "B5"; owner decisions 2026-11-02, addendum 2026-11-04, owner
rulings 2026-11-07 03:53 and 04:47.

## 1. D-NEXT-refsem-b (decision record)

Arrays and objects are shared mutable references in the production VM, like maps, sets, stacks
and struct instances (Phase A). The Phase A forward-compatibility guard and the `aliasing`
location-alias refusal are deleted; no replacement static mechanism. Plan §3 is the contract, with
the owner's rulings: Q1 `args` immutable through every alias (kImmutable, the error names the
receiver expression); Q2 option B (`const` is binding-only: `const a := [1]; a.push(2)` runs);
Q7 comparator sort copy-sort-commit (2026-11-07); D5 entry-snapshot iteration for every loop
(B2 coordinator ruling 1 moved the binding-loop snapshot to B5). Legacy (`NIFT_VM=0`) unchanged.

## 2. Static machinery (plan section 5 list)

| Item | B5 |
| --- | --- |
| 1 guard (`reference_guard{,_walk,_call}.{h,cpp}`, the call in `compiler.cpp`) | deleted (1,043 lines) |
| 2 `UnitCompiler::no_alias`, refusal `aliasing` | deleted, with `collection_copy.{h,cpp}` |
| 3 take/put | gone since B2 except the compound-assignment string take (`g += x`), kept by B2 ruling 4 |
| 4 static const-array path | deleted (Q2 B); B2 L3 (its validation order) is gone with it; `declare-const` for non-array containers stays |
| 5 guard-only inference facts | deleted; B4b's L-2 monotonicity exception disappears with them |
| 6 live array-loop path | deleted (B5 4): lazy `for` bindings (`Binding::lazy_*`), `for_lazy_read`, `for_source_may_change`, the loop selector descriptor and every `selector` branch (assign, compound, increment, atomic, const, cells, closure capture, array mutation, sort, globals). Every loop emits `IterCheck c = 1` and iterates its entry snapshot. Kept: the vanished-element scan (`for_source_may_shrink`), which still gives the CP12C `for-capture-vanishing` and `for-missing-write` refusals; under the snapshot no element can vanish, so both are now over-refusals (for-s2 t21, t27): a precision follow-up, not changed here |
| selector cells (B2 L2) | deleted (B5 4): kind -2 cells, `cell.cpp` `replace_path` and the cached-path store; no compiled code can create one. The cell opcodes keep their names; a cached access is the plain cell access |
| snapshot budget | `reserve_snapshot` charges an array n Values (was the keyed bound, 2.25x), since every binding loop now snapshots (1M-element loop: peak 32.8 MB = array + snapshot, +0.95x; plan section 7 allows +1x) |
| 7 presentation/receiver refusals | superseded by B4a/B4b; `builtin-receiver`, `collection-element`, ... stay the admission table's Refuse outcomes until B (section 8) |
| `reference_probe.h` | kept as the comparator-profile test configuration only |

## 3. U6: session persistence (B5 1; mandatory blocker, owner ruling item 2)

Mechanism (`infer_session.cpp`, `infer_sites.cpp`, `compiler.cpp`, `session_shape.cpp`):
- **Session heap.** Every array or object an earlier statement made that has no site in this unit
  is in one of two heap classes (arrays' elements, objects' members; an array never aliases an
  object), seeded with every kind any earlier value's contents may hold
  (`SessionCompileContext::heap/heap_objects`, the recorded structures of non-private bindings,
  capture/parameter/call-result seeds). Every store through any earlier value grows it and
  re-walks its readers; it is exposed (earlier code reaches it, C5). Monotone across statements.
- **Private classes.** A binding the statement proves private (every class it reaches, at every
  depth, is reached by no other binding, lambda-captured slot, struct field or the heap; one class
  per depth; nothing open, callable, instance or collection) keeps its own class next statement,
  seeded with its materialized structure (`SessionSymbol::heap_contents`); `args` starts private.
- Recorded structures are materialized from the sites; persisted shapes carry no site numbers.

Evidence: `expected_u6.txt` is empty; the five former entries run in the guard-off session suite;
`tests/runtime2/b5/u6/` has 38 session fixtures with goldens, covering the owner's list (a function
compiled before a mutation and called after; successive mutations changing kinds; aliases and
nested containers; closures capturing session variables; schema replacement; mutation through
functions defined in later statements). 13 of them exit 70 on the B4b tip's guard-off build; 0
now (each gives the §3 result or a refusal before any effect). Mutants B5-U1 (recorded structure
kept), U3 (unmaterialized export), U4 (everything private) are caught; U2 is equivalent (every
store's container is read through `sited()`), retired.

**Precision cost E-U6-1 (owner reopened it 2026-11-07 04:47; section 8 A: STOP, alternatives listed):** cp19 proof `shared-callable` (an escaped
closure's unknown parameter stored into a closure-captured array, read by a sibling closure) was
admitted with a run-time callable guard; it is now refused before any effect
(`session:current-callable-proof`). CP19 admission mutant 13 (capture-location sync) is subsumed
for array contents by the heap; its proof witness now checks the refusal on both binaries.

## 4. Comparator sort (owner Q7 ruling 2026-11-07); B2 L1, L2, L3

`collection_sort.cpp`: the sort runs on its working copy and commits it into the receiver once
(`Protocol::commit_sorted`) on success; the error drain commits nothing. Fixtures `b5/sort/`
(run on the probe build, since this host's libstdc++ profile refuses comparator sort in
production; they run on production where the profile matches): aliased reads during the sort see
the original order (L1), deterministic failure injection at every comparator call leaves the
receiver and every alias unchanged, identity kept, receiver mutation is a catchable error.
Mutant 8 (in place again) is caught. **L2** resolved by deletion (section 2). **L3** gone with the
static const path (B5 2).

## 5. Gates (local; `scratchpad/b5/gates/`)

Not the freeze gate set (that runs on the final code, after section 8 B). Pre-B smoke on the
B5 5 code (`scratchpad/b5/gates/pre-*.result`; `pre2-test-rt-lint.result`), all PASS:
`test-rt-lint` (after deleting two stale `cell.cpp` annotation-manifest lines, B5 6), `-b3`
(468), `-b4` (12,684; op-admission-unit 4,027), `-b4b` (6,629; repo state unchanged), `-b5` (57),
`-b5-mutants`, `test-rt-refrep` (129), `-refsem-mutants` (19 caught), `-closures-s1-mutants`,
`-for-s2-mutants` (M3-M7), `test-cp19-proof` (393), `-session-cli`, `-args-cmd`, `-packages`
(831), `-safety` (fuzz 33,512), `-coro-module`. During the conversions (B5 4/5): `-for-s2`,
`-closures-s1`, `-capture-origins`, `-refsem` (566), `-collections` (280), `test-cp19-session`,
`-cp18-cow`, `-cp14b-design-mutants` (10 detected, 1 retired, 1 comparator unverified on this
libstdc++ profile), cp14b functional/effects/origins, `vm_parity.py` corpus, rt units (vm-unit
40,347, globals, modules, prepared-parse 100,136).

Known pre-existing failures (identical at 17f7964c, not B5's):
- cp14b functional `script_return_kernel_error` (`a := []; return a.pop()` refused
  `type:script-return` at the B4b tip too: the B3 N1 refinement regressed after B3; checked by
  running the 17f7964c harness on the 17f7964c binary).
- `test-runtime2-vm-corpus` (`vm_corpus.sh`, default VM-on environment against legacy goldens):
  155 FAIL lines at 17f7964c, 124 on B5 (a subset; B5 removes 31).
- cp19 `-proof-cli` (needs `CP19_LEGACY_ORACLE`), `-s1-dispatch` (`run()` unsupported by the VM:
  release scope), `-shared-eval` (expects `NIFT_VM=require` in a refusal note the CLI no longer
  prints), `-admission-mutants`, `-session-mutants`: identical on the B4b-tip code per the
  previous implementer (`base-*.result`, built at B5 0 = B4b code); a run on an exact 17f7964c
  build is still to do (section 9).

## 6. Expected-output changes

`docs/runtime-2/evidence/b5/expected-changes.tsv` (one row per fixture or case family:
previous, new, reason, establishing test). Classes: **i** newly admitted under shared-reference
semantics 158 rows; **iii** previously admitted, behaviour changed by an approved rule 7 rows
(D5 entry snapshot and ordinary loop binding: for-s2, capture_origins; Q7; Q2 B; cp14b const);
**p** outcome preserved, only the expectation's form changed 7 rows (a heap budget, a refusal
reason, a stale pre-existing expectation, a harness baseline, mutant retirements); **ii**
previously admitted, behaviour changed without an approved rule: 1 row, E-U6-1 (section 8 A).
Approved differences are pinned exactly: `.r2.stdout/.stderr/.exit` goldens with
`approved=<rule>` (for-s2, closures-s1, vm-corpus/cp11), `B5_APPROVED` (capture_origins),
`APPROVED` (cp14b functional); each check also requires the pinned result to differ from legacy.
The capture_origins "D5 ordinary binding" sub-family (a closure sees a later assignment to the
loop variable: legacy 1, VM 7; `descriptor_write_calls` 2 2 against 2 3) follows the section 3
Iteration row ("the loop binding is an ordinary binding"), not a Q3 example row: reviewer focus.

## 7. Mutants

B5 suite (`tests/runtime2/b5/mutants.sh`): U1, U3, U4, R-B6, R-B7, 8 (comparator sort in place).
args-cmd mutant 5 redefined. CP19 proof mutant 13 subsumed. Retired with a reason: refsem R13,
R17, R18, R20 (obsolete: the guard they mutated is deleted); for-s2 M1, M2 (obsolete: the live
re-read path is deleted; refsem R-B3, run by `test-rt-refrep-mutants`, makes the loop live);
cp14b design mutant removed-const-guard (obsolete under Q2 B). Reinstated: the cp14b design
mutants' guard-excluded witnesses (copied-mutation-receiver, live-source-not-snapshot detected).
B5 U2 equivalent (retired in B5 1).

## 8. Owner decisions of 2026-11-07 04:47

**A. E-U6-1 (cp19 `shared-callable`) reopened: STOP, alternatives below, nothing implemented.**
- Exact trigger: a later statement calls a historical closure (here `o.readv()`; also
  `o.putv(()=>2)` alone, and `o.readv()` alone) whose body passes an element of a
  closure-captured array as a callable argument to a session function whose prototype needs call
  facts (`applycb(q)`, `q := a[0]`). Minimal witness (B4b admits, B5 refuses):
  `fn(ap(v)){return v()};fn(mk()){a:=[()=>1];return {"f":()=>{q:=a[0];return ap(q)}}};o:=mk()`
  then `o.f()`. No escaping store is needed. Not refused: the same without `ap` (`q()` directly),
  `ap(x)` with a captured scalar binding, the U6 fixtures.
- Mechanism: U6's session heap (B5 1) materializes earlier array contents into heap classes
  whose callable types lose their targets (`plain()` in `infer_session.cpp`: callables cleared,
  `historical_callable` set). The closure's recorded call fact for `ap(q)` therefore has an
  unproved shape; `prepare_session_history` stages no seed, `session_historical_call` adds the
  call to `unproved_current_calls`, and `validate_session_history` refuses
  (`session:current-callable-proof`).
- The program is valid (B4b: `WROTE`, then the ordinary error "arithmetic operators require
  numeric operands"). It cannot be restored with a narrow correction: the historical closure's
  compiled code was proved against the heap of its own statement, and admitting a later call
  needs either a proof that the heap classes it read have not grown since, or callable targets
  persisted in the heap classes. Both change U6's dependency model.
- Alternatives (owner choice): (1) heap-version dependencies: record per prototype the heap
  classes (kinds and callable targets) it was compiled against; admit a later call when the
  current heap is within them, else refuse (the B4b-style call admission returns where the heap is
  unchanged); medium change in `infer_session.cpp`, `session_history.cpp`, `session_shape.cpp`.
  (2) Persist callable shapes in heap classes (as non-private symbols persist `callable_shape`),
  so the call fact stays proved while the heap's callable set is complete; an escaped unknown
  store still refuses. (3) Keep the refusal (status quo), recording the broader witness family.
  Tests required for (1)/(2): the original program, sibling closures, an escaping closure storing
  into a captured array, a nested container, repeated kind-changing stores (owner list).

**B. `trim` and dynamic dispatch: APPROVED (PROPOSAL.md), NOT IMPLEMENTED (stop point).** Scope
facts gathered: the 1,276 programs are in `scratchpad/review-b4b-r5/galias/off.jsonl` (`src`
field; refused with `builtin-receiver:trim` 824, `collection-element:sort` 452). The `trim`
part is `op_admission.cpp` (`admit_dynamic_method`, `dispatch_failure`); the `sort` part is a
different refusal (`compiler_native_args.cpp` / `compiler_collection.cpp`
`collection-element:<name>`, plain sort over an array that may hold a map/set/stack), which the
proposal's section (c) does not cover: its legacy behaviour must be established first.

## 9. Not done (exact stop point)

Tip after B5 6. Remaining, in order:
1. B (section 8): implement the `trim` admission in `op_admission.cpp`; establish legacy's
   behaviour for the 452 `collection-element:sort` programs and decide whether they are in B's
   scope; classify all 1,276 by expected outcome before running (table under
   `scratchpad/b5/trim/`); mutants T1, T2; overhead (instructions and wall clock) on new and
   unaffected paths; identical bytecode for static sites.
2. A: the owner chooses an alternative (section 8 A).
3. The full gate set on the final code: fixpoint, b4/b4b mutants, generators (gen1, gen_alias,
   gen_struct, ...), CP25-R re-inventory, classification and semantic differentials,
   CP22/CP23/VM-unit/adversarial lanes, all earlier gates; the five cp19 targets on 17f7964c;
   local benchmarks against 17f7964c (compile and run time; the 1.5x compile target).
4. Freeze record.
