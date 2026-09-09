# Minify++ — Release Notes

## v1.1.2 (development)

- Post-release development version after the public **v1.1.1** release. The
  executable identity is advanced to `1.1.2` for further development; the public
  API format version remains `1`.
- Accept browser-recoverable stray quotes in unquoted HTML attribute values
  instead of misclassifying them as unterminated quoted attributes. This defect
  was found by the independent WPT HTML conformance harness and is retained in
  the standalone smoke suite.
- Preserve HTML self-closing and following-attribute boundaries after unquoted
  values, raw/preformatted content (`iframe`, `xmp`, `listing`, `plaintext` and
  inline preserved-whitespace styles), foreign SVG/MathML subtrees, recoverable
  `<<script>` openers, and ordinary comments recovered at EOF. These families
  were reduced from the complete independent 9,651-case WPT HTML run.
- Preserve nested template-literal source text, Unicode U+2028/U+2029 line
  terminators carried by removed JavaScript block comments, and the required
  boundary between a regular-expression literal and a following word token.
  These fixes reduce seven genuine failures found by the complete selected
  Test262 run; all 39,741 cases runnable under the pinned Node runtime then
  preserve behavior after minification.
- Preserve ASI-significant line boundaries before and after JSX roots, never
  reinterpret closing tags as fresh roots, and recognize standalone JSX after
  line-comment boundaries. The pinned TypeScript JSX corpus exposed these
  scanner defects; the corrected complete run preserves all 221 eligible
  JSX/TSX programs.
- Added independent complete-corpus checkpoints for the remaining claimed
  formats: 93/93 eligible JSONTestSuite documents, 535/535 conservative W3C XML
  documents and 1,176/1,176 strict standalone WPT SVG documents preserve their
  format-specific semantic projections.

## v1.1.1

- Added checksum-verifying curl install, download, update and uninstall scripts,
  cross-platform release archives, release rehearsals and public installer smoke
  tests.

- Preserved whitespace runs that follow CSS escapes. A hex escape consumes one
  trailing whitespace as its terminator, and an escaped whitespace character is
  an identifier character, so collapsing the following whitespace run merged
  what browsers tokenize as separate identifiers (for example `@counter-style`
  symbol lists such as `\2020  \2021`) into a single identifier. These cases are
  outside the structured-CSSOM conformance oracle's view, so they are covered by
  focused permanent regressions in the smoke suite as well as by the
  adversarial WPT re-verification.

- Added Web Platform Test-driven CSS recovery coverage. CSS EOF comments and
  strings now follow browser-style recovery semantics instead of becoming
  Minify++ errors, and bad strings stop at unescaped newlines so subsequent CSS
  remains visible to the scanner.

- Reopened the production-readiness audit after a real Minify++ website build
  exposed unsafe CSS whitespace removal. CSS token-boundary handling now protects
  leading-decimal value lists, descendant selectors, adjacent quoted values,
  function/value lists, media conditions, and percentage/component boundaries.
- Added an independent PostCSS semantic-tree differential gate for representative
  modern CSS alongside expanded focused and generated idempotence regressions.
- Kept Node-based CSS/JSON oracles process-supervision independent: the shell
  invokes native drivers and Node parses completed files, allowing clean-package
  validation in restricted desktop and CI wrappers without skipping evidence.
- Revalidated the scoped production-readiness decision only after the committed
  clean source archive passed the complete suite and the repaired website passed
  fresh-browser layout and content checks.
- Prevented JavaScript minification from manufacturing `/*` or `*/` across
  authored whitespace.
- Prevented the JSX root finder from interpreting JSX-like text inside JavaScript
  comments as live markup, and made escaped JSX attribute quotes explicit.
- HTML, XML, SVG, and JSX now reject affected unterminated quoted constructs
  instead of accepting partial lexical output. CSS follows CSS Syntax EOF recovery:
  unterminated strings and comments are recovered rather than rejected, while an
  unescaped newline terminates a bad string without swallowing following tokens.
- Raised the adversarial validation campaign to 7,000,000 deterministic mutations
  across the seven formats under both ordinary and ASan/UBSan builds; all
  successful first-pass outputs remained accepted on the second pass.

## v1.1.0

- Renamed the project to **Minify++**.
- The standalone executable is now **`minify`**.
- Renamed the public C++ API from the old project namespace/header identity to `namespace minify` and `<minify/Minify.h>`.
- Renamed the implementation source to `src/Minify.cpp`; the Makefile now produces `minify`.
- Minification semantics and format version remain unchanged from the v1.0.5 hardening checkpoint.
- Hardened CLI file handling: read failures are distinct from valid empty input,
  output is prepared before destination replacement, existing permission bits are
  preserved, and symbolic-link destinations are rejected explicitly.
- Added a machine-checkable 18-file standalone/Nift synchronization gate.
- Added repeatable deterministic fuzz-smoke, ASan/UBSan, and per-format
  throughput/output-size/RSS benchmark targets.
- Prevented whitespace removal from manufacturing CSS comment delimiters, JSX
  openers, or HTML/XML comment/CDATA-like syntax. These boundary families were
  discovered by the new 70,000-case mutation gate and retained as focused tests.
- Added a clean committed source-package `distcheck` and completed the Linux
  production-readiness review with bounded platform/syntax limitations documented.

## v1.0.5

- Added a dedicated cross-format adversarial gate covering idempotence and malformed-input behaviour across HTML, CSS, JavaScript, JSX, JSON, XML and SVG.
- Expanded the generated non-JavaScript idempotence corpus from 39 to 111 documents using deterministic HTML/CSS/XML/SVG cross-products, while retaining JSON structural semantic checks.
- Kept XML/SVG validation claims deliberately conservative: Minify++ protects syntax it understands and remains a minifier rather than pretending to be a complete validating XML parser.
- Existing JavaScript/JSX gates remain green at 15,459 executable JavaScript semantic programs and 180 JSX/TSX syntax/idempotence programs in this checkpoint.

## v1.0.4

- Introduced the first cross-format adversarial test layer and tightened the separation between malformed-input rejection promised by validating modes and conservative handling in XML/SVG.
