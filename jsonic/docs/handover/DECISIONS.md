# Jsonic++ decisions

- Keep the project header-only and dependency-free.
- Keep the public header named `json.h`.
- Preserve the existing `json` namespace and `json::Document` API during extraction.
- Reject duplicate object keys.
- Standalone Jsonic++ owns parser semantics; Nift and Minify++ vendor synchronized copies.
- Generalize only after concrete parser contracts justify the added surface.
