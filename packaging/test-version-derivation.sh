#!/usr/bin/env bash
# Durable synchronized-version derivation regression: stages METADATA and BUILDS
# the actual artifacts (wheel, sdist, npm tarball, NuGet nupkg, nift.pc) with a
# deliberately non-current test version (9.8.7) and asserts every consumer-facing
# artifact reports exactly that value in its filename and metadata. This
# exercises the same stamping inputs as production staging WITHOUT touching the
# production release-validation path (stage-release.sh always validates against
# the CLI; this test does not).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
TV="9.8.7"
WORK="$(mktemp -d /tmp/nift-verderiv.XXXXXX)"
trap 'rm -rf "$WORK" bindings/python/native bindings/python/nift.egg-info' EXIT

fail() { echo "FAIL: $1" >&2; exit 1; }

# 1. nift.pc
sed -e "s/__VERSION__/$TV/" -e 's|__LIBS__|-L${libdir} -Wl,-Bstatic -lnift_c -Wl,-Bdynamic -lstdc++ -lm -pthread|' \
    packaging/nift.pc.in > "$WORK/nift.pc"
grep -q "^Version: $TV\$" "$WORK/nift.pc" || fail "nift.pc Version != $TV"

# 2. npm tarball: stamp package.json, pack, and inspect filename + metadata.
# The addon is built in canonical (the temp copy lacks native sources).
make node-binding >/dev/null 2>&1
NPMTMP="$WORK/npm"
cp -r bindings/node "$NPMTMP"
python3 - "$NPMTMP/package.json" "$TV" <<'PY' || fail "npm stamp"
import json, sys
p = json.load(open(sys.argv[1]))
p["version"] = sys.argv[2]
p["files"] = ["lib/", "build/nift_node.node", "README.md"]
json.dump(p, open(sys.argv[1], "w"), indent=2)
PY
( cd "$NPMTMP" && npm pack --pack-destination "$WORK" >/dev/null 2>&1 )
[ -f "$WORK/nift-$TV.tgz" ] || fail "npm tarball filename lacks $TV"
python3 - "$WORK/nift-$TV.tgz" "$TV" <<'PY' || fail "npm version"
import sys, tarfile, json
tv = sys.argv[2]
t = tarfile.open(sys.argv[1]); p = t.extractfile("package/package.json"); 
if json.load(p)["version"] != tv: sys.exit(1)
PY

# 3. NuGet nupkg
( cd bindings/csharp/src/Nift && dotnet pack -v q --nologo -p:Version="$TV" -o "$WORK" >/dev/null 2>&1 )
python3 - "$WORK" "$TV" <<'PY' || fail "nuget version"
import sys, zipfile, re, glob
work, tv = sys.argv[1], sys.argv[2]
nupkg = glob.glob(work + "/Nift." + tv + ".nupkg")
if not nupkg: sys.exit(1)
z = zipfile.ZipFile(nupkg[0])
nuspec = z.read([n for n in z.namelist() if n.endswith(".nuspec")][0]).decode()
if not re.search(r"<version>" + tv + r"</version>", nuspec, re.I): sys.exit(1)
PY

# 4. Python: build the self-contained sdist and the wheel from that sdist in a
#    clean dir; inspect sdist filename, wheel filename, and wheel METADATA.
rm -rf bindings/python/native && mkdir -p bindings/python/native
cp -r src bindings/python/native/src && cp -r include bindings/python/native/include
cp -r minifypp bindings/python/native/minifypp && cp -r markuppp bindings/python/native/markuppp
cp -r jsonic bindings/python/native/jsonic
( cd bindings/python && NIFT_VERSION="$TV" python3 setup.py sdist --dist-dir "$WORK" >/dev/null 2>&1 )
[ -f "$WORK/nift-$TV.tar.gz" ] || fail "sdist filename lacks $TV"
SDIST_DIR="$WORK/pysrc" && mkdir -p "$SDIST_DIR" && tar xzf "$WORK/nift-$TV.tar.gz" -C "$SDIST_DIR"
( cd "$SDIST_DIR"/nift-$TV && NIFT_VERSION="$TV" python3 -m pip wheel --no-deps --no-build-isolation -w "$WORK" . >/dev/null 2>&1 )
WHEEL="$(ls "$WORK"/nift-$TV-cp*-cp*-*.whl 2>/dev/null | head -1)"
[ -n "$WHEEL" ] || fail "wheel filename lacks $TV / platform tag"
python3 - "$WHEEL" "$TV" <<'PY' || fail "wheel METADATA"
import sys, zipfile
tv = sys.argv[2]
z = zipfile.ZipFile(sys.argv[1])
meta = [n for n in z.namelist() if n.endswith("METADATA")][0]
ver = [l for l in z.read(meta).decode().splitlines() if l.startswith("Version:")]
if not ver or ver[0] != "Version: " + tv: sys.exit(1)
PY

echo "VERSION_DERIVATION_PASS: every built artifact reports $TV (nift.pc, npm tarball+package.json, NuGet nuspec, sdist, wheel filename+METADATA); production stage-release always validates against the CLI"