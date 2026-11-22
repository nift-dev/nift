#!/usr/bin/env bash
# Run with each Bash version to exercise both link modes with real objects.
set -eu -o pipefail
build=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"

expect() {
  local expected=$1 label=$2 status=0
  shift 2
  "$@" >out 2>&1 || status=$?
  if [ "$status" -ne "$expected" ] ||
     { [ "$expected" -eq 1 ] && ! grep -q 'objects were not built by this configuration' out; }; then
    cat out >&2
    printf 'FAIL %s (exit %s, expected %s)\n' "$label" "$status" "$expected" >&2
    exit 1
  fi
  printf 'PASS %s (exit %s)\n' "$label" "$status"
}

export NIFT_INTREE_SIGNATURE='signatures with spaces/default.flags'
printf 'int one(void) { return 1; }\n' >one.cpp
printf 'int two(void) { return 2; }\n' >two.c
printf 'int three(void) { return 3; }\n' >three.cpp
"$BASH" "$build/write-signature.sh" "$NIFT_INTREE_SIGNATURE" c++ -O1 --- cc -O2
"$BASH" "$build/write-signature.sh" build/pic.flags c++ -O0
mkdir -p build/obj-config
mv build/pic.flags build/obj-config/pic.flags
"$BASH" "$build/compile-object.sh" one.o -- c++ -O1 -c one.cpp
"$BASH" "$build/compile-object.sh" two.o -- cc -O2 -c two.c
"$BASH" "$build/compile-object.sh" build/pic/three.o -- c++ -O0 -c three.cpp

check=("$BASH" "$build/check-config.sh")
expect 0 'direct fingerprint' "${check[@]}" c++ -O1 -- one.o
expect 0 'multiple signatures, including cached C template' "${check[@]}" --sets build one.o build/pic/three.o two.o
expect 1 'different flags' "${check[@]}" c++ -O2 -- one.o
expect 1 'different compiler' "${check[@]}" cc -O1 -- one.o

cp one.o good.o
cp one.cfg good.cfg
printf '%s\n' corrupt >>one.o
expect 1 'stale object' "${check[@]}" --sets build one.o
mv good.o one.o
rm one.cfg
expect 1 'missing sidecar' "${check[@]}" c++ -O1 -- one.o
printf '%s bogus\n' "$(cut -d' ' -f1 good.cfg)" >one.cfg
expect 1 'malformed sidecar' "${check[@]}" c++ -O1 -- one.o
mv good.cfg one.cfg
rm "$NIFT_INTREE_SIGNATURE"
expect 1 'missing signature' "${check[@]}" --sets build one.o
printf 'invalid signature\n' >"$NIFT_INTREE_SIGNATURE"
expect 1 'malformed signature' "${check[@]}" --sets build one.o
: >"$NIFT_INTREE_SIGNATURE"
expect 1 'empty signature' "${check[@]}" --sets build one.o two.o
