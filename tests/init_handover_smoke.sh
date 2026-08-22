#!/usr/bin/env bash
# `nift init --handover` writes a project-root HANDOVER.md byte-for-byte
# identical to the canonical Nift handover. The equivalence chain is:
#   tests/fixtures/HANDOVER.md  ==  src/handover_content.h (embedded bytes)
#                            ==  generated HANDOVER.md
# and the gated live check proves fixture == https://nift.dev/HANDOVER.md.
# Plain `nift init` must not create the file, the handover must never land
# under the output directory, and all compatible named init options are
# order-independent.
set -u
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
FIXTURE="$(pwd)/tests/fixtures/HANDOVER.md"
CANONICAL_SHA="$(sha256sum "$FIXTURE" | cut -d' ' -f1)"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-init-handover.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

fail() { echo "init-handover FAIL: $*" >&2; exit 1; }

check_no_handover() {
    local p="$1"
    [ ! -f "$p/HANDOVER.md" ] || fail "plain init unexpectedly created $p/HANDOVER.md"
    [ ! -f "$p/public/HANDOVER.md" ] || fail "handover leaked into $p/public/HANDOVER.md"
}

check_handover() {
    local p="$1"
    [ -f "$p/HANDOVER.md" ] || fail "expected $p/HANDOVER.md not created"
    [ -f "$p/public/HANDOVER.md" ] && fail "handover must not be written under public/"
    local got="$(sha256sum "$p/HANDOVER.md" | cut -d' ' -f1)"
    [ "$got" = "$CANONICAL_SHA" ] || fail "HANDOVER.md not byte-identical to canonical (got $got, want $CANONICAL_SHA)"
}

# Embedded literal in src/handover_content.h must decode to exactly the fixture,
# completing embedded == fixture == generated.
check_embedded_matches_fixture() {
    python3 - "$FIXTURE" <<'PY' || fail "src/handover_content.h does not decode to tests/fixtures/HANDOVER.md"
import re, sys
from pathlib import Path
fixture = Path(sys.argv[1]).read_bytes()
h = Path('src/handover_content.h').read_text()
m = re.search(r'constexpr const char\* handover_content =\n    "(.*)";', h, flags=re.S)
assert m, "could not extract handover_content literal"
lit = m.group(1)
out = bytearray()
i = 0
while i < len(lit):
    c = lit[i]
    if c == '\\' and i + 1 < len(lit):
        nxt = lit[i+1]
        if nxt in '\\"': out.append(ord(nxt)); i += 2; continue
        if nxt == 'n': out.append(0x0a); i += 2; continue
        if nxt in '01234567':
            j = i + 1
            while j < min(i + 4, len(lit)) and lit[j] in '01234567': j += 1
            out.append(int(lit[i+1:j], 8)); i = j; continue
        raise SystemExit(f"unhandled escape at {i}")
    out.append(ord(c)); i += 1
assert bytes(out) == fixture, f"embedded literal ({len(out)} bytes) != fixture ({len(fixture)} bytes)"
PY
}

check_embedded_matches_fixture

# Inverse: plain init must not create the handover.
P="$TMP/plain"; mkdir -p "$P"
(cd "$P" && "$NIFT_BIN" init >/dev/null 2>&1) || fail "plain init failed"
check_no_handover "$P"

# Primary: init --handover writes a byte-identical root HANDOVER.md.
P="$TMP/handover"; mkdir -p "$P"
(cd "$P" && "$NIFT_BIN" init --handover >/dev/null 2>&1) || fail "init --handover failed"
check_handover "$P"

# Order independence: every compatible named init option works in any order,
# and the handover is identical regardless of spelling.
for combo in \
    "--handover --target=vercel" "--target=vercel --handover" \
    "--handover --target=netlify" "--target=netlify --handover" \
    "--handover --target=azure" "--target=azure --handover" \
    "--handover --ext=.php" "--ext=.php --handover" \
    "--ext=.html --target=vercel" "--target=vercel --ext=.html"; do
    P="$TMP/$(echo "$combo" | tr ' =.' '---')"; mkdir -p "$P"
    (cd "$P" && $NIFT_BIN init $combo >/dev/null 2>&1) || fail "init $combo failed"
    if [[ "$combo" == *--handover* ]]; then
        check_handover "$P"
    else
        check_no_handover "$P"
    fi
done

# The handover is created as part of project creation, before the initial
# build: even when that first build fails, HANDOVER.md must already exist.
P="$TMP/failbuild"; mkdir -p "$P/public"
chmod 555 "$P/public"
(cd "$P" && "$NIFT_BIN" init --handover >/dev/null 2>&1)
chmod 755 "$P/public"
[ -f "$P/HANDOVER.md" ] || fail "HANDOVER.md missing when the initial build fails (must be written before the build)"

echo "init-handover smoke test passed: embedded == fixture == generated; plain init untouched; options order-independent"
