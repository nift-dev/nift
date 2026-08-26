#!/usr/bin/env bash
# Build the Nift Python binding native extension (nift/_nift<abi>.so).
#
# Locates Python headers via python3-config, compiles the frozen C ABI
# sources into a PRIVATE pic directory (build/cabi-pic/) and statically links
# them into the extension. The private dir makes the build self-contained:
# it never touches the main Makefile's shared .build/pic objects, so running
# this concurrently with `make libnift_c.so` cannot link a torn object set.
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
mkdir -p nift build/cabi-pic

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
  -I"$PY_INCLUDES" -I"$EMBED/include" \
  src/nift_module.cc $PIC_OBJECTS -pthread \
  -o "nift/_nift$SUFFIX"

# Fail-fast: the extension must import cleanly (a torn/incomplete link is
# caught here, not on the first user run).
"$PYTHON" -c "import sys; sys.path.insert(0, '.'); import nift" || {
  echo "error: built extension does not import (torn link?)" >&2
  exit 1
}
echo "built nift/_nift$SUFFIX"
