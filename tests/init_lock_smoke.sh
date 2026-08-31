#!/usr/bin/env bash
# Deterministic coverage for `nift init` establishing the persistent project
# serialization lock (.nift/.lock) using the same canonical sentence and
# reliable population helper as the runtime ownership protocol.
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
LOCK_TEXT="Nift project lock. This persistent file is normal and does not indicate an active or failed build."

TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-init-lock.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

fails=0
check() {
  if [ "$1" = PASS ]; then echo "  [PASS] $2"; else echo "  [FAIL] $2"; fails=1; fi
}

# 1. A fresh init creates a populated .nift/.lock and covers it in .gitignore,
#    without creating .unfinished.
P="$TMP/fresh"; mkdir -p "$P"; ( cd "$P" && "$NIFT_BIN" init >/dev/null 2>&1 )
test -f "$P/.nift/.lock" && check PASS "fresh init creates .nift/.lock" || check FAIL "fresh init creates .nift/.lock"
[ "$(cat "$P/.nift/.lock")" = "$LOCK_TEXT" ] && check PASS "lock contents exactly canonical" || check FAIL "lock contents exactly canonical"
test -f "$P/.nift/.unfinished" && check FAIL "init does not create .unfinished" || check PASS "init does not create .unfinished"
test -f "$P/.gitignore" && check PASS "init generates a .gitignore" || check FAIL "init generates a .gitignore"
grep -qx '.nift/.lock' "$P/.gitignore" && check PASS "generated .gitignore ignores .nift/.lock" || check FAIL "generated .gitignore ignores .nift/.lock"
! grep -qE '^(\*?\.lock|\*\.lock|\.lock)$' "$P/.gitignore" \
  && check PASS "ignore rule is scoped (no bare .lock or *.lock)" \
  || check FAIL "ignore rule is scoped (no bare .lock or *.lock)"

# 2. A fresh init followed by a successful build leaves .lock present, no
#    .unfinished, and .lock still canonical.
( cd "$P" && printf 'x\n' > index.html && "$NIFT_BIN" build --all >/dev/null 2>&1 )
[ "$(cat "$P/.nift/.lock")" = "$LOCK_TEXT" ] && check PASS "lock persists after first build" || check FAIL "lock persists after first build"
test -f "$P/.nift/.unfinished" && check FAIL "no .unfinished after successful build" || check PASS "no .unfinished after successful build"

# 3. Injected population failures make init fail cleanly, before any project
#    configuration is written (no misleading successful state, no .unfinished).
for seam in err partial flush size; do
  S="$TMP/seam-$seam"; mkdir -p "$S"
  set +e
  ( cd "$S" && NIFT_TEST_LOCK_WRITE_FAIL="$seam" "$NIFT_BIN" init >/dev/null 2>&1 )
  rc=$?
  set -e
  [ "$rc" -ne 0 ] && check PASS "init fails cleanly on injected $seam write failure" || check FAIL "init fails cleanly on injected $seam write failure"
  test -f "$S/.nift/config.json" && check FAIL "injected $seam failure leaves no config.json" || check PASS "injected $seam failure leaves no config.json"
  test -f "$S/.nift/.unfinished" && check FAIL "injected $seam failure creates no .unfinished" || check PASS "injected $seam failure creates no .unfinished"
done

# 4. A project created by an older Nift (no .lock, no .ownership-gate) acquires
#    .lock on its first mutating command.
P="$TMP/old"; mkdir -p "$P/.nift" "$P/content" "$P/templates" "$P/public"
cat > "$P/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","output-dir":"public/","default-template":"templates/template.html","build-threads":1,"incremental-mode":"modified"}}
JSON
printf '{"tracked":[{"name":"p0","title":"P0","template":"templates/template.html"}]}' > "$P/.nift/tracked.json"
printf '<main>@content</main>\n' > "$P/templates/template.html"
printf '<p>0</p>\n' > "$P/content/p0.html"
( cd "$P" && "$NIFT_BIN" build --all >/dev/null 2>&1 )
[ "$(cat "$P/.nift/.lock")" = "$LOCK_TEXT" ] && check PASS "older project acquires .lock on first build" || check FAIL "older project acquires .lock on first build"

# 5. `nift init` over an existing initialized project (config.json present)
#    still refuses; the "do not initialize over a project" protection holds.
P="$TMP/already"; mkdir -p "$P/.nift"; printf '{}' > "$P/.nift/config.json"
set +e; ( cd "$P" && "$NIFT_BIN" init >/dev/null 2>&1 ); rc=$?; set -e
[ "$rc" -ne 0 ] && check PASS "init refuses over an already initialized project" || check FAIL "init refuses over an already initialized project"

# 6. Partial-init cases reach ensure_lock_file through `nift init`:
#    an existing non-empty .lock is preserved (identity + contents) and a
#    project completes; an existing empty .lock is repaired.
P="$TMP/partial-nonempty"; mkdir -p "$P/.nift"
printf 'custom persistent content\n' > "$P/.nift/.lock"
INODE_BEFORE="$(stat -c %i "$P/.nift/.lock" 2>/dev/null || true)"
( cd "$P" && "$NIFT_BIN" init >/dev/null 2>&1 )
[ "$(cat "$P/.nift/.lock")" = "custom persistent content" ] \
  && check PASS "partial-init preserves an existing non-empty .lock" \
  || check FAIL "partial-init preserves an existing non-empty .lock"
if [ -n "$INODE_BEFORE" ]; then
  INODE_AFTER="$(stat -c %i "$P/.nift/.lock" 2>/dev/null || true)"
  [ "$INODE_BEFORE" = "$INODE_AFTER" ] && check PASS "partial-init keeps the same .lock inode" || check FAIL "partial-init keeps the same .lock inode"
fi
test -f "$P/.nift/config.json" && check PASS "partial-init completes the project" || check FAIL "partial-init completes the project"

P="$TMP/partial-empty"; mkdir -p "$P/.nift"; : > "$P/.nift/.lock"
( cd "$P" && "$NIFT_BIN" init >/dev/null 2>&1 )
[ "$(cat "$P/.nift/.lock")" = "$LOCK_TEXT" ] && check PASS "partial-init repairs an existing empty .lock" || check FAIL "partial-init repairs an existing empty .lock"

# 7. A directory or symlink at .nift/.lock makes `nift init` refuse.
P="$TMP/dir-lock"; mkdir -p "$P/.nift/.lock"
set +e; ( cd "$P" && "$NIFT_BIN" init >/dev/null 2>&1 ); rc=$?; set -e
[ "$rc" -ne 0 ] && check PASS "init refuses a directory at .nift/.lock" || check FAIL "init refuses a directory at .nift/.lock"
test -f "$P/.nift/config.json" && check FAIL "directory-.lock refusal leaves no config.json" || check PASS "directory-.lock refusal leaves no config.json"
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) ;;
  *)
    P="$TMP/symlink-lock"; mkdir -p "$P/.nift"; ln -s somewhere "$P/.nift/.lock"
    set +e; ( cd "$P" && "$NIFT_BIN" init >/dev/null 2>&1 ); rc=$?; set -e
    [ "$rc" -ne 0 ] && check PASS "init refuses a symlink at .nift/.lock" || check FAIL "init refuses a symlink at .nift/.lock"
    test -f "$P/.nift/config.json" && check FAIL "symlink-.lock refusal leaves no config.json" || check PASS "symlink-.lock refusal leaves no config.json"
    ;;
esac

# 8. Injected parent-directory-sync failure makes init fail before config.json.
P="$TMP/parent-sync"; mkdir -p "$P"
set +e; ( cd "$P" && NIFT_TEST_PARENT_SYNC_FAIL=1 "$NIFT_BIN" init >/dev/null 2>&1 ); rc=$?; set -e
[ "$rc" -ne 0 ] && check PASS "init fails on injected parent-sync failure" || check FAIL "init fails on injected parent-sync failure"
test -f "$P/.nift/config.json" && check FAIL "parent-sync failure leaves no config.json" || check PASS "parent-sync failure leaves no config.json"

if [ "$fails" -ne 0 ]; then echo "FAILED"; exit 1; fi
echo "ALL INIT LOCK SMOKE TESTS PASSED"