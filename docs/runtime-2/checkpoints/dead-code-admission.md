# Dead-code admission (C118, revised in C119)

Owner ruling 2026-11-11 02:53, D-067 #11. This ruling is a correction: Option A of rescan-post-cp1.md section 13.1 is
withdrawn. The ruling says:

- **Reject unconditionally invalid code even when it is unreachable.** This means malformed syntax, malformed
  declarations, invalid assignment targets, and constructs the language contract defines as compile-time errors.
- **Do not reject a program because an unreachable branch contains an execution-dependent error.** Examples are an
  undefined name, an unavailable platform-specific operation, a run-time type error and an unsupported host op. Raise
  the error only if execution reaches it: `if (false) { nope() }` / `print("success")` must print `success`.
- An unsupported VM feature in an unreachable branch should ideally not refuse the program either. If it is reached,
  it is an ordinary error, never a silent legacy fallback.
- Correct platform-conditional execution is a Runtime 2.0 requirement.
- The 00:29 early-refusal policy (D-013) remains valid for genuinely static errors only.

This document reclassifies the inventory under that rule and proposes a bounded implementation. **Nothing here is
implemented. Section 5 is a PROPOSAL.**

The C118 version was reviewed in REVIEW-C117-C118, which judged it **sound with changes**. Its central flaw was in
section 5.3 item 1: the kind-based X rows are not errors that legacy raises whenever the construct executes. C119 makes
the changes that review required:

- the type rows are reclassified;
- the design is restricted to kind-certain sites;
- dynamic-scope sites are excluded;
- a per-site evaluation-order allow-list is added;
- the empty kind is replaced by a distinct Bottom kind;
- deferred sites are mapped into the inventory;
- the host-guard options, the S departure and the extended must-not-change and mutant lists are written up.

Inventory identity:

- `src/` is at `0c66cad6`, and the nift sha256 is `8195f1b34f91d018e336451153aa55bf0e05d590f9acfcd0dffa566ba35834e4`.
- The tsv revision is the C119 commit.
- Probes ran on that binary, both with the VM default and with `NIFT_VM=0`, in the session scratch directory.

## 1. Classification of the class (ii) rows, and the mixed kind-based rows

### 1.1 C119 reclassification: 19 kind-based rows are mixed, class (i)

Review P1-2 found that the `type:<op>` rows do not refuse only a certain error. They also refuse when the operand's
**inferred kind is a union**, and legacy runs that case whenever the actual run-time kind fits. C119 probed every
kind-based (ii) row in two forms:

- a **single-kind** sub-case, where the operand kind is certain;
- a **union** sub-case, where a function returns one of two kinds and is called with the fitting one.

Both programs and both outputs are recorded in each row's evidence column.

| Row | Union sub-case (legacy, VM refuses with the row's reason) | Single-kind sub-case (legacy) |
| --- | --- | --- |
| `type:+` | `v + 1`, v ∈ {array, number}, is 2: prints `3` | error |
| `type:-`, `type:*`, `type:/`, `type:%` | v ∈ {string, number}, is 2: `1`, `2`, `2`, `0` | error (`arithmetic operators require numeric operands`) |
| `type:<`, `type:<=`, `type:>`, `type:>=` | `false`, `false`, `true`, `true` | error (`ordering comparisons require ...`) |
| `type:unary` | `-v`: `-2` | error |
| `type:increment` | `v++`: `3` | error (`increment/decrement requires a numeric lvalue`) |
| `type:compound-assignment` | `v -= 1`: `1` | error |
| `type:length` | v ∈ {number, string}, is "ab": `2` | error (`length: unsupported for this value`) |
| `type:member` | v ∈ {array, object}, is `{"k": 7}`: `7` | error (`value has no member: k`) |
| `type:script-return` | `return g(false)`, g ∈ {array, number}: legacy renders `2` | error (`script return value is not directly renderable`) |
| `type:for-source-union` | `x := [1]`, then an untaken reassignment to an object, then `for (e : x)`: prints `1` | error |
| `interpolation:non-scalar` | `"v=$[v]"`, v ∈ {array, number}: `v=2` | error |
| `coro-future-flow:ForBinding` | `for (e : v)`, v ∈ {future, array}, is `[2]`: `2` | error |
| `coro-future-flow:Throw` | `throw f(false)`, f ∈ {future, error}: catches, `c` | error |

These 19 rows move from `outcome:ii-B` to **`outcome:i` (mixed)**. The union sub-case is a capability gap. The
single-kind sub-case is still ii-B. Each row keeps its C115 tag as a "Previous outcome (C115-C118)" note, and its
deadcode tag now reads "deadcode:X for the single-kind sub-case only".

Rows checked and **not** mixed, with the union probe in the C119 record:

| Row | Union probe result |
| --- | --- |
| `builtin-receiver:*` | runs on the VM; the run-time receiver check already matches legacy |
| `expression-statement` | runs on the VM |
| `context:assignment-type` | legacy also errors |
| `compound-operator` | legacy also errors |
| `coro-future-flow:Member` | legacy also errors |
| `coro-future-flow:CompoundAssign`, `IncDec` | the union is refused first at the call, as `coro-future-flow:Call` |
| `static-error:member-assign` | refused first as `struct-value` |
| `error-result-expression` | refused first as `interpolation:unknown-type` |

`type:index` (a D row) also has legacy-running union cases (`v[0]`, v ∈ {number, array} or {object, array}: `1`). It
stays D, with that evidence, because its emission is downstream of another refusal.

**Counts after C119:**

| | Before (C115-C118) | C119 |
| --- | --- | --- |
| (i) | 152 | **171** |
| (ii) | 321 | **302** |
| (iii) | 3 | 3 |
| (iv) | 1 | 1 |
| D | 39 | 39 |
| E | 22 | 22 |

Within the 302 (ii) rows, the contracts are B-same-position 290 and B-different-position 12. The 12 are the 7 "fix:
take legacy's position" rows and the 5 rows whose more precise position is approved (1.4).

### 1.2 The 302 class (ii) rows: S / X / L

| Class | Rows | Meaning |
| --- | --- | --- |
| **S**, inherently static | 10 | rejected before execution even when unreachable (the 00:29 policy applies) |
| **X**, execution-dependent | 280 | raise only when reached |
| **L-lang**, genuine language limitation | 2 | the whole-program refusal stays |
| **L-defer**, deferrable VM limitation | 10 | the VM could defer with a bounded run-time check |
| **total** | **302** | |

The 19 mixed (i) rows carry X for their single-kind sub-case (section 5.2), so the C118 count of 299 X rows becomes
280 + 19 sub-cases.

The X rows excluded from deferral for dynamic scope are covered in 1.3.

**S (10), each justified from the contract or the docs:**

| Row | Justification |
| --- | --- |
| `static-error:redeclared` | semantics.md: "redeclaring in the same scope is an error" (a malformed declaration) |
| `opaque:struct-body`, `import-opaque:struct-body` | V4.2-LANGUAGE-CAMPAIGN.md: fields are declared with `name := initializer`, and the initializer is mandatory (a malformed declaration) |
| `import-opaque:legacy-runtime-error` | malformed source in an imported module (`1 ^ 2` is not an operator) |
| `number-literal` | a malformed literal (an integer outside the signed 64-bit range) |
| `assign-catch-binding` | an invalid assignment target: the catch binding is immutable (V4.6-BATCH-4-RECOVERABLE-ERROR-DESIGN.md) |
| `async(callable, ...) was removed; ...`, `await(...) was removed; ...` | removed syntax (CP18 public syntax refinement, V4.5-NATIVE-RUNTIME-SHELL.md) |
| `text-type-name` | a malformed expression (`x := await` with no operand) |
| `jump-outside-loop` | `break`/`continue` outside a loop is malformed by placement. **No contract text calls it a compile-time error, so the owner must rule (1.5).** |

**`number-literal` (review Low).** Its status `implement` (13:26: raise legacy's message "integer literal outside
signed 64-bit range") and its class S are compatible:

- The implement item gives the pre-execution rejection legacy's wording.
- S says the rejection stays pre-execution even when the literal is unreachable. Legacy accepts the literal in dead
  code, so this is a deliberate departure (1.5).

**L-lang (2):**

- `import-limit`: the module-graph depth is static program structure and a resource bound (D-067 #4). Imports run at
  load, not in branches.
- `string-arg-type`: D-018, CP11 M3. The binding types of the whole unit depend on how a string literal argument is
  written.

**L-defer (10):**

- `import-dynamic`: raise legacy's "import path must be a string expression" when reached.
- The legacy built-ins missing on the VM: `callable:inject`, `callable:build_repair`, `callable:ffi_buffer`,
  `callable:ffi_bytes`, `callable:ffi_sizeof`, `callable:ffi_snapshot_bytes`, `callable:ffi_struct`,
  `callable:package_path` and `callable:page`. These raise "not supported by the Runtime 2.0 VM" when reached. They are
  subject to the name-certainty condition of 5.2 item 3.

**X (280), by family:**

| Family | Rows | Deferral (section 5) |
| --- | --- | --- |
| `builtin-arity:*` members | 123 | allow-listed: 119 (order probed); 4 keep the refusal (`cp`, `mv`, `rm`, `same`: order not probed) |
| `builtin-receiver:*` members | 77 | allow-listed: 77 |
| `method:*` (family row and members) | 41 | allow-listed: 41 |
| `callable:*` (family row and members) | 11 | **excluded: dynamic scope (1.3)** |
| `assign-free-name` | 1 | **excluded: dynamic scope (1.3)** |
| static checks (`static-error:arity`, `struct-arity`, `member-assign`, `push-arity`) | 4 | allow-listed: 4 |
| struct use (`struct-no-field`, `struct-private`) | 2 | allow-listed: 2 |
| `push-receiver`, `compound-operator`, `builtin-arg-fault-context`, `coro-thread-callee`, `expression-statement`, `coro-future-flow:CompoundAssign` | 6 | allow-listed: 6 |
| coroutine admission (`await-outside-coroutine-unit`, `coro-module-before-import`, `coro-module-private-async`) | 3 | keep the refusal (coroutine admission scans, 5.6) |
| `coro-future-flow:Member`, `coro-future-flow:method`, `coro-future-flow:IncDec` | 3 | keep the refusal (order not probed) |
| binding-type and context (`context:assignment-type`, `context:assignment-error`, `context:struct-field`, `for-missing-write`) | 4 | keep the refusal (flow-sensitive inference, 5.6) |
| handle flow (`host-handle-flow`, `non-transferable`) | 2 | keep the refusal (handle-flow analysis, 5.6; `non-transferable` is refused first as `host-handle-flow`) |
| `error-result-expression` | 1 | keep the refusal (refused first as `interpolation:unknown-type`) |
| name resolution (`fn-called-before-declaration`, `import-scope`) | 2 | keep the refusal (name resolution and order; D-067 #2c not decided) |
| **total** | **280** | **allow-listed 249**, refusal kept 31 (12 dynamic scope, 4 arity order not probed, 15 other) |

**Measured today (C115, unchanged binary).** Before the C119 reclassification, 316 of the 321 refusals also fired in
an `if (false)` branch, where legacy completes the program. The exceptions:

- `coro-module-before-import` does not fire (it is flow-sensitive).
- `import-dynamic` in a dead block is refused as `import-nested`.
- Three module-initialisation rows (`import-limit`, `import-opaque:*`) run at load.

So the X and L-defer rows **violate the D-067 #11 rule today** wherever they appear in code that does not run.

### 1.3 Excluded: names legacy resolves through dynamic scope (review P2-1)

Legacy resolves free names dynamically: through the caller's frame at run time. The review's probes show this:

| Program | Legacy | VM |
| --- | --- | --- |
| `fn(g()) { return h(1) }` / `fn(m()) { h := (x) => x + 1` / `return g() }` / `print(m())` | `2` | refused, `callable:h` (a `callable:*` member) |
| `fn(g()) { q = 3 }`; `m` declares `q := 1`, calls `g()` and returns `q` | `3` | refused, `assign-free-name` |
| `h.size()` in `g`, read through the caller's `h` | `2` | refused, `free-name` |

The dynamic-scope question is deferred (D-067 #2c). Deferring these sites to a run-time "undefined" error would in
effect implement lexical-only scope, and would turn programs legacy runs into errors when the name is bound in a
caller. These sites therefore **keep the refusal**, reachable or not:

- the `callable:*` family row and its 10 members (`callable:nope`, `exit`, `undefined_fn`, `undefined_thing`,
  `assert`, `len`, `nosuch`, `to_string`, `unknown`, `stringify`);
- `assign-free-name`;
- the related rows `assign-free-name:undeclared`, `free-name`, `free-name:dynamic-caller`, `free-name:undeclared` and
  `free-name:legacy-binding`. These are (i), D or deferred-named, and are marked so that no later plan defers them.

Each of these 17 rows carries "deferral-excluded (C119, review P2-1)" in its tsv disposition.

### 1.4 Position review (D-067 #1)

The independent position review of C117/C118 verified all 10 "VM more precise" positions; all 10 pass. Under D-067 #1
(owner 2026-11-11 02:53, the common diagnostic decision: the VM may point at the actual offending construct once a
review verifies it), C119 does the following:

- **Approved (5 (ii) rows):** `builtin-arity:deepcopy`, `coro-module-private-async`, `method:nope`,
  `non-transferable`, `static-error:push-arity`. Their status is `approved`, and the approval cites D-067 #1 and the
  review.
- **Position verified, but not approved (5 rows):** `coro-future-flow:ForBinding`, `coro-future-flow:Throw`,
  `interpolation:non-scalar`, `type:member`, `type:script-return`. C119 found these mixed (1.1): their union sub-case
  is a capability gap, which no position ruling closes. They stay PENDING-OWNER, with the verified position recorded.
  The owner should confirm (section 7).
- **Review Lows:**
  - `type:script-return` could point at the returned value (1:8) instead of the statement (1:1). This is noted in
    the row.
  - `non-transferable` emits its long message as the reason string. It is added to the rename proposals
    (rescan-post-cp1.md section 10 item 3a).

### 1.5 S rows are a deliberate departure from legacy (review P2-6)

Legacy accepts **every** S construct in dead code. Each of the following, wrapped in an untaken branch, an uncalled
function or an uncalled lambda, followed by `print("n")`, prints `n` in legacy. The VM refuses each with the row's own
reason:

- `break` outside a loop;
- a redeclaration;
- an out-of-range literal;
- an assignment to a catch binding;
- `x := await`;
- a malformed struct body (in a function and in an `if`);
- `async(...)`;
- `await(...)`.

The probe for each row is in the row's evidence, marked "C119 legacy-in-dead-code probe".

The owner rule allows this departure only for code that is malformed whatever runs. Nine of the ten rows have contract
text for that. **`jump-outside-loop` has none: the owner must confirm S, or move it to X.**

**`text-type-name`, corrected probe (review Low).** C118's `if (false)` wrapper reported `unresolved-name` first, so it
did not test the S construct. C119's probe places `x := await` in an uncalled lambda: `f := () => { x := await }`, then
`print("n")`. The VM refuses with `text-type-name`; legacy prints `n`.

## 2. The class (i) capability gaps in unreachable code

Each (i) row's legacy-running program was wrapped twice:

- its executable statements inside `if (false) { ... }`, with top-level declarations kept;
- inside an uncalled function.

Legacy completes every wrapped program. The measurement covers the 152 (i) rows of C118. The 19 rows moved to (i) in
C119 were already measured as (ii) rows: their refusal fires in dead code (1.2).

| Family | Rows measured | Whole program still refused (if-false / uncalled) | Could a guarded run-time error op replace the refusal? |
| --- | --- | --- | --- |
| host op in a program with imports (`module-host-op:*`) | 27 | 27 / 27 | see 5.5: the slot-2 repair option (a) admits these without reachability analysis |
| missing built-in callables (`sorted_map`, `module_path`, `queue`, ...) | 10 | 10 / 10 | **Yes:** compile the call as a raise op ("not supported by the Runtime 2.0 VM"), under the name-certainty condition |
| lazy built-in argument order (`builtin-lazy-args:*`, `builtin-default:get`) | 7 | 7 / 7 | Yes: raise at the call when reached |
| bytes (`builtin-bytes:*`) | 4 | 4 / 4 | Yes, with a Bottom result kind (no bytes kind enters inference) |
| built-in arity that legacy accepts (`keys`, `copy`, `move`, `entries`, `values`) | 5 | 5 / 4 (`move`: the uncalled wrapper hit `assign-free-name` first) | Yes: raise at the call |
| methods (`method:sort`, `sort_by`) | 2 | 2 / 2 | Yes: raise at the call |
| context and loop-context built-ins | 8 | 8 / 8 | Yes for the call-local ones (`object-length`, `collection-empty`, `number-to-int`, `%=`); the binding-type ones need inference (5.6) |
| futures in ordinary expressions (`coro-future-flow:*` (i)) | 8 | 8 / 8 | No (bounded): future kinds flow through inference and coroutine admission |
| `?.` / `?[]` | 3 | 3 / 3 | Yes: an expression-local raise (then implement, D-067 #7) |
| statements (`statement:Export`, `statement:FnDecl`, `statement:Command`) | 3 | 3 / 3 | Yes (Export: a no-op; FnDecl and Command: a statement-local raise) |
| variadics, spread, destructuring | 4 | 4 / 4 | Yes at function granularity: an uncalled variadic function or lambda compiles to a stub that raises when called; spread and patterns raise at the statement |
| structs | 9 | 8 / 8 (`struct-after-statement`: the wrapper moved the statements, an artefact) | No for the declaration model (plan-time schemas, CP2); yes for use-site rows |
| opaque source (`opaque:*` (i)) | 4 | 4 / 4 | No: the VM parser cannot structure the source; this needs lazy parsing (5.6) |
| imports and exports | 6 | 4 / 4; `import-collision` → `export-nested`; `import-name-clash` uncalled → `import-scope` | No: these are link-time properties of the module graph |
| inference (`type:element-assignment`, `type:for-source`) | 2 | 2 / 2 | No (bounded): kind inference |
| capacity | 1 | 1 / 1 | No: whole-unit resource bounds |
| syntax and singles | 36 | 36 / 36 | Per row; most are expression-local (comparison chains, prefix-not, string literals, object keys, parameters) |
| **total measured** | **139** | **135 refuse in both wrappers**; 4 are wrapper artefacts or other reasons | |

Not measured (13):

- the CP24 project callables (they need a project; deferred, D-067 #2d);
- `free-name:dynamic-caller` (excluded, 1.3);
- `unresolved-name`;
- `struct-no-method`;
- `callable:secure_random_bytes` (refused first as `builtin-bytes`);
- the three big capacity programs.

**Conclusion: today the mere presence of any class (i) construct in an unreachable branch or an uncalled function
refuses the whole program.** Each (i) row records this as a `deadcode-presence` note.

## 3. Platform-specific built-ins

Built-ins and behaviours that differ by OS, in both engines:

- legacy: src/ParserHost*.cpp, src/FileSystem.cpp, src/Environment.h, src/Process*.cpp;
- VM: src/rt/natives/host_env.cpp, src/rt/host/*.cpp, src/rt/module_loader.cpp.

| Built-in or behaviour | What differs | On the VM |
| --- | --- | --- |
| `os()`, `platform()` | return `windows`, `macos` or `linux` (Environment.h `host_os`, host_env.cpp) | run |
| `arch()`, `hardware_concurrency()` | host architecture and core count | run |
| `getenv`, `setenv`, `unsetenv`, `env()` | Windows `_putenv_s` (unset = empty value), environment block (host_env.cpp:111,133) | run; refused in a program with imports (`module-host-op:*`) |
| `which` | the search depends on `PATH` and, on Windows, `PATHEXT` (the executable extensions tried); results differ by OS and environment (review Low) | runs (builtin_list.inc:140, host op `Which`); refused in a program with imports |
| paths: separators, drive letters, case, canonical identity | host/path.cpp:29, module_loader.cpp:34 (import identity), FileSystem.cpp | run |
| file system: permissions, symlinks, `stat`, `ls`, `mkdir`, `cp`, `move`, `remove` | FileSystem.cpp (Windows and macOS branches, e.g. lines 97-834) | run (some argument forms refused: `builtin-arity:copy/move`, lazy args) |
| `secure_random_bytes` | CSPRNG: BCryptGenRandom / getrandom / arc4random (ParserHostHelpers.cpp:21-131) | refused (`builtin-bytes:secure_random_bytes`, implement) |
| `epoch`, `sleep` | Windows FILETIME vs clock_gettime | run |
| processes: `run`, `cmd`, pipes, signals, job control, terminal | process_win.cpp vs process_posix*.cpp, Process*.cpp, JobControl.cpp | refused (deferred-named, D-065 #2) |
| FFI: `ffi_open` and library names (`kernel32.dll`, `libc.so.6`, `.dylib`) | the loader per OS | refused (deferred-named, D-065 #3) |
| console colour and terminal | Console.h | n/a in scripts |

**The owner's pattern and variants on Linux** (programs and both outputs are in the probe record):

| Program (the non-Linux branch never runs) | Legacy | VM today | After the C119 plan |
| --- | --- | --- | --- |
| `if (os() == "windows") { windows_only() } else { print("linux") }` | `linux` | refused, `callable:windows_only` | **still refused** (`callable:*` is excluded for dynamic scope, 1.3) |
| `if (false) { nope() }` / `print("success")` | `success` | refused, `callable:nope` | **still refused** (same reason) |
| windows branch `r := run("cmd", "/c", "ver")` | `posix` | refused, `callable:run` | still refused (deferred-named) |
| windows branch `ffi_open("kernel32.dll")` | `posix` | refused, `callable:ffi_open` | still refused (deferred-named) |
| windows branch `x := "a" - 1` | `posix` | refused, `type:-` | runs (single-kind `type:-`) |
| windows branch `print(sha256("a", "b"))` | `posix` | refused, `builtin-arity:sha256` | runs |
| windows branch `s.win_only()` | `posix` | refused, `method:win_only` | runs (receiver kind certain) |
| windows branch `f := (...xs) => xs.size()` | `posix` | refused, `lambda-variadic` | runs (a variadic stub, class (i) part of the plan) |
| macOS branch `secure_random_bytes(4)` | `other` | refused, `builtin-bytes:secure_random_bytes` | runs (a call-local (i) raise) |
| `import("./m.f")`, then `if (os() == "windows") { print(pwd()) }` | `linux` | refused, `module-host-op:os` | runs only under host-guard option (a) (5.5) |
| `p := os() == "windows" ? getenv("USERPROFILE") : getenv("HOME")` | `true` | refused, `opaque:irregular-legacy-parse` (the ternary spelling) | still refused |
| the same with `if`/`else` and assignment | `true` | runs | runs |
| `platform()` if/else-if chain; `arch() != ""` | `l`; `true` | run | run |

Today, 11 of the 15 patterns are refused on Linux, although the code that would fail is never reached.

**The owner's own example** (`if (false) { nope() }`) is **not** fixed by the C119 plan. `nope` is an unbound callee,
and legacy would resolve it through a caller's frame if it were called from one (1.3). Fixing it needs the
dynamic-scope decision (D-067 #2c), or an owner ruling that a callee bound in no unit of the program may be deferred
(section 7, item 3). That item is the name-certainty condition of 5.2 applied to `callable:*`.

## 4. Impact

### 4.1 The corpus base, reproducibly

The corpus base is measured as follows:

- **Corpora:** the shared inventory corpora of `tests/runtime2/cp25/inventory.py` (its `collect` subcommand, all
  corpora).
- **Collect command:** `python3 -B tests/runtime2/cp25/inventory.py collect --nift $PWD/nift --out $OUT`, run in the
  worktree on the pinned binary.
  - It writes `$OUT/classify-all.jsonl`, one record per **unit**, from `NIFT_VM=classify-all`.
  - It also writes `$OUT/execute.jsonl`, one record per **entry**, with one or more **executions** per entry.
- **The 318 base:** units in `classify-all.jsonl` with at least one non-consequential reason (`reasons[]` items without
  `consequential`). On the C119 collect, 588 units give 318 such units: 266 with outcome `fallback`, 32 with
  `program-error`, and 20 `compiled` units that still record a non-consequential reason.
- **Reconciliation with the review:**
  - The review's **266** is the classify-all `fallback` units: the same file, outcome `fallback` only.
  - The review's **298** is the number of `execute.jsonl` **executions** whose outcome is `fallback`. The 607 entries
    give 298 fallback, 240 vm and 58 vm-error executions. One entry can have several executions, and execute
    measures the run, not admission.
  - So 318 (units, admission, any non-consequential reason) ⊇ 266 (units, outcome `fallback`). 298 is a different
    unit of count.
- **Legacy completion:** for each of the 318 units, the unit's directory was copied to scratch and
  `NIFT_VM=0 nift <file>` was run there with `PATH=/usr/bin:/bin` and a scratch `HOME`.
  - The record is the C118 probe file `corpus.json` (session scratch, `rescan/p6/corpus.json`), one entry per unit:
    unit id, first reason, outcome class, legacy exit code.
  - 317 units ran.
  - The workspace unit `rt-diff-corpus/workspaces/script-outside|main.f` needs its workspace and was not run.
  - The collect output has no legacy-completion field, which is why this separate record exists. A test-side helper
    that writes this field is part of the checkpoint's tests (5.7).

### 4.2 Results

- Legacy completes **136** of the 317 units:
  - 98 refused with a class (i) reason;
  - 18 E (F12);
  - 17 deferred-named;
  - 3 approved.
- It fails in the other 181.
- **Every unit refused with a class (ii) reason (75) also fails in legacy.** No corpus program today is refused only
  because of an X error in dead code. The 75 are counted by C118 class. 7 of them are refused first by a row that
  C119 moved to (i): `type:+`, `type:increment`, `type:unary`, `type:<`, `type:script-return`,
  `type:compound-assignment` and `type:length`, one unit each. All 7 also fail in legacy, so no corpus program is
  refused only because of a union sub-case either. By C119 class this is 68 (ii) units plus 7 mixed (i) units.
- How many of the 98 class (i) units fail only because the construct sits in code they never run is not measured. It
  needs coverage, so 98 is an upper bound.
- **Platform-conditional code in the corpus:** 1 refused unit calls `os()`, `platform()` or `arch()`. It is an
  integration smoke, refused for a package import error. The corpora are Linux-centred, so they understate the
  impact.
- **Cross-platform impact:** a script that guards platform-specific code with an `os()` check is refused on every
  platform whose branch contains any refused construct. The guard cannot help, because admission is whole-program.
- **The most important patterns:**
  1. platform dispatch to platform-only helpers or libraries;
  2. feature probes and fallbacks (`if (exists(...)) { new_api() } else { old_api() }`);
  3. rarely taken debug and error paths;
  4. library modules that define many functions, most of them uncalled by a given script;
  5. defensive branches with deliberately invalid operations.

## 5. Smallest sound implementation strategy (PROPOSAL, not implemented)

### 5.1 Design choice: defer only kind-certain sites (option b)

The review offered two designs for kind-based sites:

- **(a) A run-time-checked op.** At a site such as `v - 1` with v ∈ {string, number}, emit an op that checks the
  actual operand kinds. It raises legacy's error if they fail, and **otherwise performs the operation**.
- **(b) Defer only sites whose operand kind is single and certain.** Every union site keeps today's refusal.

**Choice: (b).** The reasons:

1. **(a) is implementing the operation, not deferring an error.** The "otherwise performs the operation" half needs
   the semantics of the operator for every kind in the union:
   - `-` on number;
   - `+` on number, string and array;
   - ordering on numbers and on strings;
   - `length` on string and array;
   - member read on object;
   - rendering for `script-return`;
   - iteration for `for` over array or object;
   - interpolation of each scalar kind;
   - `throw` of an error value;
   - iteration over an awaited value.

   It also needs the VM's typed code paths to accept a value whose kind is known only at run time, with the matching
   result kind, the inference consequences of that kind, and the store and ownership effects of the result. That is
   exactly the union capability that makes these rows class (i) (1.1). It is feature work under the (i) rows. It needs
   its own implement decision per operator, conformance tests against legacy for each kind combination, and the
   inference changes. It is not a bounded dead-code change, and it is not covered by D-067 #11.
2. **(b) changes nothing for any program legacy runs on a reachable path.** At a kind-certain site, the single kind is
   the run-time kind on every execution that reaches the site, so legacy raises there whenever the site executes. The
   rows' single-kind probes record legacy's message and position. Union sites keep the refusal: no behaviour changes,
   and the union gap stays visible as a class (i) row.
3. **(b) is the bounded subset of (a).** If the owner later approves union support for an operator (an implement
   item), that operator's site moves from "refuse on union" to "perform on union", and the deferred raise of (b)
   becomes the failing branch of that op. Nothing in (b) needs to be undone.

**Kind-certain** means all of the following:

- The operand's inferred kind set is exactly one kind bit.
- It is not `Unknown`.
- It does not contain `Bottom` (5.3).
- It is not derived from a name that legacy could resolve through dynamic scope (1.3).
- For receiver sites, the receiver is not a struct kind whose schema could supply the member or method. For example,
  `method:*` on a struct receiver stays with the struct rows.

### 5.2 Mechanism: a deferred-error op at allow-listed sites only

The compiler already has the raise-at-point primitive used by the CP25 corrections: `UnitCompiler::legacy_fatal`
(compiler_rules.cpp:273). It emits `OpCode::Undefined` with a message note at the construct's offset, then a dead
`LoadNull`. It is used today in three places:

- user-function argument-count mismatches (compiler_call.cpp:97);
- const writes (compiler_const.cpp:55);
- `struct has no method` (compiler_structs.cpp:208).

**Proposal:**

1. Add a single helper, `UnitCompiler::deferred_error(at, message, dst, site_id)`, beside `legacy_fatal`. It compiles
   the site's legacy-ordered prefix (item 2), then the raise, and gives `dst` the kind `Bottom`.
2. Add a static **allow-list** (`deferred_sites.inc`), one entry per site, with these fields:
   - the reason;
   - the order class (the per-site table below);
   - legacy's message template;
   - legacy's position rule;
   - catchability.

   At each listed site, `return fail(node, "<reason>")` becomes `deferred_error(...)` **only if** both:
   - the site is on the allow-list;
   - the site's precondition holds: kind-certain operands, and the name-certainty condition (item 3) for callee
     sites.

   Every other site, and every site of a reason not on the list, keeps `fail()` unchanged. Membership is a lookup,
   never a default.
3. **Name certainty** applies to the callee-named sites: `static-error:arity`, the L-defer callables and the
   call-local (i) built-ins. It holds when the callee name is bound to no variable, parameter, function or import
   anywhere in the program, across all units. In that case no caller frame can supply it, and legacy's built-in or
   top-level resolution is the only one. Otherwise the site keeps the refusal. (`callable:*` stays excluded whatever
   this check says, unless the owner rules otherwise, section 7.)
4. **Function granularity for declaration-level (i) refusals** that the resolver raises today (`fn-param` variadics,
   `lambda-variadic`): the function or lambda compiles as a stub whose body is the deferred op. Declaring or creating
   it is free; only a call raises.
5. **Class (ii) rows only, plus the single-kind sub-case of the 19 mixed rows.** S and L-lang sites, the 1.3 exclusions
   and every 5.6 item keep the refusal.

**Per-site evaluation order and message (review P2-2), as measured in legacy.** In each probe program, `g(x)` prints
`g` and `rr(x)` prints `r`. The evidence programs are in the C119 probe record (session scratch, `rescan/p7/r5.json`,
`r6.json` and `order_family.json`) and summarised in the rows.

| Site (rows) | Legacy order before the error | Legacy message (template) | Evidence | Allow-list |
| --- | --- | --- | --- | --- |
| `builtin-arity:*` free function (`sha256(g(1), g(1))`) | **no argument** evaluated | `<name>: expected ...` (per member; 33 quirk messages kept verbatim) | prints nothing, then `sha256: expected one string or bytes value` | yes, the 119 members probed (`order_family.json`: 119 error-before-args) |
| `builtin-arity:*` method form (`rr("a").trim(g(1))`) | the receiver, then the error; no argument | `<name>: expected no arguments` | prints `r`, then the error | yes (same 119) |
| `builtin-arity:cp`, `mv`, `rm`, `same` | not probed | | | **no**: they keep the refusal |
| `builtin-receiver:*` (`rr(5).index_of(g(1))`) | the receiver, then the error; no argument | `<name>: unsupported for this value` | `r`, then the error; 21 members with arguments probed error-before-args, 56 take no arguments | yes, 77 (single-kind receiver; unions already run) |
| `method:*` unknown method, binding receiver (`x.nope(g(1), g(2))`) | the receiver binding, then the error; no argument | `unknown value or malformed expression: <source text>` | prints nothing, then the error | yes, 41, **binding or plain-expression receivers only**. With a call receiver (`rr("a").nope(...)`), the VM today refuses `opaque:legacy-runtime-error` first, so that form stays refused |
| binary `type:+ - * / %` (single-kind) | **both operands**, left to right, then the error | `arithmetic operators require numeric operands` | `g("a") - g(1)`: `g`, `g`, then the error | yes, single-kind sub-case |
| ordering `type:< <= > >=` (single-kind) | both operands, then the error | `ordering comparisons require two numbers or two strings` | `g`, `g`, then the error | yes, single-kind |
| `type:unary` | the operand, then the error | `unary arithmetic operators require a numeric operand` | `g`, then the error | yes, single-kind |
| `type:compound-assignment` | the right-hand side, then the error | `arithmetic operators require numeric operands` | `x -= g(1)`: `g`, then the error | yes, single-kind |
| `type:increment` | nothing beyond the lvalue | `increment/decrement requires a numeric lvalue` | prints nothing, then the error | yes, single-kind |
| `type:length` | the receiver, then the error | `length: unsupported for this value` | `rr(5).length()`: `r`, then the error | yes, single-kind |
| `type:member` | the receiver, then the error | `cannot access member '<k>' because the current JSON value is not an object` for a call-result receiver; `value has no member: <k>` for a binding receiver (both on an array) | `rr([1]).k`: `r`, then the error | yes, single-kind; the message depends on the certain receiver kind |
| `type:script-return` | the value, then the error | `script return value is not directly renderable` (no position) | `return rr([1])`: `r`, then the error | yes, single-kind |
| `type:for-source-union`, `interpolation:non-scalar`, `coro-future-flow:ForBinding`, `coro-future-flow:Throw` | not probed for order | | | **no**: the single-kind sub-case keeps the refusal until probed |
| `static-error:arity` (user function) | **all arguments**, then the error | `callable argument count mismatch: <name>` | `f(g(1), g(2))`: `g`, `g`, then the error (the VM already raises this at the point) | yes, under name certainty |
| `static-error:arity` (`print` with 2 arguments) | no argument | `print: expected one value` | prints nothing, then the error | yes |
| `static-error:push-arity` | no argument | `push: expected one argument` | prints nothing, then the error (the VM already raises at the point) | yes |
| `static-error:struct-arity` | no argument | `struct constructor argument count mismatch: <S>` | `P(g(1), g(2))`: nothing, then the error | yes |
| `static-error:member-assign` on an object | no right-hand side | `member assignment requires a struct instance: <name>` | `o.k = g(2)`: nothing, then the error | yes, single-kind (object) |
| `struct-no-field` | the receiver binding, then the error | `struct has no field: <f>` | prints nothing, then the error | yes |
| `struct-private` | the receiver binding, then the error | `private struct field: <f>` | prints nothing, then the error | yes |
| `push-receiver` | no argument | `unknown value or malformed expression: <source text>` | `o.k.push(g(2))`: nothing, then the error | yes |
| `compound-operator` | the right-hand side, then the error | `<op>: requires atomic<int>` | `x &= g(3)`: `g`, then the error | yes, single-kind |
| `builtin-arg-fault-context` | the receiver, then the faulting argument, then the error | `<name>: argument must be a string` (at the argument) | `s.contains(o.zz)`: the error at 7:18 | yes |
| `coro-thread-callee` | no argument | `thread: expected callable and optional arguments` | prints nothing, then the error | yes |
| `expression-statement` | the object expression, then the error | `cannot render JSON object $[<name>]; select a member first` | `o := {"a": g(1)}`, then `o`: `g` (from the declaration), then the error | yes |
| `coro-future-flow:CompoundAssign` | the right-hand side, then the error | `arithmetic operators require numeric operands` | `fu += g(1)`: `g`, then the error | yes, single-kind (future) |
| `non-transferable` | the arguments, then the error | `thread: argument contains a non-transferable resource` | `g`, then the error; the VM refuses `host-handle-flow` first | **no** (5.6) |
| `error-result-expression` | the callee | `print: parameter expression must resolve to a scalar value` | `h`, then the error; the VM refuses `interpolation:unknown-type` first | **no** |
| `callable:*` (`abs(g(1), g(2))`) | no argument | `undefined callable: <name>` | prints nothing, then the error | **no** (dynamic scope, 1.3) |
| L-defer callables, call-local (i) built-ins | no argument (the VM's own error) | `<construct> is not supported by the Runtime 2.0 VM` | VM-defined: legacy runs these | yes, under name certainty |

The tsv rows carry the order class in the per-row evidence or the family row. The allow-list file is generated from
this table and checked against the tsv by a test (5.7).

**Atomicity.** The prefix evaluations are exactly legacy's. Any side effects they have (output, stores, host calls)
happen before the raise in both engines. Allocation of the error message uses the ordinary error path. If that
allocation fails, the process still exits with status 1 through the existing allocation-failure path. It never exits
with 70.

### 5.3 Bottom kind (review P2-3)

- A new kind bit, `kKindBottom`, distinct from every value kind, marks "this expression never produces a value: it
  raises". The empty set 0 stays invalid: an assertion in the kind join and in the admission checks rejects 0.
- Kind rules:
  - A deferred op's result is `{Bottom}`.
  - `join(Bottom, K) = K` for any K that has a value kind, because the Bottom path never delivers a value.
  - `join(Bottom, Bottom) = {Bottom}`.
- **Every admission check treats Bottom explicitly.** A check of the form `(k & ~kKindX) == 0` would pass vacuously on
  0. On `{Bottom}` it fails, because Bottom is not X. A site whose operand is exactly `{Bottom}` is itself unreachable,
  because the operand raised first. It compiles to a trap (`OpCode::Undefined` with the note "unreachable after a
  deferred error") whose result is also `{Bottom}`. It is never admitted as if its operand had some kind.
- **Dead code cannot change reachable admission.** Bottom reaches a reachable site's operand only through a join
  (`x = cond ? bad() : 1`, or an assignment in an untaken branch). There it is dropped, so the reachable kinds are the
  same as with the dead branch removed. This is checked by the non-poisoning test and its mutant (5.7).

### 5.4 Inventory visibility (review P2-4)

- **classify-all:** a unit with a deferred site keeps the outcome it would otherwise have (`compiled`). It also
  records each deferred site in `reasons[]` as `deferred:<reason>`, with the site's line and column, marked
  non-consequential. The stats line gains a `deferred` count.
- **inventory.py:** `reason_key()` strips the `deferred:` prefix before matching, so `row_for("deferred:<reason>")`
  finds `<reason>`'s exact row, or its family row `<prefix>:*`.
  - The report shows the deferred sightings in their own column.
  - `--check` applies the row's status to them as it does to refusals: a deferred sighting of a PENDING-OWNER row
    still blocks with exit 3.
  - A `deferred:` reason with no row is a G1 failure.
  - A deferred site is never counted as a fallback entry.
- **Test:** `if (false) { print(sha256("a", "b")) }` / `print("n")`. classify-all must show
  `deferred:builtin-arity:sha256` at 1:20, and `inventory.py report` must map it to the `builtin-arity:sha256` row with
  that row's status. A mutant that omits the record fails the test.

### 5.5 Host-import guard: options for the owner (review P2-5)

Today the R1 guard refuses any host op in a program with imports. It uses `first_module_host_op`
(module_host_op.h:29), which scans every proto, including uncalled functions and untaken branches. The guard call is
at script_runner.cpp:325-339. So `import("./m.f")`, then `if (os() == "windows") { print(pwd()) }`, is refused. This
conflicts with the platform-dispatch requirement (D-067 #11). The slot-2 repair (D-067 #8) is the place to decide.

**Option (a): the slot-2 repair also admits host ops in uncalled functions and untaken branches.**

The repair rule (rescan-post-cp1.md section 11) admits a program with imports if both conditions hold:

1. Every import of the entry is in its **leading import block**: the imports are the first statements, before any
   other statement.
2. The **initialisation code of every module** in the graph contains no host op and no call.

*Soundness argument: no host op can run before the last module read.*

- Module reads happen at load. Under condition 1, the entry's imports are processed, and every module is read and
  initialised, before the first non-import statement of the entry executes.
- Under condition 2, module initialisation itself executes no host op. It also calls no function that could execute
  one.
- So the first point at which any host op can execute is after the leading block completes. That is after the last
  module read, wherever the host op sits:
  - a called function;
  - an uncalled function;
  - a taken or untaken branch;
  - a lambda;
  - a method body.
- No later module read exists: imports outside the leading block violate condition 1, and dynamic imports are
  refused (`import-nested`, `import-dynamic`).
- The R1 property ("host ops observe the same file system state as in legacy, because all module reads precede them")
  therefore holds for every host op in the program. That includes host ops in code that never runs, for which the
  property is vacuous.
- No reachability or effect analysis is needed. The guard's per-proto scan is replaced by two syntactic checks: the
  leading block, and the absence of host ops and calls in module init code. Uncalled and untaken host ops become
  admissible.
- Programs that fail either condition keep today's conservative refusal.

What it covers:

- the platform-dispatch pattern after imports;
- the 27 `module-host-op:*` rows in the usual layout (imports first).

What it does not cover:

- host ops in module initialisation code;
- imports after other statements.

Both stay refused, with a diagnostic naming the condition.

**Option (b): the owner accepts the gap.**

- Slot 2 repairs only the reachable case, as currently specified for its implement rows. Host ops in uncalled functions
  or untaken branches of a program with imports stay refused.
- The platform-dispatch requirement is then **not** met for programs with imports, until a later effect or
  reachability analysis (5.6).
- This should be recorded in known-limitations.md as an approved gap, citing the ruling.

Recommendation: (a). It is a syntactic condition, it needs no analysis, and it is what makes D-067 #11 hold for
programs with imports.

### 5.6 Not in the bounded plan (each needs a separate owner decision)

- **Union operations:** the union sub-cases of the 19 mixed rows (5.1 option (a)), as class (i) implement items.
- **Dynamic scope:** `callable:*`, `free-name:*`, `assign-free-name` (1.3; D-067 #2c).
- **Effect or reachability analysis:** coroutine admission scans (`coro-*` over protos), handle-flow analysis
  (`host-handle-flow`, `non-transferable`), capacity limits. The host-import guard is item 5.5, not this.
- **Flow-sensitive inference:** binding-type conflicts (`context:assignment-*`, `for-missing-write`,
  `type:for-source(-union)`, `type:element-assignment`), futures in expressions, kind-limit convergence.
- **Broad lazy compilation or parsing:**
  - opaque source (`opaque:irregular-legacy-parse`, `opaque:template-text`, nesting);
  - the struct declaration model (plan-time schemas; CP2's own work);
  - module-graph properties (collisions, name clashes, dynamic imports beyond the L-defer raise).
- **Sites without order evidence:** the 4 unprobed `builtin-arity` members, `coro-future-flow:Member`, `:method` and
  `:IncDec`, and the single-kind sub-case of `type:for-source-union`, `interpolation:non-scalar`, `ForBinding` and
  `Throw`. They join the allow-list only after an order probe.

### 5.7 Tests, mutants, what must not change, risks

**Tests:**

- **A dead-code matrix per allow-listed site.** Each site is placed in each of these contexts:
  - `if (false)`;
  - an untaken `else`;
  - an uncalled function;
  - an uncalled lambda;
  - an `os()`-guarded branch;
  - a module function that is not called.

  The VM's output must equal legacy's.
- **The reached variants.** Message, position, exit status and earlier output (including the prefix markers `g` and
  `r` of 5.2) must equal legacy's, and `try`/`catch` must behave the same.
- **The union guard.** Each mixed row's union program stays refused with the row's reason.
- **Inference non-poisoning.** The same unit, with and without the dead branch, gives the same kinds for reachable
  code.
- **Bottom.** No kind 0 is reachable (assertion), and a `{Bottom}` operand compiles to the trap.
- **Inventory mapping** (5.4).
- **The allow-list against the tsv.** Every listed reason has a row whose disposition names it allow-listed. No
  excluded (1.3) or S row is listed.
- **The legacy-completion helper** for 4.1.
- **The coroutine case:**
  - `fn[async](af(c)) { if (c) { print(sha256("a", "b")) }` / `return 1 }`.
  - `af(false)` resolves to `1`.
  - `try { await af(true) } catch (e) { print("rejected") }` prints `rejected`: the future is rejected (CP22 L1/L4),
    and the scheduler is not aborted.
  - An uncaught `await af(true)` exits with 1 and prints legacy's message.

**Mutants that must exist, and the test that kills each:**

| Mutant | Killed by |
| --- | --- |
| The deferred op raises at compile time | the dead-code matrix |
| The deferred op does nothing (no raise) | the reached variants |
| A deferred site's operand kind leaks into reachable code (no Bottom, or Bottom not dropped by the join) | the non-poisoning test |
| Bottom is treated as the empty set (checks pass vacuously) | the Bottom test: a `{Bottom}`-operand site is admitted as generic code, and the kind-0 assertion fires |
| The kind-certainty check is removed (a union operand is deferred and raises unconditionally) | the union guard: the union program must stay refused, not raise at run time |
| Evaluation order swapped (arguments evaluated before an arity or callee error, or operands skipped) | the reached variants' prefix markers |
| A dynamic-scope callee is deferred (`callable:*`, `assign-free-name`) | the exclusion test: the 1.3 programs stay refused |
| Wrong position | the reached variants' position check |
| Catchability flipped | `try`/`catch` parity |
| A deferred site is dropped from classify-all or from the inventory mapping | the 5.4 test |
| An S row is deferred instead of refused | the S matrix: every 1.5 program stays refused in every dead-code context |
| Allow-list default-on (an unlisted reason is deferred) | the allow-list test: an unlisted site in dead code stays refused |
| The async raise aborts the scheduler instead of rejecting the future | the coroutine test |

**Must not change:**

- S and L-lang refusals, and every refusal in 5.6.
- **D-013 early refusal on reachable sites that are not allow-listed**, and the 00:29 policy for static errors. A
  deferred site changes timing only for allow-listed sites, and only in that they now raise when reached, as legacy
  does.
- D-013, one engine per unit: never a legacy fallback.
- **Exit status 1, never 70,** on every deferred raise, uncaught or after a rejected future, including when allocating
  the message fails.
- **Heap budget accounting.** The op allocates only the error value on raise. Stubs allocate nothing until called.
  Budget limits and heap statistics for units with unreached deferred sites equal those of the same unit with the
  sites removed.
- **Sessions and eval.** Each compile unit (a session line, an `eval` unit) decides its deferred sites independently.
  No deferred state, Bottom kind or allow-list decision leaks between units or between eval calls. A deferred site
  reached in a later session line raises in that line.
- **Coroutines.** A raise inside an async body rejects that future (CP22 L1/L4).
- B4b/B5 contracts (stores, ownership, cycles, atomic commit) and R1.
- No re-baselining.

**Risks:**

- legacy's quirk messages (about 33 `builtin-arity` members, e.g. `deepcopy(1, 1)`: "malformed expression: 1, 1"),
  which must be reproduced verbatim;
- catchability classes;
- loop-context paths where legacy's message differs (prepared loops);
- the method-form receiver evaluation when the receiver is itself a refused construct.

### 5.8 Size and coverage (revised)

**Size estimate.** The files touched:

- `compiler_rules.cpp` and `compiler_expr.h`: the helper and the Bottom rule;
- `kinds*.cpp`: the Bottom bit, the join and the assertions;
- `deferred_sites.inc`: the allow-list;
- the site conversions in `compiler_call.cpp`, `compiler_keyed.cpp`, `compiler_dispatch.cpp`,
  `compiler_native_args.cpp` (arity, receiver, method and missing built-in sites), `compiler_expr.cpp` (single-kind
  `type:<op>`), `compiler_structs.cpp` (private, no-field, struct arity), `compiler_assign.cpp` (member assignment),
  `compiler_stmt_expr.cpp` and `compiler_thread.cpp` (the thread callee);
- `resolver.cpp`: variadic stubs;
- `refusal_inventory.cpp`: `deferred:` records;
- `tests/runtime2/cp25/inventory.py`: `reason_key`.

The estimate is **about 400-600 lines of src** (C118 estimated 500-800, with fewer sites now). The tests and mutants
are about 1000-1200 lines: the dead-code matrix, the union guards and the order prefixes per site.

**Coverage of the X and L-defer set:**

| Set | Rows | Deferred by the plan | Kept refused |
| --- | --- | --- | --- |
| X (ii) | 280 | **249** (89%) | 31: 12 dynamic scope, 4 arity members not probed, 15 inference, coroutine scans, handle flow, name resolution or order not probed |
| X single-kind sub-cases of the mixed (i) rows | 19 | **15** (the 16 `type:<op>` rows less `for-source-union`) | 4: order not probed |
| L-defer | 10 | 9 callables under name certainty; `import-dynamic` only if nested imports in dead code are separately made admissible (today `import-nested` fires first) | 1 |
| **X + L-defer, total** | **309** | **273** (88%) | 36 |

C118 claimed about 284 of 299 X rows plus the 10 L-defer rows. The C119 plan covers less because of the dynamic-scope
exclusion, the union restriction and the allow-list.

The call-local class (i) families are also covered, for their unreachable occurrences: missing built-ins (under name
certainty), lazy arguments, the arities legacy accepts, `sort`/`sort_by`, `?.`/`?[]`, statement forms and variadic
stubs.

Of the platform patterns in section 3, the plan fixes 5. The owner's `nope()` example and `windows_only()` stay
refused (1.3). `module-host-op` depends on 5.5.

## 6. Place in the owner's sequence

The review recommends **its own checkpoint "dead-code admission", after slot 2**, not folded into slot 1. This
revision adopts that. The reasons:

- It depends on the slot-2 host-guard repair (5.5) and on the per-site order table (5.2).
- Folding it into slot 1 would mix an admission-policy change into feature work.

Effects on the other slots:

- **Slot 1 (infrastructure):** no scope change.
- **Slot 2 (host-import guard):** the owner chooses 5.5 (a) or (b) for the repair. Under (a), slot 2's design
  includes the uncalled and untaken case.
- **CP2:** no scope change. `struct-private` and `struct-no-field` become allow-listed deferred ops. The declaration
  model stays CP2's work.

The checkpoint is gated on an independent review of this revision.

## 7. Owner items

1. **`jump-outside-loop`:** confirm S (a deliberate departure from legacy, 1.5), or move it to X.
2. **Approve the S departure as a whole** (1.5): legacy accepts all 10 S constructs in dead code, and the VM keeps
   refusing them.
3. **The dynamic-scope exclusion** (1.3) stays until D-067 #2c is decided. Optionally, rule that a callee bound in no
   unit of the program may be deferred (name certainty, 5.2 item 3). That would fix the owner's own `nope()` example.
4. **Approve design (b)** (5.1-5.4): kind-certain, allow-listed deferral, a Bottom kind and inventory mapping. The
   union operations (5.1 (a)) are class (i) implement items, to be decided per operator.
5. **Host-import guard:** option (a) (the slot-2 repair covers uncalled and untaken host ops, 5.5) or option (b) (accept
   the gap).
6. **The 5 mixed rows with a verified position** (1.4: `ForBinding`, `Throw`, `interpolation:non-scalar`,
   `type:member`, `type:script-return`): confirm that the position approval applies to their single-kind sub-case,
   while the union sub-case stays a capability gap.
7. **Decide separately on each 5.6 item**, or defer them.
8. **The checkpoint's place:** its own checkpoint after slot 2 (recommended, 6).
9. **The slot label of the D-067 #7 required features** (review of C117, P2). This is raised by the coordinator and is
   not changed here.
