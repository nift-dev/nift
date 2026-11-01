# Compatibility contract

Must not change unless a decision in `decisions.md` approves it.

| Surface | Rule |
| --- | --- |
| C ABI 1.3 (`include/nift/c_abi.h`) | Symbols and signatures unchanged; additions bump the minor version |
| `nift::Value` / `nift::Engine` C++ API | Source compatible |
| CLI commands and flags | Unchanged; new flags allowed |
| `.nift/` project files and page-info | Readable by new code; new fields optional and versioned; old metadata triggers at most one rebuild |
| Template syntax and output bytes | Byte-identical for unchanged inputs |
| Script syntax and observable results | As `semantics.md`; changes only where a row says **Change** |
| Diagnostics | Message text, codes and spans identical unless a checkpoint records an improvement |
| `nift eval --json` / `--capabilities` | Contract version 1 kept |
| Bindings (Python, Node, Go, C#) | Unchanged; they sit on the C ABI |

## Approved breaks

| Break | Decision | Checkpoint |
| --- | --- | --- |
| Project-root confinement removed from general filesystem operations | D-002 | CP03 |
| String-encoded handles stop working | Design report; D-001 | CP17 |
| Reconstructed handle tags no longer resolve after release | Design report; D-001 | CP17 |
| Recursion limit raised from 64 | Design report; D-001 | CP11–CP12 |
| Candidates for deliberate language changes (break, argument evaluation, value-based typing, declarations in loops, aliasing, catchable errors) | D-015 table (proposed, not yet scheduled) | — |
| User functions shadow built-ins of the same name | D-009 #3, D-014 | to be scheduled |
| `exit` in `~/.niftrc` / `nift -i file` on a terminal is refused (the file stops there, the session continues at the prompt) instead of ending the session with status 1; migration: drop the line | D-061 | VM default |
| A write to the script invocation bindings (`args = …`, `cmd = …`, `args[i] = …`, compound/increment) is rejected before execution (`invocation-assign:<name>`, nothing of the program runs) instead of failing fatally at the statement after earlier statements ran; migration: declare a new binding (`my_args := args`) or replace it with a top-level `args := …` before any use | D-062 #2 | weekend args/cmd |
| An import-resolution error (package not installed or invalid, lock or revision mismatch, lock acquisition failure, missing or unreadable module, import cycle, package-relative escape) rejects the whole program before it runs, with legacy's text and position and exit 1; legacy runs the statements before the import first. An import a top-level `return` leaves unreached is rejected too (legacy exit 0); the diagnostic says so | D-NEXT-packages #1 | weekend packages |
| A VM package lookup creates `.nift/.packages-transaction.lock` only inside an existing `.nift/` (never truncating it) and never creates `.nift/` itself; legacy creates both. One read lock covers all package imports of the program | D-NEXT-packages #2 | weekend packages |
| A module's `return <value>` outside functions, `export(x)` naming no binding, a duplicate export and `export(<not a name>)` reject the program before it runs (legacy fails when the statement executes); a `return <value>` in a branch legacy never takes is rejected too | D-NEXT-packages #5 | weekend packages |
| A module's top-level code (or a function it may call while initialising) reading the entry script's `args`/`cmd` is refused before running (`import-scope:invocation-init`; legacy fails when it executes, and runs it when the code is never executed) | D-NEXT-packages #4 | weekend packages |
| Of several program errors, the one legacy hits first is reported, with one accepted gap: a compile-time program error inside the failing module before its failing statement, or one reached only through a call, is not detected, so the later import or module error is reported (both are real errors; only which one is named differs) | D-NEXT-packages | weekend packages |

## Syntax compatibility (D-045, owner, 2026-10-20)

Runtime 2.0 must accept exactly Nift's established syntax, as implemented by
legacy Nift and documented at https://nift.dev, and must refuse (before
execution) anything it does not support rather than reinterpret it. Examples:
template functions `@fn(name(args)) { … }`, standalone `.f` functions
`fn(name(args)) { … }`, single-brace bodies in Nift statement syntax, optional
semicolons. Every checkpoint that adds or extends syntax checks the
documentation, legacy and the Runtime 2.0 parser, keeps documentation-derived
executable fixtures run against both engines, and records any
documentation/legacy disagreement for an owner decision before changing the
language. Syntax compatibility is part of the certification gates of every such
checkpoint. Owner-approved language changes (D-036–D-044) are listed as such and
never confused with compatibility regressions.
