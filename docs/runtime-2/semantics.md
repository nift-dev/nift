# Language semantics (v1 — canonical behaviour of the legacy evaluator)

Status: v1, CP08. Every row is backed by a probe in `tests/runtime2/semantics/`
(or `tests/runtime2/vm-corpus/cp04/`) whose stdout, stderr and exit code are
recorded as golden files and checked by `make test-runtime2-vm-corpus`; the same
probes run through `tools/rt-diff` (`make test-runtime2-rt-diff`). Observed on
`runtime-2` (identical results on v4.12.0 `ce8cd36`). Source references are to
`runtime-2` at the CP08 commit; line numbers drift, so each also names the
diagnostic or symbol to search for.

"Canonical" means top-level statements and template expressions (the textual
evaluator). Loop bodies run on a separate prepared evaluator that differs in
the rows marked **Loop divergence**. Under D-006 neither path changes now; the
new engine's target for each divergence is an owner decision (see the last
column).

Decision column: **Keep** = the new engine reproduces it (no change, D-006);
**D-001** = change already approved with the architecture (listed under
"Approved breaks" in `compatibility.md`); **Owner decision needed** = the new
engine needs a ruling before CP12–CP14 implement it (recommendation given).

## Bindings and scope

| Area | Observed behaviour | Probe | Source | Decision |
| --- | --- | --- | --- | --- |
| Declaration | `name := expr` declares in the current scope; redeclaring in the same scope is an error | `decl_redeclare.f` → "binding already declared in this scope: r" | `ParserExpression.cpp:4603` ("binding already declared") | Keep |
| Block scope | `:=` inside an `if` block declares a new binding that shadows the outer one; `=` inside the block rebinds the outer one | `decl_block_scope.f` → 7, 0, 8 | `ParserTemplate.cpp` block scopes (`push_variable_scope`) | Keep |
| Loop scope | `:=` in a loop body is a fresh binding per iteration (no redeclaration error) | `decl_loop_scope.f` → 0, 10, 20, 3 | prepared loop executor, `ParserTemplate.cpp:1207` ff. | Keep |
| Assignment | `name = expr` rebinds an existing binding; assigning an undeclared name is an error | `assign_undeclared.f` → "assignment to undefined binding: zz" | `ParserExpression.cpp:2274` | Keep |
| Typed bindings | A binding keeps the type of its first value; assigning another type is an error | `toplevel_type_mismatch.f` → "cannot assign string to int binding 'x'" | `ParserExpression.cpp:2262`, `:2281` | Keep |
| **Loop divergence: type error wording** | The same error inside a loop reads "cannot change binding type: x" with the column of the assignment | `loop_type_mismatch.f` | `ParserExpression.cpp:4605`, `ParserTemplate.cpp:1556` | Owner decision needed — recommend the canonical wording everywhere (`test-call-semantics` currently pins the loop wording) |
| Const | `const` bindings reject assignment at top level | `loop_const.f` line 4 → "cannot assign to const binding: k" | `ParserExpression.cpp:2275` | Keep |
| **Loop divergence: const** | A `const` declared inside a loop body can be reassigned (prints 2) | `loop_const.f` line 3 | prepared loop executor | Owner decision needed — recommend enforcing const everywhere (the loop behaviour looks like a defect) |
| Free names | Resolved at call time through the *caller's* scopes (dynamic scoping) | `dynamic_scope.f` → 5 | callable invocation scope chain, `ParserExpression.cpp` (`variable_scopes_` walk) | Keep; owner decision needed before any later strict-mode warning becomes an error |
| Top-level `return` | Ends the script with exit status 0; later statements do not run | `top_return.f` → "a" | `ParserTemplate.cpp` (`pending_control_`) | Keep |

## Values, aliasing and copying

| Area | Observed behaviour | Probe | Source | Decision |
| --- | --- | --- | --- | --- |
| Aliasing | Declaring from a *place* (name, member or index path) creates a location reference: mutation through it changes the owner, rebinding it does not | `alias_place.f` → `[1,2,3]`, `[1,2,3]`, `[7]`; `alias_index_path.f` → `{"k":[1,2]}` | location references in `ParserExpression.cpp` declarations (`:4603` ff.) | Keep; implement as reference cells with COW owners |
| Copy at calls | Arguments are copied; callee mutation is invisible to the caller | `copy_at_call.f` → 2, `[1]` | call binding in `ParserExpression.cpp:3511` ff. | Keep semantics; implement with COW |
| Copy at return | A returned aggregate is a copy; mutating it leaves the source | `copy_at_return.f` → `[1]`, `[1,5]` | return path, `ParserTemplate.cpp` | Keep |
| Scalars | Copied | `scalar_copy.f` → 5, 10 | — | Keep |
| Structs | Reference semantics | `struct_reference.f` → 9 | struct instances, `ParserExpression.cpp:4029` area | Keep |
| Struct construction | `S()` builds with field defaults; a field-initialising constructor is `fn(S(a, b))` inside the struct; `S(args)` without one is an error | `struct_constructor.f`, `struct_constructor_args.f` → "struct constructor argument count mismatch: bare" | `ParserExpression.cpp:4029` | Keep |
| Plain objects | Member assignment rejected | `object_member_assign.f` → "member assignment requires a struct instance: o" | `ParserExpression.cpp:4661` | Keep |
| Equality | Structural for arrays, objects and maps; `1 == 1.0` is true; `"1" == 1` is false | `equality.f` | `Ast.cpp` binary `==` | Keep |
| Truthiness | `0`, `""`, `[]` and `null` are falsy | `truthiness.f` | `Ast.cpp` / condition evaluation | Keep |
| Rendering | `print` renders scalars; arrays and objects need `.stringify()` ("print: value is not directly renderable"); a bare expression statement whose value is an array or object renders nothing | `print_array.f`, `vm-corpus/cp04/render_array.f` | `ParserExpression.cpp:1183` | Keep |

## Functions and callables

| Area | Observed behaviour | Probe | Source | Decision |
| --- | --- | --- | --- | --- |
| Closures | See later writes to captured names and globals declared after creation | `closure_late_write.f` → 2; `closure_global_later.f` → 42 | lambda environment capture | Keep via upvalues/`GETGLOBAL` |
| Closure state | A closure over a local keeps its own cell per call of the factory | `closure_counter.f` → 10, 11, 0, 12 | — | Keep |
| Callable identity | Aliases compare equal; distinct lambdas do not; a named function compares equal to its alias | `callable_identity.f` → true, false, true | callable tags | Keep (pointer identity) |
| Recursion | Depth 63 succeeds; depth 64 fails with "callable recursion depth exceeded" | `recursion_limit.f` | `ParserHelpers.cpp:69` (`kMaxCallableDepth = 64`), `ParserExpression.cpp:3511` | D-001: frame budget (~100k), same error code |
| Evaluation order | Operands are evaluated once each, left to right, in calls, binary operators (regardless of precedence), array and object literals | `eval_order.f` → `[1,…,10]` | `Ast.cpp` | Keep; rt-diff traces Host ops to check counts |
| Short circuit | `&&` and `||` do not evaluate the right side when decided | `short_circuit.f` → "yes", 0 | `Ast.cpp` | Keep |
| Natives vs user names | Precedence is per name, not uniform: a user `error` and a user `len` win over the natives, a user `min` does not | `natives_vs_user.f` → "user:e", 3; `loop_builtin_shadow.f` → 99, 99 | `ParserExpression.cpp:1476` (`error`), native dispatch tables | Owner decision needed — recommend one rule (user definitions shadow natives) with a warning; until then the VM reproduces the per-name table |
| **Loop divergence: builtin shadowing** | A user `min` or `type` loses to the native at top level (3, "int") but wins inside a loop body (0, "user"); `len` resolves to the user function in both | `loop_native_shadow.f`, `loop_builtin_shadow.f` | prepared call dispatch vs canonical native dispatch | Owner decision needed (same rule as above) |
| **Loop divergence: error location** | An error inside a function called from a loop is reported at the failing expression ("3:14: unknown value…"); from top level it is reported at the `return` statement with a "return:" prefix ("3:3: return: …") | `loop_callee_error_location.f`, `toplevel_callee_error_location.f` | prepared vs canonical call paths | Owner decision needed — recommend the more precise loop location with the canonical prefix |

## Numbers and strings

| Area | Observed behaviour | Probe | Source | Decision |
| --- | --- | --- | --- | --- |
| Integers | int64 with checked overflow (error), including multiplication | `int_overflow.f`, `int_overflow_mul.f` → "signed 64-bit integer overflow" | `ParserExpression.cpp:1972` | Keep |
| Division | `/` yields a float when inexact (`7/2` → 3.5, `-7/2` → -3.5); `%` keeps the dividend's sign (`-7 % 3` → -1); division by zero is an error | `division.f`, `division_by_zero.f` | `Ast.cpp:218` | Keep |
| Power | There is no `**` operator ("expression cannot be empty") | `power_operator.f` | `ParserExpression.cpp:661` | Keep (adding one is a language change) |
| Floats | Shortest round-trip printing (`0.30000000000000004`); integral floats print without `.0`; `1e21` prints `1e+21`; `-0.0` prints `-0.0` | `floats.f` | number formatting in `RuntimeValue.cpp` | Keep |
| Exact numbers | Integers beyond 2^53 stay exact | `exact_numbers.f` → 9007199254740993, …994, 18014398509481986 | decimal kind | Keep |
| Interpolation | `$[expr]` interpolates inside string literals at top level and in functions; a lone `$` is literal | `strings_interp.f` | `Ast.cpp:182`, `ParserExpression.cpp:1173` | Keep; compile interpolation |
| Nested quotes | A string literal inside `$[...]` inside a string is a parse error | `strings_interp_nested_quote.f` | `ParserExpression.cpp:4876` | Keep |
| **Loop divergence: interpolation** | Inside `while`/`for` bodies, `"$[x]"` is printed literally (`loop=$[x]`), including inside functions called from the loop body (`w=$[x]`), while the same function interpolates when called from top level | `loop_interpolation.f`, `vm-corpus/cp04/interpolation.f` | prepared loop executor | Owner decision needed — recommend interpolating everywhere (the loop behaviour looks like a defect) |
| Escapes | `\t`, `\n`, `\"` are escapes; an unknown escape drops the backslash (`"x\xy"` → `xxy`) | `strings_escape.f` | string literal decoding | Keep (v0 said "not an escape"; corrected) |
| String methods | `length()` counts code points (`"héllo"` → 5); `split` keeps empty fields | `strings_methods.f` | string methods | Keep |
| String indexing | `"abc"[1]` is not supported ("unknown value or malformed expression") | `strings_index.f` | — | Keep |

## Errors

| Area | Observed behaviour | Probe | Source | Decision |
| --- | --- | --- | --- | --- |
| Raised errors | `throw error(msg)`; `catch(e)` exposes `e.message` and `e.code` (`user.raised`) | `errors_try_catch.f` | `ParserExpression.cpp:2488`, `ParserTemplate.cpp:507` | Keep |
| Throw value | Only `Error` values may be thrown | `errors_throw_value.f` → "throw expression must evaluate to Error" | `ParserTemplate.cpp:510` | Keep |
| Runtime errors are not catchable | Errors raised by the runtime (undefined callable, index out of range) are fatal even inside `try`; they are reported at the `try` statement's position (1:1) | `errors_try_catch.f` (undefined callable), `errors_catch_index.f` | diagnostic propagation in `ParserTemplate.cpp` | Owner decision needed — recommend keeping (making them catchable changes control flow of existing scripts) |
| Uncaught errors | Print `error: <file>:<line>:<col>: <message>` to stderr, exit status 1, no further output | `errors_uncaught.f` | `Diagnostic.h` | Keep |
| `error()` values | `error("x").message` is "x" outside `try` | `errors_message_member.f` | `ParserExpression.cpp:1476` | Keep |

## Collections

| Area | Observed behaviour | Probe | Source | Decision |
| --- | --- | --- | --- | --- |
| Object iteration | `for((k, v) : obj)` iterates in insertion order; `for(k : obj)` is an error naming the right syntax | `map_iteration.f` → b, a, c | `ParserTemplate.cpp:2027` | Keep |
| Map equality | Maps with equal entries compare equal | `equality.f` | — | Keep |

## Handles

| Area | Observed behaviour | Probe | Source | Decision |
| --- | --- | --- | --- | --- |
| Handles | Strings starting `\x1fnift:<kind>:` act as handles, including strings from data | design report probe (JSON value invoking a closure) | `RuntimeValue` handle tags | D-001: typed handles; strings never coerce |
| Handle lifetime | Registries never reclaim; a reconstructed tag stays valid | design report probe | handle registries in `Parser.h` | D-001: refcounted resources |
| Callables as strings | `type(lambda)` is "function"; concatenating a callable with a string is an error ("string concatenation requires renderable scalar values") | `handles_lambda_string.f` | `Ast.cpp:231`, `ParserExpression.cpp:4845` | Keep |

## Host operations

| Area | Observed behaviour | Probe | Source | Decision |
| --- | --- | --- | --- | --- |
| Files | `file().open/write/save`, `open`, `stat().size`, `mkdir`, `cp`, `mv`, `rm`, `ls` (directory and glob), `exists`, `is_dir` behave as in `host_fs_ops.f`; each appears in the Host-op trace | `host_fs_ops.f` | `ParserExpression.cpp:994` ff., `:2748` ff.; trace points in `ParserObservation.cpp` | Keep; the trace order is part of the contract checked by rt-diff |
| Environment | `getenv` of an unset name is `null` | `getenv_unset.f` | `ParserExpression.cpp:2543` | Keep |
| Processes | `run(...)` returns an object | `process_run.f` | `Process.cpp` (`nift_run_pipeline`) | Keep |
