# Minify++ — Release Notes

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

## v1.0.5

- Added a dedicated cross-format adversarial gate covering idempotence and malformed-input behaviour across HTML, CSS, JavaScript, JSX, JSON, XML and SVG.
- Expanded the generated non-JavaScript idempotence corpus from 39 to 111 documents using deterministic HTML/CSS/XML/SVG cross-products, while retaining JSON structural semantic checks.
- Kept XML/SVG validation claims deliberately conservative: Minify++ protects syntax it understands and remains a minifier rather than pretending to be a complete validating XML parser.
- Existing JavaScript/JSX gates remain green at 15,459 executable JavaScript semantic programs and 180 JSX/TSX syntax/idempotence programs in this checkpoint.

## v1.0.4

- Introduced the first cross-format adversarial test layer and tightened the separation between malformed-input rejection promised by validating modes and conservative handling in XML/SVG.
