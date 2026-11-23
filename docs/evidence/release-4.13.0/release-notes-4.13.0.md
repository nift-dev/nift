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
or the file forms of `minify()`.

## Parallel tasks and platform support

Parallel-task admission uses the root heap budget, with separate accounting
for each worker isolate. This release includes task ownership and publication
corrections, and adds FreeBSD 14 amd64 to the required platform checks
alongside Linux, macOS, and Windows.

The website build comparison uses Nift v4.12.0 as the historical baseline.
The tested workload produced byte-for-byte identical output.
