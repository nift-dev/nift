# Minify++ production-readiness roadmap

This is a living risk assessment. Review it at every substantial checkpoint;
new corpus failures, syntax evolution, sanitizer findings, performance evidence,
Nift integration, and user needs may reorder, expand, narrow, or remove work.

The staged JavaScript/JSX minification plan is maintained in
`JAVASCRIPT-OPTIMIZATION-ROADMAP.md`. It separates the default conservative,
structured and explicitly aggressive contracts into conformance-gated steps.

## Architecture-reconciliation findings

The August 2026 reconciliation verified a stateless whole-buffer implementation
with seven formats, byte-identical standalone and Nift-embedded trees, substantial
JavaScript/JSX generated evidence, and final-output Nift integration. It also
converted inherited concerns into concrete roadmap work:

- enforce standalone-to-embedded equality with a machine-checkable sync/diff gate;
- make standalone CLI reads report open/read failure rather than treating failure
  as empty input;
- replace direct truncating writes with a safe temporary/commit model, preserving
  appropriate metadata and accounting for cross-platform rename rules;
- rerun the non-JavaScript JSON oracle outside the restricted Codex stream
  environment; the JSX generated corpus has since passed all 180 programs with
  tsc 7.0.2;
- add repeatable ASan/UBSan, fuzz, and performance/RSS targets;
- retain the conservative policy that HTML raw script/style bodies and JavaScript
  template contents are preserved rather than recursively minified.

Reorder or retire these items as implementation evidence changes.

## Current checkpoint status

- **COMPLETE — reopened production-readiness audit:** a production website CSS
  failure invalidated the earlier finality claim. The repaired committed candidate
  passes all standalone gates, a new independent CSS semantic oracle, clean-archive
  `distcheck`, synchronized Nift integration, and final browser validation.
- **COMPLETE — current executable evidence:** all current standalone gates pass,
  including 15,459 executable JavaScript programs, 180 JSX/TSX programs, and the
  115-document non-JavaScript corpus with JSON structural comparison. The latter
  was rerun successfully outside the desktop stream wrapper.
- **COMPLETE — synchronization evidence:** a 20-file standalone/Nift equality
  gate now makes embedded drift machine-checkable.
- **COMPLETE — bounded CLI hardening:** checked reads distinguish failure from
  empty input; sibling-temporary replacement preserves existing permission bits,
  rejects symbolic-link destinations, and has CLI regression coverage.
- **COMPLETE — current native/tooling evidence:** repeatable sanitizer,
  7,000,000-case ordinary and sanitized fuzz campaigns, and
  performance/output-size/RSS targets are green. The enlarged campaign exposed
  JavaScript comment-delimiter and JSX comment/root failures, now retained.
- **COMPLETE — clean package evidence:** the committed source archive builds and
  passes the full suite without developer-local or generated inputs; `distcheck`
  makes the workflow repeatable.
- **COMPLETE — public reconciliation:** the website reflects the 111-document and
  70,000-case evidence plus current CLI transaction behavior.
- **KNOWN LIMITATION — platform evidence:** Linux x86-64 with g++ 15.2.0, Node
  22.22.1, and tsc 7.0.2 is directly validated. macOS and Windows remain explicit
  release-evidence gaps rather than inferred support claims.

## Production-ready meaning

A developer should be able to run Minify++ deliberately on documented supported
production assets with strong evidence that meaning is preserved, unsupported or
problematic input fails/preserves predictably, the CLI/API are stable enough to
use, performance is strong, and releases are repeatably validated.

It does not require smallest output on every file or perfect support for every
future syntax.

## Current inherited priorities

1. Reconcile exact per-format supported and malformed-input contracts with source,
   tests, README, and website.
2. Systematically audit HTML whitespace/raw-text contexts, CSS token/custom
   property/modern syntax boundaries, and JavaScript ASI/slash/template/token
   families.
3. Expand real-world HTML/CSS/JS/JSX/XML/SVG corpus coverage and minimize findings.
4. Strengthen runtime/structural semantic oracles where exact output is weak.
5. Run substantial ASan/UBSan validation and introduce sanitizer-backed fuzzing
   proportionately.
6. Establish reproducible startup/throughput/RSS/output-size benchmarks only for
   semantically green checkpoints.
7. Keep standalone and Nift embedded copies synchronized and exercise Nift's
   opt-in integration.
8. Reconcile documentation, website maturity language, limits, tests, and claims.
9. Validate a clean release-like package and decide production status with Nick.

The current readiness decision is **PASS WITH KNOWN LIMITATIONS; REVALIDATED AFTER
REOPENING** for the documented conservative contract on the directly validated
Linux toolchain. See `PRODUCTION-READINESS.md`. Reopen it again whenever supported
behavior or contradictory production evidence changes.

## After production

Track evolving web syntax deliberately, expand corpus diversity, respond to every
production bug with a regression, improve diagnostics/platform coverage, monitor
performance, and keep Nift integration and the public contract synchronized.
