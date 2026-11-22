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
# Tooling review F5: it fails closed: on no object, on an nm failure or any nm
# diagnostic other than "no symbols", and on an object nm reports empty that
# holds LTO sections (bytecode nm could not read) or that the platform section reader cannot read.
# Its granularity is (object, symbol): inside an object that is allowed a
# symbol (every builder user is allowed Construction::array_push, for
# example) it cannot tell a builder's use from an in-place or forged one;
# there the compile-time key and the lint are the guards (tooling.md 4.3, 2.4).
set -u -o pipefail
allow=$1; shift
[ $# -gt 0 ] || { echo "error: check-symbol-refs.sh: no objects to check" >&2; exit 1; }
RESTRICTED='^nift::rt::(Storage::|Construction::|mutation::Protocol::(sort_storage|sort_items|commit_permutation|commit_array)\()'
want=$(grep -v '^#' "$allow" | grep -v '^$' | sort -u)
symbol_reader=(nm)
section_reader=(readelf -SW)
lto_section='\.gnu\.lto_'
case "$(uname -s)" in
  Darwin)
    symbol_reader=(xcrun nm)
    section_reader=(xcrun otool -l)
    lto_section='^[[:space:]]*segname[[:space:]]+__LLVM([[:space:]]|$)'
    ;;
  MSYS*|MINGW*|UCRT*|CYGWIN*) section_reader=(objdump -h) ;;
esac
errs=$(mktemp) || exit 1; trap 'rm -f "$errs" "$errs.u" "$errs.e" "$errs.sections"' EXIT
for o in "$@"; do
  rel=$(echo "$o" | sed -E 's|^\.build/[^/]+/||')
  if ! "${symbol_reader[@]}" -u -C "$o" > "$errs.u" 2> "$errs.e"; then
    echo "error: check-symbol-refs.sh: nm failed on $o: $(head -1 "$errs.e")" >&2; exit 1
  fi
  if grep -qv ': no symbols$' "$errs.e"; then
    echo "error: check-symbol-refs.sh: nm on $o: $(head -1 "$errs.e")" >&2; exit 1
  fi
  if [ ! -s "$errs.u" ]; then
    if ! "${section_reader[@]}" "$o" > "$errs.sections" 2> "$errs.e" ||
       [ ! -s "$errs.sections" ] || [ -s "$errs.e" ]; then
      echo "error: check-symbol-refs.sh: cannot read $o: $(head -1 "$errs.e")" >&2
      exit 1
    fi
    if grep -Eq "$lto_section" "$errs.sections"; then
      echo "error: check-symbol-refs.sh: $o is LTO bytecode nm did not read" >&2; exit 1
    fi
  fi
  # re-review N1: every class nm -u lists is a reference (U, and the weak w and
  # v, which a static link binds to the strong definition)
  # Darwin nm -u prints the demangled names without a type prefix.
  sed -E 's/^ *[A-Za-z] //' "$errs.u" | grep -E "$RESTRICTED" | sed -E 's/\(.*//' | sed "s|^|$rel |" >> "$errs"
done
have=$(sort -u "$errs")
extra=$(comm -13 <(echo "$want") <(echo "$have"))
if [ -n "$extra" ]; then
  echo "error: references to the storage commits outside the reviewed allowlist ($allow):" >&2
  echo "$extra" | sed 's/^/  /' >&2
  echo "  (a forged SortKey/Protocol/Storage declaration, RECHECK2 N4, or a new use to review: tooling.md 4)" >&2
  exit 1
fi
