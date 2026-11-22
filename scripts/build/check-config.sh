#!/usr/bin/env bash
# Tooling checkpoint (component 1): refuse to link objects of another
# configuration.
#   check-config.sh COMPILER [ARGS...] -- OBJECT...
#     every OBJECT was built by COMPILER ARGS (config-id.sh);
#   check-config.sh --sets TEST_DIR OBJECT...
#     (tooling review F7, on every link of a signed object set) every OBJECT of
#     a signed set was built by its set's configuration: an in-tree object by
#     the in-tree signature (NIFT_INTREE_SIGNATURE, .build/obj-config/default.flags
#     whatever TEST_DIR is: the in-tree objects do not live in TEST_DIR), TEST_DIR/{pic,san,san-lifetime,tsan}/
#     by TEST_DIR/obj-config/<set>.flags, TEST_DIR/rt-obj/<config>/ by that
#     directory's .flags (the C++ or the C template, by the source's suffix);
#     other objects under TEST_DIR (private mutant and probe objects) carry no
#     signature and are not checked (tooling.md 1.4).
# Each object's sidecar (OBJECT minus .o, plus .cfg, written by
# compile-object.sh) holds the configuration identity and the sha256 of the
# object it was written with. A missing sidecar, another identity, or another
# object (a sidecar and an object written by two different compiles, which
# compile-object.sh renames one after the other) means the object was built by
# another configuration (for example a concurrent make with other flags in the
# same object directory), so the link stops instead of mixing configurations.
# What remains open: an object and its sidecar replaced together, by another
# configuration, between this check and the link (tooling.md 1.4).
set -u -o pipefail
. "$(dirname "$0")/config-id.sh"
enable set; builtin set -o pipefail  # (B18) restated after sourcing (config-id.sh sets it too)
bad=0; first=""; total=0
# check OBJECT ID...: the sidecar names one of the IDs and this object's content
check() {
  local o=$1; shift
  local c="${o%.o}.cfg" line id sum actual ok=0 want
  total=$((total + 1))
  if [ -f "$c" ] && line=$(cat "$c"); then
    id=${line%% *}; sum=${line#* }
    for want in "$@"; do [ "$id" = "$want" ] && ok=1; done
    if [ "$ok" = 1 ]; then
      if ! actual=$(sha256sum < "$o" | cut -d' ' -f1) || [ "$sum" != "$actual" ]; then ok=0; fi
    fi
  fi
  if [ "$ok" = 0 ]; then bad=$((bad + 1)); [ -n "$first" ] || first=$o; fi
}
# block_ids FILE: the identity of each compile template in a signature file
block_ids() {
  awk -v dir="$tmpd" '
    /^nift-object-config-v4$/ { n++ }
    !n { exit 1 }
    { print > (dir "/block" n) }
    END { if (!n) exit 1 }
  ' "$1" || return 1
  for b in "$tmpd"/block*; do sha256sum < "$b" | cut -d' ' -f1 || return 1; done
}
if [ "${1:-}" = --sets ]; then
  test_dir=$2; shift 2
  tmpd=$(mktemp -d) || exit 1; trap 'rm -rf "$tmpd"' EXIT
  signatures=()
  ids=()
  signature_count=0
  for o in "$@"; do
    case "$o" in
      "$test_dir"/rt-obj/*) rest=${o#"$test_dir"/rt-obj/}; set_dir=${rest%%/*}
        sig="$test_dir/rt-obj/$set_dir/.flags"; stem=${rest#*/} ;;
      "$test_dir"/pic/*|"$test_dir"/san/*|"$test_dir"/san-lifetime/*|"$test_dir"/tsan/*)
        rest=${o#"$test_dir"/}; set_dir=${rest%%/*}
        sig="$test_dir/obj-config/$set_dir.flags"; stem=${rest#*/} ;;
      "$test_dir"/*|/*|../*) continue ;;
      *) sig=${NIFT_INTREE_SIGNATURE:-.build/obj-config/default.flags}; stem=$o ;;
    esac
    if [ ! -f "$sig" ]; then check "$o"; continue; fi
    for ((i = 0; i < signature_count; i++)); do
      [ "${signatures[i]}" != "$sig" ] || break
    done
    if [ "$i" -eq "$signature_count" ]; then
      if ! hashes=$(block_ids "$sig" | tr '\n' ' '); then hashes=; fi
      rm -f "$tmpd"/block*
      signatures[i]=$sig
      ids[i]=$hashes
      signature_count=$((signature_count + 1))
    fi
    if ! read -r -a blocks <<< "${ids[i]}" || [ -z "${blocks[0]:-}" ]; then
      check "$o"
      continue
    fi
    if [ -f "${stem%.o}.c" ] && [ ${#blocks[@]} -gt 1 ]; then check "$o" "${blocks[1]}"
    else check "$o" "${blocks[0]}"; fi
  done
else
  tpl=()
  while [ $# -gt 0 ] && [ "$1" != -- ]; do tpl+=("$1"); shift; done
  if [ "${1:-}" != -- ]; then echo "check-config.sh: usage: COMPILER ARGS... -- OBJECTS... | --sets TEST_DIR OBJECTS..." >&2; exit 2; fi
  shift
  want=$(config_id "${tpl[@]}")
  for o in "$@"; do check "$o" "$want"; done
fi
if [ "$bad" -ne 0 ]; then
  echo "check-config: $bad of $total objects were not built by this configuration (first: $first);" \
       "another configuration wrote them (rebuild, and do not run makes with different flags" \
       "on one object directory at once)" >&2
  exit 1
fi
