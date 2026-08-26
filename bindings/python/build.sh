#!/usr/bin/env bash
# Build the Nift Python binding native extension (nift/_nift<abi>.so).
#
# Locates Python headers via python3-config, builds the frozen C ABI objects
# with -fPIC (make libnift_c.so) and statically links them into the extension.
set -euo pipefail
cd "$(dirname "$0")"
EMBED="$(cd ../../ && pwd)"

PYTHON="${PYTHON:-python3}"
PY_INCLUDES="$("$PYTHON" -c 'import sysconfig; print(sysconfig.get_paths()["include"])')"
if [ ! -f "$PY_INCLUDES/Python.h" ]; then
  echo "error: Python headers not found (install python3-dev; set PYTHON=...)" >&2
  exit 1
fi
SUFFIX="$("$PYTHON" -c 'import sysconfig; print(sysconfig.get_config_var("EXT_SUFFIX"))')"

echo "using python headers: $PY_INCLUDES"
make -C "$EMBED" -j2 libnift_c.so >/dev/null

mkdir -p nift build
PIC_OBJECTS="$(find "$EMBED/.build/pic" -name '*.o' | tr '\n' ' ')"
g++ -std=c++17 -O2 -fPIC -shared \
  -I"$PY_INCLUDES" -I"$EMBED/include" \
  src/nift_module.cc $PIC_OBJECTS -pthread \
  -o "nift/_nift$SUFFIX"
echo "built nift/_nift$SUFFIX"
