# Nift v4.13.0 — Runtime 2.0

Runtime 2.0 is the headline feature of Nift v4.13.0. The new engine is the
default for the documented scripting entry points. Nift v4.13.0 is the
product version. Runtime 2.0 is the scripting engine generation.

## Script compatibility

The VM validates scripts before execution and reports unsupported constructs
with source locations. Use `NIFT_VM=0` for scripts that require documented
legacy behavior. The affected host names use the `native::` qualifier in
Runtime 2.0. The approved `ifstream(...).read_val()` refusal remains in effect.

In-memory `minify(source, format)` and plain top-level mid-file shebang comments work under
Runtime 2.0. Use `NIFT_VM=0` for supported legacy scripts that require bare
shell-command statements, stored or indirectly invoked async function values,
the file forms of `minify()`, nested `#!` comments, or the deferred
contextual callable spellings. Direct scalar literals support
`.to_string()`, and the VM preserves the tested escaped and embedded
quote forms in single-member object literals.

Runtime 2.0 supports `bytes()` and the tested literal byte-array writes to
managed files. It also supports the admitted scalar `write_val()` forms and
the managed buffer methods `append`, `prepend`, `insert`, `insert_before`,
`insert_after`, `replace`, and `replace_once`. The historical managed-file
`read_val()` token-evaluation case remains available through explicit legacy
execution.

The local Checkpoint 10 file-buffer oracle recorded 37 executed VM matches
and 65 approved exact pre-execution refusals. All 102 legacy witnesses matched
their historical oracle. In particular, the five documented nested string
`replace()` inputs retain their existing refusals. Evaluate a supported
replacement separately before passing it to `replace()`, or use `NIFT_VM=0`
where legacy conditional argument evaluation is needed.

Across the Checkpoint 10 callback and object-member suites, all 109
historical witnesses matched in explicit legacy mode. Runtime 2.0 matched
103 pinned execution results and exercised six approved pre-execution
refusals. These include the exact nested `cmd` declaration and computed
object insertion witnesses. Use `NIFT_VM=0` for those historical forms.

Compact grouped string receivers support `split()` and `stringify()`. A
one-argument `copy(value)` evaluates its operand once in the VM.

Sorted collections preserve ordering for homogeneous numeric or string keys
and members. Legacy accepts mixtures of these domains without promising a
portable order. For v4.13.0, an insertion that mixes the domains fails with
`sorted_map: incomparable keys` or `sorted_set: incomparable values`, in
either insertion order. The reached operation fails before changing the
collection, including through an alias. This is a runtime diagnostic. Earlier
statements and ordinary argument evaluation retain their effects. The two
approved mutable-key refusals still apply before script execution.
Boolean-only sorted collections retain insertion order. Other combinations
are outside this compatibility approval.

The separate external Nift regression suite recorded 57/94 passes with the VM
default and 94/94 with explicit legacy execution.

## Parallel tasks and platform support

Parallel-task admission uses the root heap budget, with separate accounting
for each worker isolate. This release includes task ownership and publication
corrections, and adds FreeBSD 14 amd64 to the required platform checks
alongside Linux, macOS, and Windows.

The website build comparison uses Nift v4.12.0 as the historical baseline.
The tested workload produced byte-for-byte identical output.
