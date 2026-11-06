# Checkpoint B5: guard removal, the reference-semantics switch (D-NEXT-refsem-b)

Status: **IN PROGRESS, NOT FROZEN** (implementation agent stopped at its budget; section 9 lists
exactly what is done and what is not). Branch `cand/b5-refsem` from the accepted B4b tip
`17f7964c` (tree `ef8c508d`). Commits B5 0 (B4b closure), B5 1 (U6), B5 2 (the switch, Q7, loop
snapshots, fixture conversion), B5 3 (this record). Plan: Phase B plan §3, §5, "B5"; owner
decisions 2026-11-02, addendum 2026-11-04 (batch certification), owner ruling 2026-11-07 03:53.

## 1. D-NEXT-refsem-b (decision record)

Arrays and objects are shared mutable references in the production VM, like maps, sets, stacks
and struct instances (Phase A). The Phase A forward-compatibility guard and the `aliasing`
location-alias refusal are deleted; no replacement static mechanism. Plan §3 is the contract, with
the owner's rulings: Q1 `args` immutable through every alias (kImmutable, the error names the
receiver expression); Q2 option B (`const` is binding-only: `const a := [1]; a.push(2)` runs);
Q7 comparator sort copy-sort-commit (2026-11-07); D5 entry-snapshot iteration for every loop
(B2 coordinator ruling 1 moved the binding-loop snapshot to B5). Legacy (`NIFT_VM=0`) unchanged.

## 2. Static machinery (plan §5 list)

| Item | B5 |
| --- | --- |
| 1 guard (`reference_guard{,_walk,_call}.{h,cpp}`, the call in `compiler.cpp`) | deleted (1,043 lines); routes no longer reachable |
| 2 `UnitCompiler::no_alias`, refusal `aliasing` | deleted, with its only helper `collection_copy.{h,cpp}` |
| 3 take/put | gone since B2 except the compound-assignment string take (`g += x`), kept by B2 ruling 4 (string append, not reference machinery) |
| 4 static const-array path (`ArrayMutate c=100`, comparator-sort path, index-store refusal, inference `const_arrays_`) | deleted (Q2 B); B2 L3 (its validation order) is gone with it; `declare-const` for non-array containers stays (not an over-refusal of a mutation) |
| 5 guard-only facts (`nested_aggregates`, `collection_arrays/objects`, the `collection_array/object` provenance flags, `holds_aggregate_below`) | deleted; B4b's L-2 monotonicity exception disappears with them |
| 6 live array-loop path | semantics switched (every loop iterates its entry snapshot, `compiler_loops.cpp`); the now-unused lazy-location code is not yet deleted (section 9) |
| 7 presentation/receiver refusals | superseded by B4a/B4b: the remaining `builtin-receiver`, `collection-receiver`, `collection-element`, `struct-value`, `struct-no-method`, `presentation-struct-method` reasons are the admission table's Refuse outcomes kept by the B4 rulings (precision bound); see the trim decision point |
| `reference_probe.h` | kept as the comparator-profile test configuration only (it no longer affects the guard or `aliasing`) |

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

**Precision cost (owner acknowledgement needed, E-U6-1):** cp19 proof `shared-callable` (an escaped
closure's unknown parameter stored into a closure-captured array, read by a sibling closure) was
admitted with a run-time callable guard; it is now refused before any effect
(`session:current-callable-proof`). CP19 admission mutant 13 (capture-location sync) is subsumed
for array contents by the heap; its proof witness now checks the refusal on both binaries.

## 4. Comparator sort (owner Q7 ruling 2026-11-07; deferred B2 L1)

`collection_sort.cpp`: the sort runs on its working copy (the KeepAlive scratch) and commits it
into the receiver once (`Protocol::commit_sorted`) on success; the error drain commits nothing.
Receiver protection (B2) is unchanged. Fixtures `b5/sort/`: aliased reads during the sort see the
original order (L1), deterministic failure injection at every comparator call leaves the receiver
and every alias unchanged (legacy leaves 27 of 27 permutations changed), identity kept, receiver
mutation is a catchable error. Mutant 8 (in place again) is caught. B2 L2 (`cell.cpp`
`replace_path`) not re-assessed (section 9).

## 5. Gates (local; `scratchpad/b5/gates/`)

On the final code (production `nift`, guard deleted): PASS `test-rt-lint`, `test-runtime2-b4`
(12,684 checks), `-b3` (468), `-b4b` (shared-reference mode, 6,629), `-b5` (57 programs),
`-b5-mutants` (7 caught), `-refsem` (566), `-collections` (280), `-packages` (831), `-args-cmd`
and its mutants (5 caught; mutant 5 redefined: args without kImmutable; tripwire retired),
`-safety` (1,163 cases, fuzz 33,512), `-coro-module`, `-refrep-cli`, `test-rt-refrep` (129),
`-vm-default`, `test-cp19-session`, `-session-cli`, `-proof`, `-proof-mutants`;
`test-runtime2-guardoff` passed after U6 (B5 1). Pre-existing failures (identical on the B4b tip
code): `test-cp19-proof-cli`, `test-cp19-s1-dispatch`, `test-cp19-shared-eval`,
`test-cp19-admission-mutants`, `test-cp19-session-mutants`.

## 6. Expected-output changes

`docs/runtime-2/evidence/b5/expected-changes.tsv` (one row per fixture or case family: previous,
new, reason, establishing test), classified per the owner's ruling item 5: **i** newly admitted
(was refused) 146 rows; **ii** previously admitted, behaviour changed 1 row (E-U6-1, owner ack);
**iii** previously admitted, changed by an approved rule 3 rows (binding-loop snapshots Q3 5-6,
comparator copy-sort-commit Q7, const binding-only Q2). refsem guard fixtures: 53 converted (44 to
`approved/` with a `.rule` naming the §3 row, 9 legacy-equal to `parity/`), 12 non-guard refusals
kept; `pending/` is empty (all nine converted as the plan lists).

## 7. Mutants

B5 suite (`tests/runtime2/b5/mutants.sh`): U1, U3, U4, R-B6 (a pushed array/object copied), R-B7
(args a binding-only check), 8 (comparator sort in place), all caught. args-cmd mutant 5
redefined and caught. CP19 proof mutant 13 recorded as subsumed. Not yet done: retiring refsem
R13/R17/R18/R20 in `refsem/mutants.sh` (their file no longer exists), reinstating the cp14b design
mutants, the for-s2 M1 redefinition (now "live re-reads again").

## 8. Decision point: `trim` and string built-ins (not implemented)

`scratchpad/b5/trim/PROPOSAL.md`: 1,276 gen_alias programs newly refused by B4b's narrowing are all
valid programs (receiver may be a map or struct instance: 1,263); B4a's flagged `CallNative`
dispatch can carry the check by giving non-plain families a defined ordinary failure in the one
admission table (no new subsystem); about 60-100 production lines; tests and mutants listed. The
conservative refusal stays until the owner approves.

## 9. Not done (stop point)

- Fixture/expectation conversion still open: `test-runtime2-cp14b` (functional: 8+ cases), for-s2
  `EXPECT`, closures-s1 `EXPECT`, `capture_origins.py` GUARDED, cp18 `cow_semantics.py`,
  vm-corpus/cp11 `EXPECT`, tests/rt units (`vm_unit.cpp`, `refrep_vm.cpp` guard assumptions),
  `refsem/mutants.sh` R13/R17/R18/R20, cp14b design mutants, `classification.tsv` row 209
  (`reference-guard:*`) and the CP22/CP23 tsv references, compatibility.md §8 Q3 list.
- Not run: full B4a/B4b gate sets (fixpoint, b4b-mutants, b4-mutants, work, compile scaling), the
  generators (gen1, gen_struct, gen_alias on production), CP25-R re-inventory, classification
  comparison against 17f7964c, semantic differential, CP22/CP23/VM-unit/VM-adversarial lanes,
  run-time benchmarks.
- Item 6 deletion (lazy loop locations) after the cp14b/for-s2 audit.
