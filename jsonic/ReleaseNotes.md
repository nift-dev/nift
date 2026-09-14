# Release notes

## Opt-in configuration parsing and structured diagnostics

- Keep RFC 8259 JSON parsing strict by default while allowing callers to opt
  into line/block comments and trailing commas independently.
- Expose parse failures as `ParseDiagnostic` with a message, zero-based byte
  offset, and one-based line and column, while retaining the existing error
  string overloads.
- Add configurable nesting depth and an opt-in duplicate-key rejection policy;
  source-order preservation remains the default.
- Apply the same parsing options to named-array streaming and add adversarial
  coverage for composition, string boundaries, EOF comments, malformed
  comments, trailing commas, duplicate members and custom depth limits.

## RFC 8259 conformance hardening

- Accept and preserve duplicate object members, which RFC 8259 permits
  syntactically while warning about interoperability.
- Reject malformed, overlong, surrogate and out-of-range unescaped UTF-8.
- Enforce a 512-level nesting limit so pathological input fails cleanly.
- Add focused regression coverage for all three failure classes.

## Initial standalone checkpoint

Jsonic++ begins as the standalone home of the dependency-free JSON implementation already used by Nift and Minify++. The initial checkpoint preserves the established parser API and behavior, adds standalone smoke/adversarial/sanitizer targets, and makes synchronization with embedded copies explicit.
