# Target architecture (one page)

```
 scripts (.f, -e, eval, C ABI) ─┐
 shell command lines ───────────┼─▶ front ends ─▶ one AST (+spans) ─▶ resolver/compiler ─▶ Proto (bytecode)
 templates (@ directives, $[]) ─┘                                                          cached by content hash
                                                                                                │
                                         Register VM (heap frame stack, inline caches, resumable frames)
                                                                                                │
                 shared services: 16-byte values + COW aggregates │ typed handles │ Host ops │ task scheduler
                                                                                                │
                             consumers: build engine (page = template call) │ shell/REPL │ C ABI + bindings
```

## Fixed decisions (see `decisions.md` for rationale)

1. Compilation unit is a whole function or template. A unit either runs on the
   VM entirely or on the legacy engine entirely. No per-expression fallback.
2. Protos are immutable and cached by (path, content hash).
3. Every filesystem, environment, clock and process effect is a Host op. Host
   ops record build dependencies (including absence, directory listings and
   environment values) in one place.
4. Nift→Nift calls never recurse on the C++ stack. Depth is a frame budget.
   Native re-entry (callbacks from built-ins, FFI) is counted separately.
5. No string-encoded types. Handles, closures and futures are value tags;
   built-ins are bound to integer ids at compile time.
6. Values: 16-byte tagged (null, bool, int64, double, decimal, string, bytes,
   array, object, function, struct, handle, error). Arrays/objects are
   refcounted copy-on-write. Strings immutable, refcounted, atoms for keys.
7. Isolates own their heaps (non-atomic refcounts). Values cross isolates by
   copy or freeze. Protos are shared.
8. Templates compile to functions that append to an output buffer.
9. The website build engine (`ProjectInfo`, scheduler, `.nift` metadata) is
   kept; only the evaluator underneath it is replaced.

## Module layout (new code)

New code lives under `src/rt/` with small files and explicit interfaces:

| Area | Files (planned) |
| --- | --- |
| Values | `rt/value.h`, `rt/string.*`, `rt/array.*`, `rt/object.*`, `rt/handle.*` |
| Front end | `rt/lexer.*`, `rt/parser.*`, `rt/ast.*`, `rt/template_lexer.*`, `rt/shell_parser.*` |
| Compiler | `rt/resolver.*`, `rt/compiler.*`, `rt/opcodes.h`, `rt/proto.*` |
| VM | `rt/vm.*`, `rt/frame.*`, `rt/native.*` (native function table) |
| Host | `rt/host.*` (filesystem, env, process, clock; dependency recording) |
| Tasks | `rt/scheduler.*`, `rt/channel.*` |

Maintainability rules for `src/rt/`: no function over 150 lines, no line over
120 columns, no string comparison on hot paths, one concern per file. These are
enforced by a lint step added in CP09.
