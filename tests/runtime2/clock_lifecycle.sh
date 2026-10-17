#!/usr/bin/env bash
# CP01 follow-up (owner review): lifecycle of the modified-mode timing records
# in .nift/clock/. They are correctness state, not reproducible metadata:
# page-info stays byte-reproducible; a missing, malformed or stale record makes
# status rebuild conservatively; records are removed with their page and
# pruned when pages leave the tracked set.
set -u -o pipefail
NIFT="${NIFT_BIN:-$(pwd)/nift}"
fail=0
ok() { echo "PASS $1"; }
bad() { echo "FAIL $1"; fail=1; }
fresh() { "$NIFT" status 2>&1 | grep "up to date" >/dev/null; }
reason() { "$NIFT" status 2>&1 | grep -F "$1" >/dev/null; }
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
mk() { rm -rf "$1"; mkdir -p "$1"; (cd "$1" && "$NIFT" init >/dev/null 2>&1) || { echo "setup failed"; exit 2; }; }

P="$T/p"; mk "$P"; cd "$P"
CLOCK=".nift/clock/public/index.info.json"
[ -f "$CLOCK" ] && ok "clean build writes the timing record" || bad "no timing record after init build"
fresh && ok "clean build is up to date" || bad "clean build not up to date"

# Page-info is reproducible across clean rebuilds; the timing record is not
# part of that contract.
sum() { find .nift/public -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum; }
a="$(sum)"; rm -rf .nift/public public/index.html; "$NIFT" build --all >/dev/null 2>&1
[ "$a" = "$(sum)" ] && ok "page-info byte-identical after a clean rebuild" || bad "page-info differs after a clean rebuild"
"$NIFT" build >/dev/null 2>&1; fresh && ok "unchanged rebuild is a no-op" || bad "unchanged rebuild not up to date"

# Missing record (deleted clock dir, metadata from before this record existed).
rm -rf .nift/clock
reason "page timing record is missing or invalid" && ok "missing record -> conservative rebuild" || bad "missing record not detected"
"$NIFT" build >/dev/null 2>&1; fresh && ok "build restores the record" || bad "not up to date after restoring"

# Malformed record (interrupted write).
printf '{"racy-dependencies": [' > "$CLOCK"
reason "page timing record is missing or invalid" && ok "truncated record -> conservative rebuild" || bad "truncated record trusted"
"$NIFT" build >/dev/null 2>&1; fresh && ok "build repairs a truncated record" || bad "truncated record not repaired"

# Stale racy entry left behind (e.g. crash after the record was written):
# conservative, then clean.
printf '{\n  "racy-dependencies": ["content/index.html"],\n  "tick-hashes": {}\n}\n' > "$CLOCK"
reason "dependency was modified while the page was being built" && ok "stale racy entry -> rebuild" || bad "stale racy entry ignored"
"$NIFT" build >/dev/null 2>&1; fresh && ok "rebuild clears the stale entry" || bad "stale entry persists"

# Process killed mid-build (SIGKILL while the page renders): next status must
# not report stale output as current; a build recovers.
printf '@script { sleep(1500) }\nKILLED-RUN\n' > content/index.html
( "$NIFT" build >/dev/null 2>&1 & echo $! > "$T/pid"; wait ) &
sleep 0.5; kill -9 "$(cat "$T/pid")" 2>/dev/null; wait 2>/dev/null
if fresh; then bad "status trusts output after a killed build"; else ok "killed build -> page still needs rebuilding"; fi
printf 'RECOVERED\n' > content/index.html
"$NIFT" build --repair >/dev/null 2>&1   # an unfinished build requires repair (existing contract)
grep RECOVERED public/index.html >/dev/null && fresh && ok "build recovers after a killed build" || bad "no recovery after killed build"

# Records are removed with their page and pruned for untracked pages.
"$NIFT" track about about templates/template.html >/dev/null 2>&1; "$NIFT" build >/dev/null 2>&1
[ -f .nift/clock/public/about.info.json ] && ok "record written for a new page" || bad "no record for new page"
"$NIFT" rm about >/dev/null 2>&1
[ ! -e .nift/clock/public/about.info.json ] && ok "rm removes the page's record" || bad "rm left the record"
"$NIFT" track extra extra templates/template.html >/dev/null 2>&1; "$NIFT" build >/dev/null 2>&1
python3 - <<'PY'
import json
d=json.load(open('.nift/tracked.json')); d['tracked']=[t for t in d['tracked'] if t['name']!='extra']; json.dump(d,open('.nift/tracked.json','w'))
PY
echo 'touch' >> content/index.html; "$NIFT" build >/dev/null 2>&1
[ ! -e .nift/clock/public/extra.info.json ] && ok "untracked page's record pruned on the next build" || bad "stale record not pruned"
n=$(find .nift/clock -type f | wc -l); [ "$n" -eq 1 ] && ok "record count bounded by tracked pages ($n)" || bad "record count $n for 1 tracked page"

# Other modes neither need nor keep the record.
sed -i 's/"incremental-mode": "modified"/"incremental-mode": "hash"/' .nift/config.json
"$NIFT" build --all >/dev/null 2>&1
[ ! -e "$CLOCK" ] && ok "hash mode removes the record" || bad "record left in hash mode"
fresh && ok "hash mode up to date without a record" || bad "hash mode needs a record"
sed -i 's/"incremental-mode": "hash"/"incremental-mode": "modified"/' .nift/config.json
reason "page timing record is missing or invalid" && ok "back to modified mode -> one conservative rebuild" || bad "mode switch trusted stale state"
"$NIFT" build >/dev/null 2>&1; fresh && ok "modified mode settles after one build" || bad "modified mode does not settle"

# External dependency reached through a symlink (D-002).
mkdir -p "$T/ext"; echo EXT-1 > "$T/ext/data.txt"; ln -sfn "$T/ext" linked
printf '@input("linked/data.txt")\n' > content/index.html; "$NIFT" build >/dev/null 2>&1
fresh && ok "external symlinked dependency settles" || bad "external dependency perpetual rebuild"
sleep 1; echo EXT-2 > "$T/ext/data.txt"
"$NIFT" build >/dev/null 2>&1
grep EXT-2 public/index.html >/dev/null && ok "external edit through symlink rebuilds" || bad "external edit missed"
exit $fail
