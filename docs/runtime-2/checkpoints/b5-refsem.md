# Checkpoint B5: guard removal, the reference-semantics switch (D-NEXT-refsem-b)

Status: **FREEZE-READY (B5 13), with open owner decisions (section 10.11).** Owner decisions of
2026-11-07 06:55: D1 approved and fixtured, D2 recorded as a release blocker, D3 accepted and gated,
D4 (bounded Option 2) STOPPED with its cause traced (10.5). Final gates: section 10.8. History: earlier stop at the owner's B decision point
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
Counts after B5 12: **i 166, ii 1 (E-U6-1, owner decision pending), iii 7 (incl. the D1 row), p 9**;
no PENDING-OWNER row remains.
Approved differences are pinned exactly: `.r2.stdout/.stderr/.exit` goldens with
`approved=<rule>` (for-s2, closures-s1, vm-corpus/cp11), `B5_APPROVED` (capture_origins),
`APPROVED` (cp14b functional); each check also requires the pinned result to differ from legacy.
**PENDING-OWNER (owner guidance 2026-11-07 06:47):** the capture_origins "D5 ordinary binding"
sub-family is NOT a snapshot effect. Witness with no source mutation:
`f:=()=>0; a:=[1]; for(x:a){f = ()=>x; x=7}; print(f())` gives legacy 1, B4b tip 1, B5 7. Cause:
B5 2 set `ForState::live = false`, which disabled the lazy loop binding and its selector
descriptor (through which a closure captured the loop variable as a source location that
rebinding did not reach); B5 4 deleted that code. The new behaviour (a closure shares the loop
binding, as for any captured local) is a capture-model change awaiting the owner's decision; its
expected-changes row is PENDING-OWNER, not accepted. (The live-reads and element-mutation
sub-families are snapshot/shared-reference effects.)

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

**B. `trim` and dynamic dispatch (approved 04:47): trim part IMPLEMENTED (B5 7, B5 8); sort part
STOP.**
- Mechanism (B4a's one admission table and its run-time kind check, no new opcode or path):
  `admit_dynamic_method` accepts a plain union that also holds legacy's tagged values (map,
  set, stack, struct instance); each is a failing family of a plain method name with legacy's
  probed text "unknown value or malformed expression: <call>" (B0's unrenderable text stays for
  interpolation's `to_string`). Still refused: unknown kinds, handles/functions, a tagged family
  with its own native of the name (`contains` on a stack), a struct type declaring the method, a
  NAMED struct receiver (legacy says "struct has no method: <name>" there, by syntax: the table
  has one text per family), observable arguments with a failing family (B4a rule). At run time
  `tagged_dispatch_failure` gives a struct instance (no built-in family) its failure.
- Static sites unchanged by construction: for every kind set without tagged bits the admission
  computation is the old one (the only sites whose admission changes were refused before); b4
  (12,684) and b4b work gates unchanged. Cost on a newly checked site: +26 instructions per call
  (callgrind, +2.0% of a trim loop iteration; `scratchpad/b5/trim/overhead.txt`). Wall clock: on
  the Linode certification list (owner rule).
- Order and evaluation: the receiver is evaluated once into CallNative's register (fixture
  `b5/dispatch/trim_receiver_once`); arguments must be unobservable when a family fails (fixture
  `trim_observable_argument_refused`); the failure precedes arguments. Copy-sort-commit and
  receiver identity are untouched.
- The 1,276 programs (`scratchpad/b5/trim/`): classified before running (`classify.py`,
  `expected.json`; v1 kept as `expected.v1.json`, v2 adds the sort STOP and the async/thread
  copy rule after the v1 run, both recorded), checked by `check.py` on the probe build
  (comparator `sort` in the programs; see section 4): 1,179 as expected; 97 not, all explained
  (`discrepancies.tsv`): 52 async/thread where the task's argument graph is a copy (Q4, owner
  2026-11-01; legacy refuses the struct inside the thread), 20 receiver-returning
  `reverse()`/`sort()` aliasing (B4b receiver identity, Q7, Q3 row 1: the store reaches the
  receiver; the ce0a9a44 oracle copied), 13 still refused by a pre-existing `method:splice`, 12
  still refused `builtin-receiver:trim` (receiver through a function-returned `reverse`). No
  internal failure, tag or dispatch defect in any run.
- Mutants: T1 (B5 11: the tagged families get no failure) and T2 (B5 12: the tagged sites
  compiled static, no run-time check) are caught by `b5/dispatch` (`test-runtime2-b5-mutants`).
- **Sort part (452 `collection-element:sort`): STOP.** These programs are refused at an
  argument-less `xs.sort()` over an array that may hold a map/struct (`compiler_native_args.cpp`,
  the D-063 tag-observing rule: legacy sorts its collections as tagged strings,
  `[m, " a "].sort()` runs in legacy and keeps the tag order). Admitting them needs a run-time
  check on the ELEMENTS of the receiver (no tagged element at sort time, else a defined error),
  not on the receiver's family: B4a's table and precondition check the receiver only. That is a
  second mechanism; the owner's rule says stop. In the generated programs the sort runs before
  the tagged store, so each would print legacy-differing results only through Q3 sharing.

**Comparator sort, owner guidance 2026-11-07 06:47 (item 1).** There is no compiler, stdlib or
run-time failure. Facts: host GCC `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`, libstdc++
`__GLIBCXX__ == 20240904`; production flags `-std=c++17 -O2 -Wall -Wextra -pedantic -pthread`, no
`_GLIBCXX_*` macros. `src/rt/compiler_collection_sort.cpp` line 8:
`#if (!defined(__GLIBCXX__) || __GLIBCXX__ != 20260321) && !defined(NIFT_RT_REFERENCE_PROBE)`
`return fail(node,"method:sort");`. So production refuses `sort(comparator)` before any effect
(`not compiled for the VM (method:sort)`), identically on the B4b tip 17f7964c's production
binary (`cand-b4/nift`, same probe). Class: a deliberate toolchain-profile gate (owner decision
2026-10-25, CP14B D4: the comparator's observable call schedule was frozen against GCC 15.2's
libstdc++), not a build-flag issue, not B5's copy-sort-commit, not a harness defect. It is a
portability limitation on every non-frozen toolchain (with the VM default and no fallback, such
programs do not run); on the GCC 15.2/20260321 certification instance production admits it.
B5's comparator-sort evidence locally is the probe build only (`NIFT_RT_REFERENCE_PROBE`), which
is not production evidence: production comparator sort is exercised only on the certification
instance. Not blocking as a wrong result; needs the owner's portability ruling.
On item 2 (the 452 sort cases): they are not admitted (sort part STOP above), so no dynamic sort
path exists to test; the guarantees list is the acceptance criterion for that part if the owner
authorises the element-kind check.

**Comparator sort on this host (owner question 5).** Not a `_GLIBCXX` assertion and not a
strict-weak-ordering check: a Nift-side compile-time gate (owner decision 2026-10-25, CP14B D4):
`compiler_collection_sort.cpp` admits `sort(comparator)` only when the binary is built against
the libstdc++ whose comparator call schedule was frozen (`__GLIBCXX__ == 20260321`, GCC 15.2),
because the comparator's side effects observe std::sort's call order; elsewhere it refuses
`method:sort` before any effect (this host: GCC 13.3). Production therefore differs by
toolchain: the certification instance with GCC 15.2/20260321 runs comparator sort; any other
toolchain (other GCC versions, clang/libc++, macOS, FreeBSD, Windows) refuses every comparator
sort, and with the VM as the default engine (no automatic fallback) those programs do not run.
It is a safe refusal, not a wrong result, so not a correctness defect; it is a release
portability gap the owner should rule on (list it for the release-blocker audit).

## 9. Stop point

Superseded by section 10 (B5 11 to B5 13, replacement implementer). The historical batch of
B5 8 to B5 10 (`scratchpad/b5/gates/fin-*.result`) is kept as evidence for tip 3ffedcac/f572d8cd.

**Gate count correction (9.1 said 40 PASS / 12 FAIL).** The 52 `fin-*.result` files hold 13
failing make targets and 39 passing:

| Failing target (fin batch) | Fixture-level failures inside it | Disposition (section 10) |
| --- | --- | --- |
| cp25r-inventory | 6 unclassified reasons (coro-blocking-host:file/:rm, method:open/write/save/close) | pre-existing, identical on an exact 17f7964c build |
| gen1 | harness crash: input path in a deleted reviewer worktree | harness fixed (candidate tree, loud failure on a missing input) |
| gen_struct | same | same |
| test-cp19-admission-mutants | B5: case `loop` control (3 CHECKs); 17f7964c: case `wrapper` control (2 CHECKs) | pre-existing target failure; its content changed: see 10.6 |
| test-cp19-proof-cli | needs `CP19_LEGACY_ORACLE` | pre-existing, identical |
| test-cp19-s1-dispatch | `run()` refused `callable:run` (8 checks) | pre-existing, identical |
| test-cp19-session-mutants | session-m7 contract witness | pre-existing target failure; content changed: see 10.6 |
| test-cp19-shared-eval | expects `NIFT_VM=require` in a refusal note | pre-existing, identical |
| test-runtime2-cp14b-witnesses | 8 copy witnesses (the log's last line named only `adversarial-copy-function`); const_admission never ran (make stopped) | converted (10.4) |
| test-runtime2-cp14b | `script_return_kernel_error` | pre-existing, identical |
| test-runtime2-cp22 | 2 of 90 refuse cases (`r_share_alias{,_task}.f`) | converted (10.4) |
| test-runtime2-cp23 | 2 of 90 classifications (same two) | converted (10.4) |
| vm-adversarial | 67 non-approved differences | analysed in 7 groups, pinned per program (10.4) |

## 10. Owner decisions of 2026-11-07 06:55 and the freeze (B5 11 to B5 13)

### 10.1 D1 loop-variable closures: APPROVED, fixtures pass (B5 11)

The VM already had the required semantics (no code change): `tests/runtime2/b5/loopclosure/`
(8 fixtures, NIFT_VM=require, in `test-runtime2-b5` on production and the probe build): distinct
binding per iteration (l01), same-iteration closures share later assignments and iterations are
independent (l02 setter/getter pairs, l03, l07), while-body bindings (l04), closures escaping the
loop and the function in objects inside arrays (l05), nested loops (l06), rebinding after capture
with no source mutation (l08: legacy 1, B4b tip 1, B5 7). VM-supported syntax: closures stored with
`push` and called through a local after the loop (`f0 := fs[0]; f0()`). capture_origins'
PENDING-OWNER row is now class iii "D1 loop-variable closures" (an intentional capture change,
distinct from snapshot iteration); compatibility.md updated.

### 10.2 D2 comparator-sort portability: RELEASE BLOCKER (recorded, nothing changed)

The GCC 15.2 (`__GLIBCXX__ == 20260321`) profile gate in `compiler_collection_sort.cpp` is kept
unchanged. Release blocker for the post-certification investigation (owner 06:55: deterministic
portable sort preserving comparator invocation order, error behaviour, allocation failure,
copy-sort-commit and identity; bounded stable merge sort vs the existing one). **The combined
Linode campaign must build with GCC 15.2** (it is the only place production comparator sort is
exercised); cross-platform release validation must cover the other supported toolchains (which
refuse comparator sort, `method:sort`, before any effect). Comparator-sort atomicity on this host
is verified on the probe build only (`b5/sort`, copy-sort-commit, failure injection at every call,
identity, catchable receiver mutation; mutant 8): NOT production evidence.

### 10.3 D3 the plain `.sort()` refusals: ACCEPTED as a documented precision limitation (B5 11)

`tests/runtime2/b5/sort_refusals.py` over `docs/runtime-2/evidence/b5/sort-refusals.jsonl` (564
gen_alias programs: the 452 sort-kind ones and 112 trim-kind ones with an argument-less sort on the
stored-into array), in `test-runtime2-b5`: every program exits 1 with no stdout, its directory
unchanged, an ordinary `error:` diagnostic naming the reason and saying nothing ran, no internal
failure. Production: 549 `collection-element:sort`, 15 `method:sort` (the program also has a
comparator sort: the D2 gate refuses it first); probe build: 564 `collection-element:sort`. None
of the 564 was admitted by the B4b tip (390 collection-element:sort, 112 builtin-receiver:trim, 47
reference-guard/aliasing, 15 method:sort), so no previously admitted production program regresses
through this limitation (owner: that would have needed a release-blocker decision). No element
dispatch mechanism was added.

### 10.4 Conversions found by the final batch (B5 11), each with its rule

Every converted program was refused before any effect by the B4b tip (class i); each new result
was checked by hand derivation or against legacy before its expectation changed:
- cp22/cp23 `r_share_alias.f` (1, 2: legacy-equal) and `r_share_alias_task.f` (2, 1: the task's
  push reaches only its graph copy, D7/Q4): `CP22-CLASSIFICATION.tsv` rows now `vm - legacy-golden`.
- cp14b copy witnesses: 6 legacy-equal; generated-102/162 differ in one line each (`mut8(a9)` then
  `a9` is `[1,2,99]`, legacy `[1,2]`): Q3 "Call arguments", pinned in `B5_DIFF`.
- cp14b const admission: 4 element stores through a `const` binding run (Q2 B; legacy "cannot
  assign to const binding"); `for(v:a){v.push(9)}` legacy-equal (section 3 Iteration).
- vm_adversarial: 67 differences in 7 root-cause groups (`scratchpad/b5/adversarial/GROUPS.md`,
  minimal reproducers with legacy, B4b-tip and B5 results): G1 `to_string` on a map, B0 text (32);
  G2 `trim`, item B text (8); G3 `to_upper` on a map, an ordinary error where legacy prints its
  internal collection tag and exits 0 (8; owner 04:47 "never a leaked tag"); G4 Call arguments
  (5); G5 Extraction through `?:`/`??` (12); G6 `q := (p)` (1); G7 `return:` prefix (1, already
  B4b behaviour). No exit 70; evaluation order unchanged (same output prefix / same line count);
  no file effects. Overlap with D1/D4: none. Each pinned by source sha256 with its exact result
  (`tests/runtime2/b5/adversarial-approved.json`); any other difference still fails.
- cp25r-inventory: NOT converted: pre-existing (section 9 table, 10.6).

### 10.5 D4 cp19 shared-callable (bounded Option 2): STOPPED, nothing implemented (B5 12)

Traced on an instrumented build (reverted; production binary rebuilt byte-identical): the
refusal is not caused by `plain()` clearing heap callable targets. In statement 2 the parameter
`v` of `ap` is seeded historical (no targets), so `v()` takes the fact-proved historical-call
path; `session_effects` sets `earlier_called_` and `session_open()` opens every exposed class,
and under U6 the two session-heap classes are always exposed. Every later read of an earlier
array or object is then `any` (monotone), `q` becomes untracked and the call unproved. A
diagnostic build that only skips opening the heap classes admits the witness and prints 1 (the
B4b result) with no Option 2 change. Option 2 is therefore neither sufficient nor, for the
witness, necessary; sound admission needs a persisted, transitive per-callable heap-EFFECT
summary checked against a complete scan of the session's targets: new session effect tracking,
outside Option 2 (owner STOP clause). Estimate 250-400 production lines, high risk
(`scratchpad/b5/cp19/OPTIONS.md`, "B5 D4"). E-U6-1 stays class ii, refused before any effect:
**owner decision needed.** The same family also explains cp19 admission-mutant case `loop`
(10.6). Not affected: D1 (no session), the vm_adversarial groups.

### 10.6 Pre-existing failures, on an exact 17f7964c build (temporary worktree, deleted)

`git worktree add --detach` 17f7964c (tree ef8c508d), `make nift` (339 s), each target run there
(`scratchpad/b5/gates17f/`). A pre-existing failure is not a waiver: each goes to the
release-blocker audit.

| Target | 17f7964c | B5 | Cause | Resolve before release? |
| --- | --- | --- | --- | --- |
| test-runtime2-cp14b `script_return_kernel_error` | FAIL | FAIL, identical | `a := []; return a.pop()` refused `type:script-return` (a B3 N1 refinement regressed after B3) | yes: a precision regression of an admitted form; release-blocker audit |
| test-cp19-proof-cli | FAIL | FAIL, identical | needs `CP19_LEGACY_ORACLE` (the frozen legacy binary) | harness input: must run (with the oracle) on the certification instance |
| test-cp19-s1-dispatch | FAIL | FAIL, identical | `run()` refused `callable:run` | no for VM support (owner release scope defers run()/cmd()); the target's expectations must be reclassified to the release scope before certification |
| test-cp19-shared-eval | FAIL | FAIL, identical | expects `NIFT_VM=require` in a refusal note the CLI no longer prints | yes (stale harness expectation; fix the harness or the note) |
| test-cp19-admission-mutants | FAIL (case `wrapper` control: probe refused by the Phase A guard) | FAIL (case `loop` control: `session:current-callable-proof`; `wrapper`, `field` pass now) | B5: the E-U6-1 family (a historical closure calling a callable read from a captured array, `scratchpad/b5/cp19/loop-witness.in`); previously admitted by the B4b control, refused before any effect now | yes: tied to the E-U6-1 owner decision (10.5) |
| test-cp19-session-mutants | FAIL (m7 contract: statement 2 refused by the guard) | FAIL (m7: statement 2 admitted now, statement 3 `nested[0][1]` refused `type:index`) | B5: U6's single heap pool for every depth (nested[0] may be a number or callable); not admitted at the B4b tip either | yes: harness witness must be rebuilt for B5 semantics, the `type:index` precision noted |
| cp25r-inventory | FAIL (after `make .build/rt-parse-dump`) | FAIL, identical 6 reasons | classify-all compiles `rt-diff-corpus/async/{fileops,thread}` without the cooperative switches (the tooling artefact of the existing `coro-blocking-host:exists` row); both run on the VM in real runs at 17f7964c and B5 | yes: classify the six reasons (tooling) or fix classify-all's switches |
| test-runtime2-vm-corpus | 155 FAIL lines | 124 (subset) | default-environment goldens (section 5) | release-blocker audit |

### 10.7 trim audit evidence (scratchpad/b5/trim/)

`expected.v1.json` (original) and `expected.json` (v2) unchanged; `expected.v3.json` adds the
corrected expectation of the 97 with both originals alongside. `CORRECTIONS.tsv`: 284 rows, one
per changed expectation (187 v1->v2, 97 v2->v3): program, stage, category, original expected,
corrected expected, actual on the B5 probe build and on production, the B4b tip and legacy, and
the rule. v1->v2: 114 sort-refusal rows (D3), 73 async/thread rows (D7/Q4 graph copy; expectation =
legacy's output). v2->v3: 52 async/thread (category a: section 3 "Threads, parallel async" /
"Cooperative async (D7)": instances cross with their descriptor, "dL for instances (legacy
refuses)"; v2 kept legacy's `future: undefined callable: C`); 20 reverse()/sort() identity
(category b: Q3 "Call arguments"/"Returns"/"Element/field store" + B4b 32 receiver identity,
`kCollectionResults`; + Q7 for the comparator context: the v1/v2 oracle ce0a9a44 gave the result a
new site and copied); **25 FLAGGED, tied to no approved rule**: 13 still refused `method:splice`
(category c, a classifier omission; the refusal itself pre-exists) and 12 still refused
`builtin-receiver:trim` (category d, the receiver flows through a function-returned `reverse()`; a
remaining precision gap of item B). All 284 B5 results are refusals before any effect, success or
ordinary errors; no exit 70. The 54 rows whose B5 result does not match their v2 correction are
exactly those whose v2 expectation the v3 stage corrected. 36 rows differ between the probe and
production builds, all by the D2 gate (`method:sort`). This is evidence for the reviewer, not a
classification by the implementer.

### 10.8 Gates on the freeze code (B5 12, `scratchpad/b5/gates/final-*.result`)

Batch `scratchpad/b5/final_gates2.sh` on B5 12 (`c775c425`, tree `ba9dd0be`; B5 13 is this
record only), production binary sha256 ac283072..., each result with its command, exit code and
summary in `scratchpad/b5/gates/final-<target>.result` (logs `final-<target>.log`); every make
target's repository status before/after unchanged.

**PASS (46):** test-rt-lint, test-runtime2-b3, -b4, -b4b, -b5 (73 fixture programs incl.
loopclosure; sort-refusals 564), -b4b-fixpoint, -b4-mutants, -b4b-mutants, -b5-mutants, -guardoff,
-refsem, -refsem-mutants, -collections, -packages, -args-cmd, -args-cmd-mutants, -safety,
-coro-module, test-rt-refrep, test-rt-refrep-mutants, test-runtime2-vm-default, test-cp19-session,
-session-cli, -proof, -proof-mutants, test-runtime2-for-s2, -for-s2-mutants, -closures-s1,
-closures-s1-mutants, -capture-origins, -cp14b-design-mutants, -cp14b-witnesses, -cp14b-sort,
-cp18-cow, -cp22, -cp23, test-cp25r, test-runtime2-rt-diff, -d040-witnesses, vm-unit, vm_parity
(--gen 300 --seed 1), vm_adversarial (--seed 1 --count 600: 0 non-approved), gen1 (production,
20,274 programs, 0 bad), gen_struct (production, 1,911, 0 bad), gen_alias (production, 12,600, 0
bad), sort-refusals on the probe build.

**FAIL (7 make targets), all pre-existing (10.6), failure signatures identical to the fin batch:**
cp25r-inventory (6 unclassified reasons), test-cp19-admission-mutants (case `loop` control only:
the other 13 cases' controls pass when run one by one), test-cp19-proof-cli, test-cp19-s1-dispatch,
test-cp19-session-mutants, test-cp19-shared-eval, test-runtime2-cp14b (`script_return_kernel_error`).

**Negative controls (expected to fail, and do):** gen1 and gen_struct pointed at a missing tree
(`NIFT_TREE=/nonexistent/tree`) exit 2 with "required input missing: ..." and write no programs
(`final-gen1-negative-control.result`, `final-gen_struct-negative-control.result`). The
generators (`scratchpad/b5/gtools/`) read the builtin table from the tree the nift binary was
built in (or NIFT_TREE) and fail loudly when it is missing or truncated.

**Repository state:** `git status --porcelain --ignored` after the batch lists only ignored build
products (objects, dependency files, `.cfg` compile-configuration sidecars of
`scripts/build/compile-object.sh`, `.build/`, `nift`) plus three `__pycache__` directories
(interpreter caches of harness runs made without PYTHONDONTWRITEBYTECODE, some older than this
session): deleted; the re-check lists build products only
(`final-git-status-ignored-after-cleanup.txt`). No tracked or untracked change.

Verified by these gates: snapshot iteration (b5/q3 r05/r06, refsem loop rows, for-s2,
capture_origins live-read rows; the 1M-element snapshot memory gate `test-runtime2-b5-snapshot-memory` (B5 9) runs `for_s2.py`, the same command as test-runtime2-for-s2, cases g9_million/g9_million_write under a 40 MB heap budget),
closure bindings (b5/loopclosure, closures-s1, capture_origins D1 rows), comparator-sort atomicity
(b5/sort and cp14b-sort on the PROBE build only, see 10.2).

### 10.9 Measures (deterministic; wall clock is Linode only)

B5 11 to B5 13 change no production source: the production binary is byte-identical to the one
the fin batch measured (sha256 ac283072...). Measures already recorded stand: trim item B +26
instructions per newly checked call (callgrind, +2.0% of a trim loop iteration; static sites
unchanged); 1M-element loop snapshot peak 32.8 MB (array + snapshot, +0.95x, plan section 7 allows
+1x; permanent gate). No new instruction or memory cost from D1 to D4.

### 10.10 What Linode must cover

GCC 15.2 build (D2: production comparator sort, `b5/sort` on production, cp14b-sort on
production); the full `make test`, sanitizers (incl. `test-runtime2-guardoff-sanitize`), the
regression suite, the cp19 targets with `CP19_LEGACY_ORACLE`; wall-clock: trim checked-path
overhead and the website-build benchmarks against v4.12; the 1M snapshot memory gate.

### 10.11 Owner decisions still needed

1. E-U6-1 / D4: accept the refusal as a documented precision limitation, or approve the
   heap-effect-summary design (10.5); the cp19 admission-mutant `loop` case follows it.
2. The 25 flagged trim-audit rows (10.7) are refusals before any effect with no approved rule
   (precision gaps, not semantic changes): reviewer/owner classification.
3. The release-blocker audit list of 10.6.
