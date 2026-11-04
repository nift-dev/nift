#!/usr/bin/env bash
# Tooling checkpoint (component 4, RECHECK2 N4): link-time allowlist of
# references to the storage commits.
#   check-symbol-refs.sh ALLOWLIST OBJECT...
# A translation unit can declare its own public SortKey, Protocol or Storage
# (a forged header, RECHECK2 forge/g20: an ODR violation that needs no
# diagnostic, so no compiler or header can refuse it) and call the real
# out-of-line commits by their mangled names. Such a unit has an undefined
# reference to them. This check lists, for every object about to be linked,
# its undefined references to the restricted symbols (rt::Storage's members,
# the protocol's sort commits and raw array commit), and fails on any
# (object, symbol) pair that is not in ALLOWLIST (tests/abi/restricted-symbol-refs.txt),
# so a new reference, legitimate or not, needs review. Object paths are taken
# relative to their object directory (.build/<set>/ is dropped).
set -u
allow=$1; shift
RESTRICTED='^nift::rt::(Storage::|mutation::Protocol::(sort_storage|sort_items|commit_permutation|commit_array)\()'
want=$(grep -v '^#' "$allow" | grep -v '^$' | sort -u)
have=$(for o in "$@"; do
  rel=$(echo "$o" | sed -E 's|^\.build/[^/]+/||')
  nm -u -C "$o" 2>/dev/null | sed 's/^ *U //' | grep -E "$RESTRICTED" | sed -E 's/\(.*//' | sed "s|^|$rel |"
done | sort -u)
extra=$(comm -13 <(echo "$want") <(echo "$have"))
if [ -n "$extra" ]; then
  echo "error: references to the storage commits outside the reviewed allowlist ($allow):" >&2
  echo "$extra" | sed 's/^/  /' >&2
  echo "  (a forged SortKey/Protocol/Storage declaration, RECHECK2 N4, or a new use to review: tooling.md 4)" >&2
  exit 1
fi
