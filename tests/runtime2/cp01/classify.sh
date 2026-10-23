#!/usr/bin/env bash
# classify.sh <nift-binary> <cycles> <followups> <label>
# Immediate edit -> build cycles (as modified_race.sh (a)); for every miss,
# run up to <followups> further ordinary builds WITHOUT edits and record which
# (if any) repairs it. Logs ns mtimes of the content file, page-info and output.
set -u -o pipefail
NIFT="$1"; CYCLES="$2"; FOLLOW="$3"; LABEL="$4"
OUT="$(mktemp)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
cd "$T" && "$NIFT" init >/dev/null 2>&1 && "$NIFT" build >/dev/null 2>&1 || { echo setup failed; exit 2; }
st() { stat -c '%.9Y' "$1" 2>/dev/null || echo none; }
: > "$OUT"
miss=0; same=0; persistent=0; build_fail=0
for i in $(seq 1 "$CYCLES"); do
  printf 'MARK-%s\n' "$i" > content/index.html
  c_m=$(st content/index.html)
  "$NIFT" build >/dev/null 2>&1 || build_fail=$((build_fail+1))
  if grep -q "MARK-$i" public/index.html; then continue; fi
  miss=$((miss+1))
  pi_m=$(st .nift/public/index.info.json); o_m=$(st public/index.html)
  # what happened on the build right before the edit? (page-info mtime is from that build)
  fixed=0
  for k in $(seq 1 "$FOLLOW"); do
    "$NIFT" build >/dev/null 2>&1
    if grep -q "MARK-$i" public/index.html; then fixed=$k; break; fi
  done
  status_line=$("$NIFT" status 2>&1 | grep -E 'up to date|need|stale|rebuild|changed' | head -1)
  if [ "$fixed" -gt 0 ]; then same=$((same+1)); kind="repaired-by-build-$fixed"; else persistent=$((persistent+1)); kind="PERSISTENT-after-$FOLLOW"; fi
  echo "cycle=$i content_mtime=$c_m pageinfo_mtime=$pi_m output_mtime=$o_m class=$kind status='$status_line'" >> "$OUT"
  # repair so the next cycle starts clean (an edit: new content)
  if [ "$fixed" -eq 0 ]; then printf 'MARK-%s-repair\n' "$i" > content/index.html; sleep 0.02; "$NIFT" build >/dev/null 2>&1; fi
done
echo "label=$LABEL cycles=$CYCLES misses=$miss repaired_next=$same persistent=$persistent build_fail=$build_fail" | tee -a "$OUT"
[ "$miss" -eq 0 ] && [ "$persistent" -eq 0 ] && [ "$build_fail" -eq 0 ]
