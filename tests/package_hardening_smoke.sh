#!/usr/bin/env bash
# Package ecosystem hardening: package paths stay contained, malformed package
# identities fail before mutation, and imports preserve package isolation.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
mkdir -p "$t/site/.nift"
# 1) manifest entry must stay inside the package directory
mkdir -p "$t/evil"
printf '{"name":"evil","version":"0.1.0","entry":"../../escape.f"}\n' > "$t/evil/manifest.json"
printf 'print("pwned")\n' > "$t/escape.f"
evil=$(cd "$t/site" && "$NIFT_ABS" add ../evil 2>&1 || true)
grep -q 'manifest entry must be a contained relative .f path' <<<"$evil"
# 2) adding a dependency name that already exists is refused
mkdir -p "$t/pkg/src"
printf '{"name":"demo","version":"0.1.0","entry":"src/main.f"}\n' > "$t/pkg/manifest.json"
printf 'v := 1\nexport(v)\n' > "$t/pkg/src/main.f"
(cd "$t/site" && "$NIFT_ABS" add ../pkg >/dev/null 2>&1)
dup=$(cd "$t/site" && "$NIFT_ABS" add ../pkg 2>&1 || true)
grep -q "already a dependency" <<<"$dup"
# 3) destructive package names and manifest dependency keys must never escape
#    .nift/packages or mutate undeclared paths.
mkdir -p "$t/victim" "$t/site/victim"
printf 'outside\n' > "$t/victim/sentinel"
printf 'inside\n' > "$t/site/victim/sentinel"
for name in '../../victim' '../victim' '/tmp/nift-package-victim' 'bad/name' 'bad\name' '..' '.staging-evil' 'Upper' 'dotted.name' 'con'; do
    if (cd "$t/site" && "$NIFT_ABS" remove "$name" >/dev/null 2>&1); then
        echo "invalid package removal unexpectedly succeeded: $name" >&2
        exit 1
    fi
done
[[ "$(cat "$t/victim/sentinel")" == outside ]]
[[ "$(cat "$t/site/victim/sentinel")" == inside ]]

mkdir -p "$t/root-link-site/.nift" "$t/external-store/demo"
printf 'external\n' > "$t/external-store/demo/sentinel"
printf '{"dependencies":{"demo":{"source":"unused","ref":"local"}}}\n' > "$t/root-link-site/manifest.json"
if ln -s "$t/external-store" "$t/root-link-site/.nift/packages" 2>/dev/null; then
    if (cd "$t/root-link-site" && "$NIFT_ABS" remove demo >/dev/null 2>&1); then
        echo 'symlinked external package store unexpectedly accepted' >&2
        exit 1
    fi
    [[ "$(cat "$t/external-store/demo/sentinel")" == external ]]
fi

mkdir -p "$t/inner-link-site/.nift" "$t/inner-link-site/victim/demo"
printf 'internal\n' > "$t/inner-link-site/victim/demo/sentinel"
printf '{"dependencies":{"demo":{"source":"unused","ref":"local"}}}\n' > "$t/inner-link-site/manifest.json"
if ln -s ../victim "$t/inner-link-site/.nift/packages" 2>/dev/null; then
    if (cd "$t/inner-link-site" && "$NIFT_ABS" remove demo >/dev/null 2>&1); then
        echo 'redirected in-project package store unexpectedly accepted' >&2
        exit 1
    fi
    [[ "$(cat "$t/inner-link-site/victim/demo/sentinel")" == internal ]]
fi

if (cd "$t/site" && "$NIFT_ABS" remove missing >/dev/null 2>&1); then
    echo 'undeclared package removal unexpectedly succeeded' >&2
    exit 1
fi
cat > "$t/site/manifest.json" <<EOF
{"dependencies":{"../../victim":{"source":"$t/pkg","ref":"local"}}}
EOF
if (cd "$t/site" && "$NIFT_ABS" install >/dev/null 2>&1); then
    echo 'invalid dependency key unexpectedly installed' >&2
    exit 1
fi
[[ "$(cat "$t/victim/sentinel")" == outside ]]
[[ "$(cat "$t/site/victim/sentinel")" == inside ]]

# Restore the valid direct dependency for later checks.
cat > "$t/site/manifest.json" <<EOF
{"dependencies":{"demo":{"source":"$t/pkg","ref":"local"}}}
EOF
if (cd "$t/site" && "$NIFT_ABS" update missing >/dev/null 2>&1); then
    echo 'undeclared package update unexpectedly succeeded' >&2
    exit 1
fi

# 4) imports must revalidate the installed manifest identity and entry path.
mkdir -p "$t/site/.nift/packages/changed/src"
printf '{"name":"other","version":"0.1.0","entry":"src/main.f"}\n' > "$t/site/.nift/packages/changed/manifest.json"
printf 'value := 1\nexport(value)\n' > "$t/site/.nift/packages/changed/src/main.f"
printf 'import("changed")\n' > "$t/site/changed.f"
if (cd "$t/site" && "$NIFT_ABS" changed.f >/dev/null 2>&1); then
    echo 'mismatched installed package name unexpectedly imported' >&2
    exit 1
fi

mkdir -p "$t/site/.nift/packages/escaped"
printf '{"name":"escaped","version":"0.1.0","entry":"../../../../escape.f"}\n' > "$t/site/.nift/packages/escaped/manifest.json"
printf 'import("escaped")\n' > "$t/site/escaped.f"
if (cd "$t/site" && "$NIFT_ABS" escaped.f >/dev/null 2>&1); then
    echo 'escaped installed package entry unexpectedly imported' >&2
    exit 1
fi

mkdir -p "$t/site/.nift/packages/linked/src"
printf '{"name":"linked","version":"0.1.0","entry":"src/main.f"}\n' > "$t/site/.nift/packages/linked/manifest.json"
if ln -s "$t/escape.f" "$t/site/.nift/packages/linked/src/main.f" 2>/dev/null; then
    printf 'import("linked")\n' > "$t/site/linked.f"
    if (cd "$t/site" && "$NIFT_ABS" linked.f >/dev/null 2>&1); then
        echo 'symlink-escaped installed package entry unexpectedly imported' >&2
        exit 1
    fi
fi

# 5) import isolation: exported functions keep private context; private
#    bindings never leak into the importer.
mkdir -p "$t/site/.nift/packages/iso/src"
printf '{"name":"iso","version":"0.1.0","entry":"src/main.f"}\n' > "$t/site/.nift/packages/iso/manifest.json"
printf 'secret_helper := "hidden"\n@fn(public_fn(x)){ return x + 1 }\nexport(public_fn)\n' > "$t/site/.nift/packages/iso/src/main.f"
printf '@import("iso")\nprint(public_fn(1))\n' > "$t/site/t.f"
[ "$(cd "$t/site" && "$NIFT_ABS" t.f)" = "2" ] || exit 1
printf '@import("iso")\nprint(secret_helper)\n' > "$t/site/t2.f"
if (cd "$t/site" && "$NIFT_ABS" t2.f >/dev/null 2>&1); then echo "private binding leaked into importer" >&2; exit 1; fi
echo 'PASS v4.4 package hardening'
