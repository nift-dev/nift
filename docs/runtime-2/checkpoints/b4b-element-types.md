# Checkpoint B4b: sound per-site element and member typing

Status: **design note, for the coordinator's approval** (no code yet). Branch `cand/b4-dispatch`
after the accepted B4a boundary `0df0df6c`. Plan: Phase B plan §5.7 and "B4" (per-site types,
mutant A2); the owner's B4b ruling (2026-11-06).

## Design note

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
