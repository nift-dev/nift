# Weekend candidate: coroutines in programs that import modules (`coro-module`)

Status: **candidate** on `cand/coro-module`, based on local `weekend/r2` at
`10c41015` (strict VM default). Independent review of `fff92edd`: **REJECT**
(§8). This revision addresses that review. It is not re-reviewed or
integrated, and nothing is pushed.

## 1. Problem

CP22 refused every coroutine unit that imports a module: `check_coroutine_unit`
returned `coro-module` whenever `CompileLimits::linked` was set
(`src/rt/compiler_coro.cpp:334`), in both the cooperative and the CP23 parallel
model. The CP22 reason was "module initialisation order vs spawns unproven"
(`cp22-coroutines.md` §5). CP23 never revisited it. Under the strict VM
default every such program failed before running.

A linked unit is one AST and one Program: the entry with each `import(...)`
replaced by the module's text (`rt/module_link.h`). Every CP22/CP23 analysis
(globals, kinds, task snapshots, future flow, transfer) already applies to it
as it does to the same program written inline. The inline equivalents of the
affected units already ran legacy-equal on the VM. What a linked unit adds is
two legacy module rules the VM does not model:

1. **Initialisation with workers.** Legacy can start tasks while a module
   initialises. When that import later fails, legacy reports
   `import: failed import created worker resources: ...`.
2. **Whole-module capture.** A task whose code is a module callable captures
   that module's whole environment, so an unused non-transferable binding
   fails the spawn (`thread: capture contains a non-transferable timer`). A task
   also receives a copy of module state at the spawn.

## 2. Corpus

Found by a compile-only sweep (`NIFT_VM=classify`, the run path's switches)
of every smoke-heredoc entry and shared-corpus script that has an import. On
the base, 17 units were refused as `coro-module`, all in `v46_timer_smoke`.
This matches the earlier real-run inventory. No `v45_threads` unit imports a
module.

Units with imports that are refused earlier for another reason are not
affected. The v46 import-worker and b4 worker units stop at `import-package`
(11), `import-nested` (8) or `import-missing` (5). Those are the packages
checkpoint's, out of scope here. They will meet these rules once they link.

| Unit (v46_timer_smoke) | Legacy (`NIFT_VM=0`) | VM before | VM now | Why |
|---|---|---|---|---|
| 027 clean-module-worker | `4`, exit 0 | `coro-module` | **runs, equal** | stateless module function as a thread body |
| 028 clean-loading-imported-wrappers | `4 4`, exit 0 | `coro-module` | **runs, equal** | entry tasks wrap a stateless module function |
| 029 import-clean-loading-imported-wrappers | `4 4`, exit 0 | `coro-module` | `coro-module-init-task` at clean-loading-imported-wrappers.f:4:11 | the imported module starts a thread at its top level (owner ruling) |
| 015 import-loading-module | exit 1, `import: thread: capture contains a non-transferable timer` | `coro-module` | `coro-module-init-task` at loading-module-thread.f:3:11 | the same |
| 017 import-loading-imported-wrapper-thread | exit 1, same family | `coro-module` | `coro-module-init-task` at :3:11 | the same |
| 019 import-loading-imported-wrapper-async | exit 1, `import: async function capture ...` | `coro-module` | `coro-module-init-task` at :3:11 | the same (async call) |
| 010 module-thread | exit 1, `thread: capture contains a non-transferable timer` (2:11) | `coro-module` | `coro-module-capture` at 2:11 | the task reads module state (a timer) |
| 011 module-future | exit 1, `async function capture ...` (2:11) | `coro-module` | `coro-module-capture` at 2:11 | the same, async |
| 013 module-reexport-thread | exit 1 (2:11) | `coro-module` | `coro-module-capture` at 2:11 | the same, through a re-export |
| 016 loading-imported-wrapper-thread | exit 0 (worker never joined) | `coro-module` | `coro-module-capture` at 3:11 | the entry's task wraps a module function that reads a timer |
| 018 loading-imported-wrapper-async | exit 0 (future never awaited) | `coro-module` | `coro-module-capture` at 3:11 | the same, async |
| 020 callable-arg-thread | exit 1, `thread: argument contains a non-transferable timer` (3:11) | `coro-module` | `coro-module-capture` at 3:11 | a module function passed into a task |
| 021 nested-callable-arg-thread | exit 1 (3:11) | `coro-module` | `coro-module-capture` at 3:11 | the same, nested in an object |
| 022 container-callable-arg-thread | exit 1 (5:11) | `coro-module` | `coro-module-capture` at 5:11 | a module function in a map passed into a task (module state holds a timer) |
| 023 callable-arg-future | exit 1 (3:11) | `coro-module` | `coro-module-capture` at 3:11 | the same, async |
| 024 nested-callable-arg-future | exit 1 (3:11) | `coro-module` | `coro-module-capture` at 3:11 | the same, async |
| 012 timer-reexport | exit 0 | `coro-module` | `callable:timer` at timer-module.f:1:17 | no task is started. `timer()` is refused by the VM anyway (G05, not this candidate) |

In summary, 2 units now run on the VM with legacy's output, and 15 stay
refused before any effect:

- 4 refused as `coro-module-init-task`, 3 of which already fail in legacy;
- 10 refused as `coro-module-capture`, 8 of which already fail in legacy;
- 1 refused as `callable:timer`.

Every refusal position is the position where legacy itself fails, or the spawn
that legacy would start. The historical CP22 fixture `tests/runtime2/cp22/refuse/r_module.f` is an
async entry function plus a module with one unrelated binding. It is no longer
refused as a coroutine unit. It now stops at an unrelated refusal that was
there before: its module binds `mv`, a built-in name (`binding-name-reserved`).
Its CP23 classification row records that. The fixture itself is not changed.
The same program with the binding renamed is the `unrelated-module-state`
parity case.

## 3. Contract

It applies to a linked coroutine unit, meaning a unit with static imports that
declares an async function, contains an async lambda or calls `thread(...)`,
under the parallel task model (the default). The unit compiles and runs on the
VM exactly as its inline equivalent would, unless one of four refusals applies
(two owner rulings, two from the review). All of them happen at compile time,
before any effect (nothing of the entry or of any module runs). Each has a
recorded position, at the refused node in the file that holds it. They are
checked in this order: init-task, private-async, before-import, capture.

Three analysis rules hold for all four refusals (review R2/R3):

- **Built-in callbacks.** A built-in or native callee (`map`, `filter`, `sort`,
  `reduce`, a native function) given an argument that may be a function (a
  lambda, a function's name, or a value whose kinds include a function) is a
  call through a value.
- **Methods.** A method is never resolved by its name alone. On a receiver
  that may be an object, a function or unknown, the call is a call through a
  value, and every user method of that name may also run.
- **Lambdas in module initialisation.** A lambda created in a module's top
  level counts as top-level code. Its body is walked, whether it is called,
  passed on or never used.

**`coro-module-init-task`** (owner ruling: kept refused). Some code in an
imported module's top level may start a task. A module's top level is its
statements outside function bodies, including the bodies of lambdas created
there. "May start a task" covers:

- a `thread(...)` call;
- a call of an async function;
- a call of a named function that may, through any chain of named calls, do
  either of those;
- a call through a value, while some function value or lambda in the unit
  may start a task.

The first such node in the text is reported. Imports never run concurrently:
imports inside functions are refused at link (`import-nested`). An import
therefore never creates workers, and legacy's "failed import created worker
resources" path cannot arise.

**`coro-module-capture`** (owner ruling: kept refused). A task may capture
module state across the boundary. A task is a `thread(f, ...)` call, a call of
an async function, or an async lambda. Its code is:

- the body;
- every function it may reach by name;
- every function named, and every lambda, in its arguments (values passed in).

Arguments themselves are evaluated by the spawner and passed by value. The
task is refused when any of these holds:

- its code reads or writes a top-level binding of an imported module (module
  state), directly or through any function it reaches;
- its code calls through a value, while some function value or lambda in the
  unit may touch module state;
- some imported module's top-level binding may hold a handle (or a value of
  unknown kind). Legacy captures the whole module environment with every module
  callable a task receives, including through containers. The unit is refused
  whatever the task does. Such a unit is refused anyway: a handle needs a host
  built-in (R1 `module-host-op`), and unknown kinds are refused. So this costs
  no program that could otherwise run.

The first such task in the text is reported.

**`coro-module-private-async`** (review R1). Code in an imported module calls
an async function. That covers module function bodies as well as the module's
top level, and the function may be exported. It also covers a call through a
value located in module code while the unit has an async lambda. Legacy fails
any such call ("module-private async callables are unsupported",
`src/ParserExpression.cpp:4232`). The VM refuses it rather than modelling that
error. Calling a module's exported async function from the entry is admitted:
legacy runs that.

**`coro-module-before-import`** (review R4). A task started in the entry may
reach, by name or through a value, a function of a module whose text comes
after the spawn: an import later in the file. Legacy's worker does not see
that function (`undefined callable`), but the linked unit already holds it.
Spawns in module code are exempt: they run only after that module's own
imports, or they are init tasks. A spawn inside an entry function counts at
the function's text position, which is conservative.

**Admitted and equal to legacy** (witnesses in §5):

- tasks (thread or async) whose code runs stateless module functions, through
  any depth of nested imports;
- module functions that start and join *threads* when called after
  initialisation (they may not call async functions: private-async);
- async functions defined in modules, called from the entry;
- the importer's own globals, snapshotted at the spawn (the task sees the
  value at the spawn, and its writes stay in the task: CP22 L2);
- exported module values read by the importer and passed into a task by value;
- tasks started before an import, still running while the module initialises,
  and joined after it, provided their code reaches no function of that module;
- Errors thrown in module code, crossing a task boundary and caught by the
  importer (`await`, `join`) or by module code around a join;
- an uncaught Error from an awaited task, located in the module file.

**Admitted, VM semantics** (no legacy equivalent, or an existing recorded
difference; the result equals the inline program on the VM):

- `cancel(...)` of a task running module code. This is CP23 cooperative
  cancellation (`runtime.cancelled`) and legacy has no `cancel`.
- An uncaught Error from a joined thread: the VM reports `thread: <message>`
  and legacy reports `<message>`. This is the CP23 thread-reporting contract
  (D-060 for Host errors). The same difference exists without modules, and
  the position is the module's.

**Unchanged:**

- The cooperative model (`NIFT_VM_COOPERATIVE_THREADS=1`) keeps CP22's
  whole-unit `coro-module` refusal. `cooperative_switches` now sets
  `CompileLimits::linked` for a linked unit only under that model, so D-055 #2
  requires no new proof. The refusal still reports the unit's start: Review 8
  finding 1, owned by `cand/diag-positions`.
- Persistent sessions are out of scope. A piped session refuses `import` as a
  statement (`statement:Import`) and any coroutine unit (`session:coroutine`).
- `coro-async-value`, R1 `module-host-op`, `import-*` link refusals, and
  `args`/`cmd` in modules are unchanged.
- `NIFT_VM=1` still falls back to legacy for a refused unit.
- `NIFT_VM=classify` reports the run's reason. `classify-all` (no run-time
  switches, inventory tooling F12) now also reports the four new reasons. It
  never reported `coro-module`.

**Semantics recorded for the owner.** For D-041 #3 ("each isolate gets its
own instances"), a task does not re-initialise a module. Under this contract
no admitted task touches module state at all, so the question "copy at spawn
or fresh instance" is not observable on the VM yet. Legacy copies at spawn.

### Conservative approximations (refuse more than necessary, never less)

- Module state is matched by name. A local in a task's code that has the same
  name as an imported module's top-level binding counts as module state.
- A method call on a receiver that may be an object, function or unknown counts
  as a call through a value, even when a user method has that name.
- Any function-valued argument to a built-in or native callee counts as a
  call through a value.
- A module top-level lambda's body counts as initialisation code even if it
  is never called.
- `before-import` treats a spawn inside an entry function as happening at the
  function's text position, whenever that function is called.
- The "may start a task" flag of a function covers lambdas created in its body
  and async lambdas created anywhere in it.
- `coro-module-init-task` does not ask whether a started task would touch
  module state. Starting a task while initialising is refused outright, per the
  ruling.

## 4. Implementation

- `src/rt/compiler_coro_module.{h,cpp}` (new) holds `check_coroutine_modules`
  and the four rules.
- `src/rt/compiler_coro_module_effects.{h,cpp}` (new) holds the analysis: one
  AST pass per function and lambda, memoised call-graph closures, and what a
  call through a value may reach.
- Both are cold: they run once per linked coroutine unit and never for other
  units.
- **Shared code:**
  - `src/rt/compiler.cpp` gains one line in `compile_unit_impl`, right after
    the `session:coroutine` refusal and before `check_coroutine_unit`. The
    check sets `positioned` itself.
  - `src/rt/compiler.h`: `CompileLimits::link` is new; the comment on `linked`
    is updated.
  - `src/rt/compiler_coro.h` includes the new header.
  - `src/rt/compiler_coro_admit.cpp`: `cooperative_switches` derives `linked`.
  - `src/rt/script_runner.cpp` (one line, no line added, still 400) passes
    `&link`.
  - `src/rt/refusal_inventory.cpp` passes `&link` to classify-all.
  - `src/rt/refusal_text.cpp` has the diagnostic phrases.
- **No runtime change.** Heap, snapshot, transfer, scheduler and SourceMap are
  untouched. Worker errors already map to module files through the shared
  `SourceMap`.
- **File-overlap constraint with `cand/diag-positions`.** The lines it owns
  are not edited:
  - the `coro-module` `positioned` handling in `compile_unit_impl`;
  - `compiler_coro.cpp:333-334`;
  - `check_coroutine_program`'s offset fallback;
  - `module_link.cpp`;
  - `module_graph.cpp`.

  `compiler_coro.cpp` is not edited at all. The new call in `compiler.cpp` is
  separated from that candidate's hunk by one unchanged line, and after both
  candidates `compiler.cpp` is 400 lines (the rt-lint limit). On merge,
  `coro-module` (cooperative only) becomes positionless through that
  candidate's change. The two new reasons keep their own recorded positions.

## 5. Tests

`make test-runtime2-coro-module` runs `tests/runtime2/coro_module/coro_module.py`
(also part of `test-rt-vm`). It has 34 cases plus extras. Each case is its own
directory of `.f` files.

- **Parity (14 cases):** VM-default and legacy have the same stdout, stderr
  and exit, with 1 and 4 parallel workers. The engine is proven by
  `NIFT_VM_STATS` (vm_executions 1, legacy_executions 0). The cases cover:
  - nested imports: main → a → b → c, and a task inside a nested module;
  - shared globals: importer snapshots, task writes, by-value module exports;
  - exceptions crossing the module/coroutine boundary: caught via await, join
    and inside the module; an uncaught await;
  - a task across an import;
  - the corpus units 027 and 028.
- **Inline (4 cases):** stdout, exit and message equal the inline program on
  the VM, with a pinned result. They cover cancellation (async, thread, after
  completion) and the uncaught thread Error at its module position.
- **Refusal (16 cases):**
  - `init-task`: direct thread, async call, transitive, transitive through
    another module, deepest nested module, through a lambda value, a failing
    worker (legacy `failed import created worker resources`);
  - `capture`: thread, async, a module's own async function, nested transitive,
    a function argument, a lambda argument, a call through a value, a shared
    mutex, a module timer.

  Each checks exit 1 and empty stdout (every entry starts with `print("pre")`),
  outcome `require-error`, zero executions, the exact reason, the exact
  `file:line:column`, `classify` agreement, `NIFT_VM=1` fallback equal to
  legacy, and legacy's own result printed.
- **Review cases (61 directories, `tests/runtime2/coro_module/review/`,
  expectations in `review/EXPECT.tsv`):** every repro from the review of
  `fff92edd`, copied unchanged.
  - 10 are parity cases, including both stress programs.
  - 49 are refusals, checked like the cases above.
  - 2 are `base-defect`: the inline programs `inl2` and `before-import-inline`,
    which have no imports. They are reported, not asserted (§7).
- **Extras:**
  - the cooperative model still gives `coro-module`, and the same unit runs
    under the default;
  - piped sessions refuse as `statement:Import` / `session:coroutine` with
    nothing run.

`make test-runtime2-coro-module-mutants` (`tests/runtime2/coro_module/mutants.sh`,
a `certify-targets` edge) has 15 mutants. Each one is the normal objects with
`compiler_coro_module.cpp`, `compiler_coro_module_effects.cpp` and
`compiler_coro_admit.cpp` built
`-DNIFT_CORO_MODULE_MUTANT=n`. Each must differ from the normal binary and fail
the suite through its named witness:

| Mutant | Defect | Witness |
|---|---|---|
| 1 | init-task check off | `init-thread` |
| 2 | module-state reads and writes not seen | `capture-thread-state` |
| 3 | capture follows the direct body only | `capture-nested-transitive` |
| 4 | init-task checks the module top level only | `init-transitive` |
| 5 | calls through a value not followed | `capture-through-value-call` |
| 6 | a task's arguments ignored | `capture-function-argument` |
| 7 | module handle state not seen | `capture-module-handle` |
| 8 | the cooperative model admits linked units | the cooperative check |
| 9 | init-task also applied to the entry | `clean-module-worker` |
| 10 | by-value arguments counted as task code | `shared-globals-importer` |
| 11 | built-in callback arguments not seen as calls through a value | `review/mx-evade` |
| 12 | methods resolved by name alone | `review/struct-collide` |
| 13 | module top-level lambdas skipped, and init callbacks not seen | `review/init-lam-ok` |
| 14 | async calls from module code admitted | `review/private-async` |
| 15 | tasks reaching a later import admitted | `review/before-import-direct` |

The `init-lam` evasion needs both the lambda walk and the callback rule. With
the callback rule alone, the refusal moves to the `map(k)` call. Mutant 13
removes both in initialisation code. The position checks of
`init-through-value` and `review/init-lam-*` pin the lambda walk itself.

`test-runtime2-coro-module-sanitize` and `-tsan` run the suite on the ASan/UBSan
and TSan builds. They are part of `test-rt-vm-sanitize` and are not run locally.

## 6. Open questions for the owner

1. **029 and the `loading` units.** Legacy runs 029 (`4 4`). Under the ruling
   the VM now refuses it as `coro-module-init-task`. The refusal is recorded
   class A with the ruling as its approval. Removing it would need modelled
   module-initialisation worker ownership (the proposal's optional step 5),
   which is not built.
2. **Capture breadth.** The ruling is implemented as "no task touches module
   state", including transferable state that legacy copies at the spawn: a
   `counter.f` `bump()` in a thread prints `2` in legacy, and a shared module
   `mutex` prints `2`. Admitting copy-at-spawn module state is a separate
   decision that should go with D-041 #3.
3. **`cancel` in module tasks** is VM-only, because legacy has no `cancel`.
   This is CP23 semantics, recorded here without a new decision.
4. **Classification of the two review refusals.** `coro-module-private-async`
   and `coro-module-before-import` refuse programs that legacy itself fails.
   I recorded them as class I with approval `pending`; there is no ruling yet.

## 7. Pre-existing CP22/CP23 issue (not changed here)

A task whose body calls a function declared after the spawn runs on the VM,
while legacy's worker fails. This needs no imports. Repro `review/inl2`:

```
t := thread(target, 3)
fn(target(x)) { return x * 2 }
print(t.join())
```

The VM prints `6` (exit 0). Legacy reports `thread: first argument must be
callable` (exit 1). `review/before-import-inline` is the same through a
wrapper function, and legacy reports `thread: return: undefined callable:
target`. The base binary behaves identically. The module form of this
(`before-import*`) is refused here as `coro-module-before-import`. The inline
form belongs to the CP22/CP23 owners.

## 8. Review of `fff92edd` (REJECT) and the changes made

| Finding | Severity | Change |
|---|---|---|
| R1: an async function called from module code runs on the VM; legacy fails it (`private-async*`, `module-starts-from-entry`) | BLOCKER | new refusal `coro-module-private-async` |
| R2: init-task evaded by a built-in callback with a function value, and by skipping module top-level lambdas (`init-lam-*`) | BLOCKER | callback rule; lambda bodies walked in initialisation |
| R3a: capture evaded by a built-in callback with an aliased function value (`mx-evade`, `map-alias`, `filter-alias`) | HIGH | callback rule |
| R3b: method calls resolved by bare name (`struct-collide`) | HIGH | an object receiver is always a call through a value |
| R4: a task started before an import reaches the module's function (`before-import*`) | HIGH | new refusal `coro-module-before-import`; inline form recorded in §7 |

Contract claims corrected in §3:

- "module functions that start tasks" now covers threads only;
- "async functions defined in modules" now means called from the entry;
- "tasks across an import" now requires that they reach no function of that
  module.

The v46 corpus outcome in §2 is unchanged: a compile-only sweep at this
revision gives the same reason for all 17 units.
