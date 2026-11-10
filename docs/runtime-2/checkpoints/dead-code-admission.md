# Dead-code admission (C118)

Owner ruling 2026-11-11 02:53, D-067 #11 (correction; Option A of rescan-post-cp1.md section 13.1 is withdrawn):

- **Reject unconditionally invalid code even when it is unreachable:** malformed syntax, malformed declarations,
  invalid assignment targets, constructs the language contract defines as compile-time errors.
- **Do not reject a program because an unreachable branch contains an execution-dependent error** (an undefined
  name, an unavailable platform-specific operation, a run-time type error, an unsupported host op). Raise it only if
  execution reaches it: `if (false) { nope() }` / `print("success")` must print `success`.
- An unsupported VM feature in an unreachable branch should ideally not refuse the program either; if reached, it is an
  ordinary error, never a silent legacy fallback.
- Correct platform-conditional execution is a Runtime 2.0 requirement.
- The 00:29 early-refusal policy (D-013) remains valid for genuinely static errors only.

This document reclassifies the inventory under that rule and proposes a bounded implementation. **Nothing here is
implemented; section 5 is a PROPOSAL** for independent review of its soundness argument before any production change.
Inventory identity: `0c66cad6` `src/`, nift sha256 `8195f1b34f91d018e336451153aa55bf0e05d590f9acfcd0dffa566ba35834e4`,
tsv revision = the C118 commit. Probes ran on that binary, VM default and `NIFT_VM=0`, in the session scratch directory.

## 1. The 321 class (ii) rows: S / X / L

Every (ii) row (`outcome:ii-B`) now carries `deadcode:S`, `deadcode:X`, `deadcode:L-lang` or `deadcode:L-defer` in its
tsv disposition, with the justification.

| Class | Rows | Meaning |
| --- | --- | --- |
| **S**, inherently static | 10 | rejected before execution even when unreachable (00:29 policy applies) |
| **X**, execution-dependent | 299 | to be raised only when reached |
| **L-lang**, genuine language limitation | 2 | whole-program refusal stays |
| **L-defer**, deferrable VM limitation | 10 | the VM could defer with a bounded run-time check |
| **total** | **321** | |

**S (10), each justified from the contract or the docs:**

| Row | Justification |
| --- | --- |
| `static-error:redeclared` | semantics.md: "redeclaring in the same scope is an error" (a malformed declaration) |
| `opaque:struct-body`, `import-opaque:struct-body` | V4.2-LANGUAGE-CAMPAIGN.md: fields are declared with `name := initializer`; the initializer is mandatory (a malformed declaration) |
| `import-opaque:legacy-runtime-error` | malformed source in an imported module (`1 ^ 2` is not an operator) |
| `number-literal` | a malformed literal (integer outside the signed 64-bit range) |
| `assign-catch-binding` | an invalid assignment target: the catch binding is immutable (V4.6-BATCH-4-RECOVERABLE-ERROR-DESIGN.md) |
| `async(callable, ...) was removed; ...`, `await(...) was removed; ...` | removed syntax (CP18 public syntax refinement, V4.5-NATIVE-RUNTIME-SHELL.md) |
| `text-type-name` | a malformed expression (`x := await` with no operand) |
| `jump-outside-loop` | `break`/`continue` outside a loop is malformed by placement; **no contract text calls it a compile-time error: owner to confirm** |

**L-lang (2):** `import-limit` (the module-graph depth is static program structure and a resource bound, D-067 #4;
imports run at load, not in branches); `string-arg-type` (D-018, CP11 M3: the binding types of the whole unit depend on
how a string literal argument is written).

**L-defer (10):** `import-dynamic` (raise legacy's "import path must be a string expression" when reached), and the
legacy built-ins missing on the VM whose (ii) probe failed on arguments: `callable:inject`, `callable:build_repair`,
`callable:ffi_buffer`, `callable:ffi_bytes`, `callable:ffi_sizeof`, `callable:ffi_snapshot_bytes`, `callable:ffi_struct`,
`callable:package_path`, `callable:page` (raise "not supported by the Runtime 2.0 VM" when reached).

**X (299), by family:**

| Family | Rows | Legacy diagnostic when reached |
| --- | --- | --- |
| `builtin-arity:*` members | 123 | `<name>: expected ...` (or a legacy quirk message) |
| `builtin-receiver:*` members | 77 | `<name>: unsupported for this value` |
| `method:*` (family row and members) | 41 | `unknown value or malformed expression: x.m()` |
| `callable:*` (family row and members) | 11 | `undefined callable: <name>` |
| `type:<op>` and type checks | 16 | `arithmetic operators require numeric operands`, ordering, unary, increment, length, member, script return |
| coroutine value errors (`coro-future-flow:*` (ii), `coro-thread-callee`, `coro-module-*`, `await-outside-coroutine-unit`) | 10 | future/thread type errors |
| undefined names, user arity, member assignment (`assign-free-name`, `fn-called-before-declaration`, `import-scope`, `static-error:arity`, `static-error:member-assign`, ...) | 6 | `undefined callable`, `assignment to undefined binding`, count mismatch |
| binding-type and context errors (`context:assignment-type`, `context:assignment-error`, `context:struct-field`, `for-missing-write`) | 4 | `cannot change binding type`, loop-context messages |
| struct use errors (`struct-private`, `struct-no-field`, `static-error:struct-arity`) | 3 | `private struct field`, `struct has no field`, count mismatch |
| other (`error-result-expression`, `expression-statement`, `non-transferable`, `host-handle-flow`, `builtin-arg-fault-context`, `interpolation:non-scalar`, `push-receiver`, `compound-operator`) | 8 | render, thread-argument, handle, argument-fault errors |

**Measured today (C115, unchanged binary):** 316 of the 321 refusals also fire in an `if (false)` branch, where legacy
completes the program. Exceptions: `coro-module-before-import` does not fire (flow-sensitive); `import-dynamic` in a
dead block is refused as `import-nested`; three module-initialisation rows (`import-limit`, `import-opaque:*`) run at
load. So the 299 X and 10 L-defer rows **violate the D-067 #11 rule today** wherever they appear in code that does not
run.

## 2. The class (i) capability gaps in unreachable code

Each (i) row's legacy-running program was wrapped twice: its executable statements inside `if (false) { ... }` (top-level
declarations kept), and inside an uncalled function. Legacy completes every wrapped program.

| Family | Rows measured | Whole program still refused (if-false / uncalled) | Guarded run-time error op could replace the refusal? |
| --- | --- | --- | --- |
| host op in a program with imports (`module-host-op:*`) | 27 | 27 / 27 | **No (bounded):** a whole-program R1 order scan over every proto; reachability needs effect analysis (section 5.4). Slot 2's guard stays conservative |
| missing built-in callables (`sorted_map`, `module_path`, `queue`, ...) | 10 | 10 / 10 | **Yes:** compile the call as a raise op ("not supported by the Runtime 2.0 VM") |
| lazy built-in argument order (`builtin-lazy-args:*`, `builtin-default:get`) | 7 | 7 / 7 | Yes: raise at the call when reached |
| bytes (`builtin-bytes:*`) | 4 | 4 / 4 | Yes, if the raise op's result kind is bottom (no bytes kind enters inference) |
| built-in arity accepted by legacy (`keys`, `copy`, `move`, `entries`, `values`) | 5 | 5 / 4 (`move`: the uncalled wrapper hit `assign-free-name` first) | Yes: raise at the call |
| methods (`method:sort`, `sort_by`) | 2 | 2 / 2 | Yes: raise at the call |
| context and loop-context built-ins | 8 | 8 / 8 | Yes for the call-local ones (`object-length`, `collection-empty`, `number-to-int`, `%=`); the binding-type ones need inference (5.4) |
| futures in ordinary expressions (`coro-future-flow:*` (i)) | 8 | 8 / 8 | No (bounded): future kinds flow through inference and coroutine admission |
| `?.` / `?[]` | 3 | 3 / 3 | Yes: expression-local raise (then implement, D-067 #7) |
| statements (`statement:Export`, `statement:FnDecl`, `statement:Command`) | 3 | 3 / 3 | Yes (Export: a no-op; FnDecl and Command: statement-local raise) |
| variadics, spread, destructuring | 4 | 4 / 4 | Yes at function granularity: an uncalled variadic function or lambda compiles to a stub that raises when called; spread and patterns raise at the statement |
| structs | 9 | 8 / 8 (`struct-after-statement`: the wrapper moved the statements, an artefact) | No for the declaration model (plan-time schemas, CP2); yes for use-site rows |
| opaque source (`opaque:*` (i)) | 4 | 4 / 4 | No: the VM parser cannot structure the source; needs lazy parsing (5.4) |
| imports/exports | 6 | 4 / 4, `import-collision` → `export-nested`, `import-name-clash` uncalled → `import-scope` | No: link-time properties of the module graph |
| inference (`type:element-assignment`, `type:for-source`) | 2 | 2 / 2 | No (bounded): kind inference |
| capacity | 1 | 1 / 1 | No: whole-unit resource bounds |
| syntax and singles | 36 | 36 / 36 | Per row; most are expression-local (comparison chains, prefix-not, string literals, object keys, parameters) |
| **total measured** | **139** | **135 refuse in both wrappers**; 4 wrapper artefacts or other reasons | |

Not measured (13): the CP24 project callables (need a project; deferred, D-067 #2d), `free-name:dynamic-caller`,
`unresolved-name`, `struct-no-method`, `callable:secure_random_bytes` (refused as `builtin-bytes` first) and the three
big capacity programs. **Conclusion: today the mere presence of any class (i) construct in an unreachable branch or an
uncalled function refuses the whole program.** Each (i) row records this as a `deadcode-presence` note.

## 3. Platform-specific built-ins

Built-ins and behaviours that differ by OS, in both engines (legacy: src/ParserHost*.cpp, src/FileSystem.cpp,
src/Environment.h, src/Process*.cpp; VM: src/rt/natives/host_env.cpp, src/rt/host/*.cpp, src/rt/module_loader.cpp):

| Built-in or behaviour | What differs | On the VM |
| --- | --- | --- |
| `os()`, `platform()` | return `windows`, `macos` or `linux` (Environment.h `host_os`, host_env.cpp) | run |
| `arch()`, `hardware_concurrency()` | host architecture and core count | run |
| `getenv`, `setenv`, `unsetenv`, `env()` | Windows `_putenv_s` (unset = empty value), environment block (host_env.cpp:111,133) | run; refused in a program with imports (`module-host-op:*`) |
| paths: separators, drive letters, case, canonical identity | host/path.cpp:29, module_loader.cpp:34 (import identity), FileSystem.cpp | run |
| file system: permissions, symlinks, `stat`, `ls`, `mkdir`, `cp`, `move`, `remove` | FileSystem.cpp (Windows/macOS branches, e.g. lines 97-834) | run (some argument forms refused: `builtin-arity:copy/move`, lazy args) |
| `secure_random_bytes` | CSPRNG: BCryptGenRandom / getrandom / arc4random (ParserHostHelpers.cpp:21-131) | refused (`builtin-bytes:secure_random_bytes`, implement) |
| `epoch`, `sleep` | Windows FILETIME vs clock_gettime | run |
| processes: `run`, `cmd`, pipes, signals, job control, terminal | process_win.cpp vs process_posix*.cpp, Process*.cpp, JobControl.cpp | refused (deferred-named, D-065 #2) |
| FFI: `ffi_open` and library names (`kernel32.dll`, `libc.so.6`, `.dylib`) | the loader per OS | refused (deferred-named, D-065 #3) |
| console colour and terminal | Console.h | n/a in scripts |

**The owner's pattern and variants on Linux** (programs and both outputs in the probe record):

| Program (the non-Linux branch never runs) | Legacy | VM today |
| --- | --- | --- |
| `if (os() == "windows") { windows_only() } else { print("linux") }` | `linux` | refused, `callable:windows_only` |
| `if (false) { nope() }` / `print("success")` | `success` | refused, `callable:nope` |
| windows branch `r := run("cmd", "/c", "ver")` | `posix` | refused, `callable:run` |
| windows branch `ffi_open("kernel32.dll")` | `posix` | refused, `callable:ffi_open` |
| windows branch `x := "a" - 1` | `posix` | refused, `type:-` |
| windows branch `print(sha256("a", "b"))` | `posix` | refused, `builtin-arity:sha256` |
| windows branch `s.win_only()` | `posix` | refused, `method:win_only` |
| windows branch `f := (...xs) => xs.size()` | `posix` | refused, `lambda-variadic` |
| macOS branch `secure_random_bytes(4)` | `other` | refused, `builtin-bytes:secure_random_bytes` |
| `import("./m.f")`, then `if (os() == "windows") { print(pwd()) }` | `linux` | refused, `module-host-op:os` |
| `p := os() == "windows" ? getenv("USERPROFILE") : getenv("HOME")` | `true` | refused, `opaque:irregular-legacy-parse` (the ternary spelling) |
| the same with `if`/`else` and assignment | `true` | runs |
| `platform()` if/else-if chain; `arch() != ""` | `l`; `true` | run |

11 of the 15 patterns are refused on Linux although the code that would fail is never reached.

## 4. Impact

- **Corpus (inventory corpora, fresh C117 collect):** 318 units are refused (first non-consequential reason). Legacy
  completes 136 of them: 98 refused with a class (i) reason, 18 E (F12), 17 deferred-named, 3 approved; it fails in
  the other 181. **Every unit refused with a class (ii) reason (75) also fails in legacy**, so no corpus program today
  is refused only because of an X error in dead code. How many of the 98 class (i) units fail only because the
  construct sits in code they never run is not measured (it needs coverage); it is an upper bound.
- **Platform-conditional code in the corpus:** 1 refused unit calls `os()`, `platform()` or `arch()` (an integration
  smoke, refused for a package import error). The corpora are Linux-centred, so they understate the impact.
- **Cross-platform impact:** any script that guards platform-specific code by an `os()` check is refused on every
  platform whose branch contains an undefined helper, a process call, FFI, a bytes producer, a host op after an import,
  a variadic, or any other refused construct: the guard cannot help, because admission is whole-program.
- **Most important patterns:** (1) platform dispatch to platform-only helpers or libraries; (2) feature probes and
  fallbacks (`if (exists(...)) { new_api() } else { old_api() }`); (3) debug and error paths that are rarely taken
  (`if (verbose) { print(sha256(x, y)) }` with a mistake); (4) library modules that define many functions, most
  uncalled by a given script; (5) defensive branches with deliberately invalid operations.

## 5. Smallest sound implementation strategy (PROPOSAL, not implemented)

### 5.1 Mechanism: a deferred-error op

The compiler already has the raise-at-point primitive used by the CP25 corrections: `UnitCompiler::legacy_fatal`
(compiler_rules.cpp:273) emits `OpCode::Undefined` with a message note at the construct's offset, then a dead
`LoadNull`. Used today for user-function argument-count mismatches (compiler_call.cpp:97), const writes
(compiler_const.cpp:55) and `struct has no method` (compiler_structs.cpp:208).

**Proposal:** a single helper `UnitCompiler::deferred_error(at, message, dst, category)` beside `legacy_fatal`, and,
at each **construct-local** X or L-defer refusal site, replace `return fail(node, "<reason>")` with the deferred op
when the refusal is not S (and not L-lang):

- X: message and position exactly legacy's when reached (the rows' C114/C115 probes record both); catchability as
  legacy's (an `Error` value a `try` can catch, when legacy's is catchable).
- L-defer, and class (i) constructs that are call-local (missing built-ins, deferred-named callables, lazy-argument
  forms, legacy-accepted arities): an ordinary error "`<construct>` is not supported by the Runtime 2.0 VM" at the
  construct, exit 1 when uncaught. Never a legacy fallback (D-013).
- Function granularity for declaration-level refusals the resolver raises today (`fn-param` variadics,
  `lambda-variadic`): compile the function or lambda as a stub whose body is the deferred op, so creating or declaring
  it is free and only a call raises.

### 5.2 Files and size (estimate)

`compiler_rules.cpp`/`compiler_expr.h` (the helper and its kind rule), `compiler_call.cpp`, `compiler_keyed.cpp`,
`compiler_dispatch.cpp`, `compiler_native_args.cpp` (arity, receiver, lazy-argument and missing built-in sites),
`compiler_thread.cpp` (the unbound-callee fallback), `compiler_expr.cpp` (`type:<op>`), `compiler_structs.cpp`
(private, no-field, struct arity), `compiler_assign.cpp` (undefined assignment, member assignment),
`compiler_interp.cpp`, `compiler_stmt_expr.cpp`, `resolver.cpp` (stubs instead of plan-time `reject` for the
function-level rows), `kinds*.cpp` (bottom kind for deferred results), and `refusal_inventory.cpp` (classify-all
records each deferred site as `deferred:<reason>` so the inventory keeps seeing it). About 500-800 lines of src, about
1000 lines of tests and mutants.

### 5.3 Soundness argument

1. **Same behaviour when reached.** An X row is, by its C114/C115 evidence, an error legacy raises at that construct
   whenever the construct executes. The op raises the same message at the same position, after the same earlier
   evaluations. Where legacy evaluates some arguments before the check (W1/Q10: "legacy checks arity before or between
   argument evaluations"), the op is placed after exactly those evaluations; where that order is not known for a
   site, the site keeps the refusal (it stays outside the bounded plan). Same exit, same earlier output: the 00:29
   "B" difference disappears for these rows.
2. **Same behaviour when not reached.** The op has no effect unless executed: no allocation, no store, no host call,
   no trace line. Unreached, the program runs exactly as legacy does on the reachable path, as the probes require.
3. **Inference is not poisoned.** The op's result has the empty kind set (bottom): `kinds(id)` of a deferred construct
   contributes nothing to any binding, parameter or return kind, so an unreachable `x = bad()` cannot widen `x` for the
   reachable code. Constructs whose refusal is itself a kind conflict produced by inference (binding-type errors,
   futures, element-assignment unions) are excluded from the bounded plan, because deferring them would need
   flow-sensitive inference (5.4). B4b's admission proofs stay valid: each proof is over the reachable constructs'
   kinds, and a bottom kind never weakens a proof.
4. **B5 references, ownership, cycles and atomicity.** The op performs no store, so the H-A store contract, the
   mutation contract, cycle refusals and atomic commit are unaffected: an op inside a store's value expression raises
   before the commit, as legacy's error does; no partial commit exists to roll back.
5. **D-013, one engine per unit.** A unit with deferred ops is still compiled and run entirely by the VM; reaching an
   op is an ordinary VM error, never a hand-over to legacy.
6. **Allocation safety.** The op allocates only the error value on raise; the stubbed functions allocate nothing until
   called.
7. **Inventory visibility.** Deferred sites stay visible (`deferred:<reason>` in classify-all and the statistics), so
   G1 and the rows keep tracking them; S and L-lang refusals stay refusals.

### 5.4 Not in the bounded plan (each needs a separate owner decision)

- **Effect or reachability analysis:** the host-import guard (R1 order over every proto; slot 2 stays conservative),
  coroutine admission scans (`coro-*` over protos), handle-flow analysis (`host-handle-flow`), capacity limits.
- **Flow-sensitive inference:** binding-type conflicts (`context:assignment-*`, `for-missing-write`,
  `type:for-source(-union)`, `type:element-assignment`), futures in expressions, kind-limit convergence.
- **Broad lazy compilation or parsing:** opaque source (`opaque:irregular-legacy-parse`, `opaque:template-text`, nesting),
  the struct declaration model (plan-time schemas; CP2's own work), module-graph properties (collisions, name clashes,
  dynamic imports beyond the L-defer raise).

### 5.5 Tests, mutants, what must not change, risks

- **Tests:** a dead-code matrix per family (`if (false)`, an untaken `else`, an uncalled function, an uncalled lambda,
  an `os()`-guarded branch): VM output = legacy output; the reached variants: message, position, exit and earlier
  output = legacy's, and `try`/`catch` parity; the owner's platform example; an inference non-poisoning test (the same
  unit with and without the dead branch compiles to the same kinds for reachable code); classify-all still reports
  every deferred site.
- **Mutants:** the op raises at compile time (caught by the dead-code matrix); the op does nothing (caught by the reached
  variants); the op contributes its operand kinds (caught by the non-poisoning test); a wrong position (caught by the
  position checks); a flipped catchability (caught by `try`/`catch` parity); the inventory loses deferred sites (caught
  by a classify-all check).
- **Must not change:** S and L-lang refusals; every refusal listed in 5.4; the 00:29 policy for static errors; D-013;
  B4b/B5 contracts; R1; no re-baselining.
- **Risks:** legacy's evaluation order before the error (W1/Q10) per site; legacy's quirk messages (about 33 rows, e.g.
  `deepcopy(1, 1)` "malformed expression: 1, 1") must be reproduced exactly; catchability classes; loop-context paths
  where legacy's message differs (prepared loops); sessions (a session unit's dead code follows the same rule but
  session contracts are separate).

**Coverage of the bounded plan:** about 284 of the 299 X rows (all family members, type:<op>, user arity, undefined
names, struct use errors, most of "other"), the 10 L-defer rows, and the call-local class (i) families (missing
built-ins and deferred-named callables, lazy arguments, legacy-accepted arities, `sort`/`sort_by`, `?.`/`?[]`,
statement forms, variadic stubs) for their unreachable occurrences. It directly fixes the owner's example and most of
section 3's platform patterns; `module-host-op` and the ternary spelling stay refused.

## 6. Place in the owner's sequence

- **Slot 1 (infrastructure):** no scope change required. The deferred-error helper is compiler-local; if slot 1 already
  touches the error and diagnostic infrastructure (positions, messages, catchability), the helper and its kind rule
  belong there, and the per-site conversions follow as their own bounded checkpoint.
- **Slot 2 (host-import guard):** no change. The guard already treats uncalled functions conservatively (any host op in
  any proto). Making it reachability-aware is the 5.4 effect-analysis item and needs its own decision.
- **CP2:** no scope change. Group E (`struct-private`, `struct-no-field`) becomes X deferred ops instead of
  raise-before-execution; the declaration model stays CP2's work.
- **Recommended:** a separate bounded checkpoint "dead-code admission" after slot 2 (or folded into slot 1's
  diagnostics part), gated on the reviewer's acceptance of 5.3.

## 7. Owner items

1. Confirm the S list (10 rows), in particular `jump-outside-loop` (no contract text calls it a compile-time error).
2. Approve the bounded deferred-error plan (5.1-5.3) for X, L-defer and the call-local class (i) constructs, after the
   reviewer's soundness review.
3. Decide separately on each 5.4 item (effect analysis, flow-sensitive inference, lazy parsing), or defer them.
4. Choose the slot: inside slot 1's diagnostics work, or its own checkpoint after slot 2.
