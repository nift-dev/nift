# Checkpoint B4b: sound per-site element and member typing

Status: **candidate, frozen for independent review** (B4b). Branch `cand/b4-dispatch`, after the
accepted B4a boundary `0df0df6c`. Plan: Phase B plan §5.7 and "B4" (per-site types, mutant A2);
the owner's B4b ruling (2026-11-06); the coordinator's approval of the design note with
conditions C1-C8 (2026-11-06) and the owner's evidence requirement (section 3). Commits B4b 0
(this note) to B4b 3; tip, tree and gates: section 8.

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
`infer_types.{h,cpp}`, `infer_unit.h`. Production: 12 files, +449 -33.

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
| Every method-call site is statically proven or dynamically checked | B4a: admission is Static only on proven kinds, else Dynamic (flagged, under the precondition) or Refuse; `op-admission-unit` (4027 checks), the spec x kind enumeration (3,000 programs) and the struct oracle (1,911) in `test-runtime2-b4`; mutants A3-A6. B4b supplies sound kinds for U2 (section 4): the oracle, mutants A2, A19-A24 |
| Every mutation path is in the type model | C1 check in `b4b_types.py` (protocol entry points, `CollectionMethod` enumerators, natives, protocol users, against the sources); `static_assert` on the effect table; oracle cases per path; mutant A20 (one mutator dropped) caught |
| Unknown values cannot reach unchecked dispatch | `kOpenSite`/`agg_unknown_`: unknown origin reads as anything (the compiler then refuses or dispatches dynamically); C2 (`dynamic_mutation`, A24), C4 (`escape_callables`/`untracked_call`, A23); generator (section 5): `dispatch_defects` 0, never exit 70 |
| Session-persistent code cannot keep stale assumptions | C5 above: coherent recompilation and shape contracts (CP19, its mutants 7/8), the guard for aliases (`b4b/sessions/*`), `session_effects` (A21, oracle `session-earlier-call`) |
| The wrong-kind generators find no internal failures | `b4b_types.py` generator (1,568 programs); the reviewer's `gen1.py` (both passes) and `gen_struct.py` on the B4b tip (section 5) |
| Performance stays within the limits | Section 7: run time identical (B4a set, ops, mutation); compile time within 1.25x of B4a; the scaling checks in `test-runtime2-b4` and `test-runtime2-b4b` |

## 4. Unflagged method-site classes (the B4a section 9 prerequisite)

| Class | Receiver | Answer | Holds after B5 (guard removed)? |
| --- | --- | --- | --- |
| U1 | a binding's own value (local, parameter, global, captured cell, module binding) | (a) its kinds are the union of every write to the binding (parameters: every call, C4 for unknown callers) | **Yes**: sharing changes contents, not which value a binding holds |
| U2 | an element or member read (`xs[i]`, `o.k`, `for` binding, element-returning methods) | (a) per-site contents (B4b) | **Yes**: stores through every alias reach the sites by construction |
| U3 | a struct field | (a) one slot per field (B3) | **Yes** |
| U4 | map/set/stack contents | (a) per site (D-NEXT-refsem) | **Yes** |
| U5 | a call result | (a) via U2 | **Yes** |
| U6 | a value of an earlier session statement (session binding) | (a) its recorded structure, kept by CP19's per-binding shape contracts and coherent recompilation; an alias store is refused by the guard | **No**: the contracts are per root binding, so after B5 an alias store in a later statement bypasses them. B5 prerequisite: per-site session contracts, or earlier values' contents read as anything (measured on this tree: the cp19 session suites, refsem and b4 sessions, safety and args-cmd lose admitted cases), or the precondition at this class |
| U7 | a transferred value (thread/async argument, mutex value, channel receive, awaited result) | (a) copies (D7): sender's sites, or unknown origin | **Yes**: transfers copy in both models |

## 5. Tests and evidence

- `make test-rt-b4b-types-unit` (`tests/rt/b4b_types_unit.cpp`, the primary oracle): 28 cases, 140
  checks (section 2 lists the paths).
- `make test-runtime2-b4b` (`tests/runtime2/b4b_types.py`): 22 scripts, 3 module trees, 5
  sessions; the C1 check; the wrong-kind generator (8 holder paths x 4 original kinds x 7 stored
  kinds, map, struct, int, string, array, atomic and mutex, x 7 observations: 1,568 programs);
  the container-heavy scaling check. Every program: never exit 70, never an internal tag,
  `dispatch_defects` 0, refused before any effect or equal to legacy (D-009#5 allowed; legacy's
  shown handle tags are the approved B0/CP17/B4 difference).
- `make test-runtime2-b4b-mutants`: section 5a.
- The reviewer's generators on the B4b tip: section 5b.

### 5a. Mutants (each must fail the oracle or the suite)

| Mutant | File | Defect | Caught by |
| --- | --- | --- | --- |
| A2 | `infer_sites.cpp` | a store reaches only the binding stored through (the stale per-binding type) | oracle, 17 cases |
| A19 | `infer_sites.cpp` | stores inside functions and lambdas miss the caller's sites (callee/closure path) | oracle, 3 cases |
| A20 | `infer_mutators.h` (infer_sites, infer_collection) | one mutator (`reverse`) dropped from the list (C1) | oracle, 1 case |
| A21 | `infer_sites.cpp` | a call into an earlier session statement's code keeps the statement's site facts (C5) | oracle, 1 case |
| A22 | `infer_sites.cpp` | an inner part has no derived site and a site-less value is taken as fresh (nested alias) | oracle, 1 case |
| A23 | `infer_sites.cpp` | an escaping function keeps its known calls' parameter types (C4) | oracle, 1 case |
| A24 | `infer_sites.cpp` | a mutator on a receiver that is not exactly an array is not modelled (C2) | oracle, 1 case |

B3's D11 (array mutators keep positional kinds outside sessions) now also keeps the sites'
positions for a script's non-append mutator (`infer_collection.cpp`): B4b's per-site drop had
subsumed the binding-level one, so the mutant survived until it followed the facts into the sites.

No separate transfer mutant: transfers have no invalidation path of their own (copies; the
callee's stores are the callee path, A19).

### 5b. The reviewer's generators on the B4b tip

| Generator | Programs | Run by the VM | Exit 70 / tag leak / internal text / defects | Differences from legacy |
| --- | --- | --- | --- | --- |
| `gen_struct.py` | 1,911 | 867 | 0 / 0 / 0 / 0 | 213, all D-009#5 (the `return:` prefix) |
| `gen1.py` pass a (`for`, `fnloop`) | 20,274 | 1,297 | 0 / 0 / 0 / 0 | 131; the same programs and the same VM output as `569e336f` (all pre-existing, same on B3) |
| `gen1.py` pass b (16,000 sampled) | 16,000 | 955 | 0 / 0 / 0 / 0 | 138: the re-review's figures for `569e336f` (124 same on B3, 14 the approved M1 class) |

No program changed its VM output or admission against B4a in pass a.

## 6. Classification (the B4a tip `0df0df6c` against the B4b tip)

- CP25-R `classify-all` (589 units): **0 changes** of outcome, reason, line or column. One unit's
  reasons list (`vm-corpus/cp04/toplevel_alias.f`, refused `aliasing` in both) lists `aliasing` and
  `unresolved-name` once more each.
- `tests/runtime2` classify (2,002 scripts, the new `b4b/` fixtures included): 2 changes, both
  refused in both binaries, with another reason:

  | Script | B4a | B4b | Why |
  | --- | --- | --- | --- |
  | `b4b/scripts/push_alias_struct.f` | `reference-guard:inflow` | `type:member` | the alias's push of a map reaches `xs`'s site: `xs[1].a` is no longer a proven struct field |
  | `prevalidate/.../probes/nift-limits.f` | `compound-target` | `type:<` | a store into a mutex's value (unknown origin) makes every container anything |

- `safety/expected.json`: `m_sortc_object_loop`, `m_sortc_object_mapget` stay refused, now `type:<`
  (the comparator passed to `sort` on an object escapes: C4) before the guard's reason.
- Sessions (the CP19 session suites, refsem, b4, safety and args-cmd sessions): no change
  (section 8).
- No previously admitted program is refused.

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

