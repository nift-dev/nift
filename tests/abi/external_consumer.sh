#!/usr/bin/env bash
# Tooling checkpoint (component 6): an external consumer of libnift_c.so.
#   external_consumer.sh LIBRARY [CC]
# Copies the public header, tests/abi/external_consumer.c and the built shared
# library into a fresh directory outside the repository, compiles the program
# there against the copied header only, links it dynamically (-lnift_c: no
# static archive, no repository object or include path), checks that the
# executable needs libnift_c.so and that every nift_* symbol it uses is
# undefined in it (resolved from the library at run time), then runs it with
# a minimal environment from that directory.
set -eu -o pipefail
lib=$(readlink -f "${1:?usage: external_consumer.sh LIBRARY [CC]}")
cc=${2:-cc}
repo=$(cd "$(dirname "$0")/../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/nift-abi-consumer.XXXXXX")
trap 'rm -rf "$work"' EXIT
case "$work" in "$repo"/*) echo "FAIL abi-consumer: work directory inside the repository"; exit 1;; esac
mkdir -p "$work/include/nift" "$work/lib"
cp "$repo/include/nift/c_abi.h" "$work/include/nift/"
cp "$repo/tests/abi/external_consumer.c" "$work/"
cp "$lib" "$work/lib/libnift_c.so"
cd "$work"
"$cc" -std=c99 -Wall -Wextra -pedantic -Werror -Iinclude external_consumer.c -Llib -lnift_c \
  -Wl,-rpath,'$ORIGIN/lib' -o consumer
grep -q 'NEEDED.*\[libnift_c.so\]' <<< "$(readelf -d consumer)" || { echo "FAIL abi-consumer: not linked dynamically"; exit 1; }
if awk '{print $3}' <<< "$(nm --defined-only consumer)" | grep -q '^nift_'; then
  echo "FAIL abi-consumer: a nift_* symbol is defined in the executable"; exit 1
fi
used=$(nm -u consumer | awk '{print $2}' | grep -c '^nift_' || true)
env -i PATH=/usr/bin:/bin HOME="$work" ./consumer
echo "PASS abi-consumer: built outside the tree, linked dynamically, $used nift_* symbols resolved from the library at run time"
