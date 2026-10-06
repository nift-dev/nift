# Nift v4.7.1

Nift v4.7.1 is a maintenance patch following the v4.7.0 release.

## Changes

- Removes the three compiler warnings introduced in the v4.7 expression-parser
  hardening: two misleading-indentation sites and an obsolete, now-unused parser
  helper (`find_binary`) left after the iterative-expression changes. No parser,
  language or runtime behavior changed.
- Makes first-party compiler warnings a release-blocking invariant: every
  Nift-owned `src/` translation unit must compile clean under
  `-Wall -Wextra -pedantic -Werror` with both GCC and Clang before a candidate
  can pass the release rehearsal or be published. The gate is a job in the
  release workflow (required by both the rehearsal and publication jobs) and in
  the Deep guards workflow, closing the process hole that let a warning-bearing
  v4.7.0 candidate reach release.

## Compatibility

No language semantics, runtime API, package API, or C ABI changes.
C ABI remains 1.3.