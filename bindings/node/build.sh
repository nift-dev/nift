#!/usr/bin/env bash
# Build the Nift Node binding native addon (build/nift_node.node).
#
# Locates Node's N-API headers (override with NIFT_NODE_INCLUDE), compiles the
# frozen C ABI sources into a PRIVATE pic directory (build/cabi-pic/) and
# statically links them into the addon so the .node is self-contained. The
# private dir makes the build safe to run concurrently with the main
# Makefile's `make libnift_c.so` (no shared .build/pic object rewrites).
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
mkdir -p build/cabi-pic

CABI_SOURCES="src/ProjectOwnership.cpp src/Engine.cpp src/Context.cpp src/Value.cpp \
  src/FileSystem.cpp src/JsonFile.cpp src/JsonSchema.cpp minifypp/src/Minify.cpp \
  src/Parser.cpp src/ProjectInfo.cpp src/ProjectRead.cpp src/ProjectState.cpp \
  src/WatchList.cpp src/BuildProgress.cpp src/c_abi.cpp"
PIC_OBJECTS=""
for src in $CABI_SOURCES; do
  obj="build/cabi-pic/$(echo "$src" | tr '/' '_' | sed 's/\.cpp$/.o/')"
  g++ -std=c++17 -O2 -fPIC -I"$EMBED/include" -I"$EMBED/src" -I"$EMBED/minifypp/include" \
      -I"$EMBED/minifypp/src" -c "$EMBED/$src" -o "$obj"
  PIC_OBJECTS="$PIC_OBJECTS $obj"
done

g++ -std=c++17 -O2 -fPIC -shared \
  -I"$NIFT_NODE_INCLUDE" -I"$EMBED/include" \
  native/nift_node.cc $PIC_OBJECTS -pthread \
  -o build/nift_node.node

# Fail-fast: the addon must load (a torn/incomplete link is caught here).
node -e "require('./build/nift_node.node')" || {
  echo "error: built addon does not load (torn link?)" >&2
  exit 1
}
echo "built build/nift_node.node"
