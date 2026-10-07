#!/usr/bin/env bash
# `nift init --migration` creates a normal Nift project plus MIGRATION.md,
# HANDOVER.md, AGENTS.md (with Nift's managed migration block) and
# investigation/. The canonical files are byte-identical to the fixtures, the
# existing-file policies (error/keep/append/replace) are deterministic, the
# AGENTS block is created/augmented/replaced safely and idempotently, and plain
# init is unchanged.
set -u
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
ROOT="$(pwd)"
MIG_FIXTURE="$ROOT/tests/fixtures/MIGRATION.md"
HAND_FIXTURE="$ROOT/tests/fixtures/HANDOVER.md"
MIG_SHA="$(sha256sum "$MIG_FIXTURE" | cut -d' ' -f1)"
HAND_SHA="$(sha256sum "$HAND_FIXTURE" | cut -d' ' -f1)"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-init-migration.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

fail() { echo "init-migration FAIL: $*" >&2; exit 1; }
blocks() { grep -c "$1" "$2" 2>/dev/null || echo 0; }

# Embedded literals must decode to the fixtures.
python3 - "$MIG_FIXTURE" "$HAND_FIXTURE" <<'PY' || fail "embedded canonical literals mismatch"
import re, sys
from pathlib import Path
def decode(path, symbol):
    fixture = Path(sys.argv[1 if symbol == "migration_content" else 2]).read_bytes()
    h = Path(f"src/{symbol}.h").read_text()
    m = re.search(r'constexpr const char\* ' + symbol + r' =\n    "(.*)";', h, flags=re.S)
    assert m
    lit = m.group(1)
    out = bytearray(); i = 0
    while i < len(lit):
        c = lit[i]
        if c == '\\' and i + 1 < len(lit):
            nxt = lit[i+1]
            if nxt in '\\"': out.append(ord(nxt)); i += 2; continue
            if nxt == 'n': out.append(0x0a); i += 2; continue
            if nxt in '01234567':
                j = i+1
                while j < min(i+4, len(lit)) and lit[j] in '01234567': j += 1
                out.append(int(lit[i+1:j], 8)); i = j; continue
            raise SystemExit("unhandled escape")
        out.append(ord(c)); i += 1
    assert bytes(out) == fixture, f"{symbol} does not decode to fixture"
decode("MIGRATION.md", "migration_content")
decode("HANDOVER.md", "handover_content")
PY

P="$TMP/plain"; mkdir -p "$P"
(cd "$P" && "$NIFT_BIN" init >/dev/null 2>&1) || fail "plain init failed"
[ -f "$P/MIGRATION.md" ] && fail "plain init created MIGRATION.md"
[ -f "$P/AGENTS.md" ] && fail "plain init created AGENTS.md"

# Fresh migration project.
P="$TMP/fresh"; mkdir -p "$P"
(cd "$P" && "$NIFT_BIN" init --migration >/dev/null 2>&1) || fail "init --migration failed"
for f in MIGRATION.md HANDOVER.md AGENTS.md; do [ -f "$P/$f" ] || fail "expected $f"; done
[ -d "$P/investigation" ] || fail "investigation/ missing"
[ -f "$P/investigation/README.md" ] || fail "investigation/README.md missing"
[ "$(sha256sum "$P/MIGRATION.md" | cut -d' ' -f1)" = "$MIG_SHA" ] || fail "MIGRATION.md not byte-identical to fixture"
[ "$(sha256sum "$P/HANDOVER.md" | cut -d' ' -f1)" = "$HAND_SHA" ] || fail "HANDOVER.md not byte-identical to fixture"
[ "$(blocks nift:migration:start "$P/AGENTS.md")" = "1" ] || fail "AGENTS.md must contain exactly one managed block"

# --handover stays single-file and is unchanged.
P="$TMP/hand"; mkdir -p "$P"
(cd "$P" && "$NIFT_BIN" init --handover >/dev/null 2>&1) || fail "init --handover failed"
[ "$(sha256sum "$P/HANDOVER.md" | cut -d' ' -f1)" = "$HAND_SHA" ] || fail "--handover HANDOVER.md drifted"
[ -f "$P/MIGRATION.md" ] || [ -f "$P/AGENTS.md" ] && fail "--handover unexpectedly created migration files"

# Default policy: error, before any scaffold, lists conflicts, preserves files.
for existing in MIGRATION.md HANDOVER.md; do
  Q="$TMP/conf-$existing"; mkdir -p "$Q"; printf 'keep 中\n' > "$Q/$existing"
  if (cd "$Q" && "$NIFT_BIN" init --migration >/dev/null 2>err); then fail "conflict accepted for $existing"; fi
  grep -q "cannot initialise migration project" "$Q/err" || fail "no conflict diagnostic for $existing"
  grep -q "$existing already exists" "$Q/err" || fail "conflicting file not listed for $existing"
  [ -f "$Q/.nift/config.json" ] && fail "partial project left for $existing"
  [ "$(cat "$Q/$existing")" = "keep 中" ] || fail "$existing was modified"
done

# keep: preserve existence, create only missing files.
Q="$TMP/keep"; mkdir -p "$Q"; printf 'mine\n' > "$Q/MIGRATION.md"
(cd "$Q" && "$NIFT_BIN" init --migration --migration-existing=keep >/dev/null 2>&1) || fail "keep failed"
[ "$(cat "$Q/MIGRATION.md")" = "mine" ] || fail "keep modified MIGRATION.md"
[ -f "$Q/HANDOVER.md" ] || fail "keep did not create missing HANDOVER.md"

# replace: recreates canonical files.
Q="$TMP/replace"; mkdir -p "$Q"; printf 'old\n' > "$Q/MIGRATION.md"; printf 'old\n' > "$Q/HANDOVER.md"
(cd "$Q" && "$NIFT_BIN" init --migration --migration-existing=replace >/dev/null 2>&1) || fail "replace failed"
[ "$(sha256sum "$Q/MIGRATION.md" | cut -d' ' -f1)" = "$MIG_SHA" ] || fail "replace MIGRATION.md not canonical"
[ "$(sha256sum "$Q/HANDOVER.md" | cut -d' ' -f1)" = "$HAND_SHA" ] || fail "replace HANDOVER.md not canonical"

# append: existing content preserved, guidance added once, rerun-idempotent.
Q="$TMP/append"; mkdir -p "$Q"; printf 'my doc 中\n' > "$Q/MIGRATION.md"
(cd "$Q" && "$NIFT_BIN" init --migration --migration-existing=append >/dev/null 2>&1) || fail "append failed"
grep -qx 'my doc 中' "$Q/MIGRATION.md" || fail "append lost user content"
[ "$(blocks nift:migration-template:start "$Q/MIGRATION.md")" = "1" ] || fail "append did not add exactly one block"
R="$TMP/append2"; mkdir -p "$R"; cp "$Q/MIGRATION.md" "$R/MIGRATION.md"
(cd "$R" && "$NIFT_BIN" init --migration --migration-existing=append >/dev/null 2>&1) || fail "append rerun failed"
[ "$(blocks nift:migration-template:start "$R/MIGRATION.md")" = "1" ] || fail "append duplicated the block on rerun"
# append malformed marker: fail safely, file unchanged.
S="$TMP/appendbad"; mkdir -p "$S"; printf 'x\n<!-- nift:migration-template:start -->\n' > "$S/MIGRATION.md"
if (cd "$S" && "$NIFT_BIN" init --migration --migration-existing=append >/dev/null 2>err); then fail "append accepted malformed marker"; fi
[ "$(cat "$S/MIGRATION.md")" = $'x\n<!-- nift:migration-template:start -->' ] || fail "append modified malformed file"

# AGENTS.md: absent, unrelated-only, valid block (replace body, preserve rest).
Q="$TMP/ag-none"; mkdir -p "$Q"
(cd "$Q" && "$NIFT_BIN" init --migration >/dev/null 2>&1) || fail "AGENTS create failed"
[ "$(blocks nift:migration:start "$Q/AGENTS.md")" = "1" ] || fail "AGENTS block not created"

Q="$TMP/ag-existing"; mkdir -p "$Q"; printf '# my agents 中\n\ninstructions...\n' > "$Q/AGENTS.md"
(cd "$Q" && "$NIFT_BIN" init --migration >/dev/null 2>&1) || fail "AGENTS augment failed"
grep -q 'my agents 中' "$Q/AGENTS.md" || fail "AGENTS unrelated content lost"
[ "$(blocks nift:migration:start "$Q/AGENTS.md")" = "1" ] || fail "AGENTS block not appended exactly once"

Q="$TMP/ag-update"; mkdir -p "$Q"; printf 'head\n\n<!-- nift:migration:start -->\n## old\nold text\n<!-- nift:migration:end -->\n\ntail 中\n' > "$Q/AGENTS.md"
(cd "$Q" && "$NIFT_BIN" init --migration >/dev/null 2>&1) || fail "AGENTS replace failed"
grep -q 'head' "$Q/AGENTS.md" && grep -q 'tail 中' "$Q/AGENTS.md" || fail "AGENTS unrelated content lost on replace"
[ "$(blocks nift:migration:start "$Q/AGENTS.md")" = "1" ] || fail "AGENTS block duplicated"
grep -q '## old' "$Q/AGENTS.md" && fail "AGENTS stale block body not replaced"

# AGENTS malformed states fail safely for every policy, preflight leaves no project.
for marker in start end; do
  Q="$TMP/ag-$marker"; mkdir -p "$Q"
  if [ "$marker" = start ]; then printf 'x\n<!-- nift:migration:start -->\n' > "$Q/AGENTS.md"; else printf 'x\n<!-- nift:migration:end -->\n' > "$Q/AGENTS.md"; fi
  for policy in error append replace; do
    if (cd "$Q" && "$NIFT_BIN" init --migration --migration-existing="$policy" >/dev/null 2>err); then fail "AGENTS $marker-only accepted under $policy"; fi
    [ -f "$Q/.nift/config.json" ] && fail "partial project left for AGENTS $marker-only"
  done
done
Q="$TMP/ag-multi"; mkdir -p "$Q"; printf '%s\n' '<!-- nift:migration:start -->' 'a' '<!-- nift:migration:end -->' '<!-- nift:migration:start -->' 'b' '<!-- nift:migration:end -->' > "$Q/AGENTS.md"
if (cd "$Q" && "$NIFT_BIN" init --migration >/dev/null 2>&1); then fail "AGENTS multiple blocks accepted"; fi

# Invalid --migration-existing value is rejected.
if "$NIFT_BIN" init --migration --migration-existing=bogus >/dev/null 2>&1; then fail "invalid policy accepted"; fi

# Nested target directory.
Q="$TMP/${TMPDIR:+nested}/nested/deep"; mkdir -p "$Q"
(cd "$Q" && "$NIFT_BIN" init --migration >/dev/null 2>&1) || fail "nested --migration failed"
[ -f "$Q/MIGRATION.md" ] || fail "nested MIGRATION.md missing"

echo "init-migration smoke test passed: fresh files canonical; error/keep/append/replace policies; AGENTS create/augment/replace/idempotent/malformed-safe; plain init unchanged"