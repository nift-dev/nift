# CP06 — Template scanner dispatch

Status: see `../progress.md`.

## Objective
Cut the per-character cost of the template scanners without changing a byte of
output, diagnostics or exit codes.

## Where the time went (callgrind, 300-page site, runtime-2 binary)
`std::string::compare(pos, n, const char*)` + `memcmp` + `strlen` were ~45% of
all instructions in a full build. Profiling by caller showed the dominant
sources were not the `Parser::parse` keyword chain itself but two helpers it
calls on every included/nested output:

| Function | Inclusive share | Cause |
| --- | ---: | --- |
| `nift::detail::append_indented` | ~44% | four keyword `compare` calls (`<!--`, `-->`, `<pre`, `</pre>`) at every byte of every included fragment |
| `Parser::find_balanced` | ~17% | three keyword `compare` calls (`@/*`, `<!--`, `@//`) at every byte of every block it balances |
| `Parser::parse` keyword chain | ~3% | 31 `compare` probe sites (27 distinct literals) per scanner step |

## Implementation
- `append_indented` (`src/ParserHelpers.cpp`): dispatch on the current byte;
  only `<`, `-` and `\n` can change state, every other byte is skipped without
  a comparison. The comment/pre probes are the same tests as before, reached
  only when their first character matches, with constant lengths.
- `Parser::find_balanced` (`src/Parser.cpp`): each keyword probe is guarded by
  its first character (`@` or `<`).
- `Parser::parse` (`src/ParserTemplate.cpp`): the 31 `source.compare(i, N,
  "lit") == 0` probes became `source_starts_at(source, i, "lit")`, a template
  helper with a compile-time literal length that rejects on the first
  character before any `memcmp`. Equivalence: for `i < size`, both are true
  exactly when the `N` bytes at `i` equal the literal. The helper takes the
  length from the literal's array type; no runtime assertion exists, so this
  relies on every literal being a plain string without embedded NULs (true for
  all 27; the review checked each original `N` against its literal).
- Scanner step boundaries are unchanged: the existing run-copy at the end of
  the loop (`find_first_of("\\@<$")`) still decides where the next step
  begins, so keyword detection that depends on step starts (`break`,
  `continue`, `return`, bare `import`) behaves exactly as before.

The roadmap suggested `memchr` scanning in `Parser::parse`; it was not needed
for the gate, and changing step boundaries there would risk the
step-start-dependent behaviours above, so it was not done.

## Compatibility
No observable change intended. Verified byte-identical against the runtime-2
binary (see Tests).

## Tests
- `tests/runtime2/scanner_parity.sh` (new; `make test-runtime2-scanner-parity
  NIFT_REF=<reference nift>`; skipped when `NIFT_REF` is unset): builds every
  tracked project site in the repository, 60 generated template-structure
  cases (`<pre>`/`<code>`, HTML and Nift comments, near-miss tokens such as
  `x-->y` and `</pr`, indented `@input`, `@if`/`@for` blocks; 51 build, 9 fail
  with diagnostics, and both outcomes are compared), and an optional extra
  site (`NIFT_SITE`); runs the 121-script probe corpus. Compares output trees,
  stdout, stderr (timings normalised) and exit codes.
- Sensitivity check: with the `-->` handling in `append_indented` disabled the
  test reports 44 failing sites; with the real change it passes.
- Full `make -k test`: see Evidence.

## Performance
See `../evidence/cp06/README.md`. Gate: full build ≥ 30% fewer instructions;
no-op build not worse than +1%.

## Residual risk
- Scanner parity relies on the corpora above plus the existing suite; it is
  not an exhaustive proof. The transformations are local and each keeps the
  original predicate, which limits the risk.
- `make test` skips the parity target unless `NIFT_REF` is provided.

## Review (same-model adversarial pass; not independent)
No output difference found: ~37,000 end-to-end cases (random sites and
scripts, keyword edge matrix in 19 positions, token pairs), 1,500 against an
ASan/UBSan build, and ~50M isolated-function comparisons of old vs new
`append_indented`/`find_balanced`/probes under sanitizers; a mutant was caught,
showing the fuzzers can fail. Findings, all in the test or this record:

| # | Finding | Resolution |
| --- | --- | --- |
| 1 | Parity test skipped silently (`NIFT_REF` unset/misspelt) | Unset: explicit SKIP and the target is not in plain `make test` (needs a reference binary; run it as a gate with `NIFT_REF=`); set but not executable: fails |
| 2 | Script stderr never compared; stdout compared with digits stripped | stdout, stderr and exit code always compared; digits masked only for scripts whose reference output is itself nondeterministic (reference run twice). The review's fake reference is now caught |
| 3 | No break/continue, `@script`, CRLF, UTF-8 cases; no minimum-count check; record inaccuracies | Added to the generator; fails below 70 sites / 100 scripts; record corrected |

