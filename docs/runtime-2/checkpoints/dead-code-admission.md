# Dead-code admission (C118, revised in C119 and C120)

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

The C119 revision was re-reviewed in REVIEW-C119: the plan is **sound with changes**, with two P1 holes. The
name-certainty option is sound with conditions, and host guard (a) is sound with small changes. C120 makes these changes:

- **Sessions (P1-1):** session units never defer a kind- or name-dependent site (5.2, "Sessions").
- **Name certainty for every callee-named site (P1-2):** this includes the 119 builtin-arity free-function sites. The
  legacy precedence of each built-in against a binding of its name is recorded (5.2, "Name certainty").
- **Name certainty is extended to `callable:*`** with the full coverage list. This makes the owner's
  `if (false) { nope() }` / `print("success")` work under the plan (3, 5.8).
- **Kind certainty is defined per site** ("no member of the operand set is admitted by the operation"), not by a bit
  count (5.1).
- **Bottom is separated from the existing uses of kind 0**, and the assertion is scoped (5.3).
- **5.1(2) is qualified** by the approved semantic differences.
- **Host guard (a) is restated as the proposed slot-2 design:** `export`/`import` are exempt, "call" is defined, the
  check is transitive, and the host set is kept complete (5.5).
- **Five more mutants** (5.7).
- **A new pinned test,** `tests/runtime2/deferral_dependencies.py` (`make test-runtime2-deferral-dependencies`). It
  fails if any of the whole-program refusals the name scan depends on stops refusing.

Inventory identity:

- `src/` is at `0c66cad6`, and the nift sha256 is `8195f1b34f91d018e336451153aa55bf0e05d590f9acfcd0dffa566ba35834e4`.
- The tsv revision is the C120 commit.
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
| `builtin-arity:*` members | 123 | allow-listed: 119 (order probed): 62 method-only, and 57 free-function members **under name certainty** (C120); 4 keep the refusal (`cp`, `mv`, `rm`, `same`: order not probed) |
| `builtin-receiver:*` members | 77 | allow-listed: 77 |
| `method:*` (family row and members) | 41 | allow-listed: 41 |
| `callable:*` (family row and members) | 11 | C119: excluded (dynamic scope, 1.3); **C120: allow-listed under name certainty** (5.2) |
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
| **total** | **280** | **allow-listed 260** (C120; 249 in C119), refusal kept 20 (`assign-free-name`, 4 arity order not probed, 15 other) |

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

**C120: name certainty for callees.** Dynamic scope can supply a name only if some frame binds it. So a callee whose
name is bound **nowhere** in the program cannot resolve through a caller's frame. Legacy then raises
`undefined callable: <name>` whenever the call executes; the review's probes of `exit`, `assert`, `len`,
`to_string`, `stringify` and `nope` all give that message.

The `callable:*` family row and its 10 members are therefore no longer excluded outright. Each is deferred only under
the name-certainty condition of 5.2, and never in a session unit. The tsv marks them "ALLOW-LISTED under name
certainty (C120 plan)".

`assign-free-name`, `assign-free-name:undeclared` and the `free-name*` rows stay excluded. The extension covers callees
only; assignments and reads are not part of it.

The same condition now also applies to the builtin-arity **free-function** sites, because legacy lets a binding shadow
some built-ins (5.2).

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
| `if (os() == "windows") { windows_only() } else { print("linux") }` | `linux` | refused, `callable:windows_only` | **runs (C120)**, under name certainty: `windows_only` is bound nowhere in the program |
| `if (false) { nope() }` / `print("success")` | `success` | refused, `callable:nope` | **runs (C120)**: prints `success` (name certainty) |
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

**The owner's own example under the C120 plan.** The program is `if (false) { nope() }` / `print("success")`.

1. **The name scan (5.2).** It walks every unit of the program (here, only the entry), at every depth, taken or not.
   It finds no variable, parameter, lambda, catch, for or destructuring binding, no fn declaration, no struct or
   struct method, and no import or export named `nope`. The program is not a session unit, and no host-registered
   callable `nope` exists (the CLI registers none).
2. **The site.** `callable:nope` is on the allow-list under name certainty, with the order class "no argument
   evaluated" and the message `undefined callable: nope`.
3. **Compilation.** The call compiles to the deferred op. Its result kind is `{Bottom}`. The `if (false)` body is
   never executed.
4. **Execution.** `print("success")` runs and prints `success`, the same as legacy (rc 0).
5. **The reached variant.** `if (true) { nope() }` raises `undefined callable: nope` at 1:14 with exit 1, as legacy does
   at the same construct.
6. **The negative controls stay refused**, because the scan finds the name:
   - `fn(m()) { fn(nope()) { return 5 }` / `return 0 }` / `m()` / `print(g())`, where g calls `nope()`. Legacy prints 5,
     because it registers the nested fn at run time. This is probe `nf.f` in the C120 record.
   - A free function `helper()` that calls `nope()` from inside a struct method of a struct declaring `nope`. Legacy
     prints 7 (probe `sm.f`).
   - Any session unit.

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
2. **(b) changes nothing for any program legacy runs on a reachable path, up to the approved semantic differences.**
   At a kind-certain site, every kind the operand can have at run time is one the operation rejects, so legacy raises
   there whenever the site executes. The rows' single-kind probes record legacy's message and position. Union sites
   keep the refusal: no behaviour changes, and the union gap stays visible as a class (i) row.

   **The qualification (review P2-3).** Kind certainty is computed under VM semantics. Where an approved VM semantic
   difference changes a value's kind or contents compared with legacy, a site can be kind-certain on the VM while
   legacy errors earlier or elsewhere, or the reverse. These differences predate the plan; the plan adds none.
   - **Example (B5 parameter mutation):** `o := {"a": 1}` / `fn(add(p)) { p["k"] = 5 }` / `add(o)` / `print(o.k)`. The
     VM prints `5`. Legacy errors with `value has no member: k`, because its parameter is a copy.
   - The parity matrix (5.7) therefore compares against legacy **under the VM's approved semantics**: a case whose
     legacy outcome differs only through an approved difference (B5 aliasing and parameter mutation, D-009 #3, the
     async self-recursion qualification, `difference:import-depth`) is pinned as that difference, not counted as a
     deferral failure.
3. **(b) is the bounded subset of (a).** If the owner later approves union support for an operator (an implement
   item), that operator's site moves from "refuse on union" to "perform on union", and the deferred raise of (b)
   becomes the failing branch of that op. Nothing in (b) needs to be undone.

**Kind-certain is decided per site** (review P2-1, replacing C119's "exactly one kind bit"). For each operand of the
site, take its inferred kind set S. The site is kind-certain when all of the following hold:

- Every S is non-empty.
- No S contains `Unknown` or `Bottom` (5.3).
- **No member of S is admitted by the operation.** For a multi-operand site, no combination of members is admitted.
  So legacy errors for every run-time kind the operand can have.
- No S is derived from a name that legacy could resolve through dynamic scope (1.3).

A bit count is the wrong test because several kinds span more than one bit (infer.h:45-63):

- `kKindNumber` is `Int|Frac`.
- `kKindFile` and `kKindStream` are `Handle|bit`.

Examples:

| Site | Operand sets | Kind-certain? |
| --- | --- | --- |
| `"a" - n`, n a Number | {String} and {Int, Frac} | **yes**: `-` admits only number with number |
| `v - 1`, v ∈ {String, Array} | | **yes** |
| `v - 1`, v ∈ {String, Int} | | **no**: Int with Int is admitted; this is the union sub-case, refused |
| `"a" < 1` | | yes |
| `v < "b"`, v ∈ {Int, String} | | no: String with String is admitted |
| `f.length()`, f a File | {Handle\|file bit} | **yes**: `length` admits no handle kind |
| `x.length()`, x ∈ {String, Int} | | no |

The tests (5.7) include Int|Frac operands (`"a" - n` with n assigned both 1 and 1.5) and handle operands (`File`,
`Stream`).
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
3. **Name certainty** (C120; the full rule is in "Name certainty" below). It applies to **every callee-named site**:
   - the `builtin-arity` **free-function** sites;
   - `callable:*`;
   - `static-error:arity`;
   - the L-defer callables;
   - the call-local (i) built-ins.

   It holds when the callee name is bound nowhere in the program. Method forms on a value receiver are not
   callee-named and need no name certainty. Sessions never defer (see "Sessions" below).
4. **Function granularity for declaration-level (i) refusals** that the resolver raises today (`fn-param` variadics,
   `lambda-variadic`): the function or lambda compiles as a stub whose body is the deferred op. Declaring or creating
   it is free; only a call raises.
5. **Class (ii) rows only, plus the single-kind sub-case of the 19 mixed rows.** S and L-lang sites, the 1.3 exclusions
   and every 5.6 item keep the refusal.

**Name certainty (C120; review P1-2 and section 2).** Dynamic scope can only supply a name that some frame binds. A
callee-named site is deferred only when the **name scan** finds no binding of the callee's name. The scan covers:

- **Every unit** of the linked program: the entry and every imported module, transitively. Eval units compiled by
  `compile_eval_program` are whole programs and are scanned as such.
- **Every binding form, at any depth, taken or not**, including code in untaken branches and uncalled functions:
  - variables;
  - parameters;
  - lambda parameters;
  - catch bindings;
  - `for` bindings;
  - destructuring bindings;
  - **fn declarations, including nested and conditional ones.** Legacy registers a nested fn when its declaration
    executes: `fn(m()) { fn(nope()) { return 5 }` / `return 0 }` / `m()` / then `g()` calling `nope()` prints 5.
    The VM refuses it today (`callable:nope`).
- **Struct names, and struct method names.** A free call inside a struct method resolves to the receiver's method in
  legacy. Probe `sm.f`: `helper()` calls `nope()` from `S.run()`, `S` declares `nope`, and legacy prints 7.
- **Import and export names.**
- **Host-registered callables in embedded use.** Legacy consults `host_.has_host_callable` (ParserExpression.cpp:4272;
  embed/Engine.cpp:102).
  - The CLI registers none.
  - An embedding engine must pass its registry to the scan; a registered name counts as bound.
  - An engine that cannot do so turns name certainty off.
- **Names created at run time by `run` or `cmd`: none.** They start separate processes and cannot bind names in this
  one.

**The dependency (a must-not-change item, 5.7).** The scan sees every binding only while every construct that could
bind a name invisibly to the VM stays a whole-program refusal, even in dead code:

- template text and fragments (`opaque:template-text`);
- opaque or irregular parses (`opaque:irregular-legacy-parse` and the other `opaque:*`);
- nested imports (`import-nested`);
- dynamic imports (`import-dynamic`). A deferred `import-dynamic` would raise and never bind, so that one is safe
  either way.

`tests/runtime2/deferral_dependencies.py` (added in C120; `make test-runtime2-deferral-dependencies`, part of
`test-rt-vm`) pins that each of these still refuses a dead-code program that legacy completes. It fails as soon as one
stops refusing. The scan must be extended before that refusal is lifted.

**Messages.** Legacy's undefined-callable message has exceptions: `inject`, `validate`, `copy` and `deepcopy` never
give `undefined callable` (ParserExpression.cpp:4273). The L-defer built-ins have their own legacy errors when legacy
cannot run them (`page: expected one page name`, `inject: source is not readable`). The VM's "not supported" message
for them is VM-defined, because legacy runs them.

**Built-in precedence against a binding of the same name.** This was probed in legacy for every builtin-arity member,
in four forms, with a wrong argument count:

- a caller-frame binding (`fn(h()) { return N(...) }` called from `m()`, which binds `N := (...) => "shadow"`);
- a top-level binding;
- `fn(N(...))`;
- a parameter named N.

| Free-function members | Legacy | Members |
| --- | --- | --- |
| 11 | **the binding wins** in all four forms (prints `shadow`) | `cancel`, `channel`, `deepcopy`, `map`, `recv`, `send`, `set`, `sha256`, `sha256_file`, `stack`, `validate` |
| 50 | **the built-in wins** in all four forms (legacy raises its own error although the name is bound) | `arch`, `attr_escape`, `cat`, `cd`, `close`, `cp`, `env`, `epoch`, `exists`, `getenv`, `hardware_concurrency`, `html_escape`, the 15 `is_*` (including `is_dir` and `is_file`), `ls`, `make_dir`, `max`, `min`, `mkdir`, `mutex`, `mv`, `open`, `os`, `platform`, `pwd`, `range`, `remove`, `rm`, `same`, `setenv`, `sleep`, `stat`, `touch`, `type`, `unsetenv`, `url_encode`, `which` |
| 62 | no free-function site (method form only, or not a bindable name: `atomic<bool>`, `atomic<int>`) | the rest |

Each member row records its result: "C120 precedence" in the tsv disposition. The probe record is `rescan/p9/prec.json`.

- **The hole is currently hidden.** For the 11 binding-wins members, the VM today refuses the shadowing program
  anyway, with `binding-name-reserved`, `fn-name-reserved` or `param-name-reserved`, except for `cancel`, `channel`,
  `recv` and `send`, where the VM already lets the binding win, as legacy does. But `binding-name-reserved` has status
  `implement`, and `fn-name-reserved` cites D-009 #3 (user functions take precedence). Once those are implemented, a
  deferred `sha256(...)` free-function site called through a caller's binding would raise where legacy prints the
  binding's result. Name certainty closes the hole whatever those rows become.
- **The rule is applied uniformly** to all 57 allow-listed free-form members, including the 50 where the built-in
  wins. That is conservative. The precedence record would allow the condition to be dropped for those 50 later, by a
  separate decision.
- **The enclosing statement affects legacy's message.** Inside `return`, legacy prefixes the error
  (`return: return: is_int: expected one value`). For `cat`, `cd`, `exists`, `is_dir`, `is_file`, `make_dir`, `mkdir`,
  `open`, `stat` and `touch`, the message after the prefix is empty, while the top-level form says
  `unknown value or malformed expression: ...`. The reached-variant tests must cover the statement context: `print`,
  `return`, nested `return`, and assignment.

**Sessions (review P1-1). Choice: session units never defer a kind- or name-dependent site.** In a session unit (a
`nift -i` line, or any unit compiled against a session's accumulated state), every allow-listed site keeps today's
refusal. This holds for every site, because every allow-listed site depends on a kind or a name.

The alternative is to route the deferral decisions through the existing session re-check of earlier functions. That
re-check already refuses an earlier function "in earlier input ... of the unit that defined it" when a later line
widens a kind (`data[0].length()` variant). It is rejected for three reasons:

1. **Name certainty cannot be established in a session at all.** Any later line can bind any name
   (`fn(g()) { return nope() }`, then `fn(nope()) { return 5 }`, then `print(g())`: legacy prints 5). A re-check
   would have to record every deferred callee name and test every binding of every later line against it. That is a
   new session-wide invariant with its own failure modes (lines already executed, functions already called).
2. **The kind re-check exists, but it is a refusal mechanism.** The deferral would have to be re-validated, and
   possibly withdrawn, after the earlier line has run, then re-surfaced as a refusal of the later line. Since the
   outcome on any change is a refusal anyway, the deferral saves only the dead-code case in interactive input, where
   dead code is rare.
3. **"Never defer" is one check at the allow-list lookup** (`unit.is_session()`) and needs no new session state. It is
   trivially sound: sessions behave exactly as today. It also leaves D-067 #11 satisfied for scripts, modules and
   eval units, which is where platform-dispatch code lives. Extending deferral to sessions later, through the
   re-check, is possible as its own item.

The two review programs are the session tests (5.7):

- **The kind program:** `data := ["ab"]` / `fn(g()) { return data[0] - 1 }` / `print("defined")` / `data[0] = 2` /
  `print(g())` / `print("end")`. Legacy prints `defined`, `1`, `end`. The VM keeps refusing line 2 with `type:-`
  (probed, C120 record `p9/s/k.in`), and must never raise at line 5.
- **The name program:** `fn(g()) { return nope() }` / `fn(nope()) { return 5 }` / `print(g())`. Legacy prints 5.
  The VM keeps refusing line 1 with `callable:nope` (`p9/s/n.in`).

**Per-site evaluation order and message (review P2-2), as measured in legacy.** In each probe program, `g(x)` prints
`g` and `rr(x)` prints `r`. The evidence programs are in the C119 probe record (session scratch, `rescan/p7/r5.json`,
`r6.json` and `order_family.json`) and summarised in the rows.

| Site (rows) | Legacy order before the error | Legacy message (template) | Evidence | Allow-list |
| --- | --- | --- | --- | --- |
| `builtin-arity:*` free function (`sha256(g(1), g(1))`) | **no argument** evaluated | `<name>: expected ...` (per member; 33 quirk messages kept verbatim; legacy prefixes the enclosing statement, e.g. `return: return: is_int: expected one value` inside nested returns) | prints nothing, then `sha256: expected one string or bytes value` | yes, the 57 free-form members probed (`order_family.json`), **only under name certainty** (C120, review P1-2) |
| `builtin-arity:*` method form (`rr("a").trim(g(1))`) | the receiver, then the error; no argument | `<name>: expected no arguments` | prints `r`, then the error | yes, the 62 method-only members (no name certainty needed) |
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
| `callable:*` (`abs(g(1), g(2))`) | no argument | `undefined callable: <name>` (never for `inject`, `validate`, `copy`, `deepcopy`: ParserExpression.cpp:4273) | prints nothing, then the error | **yes, under name certainty** (C120); excluded otherwise (1.3) |
| L-defer callables, call-local (i) built-ins | no argument (the VM's own error) | `<construct> is not supported by the Runtime 2.0 VM` | VM-defined: legacy runs these | yes, under name certainty |

The tsv rows carry the order class in the per-row evidence or the family row. The allow-list file is generated from
this table and checked against the tsv by a test (5.7).

**Atomicity.** The prefix evaluations are exactly legacy's. Any side effects they have (output, stores, host calls)
happen before the raise in both engines. Allocation of the error message uses the ordinary error path. If that
allocation fails, the process still exits with status 1 through the existing allocation-failure path. It never exits
with 70.

### 5.3 Bottom kind (review P2-3)

- A new kind bit, `kKindBottom = 1u << 24`, marks "this expression never produces a value: it raises".
  - It is distinct from every value kind, from `Unknown` (bit 10), and from the handle and coroutine bits (11-23,
    infer.h).
  - Like the handle bits, it lies outside `kKindAll`, so no generic value operation admits it.
- **Kind 0 keeps its existing meanings (review P2-2).** Bottom is not kind 0, and kind 0 is not made invalid. The
  existing `kinds == 0` tests in src/rt are listed below. None of them may see Bottom as a substitute for its own
  meaning:

  | Site | Existing meaning of kind 0 | With Bottom |
  | --- | --- | --- |
  | infer_types.cpp:186-187 | an empty shape (empty array element, no fields) for shape equality | `{Bottom}` is not empty; a shape holding Bottom is compared as non-empty (conservative: may refuse, never admits more) |
  | infer_collection.cpp:11, 68 | an empty-tuple or empty element ("stored nothing") | not reached by pure Bottom: an array or object literal with a `{Bottom}` element raises before construction, so the literal itself is `{Bottom}` (strictness rule below) |
  | session_shape.cpp:16 | the element of an empty array in a session global | sessions never defer (5.2), so no Bottom here; asserted |
  | infer_callable.cpp:46 | a pending callee (B4b 8) | a `{Bottom}` callee is a trap site, decided before the pending test |
  | infer.cpp:314, 378 | session bottom arguments, session empty callee | sessions never defer; asserted |
  | builtin_bind.cpp:11, 45 | `Receiver::None`, "never executed" parameter | a `{Bottom}` receiver is a trap site, decided before binding |
  | op_admission.cpp:149 | no admission (refuse) for 0 or Unknown | `{Bottom}` is tested **before** the mask tests (`(k & ~kKindCollection) == 0` and the like) and yields the trap |
  | infer_for.cpp:24 | a missing value kind becomes Unknown | a `{Bottom}` source is a trap site |

- **The assertion is scoped.** "No operand kind is 0 or mixes Bottom with a value kind" is asserted only at the entry
  of the admission and deferral decisions: `op_admission`, builtin receiver binding, and the deferral precondition
  check. It does not cover the inference internals above, where 0 legitimately means empty or pending.
- **The audit is part of the checkpoint.** Every `kinds == 0` and `kinds & ~mask` test that can now see a
  Bottom-carrying set is listed in the checkpoint and given a test.
- **Strictness rule.** An expression that unconditionally evaluates a `{Bottom}` subexpression (an operand, argument,
  receiver, literal element or callee) is itself `{Bottom}`. Only a join of control-flow paths (conditional,
  branch-assigned binding, `try`/`catch` binding) drops Bottom.
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

**Option (a), the proposed slot-2 design (C120, revised per REVIEW-C119 section 3).** The slot-2 repair also admits
host ops in uncalled functions and untaken branches. It replaces the per-proto scan with an order condition checked on
the syntax of the linked program.

A program with static imports is admitted when both conditions hold.

**E, the entry's leading import block.** Every top-level statement of the entry before its last `import(...)` is itself
an `import(...)`. This is stricter than the section 11 proposal, which also allowed call-free declarations before the
imports.

**M, call-free and host-free module initialisation, transitively.** For **every module in the import graph**
(followed from the entry through every module's own imports, diamonds included; the VM refuses
`import-reinitialised` anyway), the module's initialisation code contains:

- no host op, and
- **no call**, where:
  - **Initialisation code** means the module's top-level statements, and the expressions they evaluate, at any
    nesting. The bodies of `fn` declarations, struct methods and lambdas are not included: they run only when called,
    and calling one is itself a call.
  - **A call** is any call node anywhere in that code:
    - user-function, lambda and method calls;
    - calls inside `$[...]` interpolation;
    - calls inside template text (refused anyway, `opaque:template-text`);
    - calls inside object and array literals;
    - struct construction (`S(...)`, which runs field initialisers);
    - any built-in that receives a callable (`[1].map(f)`, `sort_by`, `thread(f)`), because it runs user code.
  - **Exempt:** the `import(...)` and `export(...)` statement forms. Every module uses `export(f)` in call syntax, and
    without this exemption (a) would admit nothing.
  - **Allowed (the optional item, included):** CallNative of a **non-host** built-in that receives no callable, such
    as `print`, `abs`, string and collection methods, and `to_string`. Such a built-in performs no host operation and
    runs no user code, so it cannot be "a host op before a module read". This is the allowance the section 11 proposal
    already had. It is included because module initialisation commonly computes constants and prints banners, and
    refusing those would leave most real modules outside (a). The allowance is sound only if the host set is complete
    (next item), and the completeness test is its precondition.

**H, a complete host set.** Today the host set is the contiguous block `HostOpen .. StreamEof` (`natives::host_builtin`,
src/rt/natives/builtin.h:159), and `os`, `arch` and `platform` are in it. A `static_assert` at builtin.h:156 guards
appends to builtin_list.inc. Two additions:

- A table-driven test (`tests/runtime2/host_set_complete.py`) lists every built-in that touches the file system, the
  environment, processes, the clock or the console input. The starting point is legacy's shell-completion name list
  (`nift_completion_names`, src/CLI.cpp:2786), filtered to the effectful names, with `read`, `read_all`, `read_line`,
  `run` and `cmd` added explicitly. For every such name that has a VM native, the test
  asserts that the native lies in the host block. Future `read`, `run` and `cmd` natives fail the test until they join
  the block.
- The `static_assert` comment names that test.

*Soundness argument: no host op can run before the last module read.*

- Module reads happen at load. The review's legacy probes confirm that legacy reads no module lazily or a second time
  after entry execution starts:
  - `cp("m2.f", "m.f")` after the leading imports overwrites the module, and `v()` still returns 1;
  - the same holds in `thread(v)`.
- Under E, the entry executes nothing but imports until every module is read and initialised.
- Under M, initialisation runs no host op, and no user code that could run one: no call, apart from the two exempt
  forms and non-host, callback-free natives. Under H, "non-host" is accurate.
- So the first host op of the program can execute only after the leading block completes. That is after the last
  module read, wherever the op sits:
  - a called function;
  - an uncalled function;
  - a taken or untaken branch;
  - a lambda;
  - a method body;
  - a module function called after the imports.
- No later module read exists: imports outside the leading block violate E, and nested and dynamic imports are refused
  (`import-nested`, `import-dynamic`).
- The R1 property (host ops observe the same file system state and trace order as in legacy) therefore holds for
  every host op. For code that never runs it holds vacuously.
- No reachability or effect analysis is needed.
- The coroutine-module rules (CP22/CP23) are unchanged, because nothing precedes the leading block.
- Low (review): with no calls in init, an error-order difference (a missing later module against an earlier module's
  init error) is pre-existing linker behaviour, not introduced here.
- Programs that fail E, M or H keep today's refusal, `module-host-op:<name>`, with a note naming the failed condition.

**Tests** (extending `tests/runtime2/module_host_order.py`). Each admitted case must match legacy's output, file tree
and `NIFT_TRACE_HOST` trace.

Admitted:

- a host op after all imports (read-only and mutating);
- a host op in a function called after the imports;
- a host op in an imported module's function called after the imports;
- a host op in an uncalled function;
- **a host op in an untaken branch;**
- **an `os()`-guarded branch** (`import("./m.f")` / `if (os() == "windows") { print(pwd()) }`, prints `linux`);
- **a lambda containing a host op, created after the imports**;
- **a module whose init is `fn(f()) { ... }` / `export(f)`** (admitted: `export` is exempt);
- a module whose init calls `print` and `abs` (admitted: the optional item);
- a diamond import (`a` and `b` both import `c`).

Refused:

- host-before-import (top level, in a function called before an import, in a loop);
- copy-over-module and remove-module before an import;
- host-in-module-init with a later sibling import;
- a nested-module function called during init;
- **a call inside `$[...]` interpolation in module init** (`x := "$[f()]"`);
- a call inside an object literal in init;
- struct construction in init;
- a callback built-in in init (`[1].map(f)`);
- `cd` or `setenv` before an import;
- a declaration before the first import in the entry (E).

**Mutants:**

| Mutant | Killed by |
| --- | --- |
| The prefix check (E) disabled | host-before-import, copy-over-module |
| The module-init check (M) disabled | host-in-module-init |
| "Calls count as safe" in init | the nested-module-function-during-init case |
| **`export` not exempted** | the admitted `export(f)` module |
| **An interpolation call missed in init** | the `$[f()]` refused case |
| Not transitive (only direct imports checked) | host-in-module-init of a second-level module |
| Callback natives counted as non-host calls | the `[1].map(f)` refused case |
| A host native outside the host block | `host_set_complete.py` |
| The existing R1 mutant (`-DNIFT_R1_MUTANT`) | kept |

**Size:**

- src: about **150-220 lines**:
  - new `module_host_order.{h,cpp}` (E, the init walker with the call definition, the transitive graph walk, the
    non-host callback-free predicate);
  - about 20 lines in `script_runner.cpp` and `refusal_inventory.cpp`, which call it instead of the blanket scan (one
    shared implementation);
  - `module_host_op.h` keeps `first_module_host_op` for the message.
- tests: about 350-450 lines.
- This is essentially the section 11 estimate (120-180), plus the call definition and the host-set test.

What it covers:

- the platform-dispatch pattern after imports;
- the 27 `module-host-op:*` rows in the usual layout (imports first; module init free of calls apart from
  `export`/`import` and non-host natives).

What it does not cover: host ops or user calls in module initialisation, and imports after other statements. Both stay
refused.

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
- **Dynamic scope:** `free-name:*` and `assign-free-name` (1.3; D-067 #2c). Since C120, `callable:*` is deferred under
  name certainty only; any callee whose name is bound somewhere keeps the refusal.
- **Session units:** no deferral at all (5.2, "Sessions").
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
- **Bottom.** At the admission and deferral entry points, no operand kind is 0 or mixes Bottom with a value kind (the
  scoped assertion, 5.3). A `{Bottom}` operand compiles to the trap. The existing kind-0 uses keep their behaviour:
  empty arrays, pending callees, session bottom arguments and `Receiver::None` give the same results as today.
- **Kind certainty per site.** `"a" - n` with n ∈ {Int, Frac} is deferred. `v - 1` with v ∈ {String, Int} stays
  refused. Handle operands (`File`, `Stream`) under `length` and `+` are deferred.
- **Name certainty** (5.2):
  - `if (false) { nope() }` / `print("success")` prints `success`.
  - `if (false) { windows_only() }` runs.
  - These stay refused: the caller-frame shadow (`fn(h()) { return sha256("a", "b") }` called from `m()`, which binds
    `sha256`); a top-level binding, fn declaration or parameter named like the callee; a nested fn declaration
    (`nf.f`); a conditional fn declaration; a struct method name (`sm.f`); an import or export name; a
    host-registered callable in an embedded engine (an embed test registers `nope`).
- **Statement context of messages.** The reached variants of each allow-listed site run in `print(...)`, `return`,
  nested `return` and assignment. They match legacy's prefixed messages (`return: return: ...`), including the
  empty-message quirk of the ten file-system built-ins.
- **Sessions:** the kind and name programs of 5.2 keep today's refusal on line 2 and line 1 respectively, and neither
  ever raises at its later line.
- **Dependencies:** `tests/runtime2/deferral_dependencies.py` (in place since C120).
- **Approved differences:** the parity matrix pins cases whose legacy outcome differs only through an approved
  difference (5.1(2)) as that difference.
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
| A dynamic-scope callee is deferred although its name is bound in some frame, or an `assign-free-name` site is deferred | the exclusion test: the 1.3 programs stay refused |
| Wrong position | the reached variants' position check |
| Catchability flipped | `try`/`catch` parity |
| A deferred site is dropped from classify-all or from the inventory mapping | the 5.4 test |
| An S row is deferred instead of refused | the S matrix: every 1.5 program stays refused in every dead-code context |
| Allow-list default-on (an unlisted reason is deferred) | the allow-list test: an unlisted site in dead code stays refused |
| The async raise aborts the scheduler instead of rejecting the future | the coroutine test |
| **Session deferral frozen at definition** (session units defer like scripts) | the session tests: the kind program raises at line 5 or the name program at line 3, instead of the line-2 or line-1 refusal |
| **A builtin-arity free-function site deferred despite a binding of its name** (name certainty skipped for built-ins) | the caller-frame `sha256` shadow program, which must stay refused |
| **The name scan misses a nested or conditional fn declaration, or a struct method name** | `nf.f`, the conditional-declaration case and `sm.f`, which must stay refused |
| **`export` not exempted** in the host guard's module-init check | the admitted `export(f)` module (5.5) |
| **An interpolation call missed in module init** | the refused `$[f()]` module (5.5) |
| The kind-0 assertion widened to the inference internals | the existing B4b and session suites (empty arrays, pending callees) |

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
- **Sessions and eval** (corrected in C120, review P1-1). Session units never defer: every allow-listed site in a
  session line keeps today's refusal, and the existing re-check of earlier functions is unchanged. An `eval` unit
  (`compile_eval_program`) is a whole program: it decides its deferred sites itself, with the name scan over that
  unit. No deferred state, Bottom kind or allow-list decision leaks between eval calls. (C119 wrote that "each session
  line decides its deferred sites independently". That was wrong: a later line can widen a kind or bind a name that an
  earlier line's function depends on.)
- **The name-scan dependency** (5.2). Template text and fragments, opaque parses, nested imports and dynamic imports
  stay whole-program refusals, in dead code too, until the name scan is extended to them. This is pinned by
  `tests/runtime2/deferral_dependencies.py`.
- **Coroutines.** A raise inside an async body rejects that future (CP22 L1/L4).
- B4b/B5 contracts (stores, ownership, cycles, atomic commit) and R1.
- No re-baselining.

**Risks:**

- legacy's quirk messages (about 33 `builtin-arity` members, e.g. `deepcopy(1, 1)`: "malformed expression: 1, 1"),
  which must be reproduced verbatim;
- catchability classes;
- loop-context paths where legacy's message differs (prepared loops);
- the method-form receiver evaluation when the receiver is itself a refused construct;
- the statement-context prefixes of legacy's messages (5.2);
- the 11 built-ins where a binding of the name wins in legacy. Their free-function sites are deferred only under name
  certainty, and that must stay so if `binding-name-reserved` and `fn-name-reserved` are implemented.

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
- `name_scan.{h,cpp}` (new): the whole-program name scan of 5.2, including nested and conditional fn declarations,
  struct methods, imports and exports, and the embed registry hook;
- `script_runner.cpp` and the session compiler: the `is_session` gate;
- `tests/runtime2/cp25/inventory.py`: `reason_key`.

The estimate is **about 550-800 lines of src**. C119 estimated 400-600; the name scan adds about 120-180 and the
session gate about 10. The tests and mutants are about 1300-1500 lines: the dead-code matrix, the union guards, the
order prefixes and statement contexts per site, and the name-scan and session cases. The host-guard (a) work is slot 2's
(5.5) and is not included.

**Coverage of the X and L-defer set:**

| Set | Rows | Deferred by the plan | Kept refused |
| --- | --- | --- | --- |
| X (ii) | 280 | **260** (93%): 249 as in C119, plus the 11 `callable:*` rows under name certainty | 20: `assign-free-name` (dynamic scope), 4 arity members not probed, 15 inference, coroutine scans, handle flow, name resolution or order not probed |
| X single-kind sub-cases of the mixed (i) rows | 19 | **15** (the 16 `type:<op>` rows less `for-source-union`) | 4: order not probed |
| L-defer | 10 | 9 callables under name certainty; `import-dynamic` only if nested imports in dead code are separately made admissible (today `import-nested` fires first) | 1 |
| **X + L-defer, total** | **309** | **284** (92%) | 25 |

Of the 260 X (ii) rows, **the following are deferred only under name certainty**:

- the 57 allow-listed builtin-arity free-function members (their 62 method-only members need no name certainty);
- the 11 `callable:*` rows;
- `static-error:arity`.

The 9 L-defer callables are also deferred only under name certainty. In session units nothing is deferred.

C118 claimed about 284 of 299 X rows plus the 10 L-defer rows. C119 covered 273 of 309. C120 restores 11 rows by
extending name certainty to `callable:*`, while adding the name condition to the builtin-arity free forms.

The call-local class (i) families are also covered, for their unreachable occurrences: missing built-ins (under name
certainty), lazy arguments, the arities legacy accepts, `sort`/`sort_by`, `?.`/`?[]`, statement forms and variadic
stubs.

**Platform patterns.** Section 3 lists 11 patterns refused today. The C120 plan fixes 7 of them:

- **the owner's `if (false) { nope() }` / `print("success")`, which now prints `success`;**
- `windows_only()`;
- `"a" - 1`;
- `sha256("a", "b")`;
- `s.win_only()`;
- the variadic lambda;
- `secure_random_bytes`.

With host guard (a) in slot 2, the `pwd()` after an import also runs, which makes 8. Three stay refused: `run` and
`ffi_open` (deferred-named, D-065), and the ternary spelling.

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
3. **Name certainty** (5.2): approve deferring a callee-named site (`callable:*`, the builtin-arity free forms,
   `static-error:arity`, the L-defer callables) when the whole-program scan finds no binding of the name. This is what
   makes the owner's own `nope()` example print `success`. Without it, `callable:*` stays excluded (1.3), the
   builtin-arity free forms must also be excluded (P1-2), and coverage falls to 206 of 309. Reads and assignments
   of free names (`free-name:*`, `assign-free-name`) stay excluded until D-067 #2c is decided.
4. **Sessions:** confirm that session units never defer (5.2). Extending deferral to sessions through the re-check is
   a later item.
5. **Approve design (b)** (5.1-5.4): kind-certain, allow-listed deferral, a Bottom kind and inventory mapping. The
   union operations (5.1 (a)) are class (i) implement items, to be decided per operator.
6. **Host-import guard:** option (a), the proposed slot-2 design (5.5: the leading import block; call-free module init
   with `export`/`import` exempt and non-host, callback-free natives allowed; transitive; a complete host set), or
   option (b) (accept the gap).
7. **The 5 mixed rows with a verified position** (1.4: `ForBinding`, `Throw`, `interpolation:non-scalar`,
   `type:member`, `type:script-return`): confirm that D-067 #1 covers the single-kind sub-case of these rows. The
   union sub-case stays a capability gap, so the rows stay PENDING-OWNER.
8. **Decide separately on each 5.6 item**, or defer them.
9. **The checkpoint's place:** its own checkpoint after slot 2 (recommended, 6).
10. **The slot label of the D-067 #7 required features** (review of C117, P2). This is raised by the coordinator and is
   not changed here.
