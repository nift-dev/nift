# Checkpoint B5: guard removal, the reference-semantics switch (D-NEXT-refsem-b)

Status: **FINAL FIX-UP FROZEN (B5 24, section 13) on `3c9c66f4`, after the targeted re-review's
ACCEPT-WITH-CONDITIONS (owner 2026-11-07 15:25: the four conditions R2-1, R2-2, R2-3 and
vm_builtins closed in section 13; awaiting the narrow independent confirmation).** Earlier status, as
written: **SECOND FIX-UP FROZEN (B5 21, section 12) on the first fix-up `09651e11`, after the
independent REJECT of the B5 13 freeze `1d9053f6` (tree `e3f1767d`); awaiting the targeted
independent re-review (owner 2026-11-07 14:00: m12/m13, the 54+ conversions, receiver identity,
D4 classifications).** (First fix-up: section 11.) The REJECT and
its evidence are preserved unchanged (`scratchpad/review-b5/REVIEW.md`, F1-F12). Owner decisions:
2026-11-07 06:55 (D1 approved and fixtured; D2 release blocker; D3 accepted and gated; D4 bounded
Option 2), 09:06 (D4 conservative refusal accepted for the B5 checkpoint only, release blocker;
the 25 trim rows are documented precision limitations) and 11:07 (D4 corrected list; record and
test fix-up), all recorded in 11.1-11.2. Item B (`trim`/dispatch, approved 04:47) IS implemented
(B5 7, B5 8; section 8 B). Local coverage is PARTIAL by design (11.9: what ran, what is deferred
to the combined Linode certification and why); this is not a local certification. History below
(sections 1-10) is kept as written at the time; where a later section corrects it, the
correction is marked. Branch `cand/b5-refsem` from
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

**Precision cost E-U6-1 (owner reopened it 2026-11-07 04:47; section 8 A: STOP, alternatives listed; mechanism text in 8 A superseded by 10.5; final classification 11.1: one of four D4 admission regressions, release blocker):** cp19 proof `shared-callable` (an escaped
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
  build is still to do (section 9). **[Corrected in 11.5: proof-cli and admission-mutants are NOT
  identical; s1-dispatch has 9 failures, one of them `builtin-lazy-args:setenv`.]**

## 6. Expected-output changes

`docs/runtime-2/evidence/b5/expected-changes.tsv` (one row per fixture or case family:
previous, new, reason, establishing test). Classes: **i** newly admitted under shared-reference
semantics 158 rows; **iii** previously admitted, behaviour changed by an approved rule 7 rows
(D5 entry snapshot and ordinary loop binding: for-s2, capture_origins; Q7; Q2 B; cp14b const);
**p** outcome preserved, only the expectation's form changed 7 rows (a heap budget, a refusal
reason, a stale pre-existing expectation, a harness baseline, mutant retirements); **ii**
previously admitted, behaviour changed without an approved rule: 1 row, E-U6-1 (section 8 A).
Counts after B5 12: i 166, ii 1, iii 7 (incl. the D1 row), p 9. **Counts after the fix-up (B5 15,
11.6): i 169, ii 4 (the four D4 admission regressions: accepted for the B5 checkpoint only,
release blockers), iii 8, p 13.** One row is for owner confirmation (the receiver-returning
`reverse()`/`sort()` break, class i, 11.6); no other PENDING-OWNER row remains.
Approved differences are pinned exactly: `.r2.stdout/.stderr/.exit` goldens with
`approved=<rule>` (for-s2, closures-s1, vm-corpus/cp11), `B5_APPROVED` (capture_origins),
`APPROVED` (cp14b functional); each check also requires the pinned result to differ from legacy.
**[Resolved: owner 2026-11-07 06:55 decision 1 approved it (10.1); the paragraph below is the
historical PENDING-OWNER text.]** (owner guidance 2026-11-07 06:47): the capture_origins "D5 ordinary binding"
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
**owner decision needed** (given 09:06 and 11:07: 11.1). **[Corrected in 11.1: "the same family
also explains cp19 admission-mutant case `loop`" is wrong. B4b production never admitted `loop`
(its seed statement is refused `reference-guard:lent`), and a build without the heap exposure
still refuses it.]** Not affected: D1 (no session), the vm_adversarial groups.

### 10.6 Pre-existing failures, on an exact 17f7964c build (temporary worktree, deleted)

`git worktree add --detach` 17f7964c (tree ef8c508d), `make nift` (339 s), each target run there
(`scratchpad/b5/gates17f/`). A pre-existing failure is not a waiver: each goes to the
release-blocker audit. **[Corrected in 11.5 from the reviewer's signature table: the
test-cp19-proof-cli row ("identical") is wrong (with the legacy oracle B5 fails 5 cases,
17f7964c 1); the admission-mutants row's 17f column omits `loop` and `nested` and its B5 cause
for `loop` is not the E-U6-1 family; s1-dispatch has 9 failures, not 8.]**

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

**FAIL (7 make targets), all pre-existing (10.6), failure signatures identical to the fin batch
[corrected in 11.5: "identical" is wrong for proof-cli and admission-mutants]:**
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

### 10.11 Owner decisions still needed (as of the B5 13 freeze; superseded)

**[Superseded: decided 2026-11-07 09:06 and 11:07, after the freeze; see 11.1 and 11.2.]**
1. E-U6-1 / D4: accept the refusal as a documented precision limitation, or approve the
   heap-effect-summary design (10.5); the cp19 admission-mutant `loop` case follows it.
2. The 25 flagged trim-audit rows (10.7) are refusals before any effect with no approved rule
   (precision gaps, not semantic changes): reviewer/owner classification.
3. The release-blocker audit list of 10.6.

## 11. Fix-up after the independent REJECT of 1d9053f6 (B5 14, B5 15)

The independent review of the B5 13 freeze (`1d9053f6`, tree `e3f1767d`) REJECTED it narrowly
(`scratchpad/review-b5/REVIEW.md`, findings F1-F12; evidence `gates/ adv/ trim/ d1/ probes/ p/ d4/
h/`), preserved unchanged. Owner 2026-11-07 11:07: a separate fix-up agent (not the reviewer), no
B5 restart, no session-analysis redesign; record and tests only. **No production source changed**:
B5 14 touches tests, harness and the Makefile; B5 15 the record. The production binary is the one
the review built (sha256 `ac283072f622c473…`, 11.12). Every claim below was re-checked by the
fix-up on this tree (evidence `scratchpad/b5-fixup/`) unless it says it quotes the review.

### 11.1 D4: the corrected admission-regression list (owner 2026-11-07 09:06, corrected 11:07)

**Decision.** D4 conservative refusal is ACCEPTED FOR THE B5 CHECKPOINT ONLY; no session-effects
redesign in B5; the broad session-heap invalidation stays until a sound replacement exists. The
admission regressions are **exactly four** interactive-session programs, each admitted
legacy-equal by B4b production `17f7964c` and refused on B5 before any effect of the refused
statement. They are **RELEASE BLOCKERS, not waived**: B5 may be ACCEPT-WITH-CONDITIONS with them
documented; no release until they are resolved or explicitly reconsidered. The release audit
evaluates the affected session operations **as a group**, not only the four fixtures, and must
cover each cause below (one root cause must not be assumed).

| cp19 proof-cli case | 17f7964c production | B5 (statement, reason, reported location) | cause (re-checked) |
| --- | --- | --- | --- |
| shared-callable-replacement (= E-U6-1; minimal witness `fn(ap(v)){return v()};fn(mk()){a:=[()=>1];return {"f":()=>{q:=a[0];return ap(q)}}};o:=mk()` / `print(o.f())`) | legacy-equal (`WROTE`, then the ordinary arithmetic error) | statement 2, `session:current-callable-proof`, "no exact position" | D4: a fact-proved historical call opens every exposed class; `infer_session.cpp:137` exposes both session-heap classes. A build without that exposure admits it, legacy-equal |
| field: `a:=[1];fn(g(p)){return p.cb(0)+a[0]};warm:=g({"cb":v=>v+1})` / `a.push("bad")` / `g({"cb":v=>v+2})` | legacy-equal (`"bad"`, `3`) | statement 2, **a plain `a.push("bad")`**, `session:callable-flow`, located in the earlier unit that defined the reader (line 1, column 24) | NOT the D4 cause: a callable flowing through an object field; still refused without the exposure |
| element: `a:=[1];fn(g(p)){q:=p[0];return q(0)+a[0]};warm:=g([v=>v+1])` / `a.push("bad")` / `g([v=>v+2])` | legacy-equal (`"bad"`, `3`) | statement 2, **a plain `a.push("bad")`**, `session:current-callable-proof`, start line 2 | D4 cause plus a second refusal: without the exposure statement 2 runs and statement 3 is still refused `session:current-callable-proof` |
| sibling-effects (sibling closures over a captured array; a later statement stores through one) | legacy-equal | statement 3, `session:callable-flow`, `<stdin>:3:6` | D4 cause (a build without the exposure admits it, legacy-equal) |

So **a plain later `a.push(...)` is refused when earlier code reads `a[0]`** (field, element).

**Why the earlier list (shared-callable + `loop`, 09:06) was wrong** (preserved evidence):
- The three extra regressions only show when `test-cp19-proof-cli` gets its legacy oracle
  (`CP19_LEGACY_ORACLE`); the implementer never ran it that way and recorded the target as
  "pre-existing, identical". With the oracle: 17f7964c fails 1 case (`comparator`, the D2
  profile gate), B5 fails 5 (review F1; reproduced by the fix-up with
  `/home/claude/review/cand-b4/nift` as the oracle, and by pointing the new pins at the 17f7964c
  binary, which admits all four and fails the pins: `scratchpad/b5-fixup/proofcli/`).
- `loop` (cp19 admission-mutants) was judged from the 17f7964c **guard-off probe** control, not
  production. 17f7964c production refuses its **seed** statement (`reference-guard:lent`,
  `<stdin>:1:78`); the guard-off probe prints `3 3`; B5 runs statement 1 and refuses statement 2
  (`session:current-callable-proof`). It is a precision gap against the guard-off reference
  model, **not** a regression of a program production admitted (review F2), and the no-exposure
  build still refuses it, so "the same family also explains `loop`" (10.5) was unsupported.
  Owner 11:07: removed from the D4 list.
- Fix-up re-check of the causes: a scratch build of `infer_session.cpp` without
  `exposed_.insert(s)` for the two heap classes (never in the tree; deleted) gives exactly the
  review's results: shared-callable-replacement and sibling-effects legacy-equal; element
  statement 2 runs, statement 3 refused; field and `loop` still refused; the minimal witness
  prints 1.

**Pins.** `tests/runtime2/cp19/session_proof_cli.py` EXPECTED_REFUSALS holds the four, separate
from the 36 passing cases: each must be refused at its statement with its reason and exact
reported location, with exactly the oracle's output of the earlier statements, nothing of the
refused statement, and `require-error` with no execution for it. When one is admitted again the
target FAILS ("EXPECTED REFUSAL NO LONGER OCCURS"), so the pin and this section are updated
deliberately. `comparator` is a profile refusal (D2): refused `method:sort` at statement 1 off the
GCC 15.2 profile, an ordinary passing case on it.

### 11.2 Owner decisions recorded after the freeze

- **2026-11-07 09:06** (postdates the freeze): D4 as above; **the 25 trim-audit rows are
  documented precision limitations, NOT approved output changes**: 13 `method:splice`
  (precision lost after a mutation) and 12 `builtin-receiver:trim` through a function-returned
  `reverse()` (precision lost across a returned receiver alias); permanent fixtures, exact refusal
  behaviour preserved. The review confirmed none of the 25 was admitted by 17f7964c (all 97 v2->v3
  programs were refused there). Independent review authorised (done: REJECT).
- **2026-11-07 11:07**: preserve the REJECT; D4 corrected scope exactly the four of 11.1; the
  `push` refusal accepted for the B5 checkpoint only and a release blocker; the fix-up list
  (11.3-11.10); then freeze and a targeted independent re-review (D4 classification, no
  unapproved expectation change, mutant resolution, fixtures/generators, coverage reconciliation,
  records).
- **Release-blocker audit additions:** the multi-line comparator lambda error position defect
  (review F4: an error inside a `sort((p, q) => { ... })` lambda spanning lines 2-7 is reported
  at a line relative to the lambda, `4:8`, on the probe build, 17f7964c and legacy alike;
  pre-existing, not B5; production refuses comparator sort on this host, but on the GCC 15.2
  certification toolchain production would print the fabricated position; the owner's
  no-fabricated-positions rule makes it release-blocking). Open release blockers from B5: D4
  (11.1), D2 comparator-sort portability (10.2), F4, plus the pre-existing failures of 11.5.

### 11.3 CP19 session-mutants m7 (review F7)

The B4b-era witness (`nested:=[[1]]`, `for(row:nested){row.push(()=>7)}`,
`derived:=nested[0][1]`, `derived = ()=>9`) never reached its property on B5: statement 3 is
refused `type:index` (U6's single heap pool: `nested[0]` may be a number or a callable) on the
control and the mutant alike, so m7 had been untested since Phase A on both trees. M7 deletes the
root check of `session_shape_contract` (a session binding's new root shape must lie within its
recorded structure). Rebuilt witness (`tests/runtime2/cp19/session_mutants.cpp`):
`held:=[1];derived:=held[0];oldderived:=derived` then `derived = ()=>9;print("ONCE")`: refused
before any effect, the binding unchanged (legacy also refuses: "cannot assign string to int
binding"; 17f7964c refuses identically). The control refuses through the structural root contract
after its coherent re-check (`session:binding-shape-contract`; a diagnostic scratch build shows
the root check firing in both the first and the coherent compile); M7 falls to the coarser kind
check (`session:binding-contract`). Coherent widening is still exercised (a callable rebound to
another callable: admitted on both, retained alias keeps 7). The former witness stays pinned as the
`type:index` precision gap, so it flags when fixed. `test-cp19-session-mutants` and `-switch`:
PASS, m7 caught. Limitation, recorded honestly: a differential search (2,091 generated sessions,
B5 production vs an M7 CLI in scratch) found no admission or output difference, only this refusal
reason (32 sessions): under U6 the contents a stale recorded structure would describe are
re-seeded from the session heap or the binding's private class, so the coherent recompile that M7
skips is no longer needed for contents. M7 is therefore detected by its diagnostic, not by a
wrong result.

### 11.4 cp19 admission-mutants: every case now runs (new finding: m12 and m13 survive)

The harness stopped at the first failing assertion; since the Phase A guard that hid every case
after the first failing control. It now runs and reports all 14 cases and fails if any fails.
Result on B5 (default and switch builds identical; 17f7964c binaries run read-only for
comparison, `scratchpad/b5-fixup/gates/admission-mutants-percase{,-17f}.txt`):

| case / mutant | 17f7964c | B5 |
| --- | --- | --- |
| wrapper m1 | control fails (guard) | PASS |
| field m2, alias m4, schema m5, order m6, direct m7, capture m8, numeric m9, native-once m11 | PASS | PASS |
| nested m7 | control fails (guard) | PASS |
| loop m3 | control fails (seed refused by the guard) | control fails (statement 2 refused, 11.1); m3 not exercised |
| native m10 | **mutant survives** | **mutant survives** (pre-existing) |
| selector m12 | PASS | **mutant survives** (B5) |
| shared m13 | PASS | **mutant survives** (B5) |

**m12** (`session_callable.cpp`, `may_call` always true) can only make admission more
conservative; **m13** (`infer_session_location.cpp`, capture-location inference skipped) produced
an internal failure at 17f7964c; on B5 the session heap appears to subsume it (section 3 already
said so for array contents). **m10** (session native dispatch forced to the string native)
survives on both trees. **m3** (`cell.cpp`, source-slot plain capture) has no B5 witness: every
probed session shape that reaches a source-slot capture calls a callable held in an earlier
captured array, which the D4 family refuses. A differential search (2,144 sessions: the 40 proof-cli
cases, the admission seeds and the 2,091 generated sessions of 11.3) on scratch CLIs built with
each of m10, m12 and m13 found **no** difference from production. **PENDING-OWNER, not waived:**
these four mutants have no discriminating witness on B5; `test-cp19-admission-mutants` (and
`-switch`) FAIL and are listed in 11.5; restoring m3's witness is tied to the D4 release blocker.
Not resolved in B5 because a witness for a mutant that only adds refusals (m12) or that the heap
subsumes (m13) needs either a proof of equivalence or a new session shape, and the owner's
fix-up scope asked only for m7.

### 11.5 The failing targets: corrected signatures (review F5)

Re-run by the fix-up on B5 15 (`scratchpad/b5-fixup/gates/`); the 17f7964c column quotes the
review's runs (`RV/gates/b4a-*`) except where marked.

| target | 17f7964c | B5 15 | class / resolution |
| --- | --- | --- | --- |
| test-cp19-proof-cli (with the oracle) | 1 case: comparator (D2) | **PASS** now: 35 legacy-equal, 4 D4 pins, comparator profile refusal | the 4 are release blockers (11.1); without the oracle the target fails loudly (exit 2) |
| test-cp19-admission-mutants (+ -switch) | controls wrapper, loop, nested fail; m10 survives | FAIL: loop control; m10, m12, m13 survive | 11.4, PENDING-OWNER |
| test-cp19-session-mutants (+ -switch) | m7 contract: statement 2 refused (guard) | **PASS** | 11.3 |
| test-cp19-s1-dispatch | 9 FAIL (8 `callable:run`, 1 `builtin-lazy-args:setenv`); completion and mutants m1/m3 FAIL, m2/m4 PASS | identical (11.10) | known supported limitation (run()/cmd() deferred by the release scope; setenv lazy args); reclassify the expectations to the release scope |
| test-cp19-shared-eval | `eval_entry.py` expects `NIFT_VM=require` in a refusal note | identical | stale harness; release audit |
| test-runtime2-cp14b | `script_return_kernel_error` only; switch/effects/origins PASS | identical | precision regression after B3 N1; release audit |
| cp25r-inventory | 6 unclassified reasons | identical | tooling (classify-all without the cooperative switches); release audit |
| test-runtime2-vm-corpus | 155 FAIL lines | 124 (a strict subset) | default-environment goldens; release audit |
| test-runtime2-cp18 | refused `b := a` (`reference-guard:inflow`, 7:6) | **PASS** | 11.7 |
| test-runtime2-collections-mutants | PASS | **PASS** (was: mutant 15 survived) | 11.8 |

### 11.6 Record corrections (review F3, F5, F8)

compatibility.md (B5 table and limitations) and expected-changes.tsv now cover:
- **`reverse()`/`sort()`/comparator `sort` return the receiver itself** (legacy sorts or
  reverses in place and returns a copy): `ys := xs.reverse(); ys.push(3)` gives `xs.length()` 3
  (legacy 2), `same(xs, ys)` true (legacy false), `c := a.sort(); c.push(0)` grows `a`. Basis:
  plan section 3 Returns + B4b Option A (22:56). Verified on legacy/17f7964c/B5 (17f refuses both
  probes `reference-guard:inflow`). **Not separately approved as a legacy break: listed for owner
  confirmation** (the 20 trim category-b corrections rest on it).
- **failing-kind `trim`/`to_upper`/`to_string` on a map/set/stack/struct with an unused (or
  later-failing) result**: legacy exit 0 (it works on the internal tag), B5 the ordinary error at
  the call, exit 1 (item B, 04:47). `e7_trim_loop_unused.f`: legacy `got got end` exit 0; B5 `got`,
  `unknown value or malformed expression: v.trim()`, exit 1; 17f refused. "Legacy's family text"
  applies only where legacy itself fails.
- **nested-object `push` correction**: `k0 := stack(); aa := [{"a": [k0]}]; for (v : aa) { v.push(1) }`:
  17f7964c printed nothing more and exited 0 (a wrong result in the accepted B4b baseline); B5
  gives legacy's error (class iii).
- **D1 loop-variable capture** (already present; distinct from snapshot iteration) and **the four
  D4 regressions** (limitations table, release blockers).
- The stale B4b `glob16` row: B5 item B does not admit it (re-checked: refused
  `builtin-receiver:trim` on B5 as on 17f7964c).
- New rows: cp18 conversion and budget (11.7), mutant 15 (11.8), m7 (11.3), `loop` (p, not a
  regression). Counts: i 169, ii 4, iii 8, p 13.
- Stale text marked in place: status line, section 3 (E-U6-1 mechanism superseded by 10.5),
  section 5 and 10.6/10.8 ("identical" claims), section 6 (counts; the resolved PENDING-OWNER
  paragraph), 10.5 (`loop`), 10.11 (superseded by 11.1-11.2).

### 11.7 CP18 combined_cp14b (review F12)

Contract: plan section 3 "Assignment, declaration": `b := a` shares (=L for arrays bound from a
name), owner Q3. B5 CLI, legacy (both binaries) and the CP18 program API (libnift_c.so and the
switch library) all print `[9,1,2,4,5]` for `a` and return 5; the pin kept D-040 #1 copy
semantics (`[9,1,2,4]`, 4). Converted (class i: 17f7964c refused the program). **Why it also
failed at 17f7964c:** its compile was refused by the Phase A guard (`collections.f:7:6:
reference-guard:inflow`, the `b := a` line), so the target never got past check 1 there. The
same run exposed a second, pre-existing stale expectation: with budget `1 << 18` the stop is at
the map call's own reservation (`grow.f:3:6`), not the callback (`3:17`), identically on
17f7964c and B5 through both libraries (budgets 300000-400000 stop at 3:17, 500000 completes);
the budget is now 393216 (class p). `make test-runtime2-cp18` (C test, switch, COW): PASS.

### 11.8 Collections mutant 15 (review F11)

Mutant 15 changes one line of `UnitCompiler::for_setup` (`compiler_loops.cpp:74-78`):
`snapshot = keyed && source_kinds != kKindMap`, so a map loop is treated like a non-keyed source.
Under B5 `snapshot` no longer selects the iteration: every loop emits `IterCheck c = 1` on its held
value and `for_step` reads `state.held` (B5 4 deleted the live path), so **the live map iteration
the mutant was written to plant is unreachable** (shown: the mutant passes all 280 earlier checks,
including the snapshot_insert/remove/update fixtures that differ under live iteration). Its only
remaining effect: `snapshot` also gates `for_source_binding`/`source_name`, i.e. the
vanished-element analysis (`for_source_may_shrink`). With the mutant a map loop over a binding
whose body calls a method on the map and writes or captures the value is refused
(`for-missing-write`, `for-capture-vanishing`): an admission loss, observable. Two parity fixtures
(`collections/parity/snapshot_map_body_{write,capture}.f`: legacy-equal on production) catch it
(4 failing checks); the mutant's description is updated; `test-runtime2-collections-mutants`:
11 caught, PASS. The production comment at `compiler_loops.cpp:73` still says "M15: the map is
re-read live"; left unchanged to keep production sources untouched (a comment-only follow-up).

### 11.9 Generators and harness hygiene (review F6)

- `tests/runtime2/b5/gen/` (gen1, gen_struct, genrun, gate.py) replaces the scratch-only tools; the
  only input is the tree's `src/rt/natives/builtin_list.inc`. `make test-runtime2-b5-generators`:
  gen1 20,274 programs (18,678 refused, 1,596 admitted, 192 admitted differing from legacy: 131
  exactly as on 17f7964c, 61 newly admitted: 30 item B, 18 approved B4 "cannot be rendered as text",
  13 error text only), 0 bad; gen_struct 1,911 (1,044 refused, 867 admitted, 213 differing from
  legacy, all exactly as on 17f7964c, error text/position only), 0 bad. gen_struct previously
  recorded no `bad` field, so its gate could not fail; it now has gen1's classification. Negative
  controls (missing tree, truncated builtin table): exit 2, no program and no result written, for
  both generators.
- `.build/cp19` directory creation: `@mkdir -p $(dir $@)` added to the cp19 admission and session
  mutant/control rules (Makefile and `admission_mutants.mk`); see 11.10 for the clean-directory check.
- proof-cli: the oracle requirement is documented in the harness; the target fails loudly
  (exit 2, "is NOT run without a legacy oracle (it never skips)") when `CP19_LEGACY_ORACLE` is unset
  or not executable. Run: `make test-cp19-proof-cli CP19_LEGACY_ORACLE=/path/to/nift`.
- The original 67 vm_adversarial expectations: `docs/runtime-2/evidence/b5/adversarial-67-original.json`
  (each program's source and sha256, the original gate expectation: legacy-equal or refused before
  any effect; the 17f7964c refusal with position; the legacy result with error position; the
  current pin; the observed B5 result). Re-run by the fix-up: all 67 match their pin, legacy is
  identical on both binaries, 17f7964c refused all 67, no file effects.

### 11.10 Coverage reconciliation (review F6; owner 11:07 "reconcile 46 executed vs 220")

**Inventory.** The review's 220 reproduces exactly as the targets *defined in the Makefile itself*
whose name starts with `test-`, `cp25r` or `vm-`, contains `runtime2`, `-rt`, `cp` or `vm`, and is
not a `test-v4*` (4.x line) target. The Makefile also includes `tests/runtime2/cp19/*.mk` and
`tests/runtime2/cp23/certification.mk`, which define 23 more such targets (among them
`test-cp19-proof`, `-proof-cli`, `-admission-mutants`, `-session-mutants-switch`, cp23
allocation/cancellation/mutants); the fix-up inventories all **243**, plus the new
`test-runtime2-b5-generators` = **244**.

**Method.** Every target ran with `make -s` on the fix-up tree (B5 14 for batch 1, B5 15 for
batch 2: the two differ only in `admission_mutants.py`), sequentially, with a disk floor, result
files with exit status, duration, HEAD and `git status` hash in `scratchpad/b5-fixup/gates/`. Every
failing target was re-run with `TARGET` pointing at a scratch copy of the 17f7964c production binary
(`make -o`, never rebuilt; `c17-*.result`) or compared case by case. No production source changed
during any run; the production binary was re-hashed after the runs (11.12).

**Totals (244): ran 159 = PASS 120, FAIL 38, FLAKY 1; not run 85 = deferred to the combined Linode
certification 71, umbrella targets whose components ran individually 7, prerequisites run inside
another row 4, not applicable 3.** This is NOT a local certification: the sanitizer/TSan lanes,
wall-clock measurements, the whole-runtime mutation campaigns and host process/terminal campaigns
are Linode work.

Of the 38 FAIL: **1 B5-introduced and new** (`test-runtime2-cp12c-probes`, 11.11), 2 the
admission-mutants pair (11.4, PENDING-OWNER), 4 build-guard harness failures (mutant sub-builds
rejected by the production build guard; harness and guard unchanged since 17f7964c), 1 stale
mutant that no longer compiles (cp17-routing-mutants), 4 unit link / source-list defects identical
on 17f7964c, and 26 that fail identically (same exit, same or fewer
FAIL lines) with the 17f7964c binary, including the known cp14b, cp25r-inventory, vm-corpus,
s1-dispatch and shared-eval. `test-runtime2-cp01-simplify` is FLAKY on both binaries (incremental
build timing; different scenarios fail each run): under the zero-flakiness policy it needs a test
infrastructure fix; not B5.

**test-rt-vm own recipe** (34 lines after its prerequisites; `scratchpad/b5-fixup/rtvm_own.sh`,
run line by line on B5 and on the 17f7964c binary): vm-unit **40,347 checks, 0 failures** (rebuilt
and run), vm-globals 23, vm-modules 179, prepared-parse 100,136 checks, rt-lint, vm_adversarial
(seed 1, count 600) and 22 more lines PASS. Fail on both trees: vm_parity `--rt-diff` (B5 1
failure: the rt-diff comparison, identical counts "30 differing, 16 approved" on 17f7964c, which
fails 8 including six corpus guard refusals B5 removes), vm_builtins (17f 2, B5 5: the 3 extra are
"expected fallback reference-guard:*/builtin-receiver:empty, got compiled"), vm_modules (3 on
both). **Pass on 17f7964c and fail on B5 (new, 11.11):** vm_subset (1), vm_subset_gen (5),
vm_records_coverage (6), vm_engine_selection (1), vm_former_replay (10), vm_review_cp12 (4).

Also run outside make: gen_alias on production (12,600 programs, 5,532 on the VM, 0 bad),
sort_refusals on the probe build (564, all `collection-element:sort`, none before an effect),
`tests/runtime2/b5/run.py --dirs sort` on the probe build (4 PASS; **comparator-sort atomicity is
probe-build evidence only, not production**: production refuses comparator sort on this GCC 13.3
host, D2), the admission mutants case by case on B5 and on the 17f7964c binaries (11.4), the
clean-directory check of the cp19 `mkdir` fix (admission-control and session-control linked into an
empty `TEST_DIR` outside the tree: PASS).

Gates the owner named, all run: every B5 correctness and mutation gate (b5, b5-mutants,
b5-generators, b5-snapshot-memory, collections(-mutants), cp18, cp19 session/admission/proof-cli
mutants and witnesses), B3/B4/B4b regression (b3, b3-mutants, b4, b4-mutants, b4b, b4b-work,
b4b-fixpoint, b4b-mutants, b4b-types/op-admission/struct-render units), classification and semantic
comparisons (vm_adversarial, vm_parity, rt-diff, cp25r, cp25r-inventory, vm-corpus(-complete),
vm-default, safety fuzz), guard-off and session soundness (guardoff, cp19 session/session-cli/proof,
U6 fixtures), snapshot loops (for-s2(-mutants, -known-deps), capture-origins, closures-s1(-mutants,
-stress), loopclosure), comparator sort (cp14b-sort, b5/sort: probe build). Disk: the run stayed
above 700 MB free; scratch binaries and corpora were deleted afterwards.

<!-- coverage table follows -->
| target | ran | result | note / reason |
| --- | --- | --- | --- |
| cp25r-inventory | ran | FAIL | FAIL, pre-existing, identical on 17f7964c (11.5) |
| test-cp15-numeric-repair | ran | FAIL | FAIL, pre-existing: same exit and same FAIL-line count with the 17f7964c binary (TARGET override) |
| test-cp17-bytes | ran | FAIL | FAIL, pre-existing: same exit and same FAIL-line count with the 17f7964c binary (TARGET override) |
| test-cp18-bytes | ran | FAIL | FAIL, pre-existing: same exit and same FAIL-line count with the 17f7964c binary (TARGET override) |
| test-cp18-bytes-sanitize | not run | deferred | sanitizer lane (ASan/UBSan build of nift and the bytes tests): Linode combined certification |
| test-cp18-bytes-tsan | not run | deferred | TSan build: Linode combined certification |
| test-cp19-admission-mutants | ran | FAIL | FAIL: loop control (precision gap) + m10/m12/m13 survive (11.4, PENDING-OWNER). **12.1: m13 caught (shared-scalar), m12 retired unreachable (tripwire); still FAIL: loop (D4/m3), native (m10)** |
| test-cp19-admission-mutants-switch | ran | FAIL | FAIL: as the default build (11.4; 12.1) |
| test-cp19-admission-sanitize | not run | deferred | sanitizer build: Linode |
| test-cp19-bytes | ran | FAIL | FAIL, pre-existing: same exit and same FAIL-line count with the 17f7964c binary (TARGET override) |
| test-cp19-certify | not run | umbrella | = test-cp19-functional + -mutants + -sanitize + -host: components run (cp19 rows) except the sanitizer lane (deferred) |
| test-cp19-cleanup-cli | ran | PASS |  |
| test-cp19-expression-development | ran | FAIL | FAIL, pre-existing: same exit and same FAIL-line count with the 17f7964c binary (TARGET override) |
| test-cp19-expression-sanitize | not run | deferred | sanitizer build: Linode |
| test-cp19-functional | not run | umbrella | = cp19 jobs, session, resources, proof, operand-windows, job-cli, resource-cli, session-cli: each run separately |
| test-cp19-host | not run | umbrella | = test-cp19-cleanup-cli: run separately |
| test-cp19-host-sanitize | not run | deferred | sanitizer build of nift and every cp19 host suite: Linode |
| test-cp19-job-cli | ran | PASS |  |
| test-cp19-job-mutants | ran | PASS |  |
| test-cp19-jobs | ran | PASS |  |
| test-cp19-mutants | not run | umbrella | = cp19 job/proof/resource/session(+switch)/admission(+switch) mutants and resource/proof alloc: each run separately |
| test-cp19-operand-windows | ran | PASS |  |
| test-cp19-proof | ran | PASS |  |
| test-cp19-proof-alloc | ran | PASS |  |
| test-cp19-proof-cli | ran | PASS |  |
| test-cp19-proof-mutants | ran | PASS |  |
| test-cp19-proof-sanitize | not run | deferred | sanitizer build: Linode |
| test-cp19-r1 | ran | FAIL | FAIL, pre-existing (depends on shared-eval), identical |
| test-cp19-r1-sanitize | not run | deferred | sanitizer build: Linode |
| test-cp19-resource-alloc | ran | PASS |  |
| test-cp19-resource-cli | ran | PASS |  |
| test-cp19-resource-mutants | ran | PASS |  |
| test-cp19-resources | ran | PASS |  |
| test-cp19-s1-dispatch | ran | FAIL | FAIL, pre-existing, identical (10 FAIL lines on both) |
| test-cp19-s1-dispatch-unit | ran | PASS |  |
| test-cp19-sanitize | not run | deferred | umbrella of the cp19 sanitizer lanes: Linode |
| test-cp19-session | ran | PASS |  |
| test-cp19-session-cli | ran | PASS |  |
| test-cp19-session-mutants | ran | PASS |  |
| test-cp19-session-mutants-switch | ran | PASS |  |
| test-cp19-session-sanitize | not run | deferred | sanitizer build: Linode |
| test-cp19-shared-eval | ran | FAIL | FAIL, pre-existing, identical |
| test-cp19-shell-exception | ran | PASS |  |
| test-cp20-bytes | ran | FAIL | FAIL, pre-existing: same exit and same FAIL-line count with the 17f7964c binary (TARGET override) |
| test-cp20-fd-leak | ran | PASS |  |
| test-cp20-host-fd-mutant | not run | deferred | builds a host fd mutant nift; process host untouched by B5; Linode |
| test-cp20-launch-bench | not run | deferred | wall-clock benchmark: Linode only (owner rule) |
| test-cp20-r1 | ran | PASS |  |
| test-cp20-r1-flake | not run | deferred | 20-round PTY flake campaign (load-sensitive): Linode |
| test-cp20-r1-windows | not run | n/a | Windows pipeline test (skips with 77 off Windows); Windows lane of the release validation |
| test-cp20-shell-spawn | ran | PASS |  |
| test-cp20-shell-spawn-mutants | not run | deferred | builds 11 shell-spawn mutant nifts (disk); process host untouched by B5; Linode |
| test-cp21-bytes | ran | PASS |  |
| test-cp21-d3-mandatory | not run | deferred | fd-inheritance D3 campaign with fault injection and mutant children; terminal/process host untouched by B5; Linode |
| test-cp21-d3-mutant | not run | deferred | as test-cp21-d3-mandatory |
| test-cp21-evidence-control | not run | deferred | depends on the cp21 mutant campaigns; Linode |
| test-cp21-fd-inheritance | not run | deferred | fd-inheritance harness with LD_PRELOAD fault injection; process host untouched by B5; Linode |
| test-cp21-fd-inheritance-mutants | not run | deferred | fd-inheritance mutant campaign; Linode |
| test-cp21-fd-protocol-unit | ran | PASS |  |
| test-cp21-interactive-host | not run | deferred | interactive PTY host test with a tty-audit preload; needs a controlling terminal setup; Linode |
| test-cp21-interactive-host-mutants | not run | deferred | builds 5 interactive-host mutant nifts; Linode |
| test-cp21-make-failure-control | not run | deferred | make-failure control of the cp21 campaign; Linode |
| test-cp21-mutant1-controls | not run | deferred | cp21 mutant controls (mutant nift builds); Linode |
| test-cp21-parser-target-mutant | not run | deferred | cp21 protocol mutant; Linode |
| test-cp21-prompt-latency | not run | deferred | wall-clock latency measurement: Linode only |
| test-cp21-terminal-lease | ran | PASS |  |
| test-cp21-terminal-recovery | not run | deferred | terminal recovery with a restore-fault preload; needs a terminal; Linode |
| test-cp21-terminal-recovery-mutants | not run | deferred | terminal recovery mutants; Linode |
| test-cp21-tty-audit-guards | ran | PASS |  |
| test-cp23-allocation | ran | PASS |  |
| test-cp23-cancellation | ran | PASS |  |
| test-cp23-certify | not run | umbrella | = cp23 + cancellation + cp19-combined + mutants + allocation (run) + cp23 sanitize/tsan (deferred) |
| test-cp23-cp19-combined | ran | PASS |  |
| test-cp23-isolate-task-unit | ran | PASS |  |
| test-cp23-mutants | ran | PASS |  |
| test-cp23-ownership-unit | ran | PASS |  |
| test-cp23-parallel-unit | ran | PASS |  |
| test-cp23-shared-unit | ran | PASS |  |
| test-cp23-shared-unit-sanitize | not run | deferred | sanitizer build: Linode |
| test-cp23-snapshot-constants | not run | deferred | needs the ASan and TSan unit builds as prerequisites: Linode |
| test-cp25r | ran | PASS |  |
| test-rt-b4b-types-unit | ran | PASS |  |
| test-rt-build | ran | PASS |  |
| test-rt-builtins | ran | FAIL | FAIL, pre-existing: builtins-unit link error (shared_* symbols missing from the unit source list), identical |
| test-rt-builtins-guards | not run | deferred | runs the builtins benchmark binary against recorded guards (performance): Linode |
| test-rt-builtins-isolation | not run | via | prerequisite of test-rt-builtins (run there) |
| test-rt-builtins-probe | ran | FAIL | FAIL, pre-existing: same exit and same FAIL-line count with the 17f7964c binary (TARGET override) |
| test-rt-builtins-sanitize | not run | deferred | sanitizer build: Linode |
| test-rt-case-conversion | ran | FAIL | FAIL, pre-existing: same unit link error, identical |
| test-rt-case-conversion-negative | not run | via | prerequisite of test-rt-builtins (run there) |
| test-rt-error-oom | ran | PASS |  |
| test-rt-error-oom-sanitize | not run | deferred | sanitizer build: Linode |
| test-rt-handles | not run | deferred | handle units under ulimit -v 4 GB with 300 s budgets; host handles untouched by B5; Linode |
| test-rt-handles-guards | not run | deferred | runs the handle benchmark binary against recorded guards (performance): Linode |
| test-rt-handles-sanitize | not run | deferred | sanitizer build: Linode |
| test-rt-handles-tsan | not run | deferred | TSan build: Linode |
| test-rt-heap-saturation | ran | PASS |  |
| test-rt-host | not run | umbrella | host layer/process units + f6 cleanup + cp17 n2/handles/handle-allocation: components run where listed, handle allocation deferred |
| test-rt-host-mutants | not run | deferred | umbrella of host mutation lanes (f6, cp17 routing/n2-rebuild/handle/unwind mutants, host_mutants.sh building mutant nifts): host layer untouched by B5 (no change outside the compiler/inference/VM files of B5); combined certification |
| test-rt-host-random-budget | ran | FAIL | FAIL, pre-existing: same unit link error, identical |
| test-rt-host-sanitize | not run | deferred | sanitizer builds (f6, handles, cp17): Linode |
| test-rt-host-sources-list | ran | FAIL | FAIL, pre-existing: host_error.cpp listed twice, identical |
| test-rt-host-tsan | not run | deferred | TSan build: Linode |
| test-rt-isolate-transfer-unit | ran | PASS |  |
| test-rt-isolation | ran | PASS |  |
| test-rt-lint | ran | PASS |  |
| test-rt-memory-c1 | ran | FAIL | FAIL, pre-existing: same exit and same FAIL-line count with the 17f7964c binary (TARGET override) |
| test-rt-memory-c2 | ran | PASS |  |
| test-rt-mutation-protocol | ran | PASS |  |
| test-rt-mutation-protocol-mutants | ran | PASS |  |
| test-rt-op-admission-unit | ran | PASS |  |
| test-rt-parser | ran | FAIL | FAIL, pre-existing: same exit and same FAIL-line count with the 17f7964c binary (TARGET override) |
| test-rt-parser-asan | not run | deferred | ASan build: Linode |
| test-rt-protocol-compile-fail | ran | PASS |  |
| test-rt-refrep | ran | PASS |  |
| test-rt-refrep-mutants | ran | PASS |  |
| test-rt-refrep-production | ran | PASS |  |
| test-rt-sort-forgery | ran | PASS |  |
| test-rt-sort-protection-mutants | ran | PASS |  |
| test-rt-source-map | ran | PASS |  |
| test-rt-source-map-retry | ran | PASS |  |
| test-rt-source-map-sanitize | not run | deferred | sanitizer build: Linode |
| test-rt-source-map-tsan | not run | deferred | TSan build: Linode |
| test-rt-struct-render-unit | ran | PASS |  |
| test-rt-value | ran | PASS |  |
| test-rt-value-sanitize | not run | deferred | sanitizer build (needs unlimited ulimit -v): Linode |
| test-rt-vm | not run | umbrella | prerequisites run as separate rows; its own recipe (vm-unit, globals, modules, prepared parse, rt-lint, vm_parity, vm_adversarial, vm_subset, ... 34 lines) run line by line (row test-rt-vm own recipe) |
| test-rt-vm-mutants | not run | deferred | builds the VM mutant set and runs vm_mutation_gate with rt-diff over the corpus (whole-runtime mutation campaign; disk); Linode |
| test-rt-vm-policy-tooling | ran | PASS |  |
| test-rt-vm-sanitize | not run | deferred | umbrella of every VM sanitizer lane (sanitized nift, vm-unit, cp14b/cp22/coro/collections/refsem/b3/b4/guardoff): Linode |
| test-runtime2-args-cmd | ran | PASS |  |
| test-runtime2-args-cmd-mutants | ran | PASS |  |
| test-runtime2-b3 | ran | PASS |  |
| test-runtime2-b3-mutants | ran | PASS |  |
| test-runtime2-b3-sanitize | not run | deferred | sanitizer build: Linode |
| test-runtime2-b4 | ran | PASS |  |
| test-runtime2-b4-mutants | ran | PASS |  |
| test-runtime2-b4-tsan | not run | deferred | TSan build: Linode |
| test-runtime2-b4b | ran | PASS |  |
| test-runtime2-b4b-fixpoint | ran | PASS |  |
| test-runtime2-b4b-mutants | ran | PASS |  |
| test-runtime2-b4b-work | ran | PASS |  |
| test-runtime2-b5 | ran | PASS |  |
| test-runtime2-b5-generators | ran | PASS |  |
| test-runtime2-b5-mutants | ran | PASS |  |
| test-runtime2-b5-snapshot-memory | ran | PASS |  |
| test-runtime2-capture-origins | ran | PASS |  |
| test-runtime2-certification-guard | ran | PASS |  |
| test-runtime2-closure-cycle-stress | ran | PASS |  |
| test-runtime2-closures-s1 | ran | PASS |  |
| test-runtime2-closures-s1-mutants | ran | PASS |  |
| test-runtime2-closures-s1-prepare | not run | n/a | maintenance mode (--prepare regenerates goldens), not a gate |
| test-runtime2-closures-s1-stress | ran | PASS |  |
| test-runtime2-closures-s1-stress-prepare | not run | n/a | maintenance mode (--prepare regenerates goldens), not a gate |
| test-runtime2-collections | ran | PASS |  |
| test-runtime2-collections-mutants | ran | PASS |  |
| test-runtime2-collections-sanitize | not run | deferred | sanitizer build: Linode |
| test-runtime2-collections-tsan | not run | deferred | TSan build: Linode |
| test-runtime2-coro-module | ran | PASS |  |
| test-runtime2-coro-module-mutants | ran | PASS |  |
| test-runtime2-coro-module-sanitize | not run | deferred | sanitizer build: Linode |
| test-runtime2-coro-module-tsan | not run | deferred | TSan build: Linode |
| test-runtime2-cp01-simplify | ran | FLAKY | FLAKY on both binaries (incremental-build timing: 4, 1, 1 failures on B5; 3, 2 on 17f7964c, different scenarios each run) |
| test-runtime2-cp03-simplify | ran | FAIL | FAIL, pre-existing: same exit and same FAIL-line count with the 17f7964c binary (TARGET override) |
| test-runtime2-cp12c-mutant-controls | ran | PASS |  |
| test-runtime2-cp12c-probes | ran | PASS | was FAIL (11.11); **12.2: 48 differences pinned (47 snapshot, 1 D1), PASS on B5 20** |
| test-runtime2-cp13b | ran | PASS |  |
| test-runtime2-cp13b-mutants | ran | FAIL | FAIL, pre-existing harness: mutant sub-builds rejected by the production build guard (test-only switch on CXXFLAGS); guard and harness unchanged since 17f7964c |
| test-runtime2-cp14b | ran | FAIL | FAIL, pre-existing: script_return_kernel_error only (11.5) |
| test-runtime2-cp14b-design-mutants | ran | PASS |  |
| test-runtime2-cp14b-mutants | not run | deferred | cp14b compiled mutation campaign (builds a mutant per site) plus the design mutants; design mutants run separately; Linode for the campaign |
| test-runtime2-cp14b-profile | ran | PASS |  |
| test-runtime2-cp14b-sanitize | not run | deferred | sanitizer build: Linode |
| test-runtime2-cp14b-sort | ran | PASS |  |
| test-runtime2-cp14b-witnesses | ran | PASS |  |
| test-runtime2-cp16-engine-parity | ran | PASS |  |
| test-runtime2-cp16-legacy | ran | FAIL | FAIL, pre-existing: same exit and same FAIL-line count with the 17f7964c binary (TARGET override) |
| test-runtime2-cp16-n1 | ran | PASS |  |
| test-runtime2-cp17-handle-allocation | not run | deferred | handle allocation campaigns with 1800 s and 3600 s budgets; host handles untouched by B5; Linode |
| test-runtime2-cp17-handle-mutants | not run | deferred | handle mutant builds; host handles untouched by B5; Linode |
| test-runtime2-cp17-handles | ran | FAIL | FAIL, pre-existing: same exit and same FAIL-line count with the 17f7964c binary (TARGET override) |
| test-runtime2-cp17-n2 | ran | PASS |  |
| test-runtime2-cp17-n2-rebuild | ran | PASS |  |
| test-runtime2-cp17-n2-rebuild-mutant | not run | deferred | rebuild-mutant campaign (rebuilds through make); host code untouched by B5; Linode |
| test-runtime2-cp17-routing-mutants | ran | FAIL | FAIL, pre-existing harness: a mutant of src/ParserScript.cpp no longer compiles (stale mutant; file and harness unchanged by B5) |
| test-runtime2-cp17-sanitize | not run | deferred | sanitizer build: Linode |
| test-runtime2-cp17-tripwire | ran | PASS |  |
| test-runtime2-cp17-unwind-mutants | not run | deferred | unwind mutant builds; Linode |
| test-runtime2-cp18 | ran | PASS |  |
| test-runtime2-cp18-cow | not run | via | prerequisite of test-runtime2-cp18 (run there) |
| test-runtime2-cp18-sanitize | not run | deferred | sanitizer build: Linode |
| test-runtime2-cp18-switch | not run | via | prerequisite of test-runtime2-cp18 (run there) |
| test-runtime2-cp22 | ran | PASS |  |
| test-runtime2-cp22-mutants | not run | deferred | 25 coroutine mutant nift builds (about 240 MB; this host has about 1 GB free); coroutine code untouched by B5; Linode |
| test-runtime2-cp22-sanitize | not run | deferred | sanitizer build: Linode |
| test-runtime2-cp22-tsan | not run | deferred | TSan build: Linode |
| test-runtime2-cp23 | ran | PASS |  |
| test-runtime2-cp23-sanitize | not run | deferred | sanitizer build: Linode |
| test-runtime2-cp23-tsan | not run | deferred | TSan build: Linode |
| test-runtime2-d040-witnesses | ran | PASS |  |
| test-runtime2-diagnostics | ran | FAIL | FAIL, pre-existing: same exit and same FAIL-line count with the 17f7964c binary (TARGET override) |
| test-runtime2-dynamic-reads | ran | FAIL | FAIL, pre-existing: same exit and same FAIL-line count with the 17f7964c binary (TARGET override) |
| test-runtime2-f6-cleanup | ran | PASS |  |
| test-runtime2-f6-mutants | not run | deferred | f6 cleanup mutant builds; host code untouched by B5; Linode |
| test-runtime2-f6-sanitize | not run | deferred | sanitizer build: Linode |
| test-runtime2-for-s2 | ran | PASS |  |
| test-runtime2-for-s2-bench | not run | deferred | wall-clock benchmark: Linode only |
| test-runtime2-for-s2-known-deps | ran | PASS |  |
| test-runtime2-for-s2-mutants | ran | PASS |  |
| test-runtime2-for-s2-sanitize | not run | deferred | sanitizer build: Linode |
| test-runtime2-fs-freedom | ran | PASS |  |
| test-runtime2-guardoff | ran | PASS | PASS on rerun (first run invalidated by a concurrent record edit: repo-state) |
| test-runtime2-guardoff-sanitize | not run | deferred | sanitizer build of the guard-off probe: Linode (named in 10.10) |
| test-runtime2-host-deps | ran | FAIL | FAIL, pre-existing: 1 FAIL line on B5, 3 with the 17f7964c binary (TARGET override) |
| test-runtime2-host-memory | ran | PASS |  |
| test-runtime2-inventory-host-mutant | ran | PASS |  |
| test-runtime2-inventory-links | ran | PASS |  |
| test-runtime2-kind-bits | ran | PASS |  |
| test-runtime2-literals | ran | PASS |  |
| test-runtime2-literals-decimal-negative | ran | FAIL | FAIL, pre-existing: "expected exactly one Decimal inference line", identical with the 17f7964c binary |
| test-runtime2-literals-f1-negative | ran | FAIL | FAIL, pre-existing harness: sub-build rejected by the build guard |
| test-runtime2-literals-mutants | ran | FAIL | FAIL, pre-existing harness: sub-build rejected by the build guard (as cp13b-mutants) |
| test-runtime2-literals-scaling | not run | deferred | timed scaling runs (--runs 3): Linode only |
| test-runtime2-modified-race | ran | PASS |  |
| test-runtime2-module-host-order-mutant | ran | FAIL | FAIL, pre-existing harness: sub-build rejected by the build guard |
| test-runtime2-packages | ran | PASS |  |
| test-runtime2-packages-legacy | ran | PASS |  |
| test-runtime2-packages-mutants | ran | PASS |  |
| test-runtime2-prevalidate-gate | ran | FAIL | FAIL, pre-existing: same exit and same FAIL-line count with the 17f7964c binary (TARGET override) |
| test-runtime2-process-capture | ran | FAIL | FAIL, pre-existing: same exit and same FAIL-line count with the 17f7964c binary (TARGET override) |
| test-runtime2-refrep-cli | ran | PASS |  |
| test-runtime2-refsem | ran | PASS |  |
| test-runtime2-refsem-mutants | ran | PASS |  |
| test-runtime2-refsem-sanitize | not run | deferred | sanitizer build: Linode |
| test-runtime2-refsem-stale-vm | ran | PASS |  |
| test-runtime2-refsem-tsan | not run | deferred | TSan build: Linode |
| test-runtime2-rt-diff | ran | PASS |  |
| test-runtime2-rt-diff-selftest | ran | FAIL | FAIL, pre-existing: same exit and same FAIL-line count with the 17f7964c binary (TARGET override) |
| test-runtime2-safety | ran | PASS |  |
| test-runtime2-safety-mutants | ran | PASS |  |
| test-runtime2-scanner-parity | ran | FAIL | FAIL, pre-existing: same exit and same FAIL-line count with the 17f7964c binary (TARGET override) |
| test-runtime2-vm-corpus | ran | FAIL | FAIL, pre-existing: 124 FAIL lines (17f7964c: 155) |
| test-runtime2-vm-corpus-complete | ran | PASS |  |
| test-runtime2-vm-default | ran | PASS |  |

### 11.11 New findings of the fix-up (not in the review; not resolved here)

1. **`test-runtime2-cp12c-probes` fails on B5 and passes on 17f7964c** (48 legacy differences in
   `cp12c-probes/src`; `src2` passes). Every one is a loop whose body mutates or rebinds the
   iterated source (or calls a function that does) or captures the loop variable: the CP12C
   live-loop probes, written for the live model B5 deleted. At 17f7964c 24 were refused, 20 ended
   in the legacy-equal vanished-element error and now run to exit 0 (nothing vanishes under the
   entry snapshot), 4 were legacy-equal live reads and now print the entry snapshot
   (`g02/g03_*_global_fn_*`, `t14_obj_replace_values`, `t17_restored_before_read`). All consistent
   with D5 and D1, but **not converted**: like the for-s2 and capture_origins conversions, each
   needs its result checked and pinned with its rule before the expectation changes.
2. **Six test-rt-vm recipe lines pass on 17f7964c and fail on B5**, all "expected a refusal
   (`aliasing`, `reference-guard:*`) or a fallback, got compiled/vm-error", except vm_former_replay:
   10 `arr.push(arr)` programs now give the approved catchable reference-cycle error (D-NEXT-refsem
   #4; legacy pushes a copy). The 5 vm_subset_gen programs checked by the fix-up are legacy-equal
   (stdout, exit, error text). The rest are unverified: obsolete expectations after the approved
   guard removal **by their failure text**, to be converted with per-program verification.
3. **cp19 admission mutants m12 and m13 survive on B5** (11.4).

Items 1-2 were invisible to the freeze and to the review because `test-rt-vm` was never run whole
(its prerequisite `test-runtime2-cp14b` fails, so make never reaches its recipe) and cp12c-probes
was not in either gate list. They are test-expectation work, not production defects found so far;
**PENDING: owner decision whether the conversion belongs to this fix-up round or a follow-up before
integration.**

### 11.12 Freeze record (B5 16)

- Commits: B5 14 `af36ad9a` (tests, harness, Makefile, adversarial originals), B5 15 `8a568abb`
  (admission-mutants harness), B5 16 (this record, compatibility.md, expected-changes.tsv).
- Production binary: `nift` sha256 `ac283072f622c473b117d76d4d2e4397606743a8804bee5c0fa1565145ac05b7`
  after all runs, equal to the review's build of `1d9053f6`. `git diff 1d9053f6 HEAD` touches no
  `src/`, `include/`, `scripts/` or vendored file.
- Repository: `git status --porcelain` empty after the commit; `--ignored` lists build products only
  (`.build/`, objects, `.d`, `.cfg`, `nift`, `libnift_c.so`).
- Not pushed, not tagged; runtime-2, weekend/r2, cand/b4-dispatch, cand-b4 and rv-b5 untouched
  (cand-b4: its `./nift` and test binaries run read-only; `git status --porcelain` empty).
- For the targeted re-review: 11.1 (D4 classification), 11.6/expected-changes (no unapproved
  expectation change: the one derived break is flagged for owner confirmation), 11.3/11.4/11.8
  (mutants), 11.9 (fixtures, generators), 11.10 (coverage), 11.11 (open items).

## 12. Second fix-up from 09651e11 (owner 2026-11-07 14:00; B5 17 to B5 21)

Bounded test-and-record round from the frozen candidate `09651e11` (tree `6e1f7a0a`), by an agent
that is neither the earlier fix-up agent nor the reviewer of `1d9053f6`. **No production source
changed**: B5 17-20 touch tests, harnesses, the Makefile's `test-runtime2-b5` recipe, fixtures,
compatibility.md and evidence; B5 21 is this record. Evidence:
`scratchpad/b5-fixup2/` (`instr/` instrumentation sources, corpora and results; `m13/`, `m12/`;
`cp12c/`; `rtvm/`; `gates/`). Scratch binaries (two mutant CLIs, the instrumented CLI and its m3
variant) were built outside the tree with production flags, logged and deleted.

### 12.1 cp19 admission mutants m12 and m13 (owner 14:00: resolve before acceptance)

**m13** (`infer_session_location.cpp`, `session_begin_locations` returns at once). Property
removed: capture-location inference, which unifies the inference types of every slot of one
compile that names the same captured cell (a template closure's capture declaration, a session
global; source-slot edges). Under B5 the only edges left are source-slot identity edges (selector
cells were deleted in B5 4), so the unification joins whole slot types. **REACHABLE and
detectable.** For array/object contents it is subsumed by the U6 session heap (every earlier
value is heap-sited, `materialize` joins the pool, which holds every seed part and store): the
old `shared` witness (`a[0]` written through one sibling, read through the other) gives
identical inference results with and without the mutation. It is **not** subsumed for the root
kind of a captured binding when the stored value keeps the binding tag: tag 4 covers string,
function, future, collection and struct. Witness (new case `shared-scalar`):
`fn(mk()){a:="s";return {"wr":v=>{a=v;return 0},"rd":()=>a.to_upper()}};o:=mk();w:=1` then
`o.wr(()=>4);x:=o.rd()`. Production (and 17f7964c) refuses the probe before any effect,
`builtin-receiver:to_upper`; m13 admits it and stops in an internal failure, `unexpected
operands (compiler defect): built-in to_upper` (exit 70 on the CLI): an unchecked dispatch on a
stale kind, a B4 invariant-1 violation. `test-cp19-admission-mutants` and `-switch`: shared-scalar
PASS in both directions (control refuses, m13 internal; control fails the mutant expectation,
m13 fails the control expectation).

Evidence (`instr/`): an instrumented CLI (production objects except `infer.cpp`,
`infer_session_location.cpp`, `session_callable.cpp`, each a copy with logging only, plus a
run-time switch that applies the m13 mutation) records a canonical digest of every `UnitKinds`
result the compiler consumes. Its production mode is output-identical to the production binary
on every session run. Over 3,462 sessions (1,230 targeted: sibling closures, globals and loop
source roots sharing one captured cell, 8 shapes x 7 stored kinds x 5 reads, same and later
statement; 141 fixture sessions: every tests/**/*.in, the cp19 proof-cli cases, admission seeds,
the m3/m7 probes; 2,091 generated sessions of 11.3): 159 sessions have a different inference
digest under m13 and 11 a different output, all in the targeted corpus; 2 of the 11 are internal
failures (`to_upper`, `length/size`), 2 admit and run a statement production refuses (the next
statement is then refused), 7 refuse at another position or for another reason. The real
m13 CLI (harness object) gives the same results as the run-time switch
(`m13/output-diffs.txt`).

**[CORRECTED in 13.1 (B5 23, owner 2026-11-07 15:25): the m12 classification in this paragraph is
WRONG. The call site IS reachable (session closures capturing a function parameter that holds a
mutex, channel or atomic; `host-handle-flow` refuses only host handles such as `ofstream`), and the
"0 arrivals" corpus never built such a session. m12 is OBSERVATIONALLY EQUIVALENT to production on
the tested reachable paths, not unreachable and not proven universally equivalent. The original text
is kept below as written.]**

**m12** (`session_callable.cpp`, `may_call` always true). **UNREACHABLE on B5; retired with a
bounded proof, kept as a tripwire.** `may_call` has one call site (line 74, plus its own
recursion): a function upvalue that is a cell for which `cell_plain_capture` returns null. That
function returns null for a cell only when the terminal cell's binding type is negative and not
`kSourceSlot` (-3), or the source-slot chain is broken or deeper than 64. (a) B5 4 deleted the
selector cell (-2), the only producer of such a cell on 17f7964c (the `selector` witness caught
m12 there through a loop variable capture). (b) Every remaining cell comes from `make_cell` with
a binding type from `BindType`/`text_binding_type`/`binding_tag` (>= 0 except -1 for a
`Kind::Handle` or `Kind::Cell` value), `LoadI 9` (try/catch), session invocation types (tags of
strings/arrays/objects), or isolate-transfer decode (copied from an existing cell); a source slot
always wraps one fresh ordinary cell and `cell_rebind` keeps that shape, so chains have depth 2.
(c) A cell is never a script value, and a closure capturing a binding whose kinds include a host
handle is refused before compile (`compiler_unit.cpp:231`, `host-handle-flow`; probes
`m12/h1-h5.in` all refused at statement 1, nothing executed). Executable evidence: the same
instrumented CLI logs every arrival at that call site: **0 arrivals in all 3,462 sessions**. A
positive control (the instrumented objects linked with the m3 `cell.cpp`, which makes source-slot
captures non-plain) arrives there in 5 sessions (`binding_type=-3`, `may_call` both 0 and 1), so
the instrumentation does detect the branch. Even where reachable, m12 can only add refusals
(an incomplete shape), never admit or change a result.

**Harness** (`admission_mutants.py`): `selector` (m12) and `shared` (m13) stay as control
witnesses; their mutant rows are TRIPWIRES that require the mutant to behave exactly as the
control and FAIL if it ever differs ("re-check 12.1"). `test-cp19-admission-mutants` and
`-switch` still FAIL, deliberately, with exactly 2 of 15 cases: `loop` (control refused: the D4
family; **m3 has no B5 witness, the coverage obligation is retained and tied to the D4 release
blocker**) and `native` (**m10 survives on B5 and on 17f7964c: a pre-existing test-adequacy
finding, moved to the release-blocker audit to be reconsidered, not accepted**).

### 12.2 test-runtime2-cp12c-probes: the 48 differences, each classified

Each of the 48 `cp12c-probes/src` probes that differ from legacy under NIFT_VM=1 on B5 was run
on legacy (NIFT_VM=0), 17f7964c (NIFT_VM=1 and require) and B5 (NIFT_VM=1 and require,
identical), its B5 result derived by hand from the rule, and, where the cause was ambiguous,
checked against closure-free or loop-free controls (`cp12c/controls/`). Rules (kept distinct):
**snapshot** = snapshot iteration (owner 2026-11-01 "loops iterate a snapshot taken at entry",
2026-11-02 "snapshot iteration kept"; plan section 3 Iteration, D5; Q3 rows 5-6), including its
element-identity clause ("elements are the original references; mutation through the binding
mutates the element"); **D1** = per-iteration loop-variable binding (owner 2026-11-07 06:55
decision 1). Criterion: D1 only where the difference needs a closure observing a later
assignment to its own iteration's binding; snapshot where the loop binding's value is the entry
element (the closure-free control shows the same difference) or a closure reads that element
after the source was written, rebound or emptied.

| rule (sub-rule) | n | 17f7964c | probes |
| --- | --- | --- | --- |
| snapshot (live reads) | 19 | refused (lent / aliasing) | capture_live_at_creation, capture_mixed_live, capture_nested_live, capture_source_after, capture_source_repeated, captured_compound_write, captured_postfix_inc, captured_prefix_inc, eager_prefix_statement, f08_alias_mutate, f13_global_from_fn_loop_elem, f24_global_elem_write_via_fn, m10_unvisited_change, prefix_dec_live_source, prefix_live_source, t09_pop_current, t10_pop_later, t16_same_iter_modify, write_before_live_capture |
| snapshot (element mutation) | 4 | refused (shared-part) | capture_named_aggregate, capture_named_repeated, selector_missing_cached_write, selector_missing_side_effect |
| snapshot (nothing vanishes) | 16 | legacy-equal vanished-element error | f01, f02, f03, f04, f05b, f11, f12, f14, f17, f20, f22, t06, t08, t24, t26, t29 |
| snapshot (live reads by a closure after the loop) | 4 | legacy-equal vanished-element error | nested_selector_missing_call, nested_selector_missing_print, selector_missing_index, selector_missing_member |
| snapshot (live reads, source rebound) | 4 | legacy-equal live reads | g02_obj_global_fn_same_keys, g03_arr_global_fn_same_len, t14_obj_replace_values, t17_restored_before_read |
| D1 | 1 | refused (lent) | escape_before_assign (`f()` after the loop: 7; legacy 2) |

**47 snapshot, 1 D1; all 48 converted; 0 PENDING-OWNER.** The classification of the programs
shared with `capture_origins.py` (selector_missing_*, capture_named_*, capture_source_*) matches
its existing B5_APPROVED labels. Supplementary finding (not a cp12c case): legacy also treats an
ordinary element binding as a location (`x := a[0]; a = []; x[0]` fails "reference target no
longer exists"); 17f7964c refuses that (`aliasing`) and B5 prints 1 (shared references); it is a
consequence of the approved reference model, recorded here only. The harness pins each B5 result
(stdout, no stderr, exit 0, for NIFT_VM=1 and require), fails a pin that becomes legacy-equal or
goes missing, and keeps every other probe legacy-equal; negative control: on the 17f7964c binary
all 48 pinned probes fail. Originals and every result:
`docs/runtime-2/evidence/b5/cp12c-probes-original.json`. expected-changes.tsv: class i (24) and
iii (20 + 4) rows.

### 12.3 test-rt-vm: the 6 recipe lines (27 cases)

Each case was run on legacy, 17f7964c and B5 (`docs/runtime-2/evidence/b5/rt-vm-originals.json`).
Every one was refused before any effect at 17f7964c by the Phase A guard.

| line | case(s) | legacy | B5 | conversion | rule |
| --- | --- | --- | --- | --- | --- |
| vm_subset | async-capture-aliased | 2 | 2 | UNSUPPORTED (`aliasing`) -> supported | guard removal, legacy-equal |
| vm_subset_gen | aliasing trigger `tr := tq; print(tr)` (5 generated programs) | legacy's error "print: value is not directly renderable" | the same, reported by the VM | acceptable outcome `fallback:aliasing` -> `vm-error` (byte equality with legacy still required) | guard removal, legacy-equal |
| vm_records_coverage | push-member-after-call, decl-from-pushed-element, push-binding, push-member, push-index-loop | as B5 | as legacy | REFUSED (`aliasing`) -> SUPPORTED | guard removal, legacy-equal |
| vm_records_coverage | push_arrlit_fnloop | `[]` | `[[0,1],[1,2]]` | REFUSED (`reference-guard:argument`) -> pinned B5_APPROVED | arguments share identity (owner 2026-11-01) |
| vm_engine_selection | vm-d040-copy | 2 | 2 | `fallback` -> `vm` | guard removal, legacy-equal |
| vm_review_cp12 | chain199/201-alias | 2 | 2 | refusal `reference-guard:inflow` -> any accepted verdict | guard removal, legacy-equal |
| vm_review_cp12 | chain199/201-aliasret | 1 | 2 | refusal `aliasing` -> pinned | returns share identity (owner 2026-11-01) |
| vm_former_replay | aggregates-10-* (`arr.push(arr)`, 10 contexts) | pushes a copy, runs on | `before`, then "reference cycle: the stored value would contain itself" at the push (line, column 1; `return: ` in the two fnnest contexts), exit 1 | pinned, derived from the source | cycle refused before mutation (owner 2026-11-01; D-NEXT-refsem #4) |

"Guard removal" = owner 2026-11-02 (arrays/objects are shared references implemented directly by
the runtime, no replacement static guard). Every approved difference has its compatibility.md
row (arguments, returns, `a.push(a)`). Each pinned harness fails a pin that legacy starts to
match. Negative control: the changed expectations fail on the 17f7964c binary. **New test**
`tests/runtime2/b5/cycle/push_self_unchanged.f` (in `test-runtime2-b5`, production and probe
builds, NIFT_VM=require): six refused cycles (`arr.push(arr)`, through the alias, one and two
levels down, through an object, an element store); after each, the receiver's length and
contents, the alias's, the identity of the receiver, its alias and its parts (`same`), and a
container holding a part are unchanged; afterwards the receiver is an ordinary shared array
again. Golden hand-derived.

### 12.4 Receiver-returning reverse()/sort()/comparator sort (APPROVED 14:00)

compatibility.md and expected-changes.tsv now mark the row **APPROVED by the owner 2026-11-07
14:00** (previously "for owner confirmation") and record that comparator sort may use temporary
storage internally (copy-sort-commit) while returning the original receiver: returning the
receiver is not the same as sorting in place. Tests:
- `tests/runtime2/b5/receiver/reverse_sort_receiver.f` (production and probe): for `reverse()` and
  plain `sort()`, the result is `same` as the receiver and as its alias, the alias and a holding
  container observe the mutation, a mutation through the result reaches all of them (no fresh
  identity), a call through the alias returns the same array, element identity is kept. Legacy
  differs on 12 of the 20 lines; 17f7964c refuses it (`aliasing`).
- `tests/runtime2/b5/sort/comparator_receiver.f` with the existing `aliased_reads.f` (atomic
  commit: every alias read during the sort sees the original order), `failure_injection.f`
  (comparator failing at every call k: receiver and alias unchanged), `nested_identity.f`,
  `receiver_mutation.f`: identity through the alias, aliases observe the success, no fresh
  identity, a failing comparator leaves no result, the receiver, its alias and its element
  identities unchanged, then a normal sort of the same receiver.
- **Comparator-sort tests run only on the NIFT_RT_REFERENCE_PROBE build** (`nift-guardoff`) on this
  GCC 13.3 host: production refuses comparator sort here (`method:sort`, D2 release blocker); the
  GCC 15.2 production run belongs to the combined Linode campaign.

### 12.5 Gates rerun on B5 20 (`scratchpad/b5-fixup2/gates/f-*`)

Sequential `make -s` runs on B5 20 (`fc0d96ac`), result files with exit, HEAD and the
`git status --porcelain` hash (empty tree status throughout):

| gate | result |
| --- | --- |
| test-cp19-admission-mutants / -switch | FAIL by design: 13 of 15 cases PASS (shared-scalar catches m13; selector/shared tripwires hold), `loop` (D4, m3) and `native` (m10) fail |
| test-runtime2-cp12c-probes | **PASS** (142 src cases, 48 pinned; src2 6) |
| test-runtime2-cp12c-mutant-controls | PASS |
| test-runtime2-capture-origins (reads cp12c-probes sources) | PASS (82 cases) |
| test-runtime2-b5 (incl. new cycle, receiver; sort on the probe build) | PASS (71 production, 76 probe, sort-refusals 564) |
| test-runtime2-b5-mutants (run.py over every b5 dir) | PASS |
| test-runtime2-d040-witnesses (hashes the vm_review_cp12 programs) | PASS |
| test-rt-vm-policy-tooling | PASS |
| test-rt-vm own recipe (34 lines, line by line) | 31 PASS (vm-unit 40,347 checks 0 failures; the six lines of 12.3 now PASS); 3 FAIL, all failing on 17f7964c too: vm_parity `--rt-diff` (1), vm_builtins (4 cases, see 12.7), vm_modules (2 import-collision wording cases) |

Production binary `nift` sha256 `ac283072f622c473b117d76d4d2e4397606743a8804bee5c0fa1565145ac05b7`
after all runs (unchanged); `git diff 09651e11 HEAD -- src include scripts` is empty.

### 12.6 Coverage reconciliation (update of 11.10)

Inventory unchanged: **244** targets. Rerun this round: the nine targets of 12.5 plus the
test-rt-vm own recipe; every other result is carried from the B5 14/15 runs of 11.10, valid
because the production binary is byte-identical and none of those targets' inputs changed (the
harness files edited here are consumed only by the rerun targets and by the deferred
test-rt-vm-sanitize / test-rt-vm-mutants lanes; the Makefile change is the test-runtime2-b5
recipe only).

**Totals (244): ran 159 = PASS 121, FAIL 37, FLAKY 1; not run 85 = deferred to Linode 71,
umbrella 7, prerequisites run elsewhere 4, not applicable 3** (14 umbrella/nested/n.a.). Change from
11.10: test-runtime2-cp12c-probes FAIL -> PASS. Of the 37 FAIL: 0 B5-introduced unexplained;
2 the admission-mutants pair (now only `loop`/D4-m3 and `native`/m10, both release-audit items);
4 build-guard harness failures; 1 stale mutant (cp17-routing-mutants); 4 unit link / source-list
defects; 26 identical on 17f7964c (11.10). test-rt-vm (umbrella row): its own recipe is 31/34
(was 25/34), the 3 remaining lines failing on both trees. test-runtime2-cp01-simplify stays FLAKY
(zero-flakiness policy: infrastructure fix needed, not B5). **This is not a local certification**:
sanitizer/TSan lanes, wall-clock measurements, whole-runtime mutation campaigns, host
process/terminal campaigns and the GCC 15.2 production comparator-sort run are Linode work.

### 12.7 Open items after this round

- **PENDING-OWNER: none new.** No cp12c or test-rt-vm case needed an unapproved rule.
- Release blockers unchanged: D4 (11.1, incl. m3's coverage obligation), D2 comparator-sort
  portability, F4 multi-line comparator position, the pre-existing failures of 11.5; **m10** added
  to the release-blocker audit as a pre-existing test-adequacy finding to be reconsidered.
- Not converted here (outside the six lines; fail on 17f7964c too): vm_builtins' 3 extra B5 lines
  ("expected fallback reference-guard:*/builtin-receiver:empty, got compiled"), vm_parity
  `--rt-diff` (1 on B5, 8 on 17f7964c), vm_modules (3 on both). vm_builtins' four B5 cases (`shapes@loop`, `shapes@fn-loop`: expected `reference-guard:inflow`;
`w3-mixed`: `builtin-receiver:empty`; `f4-sort`: `reference-guard:shared`; all now compiled) are
guard-era refusal expectations of the same kind as 12.3 but were outside this round's six lines:
**not converted, to be verified case by case in a follow-up before integration.**
- `compiler_loops.cpp:73` stale "M15" comment (comment-only follow-up, production file untouched).

### 12.8 Freeze record (B5 21)

- Commits: B5 17 `f3df39b9` (admission mutants), B5 18 `bd7185a0` (cp12c-probes), B5 19
  `58bcb9bc` (test-rt-vm conversions, cycle test), B5 20 `fc0d96ac` (receiver tests,
  compatibility/expected-changes), B5 21 (this record).
- Production binary unchanged (sha256 `ac283072...`); no `src/`, `include/` or `scripts/` change
  since `09651e11`.
- `git status --porcelain` empty after the commit; `--ignored` lists build products only.
  Scratch mutant/instrumented CLIs and objects deleted after logging; no generated corpus in the
  tree.
- Not pushed, not tagged; runtime-2, weekend/r2, cand/b4-dispatch, cand-b4 (its `./nift` run
  read-only) and rv-b5 untouched.
- For the targeted re-review (owner 14:00): 12.1 (m12/m13), 12.2-12.3 (the 48 + 27 conversions
  and their rules), 12.4 (receiver identity), D4 classifications unchanged (11.1).

## 13. Final fix-up from 3c9c66f4 (owner 2026-11-07 15:25; B5 22 to B5 24)

The targeted re-review of `3c9c66f4` (tree `b1b1e389`; `scratchpad/review-b5/REVIEW-R2.md`) is
ACCEPT-WITH-CONDITIONS; the owner's 15:25 decision (binding) set four conditions. This round, by
an agent that is neither the reviewer nor an earlier implementer, closes them with **tests,
harnesses, one Makefile dependency fix and documentation only: no `src/`, `include/` or
`scripts/` change** (`git diff 3c9c66f4 HEAD -- src include scripts` is empty). Evidence:
`scratchpad/b5-fixup3/` (`clean/` clean-checkout builds, `m12/`, `compat/`, `vb/`, `gates/`).
Commits: B5 22 `22624c05` (Makefile), B5 23 `f6e1aac7` (probe, fixtures, harness, compatibility,
evidence), B5 24 (this record and the 12.1 correction marker).

### 13.1 m12: corrected classification (R2-1; owner 15:25 ACCEPTED)

**The original classification (12.1, B5 21) was wrong, and stays visible there, marked.** It
said m12 (`session_callable.cpp`: `may_call` always true) was UNREACHABLE on B5, because the
only remaining cell with binding type -1 would hold a host handle, which `host-handle-flow`
refuses, and the instrumented corpus had 0 arrivals in 3,462 sessions. Correction: `binding_tag`
returns -1 for any `Kind::Handle`, and `HandleKind` includes Mutex, Channel and Atomic;
`host-handle-flow` refuses only host handles such as `ofstream`. A session closure capturing a
**function parameter that holds a mutex, channel or atomic** reaches the call site
(`is_cell(capture) && !may_call(bound)`); the B5 21 corpus never built such a session.

**Classification: m12 is OBSERVATIONALLY EQUIVALENT to production on the tested reachable paths.
It is not unreachable and has not been proven universally equivalent.** At every arrival observed,
production's `may_call(bound)` is already true (the bound type carries the handle kind bit,
outside the allowed set), so forcing it true changes nothing on those paths. m12 stays an
**informational negative control**: the `selector` tripwire rows in `admission_mutants.py`
(comment corrected), plus a new permanent probe. **If m12 ever becomes observable (the probe
fails, or the tripwire trips), this classification must be revisited.**

Permanent reachable-path regression probe: `tests/runtime2/cp19/m12_reachable.py`, run by
`make test-cp19-m12-reachable`, which is part of `test-cp19-mutants`. Its sessions,
`tests/runtime2/cp19/m12_reachable/p1-p10.in`, are the reviewer's probes, verbatim. From the
production build plan (`make -n nift`) the probe builds, in a temporary directory that is deleted
afterwards:
- an instrumented production CLI: a copy of `session_callable.cpp` that logs production's
  `may_call` at each arrival at the call site;
- the real m12 CLI: `-DNIFT_SESSION_ADMISSION_MUTANT=12`.

It then runs each session with `nift -i` and `NIFT_VM=require`, and requires:
- **p4-p10 reach the call site, 23 arrivals in all, with production's `may_call` = 1 at each.**
  These are mutex (p4, p5, p7, p8), channel (p6, p9) and atomic (p10) parameters captured by
  closures.
- The p1-p3 controls (`ofstream` captures) are refused with `host-handle-flow` and never arrive.
- Production, the instrumented CLI (log lines removed) and **m12 give identical stdout, stderr and
  exit status**, equal to the pinned results (`1`, `ok`, `1`, `1`, `2`, `2`, `2`; the controls exit
  1).
- An arrival where production's `may_call` is false fails the probe ("m12 may now be observable").
  So does an m12 difference.
- If the call-site anchor moves, or there is no build plan, the probe exits 2 (a harness failure).

Run on the tree (`m12/run2.log`) and in the clean worktree (`gates/test-cp19-m12-reachable.log`):
PASS. This is the demonstration the owner asked for: **m12 built temporarily, same result as
production, build deleted.** Nothing is left under `/tmp` or in the tree.

Limit, found while checking whether the probe can discriminate (scratch only,
`m12/inverse_control.log`): a scratch variant that makes `may_call` return **false** at the same
site also gives output identical to production on p1-p10. So these sessions' printed results do
not depend on the branch's value. The equivalence on the tested paths therefore rests on the
logged fact that production's `may_call` is already true at every arrival (m12 computes the same
value there), not on output sensitivity. The probe checks that fact directly.

### 13.2 Clean-build dependencies (R2-2)

Cause: `test-runtime2-b5` and `test-runtime2-b5-mutants` named `$(B4B_GUARDOFF_NIFT)` as a
prerequisite before the Makefile defined it. Make expands prerequisites when it reads a rule, so
the prerequisite was empty. Fix (B5 22): the definition and its rule move, unchanged, above the two
targets. Evidence (`clean/dryrun.txt`): in a clean worktree, the `make -n` plan contains **0**
probe-binary links at `3c9c66f4` for both targets, and **1** at `22624c05`.

Clean-checkout runs, in a temporary worktree (`git worktree add --detach … f6e1aac7`). Each run
starts from `git checkout --detach f6e1aac7; git clean -ffdxq`. Before each run
`git status --porcelain --ignored` is empty and there is no `.build`: no cached objects, no
prebuilt probe, no libffi. Script: `clean/run.sh`; logs and results: `clean/<name>.{log,result}`.

| run | command (from clean) | result |
| --- | --- | --- |
| b5-seq | `make -j1 test-runtime2-b5` | exit 0: builds libffi, `nift` (sha256 `ac283072…`) and `.build/b4b-guardoff/nift-guardoff`; PASS 73 production, PASS 78 probe, sort-refusals 564 |
| b5mut-seq | `make -j1 test-runtime2-b5-mutants` | exit 0: builds `nift` and the probe binary; PASS b5 mutants: 1, 3, 4, 6, 7, 8, 11, 12 all caught |
| b5-j4a / b5-j4b | `make -j4 test-runtime2-b5` (twice, each from clean) | exit 0 both; same PASS lines; `nift` sha256 `ac283072…` |
| b5mut-j4a / b5mut-j4b | `make -j4 test-runtime2-b5-mutants` (twice, each from clean) | exit 0 both; all 8 caught |

No dependency race: none of the six logs has a make error, a missing-file error or a "No rule"
line. An earlier `make -j1 test-runtime2-b5` from clean at `22624c05` also passed
(`clean/b5-seq-22624c05.result`; 71/76, before the two new fixtures). Fixture counts are now 73
production and 78 probe, previously 71/76: `q3` gained r14 and r15 (13.3).

### 13.3 Compatibility rows (R2-3; both APPROVED by the owner 2026-11-07 15:25)

Added to the compatibility.md table "Arrays and objects are shared references". The earlier
rows and the approval history (14:00 receiver row and others) are unchanged. Also added to
`expected-changes.tsv` (class i), each row citing the approval.

1. **Extracted array elements stay valid after the source binding is reassigned.** Measured
   (`compat/`), comparing B5 (`require` and `NIFT_VM=1`), legacy (`NIFT_VM=0`) and 17f7964c:

   | program | B5 | legacy | 17f7964c |
   | --- | --- | --- | --- |
   | `a := [1]; x := a[0]; a = []; print(x)` (the owner's example) | `1` | **`1`** | `1` |
   | `a := [[1]]; x := a[0]; a = []; print(x[0])` (reviewer's `side.f`) | `1` | error `reference target no longer exists: x[0]` at 4:7, exit 1 | refused `aliasing` (2:1) |
   | `… print(y.stringify())` | `[1]` | error `reference target no longer exists: y` (exit 1) | refused `aliasing` |
   | `o := [{"k": 1}]; z := o[0]; o = []; print(z.k)` | `1` | error `reference target no longer exists: z.k` | refused `aliasing` |
   | `c := [[1]]; w := c[0]; c = [[5]]; print(w[0])` | `1` | `5` (reads the location through the new `c`) | refused `aliasing` |

   **Correction to the brief:** legacy prints `1` for the owner's literal scalar example, so it
   is legacy-equal; legacy copies a scalar element. The legacy difference exists for an
   aggregate element, which legacy binds as a location in the source: after `a = []` legacy fails
   "reference target no longer exists: <expr>", and after a rebinding it reads the new source.
   The row records both. B5's behaviour is the one approved: `1` for the example, and the element
   itself in every case. Fixture: `tests/runtime2/b5/q3/r14_extracted_element_survives_rebind.f`
   (golden hand-derived, `1 [1] 1 1`). On it, legacy fails at 13:7 with `reference target no
   longer exists: y` and 17f7964c refuses `aliasing` at 11:1.
2. **`first()` returns the element itself.** `a := [[1, 2], [3]]; head := a.first(); head.push(9);
   print(a.stringify()); e := a[0]; print(same(head, e))`: B5 `[[1,2,9],[3]]` and `true`; legacy
   `[[1,2],[3]]` and `false`; 17f7964c refuses `aliasing` at 8:1 (the version without `same`:
   `reference-guard:inflow` at 2:9). Fixture: `tests/runtime2/b5/q3/r15_first_returns_element.f`.
   Both fixtures pass on the production and probe builds and are part of `test-runtime2-b5`.

### 13.4 vm_builtins: the 4 cases, each converted on its own rule

Each case was run on legacy, B5 (classify, `require`, `NIFT_VM=1`) and 17f7964c (classify,
`require`, `1`). Original expectations, every result, the conversion and the rule are in
`docs/runtime-2/evidence/b5/vm-builtins-originals.json`. The original harness is
`git show 3c9c66f4:tests/runtime2/vm_builtins.py`.

| case | original expectation | legacy | 17f7964c | B5 | conversion | rule |
| --- | --- | --- | --- | --- | --- | --- |
| `shapes@loop` | refused `reference-guard:inflow` | 16 lines incl. `[[1,2],[3]]` x2, exit 0 | refused `reference-guard:inflow` | compiled, `vm`: legacy's output with `[[1,2],[3]]` -> `[[1,2,9],[3]]` (2 lines), nothing else differs | `B5_PINNED_IN_CONTEXT` (exact: legacy with the pinned line replaced; fails if legacy starts to print the B5 line) | `first()` returns the element (13.3 row 2, APPROVED 15:25), after guard removal (owner 2026-11-02) |
| `shapes@fn-loop` | refused `reference-guard:inflow` | as above, plus `0` lines | refused `reference-guard:inflow` | as above (2 lines differ) | as above | as above |
| `w3-mixed` | refused `builtin-receiver:empty` (CP14 W3) | `false false` | **compiled, `false false`** (the expectation already failed there) | compiled, `false false` | `SUPPORTED_TOP` (legacy-equal) | **not a B5 rule**: B4a Dynamic admission (owner B4 ruling 2026-11-06 01:09; b4-dispatch.md section 1, a closed string/array union with each family's `empty`). B3 `4c7d0b40` still refused it |
| `f4-sort` | refused `reference-guard:shared` | `2` | refused `reference-guard:shared` | compiled, `2` | `SUPPORTED_TOP` (legacy-equal) | guard removal (owner 2026-11-02); `sort()` returns the receiver (APPROVED 14:00) |

The reviewer's claim checks out with one correction. w3-mixed and f4-sort are legacy-equal, and
the two `shapes` cases differ from legacy only through `first()` returning the element. But
w3-mixed was **not** refused at 17f7964c: it was a stale expectation from B4a (the 12.5 / 12.7
note "failing on 17f7964c too" is right only for this case). Negative control: on the 17f7964c
binary the converted harness fails exactly `shapes@loop`, `shapes@fn-loop` and `f4-sort` (not
compiled). On B5: **PASS** (67 supported, 2 pinned, 26 error programs, 28 refusals, 2 budget stops).

### 13.5 Gates on a clean tree (`scratchpad/b5-fixup3/gates/`, clean worktree at `f6e1aac7`)

| gate | result |
| --- | --- |
| test-runtime2-b5 | **PASS** (73 production, 78 probe, sort-refusals 564); sequential and `-j4` x2 from clean (13.2) |
| test-runtime2-b5-mutants | **PASS** (all 8 caught); sequential and `-j4` x2 from clean |
| test-rt-vm, vm_builtins line (`python3 tests/runtime2/vm_builtins.py --nift $PWD/nift`) | **PASS** (0 failures; was 4) |
| test-cp19-m12-reachable (new) | **PASS** (7 reachable, 23 arrivals with production `may_call` already true; 3 controls; production = m12) |
| test-cp19-admission-mutants / -switch | FAIL by design, exactly as in 12.5: 13 of 15 cases PASS (the selector/shared tripwires hold, shared-scalar catches m13); `loop` (D4, m3) and `native` (m10) fail |

After the runs: the production `nift` sha256 is `ac283072f622c473b117d76d4d2e4397606743a8804bee5c0fa1565145ac05b7`
(unchanged, built from clean); `git status --porcelain` in the clean worktree is empty;
`--ignored` lists build products only (`.build/`, `nift`, `*.o`/`*.d`/`*.cfg`, `libnift_c.*`).

### 13.6 Open items (unchanged by this round)

These remain release blockers, as recorded in 12.7: the D4 four (with the group audit; m3's
coverage obligation), D2 comparator-sort portability, F4 lambda line positions, m10, the
pre-existing failures, and cp01-simplify FLAKY (zero-flakiness policy). m12 is informational
(13.1), not a release blocker, and must be revisited if it becomes observable. No PENDING-OWNER
item is new.

### 13.7 Freeze record (B5 24)

- Commits: B5 22 `22624c05`, B5 23 `f6e1aac7`, B5 24 (this record). Production binary
  unchanged (sha256 `ac283072…`). No `src/`, `include/` or `scripts/` change since `3c9c66f4`.
- `git status --porcelain` is empty after the commit; `--ignored` shows build products only. The
  temporary clean worktree was removed (`git worktree remove --force`). The scratch m12 and
  instrumented CLIs were deleted by the probe. No generated corpus is in the tree.
- Not pushed, not tagged. runtime-2, weekend/r2, cand/b4-dispatch, cand-b4 (its `./nift` was
  only run) and rv-b5 are untouched.
- For the narrow independent confirmation: 13.1 (the m12 correction and probe), 13.2 (the clean
  and parallel builds), 13.3 (the two rows, including the scalar-example correction), 13.4 (the
  four conversions).
