#!/usr/bin/env bash
# Build the Nift Node binding native addon (build/nift_node.node).
#
# Locates Node's N-API headers (override with NIFT_NODE_INCLUDE), compiles the
# frozen C ABI sources into a PER-INVOCATION temporary pic directory and
# statically links them into the addon. Intermediate objects are never shared
# between concurrent build invocations or with the main Makefile, and the final
# artifact is published atomically, so same-binding concurrent builds are
# safe. A fail-fast load check catches a malformed artifact at build time.
set -euo pipefail
cd "$(dirname "$0")"
EMBED="$(cd ../../ && pwd)"

if [ -z "${NIFT_NODE_INCLUDE:-}" ]; then
  for cand in /usr/include/node /usr/local/include/node \
    "$(dirname "$(dirname "$(readlink -f "$(command -v node)")")")/include/node"; do
    if [ -f "$cand/node_api.h" ]; then
      NIFT_NODE_INCLUDE="$cand"
      break
    fi
  done
fi
if [ -z "${NIFT_NODE_INCLUDE:-}" ] || [ ! -f "$NIFT_NODE_INCLUDE/node_api.h" ]; then
  echo "error: Node headers not found (set NIFT_NODE_INCLUDE to the dir containing node_api.h)" >&2
  exit 1
fi

echo "using node headers: $NIFT_NODE_INCLUDE"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-node-build.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
OBJ="$TMP/cabi-pic"
mkdir -p "$OBJ" build

CABI_SOURCES="src/ProjectOwnership.cpp src/embed/Engine.cpp src/embed/Context.cpp src/Value.cpp \
  src/FileSystem.cpp src/JsonFile.cpp src/JsonSchema.cpp minifypp/src/Minify.cpp \
  src/Parser.cpp src/ProjectInfo.cpp src/ProjectRead.cpp src/ProjectState.cpp \
  src/WatchList.cpp src/BuildProgress.cpp src/embed/c_abi.cpp"
PIC_OBJECTS=""
for src in $CABI_SOURCES; do
  obj="$OBJ/$(echo "$src" | tr '/' '_' | sed 's/\.cpp$/.o/')"
  g++ -std=c++17 -O2 -fPIC -I"$EMBED/include" -I"$EMBED/src" -I"$EMBED/minifypp/include" \
      -I"$EMBED/minifypp/src" -c "$EMBED/$src" -o "$obj"
  PIC_OBJECTS="$PIC_OBJECTS $obj"
done

g++ -std=c++17 -O2 -fPIC -shared \
  -I"$NIFT_NODE_INCLUDE" -I"$EMBED/include" \
  native/nift_node.cc $PIC_OBJECTS -pthread \
  -o "$TMP/nift_node.node"

node -e "require('$TMP/nift_node.node')" || { echo "error: built addon does not load (torn link?)" >&2; exit 1; }

mv -f "$TMP/nift_node.node" "build/nift_node.node"
echo "built build/nift_node.node"
