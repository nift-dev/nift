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
CC="${CC:-gcc}"
CXX="${CXX:-g++}"
export LC_ALL=C

if [ -z "${NIFT_NODE_INCLUDE:-}" ]; then
  NODE_DISTRIBUTION_INCLUDE="$(node -p "require('path').resolve(require('path').dirname(process.execPath),'..','include','node')" 2>/dev/null || true)"
  for cand in /usr/include/node /usr/local/include/node \
    "$NODE_DISTRIBUTION_INCLUDE"; do
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
CC="$CC" CXX="$CXX" CFLAGS="-O2 -fPIC" bash "$EMBED/scripts/build_vendored_libffi.sh" "$TMP/libffi"
LIBFFI_INCLUDE="$TMP/libffi/install/include"
LIBFFI_A="$TMP/libffi/install/lib/libffi.a"
case "$(uname -s)" in
  Darwin)
    SHARED_FLAGS=(-bundle -Wl,-undefined,dynamic_lookup)
    PRIVATE_FFI_LDFLAGS=(-Wl,-dead_strip)
    ;;
  MINGW*|MSYS*|CYGWIN*)
    SHARED_FLAGS=(-shared)
    PRIVATE_FFI_LDFLAGS=(-Wl,--exclude-libs,ALL)
    ;;
  *)
    SHARED_FLAGS=(-shared)
    PRIVATE_FFI_LDFLAGS=(-Wl,--exclude-libs,ALL -Wl,-Bsymbolic)
    ;;
esac

PARSER_SOURCES=""
for source in "$EMBED"/src/Parser*.cpp; do
  [ -f "$source" ] || continue
  PARSER_SOURCES="$PARSER_SOURCES src/${source##*/}"
done
[ -n "$PARSER_SOURCES" ] || { echo "error: no src/Parser*.cpp sources found" >&2; exit 1; }

CABI_SOURCES="src/ProjectOwnership.cpp src/PackageTransaction.cpp src/embed/Engine.cpp src/embed/Context.cpp src/RuntimeValue.cpp src/Value.cpp \
  src/FileSystem.cpp src/JsonFile.cpp src/JsonSchema.cpp minifypp/src/Minify.cpp \
  markuppp/src/Markup.cpp markuppp/src/AsciiDoc.cpp markuppp/src/ReStructuredText.cpp \
  $PARSER_SOURCES src/Ast.cpp src/ProjectInfo.cpp src/ProjectRead.cpp src/ProjectState.cpp \
  src/WatchList.cpp src/BuildProgress.cpp src/embed/c_abi.cpp"
MARKUP_C_NAMES="blocks buffer cmark cmark_ctype houdini_href_e houdini_html_e houdini_html_u \
  html inlines iterator node references render scanners utf8"
PIC_OBJECTS=""
compile_object() {
  local src="$1"
  local obj="$2"
  case "$src" in
    *.c)
      "$CC" -std=c99 -O2 -fPIC -I"$EMBED/markuppp/vendor/cmark" -c "$EMBED/$src" -o "$obj"
      ;;
    *)
      "$CXX" -std=c++17 -O2 -fPIC -I"$LIBFFI_INCLUDE" -I"$EMBED/include" -I"$EMBED/src" -I"$EMBED/minifypp/include" \
          -I"$EMBED/minifypp/src" -I"$EMBED/markuppp/include" -I"$EMBED/markuppp/vendor/cmark" \
          -c "$EMBED/$src" -o "$obj"
      ;;
  esac
}
for src in $CABI_SOURCES; do
  obj="$OBJ/$(echo "$src" | tr '/' '_')"
  obj="${obj%.*}.o"
  compile_object "$src" "$obj"
  PIC_OBJECTS="$PIC_OBJECTS $obj"
done
for name in $MARKUP_C_NAMES; do
  src="markuppp/vendor/cmark/$name.c"
  obj="$OBJ/markuppp_vendor_cmark_${name}.o"
  compile_object "$src" "$obj"
  PIC_OBJECTS="$PIC_OBJECTS $obj"
done

"$CXX" -std=c++17 -O2 -fPIC "${SHARED_FLAGS[@]}" \
  -I"$NIFT_NODE_INCLUDE" -I"$EMBED/include" \
  native/nift_node.cc $PIC_OBJECTS "$LIBFFI_A" "${PRIVATE_FFI_LDFLAGS[@]}" -pthread \
  -o "$TMP/nift_node.node"

node -e "require('$TMP/nift_node.node')" || { echo "error: built addon does not load (torn link?)" >&2; exit 1; }

mv -f "$TMP/nift_node.node" "build/nift_node.node"
bash "$EMBED/scripts/audit_no_dynamic_libffi.sh" "build/nift_node.node"
bash "$EMBED/scripts/audit_private_libffi.sh" "build/nift_node.node"
echo "built build/nift_node.node"
