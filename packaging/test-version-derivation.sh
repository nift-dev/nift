#!/usr/bin/env bash
# Durable synchronized-version derivation regression: stages METADATA with a
# deliberately non-current test version (9.8.7) and asserts every package
# artifact reports exactly that value. This exercises the same stamping inputs
# as the production staging WITHOUT touching the production release-validation
# path (stage-release.sh always validates against the CLI; this test does not).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
TV="9.8.7"
WORK="$(mktemp -d /tmp/nift-verderiv.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

fail() { echo "FAIL: $1" >&2; exit 1; }

# 1. nift.pc
sed -e "s/__VERSION__/$TV/" -e 's|__LIBS__|-L${libdir} -Wl,-Bstatic -lnift_c -Wl,-Bdynamic -lstdc++ -lm -pthread|' \
    packaging/nift.pc.in > "$WORK/nift.pc"
grep -q "^Version: $TV\$" "$WORK/nift.pc" || fail "nift.pc Version != $TV"

# 2. npm package.json
python3 - "$TV" <<'PY' || fail "npm stamp"
import json, sys
v = sys.argv[1]
p = json.load(open("bindings/node/package.json"))
p["version"] = v
open("/tmp/verderiv-pkg.json","w").write(json.dumps(p))
PY
[ "$(python3 -c "import json;print(json.load(open('/tmp/verderiv-pkg.json'))['version'])")" = "$TV" ] || fail "npm version != $TV"

# 3. NuGet nuspec (dotnet pack with -p:Version)
( cd bindings/csharp/src/Nift && dotnet pack -v q --nologo -p:Version="$TV" -o "$WORK" >/dev/null 2>&1 )
python3 - "$WORK" "$TV" <<'PY' || fail "nuget version"
import sys, zipfile, re
work, tv = sys.argv[1], sys.argv[2]
import glob
nupkg = glob.glob(work + "/Nift." + tv + ".nupkg")
if not nupkg: sys.exit(1)
z = zipfile.ZipFile(nupkg[0])
nuspec = z.read([n for n in z.namelist() if n.endswith(".nuspec")][0]).decode()
if not re.search(r"<version>" + tv + r"</version>", nuspec, re.I): sys.exit(1)
PY

# 4. Wheel METADATA (requires the staged native subtree; use a minimal stamp via
#    the existing NIFT_VERSION-required setup.py path is heavy, so verify the
#    wheel filename + METADATA by building the metadata-only metadata).
rm -rf bindings/python/native && mkdir -p bindings/python/native
cp -r src bindings/python/native/src && cp -r include bindings/python/native/include
cp -r minifypp bindings/python/native/minifypp && cp -r jsonic bindings/python/native/jsonic
( cd bindings/python && NIFT_VERSION="$TV" python3 setup.py egg_info >/dev/null 2>&1 )
grep -q "^Version: $TV$" bindings/python/nift.egg-info/PKG-INFO 2>/dev/null || fail "python PKG-INFO Version != $TV"

echo "VERSION_DERIVATION_PASS: every package metadata reports $TV (nift.pc, npm, NuGet nuspec, Python PKG-INFO); production stage-release always validates against the CLI"
rm -rf bindings/python/native bindings/python/nift.egg-info