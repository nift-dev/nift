#!/usr/bin/env bash
# Build the Nift Node binding native addon (build/nift_node.node).
#
# Locates Node's N-API headers (override with NIFT_NODE_INCLUDE), builds the
# frozen C ABI objects with -fPIC (make libnift_c.so) and statically links
# them into the addon so the .node is self-contained.
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
make -C "$EMBED" -j2 libnift_c.so >/dev/null

mkdir -p build
PIC_OBJECTS="$(find "$EMBED/.build/pic" -name '*.o' | tr '\n' ' ')"
g++ -std=c++17 -O2 -fPIC -shared \
  -I"$NIFT_NODE_INCLUDE" -I"$EMBED/include" \
  native/nift_node.cc $PIC_OBJECTS -pthread \
  -o build/nift_node.node
echo "built build/nift_node.node"
