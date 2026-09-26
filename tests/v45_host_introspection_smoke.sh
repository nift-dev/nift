#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) BIN="$NIFT";; *) BIN="$(pwd)/$NIFT";; esac

out=$(NIFT_CP8_ORIGINAL=present "$BIN" -e '
print(getenv("NIFT_CP8_ORIGINAL"))
print(env().get("NIFT_CP8_ORIGINAL"))
setenv("NIFT_CP8_MUTATED", "yes")
print(env().get("NIFT_CP8_MUTATED"))
unsetenv("NIFT_CP8_MUTATED")
print(getenv("NIFT_CP8_MUTATED"))
print(os())
print(arch())
')
[ "$(sed -n '1p' <<<"$out")" = present ]
[ "$(sed -n '2p' <<<"$out")" = present ]
[ "$(sed -n '3p' <<<"$out")" = yes ]
[ "$(sed -n '4p' <<<"$out")" = null ]
case "$(sed -n '5p' <<<"$out")" in linux|macos|windows) ;; *) echo "unexpected os(): $out" >&2; exit 1;; esac
case "$(sed -n '6p' <<<"$out")" in x86_64|x86|arm64|arm|wasm32|wasm64|unknown) ;; *) echo "unexpected arch(): $out" >&2; exit 1;; esac
if "$BIN" -e 'env(1)' >/dev/null 2>&1; then echo 'env accepted argument' >&2; exit 1; fi
if "$BIN" -e 'os(1)' >/dev/null 2>&1; then echo 'os accepted argument' >&2; exit 1; fi
if "$BIN" -e 'arch(1)' >/dev/null 2>&1; then echo 'arch accepted argument' >&2; exit 1; fi
echo 'PASS v4.5 host introspection'
