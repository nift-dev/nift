# Jsonic++ decisions

- Keep the project header-only and dependency-free.
- Keep the public header named `json.h`.
- Preserve the existing `json` namespace and `json::Document` API during extraction.
- Accept duplicate object keys and preserve them in source order because RFC
  8259 permits them syntactically; applications should avoid relying on their
  interpretation. Callers that require unique configuration keys may explicitly
  select `DuplicateKeyPolicy::Reject`; do not change the compatibility default.
- Require well-formed UTF-8 in unescaped string content.
- Reject nesting deeper than 512 arrays/objects by default before stack
  exhaustion, while allowing callers to select a lower boundary.
- Keep RFC 8259 parsing strict by default. Comments and trailing commas are
  independent opt-in options for configuration-file consumers, not permissive
  recovery behavior.
- Keep structured diagnostics at the parse boundary. Do not add source spans to
  every DOM node without a separate consumer contract that justifies the memory
  and API cost.
- Standalone Jsonic++ owns parser semantics; Nift and Minify++ vendor synchronized copies.
- Generalize only after concrete parser contracts justify the added surface.
