# C145: literal call arguments (F5) and legacy method heads (F6) (production checkpoint)

**Status:** implemented locally; pending freeze review and owner acceptance. Owner decisions D-102 (F5 Option B and
the F6 refusal extension approved) and D-106 (C145 authorised, exactly three changes). Design:
review-f5/DESIGN-C145.md (read-only design check); evidence: review-f5/INVESTIGATE-F5.md and the F6 tables.

## The three changes (separate commits)

| Commit | Change | src |
| --- | --- | --- |
| F5 | `ExprParser::call` classifies the callee; `ExprParser::sequence` builds a direct quoted-literal argument containing `$[` (no backslash, followed by `,` or `)`) as a plain String leaf flagged `kFlagRawLiteral`; `decode_string_literal` keeps `$[` for such a leaf, so the existing constant path emits legacy's raw text | expr_postfix.cpp, expr_literals.cpp, expr_parser.h, ast.h, literal.{h,cpp}, compiler_literal_expr.cpp |
| write-family fallback | a call flagged `kFlagAmbiguousLiteralArg` (a method named write/write_line/write_val with such a literal) is refused as `host-literal-arg` at the single Call dispatch, before anything runs | compiler_expr.cpp |
| F6 | `legacy_field_call_refused()` (reserved_names.cpp): exactly the 46 pinned names, sorted, binary search; a member `call_value` (reached only from method_call's NotBuiltin case) is refused as `legacy-method-precedence` before the callable is evaluated | reserved_names.{h,cpp}, compiler_closure.cpp, refusal_text.cpp |

No new compiler pass, opcode, interpolation lowering or runtime change. The output family (`print`, `err`, `warn`)
is decided by name at parse time (the names are reserved; a user function cannot take them; `native::` names are never
output). Calls through values (variables, parameters, fields, lambdas, async functions) are always raw, as in legacy.

## Semantics

See compatibility.md ("Literal call arguments and legacy method heads"). In short:
- raw: `id("$[x]")`, `'$[x]'`, `"pre-$[x]"`, `"$[x]$[x]"`, `"$[ x ]"`, blanks around the argument, a second
  argument, nested `id(id("$[x]"))`, never-evaluated `"$[nope]"`, `"$[1/0]"`, `"$[x"`, `"$[]"`; every callee kind and
  native (file trees, traces and outputs equal legacy's);
- interpolating: `print(...)`, partial-expression arguments, receivers, assignments, returns;
- refused: write/write_line/write_val with such a literal (`host-literal-arg`); `\` with `$` and array literals with
  `$[` stay opaque, as before;
- F6: the 46 names refused before the field runs; `front` and `top` keep their pre-existing `builtin-receiver`
  refusal (legacy calls the field; outside F6); the other 88 names match legacy at arities 0-2.

## Tests and mutants

`tests/runtime2/literal_args.py` (`make test-runtime2-literal-args`, and a test-rt-vm recipe suite), VM default against
`NIFT_VM=0`, byte-exact (rc, stdout, stderr, resulting files), scratch fixtures (a.txt = "A", a file named `$[x]`):
- F5 callee kinds (user function, lambda, parameter-held function, field, thread, await, error(), nested, blanks,
  second argument) x 9 argument spellings; struct method and constructor;
- natives with file trees: exists, open, file().read_all, `native::cat/cp/mv/rm/touch/mkdir/ls`, remove, move,
  string, array, set and object methods; a host-call trace (fd 3, scratch file) naming the raw path in both engines;
- controls that must keep interpolating; contexts (for, while, function, lambda, async, thread, module, `-e`, `-i`);
- the four fix-only cases on the VM default (`id("$[x]")`, the `exists` branch, `native::rm` removing the `$[x]`
  file, `id("$[nope]")`), which a refusal cannot pass;
- the struct `s.write` and ofstream refusals (nothing runs) and the unchanged opaque refusals;
- F6: a table-driven oracle over the 136 legacy method names at arities 0, 1 and 2 (the field prints a marker, which
  must never appear when the VM refuses); controls `o.f`, `o.push`, `o.cp`, a struct method `cat`, `native::cat`;
  `-e` and an imported module refused.

Mutants (NIFT_C145_MUTANT, sanctioned builds, after a no-mutation control): 1 restores interpolation of the raw
literal; 2 makes the output family raw too; 3 drops the write-family fallback; 4 removes the F6 guard; 5 adds an
over-refused name (`push`) to the pinned list. Each is caught.

## Inventory

One new classification row, `legacy-method-precedence` (class I, approved, D-102/D-106). `host-literal-arg` keeps its
row unchanged. The PENDING-OWNER set is unchanged.

## Recorded safety transcripts changed (D-106, D-107)

The F6 guard refuses a call of one of the 46 names on any object-kind or unknown-kind receiver, whether or not the
field exists (design 6c). Two safety cases call `.insert(0, 1)` on a plain object without that field. Legacy prints
`b` and then reports `unknown value or malformed expression: a.insert(0, 1)` at the same position; the VM previously
ran and raised `value has no member: insert`; it now refuses the program before anything runs. D-107 approved
updating exactly these two entries of tests/runtime2/safety/expected.json (`safety.py --update`; the diff touches only
these two entries, 4 lines each way):

| Case (program unchanged, tests/runtime2/safety/safety.py) | Recorded before (C134 transcript) | Recorded now |
| --- | --- | --- |
| `m_insert_object_loop`: `print("b")` / `xs := [{"k": 1}]` / `for (a : xs) { a.insert(0, 1) }` / `print("after")` | exit 1; stdout `b\n`; stderr `error: p.f:3:16: value has no member: insert` | exit 1; stdout empty; stderr `error: p.f:3:16: not compiled for the VM (legacy-method-precedence): calling an object field whose name legacy reserves as a method is not supported by the Runtime 2.0 VM yet` and the standard notes (the program was not executed; `NIFT_VM=0 nift p.f`; the issues URL) |
| `m_insert_object_mapget`: `print("b")` / `m := map()` / `m.set("k", {"k": 1})` / `a := m.get("k")` / `a.insert(0, 1)` | exit 1; stdout `b\n`; stderr `error: p.f:5:1: value has no member: insert` | exit 1; stdout empty; the same refusal at `p.f:5:1` |

`m_insert_object_session` keeps its recorded transcript (`session:legacy-method-precedence`): the F6 guard does not
apply in sessions (9789d332). Mutant 4 (the F6 guard removed) is caught by literal_args.py (`f6-oracle`,
`f6-entry-points`) and by these two safety cases (`m_insert_object_loop`, `m_insert_object_mapget` fail with the old
transcript); `m_insert_object_session` passes under mutant 4, as it should.

## Review notes (REVIEW-C145; D-109)

- **N1 (D-109): unreachable code is refused too.** The F6 refusal is a compile-time admission check, so it also
  applies where the call is never reached: `o := {"cat": (p) => "U"}` / `print("before")` /
  `if (false) { print(o.cat("a")) }` / `print("after")` is refused on the VM (`legacy-method-precedence` at 3:20,
  nothing runs), while legacy prints `before` and `after` and exits 0. This is conservative admission behaviour that
  can differ from legacy execution.
- **N2 (D-109): the message is broader than its cause.** "calling an object field whose name legacy reserves as a
  method" is also the text when the receiver has no field of that name (`{"k": 1}.insert(0, 1)`, the safety cases
  above) or when the receiver's type is not known at compile time (a parameter, `m.get("k")`). The message is
  unchanged by decision.
- The classification row for `legacy-method-precedence` (tests/runtime2/cp25/classification.tsv) is test data and is
  not edited; these notes are its documentation.
