#!/usr/bin/env bash
set -euo pipefail

# ---------------------------------------------------------------------------
# Minify++ / Nift release-content synchronization check.
#
# Nift vendors a RELEASED version of Minify++. The comparison target is the
# sibling repository's release tag for the version Nift declares it vendors,
# NOT the sibling working tree / HEAD (which normally advances to a development
# version such as 1.1.4-dev immediately after a release).
#
#   sibling HEAD              = development state; irrelevant here
#   sibling release tag       = canonical comparison point for the vendored version
#   Nift vendored payload     = must match that tagged release content
#
# The script never checks out, resets or modifies either repository; tags are
# resolved and inspected through Git only.
#
# The checker script itself (scripts/check-nift-sync.sh) is documented Nift
# integration machinery, not released dependency content: it is excluded from
# the payload-vs-tag comparison and instead verified to match between the
# sibling and vendored trees so the machinery stays synchronized.
# ---------------------------------------------------------------------------

# Version of Minify++ that Nift declares it vendors. Update only when Nift
# intentionally adopts a new Minify++ release (this value must travel with the
# payload update).
DECLARED_VERSION="1.1.3"

# Released payload files that must match the release tag byte-for-byte.
# scripts/check-nift-sync.sh is intentionally not listed here (see above).
PAYLOAD_FILES=(
  benchmarks/minify_benchmark.cpp
  LICENSE
  Makefile
  README.md
  ReleaseNotes.md
  cli/main.cpp
  docs/MEMORY-SAFETY.md
  include/minify/Minify.h
  scripts/distcheck.sh
  scripts/memory_safety.py
  src/Json.h
  src/Minify.cpp
  tests/cli_smoke.sh
  tests/cross_format_adversarial.sh
  tests/fuzz_smoke.cpp
  tests/memory_lifetime.cpp
  tests/memory_cli_stress.sh
  tests/minify_format_idempotence.sh
  tests/minify_css_postcss_semantics.sh
  tests/minify_generated_semantics.sh
  tests/minify_jsx_generated.sh
  tests/minify_node_semantics.sh
  tests/minify_smoke.cpp
)

if [[ $# -ne 1 ]]; then
  echo "usage: $0 /path/to/nift/minifypp" >&2
  exit 2
fi
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EMBEDDED="$(cd "$1" 2>/dev/null && pwd)" || {
  echo "Minify++ sync check: embedded directory does not exist: $1" >&2
  exit 2
}

version_ge() {  # returns 0 if $1 >= $2
  local -a a=() b=()
  IFS='.' read -r -a a <<< "$1"
  IFS='.' read -r -a b <<< "$2"
  local i
  for i in 0 1 2; do
    local ai="${a[$i]:-0}" bi="${b[$i]:-0}"
    if ((10#$ai > 10#$bi)); then return 0; fi
    if ((10#$ai < 10#$bi)); then return 1; fi
  done
  return 0
}

# --- locate the release tag for the declared vendored version ----------------
tag="v$DECLARED_VERSION"
if [[ ! "$DECLARED_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] ||
   ! git -C "$ROOT" rev-parse -q --verify "refs/tags/$tag" >/dev/null 2>&1; then
  echo "Minify++ sync: declared vendored version $tag has no matching release tag in the sibling repository (tag unavailable locally)" >&2
  exit 1
fi

# --- verify vendored payload against the release tag -------------------------
failed=0
for file in "${PAYLOAD_FILES[@]}"; do
  if ! git -C "$ROOT" cat-file -e "$tag:$file" >/dev/null 2>&1; then
    echo "Minify++ sync: '$file' does not exist at release tag $tag" >&2
    failed=1
    continue
  fi
  if [[ ! -f "$EMBEDDED/$file" ]]; then
    echo "Minify++ sync check: embedded file missing: $file" >&2
    failed=1
    continue
  fi
  if ! git -C "$ROOT" show "$tag:$file" | cmp -s - "$EMBEDDED/$file"; then
    echo "Minify++ sync: payload differs from release tag $tag: $file" >&2
    failed=1
  fi
done

# --- checker-machinery self-sync ---------------------------------------------
if ! cmp -s "$ROOT/scripts/check-nift-sync.sh" "$EMBEDDED/scripts/check-nift-sync.sh"; then
  echo "Minify++ sync: check-nift-sync.sh differs between sibling and vendored trees" >&2
  failed=1
fi

# --- latest stable release tag (semantic ordering, releases only) -------------
latest=""
while read -r t; do
  v="${t#v}"
  if [[ "$v" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    if [[ -z "$latest" ]] || version_ge "$v" "${latest#v}"; then latest="$t"; fi
  fi
done < <(git -C "$ROOT" tag -l "v*" | sort)

update_available="no"
if [[ -n "$latest" ]] && [[ "${latest#v}" != "$DECLARED_VERSION" ]] &&
   version_ge "${latest#v}" "$DECLARED_VERSION"; then
  update_available="yes"
fi

echo "vendored version:        v$DECLARED_VERSION"
echo "matching sibling tag:    $tag"
echo "latest sibling release:  ${latest:-none}"
echo "payload matches tag:     $([ "$failed" -eq 0 ] && echo yes || echo no)"
echo "update available:        $update_available"

if [[ "$failed" -ne 0 ]]; then exit 1; fi
printf 'Minify++ standalone/Nift synchronization passed (%d payload files at %s)\n' "${#PAYLOAD_FILES[@]}" "$tag"