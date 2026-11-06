# Checkpoint B4b: sound per-site element and member typing

Status: **widened-group repair round candidate (B4b 22 to 25), frozen for a fresh independent
re-review of the whole B4b scope** (B4b). The final repair round's tip `16f24f36` was REJECTED by
the independent review (section 14; report and reproducers kept). Branch
`cand/b4-dispatch`, after the accepted B4a boundary `0df0df6c`. Plan: Phase B plan §5.7 and "B4"
(per-site types, mutant A2); the owner's B4b ruling (2026-11-06); the coordinator's approval of the
design note with conditions C1-C8 (2026-11-06) and the owner's evidence requirement (section 3).
Commits B4b 0 (this note) to B4b 6 (rejected tip `e293105e`), fix round B4b 7 to 14 (section 10),
final repair round B4b 15 to 21 (sections 11 to 13: F1 convergence, F2 test isolation, P1 mutant
and audit, and a pre-existing U1 hole found on the way; gates of that round: section 8), widened-
group repair round B4b 22 to 25 (sections 14 to 17: the union invalidation defect of the review of
`16f24f36`, the deterministic compile-scaling gate, the `glob` admission difference, and the
verification of the final tree; tip, tree and gates: section 17).

B4b changes no observable behaviour while the guard is on, except that programs which exited 70
on B4a are now refused or run their checked dispatch (sections 6, 13); otherwise it only moves
programs between admitted and refused (section 6). Run time is unchanged (section 7).

## 1. Design note (approved, with conditions C1-C8)

**Today.** A binding's type (`infer_detail::Ty`) carries its array's `elem`, positional `items`
(tuples) and its object's `fields`. They flow with the value and are widened only through the
binding that is stored into (`add_element`, index/member stores; B3 N1 drops positions there).
Under copy semantics that is exact; under sharing an alias's store is invisible to the other
holders: stale. The guard (`reference_guard`) currently refuses every unit where that could show,
so B4b changes no admitted output; the facts must be sound before B5 removes the guard.

**What is tracked per site.** Arrays and objects get allocation sites, as map/set/stack already
have (`Ty::sites`, `Inference::contents`, D-NEXT-refsem): a site is the node that creates the
value (an array or object literal, a constructing built-in or method: `split`, `keys`, `map`,
`copy`/`deepcopy`, `json` parse, ...; `args` has one fixed site). A value's type carries its
sites, never contents. Per site the inference keeps: the element union; the member union per
literal key plus a union for other keys; the positional types of a literal, valid while the site
is `positional`; and a `deep` flag. `element_of` becomes an `Inference` member reading the union
over the value's sites. Contents are monotone unions over a finite, depth-capped lattice, so the
existing worklist converges; growth of a site re-walks the units that read it.

**Mutation paths.** Every store merges into **every site of the receiver's type**, whatever the
spelling of the receiver: direct (`xs[0] = v`, `o.k = v`, `xs.push(v)`); through an alias
(`ys := xs` gives `ys` the same sites); nested (`xs[0][1] = v`, `inner := xs[0]; inner.push(v)`:
the inner value's sites are read from the outer site's contents); in a callee or closure (a
parameter's type is the union of the arguments, a captured cell is the binding's one slot, so
their sites are the caller's); a module export (modules are linked into one unit, D-037: the
importer's store reaches the module's site). Any mutator other than a pure append, any index
store with a non-literal index, and any store into a site of unknown position clears
`positional` (B3 N1 per site, so through every alias). **Session re-entry**: the contents of a
container created by an earlier statement are unknown (anything); only the statement's own sites
are tracked. **Thread and async transfers** copy arguments (D7, `deep_copy_tuple`, alias-preserving
`TransferGraph`); the callee's parameter keeps the sender's sites, a sound over-approximation
(its stores widen the origin too); a channel receive or an awaited result of unknown provenance is
anything.

**Where it gives up and widens.** Unknown origin (a value from an earlier session statement, an
untyped native result, an element of an unknown container), more than 16 sites on one value, the
depth cap, or a walk budget overflow: contents are anything. A method on such a receiver is
`Dynamic` (flagged, under the precondition) or refused, never `Static`.

**Relation to existing rules.** B3 N1 (positions dropped after a non-append mutator of the binding)
becomes the per-site `positional` flag, so it holds through aliases, callees and modules. The
union widening ("every kind ever stored", B3 §9) is unchanged, now per site: a superset of what any
slot holds now. The per-binding `elem`/`items`/`fields` remain only in the guard's own facts
(`nested_aggregates`, `collection_arrays`/`_objects`), which B5 deletes with the guard; nothing in
admission reads them after B4b.

**The §9 prerequisite: unflagged method-site classes.** A `Static` (unflagged) method site's
receiver kind comes from one of:

| Class | Receiver | Under sharing | Answer |
| --- | --- | --- | --- |
| U1 | a binding's own value: local, parameter, global, captured cell, session binding, module binding | sharing changes contents, never which value a binding holds; a binding's kinds are the union of every write to it (parameters: every call; cells and globals: one slot) | (a) sound as is |
| U2 | an element or member: `xs[i]`, `o.k`, `o[k]`, a `for` element binding, a destructured name, an element-returning method (`pop`, `first`, `last`, `get`, `find`, `values`...) | the stale case | (a) per-site contents (this checkpoint) |
| U3 | a struct field read | one slot per struct field, shared by every unit (B3) | (a) sound as is; a field's container contents are U2 |
| U4 | map/set/stack contents | per site already (D-NEXT-refsem) | (a) sound as is |
| U5 | a call's result (function, lambda, native) | the union of returns; container parts per site | (a) via U2 |
| U6 | a value carried from an earlier session statement | contents unknown | (a) by widening to `Dynamic` (precondition in force) |
| U7 | a transferred value (thread or async argument, channel receive, awaited result) | copies; sender's sites or unknown | (a) via U2, or widening to `Dynamic` |

No class takes (b). If the review rejects the argument for any class, that class's sites are
flagged instead (the precondition costs about 15 instructions per call, B4a §6).

**Evidence.** While the guard is on, a stale fact cannot change an admitted program's output (the
guard refuses those programs), so end-to-end fixtures cannot catch A2. The primary oracle is
therefore a unit test on the inference (`tests/rt/b4b_types_unit.cpp`): for each required shape
(alias replace, member change through another reference, inner alias, function, closure, session
re-entry, module export, thread/async argument; wrong-kind swaps such as a map where a struct was)
the kinds of the observing expression must include the stored kind. The same shapes are
`b4b/` fixtures with a legacy differential (refused before any effect, or equal to legacy; never a
wrong dispatch, exit 70 or a leak). Mutants: A2 (an alias store merges into the binding only), one
per further path (callee/closure, nested inner alias, positional flag, session contents, transfer).

**Risks.** Precision: reads of one site's contents now see every alias's stores; positions are lost
program-wide after any non-append mutation of the site. Compile time: site growth re-walks readers.
I stop and report on either beyond the agreed bounds. Size estimate: 500 to 800 production lines
in the inference (`infer_types`, `infer_collection`, `infer`, `infer_walk`, `infer_for`,
`infer_builtins`, session seeds).

## 2. Implementation and the conditions

`src/rt/infer_sites.{h,cpp}` (new), `infer_mutators.h` (new), and hooks in `infer.cpp`,
`infer_record.cpp`, `infer_walk.cpp`, `infer_collection.cpp`, `infer_for.cpp`, `infer_session.cpp`,
`infer_types.{h,cpp}`, `infer_unit.h`. Production: 12 files, +490 -33 (against `0df0df6c`).

- **Sites.** `Inference::fresh` gives a construction its site: an array or object literal, a
  native's new array or object (the call node: `split`, `keys`, `map`, `filter`, `slice`, ...),
  the `for` loop object, an invocation binding (`args`). `materialize` (called by `record`, so every
  expression's type) joins the contents of the value's sites into its element, member and
  positional types, drops positions of a site any store reached, and gives a native's inner part
  without a site one derived site per container site (`group_by`'s arrays). A value of unknown
  origin carries `kOpenSite`: its contents read as anything, and a store through it sets
  `agg_unknown_` (every container then reads as anything). `unite` keeps `kOpenSite` when a value
  without sites joins one with sites (never "only the other's sites"); the map/set/stack contents
  rule treats it the same way.
- **Stores** (`site_store`): every site of the receiver's type gets the stored value (under a
  literal key for an object, otherwise the union read by every key and position) and loses its
  positions. Paths: index store, member store, compound assignment and increment of an element
  or member (`element_update`), the `xs.push(v)` statement, the array methods by their effect.
- **C1, mutator completeness.** `infer_mutators.h` classifies every entry point of the B1 protocol
  (`rt/mutation.h`; `kProtocolEntries`) and every `CollectionMethod` (`kCollectionEffects`, one
  entry per method, enforced by a `static_assert` on the count). `b4b_types.py` checks against the
  sources that every protocol entry point is classified, that the effect table lists exactly the
  `CollectionMethod` enumerators in order, that no native (`natives/*.cpp`) calls the protocol
  (the CP14 natives never mutate their receiver or arguments), and that no other file starts using
  it unclassified. Map/set/stack stores are their per-site contents (D-NEXT-refsem); values stored
  there carry their own sites. Mutant A20 drops `reverse` from the table.
- **C2, Dynamic or unknown receivers.** A method that may mutate (`set`, `add`, any array method
  whose effect is not None) on a receiver that is not exactly an array stores its stored
  arguments (by the method's effect; a comparator is not stored) into every site of the receiver,
  or makes every container anything when it carries none (`dynamic_mutation`; mutant A24).
- **C3, dynamic keys.** A store under a non-literal key goes to the union read under every key; a
  read with a default (`get(k, d)`) includes the default (`ResultRule::MemberOf`, existing). The
  language has no member deletion (`object_erase` is reached by no source operation); a key-set
  change would be a store like any other, dropping key-exact precision through the same union.
- **C4, escaping functions.** Callables flow through every container, return and closure (CP12C):
  a call through a tracked value merges its arguments into every target. Two places lose the
  targets: a function value passed to a native the inference does not walk (`escape_callables`:
  the CP14 natives and free built-ins; collection callbacks and `thread` are walked), and a call
  whose callee may be a function of unknown targets (`untracked_call`: every function used as a
  value may be the callee). Their units' parameters become anything (mutant A23). Calls from a
  later session statement are U1/U6 (section 4).
- **C5, session-compiled code.** Option 3, shown: the existing CP19 machinery recompiles a
  session's functions coherently with each statement (`coherent_recompile`; the stats of
  `b4b/sessions/function_reads_after_store.in` show the function's protos compiled again by every
  later statement), and a statement storing into an earlier binding is checked against that
  binding's shape contract (`session:binding-shape-contract`, CP19 mutants 7/8). An alias store in
  a later statement (`ys := xs`, `ys[0] = ...`) is refused by the guard
  (`b4b/sessions/alias_store_reentry.in`: `reference-guard:inflow`). B4b adds: a statement's own
  containers are tracked per site like a script's (`session-alias`, `session-function` in the
  oracle), and a call into an earlier statement's code makes every container it can reach anything: the
  call's arguments and receiver and every container the statement stored into an earlier
  statement's binding, at every depth (`session_effects`, `expose`; oracle `session-earlier-call`,
  `session-exposed`; mutant A21). Values of earlier
  statements keep their recorded structure: this is **guard-dependent** (U6, section 4).
- **C6, modules.** Modules are linked into one unit (D-037), so an importer's store reaches the
  module's sites: `b4b/modules/importer_store` (the module's function sees the importer's string
  where an instance was: refused `builtin-receiver:get`, never C's method), `reexport` (the VM
  dispatches `trim` on the int the importer stored, equal to legacy). A module imported by two
  importers is not admitted (`two_importers`: `import-reinitialised`; legacy initialises it twice).
- **C7, transfers.** A thread's and an async task's arguments are copied at Spawn (D7,
  alias-preserving `TransferGraph`); the callee's parameters keep the sender's sites, a sound
  over-approximation (`thread` in the oracle). `b4b/scripts/thread_read.f` (admitted, equal to
  legacy): the worker reads its copy while the sender stores into the original. No admitted form
  shares a container between threads without a copy: `thread_copy.f` and `thread_return.f`
  (mutating) are refused by the guard (`reference-guard:argument`, `aliasing`), `channel_container.f`
  by `method:send`; a mutex's value read back (`mutex_container.f`) is of unknown origin (refused
  `index-target`); an atomic holds only integers.
- **C8**: sections 5-7.
- **Final repair round** (B4b 15 to 18): `infer_prune.cpp` (opened classes read and stored as
  anything; materialized reads kept per state of the site facts), `infer_bindings.cpp` (binding
  lookup; the lambdas that read a binding), `covers` in `infer_types.cpp`, and the changes of
  section 11 in `infer_sites.cpp`, `infer_walk.cpp`, `infer.cpp`, `infer_unit.h`; production
  against `0df0df6c`: 23 files, +1011 -56 (`src/rt`).

## 3. The owner's properties and their evidence

| Property | Evidence |
| --- | --- |
| Every method-call site is statically proven or dynamically checked | B4a: admission is Static only on proven kinds, else Dynamic (flagged, under the precondition) or Refuse; `op-admission-unit` (4,027 checks), the spec x kind enumeration (3,000 programs) and the struct oracle (1,911) in `test-runtime2-b4`; mutants A3-A6. B4b: sound kinds for U1 (C4 incl. review H1: oracle `thread-unknown-callee`, `thread-unknown-returned`, `call-unknown-ternary`, `escape-*`; fixtures `b4b/scripts/*unknown_callee*.f`, `b4b/modules/escape_export`; mutants A23, A28; and the pre-existing lambda-capture hole of section 13: oracle `lambda-capture-*`, 8 fixtures that exit 70 on B4a, mutant A32) and U2 (oracle cases of sections 2, 10, 11; mutants A2, A19-A22, A24, A26, A27, A29-A31); past 16 origins the kinds are anything, so such sites are Dynamic or refused (section 11). P1: the builder invariant and its mutant A34 (section 10.1). **Direct evidence:** the guard-off gate (`test-runtime2-guardoff`, shared references, so a stale Static fact shows as `compiler defect`, exit 70; section 10.2) on the final tree: 0 failures over the b4b suite (36 fixtures, the F1 shapes, 1,568 generated; 6,297 checks), 1,500 alias programs, 12,512 fuzz programs and the 2,029-script cross-check, each fixture in a temporary copy, the repository unchanged |
| Every mutation path is in the type model | C1 check in `b4b_types.py` against the sources; `static_assert` on the effect table; oracle cases per path (alias, inner, callee, closure, global, thread, reorder, dynamic receiver incl. L1 `union-callable-member`, dynamic key, session push/index/member/closure stores; a write that grows a binding a lambda captured, section 13); mutant A20 (one mutator dropped), A29 (session store paths), A32 (lambda readers) |
| (widened-group repair round, section 17) Invalidation | The 16f24f36 defect (a class opened by a union kept its precise readers; guard-off exit 70) fixed in B4b 22; the invalidation invariant checked directly as a fixed point on every oracle case and fixture (`verify_fixed_point`); 27 `widen` fixtures incl. A -> B -> C transitive merges, both orders, functions, lambdas, globals; mutant A35; audit of every opened-class shortcut (section 14). Re-verified on `0cd6d6ae`: 36 gates, generators, classification (0 changes against `16f24f36`; nothing newly admitted against B4a), run time identical (section 17) |
| Unknown values cannot reach unchecked dispatch | `kOpenSite`/`agg_unknown_`; C2 (A24), C4 (A23; A28: an inexact callee or any thread/async entry widens every function used as a value); too many known origins read as anything (opened classes, A31). Measured on the final tree: H1 reproducers refused (no exit 70); the four exit-70 fixtures of B4a are refused and the eight lambda-capture ones run checked (sections 6, 13); generators 38,185 programs, 0 exit 70, 0 defects (section 5b); guard-off gate `dispatch_defects` 0 |
| Session-persistent code cannot keep stale assumptions | Today: coherent recompilation and shape contracts (CP19, mutants 7/8), the guard for aliases; `session_effects` with the full reachability rule (review M1: oracle `session-push-exposed`, `-index-`, `-member-`, `-closure-exposed`, `session-late-store`, `session-store-after-call`; mutants A21 (8 cases), A29 (3 cases)); the reviewer's `rv_b4b_unit.cpp` 40 checks, 0 failures on the tip objects. After B5: **not yet** (U6, section 4): the guard-off gate shows it (`b4b/guardoff/u6_session_alias.in`, listed in `expected_u6.txt`, which B5 must empty; re-checked on the tip: guard-off exit 70 `unexpected operands (compiler defect): built-in trim`, production refused `reference-guard:inflow`) |
| The wrong-kind generators find no internal failures | On the final tree: `b4b_types.py` generator (1,568), `gen_alias.py` (1,500, guard-off), `gen1.py` pass a (20,274) and pass b (16,000), `gen_struct.py` (1,911): 0 exit 70, signals, tag leaks, internal text or defects; the same admissions and VM output as B4a and `a42035c1` (section 5b). P1 (a pre-existing codegen crash the review found) fixed in B4b 7 and 11 (section 10.1); a 390-program assignment-as-value probe set crashes 117 times on B4a and 0 times on the tip |
| Performance stays within the limits | **Widened-group round:** compile scaling gated by deterministic analysis-work counts against B4a (section 15: linear on every shape; the 19x and `nestb` pathologies of `a42035c1`/`e293105e` fail it); wall clock guidance 1.06x to 1.54x (section 17; 1.5x is the Linode objective); run time identical (section 17). Final repair round: Run time identical (section 7, re-measured on the final tree). Compile time (section 11, gate `z-compile-scaling`, best of 7 against B4a at 100, 400, 800, 1,600 helpers): `glob` 1.35x/1.31x/1.22x/1.01x, `many` 1.31x/1.34x/1.31x/1.38x, `nestb` 1.48x/1.49x/1.43x/1.38x (close to the bound, section 11), containers 1.22x; no analysis-limit refusal at any size (F1 fixed); before: `many` 19x and refused from 16 helpers, `nestb` 3.4x to 5.9x |

## 4. Unflagged method-site classes (the B4a section 9 prerequisite)

| Class | Receiver | Answer | Holds after B5 (guard removed)? |
| --- | --- | --- | --- |
| U1 | a binding's own value (local, parameter, global, captured cell, module binding) | (a) its kinds are the union of every write to the binding. Parameters: every tracked call; a function whose callers are not all known (a native's callback, any call or thread/async entry whose targets are not exact: review H1) has parameters of anything (C4). The c56c4093-era claim "parameters: every call" was false for `thread(f, ...)` with inexact `f` (exit 70 on the B4b tip `e293105e`; fixed in B4b 8). Captured cells: the claim "one slot" held, but a lambda walked before the slot's last growth was not walked again (exit 70 on B4a and `a42035c1`; fixed in B4b 15, section 13) | **Yes**: sharing changes contents, not which value a binding holds |
| U2 | an element or member read (`xs[i]`, `o.k`, `for` binding, element-returning methods) | (a) per-site contents (B4b) | **Yes**: stores through every alias reach the sites by construction |
| U3 | a struct field | (a) one slot per field (B3) | **Yes** |
| U4 | map/set/stack contents | (a) per site (D-NEXT-refsem) | **Yes** |
| U5 | a call result | (a) via U2 | **Yes** |
| U6 | a value of an earlier session statement (session binding) | (a) its recorded structure, kept by CP19's per-binding shape contracts and coherent recompilation; an alias store is refused by the guard | **No**: the contracts are per root binding, so after B5 an alias store in a later statement bypasses them. B5 prerequisite: per-site session contracts, or earlier values' contents read as anything (measured on this tree: the cp19 session suites, refsem and b4 sessions, safety and args-cmd lose admitted cases), or the precondition at this class |
| U7 | a transferred value (thread/async argument, mutex value, channel receive, awaited result) | (a) copies (D7): sender's sites, or unknown origin | **Yes**: transfers copy in both models |

## 5. Tests and evidence

Figures on the final repair round's gate tree (section 8) unless stated.

- **Widened-group repair round (B4b 22 to 24), on the final tree (section 17):** oracle 864 checks
  (the 442 below, each now also checked as a fixed point, plus the `widen` cases, the fixed point of
  every convergence shape and b4b script); `test-runtime2-b4b` 6,846 checks (adds the `widen` and
  `admission` steps and the work gate); `test-runtime2-b4b-work` (section 15).
- `make test-rt-b4b-types-unit` (`tests/rt/b4b_types_unit.cpp`, the primary oracle): 442 checks, 0
  failures (section 2 lists the paths; the fix round's H1, H2 and M1 cases: section 10; F1
  convergence, U1 lambda captures and the P1 builder cases: sections 11, 13, 10.1).
- `make test-runtime2-b4b` (`tests/runtime2/b4b_types.py`): 6,589 checks, 0 failures; 33 scripts
  (the 8 `lambda_capture_*` added), 4 module trees, 5 sessions (36 fixture programs: 12 run by the
  VM, equal to legacy; 24 refused before any effect); the F1 convergence step (4 shapes x 4 sizes);
  11
  `b4b/codegen` P1 fixtures (9 run by the VM, equal to legacy; 2 `refused_*`); the C1 check; the
  wrong-kind generator (8 holder paths x 4 original kinds x 7 stored kinds, map, struct, int,
  string, array, atomic and mutex, x 7 observations: 1,568 programs, 74 run by the VM); the
  container-heavy, `glob`, `many` and `nestb` scaling checks. Every program: never exit
  70, never an internal tag, `dispatch_defects` 0, refused before any effect or equal to legacy
  (D-009#5 allowed; legacy's shown handle tags are the approved B0/CP17/B4 difference).
- `make test-runtime2-b4b-mutants`: section 5a.
- `make test-runtime2-guardoff`: section 10.2.
- The reviewer's generators on the tip: section 5b.

### 5a. Mutants (each must fail the oracle or the suite)

**Widened-group repair round (gate `y-test-runtime2-b4b-mutants`, section 17): 17 caught**, A35
added; each mutant is also run through the work gate (B4b 23); the fixed-point check makes the
oracle catch more cases (A2 37, A19 18, A26 65, A27 40, A31 64, A32 24 failing cases; work gate
failures: A26 10, A30 9, A31 4, others 0).


Final repair round: `test-runtime2-b4b-mutants` on the final gate tree: **16 caught** (counts of that run; on `2e7cde13`
the first 12 rows had A2 28, A19 8 and A26 1 oracle cases and A26 4 suite checks: the oracle has
grown). Each mutant is also linked into a
guard-off CLI and run through the guard-off b4b suite and 600 alias programs (column "guard-off";
informational, the mutant is caught by the oracle or the suite).

| Mutant | File | Defect | Oracle cases | Suite checks | Guard-off |
| --- | --- | --- | --- | --- | --- |
| A2 | `infer_sites.cpp` | a store reaches only the binding stored through (the stale per-binding type) | 37 | 0 | failed |
| A19 | `infer_sites.cpp` | stores inside functions and lambdas miss the caller's sites (callee/closure path) | 18 | 0 | failed |
| A20 | `infer_sites.cpp`, `infer_collection.cpp` | one mutator (`reverse`) dropped from the list (C1) | 1 | 0 | passed |
| A21 | `infer_escape.cpp` | a call into an earlier session statement's code keeps the statement's site facts (C5) | 8 | 0 | passed |
| A22 | `infer_sites.cpp` | an inner part has no derived site and a site-less value is taken as fresh (nested alias) | 1 | 0 | passed |
| A23 | `infer_escape.cpp` | an escaping function keeps its known calls' parameter types (C4) | 9 | 9 | failed |
| A24 | `infer_sites.cpp` | a mutator on a receiver that is not exactly an array is not modelled (C2) | 2 | 0 | passed |
| A25 | `compiler_assign_expr.cpp` | P1: an assignment's constant value moved as a register | 0 | 8 (the codegen fixtures refused by the builder invariant) | failed |
| A26 | `infer_sites.cpp` | H2: no merge beyond 16 sites | 16 (`collapsed-sites` and the `converge-*` cases) | 5 (the scaling checks) | passed |
| A27 | `infer_sites.cpp` | H2: a grown site's readers are not re-walked | 1 (`reader-rewalk-late`) | 0 | passed |
| A28 | `infer_escape.cpp` | H1: an inexact callee widens no target | 6 | 9 | failed |
| A29 | `infer_sites.cpp` | M1: a push/index/member store into an earlier statement's value exposes nothing | 3 (`session-push/index/member-exposed`) | 0 | passed |
| A30 | `infer_walk.cpp` | F1: a push puts the class spelling in place of the binding (no fixed point) | 6 (`converge-*`) | 7 | failed |
| A31 | `infer_sites.cpp`, `infer_prune.cpp` | F1: past 16 sites the collapsed class keeps its precision | 15 (`converge-*` anything checks) | 0 | passed |
| A32 | `infer_bindings.cpp` | U1: a lambda keeps a captured binding's kinds read before it grew | 8 (`lambda-capture-*`) | 17 | failed |
| A34 | `codegen.cpp` | P1: `FunctionBuilder::emit`'s operand invariant removed | 4 (`p1-*` rejection cases) | 0 | passed |
| A35 | `infer_sites.cpp` | review of `16f24f36`: a class a union opens skips its earlier readers (the unconditional reset restored) | 26 (9 `widen` cases: readers not anything, not a fixed point) | 0 (guard on: refused `aliasing`) | failed (9 `widen` programs exit 70) |

B3's D11 (array mutators keep positional kinds outside sessions) now also keeps the sites'
positions for a script's non-append mutator (`infer_collection.cpp`): B4b's per-site drop had
subsumed the binding-level one, so the mutant survived until it followed the facts into the sites.

No separate transfer mutant: transfers have no invalidation path of their own (copies; the
callee's stores are the callee path, A19). A34 (final repair round) removes the builder invariant
itself (section 10.1).

### 5b. The reviewer's generators on the tip

`review-b4/repro2/gen1.py` and `gen_struct.py` (the reviewer's copies; only the path of
`builtin_list.inc`, identical in `0df0df6c`, `e293105e` and the tip, and the work directory
changed), the same parameters (pass a: `for`, `fnloop`; pass b: `NB=16000`, seed 7), run on the
final tree's `nift` and, for the comparison, on a B4a `0df0df6c` binary built for it
(byte-identical to the `--baseline` binary of the scaling gate) and the previous round's results
on `2e7cde13`/`a42035c1` (`scratchpad/hv/gen/`; jsonl of this round: `scratchpad/f1/gen/`).

| Generator | Programs | Run by the VM | Exit 70 / signal / tag leak / internal text / defects | Differences from legacy | Against B4a and `a42035c1` |
| --- | --- | --- | --- | --- | --- |
| `gen1.py` pass a | 20,274 | 1,297 | 0 / 0 / 0 / 0 / 0 | 131 | identical: 0 admission changes, 0 VM output changes, 0 new differences |
| `gen1.py` pass b | 16,000 | 955 | 0 / 0 / 0 / 0 / 0 | 138 | identical (same) |
| `gen_struct.py` | 1,911 | 867 | 0 / 0 / 0 / 0 / 0 | 213, all D-009#5 (the `return:` prefix) | identical (same) |

The 131 and 138 differences are the pre-existing ones of the earlier rounds (the same programs and
the same VM output on B4a).

## 6. Classification

Tooling of the earlier rounds (`scratchpad/b4/tools/classify_tree.py`, `classify_all.py` over
`tests/runtime2/cp25/inventory.collect_classify_all`, `invcmp.py`); `NIFT_VM=classify`; the
`tests/runtime2` corpus of the tip for every binary.

Re-run on the final repair round's tree (B4b 15 to 18; corpus of that tree, 2,029 scripts, for
every binary; the `a42035c1` binary is the one `make -q` reported current for its tree,
`scratchpad/b4b/inv/nift-p1`). **Against `a42035c1`: CP25-R 589 units, 0 changes (reasons lists
included); `tests/runtime2` 0 changes.** Against B4a the result is the previous round's, below,
unchanged. The 8 `lambda_capture_*` fixtures are compiled by all three binaries (B4a's dispatch
exits 70, the tip's is checked: section 13); the generated F1 shapes: section 11.

**B4a `0df0df6c` against the tip** (`2e7cde13`; the same on the final tree).

- CP25-R `classify-all` (589 units): **0 changes** of outcome, reason, line or column. One unit's
  reasons list (`vm-corpus/cp04/toplevel_alias.f`, refused `aliasing` in both) lists `aliasing` and
  `unresolved-name` once more each (the same as on `e293105e`).
- `tests/runtime2` classify (2,021 scripts then, 2,029 now): 6 changes.

  | Script | B4a | Tip | Why |
  | --- | --- | --- | --- |
  | `b4b/modules/escape_export/main.f` | compiled | refused `builtin-receiver:trim` (1:19) | H1 (module boundary): exit 70 on B4a and `e293105e` |
  | `b4b/scripts/call_unknown_callee.f` | compiled | refused `builtin-receiver:trim` (2:19) | H1 (inexact call target): exit 70 on B4a; already refused on `e293105e` (C4) |
  | `b4b/scripts/thread_unknown_callee.f` | compiled | refused `builtin-receiver:trim` (4:19) | H1 (thread entry): exit 70 on B4a and `e293105e` |
  | `b4b/scripts/thread_unknown_callee_returned.f` | compiled | refused `builtin-receiver:trim` (2:19) | H1 (returned function as thread entry): exit 70 on B4a and `e293105e` |
  | `b4b/scripts/push_alias_struct.f` | refused `reference-guard:inflow` | refused `type:member` | the alias's push of a map reaches `xs`'s site: `xs[1].a` is no longer a proven struct field |
  | `prevalidate/.../probes/nift-limits.f` | refused `compound-target` | refused `type:<` | a store into a mutex's value (unknown origin) makes every container anything |

**`e293105e` against the tip.** CP25-R: 0 changes (reasons lists included). `tests/runtime2`: 3
changes, `escape_export/main.f`, `thread_unknown_callee.f`, `thread_unknown_callee_returned.f`,
compiled (exit 70) to refused `builtin-receiver:trim`, as above.

**Admitted programs that become refused:** the four H1 fixtures above, each of which exits 70
(`unexpected operands (compiler defect)`) under `NIFT_VM=require` on B4a (and three of them on
`e293105e`) and exits 1 on legacy. No other admitted program becomes refused; no program becomes
admitted.

- `safety/expected.json`: `m_sortc_object_loop`, `m_sortc_object_mapget` stay refused, now `type:<`
  (the comparator passed to `sort` on an object escapes: C4) before the guard's reason
  (`test-runtime2-safety` on the tip: 33,512 programs, PASS).
- Sessions (the CP19 session suites, refsem, b4 and safety sessions): no change (the gates of
  section 8 on the tip).

## 7. Performance (local callgrind and compile_us; guidance only)

Run time (instructions per iteration, B4a against the B4b tip): identical, re-measured on the
final repair round's tree (callgrind, local).

| Workload | B4a | B4b |
| --- | --- | --- |
| B4a built-in set: `s.trim()`, `a.contains(2)`, `i.to_string()`, `o.has("a")`, Dynamic `contains` | 1074, 903, 1187, 870, 2844 | 1074, 903, 1187, 870, 2844 |
| `run.py --groups ops`: loop, add_local, call, array_index, array_push, object_member, string_concat, if_branch | 117, 188, 567, 436, 367, 763, 3315, 249 | 117, 188, 567, 436, 367, 763, 3313, 249 (string_concat within 0.1%) |
| `vm_compare.py --groups mutation`: array_set, array_push_pop, array_insert_pop, map_set, set_add, stack_push_pop | 381, 1430, 2446, 1496, 1363, 2190 | identical |

Compile time (`compile_us`, best of five, ms; the suites' scaling shapes and a container-heavy
shape: per index an array holding an object holding an array, read through a parameter, and a
fresh array pushed to and stored into, 4,000 sites at 1,000):

| Shape | B4a 250 | B4b 250 | B4a 1,000 | B4b 1,000 |
| --- | --- | --- | --- | --- |
| functions (`test-runtime2-b4`) | 5.7 | 5.9 | 24.2 | 26.4 |
| chain from `for` | 2.7 | 2.8 | 13.4 | 13.3 |
| chain from `while` | 4.5 | 3.0 | 13.3 | 13.6 |
| containers | 12.3 | 15.0 | 86.1 | 105.8 |

At most 1.23x B4a (containers), inside the 1.5x bound. The container shape grows about 7x for 4x
on both binaries (the existing walk, not B4b's); `test-runtime2-b4b` checks it stays within 12x.

The compile-time table above ran on B4b 3 (the frozen round); the final compile-time figures are
in section 11. The run-time rows were re-measured on the final tree (B4b 15 to 18): the fix
rounds change the inference, the assignment-as-value result (B4b 7) and build-time checks only.

## 8. Gates (local; result files `scratchpad/b4/gates/z-*.result`)

(The final repair round's gates, on `f246345b`; the widened-group repair round's, `y-*` on
`0cd6d6ae`: section 17.)

Run by `scratchpad/b4/gate2.sh` (as `gate.sh`, plus `tests/runtime2/repo_state.py` around each
gate: tracked, untracked and ignored files) on head `f246345b` (tree `d89708e1`; the code of
B4b 15 to 18 and this record's first draft), every gate's status clean (0 entries) before and
after and the repository state unchanged; `make -q nift` reported `nift` current. One gate
failed on that run and was fixed after it (`test-rt-refrep`, below); the commits after the
gate tree change `tests/rt/refrep_production.sh` (B4b 20) and this record only (B4b 21), and
`test-rt-refrep` was run again on B4b 20.

| Gate | Exit | Repository | Result |
| --- | --- | --- | --- |
| `test-rt-lint` | 0 | unchanged | PASS rt-lint: 0 problem(s); boundary 0 problems in 707 files |
| `test-rt-op-admission-unit` | 0 | unchanged | op-admission-unit: 4027 checks, 0 failures |
| `test-rt-b4b-types-unit` | 0 | unchanged | b4b-types-unit: 442 checks, 0 failures |
| `test-runtime2-b4b` | 0 | unchanged | PASS b4b-types: 6589 checks, 0 failure(s) |
| compile scaling (`b4_dispatch.compile_scaling`; `b4b_types.py --quick --baseline` B4a) | 0 | unchanged | PASS; the four shapes within 1.5x of B4a (section 11) |
| `test-runtime2-guardoff` | 0 | unchanged | PASS: b4b `--guardoff` 6,297 checks; gen_alias 1,500 (0 bad); fuzz 12,512; cross-check 2,029 scripts, 0 failures; repo-state 0 changes (section 10.2) |
| `test-runtime2-b4` | 0 | unchanged | PASS b4-dispatch: 12648 checks, 0 failure(s) |
| `test-runtime2-b3` | 0 | unchanged | PASS b3-structs: 468 checks, 0 failure(s) |
| `test-runtime2-safety` | 0 | unchanged | PASS runtime2 safety fuzz: 33512 programs (8512 exhaustive, 20000 sampled, 5000 session; seed 1; 1676 run twice) {"file:error-or-refusal": 22384, "fil |
| `test-cp19-session-cli` | 0 | unchanged | PASS retained-reader numeric guard and legacy channels |
| `test-cp19-functional` | 0 | unchanged | PASS retained-reader numeric guard and legacy channels |
| `test-runtime2-refsem` | 0 | unchanged | PASS refsem-a: 475 checks, 0 failure(s) |
| `test-runtime2-collections` | 0 | unchanged | PASS collections: 278 checks, 0 failure(s) |
| `vm-modules-unit` | 0 | unchanged | rt vm modules unit: 179 checks, 0 failures |
| `semantics-modules` (+ structs, `modules_d041 --legacy-today`) | 0 | unchanged | PASS modules-d041 (legacy-today): 22 fixtures, 0 failure(s) |
| `test-runtime2-packages` | 0 | unchanged | packages: 830 checks, 0 failed |
| `test-runtime2-args-cmd` | 0 | unchanged | PASS heap: cmd ends with the live heap of the string literal |
| `test-runtime2-cp22` | 0 | unchanged | PASS cp22 print-order evidence |
| `test-runtime2-cp23` | 0 | unchanged | PASS cp22 default: 87 case(s), 0 failure(s) |
| `test-runtime2-coro-module` | 0 | unchanged | PASS coro-module: 0 failure(s) |
| `test-runtime2-vm-default` | 0 | unchanged | PASS VM-default policy |
| `test-rt-refrep` | 2, then 0 on the tip after B4b 20 | unchanged | FAIL `unexpected NIFT_RT_REFERENCE_PROBE build rules` (pre-existing since B4b 12: the guard-off sanitizer rule); after the fix (B4b 20) PASS, run again on `f805e3d3` (result `z2-test-rt-refrep`) |
| `test-rt-mutation-protocol` | 0 | unchanged | mutation-protocol: 191 checks, 0 failures |
| `test-rt-value` | 0 | unchanged | native-budget: 105 checks, 0 failures |
| `vm-unit` | 0 | unchanged | rt vm unit: 40341 checks, 0 failures |
| `vm-globals-unit` | 0 | unchanged | rt vm globals unit: 23 checks, 0 failures |
| `vm-adversarial` | 0 | unchanged | PASS vm adversarial: 39612 programs, 0 non-approved differences, 196 approved {'D-001#4 recursion': 73, 'D-009#5 return prefix': 18, 'D-018c diagnosti |
| `test-rt-isolation` | 0 | unchanged | PASS isolation: production cannot reach the storage-level test helpers |
| `test-production-deps` | 0 | unchanged | PASS production-deps: 1508 production compile commands, no dependency in tests/ (-M, system headers included) |
| `test-rt-build` | 0 | unchanged | PASS (its builder mutants caught) |
| `test-runtime2-b4b-mutants` | 0 | unchanged | PASS b4b mutants: 16 caught |
| `test-runtime2-b4-mutants` | 0 | unchanged | PASS b4 mutants: 17 caught or covered |
| `test-runtime2-b3-mutants` | 0 | unchanged | PASS b3 mutants: 11 caught |
| `test-runtime2-safety-mutants` | 0 | unchanged | PASS safety mutants: 7 caught |
| `test-runtime2-collections-mutants` | 0 | unchanged | PASS collections mutants: 11 caught |
| `test-runtime2-refsem-mutants` | 0 | unchanged | PASS refsem mutants: 23 caught |

Not run locally (combined certification): full `make test`, `test-rt-vm-sanitize` with
`test-runtime2-guardoff-sanitize`, `test-build-reuse`, perf campaigns (the owner's rule: heavy
validation and benchmarks on Linode; the local compile-scaling figures are guidance).

## 9. Findings and carried items

- **Async lambda after `await`** (review L2): not a finding. A lambda returned by an async function
  and called after `await` runs on the VM where legacy reports `invalid lambda`: the approved CP22
  correction `correction:lambda-registry` (owner, round 7; cp22-coroutines.md 874 and 1238;
  `tests/runtime2/cp22/sem_lambda_*`). compatibility.md now has its row.
- **`atomic.store` text** (review L3), carried to the release audit: `a.store([1])` reports
  `store requires a signed 64-bit integer`, legacy `store: atomic<int> requires a signed 64-bit
  integer` (same position and exit status).
- **L1 (latent C2 hole)**, fixed in B4b 8: a union receiver whose object member of a mutator's name
  is callable took the member call before the mutator model; the compiler refuses such calls
  (`type:push`, `method:insert`), so it was unreachable. Oracle `union-callable-member`.
- **F1 (fixed in B4b 15, section 11):** the parameter-push shapes stopped at the walk budget from
  16 helpers on (`analysis-limit:kinds`, about 19x B4a). Root cause: a push replaced the binding by
  a type spelling its sites by class, which the next walk's parameter merge spelled again as made:
  the walks oscillated. Now monotone; past 16 origins the contents read as anything.
- **F2 (fixed in B4b 16, 17, section 12):** the guard-off cross-check wrote into fixture
  directories and three outputs were committed; it now runs every fixture in a temporary copy, the
  files are removed, and the gate fails if the repository changed (ignored files included).
- **`nift eval --json` internal tag** (owner ruling): a function's tag in `--json` output is a
  release blocker assigned to the release-blocker audit (b4-dispatch.md section 9); B4b does not
  touch the JSON conversion path.
- **B5 prerequisite (U6, section 4):** earlier session statements' values keep their recorded
  structure, which only the CP19 per-binding contracts and the guard keep true. Before the guard
  goes: per-site session contracts, or contents of earlier values read as anything (measured here:
  admitted session cases in cp19, refsem, b4, safety and args-cmd become refused), or the
  precondition at that class.
- **Widened-group repair round (section 14 to 17):** the review of `16f24f36` (REJECT) fixed and
  audited; the `glob` admission difference recorded (B5 decision point: runtime-kind checks for
  `trim` and the related string built-ins); the compile-scaling gate is deterministic. Review
  low items: L-a, `tests/runtime2/repo_state.py` leaves out any path ending in `.o`, `.d`, `.cfg`,
  `.obj`, `.pdb` or `.exe` anywhere in the tree (build products), so a test output with such a
  suffix would go unnoticed (accepted for its purpose, recorded); L-b, the section 11 claim is
  corrected (section 14). The `z-test-rt-refrep` exit 2 of the final repair round is explained in
  section 8 (a pre-existing rule check fixed in B4b 20; `y-test-rt-refrep` passes).
- The guard's own facts (`nested_aggregates`, `collection_arrays`/`_objects`) still come from the
  per-binding types; B5 deletes them with the guard.


## 10. Fix round (B4b 7 to 13) after the review rejection of `e293105e`

Review: `scratchpad/review-b4b/REVIEW.md` (H1, H2, M1 blocking; P1 pre-existing release blocker;
L1-L4). Commits: B4b 7 and 11 (P1, codegen), 8 (H1, H2, M1, L1 in the inference), 9 (the standing
guard-off gate, fixtures, mutants), 10 (site classes keep map/set/stack sites; reader re-walk
oracle cases), 12 (guard-off cross-check, ASan/UBSan variant, the owner's H1 categories, H2 at four
sizes), 13 and 14 (this record). Verification of this section: the hand-over agent on `2e7cde13`.

### 10.1 P1: root cause and codegen audit

**Root cause.** The VM's operands are typed by position: an RK operand (bit 15 set: constant
`K[x & 0x7fff]`, else register) or a register-only operand (`R[x]`). `expr_any` returns an RK
operand (a constant for a literal value). `UnitCompiler::assign`'s index path used that operand as
the assignment expression's result with `Move dst, value`, and `Move` reads `R[b]`: with a constant
operand the VM indexed the register file with `0x8000 + k`, outside the frame (SIGSEGV, exit 139).
Present since before B0. The invariant that was violated: *a register-only operand never carries the
constant bit*; nothing enforced it.

**Fix.** (B4b 7) `assignment_value` loads a constant operand with `LoadK` and moves only a register.
(B4b 11) `FunctionBuilder::emit` enforces the invariant for the destination `a` of every opcode
that writes `R[a]` (all but the opcodes whose `a` is an RK operand, a jump target, a global, an
upvalue or a handler index: `writes_register_a`) and for `Move`'s source; a violation refuses the
unit before it runs (`limit:register-operand-constant`). Classify over `tests/runtime2`: no fixture
hits it.

**Audit (read-only, hand-over agent).** RK operands have two producers: `expr_any` (9 call sites)
and `FunctionBuilder::constant` (37). Each consumer checked against `opcodes.h` and the VM:

| Form | Producer, consumer | Result |
| --- | --- | --- |
| index assignment as a value `print(xs[0] = 5)` | `expr_any` -> `SetIndex` c (RK) and the result | fixed (`assignment_value`); fixture `index_assign_int_expr.f` |
| index assignment key `xs[k] = v` | `expr_any` -> `SetIndex` b (RK) | correct |
| name assignment as a value `print(x = 5)` | `expr_to` (a register) | correct; `ident_assign_const.f` |
| compound assignment `x += 4` (and as a value) | `expr_any` -> `AddAssign` b, `Sub`/`Mul`/`Div`/`Mod` c (RK) | correct; as a value refused (`refused_compound_assign_expr.f`) |
| atomic compound/inc-dec | `expr_any` -> `AtomicUpdate` c (RK) | correct |
| struct field compound `c.a += v` | `expr_any` -> `RK[c]` arithmetic | correct |
| index read `xs[k]` | `expr_any` -> `GetIndex` c (RK) | correct |
| object literal `{"k": 5}` | `expr_any` -> `ObjectSet` c (the VM reads `RK(c)`; the opcode comment says `R[c]`, stale) | correct |
| binary and fused compare-jump | `expr_any` -> RK b/c, `JmpNot*` a/b (RK) | correct |
| `xs.push(5)` statement | `expr_any` -> `Push` b (RK) | correct |
| member/object element store as a value | refused before any effect | `refused_object_element_store_expr.f` |
| nested assignment `xs[0] = ys[0] = 5`, `x = y = 7` | inner result via `assignment_value` | `nested_assign_expr.f` |
| assignment as a call argument (user function, native) | `assignment_value` into the argument register | `assign_as_call_argument.f` |
| literal kinds (int, negative, double, bool, null, string, array, object) and register values | `assignment_value` (LoadK / Move) | `array_element_store_literals.f`, `index_assign_nested_expr.f`, `index_assign_register.f` |
| inside functions, returned, global array from a function | as above | `index_assign_string_fn.f`, `index_assign_global_fn.f` |
| `fb_->constant` consumers (`LoadK`, `CellStore`/`SetUpvalue`/`CheckAssign` text type c, `BindType` c, `StructAdd` c, `AddAssign` b in interpolation) | RK or `LoadK` operands | correct |

A generated probe set (30 assignment-as-value contexts, among them `if`/`while` conditions,
ternary, interpolation, unary operators, method calls on the result, `push(xs[0] = v)`, closures,
global functions, struct fields, compound stores, x 13 value kinds: 390 programs) on the tip:
0 crashes, 0 exit 70, 0 internal text; 211 run on the VM, all equal to legacy, 179 refused before any
effect. The same set on B4a: 117 SIGSEGV over 13 contexts. **No unfixed sibling form that crashes
was found.** The builder invariant does not cover register-only `b`/`c` operands of other opcodes
(`Not`/`Neg` b, `GetIndex` b, `CellStore` b, `CallValue` b, ...): no producer feeds them an RK
operand today (the audit above), so a slip there would not be caught by the invariant.

**Permanent fixtures:** the 11 `tests/runtime2/b4b/codegen/*.f` (run by `test-runtime2-b4b`: 9 on
the VM equal to legacy, 2 refused before any effect). **Mutant:** A25 (the `Move` restored at the
assignment site): caught by 8 failing suite checks (the fixtures are refused by the builder
invariant instead of running); guard-off build failed.

**The invariant's own mutant (final repair round, B4b 18).** The oracle builds invalid
instructions directly (`builder_case` in `b4b_types_unit.cpp`): `Move` with a constant-flagged
source, and `LoadK`, `Add` and `GetIndex` with a constant-flagged destination; `emit` must set
`register-operand-constant` when the instruction is built, before any proto exists (the compiler
then refuses the unit, `limit:register-operand-constant`, `UnitCompiler::builder_ok`). Controls:
register operands, `LoadK`'s constant `b`, `Add`'s RK `c`, and `JmpNotLt`'s RK `a` are accepted.
Mutant **A34** removes the check from `FunctionBuilder::emit`: the 4 rejection cases fail.

**Bounded audit of the other register-only operands (final repair round, read-only).** A constant
operand exists only as the result of `FunctionBuilder::constant` (37 call sites) or `expr_any`
(9 call sites, which returns `constant`'s operand for a literal); every other operand is a
register from `alloc` (`top_ < kMaxRegisters = 0x7fff`, so the constant bit `0x8000` cannot be
set), a binding's register from `Scopes` (allocated the same way), or a count, an atom, a proto,
global, upvalue or struct-type index (not a register operand). So a register-only operand can
carry the bit only if one of those 46 results reaches it:

| Register-only operands (`opcodes.h`) | What guarantees no constant reaches them |
| --- | --- |
| `a` of every opcode writing `R[a]` (all but `writes_register_a`'s exclusions), `Move` b | the builder invariant (B4b 11), now with its mutant A34 |
| `b` of `AddI`, `Not`, `Truth`, `Neg`, `Pos`, `BindType`, `GetIndex`, `GetMember`, `Length`, `FieldGet`, `StructIs`, `ArrayElem`(+c), `ArrayElemValue`(+c), `ObjectKeys`, `ObjectLookup`(+c), `ObjectLookupValue`(+c), `CellLoad`, `CellAnchor`, `CallValue`, `Await`, `FutureOp`, `ErrorJson`; `b` and `c` of `NewCell`; `b` of `NewArray`, `ErrorNew` (a register range) | the producer is `expr_to`/`reg_any`/`temp`/`alloc` (a register) at the emit sites read; no `expr_any` or `constant` result is passed: of the 9 `expr_any` results, 8 go to RK slots (`SetIndex` b/c, `AddAssign` b and `Sub`..`Mod` c, `AtomicUpdate` c, `GetIndex` c, binary `b`/`c`, `JmpNot*` a/b, `Push` b, struct-field arithmetic) and 1 to `ObjectSet` c, which the VM reads as `RK(c)` (`vm_report.cpp`; the opcode comment's `R[c]` is stale); the assignment value goes through `assignment_value` (`LoadK`, B4b 7) |
| `a` read as a register by `JmpFalse`, `JmpTrue`, `JmpNotNull`, `Return`, `Throw`, `TopReturn`, `Push`, `SetIndex`, `FieldSet`, `IterCheck`, `JmpIncLt` (and its `b`), `Call`/`CallNative`/`Spawn`/`Sleep`/`Closure` frame bases | not excluded by `writes_register_a`, so the builder invariant covers them too (a constant `a` is refused) |
| `b` of `SetGlobal`, `CellStore`, `SetUpvalue`, `CheckType` (a and b) | register producers at the emit sites read (`SetGlobal` takes a binding or `temp` register, also for the `LoadNull` of a global take; cell and upvalue stores take the value's register; text types are RK `c`) |
| the 37 `constant` results | each goes to `LoadK` b (some masked with `& 0x7fff`), an RK text-type or field-type operand (`BindType`/`CellStore`/`SetUpvalue`/`CheckAssign` c, `CheckField` a/b, `StructAdd` c; most of them the `-1` "untyped" text type), a compare-jump RK operand (`JmpNotEq` b in `compiler_session_native.cpp`), or an `expr_any` result above |

Result: no reachable defect found; the operands not covered by the invariant (`b`/`c` rows above)
are guaranteed by their producers only, as section 10.1 already recorded; widening the invariant
to them would need a per-opcode operand table (a redesign of the builder's checks), not done.

### 10.2 The standing guard-off gate `test-runtime2-guardoff`

**Probe binary.** `.build/b4b-guardoff/nift-guardoff`: the production CLI objects with
`src/rt/compiler.cpp`, `compiler_rules.cpp` and `compiler_collection_sort.cpp` replaced by their
`-DNIFT_RT_REFERENCE_PROBE` objects (`RT_REFREP_PROBE_OBJECTS`, the B2 reference-probe
configuration: no Phase A reference guard, no `aliasing` location-alias refusal, so the VM runs
shared references and a stale Static fact meets a foreign kind as `unexpected operands (compiler
defect)`, exit 70). Guard-on gates stay mandatory; this gate is in addition.

**Programs (counts on the final repair round's gate tree; `2e7cde13` in brackets):**

| Step | Programs | Result |
| --- | --- | --- |
| `b4b_types.py --guardoff` | the b4b fixtures (17 run by the VM, 19 refused; [9, 19]), the sessions incl. `b4b/guardoff/*.in`, the F1 convergence shapes, the 1,568-program generator (259 run by the VM) | 6,297 checks, 0 failures [6,203] |
| `gen_alias.py --sample 1500 --seed 1` | 1,500 transitive-alias programs | 509 run by the VM, 0 bad |
| `safety/fuzz.py --seed 1 --sample 3000 --session-sample 1000` | 12,512 (8,512 exhaustive, 3,000 sampled, 1,000 session; 626 run twice) | PASS |
| `b4b/guardoff.py` (cross-check) | every `.f` under `tests/runtime2` (module directories excluded), each in a temporary copy (section 12): 2,029 scripts [2,021] | 1,074 admitted by both builds (2 approved changes) [1,066], 0 failures |
| `repo_state.py` | the repository before and after the steps (and its self-test) | unchanged |

**Failure conditions.** In the cross-check, per script on the guard-off build (`NIFT_VM=require`,
`timeout 30`, a temporary copy of its directory, stats on): exit 70 (unless approved `exit70`); a timeout (124,
unless approved `timeout`); a signal or crash (exit > 124, 125-127, or negative); leaked internal
text or tags (`compiler defect`, `internal failure`, `unexpected operands`, `cell reached`,
`\x1fnift:`, `\u001fnift:`; the approved memory-budget stop excepted); a nonzero
`dispatch_defects` counter; nondeterminism (the first 300 scripts run twice must agree in stdout,
stderr and exit, unless approved `output`); output different from the guard-on build where both
builds admit the program (unless approved `output`). In `b4b_types.py --guardoff`: exit 70, internal
text, a tag, `dispatch_defects`, a refusal after an effect; the U6 expectations of
`expected_u6.txt` must still show (B5 empties the list). `gen_alias.py`: internal failures and
defects; `fuzz.py`: its own checks (determinism included).

**Approved exceptions** (`tests/runtime2/b4b/guardoff/approved_changes.txt`):
`cp22/vm-only/v_sleep_ties.f` (output: equal sleeps, wake order unspecified),
`cp22/vm-only/v_fifo_order.f` (output: parallel print order), `rt-diff-corpus/scripts/home-tmp/main.f`
(output: prints its temporary directory), `memory/c1_push.f` (exit70: the heap-budget stop, the same
on the guard-on build), `rt-diff-selftest-corpus/timeout/loop/main.f` (timeout: deliberate), `cp22/vm-only/v_thread_order.f`
(output: parallel threads; final repair round, section 12).
Expected U6 failure: `u6_session_alias.in` (`unexpected operands (compiler defect)`).

**ASan/UBSan variant.** `make test-runtime2-guardoff-sanitize`
(`.build/b4b-guardoff/nift-guardoff-sanitize`: the sanitizer CLI objects with the probe objects
built with `SANITIZER_FLAGS`; `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`,
`UBSAN_OPTIONS=halt_on_error=1`): `b4b_types.py --guardoff --quick`, `gen_alias.py --sample 400`,
and the cross-check over `b4 b4b b3 refsem` without the twice-run. A prerequisite of
`test-rt-vm-sanitize`. **Not run locally** (not built: disk and time); it runs in the combined
Linode certification campaign.

Test hygiene: fixed in the final repair round (section 12).

### 10.3 H1, H2, M1 against the acceptance criteria

**H1** (an unknown or inexact call target widens every candidate's parameters; escaping functions
keep no narrow assumptions). Implementation: `exact_callee`/`thread_target` in `call_callables` and
`thread_call` call `untracked_call()`, which widens every function used as a value (and, through
`untracked_seen_`, one first used as a value later). Per category:

| Category | Oracle case | Fixture |
| --- | --- | --- |
| thread/async targets | `thread-unknown-callee`, `thread-unknown-returned`, `escape-async-target` | `b4b/scripts/thread_unknown_callee.f`, `thread_unknown_callee_returned.f` |
| callbacks | `escape-native`, `escape-callback-native` | - |
| returned functions | `escape-returned`, `call-unknown-ternary` | `b4b/scripts/call_unknown_callee.f` |
| stored functions | `escape-stored` | - |
| closures | `escape-closure`, `escape-closure-capture` | - |
| module boundaries | - | `b4b/modules/escape_export` (exit 70 on B4a and `e293105e`) |

Mutants: A28 (6 cases, 9 suite checks), A23 (9 cases, 9 checks). The review's
`repro/H1_thread_unknown_callee_exit70.f` and `_b.f` on the tip: refused before any effect in default
mode and under `NIFT_VM=require` (exit 1; legacy exit 1); exit 70 on B4a and `e293105e`.

**H2** (more than 16 sites widen; only readers of a grown site re-walked; compile within about 1.5x
B4a at 100, 400, 800, 1,600; growth removed). Implementation: site classes (union-find), a value
with more than 16 classes merged into one collapsed class (`kMaxSites`); `site_changed` re-walks the
class's `readers_`. Evidence: oracle `collapsed-sites`, `collapsed-sites-read`, `reader-rewalk`,
`-2`, `-lambda`, `-late`, `-main`; mutants A26 (also the `glob` scaling check), A27. Measured,
`compile_us` (gate `fx-compile-scaling`, best of five, against the B4a binary):

| Shape | 100 | 400 | 800 | 1,600 |
| --- | --- | --- | --- | --- |
| `glob` B4a / tip (ms) | 1.51 / 2.01 | 5.73 / 7.40 | 12.6 / 15.9 | 31.1 / 36.7 |
| ratio | 1.32x | 1.29x | 1.26x | 1.18x |
| `e293105e` (ratio, `ctime.sh` median of 9) | 1.68x | 3.19x | 7.3x | 22x |

Containers shape: 1.32x at 250, 1.29x at 1,000. Review reproducers `H2_compile_glob100.f`,
`H2_compile_glob1600.f` (`ctime.sh`, median of 9): 1.25x and 1.26x on the tip. The ratio does not
rise with size: growth is removed for these shapes. The `many` and `nestb` shapes (F1) and the
final figures for all four: section 11 (`glob` is now refused, `builtin-receiver:trim`, past 16
origins; its compile time is measured all the same).

**M1** (a later call into earlier session code opens every reachable container: push, index
stores, member stores, captured cells, containers stored later into exposed ones). Implementation:
`session_effects`/`expose` in `infer_escape.cpp` and `site_store` (flow-insensitive). Oracle:
`session-push-exposed`, `session-index-exposed`, `session-member-exposed`, `session-closure-exposed`
(captured cell), `session-late-store` (stored later into an exposed container),
`session-store-after-call`, with controls `session-exposed`, `session-earlier-call`. Mutants: A29
(3 cases), A21 (8 cases). The reviewer's `repro/rv_b4b_unit.cpp`, built in a scratch location
against the tip's unit objects: **40 checks, 0 failures** (6 failures on `e293105e`). Guard-off
fixture `b4b/guardoff/m1_push_exposed.in`. U6 after B5 stays the recorded B5 prerequisite (section
4); the reviewer's U6 probe on the tip: guard-off exit 70 (the recorded expectation), production
refused `reference-guard:inflow`.

### 10.4 The review's other reproducers on the tip

`P1_index_assign_expr_segv.f` and `_fn.f`: exit 0, `5` and `k`, equal to legacy, in default mode
and under `NIFT_VM=require`. `L1_union_callable_member.f`: refused `aliasing` before any effect
(legacy 5). `L2_async_lambda_after_await.f`: VM `a`, `end`, exit 0; legacy `invalid lambda`, exit 1
(the approved CP22 correction, section 9).

## 11. F1: type-analysis convergence (final repair round, B4b 15)

**Behaviour before.** `review-b4b/perf/many<N>.f` (N helpers `fn(kN(r)) { r.push([<a literal of
kind N mod 5>, " s "]) ... }`, each called once with one shared `acc`): refused
`analysis-limit:kinds` from exactly 16 helpers on, at about 19x B4a's compile time; `nestb<N>` (the
pushed array passed in as a parameter too) compiled, at 3.4x B4a at 100 rising to 5.9x at 800.

**Root cause** (found with a walk trace before any change). Not the widening itself and not a
reset of the per-site union or flags: `add_element`, the binding update of a `push` statement
(`infer_walk.cpp`), *replaced* the receiver binding by its new type instead of joining it. The
new type's element spells the pushed container's sites by class (`canonical`, since B4b 8); the
binding is merged again at the start of every walk from the parameter (`params_`, a union of the
arguments, which spell the sites as they were made). With at most 16 sites every class is a
single site and both spellings agree. From the 17th site on one element union (16 helpers plus
`acc`'s own inner literal: hence exactly 16 helpers) the classes merge, the spellings differ, and
each walk of each helper does: the parameter merge grows the binding (`[5]` to `[5, 282]` in the
trace of `many16`), the push shrinks it back to the class spelling (`[5]`), `same()` reports a
change, the unit is enqueued again. No site store or merge happens after the first pass: the
walks oscillate on the spelling alone until the budget (64 walks per unit) refuses the program.
Readers being re-queued on every merge, and the collapsed classes, were secondary costs (below),
not the cause of the non-termination.

**Fix (monotone).**

- `add_element` joins the new type with the binding (`unite(*var, updated)`): a binding only grows.
  Mutant **A30** restores the replacement: the oracle's convergence cases fail (6), and the b4b
  suite sees `analysis-limit:kinds` for `many` at 16, 17 and 100 helpers and in its scaling check.
- Past 16 classes the collapsed class is **opened** (`classes()`, the approved design note's
  "more than 16 sites ... contents are anything"): its contents read as anything from then on, so
  a method on an element is Dynamic under the B4a precondition or refused where Dynamic is not
  valid; never Static. `opened_` only grows, and every site merged into an opened class later
  reads anything too (`unite_sites` propagates the flag). **Corrected in B4b 22 (section 14):** the
  original claims here, "discarded precision never comes back" and "growth of an opened class
  changes no read, so it re-walks nobody", held for later reads only. A reader that read a class
  while it was precise kept its facts when a *union* opened the class: `unite_sites` cleared the
  root's change flag also for a root opened by that union (the independent review's H1 of
  `16f24f36`, exit 70 with the guard off). Now every reader that read a class before it opened, by
  any path, is walked again at the opening; only growth of a class opened *before* it changes no
  read and re-walks nobody (`site_store`, `unite_sites`).
  Mutant **A31** keeps the collapsed class's precision (no opening, no pruning): 15 oracle cases
  fail (the "reads anything" checks).
- The parts of an opened class are pruned to anything (`infer_prune.cpp`: `prune_part` in
  `materialize` and for stored contents), which is exactly what their own read gives; types stay
  small. Only opened classes are pruned (a program without one is analysed as before).

Analysis imprecision and unsupported operations stay separate: the widening only changes kinds to
"anything"; whether a method on "anything" is Dynamic or refused is B4a's unchanged admission
(`op-admission-unit`), and no refusal reason of the language level is touched.

**Cost (no change to results).** Readers lists join smaller-into-larger and are vectors with a
membership bitmap (`UnitSet`; the root is the smaller site id, and helpers are walked in
decreasing order, so the collapsed class's list was copied at every merge); `unite` returns its
first operand when it already covers the second (`covers`, no allocation); `materialize` keeps its
result per type object while no site fact changes (`site_version_`; the readers of the classes
read are still recorded per unit); a binding that grows at its declaration, before the walk read
it, does not walk the unit again (the second walk was for values read before they grew;
`slot_read_`; other units that read the binding are enqueued as before: functions for a global,
lambdas for a capture, section 13).

**Measurements** (superseded as a gate by the deterministic work counts of section 15; kept as
recorded) (`compile_us`, best of 7, the tip and B4a `0df0df6c` interleaved; local,
guidance only; `test-runtime2-b4b` with `--baseline`, gate `z-compile-scaling`; the guard refuses
`many` and `nestb` after the analysis on both binaries, so the times include the analysis):

| Shape | 100 | 400 | 800 | 1,600 |
| --- | --- | --- | --- | --- |
| `glob` tip / B4a (ms) | 1.71 / 1.27 | 6.50 / 4.95 | 14.0 / 11.5 | 28.4 / 28.1 |
| ratio | 1.35x | 1.31x | 1.22x | 1.01x |
| `many` tip / B4a (ms) | 2.19 / 1.67 | 8.60 / 6.40 | 18.1 / 13.9 | 41.0 / 29.7 |
| ratio (before: 19x, refused from 16 helpers) | 1.31x | 1.34x | 1.31x | 1.38x |
| `nestb` tip / B4a (ms) | 3.21 / 2.17 | 13.4 / 8.97 | 34.4 / 24.0 | 80.2 / 58.2 |
| ratio (before: 3.4x at 100, 5.9x at 800) | 1.48x | 1.49x | 1.43x | 1.38x |
| `containers` (250, 1,000) | 1.22x | 1.22x | | |

All within 1.5x, and the ratio does not rise with the size: the growth is B4a's own (100 to 400
helpers: 3.8x to 4.2x the time on the tip, 3.9x to 4.1x on B4a; each later doubling 2.0x to 2.6x
on the tip, 2.0x to 2.7x on B4a; the slightly superlinear part is in both binaries, e.g. name
lookup over main's 1,600 bindings). **`nestb` is close to the
bound** (1.43x to 1.49x on this run; repeated local runs during development gave 1.37x to 1.67x
with the machine loaded): the gate's 1.5x check may fail under load, which the zero-flakiness
policy forbids. The remaining cost is spread over the per-read site work (`materialize`,
classes), not a single term; reducing it further needs a cheaper representation of materialized
reads, not done in this round. This is recorded for the reviewer, not hidden by a looser bound.

Walks are linear (`many` and `nestb`: 2 walks per helper and 2 or 3 of main at every size, from
3 per helper and 3 of main before the declaration rule; `glob`: 2 per helper). Instruction counts
(callgrind, compile only) give the same picture with less noise: `many` 1.20x to 1.37x, `nestb`
1.28x to 1.46x, `glob` 1.12x to 1.36x B4a at 100 to 800.

**Tests.** Oracle (`b4b_types_unit.cpp`): `converge-many`, `converge-nestb`, `converge-glob` at
8, 14, 15, 16, 17, 18, 24, 40 and 100 helpers: converged within `4 x units + 16` walks, the
observed element's kinds monotone in the size, and anything from 17 on. Suite
(`b4b_types.py convergence`): `many`, `nestb`, `glob` and `globp` (`glob` observed by `print`) at
15, 16, 17 and 100 helpers: refused before any effect or equal to legacy, never an analysis limit;
the same step runs under `--guardoff` (`test-runtime2-guardoff`), where `nestb` and `globp` run on
the guard-off VM. The compile-scaling check now covers `glob`, `many` and `nestb` at 100, 400, 800
and 1,600 (and `containers`): four times the helpers within six times the time, within 1.5x of
B4a at every size, the ratio not rising by more than 10% from size to size; it now measures the
compile time whatever the outcome (a run stopped by an analysis limit fails it). (B4b 23: these wall-clock bounds are no longer checked; the gate is the work count, section 15.)

**Classification changes (old against new).** The F1 shapes are generated, not corpus programs.

| Program | B4a | `a42035c1` | Tip | Why |
| --- | --- | --- | --- | --- |
| `glob` (16 helpers and more), `acc[1][1].trim()` | admitted (Static `trim` on the stale per-binding tuple) | admitted (Static `trim`, the collapsed class's union) | refused `builtin-receiver:trim` | the element union has more than 16 origins: anything; `trim` on anything is not admitted (B4a admission, unchanged) |
| `many` (16 helpers and more) | refused `reference-guard:argument` | refused `analysis-limit:kinds` | refused `builtin-receiver:trim` | the analysis converges; the same widening |
| `many` (up to 15), `nestb` (all sizes) | refused (guard) | refused (guard) | refused (guard) | unchanged |
| `globp` (any size) | admitted | admitted | admitted | `print` of anything is admitted; output equal to legacy |

On `tests/runtime2` (2,029 scripts) and CP25-R (589 units) no program changes outcome against
`a42035c1` (section 6). The `glob` change is the cost of the coordinator's rule for this round
("analysis imprecision widens to anything"): a program whose one array receives more than 16
distinct allocation sites and then calls a built-in that is not admitted on Dynamic receivers on
an element is refused instead of run. The previous round's collapsed class kept the union of the
contents, also sound, and admitted it; the choice is recorded here for the reviewer.

## 12. F2: test isolation (B4b 16, 17)

- `tests/runtime2/b4b/guardoff.py` runs every fixture in a fresh copy of its directory (of its
  parent when the script names `../`) made at one temporary path for every run of it, so the
  guard-on and guard-off runs see the same absolute paths and nothing is written into the source
  tree. The other steps of `test-runtime2-guardoff` (`b4b_types.py --guardoff`, `gen_alias.py`,
  `safety/fuzz.py`) already wrote their programs into temporary directories.
- The three files B4b 12 committed are removed (B4b 16, a normal deletion commit).
- **Permanent check:** `test-runtime2-guardoff` (and its sanitizer variant) saves the
  repository's state before its steps and fails if it changed after
  (`tests/runtime2/repo_state.py`: every path of `git status --porcelain --ignored=matching
  --untracked-files=all`, so untracked and ignored outputs are named too, and the hash of `git
  diff HEAD`; build products left out; a self-test with a scratch repository: a tracked change, a
  further change to a changed file, an untracked and an ignored file are caught, a build product
  and the restored tree are not). Python bytecode is not written by the gate.
- **The CP16 `fs_symlink` interference** (evidence): with the committed regular file
  `link.txt` in the probe directory, `capture.py --nift nift --check --only fs_symlink` (which
  copies the probe directory, then runs `setup.sh`) printed `ln: failed to create symbolic link
  'link.txt': File exists` on each of its two runs, so the probe ran with a regular file where its
  symlink belongs. After the removal `setup.sh` runs without an error. The probe's pre-existing
  expected-output difference remains, the same with and without the file: `expected/fs_symlink/stdout`
  has `true` first, the run prints it after the `cat` lines (`DIFF fs_symlink: stdout`); not
  touched here (no Makefile gate runs this capture).
- One timing fixture joined the cross-check's approved output differences:
  `cp22/vm-only/v_thread_order.f` (two threads print in parallel and the first thread's final
  `status()` is timing; it differed once between the builds under load, and varies run to run on
  both). It is still checked for exit 70, internal text and dispatch defects.

## 13. U1: a lambda's captured binding growing after its walk (pre-existing; found in this round)

While checking the walk-count rule of section 11, a probe showed a hole in the U1 claim
(section 4) present on B4a `0df0df6c`, `e293105e` and `a42035c1`: a lambda unit is walked before
a later write grows the binding it captures (or a global it reads); nothing walked the lambda
again (`merge_local` enqueued the functions for a global, never the lambdas), so its kinds stayed
those of the earlier write. Example: `xs := [" s "]`, `g := () => { return xs[0].trim() }`,
`xs = q()` where `q()` returns `[1]` through a call chain: B4a admits a Static `trim` and exits 70
(`unexpected operands (compiler defect): built-in trim`) in default and strict mode; legacy exits
1 (`trim: unsupported for this value`).

Fix (`infer_bindings.cpp`): the inference records which lambda units read each binding slot
(`find` in a lambda's walk) and enqueues them when the slot grows (`merge_local`, and the push
statement's binding update). Fixtures `b4b/scripts/lambda_capture_*.f` (8: top-level assignment,
a function assigning the global, a function's local and parameter, a nested lambda, another
lambda assigning, an object member, a collection callback): exit 70 on B4a each; on the tip the
VM runs them with the checked dispatch, equal to legacy (D-009#5 allowed). Oracle cases
`lambda-capture-*` (8). Mutant **A32** (lambda readers dropped): 8 oracle cases and the 8 fixtures
fail. Classification of `tests/runtime2` and CP25-R does not change (these programs were and are
compiled; what changes is the dispatch the compiler emits). Struct fields read in a lambda are not
affected (a field initialised with an array has no site: its contents are anything).

## 14. Widened-group invalidation (B4b 22; independent review of `16f24f36`: REJECT)

The independent re-review of `16f24f36` (report and reproducers kept in
`scratchpad/review-b4b-final/`: `REVIEW.md`, `repro/H1new_opened_union_stale_static_exit70.f`,
`p/O1`-`O6`) rejected the candidate: on the guard-off build the reviewer's 30-line program exits
70 (`NIFT_VM internal failure: unexpected operands (compiler defect): built-in trim`), with `trim`
compiled Static. The guard refuses it (`aliasing`), which masks it in production until B5. The
rejection and its evidence stand; this section is the repair.

**Root cause (confirmed with a traced copy of the inference before the fix).** `unite_sites` makes
the smaller site id the root. When a precise class `a` (the reviewer's global `a := [" x "]`, site
2) is united with an already opened class `b` (the 19-literal class of `mix`, site 11), `a`
inherits the opened flag and `grew` is set; then the B4b 15 line `if (opened_.count(a)) grew =
false;` cleared it, because `a` was opened *now*. So `site_changed(a)` never ran and the readers
that had read `a` while it was precise (`use()`) were not walked again; `classes()` does not make up
for it (`opened_.insert(r)` is already false), and later stores into the opened class skip
`site_changed` by design. The trace on the reviewer's program: walks main, use, mix, getA, main,
mix; in mix's second walk `unite a=2 b=11 grew=1 a_was_open=0 b_was_open=1`, then no
`site_changed` and no walk of `use`. Confirmed on 16f24f36's binaries: guard-off exit 70,
guard on refused `aliasing`.

**Fix** (`infer_sites.cpp`, `unite_sites`): whether each side was opened is taken *before* the
union (`a_was_open`, `b_was_open`); only a class opened before it skips its readers (`if
(a_was_open) grew = false`); the `b` side keeps its rule, now spelled `b_was_open` (unchanged in
effect: `b`'s flag is not changed by the union). The recursive union of the derived inner sites
applies the same rule. Mutant **A35** restores the unconditional reset.

**The invariant and why the fixed point is sound and monotone.** A unit's last walk must have seen
the final value of everything it read. Reads of a site class depend on its contents and on its
flags `opened` and `nonpositional` (an opened class reads as anything). Each change of either
re-walks the class's readers, at the moment of the change:
- contents grow (`site_store`, `unite_sites`): `site_changed`, unless the class was opened before
  (its readers read anything since then, whatever the contents);
- a class opens: in `classes()` (past 16 classes, `site_changed`), in `session_open` (every unit,
  `site_grew`), and through a union (`unite_sites`, now `site_changed(a)` for the root, `b_grew`
  for the absorbed side's readers);
- a union moves `b`'s readers to `a`'s list and re-walks them if `a` holds more than `b` did or has
  a read-changing flag `b` lacked.
So every reader registered before an opening is re-walked by the opening, and every reader
registered after it reads anything; skipping later growth of an opened class loses nothing. The
lattice is unchanged (the flags and contents only grow), so the fixed point is still reached
monotonically; the fix only adds walks, at most one per reader per opening.

**The check of the invariant itself** (permanent, B4b 22): `Inference::verify_fixed_point()`
(`infer_fixpoint.cpp`, used by tests only) walks every unit once more after convergence and records
every node whose kinds that walk changes; a converged result is a fixed point, so the list is empty
unless a unit kept facts an input change should have re-walked it for. The oracle applies it to
every case (the 442 earlier ones included), every convergence shape and size, every b4b fixture
script and every `widen` fixture: 0 stale nodes on the tip; on A35, the reviewer's program has 3
(`a[0]`, `a[0].trim`, `a[0].trim()`).

**Tests** (`tests/runtime2/b4b/widen/`, 27 programs):

| Fixtures | What | A35 (oracle / guard-off) |
| --- | --- | --- |
| `review_opened_union_stale_static.f` | the reviewer's reproducer, verbatim | caught: kinds `string` only, 3 stale nodes; exit 70 |
| `single_lambda_reader`, `single_param_reader`, `single_push_store`, `single_object_member` | one merge into an opened class, other readers and stores | rescued (another path re-walks the reader first) |
| `transitive_same_fn_{a,b}` | A -> B -> C with C widened: `m` may be B or C (`m[0] = 5` opens B by union), `k` may be A or B (A joins B's now opened class), both in one function | caught (both readers stale); exit 70 |
| `transitive_two_fns_rev_{a,b}` | the two merges in two functions, the A-B merge first called | caught; exit 70 |
| `transitive_lambda_in_fn_readers_{a,b}` | the readers are lambdas inside functions | caught; exit 70 |
| `transitive_union_in_fn_lambda_{a,b}` | the merges inside a lambda | caught; exit 70 |
| `transitive_ab_first_{a,b}`, `transitive_two_fns_{a,b}`, `transitive_lambda_readers_{a,b}`, `transitive_union_in_lambda_{a,b}`, `transitive_globals_union_{a,b}` | the other orders and holders (merge order within the function, call order, readers as top-level lambdas, merges in a top-level lambda, the widened class a global) | rescued (a re-walk of main or a store of unknown origin re-walks every unit) |
| `transitive_widened_root_{a,b}`, `transitive_widened_middle_{a,b}` | the widened class made first (it is the root: B and A join it, the `b_was_open` side), or between A and B | rescued (the root path was correct before) |

`_a` observes A's reader with `trim`, `_b` B's (one `trim` per program, so a refusal of one
reader cannot hide the other). Every fixture: guard on refused `aliasing`, guard off refused
`builtin-receiver:trim` (the reader's `trim` on anything), before any effect, exit 1 (suite step
`widen`, both modes). Oracle: each fixture's readers (`a[0]`, `bb[0]`, `a.k`, `v[0]`) read
anything, and the result is a fixed point (27 cases). A35: 9 oracle cases (26 checks) fail, the
guard-off suite fails on the same 9 programs with exit 70; the rescued ones are kept as order and
holder coverage (on the fixed tree each must hold as well).

**Audit of every path where an opened (widened) class suppresses re-analysis or ignores a store**
(B4b 22):

| Path | What it skips | Guarantee it relies on | Verdict |
| --- | --- | --- | --- |
| `unite_sites`, root side (`grew`) | the root's readers after a union | was: "the root is opened" (also when opened by this union) | **defect (H1 of the review), fixed**: only a root opened before the union |
| `unite_sites`, absorbed side (`b_grew`) | `b`'s readers after a union | `b` was opened before (its flag is not changed by the union): its earlier readers were walked at its opening, later ones read anything | correct (unchanged) |
| `site_store`: `grew && !opened_.count(s)` | the readers of an opened class when a store grows it; `self_changed_` too | `s` (a class representative) was opened before the store: the same | correct |
| `classes()`: `opened_.insert(r).second` | a second `site_changed` when the class is already opened | opened before (as above) or by a union inside this call (now re-walked by `unite_sites`) | correct with the fix (this is the place the reviewer's step 3 names) |
| `session_open` | nothing: every newly opened class re-walks every unit (`site_grew`) | — | correct |
| `materialize` memo (`site_version_`) | stores into an opened class do not bump the version | an opened class's read is anything whatever its contents (`materialize_now` returns before the contents) | correct |
| `prune_part`: `opened_.empty()` early return; `pruned()` drops its `read` | no pruning, and no registration of the parts' classes, while nothing is opened; a store records no reader | every part is read through its own `record`/`materialize`, which registers the reader of its class; pruning at read time registers the pruned parts' classes | correct |
| `declare`: the growth-at-declaration rule (`slot_read_`) | a second walk of the unit when a binding grows where it is declared, before the walk read it | single pass: later reads in the walk see the grown value; other readers are enqueued by `merge_local` (functions for a global, lambdas for a capture) | correct (not opened-specific; reviewed in the previous round) |
| exposure (`exposed_`) gained through a union | no re-walk of the storers of an already opened root, and none for the absorbed side's storers (pre-existing) | exposure concerns stores, not reads; a container is reachable by earlier session code only through a tracked store into an exposed class, which exposes the stored value at that store (per site, before any union) | correct for reads; session reachability remains under the U6 B5 prerequisite (section 4) |

No other reachable defect found. The fixed-point check above verifies the whole set on every oracle
case and fixture, not only these paths.

**Corrected claims.** Section 11 said "`opened_` only grows, and every site merged into an opened
class later reads anything too ...: discarded precision never comes back. Growth of an opened class
changes no read, so it re-walks nobody". That was true for *later* reads only: readers that read a
class before a union opened it kept its precise facts (the H1 defect). Corrected wording (section
11 now refers here): an opened class's later reads are anything; every reader that read it before
it opened, by any of the three paths, is walked again at the opening (for a union since B4b 22);
growth of a class that was already opened changes no read, so it re-walks nobody.

## 15. Compile-time scaling: a deterministic analysis-work gate (B4b 23)

The previous local gate compared wall-clock `compile_us` (best of 7) against B4a with a 1.5x
bound; `nestb` measured 1.37x to 1.67x under load, so it could fail on noise (zero-flakiness
policy; review recommendation). Replaced by deterministic counts of the analysis's work.

**Metric** (`UnitKinds::work`, counted by the inference itself; `tests/rt/b4b_work.cpp` prints them
for a script planned and inferred exactly as the compiler does):

| Count | Where | B4a |
| --- | --- | --- |
| `nodes` | every expression and statement walked (`Inference::visit`, the first step of `expr` and `statement`) | yes |
| `enqueues` | a unit pushed onto the worklist | yes |
| `unite_items` | every type union past the trivial cases (empty, identical, unknown operand): one type plus both operands' entries (sites, fields, items, callables); counted through a thread-local sink set by `Inference::run` | yes (B4a's unions) |
| `materializations` | a site read computed (a memo miss) | no sites |
| `site_unions`, `reader_moves` | site classes merged; reader entries moved by the merges | no sites |
| `site_items` | site entries mapped to classes (`classes()`) | no sites |

`W` is their sum. `walks` (units walked) is counted as before. The counts track the work that went
wrong before: a walk that never stabilises (`nodes`, `walks`), type unions over long site
spellings (`unite_items`: the quadratic term of the earlier `nestb` regression, 48% of its
instructions in `unite` by callgrind) and site bookkeeping (the others).

**B4a baseline** (`tests/runtime2/b4b/work_b4a.json`): `tests/runtime2/b4b/work_build.sh 0df0df6c H`
extracts B4a's `src/rt/infer*` from git, replaces its `rt/infer.h` by the current one (the public
interface is identical from B4a to B4b 23 except `UnitKinds::work`), adds the counters at the same
points (`nodes`, `enqueues`, `unite_items`; B4a has no sites), compiles them and links the harness
with the current tree's other unit objects (parser, plan, resolver, globals). `work.py --harness H
--measure` measures the four shapes of `b4b_types.py` (`glob`, `many`, `nestb`, `containers`) at
100, 400, 800 and 1,600 helpers. The same procedure measured `e293105e`, `a42035c1`, `16f24f36`
and the mutant A30 (`MUTANT=30`, commit `.`): `work_history.json`.

**Budget** (`work.py`, derived from B4a's counts, not from the tip's):
- every shape converges (no walk-budget refusal);
- from each size to the next, the tip's `W` grows by at most 1.10x B4a's growth of its `W` over
  the same step (B4a's analysis is linear on every shape: 3.88x to 3.97x from 100 to 400, 1.99x to
  2.00x per doubling after; a quadratic term fails at the first step);
- walks at most B4a's walks plus one per unit for the shape's one widening (`glob`, `many`, `nestb`:
  the one re-walk of every reader when the shape's class widens past 16 origins; `containers` has
  none and B4a's walks are its bound).

**Results** (the tip; `W` tip / B4a, walks tip / B4a):

| Shape | 100 | 400 | 800 | 1,600 |
| --- | --- | --- | --- | --- |
| `glob` | 21,000 / 2,712 (7.74x); 203 / 106 | 84,000 / 10,512 (7.99x); 803 / 406 | 168,000 / 20,912 (8.03x); 1,603 / 806 | 336,000 / 41,712 (8.06x); 3,203 / 1,606 |
| `many` | 16,813 / 5,151 (3.26x); 202 / 202 | 61,813 / 20,451 (3.02x); 802 / 802 | 121,813 / 40,851 (2.98x); 1,602 / 1,602 | 241,813 / 81,651 (2.96x); 3,202 / 3,202 |
| `nestb` | 16,872 / 6,555 (2.57x); 202 / 202 | 59,890 / 26,145 (2.29x); 802 / 802 | 117,172 / 52,255 (2.24x); 1,602 / 1,602 | 232,104 / 104,505 (2.22x); 3,202 / 3,202 |
| `containers` | 16,197 / 14,102 (1.15x); 102 / 202 | 64,797 / 56,402 (1.15x); 402 / 802 | 129,597 / 112,802 (1.15x); 802 / 1,602 | 259,197 / 225,602 (1.15x); 1,602 / 3,202 |

Linear on every shape (the ratio to B4a is flat or falling). `W` weighs every elementary operation
alike, so its ratio is not a time ratio: a walked node costs far more than a site entry. For
reference, callgrind instructions of the analysis harness (parse, plan and inference; one run each,
local): `glob` 1.79x B4a at 400 and 1.76x at 1,600, `many` 1.83x at 400, `nestb` 1.44x at 400 and
1.34x at 1,600, `containers` 1.22x at 400. The whole compile (`compile_us`, which also holds code
generation and the guard) is 1.0x to 1.6x B4a (below).

**The gate detects the earlier pathologies** (`work.py --self-test` on the recorded counts, and
live on harnesses built by `work_build.sh`):

| Inference | Result |
| --- | --- |
| B4a `0df0df6c` against itself, `16f24f36` | pass |
| `a42035c1` (the previous round's tip) | fails: `many` never converges (7,488 walks at 100 helpers, B4a 202: the 19x pathology; `analysis-limit:kinds`) and `nestb` grows 13.8x from 100 to 400 helpers (B4a 3.99x; `unite_items` 266,088 to 3,814,420: the earlier `nestb` regression, 3.4x to 5.9x wall-clock) |
| `e293105e` | fails: `glob`, `many`, `nestb` superlinear (e.g. `glob` 15.1x from 100 to 400, B4a 3.9x) |
| mutant A30 (the F1 oscillation restored) | fails: `many` not converged (9 failures); A30 is caught by the work gate in the mutant suite too |

**Wall-clock guidance (not gating)**: `b4b_types.py --baseline B4A --runs N` prints, for all four
shapes at 100, 400, 800 and 1,600, the median of N runs of the tip and B4a interleaved run by run,
and their ratio; no bound is checked locally. The objective stays 1.5x B4a, measured on Linode with
repeated runs (median), not the fastest sample. Numbers of this round: section 17.

## 16. The `glob` admission narrowing (B4b 24)

From 16 helpers the `glob` shape is refused `builtin-receiver:trim` where B4a and `a42035c1`
admitted it (Static `trim`): an **admission difference**, not unchanged behaviour, now listed in
compatibility.md ("Compatibility limitations"). Cause: more than 16 allocation sites in one element
union make the contents read as anything (F1), and `trim` on anything is not admitted (B4a's
admission, unchanged); the refusal is before any effect, exit 1, the same with the guard off, in
`NIFT_VM=require` and the default. Permanent tests: `tests/runtime2/b4b/admission/` (`glob15.f`
admitted and equal to legacy; `glob16.f`, `glob17.f`, `glob100.f` refused) with `expect.txt`
(outcome now, B4a's and `a42035c1`'s, and the expected-future marker `recover:vm`), checked by
`test-runtime2-b4b` and its `--guardoff` run. Provisionally accepted by the owner (2026-11-06
15:16). **B5 decision point:** whether runtime-kind checks (B4a's Dynamic precondition) for `trim`
and the related string built-ins are required before the reference guard is removed; they would
admit these programs again with a checked dispatch. Not implemented here (a separate decision).

## 17. Verification of the widened-group repair round (B4b 22 to 25)

Code tree: `0cd6d6ae` (B4b 24); B4b 25 changes this record only. Result files
`scratchpad/b4/gates/y-*.result` (driver `scratchpad/b4/gates-y.sh`, each gate through `gate2.sh`:
`repo_state.py` before and after, ignored files included); `make -q nift` reported `nift` current.

**Gates: 36 of 36 pass, every one with the repository unchanged** (the 35 gates of section 8 with
`test-runtime2-b4b-work` in place of the wall-clock compile-scaling gate):

| Gate | Result |
| --- | --- |
| `test-rt-lint` | 0 problems; boundary 0 problems in 709 files |
| `test-rt-op-admission-unit` | 4,027 checks, 0 failures |
| `test-rt-b4b-types-unit` | 864 checks, 0 failures (fixed point of every case) |
| `test-runtime2-b4b-work` | self-test PASS (B4a, 16f24f36 pass; a42035c1, e293105e, A30 fail as expected); 4 shapes x 4 sizes, 0 failures |
| `test-runtime2-b4b` | 6,846 checks, 0 failures (36 fixtures: 12 run, 24 refused; 27 `widen`, 4 `admission`; generator 1,568) |
| `test-runtime2-guardoff` | b4b `--guardoff` 6,451 checks (generator 1,568, 259 run); `gen_alias` 1,500 (509 run, 0 bad); fuzz 12,512; cross-check 2,060 scripts, 1,075 admitted by both builds (2 approved changes), 0 failures; repo-state 0 changes |
| `test-runtime2-b4`, `-b3`, `-safety` | 12,648; 468; 33,512 programs: PASS |
| `test-cp19-session-cli`, `test-cp19-functional` | PASS |
| `test-runtime2-refsem`, `-collections`, `-packages`, `-args-cmd`, `-cp22`, `-cp23`, `-coro-module`, `-vm-default` | 475; 278; 830; PASS; PASS; 87 cases; PASS; PASS |
| `vm-modules-unit`, `semantics-modules`, `test-rt-refrep`, `test-rt-mutation-protocol`, `test-rt-value` | 179; 22 fixtures; 129; 191 checks; PASS |
| `vm-unit`, `vm-globals-unit`, `vm-adversarial` | 40,341; 23; 39,612 programs, 0 non-approved differences |
| `test-rt-isolation`, `test-production-deps`, `test-rt-build` | PASS (1,513 production compile commands) |
| mutant suites: b4b, b4, b3, safety, collections, refsem | 17 (A35 added), 17, 11, 7, 11, 23 caught |

Not run locally (the combined Linode certification): full `make test`, `test-rt-vm-sanitize` with
`test-runtime2-guardoff-sanitize`, `test-build-reuse`, performance campaigns.

**Classification** (`classify_tree.py` over the tip's `tests/runtime2`, 2,060 scripts, and CP25-R
`classify-all`, 589 units; the four binaries on the same corpus: tip, `16f24f36` (the reviewer's
byte-identical build), `a42035c1`, B4a `0df0df6c`):

- against `16f24f36`: **0 changes** (both corpora);
- against `a42035c1`: CP25-R 0; `tests/runtime2` 3: `b4b/admission/glob16.f`, `glob17.f`,
  `glob100.f` compiled to refused `builtin-receiver:trim` (section 16; `16f24f36` refuses them too);
- against B4a: CP25-R 0 changes of outcome, reason, line or column (the reasons list of
  `vm-corpus/cp04/toplevel_alias.f` as in section 6); `tests/runtime2` 9: the 6 of section 6 and the
  3 `glob` fixtures.
- The 27 `widen` fixtures are refused (`aliasing`) by all four binaries.
- **No program is admitted on the tip that B4a (or `a42035c1`, `16f24f36`) refuses**: 0 of the
  tip's 1,077 compiled scripts and 0 of its 264 compiled CP25-R units. With the guard off, every
  script compiled by both builds runs without exit 70, internal text or `dispatch_defects` (the
  cross-check, 2,060 scripts), and the generators below find none.

**Wrong-kind generators** (the reviewer's `gen1.py` and `gen_struct.py`, copies in
`scratchpad/b4c/gen/tools` with only `builtin_list.inc`'s path changed, identical to B4a's;
`gen_alias.py`; results `scratchpad/b4c/gen/`):

| Generator | Guard on: programs, run by the VM, bad, differences from legacy | Against B4a, `a42035c1` (2e7cde13 data) and `16f24f36` | Guard off: run by the VM, bad |
| --- | --- | --- | --- |
| `gen1.py` pass a | 20,274; 1,297; 0; 131 | identical (0 admission changes, 0 VM output changes, 0 new differences) | 1,318; 0 |
| `gen1.py` pass b (`NB=16000`) | 16,000; 955; 0; 138 | identical | 964; 0 |
| `gen_struct.py` | 1,911; 867; 0; 213 (all D-009#5) | identical | 867; 0 |
| `gen_alias.py --sample 1500 --seed 1` | 1,500; 9; 0 | - | 509; 0 |

"Bad": exit 70, a signal, internal text, an internal tag or a dispatch defect. With the guard off
the differences from legacy are the sharing itself and are not compared.

**H1, H2, M1, P1, U1 evidence on the tip** (`scratchpad/b4c/ev/evidence.txt`): the earlier review's
`H1_thread_unknown_callee_exit70.f` and `_b.f` refused `builtin-receiver:trim` before any effect in
default and `require` mode and with the guard off; `P1_index_assign_expr_segv.f`, `_fn.f` print `5`
and `k` (exit 0) in all three; `L1` refused (`aliasing`, guard off `method:insert`); `L2` the CP22
correction (`a`, `end`); `H2_compile_glob100.f` refused `builtin-receiver:trim` (section 16); the
reviewer's `rv_b4b_unit.cpp` (M1) against the tip's objects: 40 checks, 0 failures; the U6 probe:
production refused `reference-guard:inflow`, guard off exit 70 (the recorded B5 prerequisite). The
review of `16f24f36`: its reproducer and all `p/O1`-`O6` refused `builtin-receiver:trim` with the
guard off, except `O1c_nomix.f`, which runs (no union) without an internal failure. U1: the 8
`lambda_capture_*` fixtures and oracle cases pass (gates above); A32 caught.

**Run time** (callgrind, instructions per iteration, tip against B4a, local): identical. B4a
built-in set 1074, 903, 1187, 870, 2844 (both); `run.py --groups ops` 117, 188, 567, 436, 367, 763,
3315, 249 (both); `vm_compare.py --groups mutation` 381, 1430, 2446, 1496, 1363, 2190 (both).

**Compile time, wall clock (guidance; median of 11 interleaved runs against B4a, local, loaded
2-core machine):**

| Shape | 100 | 400 | 800 | 1,600 |
| --- | --- | --- | --- | --- |
| `glob` | 1.34x | 1.29x | 1.24x | 1.06x |
| `many` | 1.29x | 1.34x | 1.43x | 1.53x |
| `nestb` | 1.54x | 1.45x | 1.41x | 1.35x |
| `containers` | 1.26x | 1.27x | 1.21x | 1.23x |

Two points are above 1.5x locally (`nestb` 100, `many` 1,600; the deterministic counts of section
15 show no superlinear term). The 1.5x objective is for the controlled Linode measurement (median
of repeated runs); recorded for the owner, not hidden by a looser bound.

**Freeze:** tip and tree in the commit B4b 25 (this record); worktree clean.
