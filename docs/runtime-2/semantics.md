# Language semantics (v1.1 — legacy behaviour and Runtime 2.0 intent)

Status: v1.1, CP08 (review findings and owner decisions D-009 applied). Every
row is backed by a probe in `tests/runtime2/semantics/` (or
`tests/runtime2/vm-corpus/cp04/`) whose stdout, stderr and exit code are
recorded as golden files and checked by `make test-runtime2-vm-corpus`; the same
probes run through `tools/rt-diff` (`make test-runtime2-rt-diff`). Observed on
`runtime-2` (identical results on v4.12.0 `ce8cd36`). Source references were
re-verified against `runtime-2-cp08`; line numbers drift, so each also names the
diagnostic or symbol to search for.

Every row states four things (the owner's categories):

- **Legacy** — what the legacy interpreter does (the "Legacy behaviour" column).
  "Canonical" means top-level statements and template expressions; loop bodies
  run on a separate prepared evaluator that differs in the rows marked
  **Loop divergence**. Under D-006 the legacy interpreter is not changed.
- **Runtime 2.0** — what the new engine does (the "Runtime 2.0" column).
- **Approved change** — a difference the owner approved (D-001 architecture
  breaks, D-009 language decisions). Rows with D-009 changes have intended
  goldens (`<probe>.r2.stdout/.stderr/.exit`) and exact rt-diff policies in
  `tools/rt-diff/policies/d009.json`, so the difference is reported as approved
  rather than as a regression.
- **Deferred** — an improvement recorded for later, not implemented in the
  initial VM migration.

The "Category" column says which applies: **Unchanged** (Runtime 2.0 = legacy),
**Approved change (D-…)**, or **Unchanged now; deferred: …**.

## Bindings and scope

| Area | Legacy behaviour | Probe | Source | Runtime 2.0 / Category |
| --- | --- | --- | --- | --- |
| Declaration | `name := expr` declares in the current scope; redeclaring in the same scope is an error | `decl_redeclare.f` → "binding already declared in this scope: r" | `ParserExpression.cpp:4625` ("binding already declared in this scope"; top-level declarations `ParserTemplate.cpp:627`) | Unchanged |
| Block scope | `:=` inside an `if` block declares a new binding that shadows the outer one; `=` inside the block rebinds the outer one | `decl_block_scope.f` → 7, 0, 8 | `ParserTemplate.cpp` block scopes (`push_variable_scope`) | Unchanged |
| Loop scope | `:=` in a loop body is a fresh binding per iteration (no redeclaration error) | `decl_loop_scope.f` → 0, 10, 20, 3 | prepared loop executor, `ParserTemplate.cpp:1207` ff. | Unchanged |
| Assignment | `name = expr` rebinds an existing binding; assigning an undeclared name is an error | `assign_undeclared.f` → "assignment to undefined binding: zz" | `ParserExpression.cpp:2274` | Unchanged |
| Typed bindings | A binding keeps the type of its first value; assigning another type is an error | `toplevel_type_mismatch.f` → "cannot assign string to int binding 'x'" | `ParserExpression.cpp:2262`, `:2281` | Unchanged |
| **Loop divergence: type error wording** | The same error inside a loop reads "cannot change binding type: x" with the column of the assignment | `loop_type_mismatch.f` | `ParserExpression.cpp:4605`, `ParserTemplate.cpp:1568` (`assign_plain`) | Runtime 2.0: "cannot assign string to int binding 'x'" everywhere (location unchanged). **Approved change (D-009#1)**; `loop_type_mismatch.r2.*`. `test-call-semantics` pins the legacy loop wording and keeps doing so for the legacy engine |
| Const | `const` bindings reject assignment at top level | `loop_const.f` line 4 → "cannot assign to const binding: k" | `ParserExpression.cpp:2275` | Unchanged |
| **Loop divergence: const** | A `const` declared inside a loop body can be reassigned (prints 2) | `loop_const.f` line 3 | prepared loop executor | Runtime 2.0: `const` immutable everywhere; the loop reassignment fails at `3:30` ("cannot assign to const binding: c"). **Approved change (D-009#2)**; `loop_const.r2.*` |
| Free names | Resolved at call time through the *caller's* scopes (dynamic scoping) | `dynamic_scope.f` → 5 | callable invocation scope chain, `ParserExpression.cpp` (`variable_scopes_` walk) | Unchanged now; **deferred (D-009#8)**: an opt-in strict-mode warning for free names, later |
| Top-level `return` | Ends the script with exit status 0; later statements do not run | `top_return.f` → "a" | `ParserTemplate.cpp` (`pending_control_`) | Unchanged |

## Values, aliasing and copying

| Area | Legacy behaviour | Probe | Source | Runtime 2.0 / Category |
| --- | --- | --- | --- | --- |
| Aliasing | Declaring from a *place* (name, member or index path) creates a location reference: mutation through it changes the owner, rebinding it does not | `alias_place.f` → `[1,2,3]`, `[1,2,3]`, `[7]`; `alias_index_path.f` → `{"k":[1,2]}` | location references in `ParserExpression.cpp` declarations (`:4603` ff.) | Unchanged; implementation note: implement as reference cells with COW owners |
| Copy at calls | Arguments are copied; callee mutation is invisible to the caller | `copy_at_call.f` → 2, `[1]` | call binding in `ParserExpression.cpp:3511` ff. | Unchanged; implementation note: implement with COW |
| Copy at return | A returned aggregate is a copy; mutating it leaves the source | `copy_at_return.f` → `[1]`, `[1,5]` | return path, `ParserTemplate.cpp` | Unchanged |
| Scalars | Copied | `scalar_copy.f` → 5, 10 | — | Unchanged |
| Structs | Reference semantics | `struct_reference.f` → 9 | struct instances, `ParserExpression.cpp:4029` area | Unchanged |
| Struct construction | `S()` builds with field defaults; a field-initialising constructor is `fn(S(a, b))` inside the struct; `S(args)` without one is an error | `struct_constructor.f`, `struct_constructor_args.f` → "struct constructor argument count mismatch: bare" | `ParserExpression.cpp:4029` | Unchanged |
| Plain objects | Member assignment rejected | `object_member_assign.f` → "member assignment requires a struct instance: o" | `ParserExpression.cpp:4661` | Unchanged |
| Equality | Structural for arrays, objects and maps; `1 == 1.0` is true; `"1" == 1` is false | `equality.f` | `Ast.cpp` binary `==` | Unchanged |
| Truthiness | `0`, `""`, `[]` and `null` are falsy | `truthiness.f` | `Ast.cpp` / condition evaluation | Unchanged |
| Rendering | `print` renders scalars; arrays and objects need `.stringify()` ("print: value is not directly renderable"); a bare expression statement whose value is an array or object renders nothing | `print_array.f`, `vm-corpus/cp04/render_array.f` | `Parser.cpp:957` ("value is not directly renderable") | Unchanged |

## Functions and callables

| Area | Legacy behaviour | Probe | Source | Runtime 2.0 / Category |
| --- | --- | --- | --- | --- |
| Closures | See later writes to captured names and globals declared after creation | `closure_late_write.f` → 2; `closure_global_later.f` → 42 | lambda environment capture | Unchanged; implementation note: via upvalues/`GETGLOBAL` |
| Closure state | A closure over a local keeps its own cell per call of the factory | `closure_counter.f` → 10, 11, 0, 12 | — | Unchanged |
| Callable identity | Aliases compare equal; distinct lambdas do not; a named function compares equal to its alias | `callable_identity.f` → true, false, true | callable tags | Unchanged (pointer identity) |
| Recursion | At most 64 nested user-callable frames: `down(63)` (64 frames: `down(63)` … `down(0)`) succeeds; `down(64)` fails with "callable recursion depth exceeded" | `recursion_limit.f` | `ParserHelpers.cpp:69` (`kMaxCallableDepth = 64`), check at `ParserExpression.cpp:3511` | Runtime 2.0: frame budget (~100k), same error code. **Approved change (D-001)** |
| Evaluation order | Operands are evaluated once each, left to right, in calls, binary operators (regardless of precedence), array and object literals | `eval_order.f` → `[1,…,10]` | `Ast.cpp` | Unchanged; implementation note: rt-diff traces Host ops to check counts |
| Short circuit | `&&` and `||` do not evaluate the right side when decided | `short_circuit.f` → "yes", 0 | `Ast.cpp` | Unchanged |
| Natives vs user names | Precedence is per name, not uniform: a user `error` and a user `len` win over the natives, a user `min` does not | `natives_vs_user.f` → "user:e", 3; `loop_builtin_shadow.f` → 99, 99 | `ParserExpression.cpp:1476` (`error`), native dispatch tables | Runtime 2.0: lexical bindings and user-defined functions take precedence over built-ins everywhere (user `min` → 0). **Approved change (D-009#3)**; `natives_vs_user.r2.*`. How a shadowed built-in stays reachable is open design work (no new standard-library namespace unless the language design justifies it). Affected existing programs: see "D-009#3 compatibility scan" below |
| **Loop divergence: builtin shadowing** | A user `min` or `type` loses to the native at top level (3, "int") but wins inside a loop body (0, "user"); `len` resolves to the user function in both | `loop_native_shadow.f`, `loop_builtin_shadow.f` | prepared call dispatch vs canonical native dispatch | Runtime 2.0: user definitions win in loops and at top level alike (`0`, `user` twice). **Approved change (D-009#3, #4)**; `loop_native_shadow.r2.*` |
| **Loop divergence: error location** | An error inside a function called from a loop is reported at the failing expression ("3:14: unknown value…"); from top level it is reported at the `return` statement with a "return:" prefix ("3:3: return: …") | `loop_callee_error_location.f`, `toplevel_callee_error_location.f` | prepared vs canonical call paths | Runtime 2.0: reported at the failing expression in the function (`3:14: unknown value or malformed expression: undefined_name`) from top level too. **Approved change (D-009#5)**; `toplevel_callee_error_location.r2.*`. Whether the `return:` prefix survives is fixed by that golden (it does not) |

## Numbers and strings

| Area | Legacy behaviour | Probe | Source | Runtime 2.0 / Category |
| --- | --- | --- | --- | --- |
| Integers | int64 with checked overflow (error), including multiplication | `int_overflow.f`, `int_overflow_mul.f` → "signed 64-bit integer overflow" | `ParserExpression.cpp:1972` | Unchanged |
| Division | `/` yields a float when inexact (`7/2` → 3.5, `-7/2` → -3.5); `%` keeps the dividend's sign (`-7 % 3` → -1); division by zero is an error | `division.f`, `division_by_zero.f` | `Ast.cpp:218` | Unchanged |
| Power | There is no `**` operator ("expression cannot be empty") | `power_operator.f` | `ParserExpression.cpp:1873` ("expression cannot be empty") | Unchanged (adding one is a language change) |
| Floats | Shortest round-trip printing (`0.30000000000000004`); integral floats print without `.0`; `1e21` prints `1e+21`; `-0.0` prints `-0.0` | `floats.f` | number formatting in `RuntimeValue.cpp` | Unchanged |
| Exact numbers | Integers beyond 2^53 stay exact | `exact_numbers.f` → 9007199254740993, …994, 18014398509481986 | decimal kind | Unchanged |
| Interpolation | `$[expr]` interpolates inside string literals at top level and in functions; a lone `$` is literal | `strings_interp.f` | `Ast.cpp:182`, `ParserExpression.cpp:1173` | Unchanged; implementation note: compile interpolation |
| Nested quotes | A string literal inside `$[...]` inside a string is a parse error | `strings_interp_nested_quote.f` | string-literal scanning in `ParserExpression.cpp` (search: "unknown value or malformed expression"; the nested quote ends the literal early) | Unchanged |
| **Loop divergence: interpolation** | Inside `while`/`for` bodies, `"$[x]"` is printed literally (`loop=$[x]`), including inside functions called from the loop body (`w=$[x]`), while the same function interpolates when called from top level | `loop_interpolation.f`, `vm-corpus/cp04/interpolation.f` | prepared loop executor | Runtime 2.0: interpolation everywhere (`loop=3`, `for=1`). **Approved change (D-009#6)**; `loop_interpolation.r2.*` |
| Escapes | `\t`, `\n`, `\"` are escapes; an unknown escape drops the backslash (`"x\xy"` → `xxy`) | `strings_escape.f` | string literal decoding | Unchanged (v0 said "not an escape"; corrected) |
| String methods | `length()` counts code points (`"héllo"` → 5); `split` keeps empty fields | `strings_methods.f` | string methods | Unchanged |
| String indexing | `"abc"[1]` is not supported ("unknown value or malformed expression") | `strings_index.f` | — | Unchanged |

*Update (2026-10-20, loop-literal fix):* the loop divergence above was a legacy regression introduced in v4.12.0 by 5fe38bd1 (prepared-code literal and output completion bypassed); v4.6.0 interpolates in loops. Runtime-2 legacy is fixed (tests/runtime2/loop-literals, v4.6.0 authoritative), so legacy and Runtime 2.0 now agree: `loop_interpolation.stdout` and `cp04/interpolation.stdout` were re-recorded from the fixed legacy and equal the `.r2.*` expectation. D-009#6 no longer describes a behaviour change against the corrected legacy, so its two approval policies were removed from `tools/rt-diff/policies/d009.json` (11 remain); an approval for a difference that no longer exists would be reported unused.

## Errors

| Area | Legacy behaviour | Probe | Source | Runtime 2.0 / Category |
| --- | --- | --- | --- | --- |
| Raised errors | `throw error(msg)`; `catch(e)` exposes `e.message` and `e.code` (`user.raised`) | `errors_try_catch.f` | `ParserExpression.cpp:2488` (`user.raised`), `ParserTemplate.cpp:515` (`catch` binding) | Unchanged |
| Throw value | Only `Error` values may be thrown | `errors_throw_value.f` → "throw expression must evaluate to Error" | `ParserTemplate.cpp:522` | Unchanged |
| Runtime errors are not catchable | Errors raised by the runtime (undefined callable, index out of range) are fatal even inside `try`; they are reported at the position where they occur (feature-site-dx, D-018: previously the `try` statement's position, 1:1) | `errors_try_catch.f` (undefined callable), `errors_catch_index.f` | diagnostic propagation in `ParserTemplate.cpp` | Unchanged in the initial VM migration (D-009#7: A for now). **Deferred**: ordinary runtime failures (undefined callable, bad index, type errors) become structured, catchable exceptions; fatal internal errors, memory exhaustion and termination stay distinct |
| Uncaught errors | Print `error: <file>:<line>:<col>: <message>` to stderr, exit status 1, no further output | `errors_uncaught.f` | `Diagnostic.h` | Unchanged |
| `error()` values | `error("x").message` is "x" outside `try` | `errors_message_member.f` | `ParserExpression.cpp:1476` | Unchanged |

## Collections

| Area | Legacy behaviour | Probe | Source | Runtime 2.0 / Category |
| --- | --- | --- | --- | --- |
| Object iteration | `for((k, v) : obj)` iterates in insertion order; `for(k : obj)` is an error naming the right syntax | `map_iteration.f` → b, a, c | `ParserTemplate.cpp:2039` | Unchanged |
| Map equality | Maps with equal entries compare equal | `equality.f` | — | Unchanged |

## Handles

| Area | Legacy behaviour | Probe | Source | Runtime 2.0 / Category |
| --- | --- | --- | --- | --- |
| Handles | Strings starting `\x1fnift:<kind>:` act as handles, including strings from data | design report probe (JSON value invoking a closure) | `RuntimeValue` handle tags | Runtime 2.0: typed handles; strings never coerce. **Approved change (D-001)** |
| Handle lifetime | Registries never reclaim; a reconstructed tag stays valid | design report probe | handle registries in `Parser.h` | Runtime 2.0: refcounted resources. **Approved change (D-001)** |
| Callables as strings | `type(lambda)` is "function"; concatenating a callable with a string is an error ("string concatenation requires renderable scalar values") | `handles_lambda_string.f` | `Ast.cpp:231`, `ParserExpression.cpp:4845` | Unchanged |

## Host operations

| Area | Legacy behaviour | Probe | Source | Runtime 2.0 / Category |
| --- | --- | --- | --- | --- |
| Files | `file().open/write/save`, `open`, `stat().size`, `mkdir`, `cp`, `mv`, `rm`, `ls` (directory and glob), `exists`, `is_dir` behave as in `host_fs_ops.f`; each appears in the Host-op trace | `host_fs_ops.f` | `ParserExpression.cpp:955` ff. (`file()` methods), `:2828` (`open`), `:2748` ff. (`mkdir`/`touch`/`rm`/`cp`/`mv`); trace points in `ParserObservation.cpp` | Unchanged; implementation note: the trace order is part of the contract checked by rt-diff |
| Environment | `getenv` of an unset name is `null` | `getenv_unset.f` | `ParserExpression.cpp:2544` (`getenv`) | Unchanged |
| Processes | `run(...)` returns an object | `process_run.f` | `Process.cpp` (`nift_run_pipeline`) | Unchanged |

## Concurrency (CP22)

Probes: `tests/runtime2/cp22/cases/*.f` (legacy goldens) and `vm-only/*.f`;
record `checkpoints/cp22-coroutines.md`.

| Area | Legacy behaviour | Probe | Source | Runtime 2.0 / Category |
| --- | --- | --- | --- | --- |
| Async call | `fn[async]` / `async (…) =>` returns a future at once; the body runs on a pool thread | `a01_basic.f` | `spawn_future` (`ParserExpression.cpp`) | VM coroutine (Spawn) **only when cooperative execution is proven legacy-equal** (record §12.1: no blocking Host call in the unit, task code without non-suspending loops, recursion or data-proportional built-ins, and no observation of scheduling: no `print` in tasks, no `epoch`, `done()` or `status()`, so the result is schedule-independent); otherwise refused before execution (`coro-blocking-host:*`, `coro-task-loop`, `coro-task-recursion`, `coro-task-native:*`, `coro-observe:*`) and run on legacy's parallel pool; Unchanged |
| Isolation | The task sees a snapshot taken at the call; writes on either side are invisible to the other | `iso_*.f` | `clone_worker_binding` | Unchanged (per-task global snapshot) |
| Completion | A task completes after its body and every task it spawned; the run waits for all tasks (normal end, top-level `return`, main error) | `drain_*.f` | `finalize_execution_workers` | Unchanged |
| `await` | Value; thrown Error re-raised unchanged (catchable, original origin); fatal task error `future: <message>` at the task's position; non-future `await: expected future` | `err_*.f` | `await_future` | Unchanged; the task's own message follows D-009#5 (`future: division by zero` at the division, legacy `future: return: …` at the `return`): **Approved change (D-009#5)**, `err_fatal_await.r2.*` |
| Ordering | Interleaving of task output is nondeterministic (threads) | `t3` probe | pool threads | Deterministic FIFO run queue, timers by deadline: one of legacy's behaviours (VM-only fixtures `vm-only/*.f`) |
| `sleep` | Blocks the calling thread | `sleep_*.f` | `sleep_for` | In coroutine units the task suspends (same validation and trace); elsewhere unchanged |
| Futures | `type` `future`; identity equality; truthy; binding type string; `done()`, `status()` | `fut_*.f` | async handle tags | Unchanged where admitted; other uses refused before execution (`coro-future-flow:*`) |
| Async named function as a value | The future fails with `invalid async callable` (defect) | `refuse/r_async_value.f` | `set_named_callable_async` | Refused before execution (`coro-async-value`) |
| Threads | `thread(f, args…)` runs on an OS thread with the snapshot rule; `join()` (repeatable), `done()`, `status()`, `type` `thread`; fatal failure `thread: <message>` at the thread's position; thrown Error re-raised unchanged; unjoined threads drain | `th_*.f` | `thread()` (`ParserExpression.cpp`) | **Refused before execution** (`callable:thread`; owner decision 2026-10-27, record §12: real parallelism is preserved; parallel threads arrive with CP23 isolates). Non-contractual test switch `NIFT_VM_COOPERATIVE_THREADS=1` (evidence only): VM task on the scheduler (cooperative, FIFO: one of legacy's interleavings, no parallelism); `thread: ` follows D-009#5 like `future: ` (`th_error_fatal.r2.*`); a lambda/async/value callee, a wrong argument count and a handle returned from a thread body refused before execution (`coro-thread-*`, `coro-future-flow:Return`) |
| Mutexes, atomics | OS locks and atomics shared across threads | `v45_mutex.sh`, `v45_atomics.sh` | `mutex()`, `atomic<…>` | Stay on legacy (CP23) |

## D-009#3 compatibility scan

Programs that define a top-level function named like a built-in, found by
scanning every `.f`, shell heredoc, template and Markdown file under `tests/`,
`docs/evidence/`, `bindings/` and the `nift-regression-suite` (77 natives
dispatched by name plus `len`, `type`, `error`, `min`, `max`, `print`):

| Program | Name | Legacy | Runtime 2.0 | Regression coverage |
| --- | --- | --- | --- | --- |
| `tests/runtime2/semantics/natives_vs_user.f` | `error`, `min` | user `error` wins; native `min` wins (`3`) | user wins for both (`0`) | legacy golden + `natives_vs_user.r2.*` + policy |
| `tests/runtime2/semantics/loop_native_shadow.f` | `min`, `type` | native at top level, user in loops | user everywhere | legacy golden + `.r2.*` + policy |
| `tests/runtime2/semantics/loop_builtin_shadow.f` | `len` | user wins everywhere | unchanged | legacy golden |
| `tests/runtime2/vm-corpus/cp04/shadow_builtin.f` | `len` | user wins | unchanged | legacy golden |
| `tests/v46_b4_cp1_characterization.sh` (`names.f`) | `error` | user wins | unchanged | existing test |

Struct methods named `read`, `run`, `error` (`v42_struct_private_smoke.sh`,
`v48_identifier_parity.sh`, `v44_struct_variadic_methods_smoke.sh`,
`v46_b4_cp1_characterization.sh`, the regression suite's
`v43_scripting_inspection_smoke.sh`) are members, not global names, and are
unaffected. No other existing program relies on a built-in winning over a user
definition; the only behaviour change in the scanned material is in the two
probes written to pin it.
