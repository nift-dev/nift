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
| `map()`, `set()` and `stack()` are values, not references: `b := a`, arguments, returns, closure reads, container insertion/extraction, struct fields and thread transfer give independent (deep) copies; self-insertion stores a snapshot instead of failing with "would create a cyclic reference"; `same()` on a collection is an error; threads accept collections. Migration: return or re-assign a collection that a function fills (`m = fill(m)`), keep shared state in one binding, drop `copy()`/`deepcopy()` used only to separate copies | D-063 | weekend collections |
| `same()` on ordinary values is an error: arrays bound to names (`a := [1]; same(a, a)`: legacy `true`; two arrays: `false`), structs and collections (legacy `true`/`false` by storage); a plain object name errors in both. Migration: compare with `==`, or keep the identity in a handle or callable | D-040 #4, D-063 | weekend collections |
| `same()` on mutex, atomic, channel, future and thread compares identity (the same resource or task: `true`; independently created: `false`), where legacy errors "same: operands must be identity-bearing values" | D-063 (owner, 2026-11-01) | weekend collections |
| A `for ((k, v) : m)` loop over a map visits the entries captured at loop entry; insertions, removals and value updates made by the body do not change the active snapshot (legacy iterates the live entry vector: shows updated values, shifts after a removal, undefined past the end). A visited entry may already be removed from `m`: removing it again fails with "remove: key not found". Migration: read the map again after the loop; guard removals with `m.contains(k)` | D-063 (owner, 2026-11-01) | weekend collections |

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

## Compatibility limitations (refused before any effect, not changed semantics)

The VM refuses these legacy-valid shapes before running anything (exit 1,
"no part of it ran"); `NIFT_VM=0` keeps legacy's behaviour. They are not
approved breaks and not completed behaviour.

| Shape | Legacy | Reason | Decision |
| --- | --- | --- | --- |
| `same()` with an argument that is not a plain binding name: a string literal (`same("f", "f")`), a call, an index, any expression; in every program | reads a string argument's text as a binding name first (`f := () => 1; same("f", "f")` is `true`; `same("a", "a")` without `a` is "unknown value or malformed expression: a"), and evaluates other argument shapes up to twice | `same:argument-shape` | D-063 (owner, 2026-11-01) |
