# Nift v4.3.0

Nift v4.3.0 completes the v4.3 language and content surface, adds the final
language tranche (runtime introspection, optionality, destructuring, ranges and
symbolic enums), expression-valued object literals, and is hardened through
exhaustive sanitizer/Valgrind/lifetime, stress, regression, performance,
security and documentation audits. Version and packaging sources are
reconciled at 4.3.0 and the intended release tag is `v4.3.0`.

## Final language tranche

- **Runtime introspection.** `type(value)` returns a stable public type name
  (`null`, `bool`, `int`, `float`, `string`, `array`, `object`, `struct`,
  `function`, `collection`, `file`, `stream`, `page`, `enum`), with matching
  `is_null`, `is_bool`, `is_number`, `is_int`, `is_float`, `is_string`,
  `is_array`, `is_object`, `is_struct`, `is_function`, `is_collection` and
  `is_enum` predicates. Internal runtime markers are not exposed as public
  types.
- **Optionality.** `lhs ?? rhs` treats `null` as the sole absence value and
  evaluates the right-hand side lazily; `?.` and `?[]` are null propagation
  only — a null receiver produces `null`, while missing members/keys and type
  errors remain errors (a misspelled member is never silently `null`). These
  compose with the existing `has`/`get` missing-key facilities.
- **Destructuring.** `[a, b] := pair` / `= pair` is flat and exact-length;
  `{x, y} := obj` / `= obj` requires each named key and ignores extra keys.
  The full pattern and right-hand side validate before any binding changes.
- **Ranges.** `range(stop)`, `range(start, stop)` and `range(start, stop,
  step)` produce stop-exclusive signed-integer arrays; a zero step is an error
  and results are bounded (a 10,000,000-item limit prevents runaway
  allocation).
- **Enums.** `enum Name { A, B, C }` declares symbolic, integer-backed enum
  members with optional explicit integer values, implicit numbering starting at
  0, mixed explicit/implicit numbering, and negative values. Members render
  symbolically (`Status.Published` -> `Published`), convert explicitly with
  `to_int()`/`to_string()`, and serialize as their backing integer in JSON
  output. Enum values are not interchangeable with arbitrary integers.
- **Expression-valued object literals.** `{"key": expr}` mirrors array
  literals: quoted string values are literal strings and every other value is
  a Nift expression evaluated left-to-right exactly once, preserving runtime
  types. Pure JSON objects retain the JSON fast path; keys remain
  double-quoted; duplicate keys remain errors.

## Language and content surface

- **Frontend algebra.** Composable read-only collection/object operations:
  `map`, `filter`, `reduce`, `any`, `all`, `find`, `find_index`, `count`,
  `sort_by`, `group_by`, `unique`, `flatten`, `sum`, `min`, `max`,
  `from_entries`, `index_by`, `pick`, `omit`, `merge_deep`, `partition`,
  `unique_by`, `min_by`, `max_by`, `count_by`, `take`, `drop`, `chunk`,
  `group_by_each`, `contains`, `indexOf` and the string/object surface
  (`starts_with`, `ends_with`, `trim`, `to_lower`, `to_upper`, `replace`,
  `split`, `empty`, `has`, `get`, `keys`, `values`, `entries`, ...).
- **Intrinsic page hierarchy.** `page` (current page), `page(name)` lookup and
  `parent`, `children`, `ancestors` (root-first), `descendants` and `siblings`
  over the tracked page tree, built lazily in expected O(N) with a compact
  structural fingerprint for incremental correctness.
- **Expression-valued array literals** evaluate elements left-to-right exactly
  once while preserving ordinary JSON-compatible literals, and computed
  bracket access accepts arbitrary resolvable string/integer keys.
- **Escaping/encoding helpers.** `html_escape`, `attr_escape` and `url_encode`
  provide explicit HTML text, quoted-attribute and URL-component encoding
  without changing ordinary interpolation (no automatic escaping).

## Reliability and hardening

- Recursive lambdas and block functions are bounded by the callable recursion
  depth guard and report a clean `callable recursion depth exceeded` error
  instead of overflowing the stack; integer literals outside the signed 64-bit
  range report a precise diagnostic.
- Exhaustive ASan/UBSan/LSan and Valgrind/leak/lifetime campaigns over the
  broad language/content surface (including repeated builds, construction
  workloads and error paths) found no memory defects. Deep nesting, long
  optionality/ternary chains, large ranges, malformed syntax, import cycles and
  path-traversal attempts are all bounded with useful diagnostics.
- The independent regression suite (47 contract modules) and the full internal
  test surface pass; ordinary, project-aware, hierarchy-aware and typed-content
  builds show no unused-feature cost.

## Compatibility

- v4.2 syntax and behaviour are retained; `$[...]` interpolation remains
  unescaped by default (explicit `html_escape`/`attr_escape`/`url_encode` are
  provided per output context).
- `@path`, `@input`, `@dep`, `@for`/`@if`, JSON/Schema, typed content,
  taxonomies, pagination, contracts, structs and the native scripting hosts
  are unchanged in their public contract.

## Evidence bounds

- Release-candidate evidence (full internal + independent regression,
  performance/pay-for-use, sanitizer and Valgrind/lifetime checks, website
  idempotence, version/guarantee consistency, security/confinement) is recorded
  in the v4.3 language-campaign handover. Known limitations (YAML-lenient front
  matter, standalone-script confinement by design, the ASan stack-frame
  recursion characteristic, and the 64-level recursion contract) are recorded
  there and are not release-blocking.