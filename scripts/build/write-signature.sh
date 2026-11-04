#!/usr/bin/env bash
# Tooling checkpoint (component 1): the configuration signature of an object
# directory.  write-signature.sh FILE COMPILER CONFIG
# FILE holds the compiler identity and the exact compile flags of the objects
# built against it. It is rewritten only when its content changes, so an
# unchanged configuration never invalidates the objects, while any change of
# compiler, flags or defines makes FILE newer than every object (each object
# depends on it) and forces a rebuild: no stale object of another
# configuration is ever reused. Each process writes its own temporary file
# (concurrent makes, as LIBFFI_SIGNATURE).
set -u
file=$1 cxx=$2 config=$3
mkdir -p "$(dirname "$file")"
t="$file.tmp.$$"
printf '%s\n%s\n%s\n' "$cxx" "$($cxx --version 2>/dev/null | head -1)" "$config" > "$t" || exit 1
if cmp -s "$file" "$t"; then rm -f "$t"; else mv -f "$t" "$file"; fi
