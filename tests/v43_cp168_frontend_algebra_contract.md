# CP168 frontend algebra contract

This fixture freezes the public contract before CP170+ production edits.

- Accepted: `unique_by`, `min_by`, `max_by`, `index_by`, `count_by`, `take`, `drop`, `chunk`, `partition`, `from_entries`, shallow `pick`, shallow `omit`, immutable conservative `merge_deep`, stable compound/directional `sort_by`, `group_by_each`, and intrinsic page hierarchy (`parent`, `children`, `ancestors`, `descendants`, with `siblings` subject to CP191).
- Deferred: `zip`; ordered `previous`/`next` navigation.
- All value operations are non-mutating. Array/source insertion order is preserved unless sorting explicitly changes it.
- Callback selectors/predicates are evaluated once per source item for the new selector operations. Callback failure is atomic: no partial value is exposed.
- Generated object keys accept scalar string/number/bool values, are converted using Nift's ordinary scalar rendering, and must be unique after conversion. Invalid or duplicate generated keys are errors.
- `from_entries()` consumes the canonical `{key, value}` records emitted by `entries()`; malformed entries are errors.
- `index_by(selector)` preserves first-source generated-key order and rejects duplicates.
- `pick(keys)` and `omit(keys)` accept an array of string keys only. Missing keys are ignored. `pick` follows requested-key order; `omit` preserves source-object order. Dotted/deep paths are not interpreted.
- `merge_deep(rhs)` requires two objects at the outer call. Object/object collisions recurse; every other collision is replaced by RHS, including arrays. Inputs are not mutated.
- `partition` must return named matched/unmatched collections (exact spelling frozen at CP176), never a positional pair.
- `take`, `drop`, `chunk` initially apply to arrays only.
- Hierarchy is lazy/pay-for-use, O(n) stored structure, and uses compact structural invalidation rather than expanded per-page dependency sets.
