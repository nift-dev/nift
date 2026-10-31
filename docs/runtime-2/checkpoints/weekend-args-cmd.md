# Weekend candidate: `args`/`cmd` on the VM, and the VM-session `cmd(...)` bug (cand/args-cmd)

Status: **candidate, for independent review**. Not integrated; nothing pushed.

- Base: local `weekend/r2` at `10c41015` (strict VM default).
- Branch: `cand/args-cmd`.
- Owner priority #2 of the weekend: "args/cmd, plus the VM-session cmd() runtime bug".
- Tests: `make test-runtime2-args-cmd` (`tests/runtime2/args_cmd/args_cmd.py`, also under
  `test-rt-vm`); mutation controls `make test-runtime2-args-cmd-mutants`
  (`tests/runtime2/args_cmd/mutants.sh`).

## Target: the six real-run fallback units (owner scope)

The owner's target is six programs the VM genuinely supports. The six are the
real-run fallback units of the G03 proposal study, *Proposal: closing G09
(`import-package`) and G03 (script `args` / `cmd`)*, section B.2 (inv2
`execute.jsonl`). The study is **not in the repository**: it is the read-only
investigation kept in the session scratchpad
(`.../scratchpad/release-plan/proposals/import-package-args.md`, register
`release-plan/refusal-register.md` G03). Nothing in `docs/runtime-2/` or
`tests/runtime2/cp25/` names a six-case list; the TSV rows give unit counts only.

| # | Unit | Program (verbatim) | Legacy semantics it needs |
|---|---|---|---|
| 1 | `vm-corpus/cp11/fallback_args.f` | `print(args.length())`, no arguments | `args` bound, an empty array of strings; `length()` on it |
| 2 | `rt-diff-corpus/scripts/args/main.f` (`main.args`: `one`, `two words`, `--flag`) | `print(args.stringify())` | arguments in order, spaces and a leading dash preserved; JSON rendering |
| 3 | `v44_executable_script_smoke` `001-hello/hello.f` | shebang, comment, `args.size()`, `for(a : args)`, `getenv` | shebang line stripped; reads at top level and in a `for` source; string kinds for `"arg:" + a` |
| 4 | `v44_executable_script_smoke` `007-certify/certify.f` | `#!/usr/bin/env nift` then `print(cmd)`, `args.join("|")`, `pwd()`, `getenv` | `cmd` is the path the OS launcher passes (`./certify.f`), not canonicalised |
| 5 | `v45_cli_invocation_smoke` `002-invocation/invocation.f` | `print(cmd)`, `print(args.join("|"))` with `"hello world" -- --no-process -x` | `cmd` the path as typed; `--` ends option parsing, later tokens are literal arguments |
| 6 | `v44_package_language_smoke` `001-a/a.f` | `args := []` then three `args.push(...)`, `size()`, `args[1]` | legacy's `is_script_invocation` exception: a top-level declaration replaces the injected binding with an ordinary mutable one |

All six run entirely on the VM with legacy's output (witness:
`args_cmd.py` §7, each verbatim, parity with `NIFT_VM=0` and `vm_executions=1`).
Case 6 is why top-level shadowing is admitted (below); no other refusal was
converted into an execution path.

## What was wrong

1. **Scripts.** Legacy installs two bindings for every script
   (`Parser::install_script_invocation_bindings`, `src/ParserScript.cpp`); the VM
   installed them only for sessions (`VmSession::initialize_invocation`). Under the
   strict default every script that names `args` or `cmd` was refused
   (`unresolved-name`, or `free-name` in a function) before running, e.g.
   `rt-diff-corpus/scripts/args/main.f`, `vm-corpus/cp11/fallback_args.f`,
   `v44_executable_script_smoke` `hello.f`.
2. **The VM-session `cmd(...)` runtime bug.** In a VM session `cmd` is bound (a
   string), so `cmd("echo", "hi")` compiled as a call of that string and failed
   *at run time* with "undefined callable: cmd", after earlier statements of the
   same line had run, and the piped session continued with exit status 0:

   ```
   $ printf 'print("before"); c := cmd("echo", "hi"); print("after")\nprint("next")\n' | nift
   before
   error: undefined callable: cmd
   next
   ```

   Legacy calls its built-in `cmd(...)` (the process builder) whatever `cmd` is
   bound to. That builder is deferred (`callable:cmd`, owner deferral), so the
   right VM behaviour is the same refusal as in a script: before the statement
   runs. The bug is the missing precedence (a compile-time name-resolution
   defect), not the builder; the builder is **not** implemented here. Running
   `cmd(...)` on the VM is process execution and stays with the deferred Host
   process work: D-029 (one process model), D-030 and D-034 (`run()` syntax and
   options), D-036 (`cmd` owned by CP16/CP20), on the CP16 Host layer
   (`src/rt/host/`, `cp16-host-ops-design.md`) and CP20 shell spawn
   (`cp20-shell-spawn.md`). This candidate only makes the call refuse before any
   effect instead of failing at run time.

## Contract (legacy oracle: `Parser::install_script_invocation_bindings`, `src/CLI.cpp` dispatch)

Values (identical on both engines; `tests/runtime2/args_cmd/args_cmd.py` §1):

| Entry point | `cmd` | `args` |
|---|---|---|
| `nift f.f a b` / `nift ./f.f` / `nift /abs/f.f` | the path as typed (`f.f`, `./f.f`, `/abs/f.f`) | `["a", "b"]` |
| executable `./f.f a` (`#!/usr/bin/env nift`) | the path the launcher passes (`./f.f`, or absolute) | `["a"]` |
| `nift f.f -- --no-process -i` | `f.f` | `["--no-process", "-i"]` (after `--`, literal) |
| `nift f.f --no-process z` | `f.f` | `["z"]` (`--no-process`, `--fs-root=` are options) |
| `nift --platform=X f.f q` | `f.f` | `["q"]` |
| `nift -e SRC a` / `-c` | `<command-line>` | `["a"]` (`--` and a later `-i` handled as legacy) |
| `nift - a` (program on stdin) | `<stdin>` | `["a"]` |
| `nift -i f.f a` / `nift -i -e SRC a` (session) | as the initial unit, kept for the session | as the initial unit |
| plain `nift` session, `~/.niftrc`, tty REPL | `<repl>` | `[]` |
| `nift eval EXPR` | not bound (legacy: "unknown value"; VM: refused `unresolved-name`) | not bound |

Types: `cmd` a string, `args` an array of strings (empty strings, spaces, non-ASCII
and leading dashes preserved).

Mutability (legacy: `mutable_binding=false`, `deep_readonly=true`):

| Use | Legacy | VM |
|---|---|---|
| read anywhere: top level, functions, lambdas, `thread(...)`, async functions | value | value (same output) |
| `args.push/pop/sort(...)` | runtime error "cannot mutate const array: args", exit 1 | the same error, at the same point, exit 1 |
| `args = ...`, `args[i] = ...`, `cmd = ...`, `cmd += ...` (anywhere, functions included) | fatal error "cannot assign to const binding" at the statement, after earlier statements ran | **D-062 #2, approved break**: a program error rejected before execution (`invocation-assign:<name>`), nothing runs, earlier statements included; exit 1 |
| `args(...)` | runtime error "undefined callable: args" | the same error |
| `cmd(...)` | legacy's built-in builder | **refused before any effect** (`callable:cmd`, deferred) |
| `b := args; b.push(x)` | aliases: `args` grows | **D-040 value semantics**: `b` is a copy, `args` unchanged |

The D-062 diagnostic is a program error, not a missing feature:

```
error: main.f:3:1: invocation-assign:args: cannot assign to `args`: the script invocation bindings `args` and `cmd` are read-only
note: the program was not executed: no part of it ran
note: declare a new binding instead (`my_args := args`), or replace it with a top-level `args := ...` before any use of it
```

No "not supported by the Runtime 2.0 VM yet", no `NIFT_VM=0` instruction, no
request to report it. A VM session uses the same reason under its
`NIFT_VM session refused:` headline (before D-062 it reported the generic
`assign-catch-binding` / `declare-const`). Only the injected bindings are affected;
general assignment (const, immut, catch bindings) is unchanged. Under `NIFT_VM=1`
(explicit fallback mode) the unit falls back to legacy, as every refusal does.

Shadowing: legacy lets a script declare its own `args`/`cmd` (`args := [...]`; the
`is_script_invocation` exception in `ParserExpression.cpp` replaces the injected
binding in that scope), declare one in a block or function, and name a function
or parameter `args`. The VM supports exactly one form, needed by case 6:

| Form | Legacy | VM |
|---|---|---|
| `args := …` / `cmd := …` directly in the entry's top-level statement list, every use after it | the user's ordinary binding from there on | **supported**: the unit compiles without that invocation binding, so the declaration is an ordinary top-level binding (mutable, function reads by the CP12b globals rules) |
| a use before that declaration (incl. its own initialiser `args := args.slice(1)`, or a function reaching it earlier) | the injected value until the declaration | refused before any effect (`unresolved-name`, `free-name`) |
| a shadow in a block or a function; a function or parameter named `args` | allowed | refused (`binding-name-reserved`, `fn-name-reserved`, `param-name-reserved`), unchanged |
| `cmd(...)` after `cmd := …` | legacy's built-in builder | refused (`callable:cmd`), as above |
| the other binding | still bound | still bound (`args := []; print(cmd)` works) |

Refusals are not behaviour changes: the program does not run; `NIFT_VM=0` runs it.

Modules: legacy runs an imported module in a fresh environment. A module's
top-level code cannot see `args`/`cmd` (legacy: "import: unknown value or malformed
expression: args"); a module *function* sees the caller's bindings only through
legacy's dynamic free-name lookup, when called from the entry script. The VM
refuses any module that names either binding before any effect, with the stable
reason `import-scope:invocation-binding` ("an imported module that names the entry
script's `args` or `cmd` is not supported by the Runtime 2.0 VM yet"). An import
does not hide the entry's own bindings.

**This is a TEMPORARY limitation (D-062 #1), not a language rule.** Modules being
unable to see `args`/`cmd` must not become established behaviour: legacy lets an
imported function called from the entry script see them, and that is worth
preserving. Obligation on the packages checkpoint: test module top-level evaluation
against an imported function called from the entry script, and lift this refusal or
take it back to the owner. (The v4.5 CLI handover's "Imports share their caller's
`cmd` and `args`" disagrees with legacy at a module's top level; that is for the same
checkpoint.)

Sessions: unchanged values (the session's namespace already held them); the only
change is that `cmd(...)` is now the refused built-in in every session form (piped:
the session stops, exit 1, nothing of the line ran; tty: the statement is refused,
no effect, the prompt continues). Persistent-session coroutines stay deferred.

`NIFT_VM=0`: unchanged (no legacy file touched).

Struct methods: a method reading `args` is refused (`free-name`), as a method reading
any top-level binding is (CP12b), and as in sessions.

## Implementation (one shared description, the session's binding mechanism)

- `rt/session_invocation.h`: `invocation_bindings(cmd, args)` builds the readonly
  `SessionSymbol`s and values once. `VmSession::initialize_invocation` now uses it
  (same cells, slots and types as before); script units use the same symbols.
- `CompileLimits::invocation` carries them into a script unit (never with a
  session). Inference seeds them exactly as session symbols (`external_`), and the
  compiler installs them as the session's external bindings (`session_bindings_`,
  `Binding::invocation`), so every read path (top level, functions, lambdas) is
  the session one. A script's globals are main's frame slots, so the main prologue
  (`rt/compiler_invocation.cpp`) puts the values in main's first registers, marks
  them initialised, and the existing isolate wrapper copies them into `thread(...)`
  tasks like any global. A unit that names neither binding compiles exactly as
  before (no prologue).
- `compiler_call.cpp`: a call whose callee is the `cmd` invocation binding (or its
  top-level shadow) is the built-in's (`callable:cmd` refusal), in scripts and
  sessions.
- Top-level shadowing: `invocation_shadow()` (`session_invocation.cpp`) finds a
  top-level `name := …`; the prologue and inference then leave that binding out
  and `new_binding` exempts exactly that declaration from `binding-name-reserved`.
- `module_link`: the invocation names belong to the entry unit; a module naming one
  is refused (`import-scope:invocation-binding`, D-062 #1, temporary).
- D-062 #2: `write_refusal()` (`resolver.h`) at every write site of
  `compiler_assign.cpp` gives `invocation-assign:<name>`; `refusal_text.cpp` words
  it as a program error in both the script and the session diagnostic.
- `script_runner`/`CLI.cpp`: `run_script_on_vm` takes the invocation that legacy's
  `set_script_invocation` receives; `NIFT_VM=classify-all` compiles with it.
- Moves with no behaviour change, for rt-lint's 400-line limit: the classify-all
  inventory JSON helpers to `script_runner_stats.cpp`; `infer_session_aliases`
  takes the `CompileLimits`.

Shared code touched: the compiler front end (`compiler.cpp`, `compiler.h`,
`compiler_expr.h`, `resolver.h`), inference (`infer*.{h,cpp}`), the linker and the
script runner. Hence the full `test-rt-vm` gate below.

## Tests

`tests/runtime2/args_cmd/args_cmd.py` (all against `NIFT_VM=0` as the oracle):

1. Parity of stdout, stderr and exit status on every entry point in the table,
   with the default run proven on the VM by `NIFT_VM_STATS` (`vm_executions=1`,
   `legacy_executions=0`); the tty REPL and tty `-i file` through a pty.
2. Runtime-error parity: `args.push` after earlier output, `args.sort`, an
   out-of-range index, `args(...)`.
3. Refused before any effect (a file touched first must not exist; the same program
   under `NIFT_VM=0` creates it, so the effect is real): `cmd(...)`,
   `cmd(...).run()`, `run()`, writes, shadowing, a function named `args`, a struct
   method, a module's top level, a module function.
4. Sessions: piped `cmd(...)` refused, nothing of the line ran, the session stopped;
   the `NIFT_VM=0` control runs the line; `-i -e` with `cmd(...)` and
   `cmd(...).run()`.
5. D-040 control: a copy of `args` is independent on the VM (legacy aliases).
6. `eval` binds neither name on either engine.
7. The six units of the target, verbatim (certify.f through the shebang).
8. Top-level shadowing: mutation of the shadow, `cmd := 5`, a function reading the
   shadow after it, the other binding still bound; refusals for a use before the
   shadow, a shadow initialised from `args`, block and function shadows, a function
   reaching it before its declaration, `cmd(...)` after `cmd := …`.

Mutation controls (`mutants.sh`; each recompiles one object with
`-DNIFT_ARGSCMD_MUTANT=n` and must fail the suite): (1) no script bindings,
(2) `args` loses its last element (wrong value; scripts and sessions), (3) no
`cmd(...)` precedence (the session bug back), (4) modules see the bindings,
(5) writable bindings, (6) no top-level shadowing (case 6 refused again). Results: see *Evidence*.

## Evidence

(filled on the final tip; see the gate table below)

## Open questions

Owner rulings (2026-10-31, D-062): modules stay refused for this candidate as a
temporary limitation (#1); writes are an approved break, rejected before execution
(#2). Remaining:

1. **Other shadowing forms.** Legacy also allows a shadow in a block or function
   and a parameter or function named `args` (0 corpus hits). They stay refused; an
   owner call if wanted.
2. **D-number.** D-062 is the next number on `weekend/r2` and `cand/diag-positions`;
   if another candidate takes it first, renumber at integration.

## Out of scope (owner deferrals, unchanged)

The `cmd(...)` builder (`callable:cmd`), `callable:run`, `method:run`/`method:pipe`
(refused, witnessed pre-effect above); persistent-session coroutines; CP24; metadata
Stage A/B; the `Value` redesign; CLI rewrites. The embedding API (`src/embed`) keeps
its legacy-only invocation (`Engine::execute`), unchanged.
