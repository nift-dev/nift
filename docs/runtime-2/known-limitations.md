# Runtime 2.0: known limitations

This page lists what the initial Runtime 2.0 release does not run on the VM, and where the VM's
behaviour differs from the legacy interpreter on purpose. Sources: owner rulings recorded in
[decisions.md](decisions.md), and the approved semantic differences in
[compatibility.md](compatibility.md), which is authoritative for them.

## How a limitation behaves

The VM is the default scripting engine. A program that uses a construct the VM does not run is
**refused before any part of it runs**:

- exit status 1 and no output;
- `not compiled for the VM (<reason>)`;
- `note: the program was not executed: no part of it ran`;
- the command that runs it on the legacy interpreter, `NIFT_VM=0 nift …`.

Such a program is never run on legacy automatically (no silent fallback).

The legacy interpreter remains available: with `NIFT_VM=0`, these constructs run as they did in
Nift 4.x. Website builds keep the existing template renderer (D-053).

## Named deferrals (owner, 2026-11-10 13:26; D-065)

The VM does not yet run the constructs below. Nothing is removed: each one keeps working on
legacy. `make test-runtime2-named-deferrals` checks that each is refused cleanly and
deterministically and that legacy still runs it. That gate is Linux-only (its FFI case opens `libc.so.6`); on other
platforms it prints a SKIP line.

| Construct | VM refusal reason | Still works with |
| --- | --- | --- |
| A delayed import inside a function: `fn(f()) { import("./m.f"); … }` | `import-nested` | `NIFT_VM=0`. Top-level, relative, chained and package imports run on the VM. |
| Process execution: `run()`, `cmd()` and their handle methods | `callable:run`, `callable:cmd`, `method:run`, `method:pipe`, … | `NIFT_VM=0` (D-029, D-030, D-034) |
| FFI: `ffi_open` and FFI calls | `callable:ffi_open` | `NIFT_VM=0`, and the non-VM paths (docs/ffi.md, D-036) |
| Timers: `timer()` and its handle methods | `callable:timer`, `method:start`, `method:stop`, `method:elapsed`, … | `NIFT_VM=0` |
| `project_root()` | `callable:project_root` | `NIFT_VM=0`, and project builds (the template compiler, CP24, is deferred: D-053) |

## Approved refusals (release behaviour)

This construct is refused on the VM by an owner decision. It is not pending work.

| Construct | Reason | Decision |
| --- | --- | --- |
| A map, set or stack as a map key or set member, lookups included | `collection-mutable-key` | D-NEXT-refsem #6 (keys, D6) |

## Refusals awaiting an owner ruling

These are refused on the VM today, but no owner decision approves the refusal as release
behaviour. Their inventory status is `PENDING-OWNER`, and the gates that meet them fail until the
owner rules. (Review of C83–C99, P1-2.)

| Construct | Reason | Why it is not approved |
| --- | --- | --- |
| A function named like a built-in | `fn-name-reserved` | D-009 #3 decides the opposite: user functions take precedence over built-ins. The refusal defers D-009 #3 and does not implement it. It is not the owner's 13:26 "reserved-name check" either; that item is `binding-name-reserved` (status `implement`). |
| A name not declared in its scope (a dynamic capture dependency) | `free-name` | No owner decision text approves it; its inventory row says "release approval pending". |
| Plain `@fn(...)`, and `@fn` flags the VM does not support, in a script | `fn-flags` | D-047 #1 and D-048 #1 approve rejecting plain `@fn` in **both** engines. Legacy keeps accepting it, so the VM-only refusal is not what was approved. |

**Unapproved behavioural differences.** Some programs run on the VM but give another result than
legacy, with no approval. They are listed, each with its gate, case, program and both outputs, in
`tests/runtime2/cp25/pending_differences.tsv`, all PENDING-OWNER. The list approves nothing: each
named gate still fails on its case until the owner rules. Today there are two:
- `copy-wrong-arity`: the VM prints `1`, legacy `1` then `2`, because legacy's loop path evaluates
  the argument twice;
- `named-precedence`: the VM prints `int`, legacy `function`.

**Plain `@fn` in a script: an explicit legacy/VM difference.**
- D-047 and D-048 make plain `@fn(...)` invalid in script context; the canonical form is
  `fn(...)`.
- The VM refuses it (`fn-flags`).
- The legacy interpreter still accepts it: the owner declined the legacy-side rejection on
  2026-11-10. So `@fn(v()) { return 3 }` then `print(v())` prints `3` with `NIFT_VM=0`, and is
  refused under the VM default.
- Repository fixtures use `fn(...)` (D-048 #4).

## Approved semantic differences

Where the VM runs a program but its result differs from legacy on purpose, the difference is
approved and listed in [compatibility.md](compatibility.md). This list does not repeat them. The
main ones:

- arrays and objects are shared references (D-NEXT-refsem-b, B5);
- reference cycles are refused (D-NEXT-refsem #4);
- `const` is binding-only (owner Q2 option B);
- a failed comparator sort leaves the receiver unchanged (Q7);
- splice evaluates its arguments first;
- receiver and operand order;
- an import-scope collision is reported before the program runs (B4 C1);
- a function value has no shown form in `nift eval` (b4-dispatch.md 6).
- the assignment family (checkpoint 1): a compound element update `a[i] += f()` reads and writes
  the element at the key read before the right-hand side (D2, owner 2026-11-10 16:35), where legacy
  reads at the old key and writes at the new one; a path store or update reads its receiver first
  for every callee (H-A D1.1, D2); its aliases, cycles, `const` and loop wording follow B5, Q2 B and
  D-018c.

**Assignment family limits (checkpoint 1, refused before any effect).** A key that is not a plain
name, an unsigned integer literal or a simple string literal (`a[i + 1][0] = v`, `o["a.b"] = v`),
`o.k = v`, `a[0]++`, a fourth level, and a compound update used as a value stay refused. Each level
must be proved an array with a number key or an object with a string key (`type:element-assignment`
otherwise): the kind inference merges a plain store's value (`o["k"] = 5`) into every member of the
object it names, so a later path through another member of that object (`o["m"]["x"] = 1`) is
refused; mixed operand kinds of a compound update are refused (`type:compound-assignment`). A future
stored through a path stays refused (`coro-future-flow:Assign`).

## Other refusals

Every other refusal reason the release gates see is listed, with its status, in the CP25-R
inventory (`tests/runtime2/cp25/classification.tsv`; `make cp25r-inventory`). Its `status`
column is one of:

- `approved`: an owner ruling, cited;
- `deferred-named`: the deferrals above;
- `implement`: approved for implementation before release;
- `PENDING-OWNER`: not yet adjudicated, so a release blocker;
- `retired`: a reason that is resolved or no longer emitted (it counts as none of the above).

An inventory row never implies approval.
