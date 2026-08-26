#!/usr/bin/env bash
# Build the Nift Python binding native extension (nift/_nift<abi>.so).
#
# Locates Python headers via python3-config, compiles the frozen C ABI
# sources into a PER-INVOCATION temporary pic directory and statically links
# them into the extension. Intermediate objects are never shared between
# concurrent build invocations or with the main Makefile, and the final
# artifact is published atomically, so same-binding concurrent builds are
# safe. A fail-fast import check turns a malformed artifact into a build
# failure instead of a first-user-run surprise.
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
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-py-build.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
OBJ="$TMP/cabi-pic"
mkdir -p "$OBJ" nift

CABI_SOURCES="src/ProjectOwnership.cpp src/Engine.cpp src/Context.cpp src/Value.cpp \
  src/FileSystem.cpp src/JsonFile.cpp src/JsonSchema.cpp minifypp/src/Minify.cpp \
  src/Parser.cpp src/ProjectInfo.cpp src/ProjectRead.cpp src/ProjectState.cpp \
  src/WatchList.cpp src/BuildProgress.cpp src/c_abi.cpp"
PIC_OBJECTS=""
for src in $CABI_SOURCES; do
  obj="$OBJ/$(echo "$src" | tr '/' '_' | sed 's/\.cpp$/.o/')"
  g++ -std=c++17 -O2 -fPIC -I"$EMBED/include" -I"$EMBED/src" -I"$EMBED/minifypp/include" \
      -I"$EMBED/minifypp/src" -c "$EMBED/$src" -o "$obj"
  PIC_OBJECTS="$PIC_OBJECTS $obj"
done

g++ -std=c++17 -O2 -fPIC -shared \
  -I"$PY_INCLUDES" -I"$EMBED/include" \
  src/nift_module.cc $PIC_OBJECTS -pthread \
  -o "$TMP/_nift$SUFFIX"

# Fail-fast: the extension must load with all symbols resolved (catches a torn
# or incomplete link at build time).
"$PYTHON" -c "
import importlib.util, sys
spec = importlib.util.spec_from_file_location('_nift', '$TMP/_nift$SUFFIX')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
" || { echo "error: built extension does not load (torn link?)" >&2; exit 1; }

mv -f "$TMP/_nift$SUFFIX" "nift/_nift$SUFFIX"
echo "built nift/_nift$SUFFIX"
