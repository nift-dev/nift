# Checkpoint B4b: sound per-site element and member typing

Status: **fix-round candidate, frozen for focused re-review** (B4b). Branch `cand/b4-dispatch`,
after the accepted B4a boundary `0df0df6c`. Plan: Phase B plan §5.7 and "B4" (per-site types,
mutant A2); the owner's B4b ruling (2026-11-06); the coordinator's approval of the design note with
conditions C1-C8 (2026-11-06) and the owner's evidence requirement (section 3). Commits B4b 0
(this note) to B4b 6 (rejected tip `e293105e`), fix round B4b 7 to 14 (section 10); tip, tree and
gates: section 8. **Open finding F1** (section 9, found in the hand-over verification): the H2
site-class merge makes a parameter-push shape exhaust the walk budget from 16 helpers on.

B4b changes no observable behaviour while the guard is on: it only moves programs between
admitted and refused (section 6). Run time is unchanged (section 7).

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

## 3. The owner's properties and their evidence

| Property | Evidence |
| --- | --- |
| Every method-call site is statically proven or dynamically checked | B4a: admission is Static only on proven kinds, else Dynamic (flagged, under the precondition) or Refuse; `op-admission-unit` (4,027 checks), the spec x kind enumeration (3,000 programs) and the struct oracle (1,911) in `test-runtime2-b4`; mutants A3-A6. B4b: sound kinds for U1 (C4 incl. review H1: oracle `thread-unknown-callee`, `thread-unknown-returned`, `call-unknown-ternary`, `escape-*`; fixtures `b4b/scripts/*unknown_callee*.f`, `b4b/modules/escape_export`; mutants A23, A28) and U2 (51 oracle cases; mutants A2, A19-A22, A24, A26, A27, A29). **Direct evidence:** the guard-off gate (`test-runtime2-guardoff`, shared references, so a stale Static fact shows as `compiler defect`, exit 70; section 10.2) on `2e7cde13`: 0 failures over the b4b suite (28 fixtures, 1,568 generated), 1,500 alias programs, 12,512 fuzz programs and the 2,021-script cross-check |
| Every mutation path is in the type model | C1 check in `b4b_types.py` against the sources; `static_assert` on the effect table; oracle cases per path (alias, inner, callee, closure, global, thread, reorder, dynamic receiver incl. L1 `union-callable-member`, dynamic key, session push/index/member/closure stores); mutant A20 (one mutator dropped), A29 (session store paths) |
| Unknown values cannot reach unchecked dispatch | `kOpenSite`/`agg_unknown_`; C2 (A24), C4 (A23; A28: an inexact callee or any thread/async entry widens every function used as a value). Measured on `2e7cde13`: H1 reproducers refused (no exit 70); the four exit-70 fixtures of B4a are now refused (section 6); generators 38,185 programs, 0 exit 70, 0 defects (section 5b); guard-off gate `dispatch_defects` 0 |
| Session-persistent code cannot keep stale assumptions | Today: coherent recompilation and shape contracts (CP19, mutants 7/8), the guard for aliases; `session_effects` with the full reachability rule (review M1: oracle `session-push-exposed`, `-index-`, `-member-`, `-closure-exposed`, `session-late-store`, `session-store-after-call`; mutants A21 (8 cases), A29 (3 cases)); the reviewer's `rv_b4b_unit.cpp` 40 checks, 0 failures on the tip objects. After B5: **not yet** (U6, section 4): the guard-off gate shows it (`b4b/guardoff/u6_session_alias.in`, listed in `expected_u6.txt`, which B5 must empty; re-checked on the tip: guard-off exit 70 `unexpected operands (compiler defect): built-in trim`, production refused `reference-guard:inflow`) |
| The wrong-kind generators find no internal failures | On `2e7cde13`: `b4b_types.py` generator (1,568), `gen_alias.py` (1,500, guard-off), `gen1.py` pass a (20,274) and pass b (16,000), `gen_struct.py` (1,911): 0 exit 70, signals, tag leaks, internal text or defects; the same admissions and VM output as B4a and `e293105e` (section 5b). P1 (a pre-existing codegen crash the review found) fixed in B4b 7 and 11 (section 10.1); a 390-program assignment-as-value probe set crashes 117 times on B4a and 0 times on the tip |
| Performance stays within the limits | Run time identical (section 7). Compile time: the review H2 `glob` shape 1.32x, 1.29x, 1.26x, 1.18x B4a at 100, 400, 800, 1,600 helpers (gate `fx-compile-scaling`; e293105e: 1.68x to 22x), containers 1.32x/1.29x (section 10.3). **Not met for one shape (open F1, section 9):** the parameter-push shape `many` (guard-refused today, admitted after B5) costs about 19x B4a at 100 to 800 helpers and is refused `analysis-limit:kinds` from 16 helpers on; `nestb` (guard-off admitted) 3.4x at 100 rising to 5.9x at 800 |

## 4. Unflagged method-site classes (the B4a section 9 prerequisite)

| Class | Receiver | Answer | Holds after B5 (guard removed)? |
| --- | --- | --- | --- |
| U1 | a binding's own value (local, parameter, global, captured cell, module binding) | (a) its kinds are the union of every write to the binding. Parameters: every tracked call; a function whose callers are not all known (a native's callback, any call or thread/async entry whose targets are not exact: review H1) has parameters of anything (C4). The c56c4093-era claim "parameters: every call" was false for `thread(f, ...)` with inexact `f` (exit 70 on the B4b tip `e293105e`; fixed in B4b 8) | **Yes**: sharing changes contents, not which value a binding holds |
| U2 | an element or member read (`xs[i]`, `o.k`, `for` binding, element-returning methods) | (a) per-site contents (B4b) | **Yes**: stores through every alias reach the sites by construction |
| U3 | a struct field | (a) one slot per field (B3) | **Yes** |
| U4 | map/set/stack contents | (a) per site (D-NEXT-refsem) | **Yes** |
| U5 | a call result | (a) via U2 | **Yes** |
| U6 | a value of an earlier session statement (session binding) | (a) its recorded structure, kept by CP19's per-binding shape contracts and coherent recompilation; an alias store is refused by the guard | **No**: the contracts are per root binding, so after B5 an alias store in a later statement bypasses them. B5 prerequisite: per-site session contracts, or earlier values' contents read as anything (measured on this tree: the cp19 session suites, refsem and b4 sessions, safety and args-cmd lose admitted cases), or the precondition at this class |
| U7 | a transferred value (thread/async argument, mutex value, channel receive, awaited result) | (a) copies (D7): sender's sites, or unknown origin | **Yes**: transfers copy in both models |

## 5. Tests and evidence

Figures on the fix-round tip `2e7cde13` (tree `40dc5d0d`) unless stated.

- `make test-rt-b4b-types-unit` (`tests/rt/b4b_types_unit.cpp`, the primary oracle): 51 cases, 255
  checks, 0 failures (section 2 lists the paths; the fix round's H1, H2 and M1 cases: section 10).
- `make test-runtime2-b4b` (`tests/runtime2/b4b_types.py`): 25 scripts, 4 module trees, 5 sessions
  (28 fixture programs: 4 run by the VM, equal to legacy; 24 refused before any effect); 11
  `b4b/codegen` P1 fixtures (9 run by the VM, equal to legacy; 2 `refused_*`); the C1 check; the
  wrong-kind generator (8 holder paths x 4 original kinds x 7 stored kinds, map, struct, int,
  string, array, atomic and mutex, x 7 observations: 1,568 programs, 74 run by the VM); the
  container-heavy and `glob` scaling checks. 6,471 checks, 0 failures. Every program: never exit
  70, never an internal tag, `dispatch_defects` 0, refused before any effect or equal to legacy
  (D-009#5 allowed; legacy's shown handle tags are the approved B0/CP17/B4 difference).
- `make test-runtime2-b4b-mutants`: section 5a.
- `make test-runtime2-guardoff`: section 10.2.
- The reviewer's generators on the tip: section 5b.

### 5a. Mutants (each must fail the oracle or the suite)

`test-runtime2-b4b-mutants` on `2e7cde13`: **12 caught**. Each mutant is also linked into a
guard-off CLI and run through the guard-off b4b suite and 600 alias programs (column "guard-off";
informational, the mutant is caught by the oracle or the suite).

| Mutant | File | Defect | Oracle cases | Suite checks | Guard-off |
| --- | --- | --- | --- | --- | --- |
| A2 | `infer_sites.cpp` | a store reaches only the binding stored through (the stale per-binding type) | 28 | 0 | failed |
| A19 | `infer_sites.cpp` | stores inside functions and lambdas miss the caller's sites (callee/closure path) | 8 | 0 | failed |
| A20 | `infer_sites.cpp`, `infer_collection.cpp` | one mutator (`reverse`) dropped from the list (C1) | 1 | 0 | passed |
| A21 | `infer_escape.cpp` | a call into an earlier session statement's code keeps the statement's site facts (C5) | 8 | 0 | passed |
| A22 | `infer_sites.cpp` | an inner part has no derived site and a site-less value is taken as fresh (nested alias) | 1 | 0 | passed |
| A23 | `infer_escape.cpp` | an escaping function keeps its known calls' parameter types (C4) | 9 | 9 | failed |
| A24 | `infer_sites.cpp` | a mutator on a receiver that is not exactly an array is not modelled (C2) | 2 | 0 | passed |
| A25 | `compiler_assign_expr.cpp` | P1: an assignment's constant value moved as a register | 0 | 8 (the codegen fixtures refused by the builder invariant) | failed |
| A26 | `infer_sites.cpp` | H2: no merge beyond 16 sites | 1 (`collapsed-sites`) | 4 (the `glob` scaling check: 6.7x per doubling and more) | passed |
| A27 | `infer_sites.cpp` | H2: a grown site's readers are not re-walked | 1 (`reader-rewalk-late`) | 0 | passed |
| A28 | `infer_escape.cpp` | H1: an inexact callee widens no target | 6 | 9 | failed |
| A29 | `infer_sites.cpp` | M1: a push/index/member store into an earlier statement's value exposes nothing | 3 (`session-push/index/member-exposed`) | 0 | passed |

B3's D11 (array mutators keep positional kinds outside sessions) now also keeps the sites'
positions for a script's non-append mutator (`infer_collection.cpp`): B4b's per-site drop had
subsumed the binding-level one, so the mutant survived until it followed the facts into the sites.

No separate transfer mutant: transfers have no invalidation path of their own (copies; the
callee's stores are the callee path, A19). No mutant removes the builder invariant itself
(`register-operand-constant`, section 10.1): with A25 applied the invariant turns the crash into a
refusal, which is what the suite sees.

### 5b. The reviewer's generators on the tip

`review-b4/repro2/gen1.py` and `gen_struct.py` (the reviewer's copies; only the path of
`builtin_list.inc`, identical in `0df0df6c`, `e293105e` and the tip, and the work directory
changed), the same parameters (pass a: `for`, `fnloop`; pass b: `NB=16000`, seed 7), run on the
tip `nift` and, for the comparison, on a B4a `0df0df6c` binary built for it (byte-identical to the
`--baseline` binary of the scaling gate). The `e293105e` figures are the reviewer's own jsonl
(`review-b4b/gen/`).

| Generator | Programs | Run by the VM | Exit 70 / signal / tag leak / internal text / defects | Differences from legacy | Against B4a and `e293105e` |
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

**B4a `0df0df6c` against the tip `2e7cde13`.**

- CP25-R `classify-all` (589 units): **0 changes** of outcome, reason, line or column. One unit's
  reasons list (`vm-corpus/cp04/toplevel_alias.f`, refused `aliasing` in both) lists `aliasing` and
  `unresolved-name` once more each (the same as on `e293105e`).
- `tests/runtime2` classify (2,021 scripts): 6 changes.

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

Run time (instructions per iteration, B4a tip against B4b tip): identical.

| Workload | B4a | B4b |
| --- | --- | --- |
| B4a built-in set: `s.trim()`, `a.contains(2)`, `i.to_string()`, `o.has("a")`, Dynamic `contains` | 1074, 903, 1187, 870, 2844 | 1074, 903, 1187, 870, 2844 |
| `run.py --groups ops`: loop, add_local, call, array_index, array_push, object_member, string_concat, if_branch | 117, 188, 567, 436, 367, 763, 3313, 249 | identical |
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

The run-time figures and the table above ran on B4b 3 (the frozen round). The fix round changes
the inference and, in codegen, only the assignment-as-value result (B4b 7) and a build-time
invariant in `FunctionBuilder::emit` (B4b 11); run time was not re-measured. Its compile-time
figures are in section 10.3; the generators show the same VM output as B4a for every admitted
program (section 5b).

## 8. Gates (local; result files `scratchpad/b4/gates/fx-*.result`)

Run by `scratchpad/b4/gate.sh` on head `2e7cde13` (tree `40dc5d0d`), every gate exit 0, git status
clean (0 entries) before and after each; `make -q nift` reports the tip `nift` up to date with that
tree. The following commit (B4b 14) changes this record only.

| Gate | Result |
| --- | --- |
| `test-rt-lint` | PASS (703 files; rt-lint 0 problems) |
| `test-rt-op-admission-unit` | 4,027 checks, 0 failures |
| `test-rt-b4b-types-unit` | 255 checks, 0 failures |
| `test-runtime2-b4b` | 6,471 checks, 0 failures |
| `test-runtime2-b4b-mutants` | 12 caught (section 5a) |
| `test-runtime2-guardoff` | PASS (section 10.2) |
| `test-runtime2-b4`, `-b4-mutants` | 12,648 checks; 17 caught or covered |
| `test-runtime2-b3`, `-b3-mutants` | 468 checks; 11 caught |
| `test-runtime2-safety` | 33,512 programs |
| `test-cp19-session-cli`, `test-cp19-functional` | PASS |
| `test-runtime2-refsem` | 475 checks |
| `test-runtime2-collections` | 278 checks |
| `vm-modules-unit` | 179 checks |
| `semantics-modules` (+ structs, `modules_d041 --legacy-today`) | 25, 26 probes; 22 fixtures |
| compile scaling (`b4_dispatch.compile_scaling`; `b4b_types.py --quick --baseline` B4a) | PASS (section 10.3) |

Not re-run on the fix round (last run on `301095af`, section 8 of the frozen round: PASS):
`test-rt-build`, `test-rt-isolation`, `test-production-deps`, `test-runtime2-safety-mutants`,
`-collections-mutants`, `-refsem-mutants`, `test-runtime2-packages`, `-args-cmd`, `-cp22`, `-cp23`,
`-coro-module`, `-vm-default`, `test-rt-refrep`, `test-rt-mutation-protocol`, `test-rt-value`,
`vm-unit`, `vm-globals-unit`, `vm-adversarial`. Not run locally (combined certification): full
`make test`, `test-rt-vm-sanitize` with `test-runtime2-guardoff-sanitize`, `test-build-reuse`, perf
campaigns.

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
- **F1 (open; found in the hand-over verification, not fixed): the H2 merge exhausts the walk
  budget on a parameter-push shape.** `review-b4b/perf/many<N>.f` (N helpers
  `fn(kN(r)) { r.push([<a literal of kind N mod 7>, " s "]) ... }`, each called once with one shared
  `acc`): with 15 helpers the tip and the guard-off build compile it (guard-off admits it); from
  16 helpers on both refuse it `analysis-limit:kinds` (the inference's walk budget), at about
  19x B4a's compile time (B4a 100: 1.8 ms, tip 34.7 ms; 800: 14.3 ms against 281 ms; B4a and
  `e293105e` refuse it `reference-guard:argument`). The cliff sits exactly at the 16-class merge
  bound, which points at the merge (B4b 8) not converging for sites reached through a parameter;
  the `glob` shape (a global `acc`, measured by the gate) and the same shape returning fresh
  arrays pushed at top level (`ret<N>`: 1.1x to 1.3x B4a at 15 to 200, admitted) are not affected.
  Today it is masked: the guard refuses the shape anyway, so no admitted program changes (section
  6, 5b), only the compile time of the refusal. After B5 these programs would be refused by the
  analysis limit instead of admitted. `nestb<N>` (the same with the pushed value passed in as a
  parameter; refused `aliasing`, admitted by the guard-off build) is 3.4x B4a at 100 rising to
  5.9x at 800 (`e293105e`: 23x at 800). Measurements: `ctime.sh`, median of 5 to 9, local, guidance
  only. Needs an owner/coordinator decision before B5; the record does not claim property 6 for
  these shapes.
- **F2 (test hygiene, not fixed):** `tests/runtime2/b4b/guardoff.py` runs every fixture in its
  source directory (as its suite does), so scripts that write files leave them in the tree; B4b 12
  committed three such outputs (`cp16/probes/scripts/fs_symlink/link.txt`, `cp22/refuse/w.txt`,
  `rt-diff-corpus/scripts/modes/out.txt`) and the status stayed clean. `fs_symlink/link.txt` is a
  regular file where the probe's `setup.sh` creates a symlink (`ln -s real.txt link.txt` now fails
  with "File exists" in the CP16 capture harness, which copies the probe directory). The CP16
  `capture.py --check` of `fs_symlink` differs with and without the file (its stdout order differs
  from `expected/` either way), and no Makefile gate runs that capture, so no gate result changes;
  the cross-check should run each script in a temporary copy and the three files be removed.
- **`nift eval --json` internal tag** (owner ruling): a function's tag in `--json` output is a
  release blocker assigned to the release-blocker audit (b4-dispatch.md section 9); B4b does not
  touch the JSON conversion path.
- **B5 prerequisite (U6, section 4):** earlier session statements' values keep their recorded
  structure, which only the CP19 per-binding contracts and the guard keep true. Before the guard
  goes: per-site session contracts, or contents of earlier values read as anything (measured here:
  admitted session cases in cp19, refsem, b4, safety and args-cmd become refused), or the
  precondition at that class.
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

### 10.2 The standing guard-off gate `test-runtime2-guardoff`

**Probe binary.** `.build/b4b-guardoff/nift-guardoff`: the production CLI objects with
`src/rt/compiler.cpp`, `compiler_rules.cpp` and `compiler_collection_sort.cpp` replaced by their
`-DNIFT_RT_REFERENCE_PROBE` objects (`RT_REFREP_PROBE_OBJECTS`, the B2 reference-probe
configuration: no Phase A reference guard, no `aliasing` location-alias refusal, so the VM runs
shared references and a stale Static fact meets a foreign kind as `unexpected operands (compiler
defect)`, exit 70). Guard-on gates stay mandatory; this gate is in addition.

**Programs (counts on `2e7cde13`):**

| Step | Programs | Result |
| --- | --- | --- |
| `b4b_types.py --guardoff` | the b4b fixtures (9 run by the VM, 19 refused), the sessions incl. `b4b/guardoff/*.in`, the 1,568-program generator (259 run by the VM) | 6,203 checks, 0 failures |
| `gen_alias.py --sample 1500 --seed 1` | 1,500 transitive-alias programs | 509 run by the VM, 0 bad |
| `safety/fuzz.py --seed 1 --sample 3000 --session-sample 1000` | 12,512 (8,512 exhaustive, 3,000 sampled, 1,000 session; 626 run twice) | PASS |
| `b4b/guardoff.py` (cross-check) | every `.f` under `tests/runtime2` (module directories excluded): 2,021 scripts | 1,066 admitted by both builds (2 approved changes), 0 failures |

**Failure conditions.** In the cross-check, per script on the guard-off build (`NIFT_VM=require`,
`timeout 30`, its own directory, stats on): exit 70 (unless approved `exit70`); a timeout (124,
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
on the guard-on build), `rt-diff-selftest-corpus/timeout/loop/main.f` (timeout: deliberate).
Expected U6 failure: `u6_session_alias.in` (`unexpected operands (compiler defect)`).

**ASan/UBSan variant.** `make test-runtime2-guardoff-sanitize`
(`.build/b4b-guardoff/nift-guardoff-sanitize`: the sanitizer CLI objects with the probe objects
built with `SANITIZER_FLAGS`; `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`,
`UBSAN_OPTIONS=halt_on_error=1`): `b4b_types.py --guardoff --quick`, `gen_alias.py --sample 400`,
and the cross-check over `b4 b4b b3 refsem` without the twice-run. A prerequisite of
`test-rt-vm-sanitize`. **Not run locally** (not built: disk and time); it runs in the combined
Linode certification campaign.

Test hygiene: see F2 (the cross-check writes into fixture directories).

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
rise with size: growth is removed for these shapes. **Not met for the `many` and `nestb` shapes
(F1).**

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
