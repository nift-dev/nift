#!/usr/bin/env bash
set -euo pipefail

NIFT=${NIFT:-./nift}
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT

make_package() {
    local root=$1 name=$2
    mkdir -p "$root/src"
    printf '{"name":"%s","version":"0.1.0","entry":"src/main.f","description":"fixture"}\n' "$name" > "$root/manifest.json"
    printf 'value := 1\nexport(value)\n' > "$root/src/main.f"
}

make_package "$t/demo" demo
demo_source="$t/demo"
if [ -n "${WINDIR:-}" ] && command -v cygpath >/dev/null 2>&1; then
    demo_source=$(cygpath -m "$demo_source")
fi

# Existing malformed project metadata must never be replaced by `add`.
mkdir -p "$t/malformed-site"
printf '{broken\n' > "$t/malformed-site/manifest.json"
before=$(cksum "$t/malformed-site/manifest.json")
if (cd "$t/malformed-site" && "$NIFT_ABS" add ../demo >/dev/null 2>&1); then
    echo 'add unexpectedly replaced a malformed project manifest' >&2
    exit 1
fi
[[ "$(cksum "$t/malformed-site/manifest.json")" == "$before" ]]

# Installable package identity is strict and shared by add/import.
mkdir -p "$t/missing-version/src" "$t/bad-entry/src" "$t/unknown-field/src"
printf '{"name":"missing-version","entry":"src/main.f"}\n' > "$t/missing-version/manifest.json"
printf 'value := 1\nexport(value)\n' > "$t/missing-version/src/main.f"
printf '{"name":"bad-entry","version":"0.1.0","entry":"src/main.txt"}\n' > "$t/bad-entry/manifest.json"
printf 'value := 1\nexport(value)\n' > "$t/bad-entry/src/main.txt"
printf '{"name":"unknown-field","version":"0.1.0","entry":"src/main.f","mystery":true}\n' > "$t/unknown-field/manifest.json"
printf 'value := 1\nexport(value)\n' > "$t/unknown-field/src/main.f"
mkdir -p "$t/backslash-entry/src"
cat > "$t/backslash-entry/manifest.json" <<'JSON'
{"name":"backslash-entry","version":"0.1.0","entry":"src\\main.f"}
JSON
printf 'value := 1\nexport(value)\n' > "$t/backslash-entry/src/main.f"
for package in missing-version bad-entry unknown-field backslash-entry; do
    mkdir -p "$t/$package-site"
    if (cd "$t/$package-site" && "$NIFT_ABS" add "../$package" >/dev/null 2>&1); then
        echo "invalid package manifest unexpectedly accepted: $package" >&2
        exit 1
    fi
done

# Dependency objects require explicit non-empty source/ref fields.
mkdir -p "$t/bad-dependency-site"
cat > "$t/bad-dependency-site/manifest.json" <<EOF
{"dependencies":{"demo":{"source":"$demo_source"}}}
EOF
bad_dependency=$(cd "$t/bad-dependency-site" && "$NIFT_ABS" install 2>&1 || true)
grep -q 'dependency.*ref' <<<"$bad_dependency"

# A malformed or inconsistent existing lock is an error, never an unlocked
# fallback. The live package slot remains untouched.
mkdir -p "$t/lock-site/.nift/packages/demo"
cat > "$t/lock-site/manifest.json" <<EOF
{"dependencies":{"demo":{"source":"$demo_source","ref":"local"}}}
EOF
printf 'live\n' > "$t/lock-site/.nift/packages/demo/sentinel"
printf '{broken\n' > "$t/lock-site/.nift/packages.lock.json"
if (cd "$t/lock-site" && "$NIFT_ABS" install >/dev/null 2>&1); then
    echo 'malformed package lock unexpectedly ignored' >&2
    exit 1
fi
[[ "$(cat "$t/lock-site/.nift/packages/demo/sentinel")" == live ]]
cat > "$t/lock-site/.nift/packages.lock.json" <<EOF
{"demo":{"source":"$t/other","requested":"local","commit":"local"}}
EOF
if (cd "$t/lock-site" && "$NIFT_ABS" install >/dev/null 2>&1); then
    echo 'inconsistent package lock unexpectedly accepted' >&2
    exit 1
fi
[[ "$(cat "$t/lock-site/.nift/packages/demo/sentinel")" == live ]]

# Successful writes have deterministic field and package ordering.
make_package "$t/zeta" zeta
make_package "$t/alpha" alpha
mkdir -p "$t/order-site"
(cd "$t/order-site" && "$NIFT_ABS" add ../zeta >/dev/null)
(cd "$t/order-site" && "$NIFT_ABS" add ../alpha >/dev/null)
alpha_manifest=$(grep -n '"alpha"' "$t/order-site/manifest.json" | cut -d: -f1)
zeta_manifest=$(grep -n '"zeta"' "$t/order-site/manifest.json" | cut -d: -f1)
alpha_lock=$(grep -n '"alpha"' "$t/order-site/.nift/packages.lock.json" | cut -d: -f1)
zeta_lock=$(grep -n '"zeta"' "$t/order-site/.nift/packages.lock.json" | cut -d: -f1)
(( alpha_manifest < zeta_manifest ))
(( alpha_lock < zeta_lock ))

# A hand-written valid local dependency receives a complete lock on install.
mkdir -p "$t/install-site"
cat > "$t/install-site/manifest.json" <<EOF
{"dependencies":{"demo":{"source":"$demo_source","ref":"local"}}}
EOF
(cd "$t/install-site" && "$NIFT_ABS" install >/dev/null)
grep -q '"requested": "local"' "$t/install-site/.nift/packages.lock.json"
grep -q '"commit": "local"' "$t/install-site/.nift/packages.lock.json"

echo 'PASS package metadata and lock schema'
