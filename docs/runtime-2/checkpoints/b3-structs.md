# Checkpoint B3: struct type descriptors and privacy-aware struct rendering

Phase B plan §5.5 and §6 B3; owner rulings of 2026-11-02 (rendering contract: struct rendering
required in B3, private fields never visible on any presentation path, presentation observational)
and the 2026-11-04 addendum (batch B3–B5 certification). Branch `cand/b3-structs`, based on
`0b5079fc` (local `weekend/r2` with B0, B1, B2 and the tooling checkpoint).

| Commit | Content |
| --- | --- |
| `90ecc5ca` | B3 1: `StructType` descriptors and their plumbing (no visible change) |
| `4f6d4597` | B3 2: rendering from the descriptor; presentation refusals deleted; fixtures converted |
| `45f4461e` | B3 3: B3 suite, unit, non-disclosure sweep, probe cases, mutants D1–D7 |
| `a89c8d63` | B3 2b: the refsem sessions part of B3 2 (left out of that commit by mistake) |
| `151fbd50` | B3 2c: owner ruling: explicit calls run a struct's own presentation method; mixed receivers refused; D8, D9 |
| `ffeecb9b` | B3 3b: the unit's checked-build branch sees its switch (the ASan lane) |
| `f24c292e` | B3 4: this record (first freeze; independent review: ACCEPT-WITH-CONDITIONS) |
| `804858a9` | B3 5: review fixes H1, L3, L1 (section 8) and the M1 fixture wording |
| `7b752464` | B3 6: this record's review section and compatibility.md (M1) (second freeze; re-review: ACCEPT-WITH-CONDITIONS) |
| `058b23e0` | B3 7: re-review fixes N1 (positional kinds after array mutators), N2 (session refusal reasons); D11 |
| `e3e5d511` | B3 8: this record's re-review section (section 9) and compatibility.md wording (N4) |
| `e22811be` | B3 9: the owner's approvals (2.4, 2.5, compatibility.md, the fixtures); the N1 union property recorded |
| (B3 10, code) | N1 refined: an empty literal keeps its proven-empty element type (cp14b) |
| (B3 11) | this section's B3 10 note; the third freeze |

Gate runs (section 6 and 9):
- first freeze: on `151fbd50` and `ffeecb9b`;
- second freeze: on `7b752464` itself;
- third freeze: on the B3 11 commit itself (a clean tree, after this record was written; the
  results are in the `f3-*.result` files).
  - A run started on `e3e5d511` was stopped when the owner's approvals arrived; its partial results
    were discarded.
  - The full run on `e22811be` (`n-*.result`) passed every gate except `test-runtime2-cp14b`
    (B3 10).

## 1. Contract

**Descriptor** (`src/rt/struct_type.{h,cpp}`).
- `StructType` is immutable and owned by `std::shared_ptr<const StructType>`. It holds:
  - `name`;
  - `fields`: the field names, as text, in declaration order;
  - `private_fields`: the mask, parallel to `fields`;
  - `version`: a process-unique creation serial.
- `StructObject::type` references the descriptor the instance was constructed from. Privacy lives
  only in the descriptor, never in instances. An instance keeps its per-field atoms, so the layout
  of `fields` is unchanged.
- Creation:
  - Scripts and modules: one descriptor per declaration per compile (cached in the `UnitCompiler`).
    Each `Proto` that constructs holds the descriptor in `Proto::struct_types`, and `NewStruct`'s b
    operand indexes that table. Before B3 the operand was an atom; it is no longer remapped as one
    in program snapshots.
  - Sessions: one descriptor per schema version. `SessionCompileContext::struct_types` maps each
    struct name to its descriptor and to the `SessionDefinition` the descriptor was made for. While
    the imported definition is unchanged, the cached descriptor is reused. A replaced schema gets a
    new descriptor. A descriptor made by the struct's declaring statement is bound to the
    definition when that statement publishes (`session_structures.cpp`). Instances keep the
    descriptor they were built with, which is legacy's behaviour (`StructInstance::definition`).
- **Deviation from plan §5.5 (approved).** Fields are kept as text, not atoms. Atoms are per isolate
  (`AtomTable` per task), so a descriptor holding atoms would be bound to one isolate and would need
  remapping in every snapshot. With text, the descriptor is isolate-independent, and it stays
  immutable.
- **Pairing invariant.** Instance field i is descriptor field i. The compiler (`StructAdd` in
  `info.fields` order), the transfer decoder (wire order) and copies (source order) all build fields
  in that order.
  - Checked builds (`NIFT_RT_RELOCATE_ON_MUTATION`) abort when a field is added at a declared
    position under another name (`aggregate_store.h` `check_struct_pairing`).
  - A field beyond the declared ones is allowed. Only C++-built test values have one, and it is
    never rendered.
  - In release builds the renderer pairs by position, falls back to pairing by name, and never
    writes a field the descriptor does not declare (fail closed).
  - Tests: `struct_render_unit` `pairing()` covers the release fallback and fail-closed path. In
    checked builds it forks a mispaired construction and requires `SIGABRT`.

**Rendering** (`natives/format_methods.cpp` `serialize_struct`). This is legacy's `serialize_value`
form (`ParserExpression.cpp` around line 651), byte for byte:
- Compact: `Name{a:1,c:[1,2]}`. Public fields only, in the descriptor's order. The field name is
  raw (not JSON-quoted) and the values use the ordinary serializer.
- Pretty: `Name{\n  a: 1,\n  c: [...],\n}`. One field per line at the next depth. Every line ends
  with `,`, the last one too: this is legacy's form, and mutant D6 pins it. The brace closes at the
  parent's indentation.
- No public field: `Name{}` in both forms.
- Nested and contained instances use the usual depth: `Out{i:In{v:2}}`, `[C{..}]`,
  `map([["k",C{..}]])`, `{"x":C{..}}`, `stack([C{..}])`.
- An alias renders at every occurrence, as in legacy (DAGs repeat).
- The depth limit is legacy's: depth above 128 fails with "value serialization depth exceeded",
  and an instance counts as one level. 128 nested instances render and 129 do not
  (`b3/parity/deep_edge.f`, parity with legacy).
- Cycles through instances are refused before they exist (D-NEXT-refsem #4, with legacy's text), so
  scripts never reach a cyclic graph. A C++-built cyclic graph stops with the depth error
  (`struct_render_unit` `cycles()`, built through the isolated test shim only).
- Rendering is observational. It changes no refcount, no heap byte and no field
  (`struct_render_unit` `observe()`; probe `struct-observe`), and it takes no lock, consumes no
  message and awaits nothing.
- **Explicit and implicit presentation** (owner ruling of 2026-11-05: "approve the legacy
  behaviour for B3 … I would explicitly preserve the ability to call those methods normally; only
  implicit presentation should bypass them"):
  - *Implicit* presentation never runs user code. This covers an instance nested in an array,
    object, map, stack or another instance while that is rendered, transfer, and sessions. Free
    functions `stringify(x)` and the like do not exist in Nift (legacy: "undefined callable"), so
    none were added.
    - The guarantee holds by construction. `natives::serialize` takes no `Context`, VM or
      callable; `format_methods.cpp` includes no VM, compiler, proto or session header; and a
      `StructType` carries no methods or code. `b3_structs.py` checks all three structurally.
  - *Explicit* `p.stringify()`, `p.prettify()` and `p.highlight()` with no arguments, on a proven
    struct receiver:
    - When the struct declares a method of that name with no parameters, it runs. This was the VM's
      behaviour before B3. Legacy ignores the method, so this is an approved difference from legacy
      under the owner's ruling.
    - Otherwise the built-in renders the instance, which is legacy parity. A method of that name
      that takes parameters, such as the nift-packages csv/url `stringify(rows)`, is not a
      presentation method: the zero-argument call renders, and `p.stringify(rows)` still calls the
      method.
    - Compiler and inference share one predicate (`resolver.h` `presentation_builtin_binds` and
      `presentation_override`, in `structs.cpp`). Inference types the built-in as a string.
  - *Explicit, on a receiver that is not one proven struct*: a mixed loop element, a parameter, a
    function value's argument, a lookup, or a session value of unknown kind.
    - If some struct of the unit or session declares such a method, the call is refused before any
      effect as `presentation-struct-method:<name>`. Choosing the method or the built-in must happen
      at run time, which is B4's dynamic dispatch, and an explicit call must never silently bypass
      a user method.
    - Otherwise the built-in binds.
  - Only these three names changed. The rest of struct method dispatch (`struct-no-method`) is
    B4's.
- Refusals deleted: `presentation-struct:*` and `presentation-struct-contents:*`
  (`compiler_call.cpp`, `compiler_keyed.cpp`, `refusal_text.cpp`, `UnitCompiler::unit_may_hold_structs`),
  with their `classification.tsv` rows. `presentation-mixed-struct` no longer existed in the source;
  round 4 had replaced it.

**Transfer.**
- `TransferGraph` carries `struct_types_`, a vector of `StructType` held by value, once per
  descriptor per graph. Each Struct node holds an index into it.
- The decoder interns each descriptor in the receiving isolate's `StructTypeTable`, keyed by
  content with the version included. The `AtomTable` owns the table, so it lives exactly as long as
  the isolate's symbol domain.
- Program snapshots copy `Proto::struct_types` by value. The isolate clone (`isolate_task.cpp`
  `clone_proto`) interns them in the task's table.
- No descriptor object crosses a thread. An instance built inside a thread, or sent there and back,
  keeps its name, order and privacy (`b3/equiv/thread.f`, `async.f`; unit `transfer()`).
- Legacy cannot transfer an instance at all ("non-transferable resource"), so these cases are
  VM-only goldens.
- The cooperative scheduler's `coro-struct` refusal (D7) is unchanged and asserted.

**Unchanged in B3, carried to B4** (coordinator ruling 1):
- Session or `nift eval` display of a bare instance: `session:collection-result`, or the run-time
  "struct values have no legacy representation". Legacy shows `C{a:1}`.
- `$[c]` interpolation: refused `interpolation:unknown-type`, or the B0 ordinary error "struct
  instances cannot be rendered as text". Legacy prints its internal tag `\x1fnift:struct:N`, a
  handle leak that is not reproduced.
- `print(c)`: both engines report "print: value is not directly renderable".
- Concatenation and `validate()`: unchanged errors.

Each of these paths has a non-disclosure test (section 3).

## 2. Approved fixture and classification changes (old against new)

Every change is listed. No expectation was changed to make a test pass. Each new expectation is
legacy's output, enforced by the parity harnesses, except where the table says otherwise.

**2.1 Refsem refusals that are now parity** (`tests/runtime2/refsem/refuse/` → `parity/`; i04 and
i05 are in 2.5). The old
outcome of every row: refused before any effect, exit 1, no stdout, with the reason shown. The new
outcome: the VM's stdout, equal to legacy's stdout, stderr and exit, checked by `refsem_a.py`.
Each file gained a B3 header line and keeps its Phase A note.

| Fixture | Old reason | New output (VM = legacy) |
| --- | --- | --- |
| `presentation_struct_array_highlight.f` | `presentation-struct:highlight` | `before\nC{v:0}\n[1]\n` |
| `presentation_struct_array_highlight_length.f` | `presentation-struct:highlight` | `before\n6\n3\n` |
| `presentation_struct_array_prettify.f` | `presentation-struct:prettify` | `before\nC{\n  v: 0,\n}\n[\n  1\n]\n` |
| `presentation_struct_array_prettify_length.f` | `presentation-struct:prettify` | `before\n12\n7\n` |
| `presentation_struct_array_stringify.f` | `presentation-struct:stringify` | `before\nC{v:0}\n[1]\n` |
| `presentation_struct_array_stringify_length.f` | `presentation-struct:stringify` | `before\n6\n3\n` |
| `presentation_struct_closure.f` | `presentation-struct:prettify` | `before\nC{\n  v: 0,\n}\nset([])\n` |
| `presentation_struct_closure_result.f` | `presentation-struct:stringify` | `before\nC{v:0}\n` |
| `presentation_struct_contents_array.f` | `presentation-struct-contents:stringify` | `before\n[C{v:0}]\n` |
| `presentation_struct_contents_array_highlight.f` | `presentation-struct-contents:highlight` | `before\n[C{v:0}]\n` |
| `presentation_struct_contents_array_prettify.f` | `presentation-struct-contents:prettify` | `before\n[\n  C{\n    v: 0,\n  }\n]\n` |
| `presentation_struct_contents_closure.f` | `presentation-struct-contents:prettify` | `before\nmap([\n  ["a", C{\n    v: 0,\n  }]\n])\n` |
| `presentation_struct_contents_function.f` | `presentation-struct-contents:stringify` | `before\n[C{v:0}]\n` |
| `presentation_struct_contents_length.f` | `presentation-struct-contents:stringify` | `before\n8\n` |
| `presentation_struct_contents_map.f` | `presentation-struct-contents:stringify` | `before\nmap([["a",C{v:0}]])\n` |
| `presentation_struct_contents_nested.f` | `presentation-struct-contents:stringify` | `before\n[map([["a",C{v:0}]])]\n` |
| `presentation_struct_contents_object.f` | `presentation-struct-contents:stringify` | `before\n{"k":C{v:0}}\n` |
| `presentation_struct_contents_stack.f` | `presentation-struct-contents:stringify` | `before\nstack([C{v:0}])\n` |
| `presentation_struct_fn_arg.f` | `presentation-struct:prettify` | `before\nC{\n  v: 0,\n}\n` |
| `presentation_struct_fn_return.f` | `presentation-struct:stringify` | `before\nC{v:0}\n` |
| `presentation_struct_function.f` | `presentation-struct:stringify` | `before\n6\n7\n` |
| `presentation_struct_index.f` | `presentation-struct:prettify` | `before\nC{\n  v: 0,\n}\n` |
| `presentation_struct_loop_binding.f` | `struct-no-method` | `before\n6\n` |
| `presentation_struct_loop_element.f` | `presentation-struct:highlight` | `before\nC{v:0}\nC{v:0}\n` |
| `presentation_struct_map_get.f` | `presentation-struct:highlight` | `before\nC{v:0}\n` |
| `presentation_struct_map_highlight.f` | `presentation-struct:highlight` | `before\nC{v:0}\nmap([])\n` |
| `presentation_struct_map_highlight_length.f` | `presentation-struct:highlight` | `before\n6\n7\n` |
| `presentation_struct_map_prettify.f` | `presentation-struct:prettify` | `before\nC{\n  v: 0,\n}\nmap([])\n` |
| `presentation_struct_map_prettify_length.f` | `presentation-struct:prettify` | `before\n12\n7\n` |
| `presentation_struct_map_stringify.f` | `presentation-struct:stringify` | `before\nC{v:0}\nmap([])\n` |
| `presentation_struct_map_stringify_length.f` | `presentation-struct:stringify` | `before\n6\n7\n` |
| `presentation_struct_review_j02.f` | `presentation-struct:stringify` | `before\n6\nC{v:0}\n7\nmap([])\n` |
| `presentation_struct_review_stringify_struct_mixed.f` | `presentation-struct:stringify` | `side effect before\nmap([])\nC{v:0}\n` |
| `presentation_struct_set_highlight.f` | `presentation-struct:highlight` | `before\nC{v:0}\nset([])\n` |
| `presentation_struct_set_highlight_length.f` | `presentation-struct:highlight` | `before\n6\n7\n` |
| `presentation_struct_set_prettify.f` | `presentation-struct:prettify` | `before\nC{\n  v: 0,\n}\nset([])\n` |
| `presentation_struct_set_prettify_length.f` | `presentation-struct:prettify` | `before\n12\n7\n` |
| `presentation_struct_set_stringify.f` | `presentation-struct:stringify` | `before\nC{v:0}\nset([])\n` |
| `presentation_struct_set_stringify_length.f` | `presentation-struct:stringify` | `before\n6\n7\n` |
| `presentation_struct_stack_pop.f` | `presentation-struct:stringify` | `before\nC{v:0}\n` |
| `presentation_struct_struct_scalar.f` | `presentation-struct:stringify` | `before\nC{v:0}\n5\n` |
| `presentation_struct_temporary.f` | `presentation-struct:stringify` | `before\nC{v:0}\n` |
| `ret_multiline_http_shape.f` | `presentation-struct-contents:stringify` | `{"buffered_text":true,"json":true,"streaming":false}\n` |

`presentation_struct_contents_set.f` stays refused under its own unchanged reason,
`collection-identity-key`: an instance as a set member, since identity keys are deferred (owner Q5).

**2.2 Sessions** (`refsem_a.py` SESSIONS):

| Case | Old | New | Reason |
| --- | --- | --- | --- |
| `session_struct_value` | refused `presentation-struct:stringify` after `before` | parity: `before`, `x`, `C{v:0}` | B3 renders instances |
| `session_struct_contents` | refused `presentation-struct-contents:stringify` | parity: `before`, `x`, `[C{v:0}]` | B3 renders instances |
| `cp19_unrelated_callback_shadow` (new parity session) | `refsem/pending/` refused `presentation-struct-contents:stringify` | parity: `[2,3]`, `0` | the pending case admitted |

**2.3 Pending cases admitted** (plan B3 "pending admitted"):

| Case | Old | New |
| --- | --- | --- |
| `refsem/pending/ret_multiline_http_shape.f` (nift-packages/http `capabilities()`) | refused `presentation-struct-contents:stringify` (the round-4 F3 over-refusal) | `refsem/parity/ret_multiline_http_shape.f`, printing `{"buffered_text":true,"json":true,"streaming":false}` (= legacy, = the former `.expected`) |
| `refsem/pending/cp19_unrelated_callback_shadow.session` | refused | refsem parity session (2.2). The original program is restored in `cp19/session_struct_cli.py` as `unrelated-callback-shadow` (3 units, `[2,3]` and `0`, all three engines equal); the 2026-11-01 index form is kept as `unrelated-callback-shadow-index` |

**2.4 Module cases** (`refsem/modules/`):

| Case | Old | New | Reason |
| --- | --- | --- | --- |
| `presentation_struct` | `reason` `presentation-struct:highlight` | `phaseb`: `before`, `C{v:0}`, `map([])` (VM = legacy) | B3 renders instances |
| `presentation_struct_contents` | `reason` `presentation-struct-contents:stringify` | VM difference **approved by the owner, 2026-11-05**: `vm` `before`, `[C{v:0}]`; `legacy` exit 1, `return: undefined callable: C` | Root cause (review M1): legacy cannot construct its own module's struct from an exported function called by the importer ("undefined callable: C"), while the VM constructs it. That difference **predates B3**: the pre-B3 VM constructs it too (review probe `m5`, base binary). It is recorded in compatibility.md ("Documented compatibility differences"). B3 only lets the instance render. The old comment, "legacy: `[C{v:0}]`", was wrong and is corrected in the fixture. **Approved by the owner, 2026-11-05**: correct VM behaviour, the legacy defect is not reproduced; a documented compatibility difference, not a release blocker. |
| `presentation_struct_expression` | `reason` `presentation-struct:stringify` | VM difference **approved by the owner, 2026-11-05**: `vm` `before`, `C{v:0}`; `legacy` exit 1, `return: undefined callable: C` | as above; **approved by the owner, 2026-11-05** |

**2.5 Explicit presentation calls on structs** (owner ruling of 2026-11-05; section 1).

*Approved difference from legacy, not a change to admitted output.* The fixture is
`tests/runtime2/b3/vm/user_method.f`: a struct declaring `fn(stringify()) { return 7 }` and the
same for `prettify` and `highlight`, called as `c.stringify()`, `c.prettify()` and `c.highlight()`.

| | Output |
| --- | --- |
| VM before B3 and after | `7`, `8`, `9`: the struct's own methods run |
| Legacy | `C{v:0}`, `C{\n  v: 0,\n}`, `C{v:0}`: legacy ignores them |

`b3/vm/explicit_after_implicit.f` shows the two kinds of call side by side. Implicit renders print
no `SIDE-EFFECT` line and show `n:0`; then the explicit call runs once; then an implicit render
shows `n:1`.

*Approved change: a refused program becomes admitted with legacy's output.* An explicit call on a
proven struct that declares no such method was refused as `struct-no-method` and now renders:

| Fixture | Old | New |
| --- | --- | --- |
| `refsem/parity/presentation_struct_loop_binding.f` | refused `struct-no-method` | parity |
| the B3 parity fixtures calling `c.stringify()` on an instance | (new) | parity |

The CP25 corpus case `smoke/v42_struct_private_smoke/014-serialize/serialize.f`
(`v.stringify()`, legacy `vault{shown:3}`) is the same shape. Its `struct-no-method` row is
annotated, and a whole-corpus classify comes with the combined certification.

*Classification changes: a parity fixture becomes refused under the new reason.*

| Fixture | Old (Phase A) | Old (earlier B3 tree, = legacy) | New | Justification |
| --- | --- | --- | --- | --- |
| `refsem/refuse/presentation_struct_review_i05.f` (a mixed loop over `[C(), map()]`; `C` declares `fn(stringify())` returning 7) | refused `presentation-struct:stringify` | `before`, `6`, `7` (the built-in) | refused `presentation-struct-method:stringify` | an explicit call that may hit a user method needs run-time dispatch (B4); refusing it is better than quietly giving it a different meaning |
| `refsem/refuse/presentation_struct_review_i04.f` (a function value called with `C()` and with `map()`; `C` declares `stringify` returning `[1,2,3]`) | refused `presentation-struct:stringify` | `before`, `6`, `7` | refused `presentation-struct-method:stringify` | the same shape through a function value. The ruling's text named i05 only; this reclassification was **approved by the owner, 2026-11-05**, under the same conservative rule (review M1) |

The new reason has a classification row (`presentation-struct-method:*`, M, pending B4 dynamic
dispatch) and a user-facing message: "`.stringify()` on a value that may be an instance of a
struct declaring its own `stringify()` method (choosing the method or the built-in at run time)
is not supported by the Runtime 2.0 VM yet".

**2.6 Classification** (`tests/runtime2/cp25/classification.tsv`):
- The rows `presentation-struct:*` and `presentation-struct-contents:*` are removed, because
  nothing emits those reasons any more.
- `presentation-struct-method:*` is added (2.5).
- `struct-no-method` is annotated (2.5).

A classify pass over every struct-declaring fixture (91 files, on the final tip) gave 81 compiled
and 10 refused:
- `type:+`, `interpolation:unknown-type` and `struct-private` (the `b3/disclose` cases);
- `presentation-struct-method` (four cases: the two `b3/refuse` fixtures, i04 and i05);
- `collection-identity-key` (two cases);
- `struct-value` (one case).

No `presentation-struct:` or `presentation-struct-contents:` reason remains
(`scratchpad/b3/classify-structs.txt`).

No other expectation changed. The `safety/expected.json` struct interpolation and `validate` rows
are unchanged.

## 3. Tests

- **`tests/runtime2/b3_structs.py`** (`make test-runtime2-b3`: 239 checks, plus the unit).
  - Every fixture keeps a private value containing `SECRET-b3`. Every output of both engines on
    every path is checked not to contain it.
  - Every output of an implicit rendering is checked not to contain `SIDE-EFFECT`, which a
    struct's own presentation methods print.
  - The fixture categories:
    - `parity/` (12 fixtures): VM = legacy (stdout, stderr, exit) and run on the VM. They cover:
      - forms: all scalar kinds, all-private, empty;
      - containers: array, object, map, stack; compact and pretty;
      - nesting: instance fields, private instance fields;
      - aliases, copies and deepcopy;
      - the depth limit: 60 deep; exactly 128 and 129; 141;
      - a refused cycle;
      - errors carrying a presentation;
      - side-effecting user methods on every implicit path (`implicit_methods.f`);
      - methods with parameters named like presentation built-ins (`method_with_params.f`).
    - `disclose/` (8): paths B3 leaves refused or erroring. These are interpolation (direct,
      through a parameter, a mixed loop element), print, concatenation, `validate`, a field type
      error and a private field read from outside. The VM must not accept them, and nothing
      private may appear on either engine.
    - `equiv/` (3: `thread.f`, `async.f`, `thread_methods.f`): parallel goldens.
      - These send an instance to a thread, return it and also render it inside the thread; they
        construct instances inside the thread or task; and they render instances with
        side-effecting user methods across the thread.
      - The cooperative scheduler must refuse `coro-struct` before effects.
    - `vm/` (3): VM goldens where legacy differs.
      - `cycle_caught.f`: after a catchable refused cycle, rendering terminates. Legacy's error is
        fatal there (the established catchability difference).
      - `user_method.f` and `explicit_after_implicit.f`: the owner's ruling (2.5).
    - `refuse/` (2): explicit calls on mixed receivers, `presentation-struct-method`.
  - Sessions (10, through `nift -i`):
    - schema replacement with swapped privacy (D3);
    - a schema replaced twice;
    - one schema reused across statements;
    - the former Phase A session cases;
    - presentation expression statements;
    - implicit presentation of side-effecting methods;
    - an explicit call on a mixed session value (refused);
    - bare display and `$[c]` stay refused, with nothing disclosed.
  - Structure: the serializer cannot reach VM code (section 1).
- **`tests/rt/struct_render_unit.cpp`** (59 checks):
  - descriptors, versions, content interning, one table per isolate (`AtomTableScope`);
  - the forms;
  - a transfer by value and interned, across isolates and back;
  - copies keep the descriptor;
  - a C++ cycle stops with the depth error, and the 128/129 boundary;
  - observational rendering;
  - pairing and fail-closed (release), and the abort (checked builds).
- **Probe configuration (guard off)**, `tests/rt/refrep_vm.cpp` `structs()`. These cases cover
  what production refuses under the reference guard:
  - arrays and objects of instances shared through aliases and mutated through them;
  - a DAG of instances inside shared arrays;
  - cycles closed through aliases of instances, and self-links, refused and then rendered;
  - observational rendering through aliases;
  - copy and deepcopy of shared arrays of instances;
  - a probe `VmSession` where an aliased array of instances spans a schema replacement.

  The probe uses cooperative threads, so the thread round trip is covered by `b3/equiv`, not here.
  `refrep-vm`: 129 checks, 0 failures.
- **Converted refsem fixtures** (section 2): 43 parity (42 from refuse plus the http shape), 2 back
  to refuse under `presentation-struct-method` (i04, i05), 1 phaseb, 2 VM-difference modules, 3
  parity sessions, and the restored CP19 case.
- **Website builds.** A fresh `nift init` site built with `nift build --all` and `NIFT_VM_STATS`
  set wrote no stats file, so no VM unit was compiled or run. B3 touches no template path.

## 4. Mutants (`make test-runtime2-b3-mutants`; each must fail the B3 suite or the unit)

| Mutant | Defect | Caught by |
| --- | --- | --- |
| D1 `format_methods.cpp` | rendering writes private fields | suite (45 failing checks) and unit |
| D2 `isolate_transfer_decode.cpp` | a transfer drops the descriptor's privacy (an all-public descriptor is interned) | suite (7) and unit |
| D3 `compiler_structs.cpp` | a replaced session schema rewrites the old instances' descriptor in place | suite (4: the schema sessions) |
| D4 `copy_graph.cpp` | copy/deepcopy give the copy an all-public descriptor (an alias or copy leaking privacy) | suite (3) and unit |
| D5 `isolate_task.cpp` | a program crossing into an isolate loses its descriptors' privacy (instances built inside a thread) | suite (5) |
| D6 `format_methods.cpp` | the pretty form drops the comma after each field line | suite (13) and unit |
| D7 `format_methods.cpp` | rendering walks the instance's fields unpaired with the descriptor (an undeclared field is written) | unit (the fail-closed check; scripts always build paired instances) |
| D8 `compiler_structs.cpp` | rendering a container compiles to a struct's own presentation method (implicit presentation runs user code) | suite (6: the SIDE-EFFECT checks in `implicit_methods.f`, `thread_methods.f`, `explicit_after_implicit.f`) |
| D9 `compiler_structs.cpp` | an explicit call on a mixed receiver binds the built-in despite a user presentation method | suite (3: `b3/refuse/presentation_method_mixed.f`, `presentation_method_param.f`, the `explicit-mixed` session) |

All 9 are caught (log `scratchpad/b3/logs/gate-f-test-runtime2-b3-mutants.log`, on `151fbd50`).

## 5. Local performance (callgrind, guidance only; Linode is authoritative in the B3–B5 certification)

Marginal Ir per operation between N = 4,000 and 8,000 (`scratchpad/b3/bench/`). The base is
`0b5079fc`; the tip measured is `a89c8d63`. The later commits change compile-time dispatch and
tests only, not the measured paths.

| Workload | Base | Tip | Change |
| --- | --- | --- | --- |
| construction: a function call building a 3-field struct (1 private) | 2,570 | 2,596 | +1.0% (+1.2% net of the 407 Ir call baseline); the limit is +5% |
| field read and write through a method | 1,359 | 1,359 | 0 |
| `thread()` transfer of an array of instances (incl. building it) | 9,898 | 9,816 | −0.8% (no per-node name atom interning on decode) |
| control: the same loop without structs | 407 | 407 | 0 |
| rendering: `stringify().length()` of an instance (2 public fields, 1 private) | refused | 2,754 (2,234 net of the 520 call loop) | new |
| control: the same for an equivalent object | 2,883 | 2,881 | 0 |

Struct rendering costs about the same as rendering an object with the same members.

## 6. Gates (local; result files in `scratchpad/b3/gates/`)

Every gate ran through `scratchpad/b3/gate.sh`. Each result file records:
- the head and tree;
- the sha256 of the worktree diff (empty on every gate);
- the command;
- start and end times;
- the exit status;
- the log path and its sha256;
- `git status` before and after (clean on every gate).

**Final gates on `151fbd50`** (tree `2b9a1a55`; prefix `f-`): 32 gates, all exit 0.

| Gate | Exit | Log sha256 (prefix) | Last line |
| --- | --- | --- | --- |
| `test-c-abi-symbols` | 0 | `b7ebe5c6bb71dbdf` | PASS abi-consumer: built outside the tree, linked dynamically, 34 nift_* symbols resolved from the librar |
| `test-cp19-functional` | 0 | `db43faa71dc90a22` | PASS retained-reader numeric guard and legacy channels |
| `test-cp19-session-cli` | 0 | `f06e22c578f7f6ed` | PASS retained-reader numeric guard and legacy channels |
| `test-production-deps` | 0 | `82e4c024f181e59a` | PASS production-deps: 1473 production compile commands, no dependency in tests/ (-M, system headers inclu |
| `test-rt-build` | 0 | `8a21f7218a32be34` | PASS builder mutant B-M2 caught: 4 observed changes to a published value (rt build unit (release): 207 ch |
| `test-rt-isolation` | 0 | `03af9dd6acdee271` | PASS isolation: production cannot reach the storage-level test helpers |
| `test-rt-lint` | 0 | `05667f070a4ebbe8` | PASS rt-lint boundary: 0 problem(s) in 691 files (src, include, bindings, markuppp, minifypp and their in |
| `test-rt-mutation-protocol-mutants` | 0 | `55e01a07fd95a2a9` | PASS M-B1 mutant 6 caught: tests/rt/mutation_protocol.cpp:399: CHECK failed: injected && unchanged |
| `test-rt-mutation-protocol` | 0 | `59c0b9b30c28b428` | mutation-protocol: 191 checks, 0 failures |
| `test-rt-protocol-compile-fail` | 0 | `8c137822ce5863b5` | PASS compile-fail control compiles |
| `test-rt-refrep-mutants` | 0 | `1a682dabebcf8ec9` | PASS refrep mutant 31 (src/rt/vm_report.cpp) caught:   case literal-nested-json: refusal '' output [{"a": |
| `test-rt-refrep` | 0 | `8516f09ea3370a7f` | refrep-vm: 129 checks, 0 failures |
| `test-rt-value` | 0 | `622507e7073a1ba4` | heap-limit-zero: PASS |
| `test-runtime2-args-cmd-mutants` | 0 | `509ad0b2f349d35e` | PASS mutant 6 (src/rt/compiler_invocation.cpp): caught, 15 failing check(s), first: FAIL read args before |
| `test-runtime2-args-cmd` | 0 | `33b9a4e0c103afa5` | PASS heap: cmd ends with the live heap of the string literal |
| `test-runtime2-b3-mutants` | 0 | `ce63657615947cba` | PASS b3 mutants: 9 caught |
| `test-runtime2-b3` | 0 | `1c6857447f031d80` | PASS b3-structs: 239 checks, 0 failure(s) |
| `test-runtime2-collections-mutants` | 0 | `76cfd33999d1f36c` | PASS collections mutants: 11 caught |
| `test-runtime2-collections` | 0 | `d4dc480404d01726` | PASS collections: 279 checks, 0 failure(s) |
| `test-runtime2-coro-module` | 0 | `3a9eda6117e9840a` | PASS coro-module: 0 failure(s) |
| `test-runtime2-cp14b-design-mutants` | 0 | `2fb533be21a7c59e` |  |
| `test-runtime2-cp14b` | 0 | `858aadd7ad3275d7` | UNVERIFIED original_uncaught |
| `test-runtime2-cp22` | 0 | `6a4dee80d61a20de` | PASS cp22 print-order evidence |
| `test-runtime2-cp23` | 0 | `d1e61f283e4e1be6` | PASS cp22 default: 87 case(s), 0 failure(s) |
| `test-runtime2-packages-mutants` | 0 | `7628b8c5f9157e62` | PASS mutant 25 (src/rt/module_graph.cpp): caught, 3 failing check(s), first: FAIL S c11 args exported bef |
| `test-runtime2-packages` | 0 | `185ae868c5fcd1a3` | packages: 828 checks, 0 failed |
| `test-runtime2-refrep-cli` | 0 | `72294fa932c1dbca` | PASS refrep-cli: 7 checks, 0 failure(s) |
| `test-runtime2-refsem-mutants` | 0 | `a14e07a223118d34` | PASS refsem mutants: 23 caught |
| `test-runtime2-refsem` | 0 | `3af0a7deb88d68fb` | PASS refsem-a: 473 checks, 0 failure(s) |
| `test-runtime2-safety-mutants` | 0 | `88dca510ff831d07` | PASS safety mutants: 7 caught |
| `test-runtime2-safety` | 0 | `434be8b605386f01` | PASS runtime2 safety fuzz: 33512 programs (8512 exhaustive, 20000 sampled, 5000 session; seed 1; 1676 run |
| `test-runtime2-vm-default` | 0 | `cf7faf09d55f50c1` | PASS VM-default policy |

**On `ffeecb9b`** (tree `a0652d6d`; prefix `fs-`). This commit changes only
`tests/rt/struct_render_unit.cpp`: the checked-build branch now sees the switch. The B3 suite and
the sanitizer lanes (ASan+UBSan, leak detection on):

| Gate | Exit | Log sha256 (prefix) | Last line |
| --- | --- | --- | --- |
| `test-runtime2-b3-sanitize` | 0 | `691c2724cee29970` | PASS b3-structs: 239 checks, 0 failure(s) |
| `test-runtime2-b3` | 0 | `1c6857447f031d80` | PASS b3-structs: 239 checks, 0 failure(s) |
| `test-runtime2-collections-sanitize` | 0 | `28fa2842feb22cfb` | PASS collections: 279 checks, 0 failure(s) |
| `test-runtime2-refsem-sanitize` | 0 | `42c2b1596da5d1a3` | PASS refsem-a: 473 checks, 0 failure(s) |

**Earlier runs** (superseded, kept as files):
- `c1-`: commit 1 (`90ecc5ca`, the plumbing), 22 gates, all exit 0.
- `t-`: `a89c8d63`, before the owner's ruling, 32 gates, all exit 0.
- A sanitizer run on `a89c8d63` was stopped when the ruling arrived.
- The first ASan lane on `151fbd50` (`fs-test-runtime2-b3-sanitize` before `ffeecb9b`) failed in
  the unit. The checked-build pairing test had taken its release branch because the test did not
  see `NIFT_RT_RELOCATE_ON_MUTATION`, and the correct abort fired. Fixed in `ffeecb9b`. Its log
  is overwritten by the rerun; the failure is recorded here.

Not run locally, by the batching ruling (the combined B3–B5 Linode certification):
- full `make test`;
- `test-rt-vm-sanitize`;
- `test-build-reuse`;
- perf campaigns;
- classify over the whole corpus;
- the TSan lanes.

**Tooling boundaries:**
- Construction: instances are built through `make_struct`/`build::Struct`, through the VM's
  existing in-place `StructAdd` key, or by the transfer decoder (a sanctioned construction site).
  No new storage annotation was added.
- Mutation: field writes go through `mutation::field_set`, unchanged.
- Lint: `rt-lint` is clean, and the boundary manifest is unchanged at 41 annotations.
- Test-only code: there are no test-only defines in production. `NIFT_B3_MUTANT` follows the
  existing mutant-define convention (`NIFT_REFSEM_MUTANT` and the others) and is absent from every
  production build. The test shim `test_struct_type()` is in the isolated
  `tests/rt/storage_access.h`.

## 7. Carried items

- **To B4 (presentation):**
  - session or `nift eval` display of a bare struct instance (legacy `C{a:1}`; the VM refuses or
    errors);
  - `$[instance]` interpolation (legacy's internal tag is not reproduced; the VM refuses or errors).
  - Both are under non-disclosure tests today.
- **To B4 (N3):** detecting `this` by its text over-refuses (`for (this : xs) { this.stringify() }`).
- **To B4 (inference):** alias-sound inference in general; B3 fixed only the positional-kind
  invalidation hole (N1).
- **To B4 (dispatch):** only the three presentation names moved. A proven struct receiver calling
  any other built-in name keeps `struct-no-method`.
- **To B4 (dynamic dispatch):** `presentation-struct-method:<name>`. An explicit
  stringify/prettify/highlight on a receiver that may be an instance of a struct declaring that
  method needs run-time selection between the method and the built-in. Refused in B3; covers
  refsem i04 and i05.
- **Decisions already taken:** the explicit/implicit presentation rule (owner, 2026-11-05;
  section 2.5).
- **Decisions taken (owner, 2026-11-05):** the two module classification changes (2.4: correct VM
  behaviour, documented compatibility differences, not release blockers) and i04's reclassification
  (2.5, the same conservative rule as i05).
- **Combined certification, performance review (review L2):** the per-isolate `StructTypeTable`
  grows without bound and is searched linearly.
  - Versions are process-unique per compile, and the main thread's `AtomTable` is a
    `thread_local` that is never released. A long-lived session or host that repeatedly compiles
    and round-trips instances through threads therefore keeps every descriptor it ever decoded.
  - Each descriptor of each decoded graph is interned by an O(entries) scan.
  - Not fixed in B3. A version-keyed `unordered_map` of weak references would bound both.
- **Release audit (review L4, predates B3):** an integer struct field accepts `1e3` and `3.0` on
  the VM (prints 1000 / 3), where legacy refuses with "cannot change struct field type: z"
  (review probe `ft.f`). The base binary behaves the same.
- **Descriptor memory:** compile-time and decoded descriptors are not charged to the VM heap
  budget. Instances hold one shared reference each. An isolate's `StructTypeTable` grows with the
  distinct descriptors it receives: bounded per program, unbounded across a long session (L2
  above).
- **Not run locally** (combined B3–B5 Linode certification):
  - full `make test`;
  - `test-rt-vm-sanitize`;
  - `test-build-reuse`;
  - perf campaigns;
  - classify over the whole corpus;
  - the TSan lanes.

## 8. Independent review of `f24c292e` and its fixes

The verdict was ACCEPT-WITH-CONDITIONS
(`scratchpad/review-b3/REVIEW.md`):
- no private-field disclosure on any path;
- 500 generated programs byte-equal to legacy;
- the 127–130 depth boundary equal to legacy;
- all 9 mutants caught.

The findings and their fixes (`804858a9`):

- **H1 (high), fixed.**
  - *Defect.* In a session, an explicit presentation call on an unproven receiver checked only
    the structs of the current statement's plan. So `d := c; d.stringify()` silently bound the
    built-in although an earlier statement had declared `C` with a user `stringify()`.
  - *Fix.* `SessionCompileContext::presentation_methods` collects, when each statement
    publishes, every public zero-parameter presentation method name that any session struct
    schema has declared (`session_structures.cpp`). The set never shrinks, because instances of
    a replaced schema stay reachable. `presentation_dispatch` refuses when the name is in it.
  - *Inference.* No change is needed: it types the built-in's string result only where the
    compiler binds the built-in (proven receivers through the shared predicate). An unproven
    receiver is now refused before inference matters.
  - *Modules.* Already covered, because a linked unit's plan includes the imported modules'
    structs. `b3/modules/visible_method` (refused) and `proven_method` (the user method runs)
    pin it.
  - *Session tests* (`b3_structs.py` SESSIONS `h1-*`): an alias, an index, an object lookup (the
    first form, a map lookup, was refused for an unrelated reason, `session:collection-result`, and
    was replaced in the re-review round, N2), a
    session function defined after the struct, and one defined before it (recompiled with the
    session's later struct and refused when called). Schema replacement removing the method: the
    proven old instance runs its own method, and an alias is refused. Schema replacement adding
    it: the proven old instance renders, the new one runs the method, and an alias is refused.
  - *Mutant D10* (the session's schemas ignored) was caught by four checks (`h1-alias`,
    `h1-index`, `h1-replace-remove`, `h1-replace-add`; `h1-function-*` refuse without the set,
    because the function is recompiled with C in the plan). After N2, seven checks catch it. The mutant suite
    covers D1–D10, all caught.
- **M1 (medium), fixed in wording.**
  - The two module fixtures, the record (2.4) and compatibility.md now say "pending owner
    approval". The root cause is recorded as a difference that predates B3: legacy cannot
    construct its own module's struct from an exported function called by the importer.
  - i04's reclassification is marked pending confirmation (2.5).
  - Both have since been approved by the owner (section 9) and the wording updated.
- **L3 (low), fixed.**
  - A private presentation method is a user method only through `this`. From outside it is not
    callable, so the built-in renders (legacy parity) and the method never runs. This holds for
    proven, container and mixed receivers (`b3/parity/private_method.f`, session `l3-private`).
  - Inside the struct, `this.stringify()` runs it (`b3/vm/private_method_inside.f`; the owner's
    explicit-call rule).
  - The H1 predicate counts only public methods.
- **L1 (low), fixed.** A short privacy mask pads with private, both when made and when interned
  (unit `descriptor()`). The D4 mutant now passes its all-public mask explicitly.
- **L2 (low), carried** to the combined certification's performance review (section 7).
- **L4 (low, predates B3), carried** to the release audit (section 7).

**Gates of the second freeze** (`7b752464`, tree `41c80903`; prefix `r-`; 26 gates, all exit 0, git
status clean before and after):

| Gate | Exit | Log sha256 (prefix) | Last line |
| --- | --- | --- | --- |
| `test-c-abi-symbols` | 0 | `9585de506dd0956f` | PASS abi-consumer: built outside the tree, linked dynamically, 34 nift_* symbols resolved from the l |
| `test-cp19-functional` | 0 | `c88b2a2ba63c8504` | PASS retained-reader numeric guard and legacy channels |
| `test-cp19-session-cli` | 0 | `f06e22c578f7f6ed` | PASS retained-reader numeric guard and legacy channels |
| `test-production-deps` | 0 | `82e4c024f181e59a` | PASS production-deps: 1473 production compile commands, no dependency in tests/ (-M, system headers  |
| `test-rt-build` | 0 | `401bfdcd972d970b` | PASS builder mutant B-M2 caught: 4 observed changes to a published value (rt build unit (release): 2 |
| `test-rt-isolation` | 0 | `b5236fbd372719f5` | PASS isolation: production cannot reach the storage-level test helpers |
| `test-rt-lint` | 0 | `05667f070a4ebbe8` | PASS rt-lint boundary: 0 problem(s) in 691 files (src, include, bindings, markuppp, minifypp and the |
| `test-rt-mutation-protocol` | 0 | `59c0b9b30c28b428` | mutation-protocol: 191 checks, 0 failures |
| `test-rt-protocol-compile-fail` | 0 | `8c137822ce5863b5` | PASS compile-fail control compiles |
| `test-rt-refrep` | 0 | `8516f09ea3370a7f` | refrep-vm: 129 checks, 0 failures |
| `test-rt-value` | 0 | `c1827ae7390a36e7` | heap-limit-zero: PASS |
| `test-runtime2-args-cmd` | 0 | `33b9a4e0c103afa5` | PASS heap: cmd ends with the live heap of the string literal |
| `test-runtime2-b3-mutants` | 0 | `894817edaffb5ca1` | PASS b3 mutants: 10 caught |
| `test-runtime2-b3-sanitize` | 0 | `0d592dc505581087` | PASS b3-structs: 302 checks, 0 failure(s) |
| `test-runtime2-b3` | 0 | `47cb0602220d018f` | PASS b3-structs: 302 checks, 0 failure(s) |
| `test-runtime2-collections` | 0 | `d4dc480404d01726` | PASS collections: 279 checks, 0 failure(s) |
| `test-runtime2-coro-module` | 0 | `3a9eda6117e9840a` | PASS coro-module: 0 failure(s) |
| `test-runtime2-cp14b` | 0 | `858aadd7ad3275d7` | UNVERIFIED original_uncaught |
| `test-runtime2-cp22` | 0 | `9c6dfd720f4766db` | PASS cp22 print-order evidence |
| `test-runtime2-cp23` | 0 | `1009b564354216da` | PASS cp22 default: 87 case(s), 0 failure(s) |
| `test-runtime2-packages` | 0 | `185ae868c5fcd1a3` | packages: 828 checks, 0 failed |
| `test-runtime2-refrep-cli` | 0 | `72294fa932c1dbca` | PASS refrep-cli: 7 checks, 0 failure(s) |
| `test-runtime2-refsem-mutants` | 0 | `a14e07a223118d34` | PASS refsem mutants: 23 caught |
| `test-runtime2-refsem` | 0 | `3af0a7deb88d68fb` | PASS refsem-a: 473 checks, 0 failure(s) |
| `test-runtime2-safety` | 0 | `434be8b605386f01` | PASS runtime2 safety fuzz: 33512 programs (8512 exhaustive, 20000 sampled, 5000 session; seed 1; 167 |
| `test-runtime2-vm-default` | 0 | `309385cb80a90fde` | PASS VM-default policy |

## 9. Re-review of `7b752464` and its fixes

The verdict was ACCEPT-WITH-CONDITIONS (`scratchpad/review-b3/REREVIEW.md`). H1, L3, L1 and M1
were verified, with no regressions. The findings:

- **N1 (medium; predates B3, but breaks B3's rule), fixed in `058b23e0`.**
  - *Defect.* `infer_collection.cpp` invalidated an array's per-position element kinds after a
    mutator only in sessions. So in a script, `xs := [C(), map()]; xs.reverse(); d := xs[0]`
    proved `d` a `C`, and `d.stringify()` ran C's own method on the map (legacy: `map([])`). The
    same happened after `remove`, `pop` and two-argument `splice`, for ordinary methods (`d.get()`
    ran C's method) and for field reads (the VM's dynamic field check failed instead). The base
    binary does the same.
  - *Fix.* The invalidation applies in scripts and modules too.
  - *Audit of every array mutator* (`CollectionMethod` below `Map`):
    - push, insert and splice with a replacement already widened the element type and dropped
      positions (`add_element`);
    - pop, remove, clear, splice with one or two arguments, reverse, sort and comparator sort now
      do too;
    - there is no shift or unshift;
    - index stores (`xs[0] = v`) and object stores already invalidate (probed: refused);
    - an alias (`ys := xs; ys.reverse()`) is refused by the reference guard;
    - a function mutating a global array invalidates it (probed: refused).
  - *Tests.*
    - `b3/refuse/n1_<mutator>_<use>.f`: reverse, remove, pop, clear, splice with one and with two
      arguments, and insert, each with a presentation method, an ordinary method and a field read.
      That is 21 scripts, each refused with its own reason: `presentation-struct-method:stringify`,
      `builtin-receiver:get`, `type:member`.
    - `b3/modules/n1_*`: seven module cases (every mutator with a presentation method; reverse
      also with a method and a field).
    - `sort` and comparator `sort` over an array holding instances are refused earlier for their
      own reasons (`collection-element:sort`, `method:sort`), so they are not fixtures.
  - *Mutant D11* (the session-only guard restored) is caught by 15 checks. The mutant suite now
    covers D1–D11, all caught.
  - *Classify* over the B3, refsem and collections fixtures (387 files, pre-fix binary = D11
    against the fix): only the nine new N1 fixtures change, from compiled to refused (reverse,
    remove and splice2, each with present, method and field). No existing fixture changes
    admission (`scratchpad/b3/probe/d11/classify-compare.txt`).
  - Related: the reviewer's `z2` (`xs := [[1,2,3], "ab"]; xs.reverse(); xs[0].length()`), which
    was a pre-existing VM internal failure (exit 70, unexpected operands), is now refused before
    any effect (`type:index`).
  - *Performance* (local callgrind, pre-fix binary against the fix): `run.py --groups ops` and
    `vm_compare.py --groups mutation` are identical:
    - loop 117, add_local 188, call 566, array_index 436, array_push 367, object_member 762,
      string_concat 3313, if_branch 249, fib_call 393;
    - array_set 381, array_push_pop 1430, array_insert_pop 2446, map_set 1496, set_add 1363,
      stack_push_pop 2190.

    Positional kinds exist only for array literals, so ordinary hot loops are unaffected.
  - *Carried to B4:* alias-sound inference in general (plan 5.7) stays B4's. This fixes only the
    invalidation hole.
- **N2 (low), fixed.** Every refused session case in `b3_structs.py` asserts its refusal reason.
  `h1-lookup` now uses an object lookup and is refused for H1's reason.
- **N3 (low), carried to B4.** Detecting `this` by its source text over-refuses:
  `for (this : xs) { this.stringify() }` outside any struct passes the text test and is then
  refused (`struct-private`), where legacy renders. Safe, since nothing runs and nothing is
  disclosed.
- **N4 (wording), fixed.**
  - compatibility.md now attributes "a private one only through `this`" to the review L3 fix (a
    coordinator ruling applying legacy parity), not to the owner's ruling.
  - The record header's stale row and the gate-commit claims are corrected.
  - D10's count is corrected.
  - The second freeze's gate results are folded in (section 8).
- **Owner rulings of 2026-11-05 (after the re-review):**
  - The two module cases are approved as correct VM behaviour (the legacy defect is not
    reproduced). They are documented compatibility differences, not release blockers.
  - i04 is approved under the same conservative rule as i05.
  - The fixtures, 2.4, 2.5, section 7 and compatibility.md now say so.
- **N1, the owner's extra requirement:** type information must describe the value currently in a
  position, never a stale concrete kind. This holds:
  - After any mutator except a pure append, the positional facts are dropped, and the element type
    is the union of every kind ever stored in the array (`add_element` clears `items` and
    `tuple`). That union is a sound superset of what any position can hold now, never a single
    kind left over from an earlier position.
  - Probes (`scratchpad/b3/probe/n1u/`), all matching legacy or refused before any effect:
    - `[1, 2]` reversed: `xs[0] + 1` still compiles (int stays int);
    - `[1, "a"]` reversed or popped: the union is handled dynamically;
    - `[[1,2,3], "ab"]` reversed: `xs[0].length()` dispatches on the union;
    - `["s"]` cleared then pushed `5`: `xs[0] + 1` gives 6;
    - `[1]` cleared then pushed `"s"`: `xs[0].length()` is refused (`type:length`). The union
      int|string is conservative; this was the same before the fix.
  - A mutation through a parameter alias or a nested alias is refused by the reference guard. One
    in a loop body or a function body over a global invalidates too (refused).
  - Precision and performance did not drop (above): only the nine new fixtures change
    classification, and ops/mutation are identical.
- **N1 refinement (B3 10), found by the gates.** The first N1 fix also invalidated an *empty*
  literal (`a := []; return a.pop()`, cp14b `script_return_kernel_error`). Its proven
  "holds nothing" element type became "unknown", so a program legacy and the VM run (to the
  run-time error "pop: array is empty") was refused (`type:script-return`): a precision loss.
  - An empty literal has no positions to forget, so it is skipped. Sessions are unchanged.
  - A classify pass over all 1855 script fixtures of `tests/runtime2`, the pre-fix binary against
    the refined fix (`scratchpad/b3/probe/d11/classify-corpus.txt`), shows only the nine new N1
    fixtures changing. No other admission or precision change.
  - The ops and mutation callgrind comparison above was taken on the first fix. The refinement
    only skips work, so it cannot add instructions on those paths.
- **Informational (from the re-review):** `StructType::is_private(i)` returns false for
  `i >= mask size`. That is unreachable while the padding invariant holds, but it would not be
  fail-closed; noted for the combined certification.
