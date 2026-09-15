#!/usr/bin/env bash
set -euo pipefail

# ---------------------------------------------------------------------------
# Jsonic++ / Nift release-content synchronization check.
#
# Nift vendors a RELEASED version of Jsonic++. The comparison target is the
# sibling repository's release tag for the version Nift declares it vendors,
# NOT the sibling working tree / HEAD (which normally advances to a development
# version such as 1.0.1-dev immediately after a release).
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

# Version of Jsonic++ that Nift declares it vendors. Update only when Nift
# intentionally adopts a new Jsonic++ release (this value must travel with the
# payload update).
DECLARED_VERSION="1.0.0"

# Released payload files that must match the release tag byte-for-byte.
# scripts/check-nift-sync.sh is intentionally not listed here (see above).
PAYLOAD_FILES=(
  .gitignore HANDOVER.md LICENSE Makefile README.md ReleaseNotes.md
  include/json.h
  tests/json_smoke.cpp tests/json_adversarial.cpp tests/json_memory_lifetime.cpp
  scripts/check-minify-sync.sh scripts/memory_safety.py
  docs/MEMORY-SAFETY.md
  docs/handover/ARCHITECTURE.md docs/handover/DEVELOPMENT.md docs/handover/TESTING.md
  docs/handover/DECISIONS.md docs/handover/ROADMAP.md docs/handover/PROJECT-HISTORY.md
)

if [[ $# -ne 1 ]]; then echo "usage: $0 /path/to/nift" >&2; exit 2; fi
root=$(cd "$(dirname "$0")/.." && pwd)
nift=$(cd "$1" && pwd)
embedded="$nift/jsonic"

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
   ! git -C "$root" rev-parse -q --verify "refs/tags/$tag" >/dev/null 2>&1; then
  echo "Jsonic++ sync: declared vendored version $tag has no matching release tag in the sibling repository (tag unavailable locally)" >&2
  exit 1
fi

# --- verify vendored payload against the release tag -------------------------
failed=0
for file in "${PAYLOAD_FILES[@]}"; do
  if ! git -C "$root" cat-file -e "$tag:$file" >/dev/null 2>&1; then
    echo "Jsonic++ sync: '$file' does not exist at release tag $tag" >&2
    failed=1
    continue
  fi
  if [[ ! -f "$embedded/$file" ]]; then
    echo "Jsonic++ sync mismatch: missing Nift file jsonic/$file" >&2
    failed=1
    continue
  fi
  if ! git -C "$root" show "$tag:$file" | cmp -s - "$embedded/$file"; then
    echo "Jsonic++ sync: payload differs from release tag $tag: $file" >&2
    failed=1
  fi
done

# --- checker-machinery self-sync ---------------------------------------------
if ! cmp -s "$root/scripts/check-nift-sync.sh" "$embedded/scripts/check-nift-sync.sh"; then
  echo "Jsonic++ sync: check-nift-sync.sh differs between sibling and vendored trees" >&2
  failed=1
fi

# --- Nift wrapper integrity ---------------------------------------------------
grep -q '#include "../jsonic/include/json.h"' "$nift/src/Json.h" || {
  echo "Nift src/Json.h is not the Jsonic++ compatibility wrapper" >&2
  exit 1
}

# --- latest stable release tag (semantic ordering, releases only) -------------
latest=""
while read -r t; do
  v="${t#v}"
  if [[ "$v" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    if [[ -z "$latest" ]] || version_ge "$v" "${latest#v}"; then latest="$t"; fi
  fi
done < <(git -C "$root" tag -l "v*" | sort)

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
echo "Jsonic++ standalone/Nift synchronization passed (${#PAYLOAD_FILES[@]} payload files at $tag)"