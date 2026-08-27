#!/usr/bin/env bash
# Single-input release staging: ONE required version input; every artifact's
# metadata is DERIVED from it (no independent hard-coded versions). Validated
# against the canonical CLI version unless NIFT_SKIP_CLI_CHECK=1 (used by the
# non-current-version regression).
#
# Builds (nothing published):
#   dist/nift-linux-<arch>                      CLI binary
#   dist/nift-embed-<os>-<arch>.tar.gz          native C/C++ bundle + SHA256SUMS
#   dist/nift-<v>-*.whl + dist/nift-<v>.tar.gz  Python wheel (built from the
#                                               self-contained sdist) + sdist
#   dist/nift-<v>.tgz                           npm package (stamped package.json)
#   dist/Nift.<v>.nupkg                         NuGet package (-p:Version)
#
# Usage: packaging/stage-release.sh <version>
#   NIFT_SKIP_CLI_CHECK=1 packaging/stage-release.sh 9.8.7   # derivation regression
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
VERSION="${1:?usage: stage-release.sh <version>}"
OUT="$ROOT/dist/$VERSION"
[ ! -e "$OUT" ] || { echo "FAIL: destination already exists and is non-empty: $OUT" >&2; exit 1; }
STAGE_OUT="$(mktemp -d /tmp/nift-stage.XXXXXX)"
BUNDLE_STAGE=""; WHEEL_TMP=""; NPM_TMP=""
trap 'rm -rf "$STAGE_OUT" "$BUNDLE_STAGE" "$WHEEL_TMP" "$NPM_TMP"' EXIT
mkdir -p "$STAGE_OUT"

CLI_VER="$(./nift --version 2>&1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)"
[ "$VERSION" = "$CLI_VER" ] || { echo "VERSION=$VERSION != canonical CLI=$CLI_VER" >&2; exit 1; }
echo "staging release $VERSION -> $OUT"

ARCH="$(uname -m)"; [ "$ARCH" = "x86_64" ] && ARCH="x86_64"
OS="$(uname -s)"; [ "$OS" = "Linux" ] && OS="linux"; [ "$OS" = "Darwin" ] && OS="macos"
chk() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi; }

# 1. CLI binary
make -j2 nift >/dev/null 2>&1
cp nift "$STAGE_OUT/nift-$OS-$ARCH"
echo "built CLI: $OUT/nift-$OS-$ARCH"

# 2. Native embed bundle (per-target .pc stamped) + SHA256SUMS
make embed >/dev/null 2>&1
if [ "$OS" = "linux" ]; then
  PC_LIBS='-L${libdir} -Wl,-Bstatic -lnift_c -Wl,-Bdynamic -lstdc++ -lm -pthread'
elif [ "$OS" = "macos" ]; then
  PC_LIBS='-L${libdir} -lnift_c -lc++ -lm'
else
  PC_LIBS='-L${libdir} -lnift_c'
fi
BUNDLE_STAGE="$(mktemp -d /tmp/nift-bundle.XXXXXX)"
trap 'rm -rf "$BUNDLE_STAGE"' EXIT
mkdir -p "$BUNDLE_STAGE/include/nift" "$BUNDLE_STAGE/lib/pkgconfig" "$BUNDLE_STAGE/tools"
cp -r include/nift/. "$BUNDLE_STAGE/include/nift/"
cp libnift_c.a libnift_c.so "$BUNDLE_STAGE/lib/"
sed -e "s/__VERSION__/$VERSION/" -e "s|__LIBS__|$PC_LIBS|" packaging/nift.pc.in > "$BUNDLE_STAGE/lib/pkgconfig/nift.pc"
cp packaging/install-embed.sh "$BUNDLE_STAGE/install-embed.sh"
tar czf "$STAGE_OUT/nift-embed-$OS-$ARCH.tar.gz" -C "$BUNDLE_STAGE" include lib install-embed.sh
echo "built native bundle: $OUT/nift-embed-$OS-$ARCH.tar.gz"

# 3. Python: self-contained sdist + wheel built from that sdist in a clean dir
rm -rf bindings/python/native bindings/python/build bindings/python/nift.egg-info
mkdir -p bindings/python/native
cp -r src bindings/python/native/src
cp -r include bindings/python/native/include
cp -r minifypp bindings/python/native/minifypp
cp -r jsonic bindings/python/native/jsonic
( cd bindings/python && NIFT_VERSION="$VERSION" python3 setup.py sdist --dist-dir "$STAGE_OUT" >/dev/null 2>&1 )
SDIST="$(ls "$STAGE_OUT"/nift-$VERSION.tar.gz 2>/dev/null | head -1)"
[ -n "$SDIST" ] || { echo "sdist build failed" >&2; exit 1; }
# Build the wheel FROM the sdist in a clean temp dir (never the checkout).
WHEEL_TMP="$(mktemp -d /tmp/nift-wheelsrc.XXXXXX)"
trap 'rm -rf "$BUNDLE_STAGE" "$WHEEL_TMP"' EXIT
tar xzf "$SDIST" -C "$WHEEL_TMP"
( cd "$WHEEL_TMP"/nift-$VERSION && NIFT_VERSION="$VERSION" python3 -m pip wheel --no-deps --no-build-isolation -w "$STAGE_OUT" . >/dev/null 2>&1 )
echo "built sdist + wheel: $(ls "$OUT" | grep -E 'nift-.*\.(whl|tar.gz)$' | tr '\n' ' ')"

# 4. npm (stamped package.json in a temp tree). The addon is built in canonical
#    (the temp tree lacks the native sources), then the whole binding incl. the
#    built addon is copied and the version stamped.
NPM_TMP="$(mktemp -d /tmp/nift-npm.XXXXXX)"
trap 'rm -rf "$BUNDLE_STAGE" "$WHEEL_TMP" "$NPM_TMP"' EXIT
make node-binding >/dev/null 2>&1
cp -r bindings/node/. "$NPM_TMP/"
python3 - "$NPM_TMP/package.json" "$VERSION" <<'PY'
import json, sys
p = json.load(open(sys.argv[1]))
p["version"] = sys.argv[2]
p["files"] = ["lib/", "build/nift_node.node", "README.md"]
json.dump(p, open(sys.argv[1], "w"), indent=2)
PY
( cd "$NPM_TMP" && npm pack --pack-destination "$STAGE_OUT" >/dev/null 2>&1 )
echo "built npm: $(ls "$OUT"/*.tgz 2>/dev/null | tr '\n' ' ')"

# 5. NuGet (version via -p:Version)
( cd bindings/csharp/src/Nift && dotnet pack -v q --nologo -p:Version="$VERSION" -o "$STAGE_OUT" >/dev/null 2>&1 )
echo "built nuget: $(ls "$OUT"/*.nupkg 2>/dev/null | tr '\n' ' ')"

# 6. SHA256SUMS over the final immutable artifact bytes
( cd "$STAGE_OUT" && for f in *; do [ -f "$f" ] && [ "$f" != "SHA256SUMS" ] && chk "$f"; done | sort > SHA256SUMS )
# Validate the exact expected file set.
EXPECTED="nift-$OS-$ARCH nift-embed-$OS-$ARCH.tar.gz nift-$VERSION.tar.gz nift-$VERSION-cp*-*.whl nift-$VERSION.tgz Nift.$VERSION.nupkg"
for pat in $EXPECTED; do
  compgen -G "$STAGE_OUT/$pat" >/dev/null || { echo "FAIL: expected artifact missing: $pat" >&2; exit 1; }
done
mkdir -p "$ROOT/dist"
mv "$STAGE_OUT" "$OUT"
echo "--- $OUT SHA256SUMS ---"
cat "$OUT/SHA256SUMS"
echo "staged release $VERSION complete"