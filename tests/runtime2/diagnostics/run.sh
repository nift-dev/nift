#!/usr/bin/env bash
# CP13 diagnostic corpus runner (make test-runtime2-diagnostics).
#
# Two suites, kept apart (owner, D-022): the BASELINE corpus is never edited to
# make new code pass; approved changes have their own OLD-vs-NEW tests.
#
#   baseline  scripts/ and sites/ (89 cases): what legacy prints today.
#             Goldens <case>.{stdout,stderr,exit}; v4.12 differences in
#             <case>.v412.*. MANIFEST.tsv gives each case's class.
#   changes   changes/ : one case per approved change (decision in
#             changes/MANIFEST.tsv). <case>.old.* = legacy behaviour before the
#             change, <case>.new.* = intended behaviour (a missing .new stream
#             means "unchanged"). Guard cases have no .new files: old and new
#             must both hold.
#
# Usage: run.sh [options] [name-filter-regex]
#   --binary PATH     binary under test (default: $NIFT_BIN or ./nift)
#   --suite S         baseline (default) | changes
#   --expect SET      baseline: legacy (default) | v412
#                     changes:  old (default) | new
#   --vm              run with NIFT_VM=1 and report the engine outcome per case
#   --regenerate SET  write goldens (baseline: legacy|v412; changes: old, or new
#                     from a binary that implements the change - reviewed, and
#                     only where it differs from old)
#   --list            print the cases and their class, then exit
#
# Comparison (docs/runtime-2/checkpoints/cp13-diagnostics-design.md (b)):
# stdout, stderr and exit status as bytes, fresh copy of the case directory,
# env -i with PATH, temporary HOME/TMPDIR, LANG=C, LC_ALL=C, TZ=UTC, stdin
# /dev/null, no TTY; the copy's real path is replaced by $ROOT.
# Case kinds:
#   <dir>/<name>.f      `nift <name>.f`
#   <dir>/<name>.args   argv, one argument per line (e.g. -e, eval --json)
#   <dir>/<name>.files  files the program may create; their contents (or
#                       "(absent)") are appended to the stdout capture
#   sites/<name>/       overlay on a fresh `nift init` project, then
#                       `nift build --all`; stdout drops "time taken:" lines;
#                       files listed in sites/<name>.publish and the presence of
#                       .nift/.unfinished are appended to the stdout capture
# Verdicts: a difference on a `site-dx` case (baseline, or changes --expect
# old) is EXPECTED-CHANGE (legacy is being changed by feature-site-dx); with --vm, a difference on an
# `approved:*` case is SUPERSEDED (checked by the changes suite instead);
# every other difference is a FAIL.
# Decisions in BOTH_ENGINES are implemented on both engines by this binary
# (D-022 #3 prevalidation before engine selection, D-025 in both front ends):
# their baseline cases are SUPERSEDED with or without --vm, and in the
# changes suite `--expect old` reports them SUPERSEDED (they are checked by
# `--expect new`). The other decisions (D-018c, D-001#4, D-009#1) are VM-only:
# `--expect new` without --vm reports them VM-ONLY (legacy keeps the old
# behaviour by design; `--expect old` checks it). With the VM off every
# changes case is thus checked against exactly one expectation (guards, which
# have no .new files, against both).
set -u
set -o pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
NIFT="${NIFT_BIN:-$(pwd)/nift}"
BOTH_ENGINES="D-022#3 D-025"
both_engines() { local d="${1#approved:}"; [[ " $BOTH_ENGINES " == *" $d "* ]]; }
SUITE=baseline; EXPECT=""; VM=0; REGEN=""; LIST=0; FILTER=""
while [ $# -gt 0 ]; do
  case "$1" in
    --binary) NIFT="$2"; shift 2;;
    --suite) SUITE="$2"; shift 2;;
    --expect) EXPECT="$2"; shift 2;;
    --vm) VM=1; shift;;
    --regenerate) REGEN="$2"; shift 2;;
    --list) LIST=1; shift;;
    -h|--help) sed -n '2,44p' "$0"; exit 0;;
    *) FILTER="$1"; shift;;
  esac
done
case "$SUITE" in
  baseline) EXPECT="${EXPECT:-legacy}"; case "$EXPECT" in legacy|v412) ;; *) echo "baseline: --expect legacy|v412" >&2; exit 2;; esac
            [ -z "$REGEN" ] || case "$REGEN" in legacy|v412) ;; *) echo "baseline: --regenerate legacy|v412" >&2; exit 2;; esac
            MANIFEST="$HERE/MANIFEST.tsv";;
  changes)  EXPECT="${EXPECT:-old}"; case "$EXPECT" in old|new) ;; *) echo "changes: --expect old|new" >&2; exit 2;; esac
            [ -z "$REGEN" ] || [ "$REGEN" = old ] || [ "$REGEN" = new ] || { echo "changes: --regenerate old|new" >&2; exit 2; }
            MANIFEST="$HERE/changes/MANIFEST.tsv";;
  *) echo "unknown --suite $SUITE" >&2; exit 2;;
esac

class_of() { awk -F'\t' -v n="$1" '$1==n {print $2; f=1} END {if(!f) print "unlisted"}' "$MANIFEST"; }

if [ "$LIST" = 1 ]; then
  grep -v '^#' "$MANIFEST" | cut -f1,2
  exit 0
fi
NIFT="$(cd "$(dirname "$NIFT")" 2>/dev/null && pwd)/$(basename "$NIFT")"
[ -x "$NIFT" ] || { echo "binary not found: $NIFT" >&2; exit 2; }

W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
pass=0; fail=0; changed=0; superseded=0; n=0

run_env() {  # run_env DIR CMD...
  local dir="$1"; shift
  local vmenv=()
  if [ "$VM" = 1 ]; then vmenv=(NIFT_VM=1 NIFT_VM_STATS="$W/stats.jsonl"); fi
  ( cd "$dir" && env -i PATH="$PATH" HOME="$W/home" TMPDIR="$W/tmp" LANG=C LC_ALL=C TZ=UTC "${vmenv[@]}" \
      timeout 20 "$@" >"$W/out" 2>"$W/err" </dev/null; echo $? >"$W/exit" )
}

normalise() { for s in out err; do sed -i "s#$1#\$ROOT#g; s#$W/case#\$ROOT#g" "$W/$s"; done; }

append_files() {  # append_files LISTFILE
  [ -f "$1" ] || return 0
  while IFS= read -r p; do
    [ -n "$p" ] || continue
    echo "== $p ==" >>"$W/out"
    if [ -f "$W/case/$p" ]; then cat "$W/case/$p" >>"$W/out"; [ -z "$(tail -c1 "$W/case/$p")" ] || echo >>"$W/out"
    else echo "(absent)" >>"$W/out"; fi
  done <"$1"
}

golden_for() {  # $1 = base path, $2 = stdout|stderr|exit
  if [ "$SUITE" = changes ]; then
    if [ "$EXPECT" = new ] && [ -f "$1.new.$2" ]; then echo "$1.new.$2"; else echo "$1.old.$2"; fi
  elif [ "$EXPECT" = v412 ] && [ -f "$1.v412.$2" ]; then echo "$1.v412.$2"
  else echo "$1.$2"; fi
}

compare() {  # $1 = case name, $2 = golden base path
  local name="$1" base="$2" bad=0 k g diffs=""
  n=$((n+1))
  if [ -n "$REGEN" ]; then
    for k in out:stdout err:stderr exit:exit; do
      case "$REGEN" in
        legacy) cp "$W/${k%%:*}" "$base.${k##*:}";;
        old) cp "$W/${k%%:*}" "$base.old.${k##*:}";;
        new) if cmp -s "$W/${k%%:*}" "$base.old.${k##*:}"; then rm -f "$base.new.${k##*:}"; else cp "$W/${k%%:*}" "$base.new.${k##*:}"; fi;;
        v412) if cmp -s "$W/${k%%:*}" "$base.${k##*:}"; then rm -f "$base.v412.${k##*:}"; else cp "$W/${k%%:*}" "$base.v412.${k##*:}"; fi;;
      esac
    done
    echo "WROTE $name"; return
  fi
  for k in out:stdout err:stderr exit:exit; do
    g="$(golden_for "$base" "${k##*:}")"
    cmp -s "$W/${k%%:*}" "$g" || { bad=1; diffs="$diffs ${k##*:}"; }
  done
  local outcome=""
  if [ "$VM" = 1 ] && [ -s "$W/stats.jsonl" ]; then
    outcome=" [$(python3 -c 'import json,sys
r=json.loads(open(sys.argv[1]).read().splitlines()[-1])
print(r["outcome"]+(":"+r["reason"] if r["outcome"]=="fallback" else ""))' "$W/stats.jsonl" 2>/dev/null)]"
  elif [ "$VM" = 1 ]; then outcome=" [legacy-only path]"; fi
  rm -f "$W/stats.jsonl"
  local cls; cls="$(class_of "$name")"
  if [ "$bad" = 0 ]; then pass=$((pass+1)); echo "PASS $name$outcome"; return; fi
  if [ "$cls" = site-dx ] && { [ "$SUITE" = baseline ] || [ "$EXPECT" = old ]; }; then changed=$((changed+1)); echo "EXPECTED-CHANGE $name ($cls;$diffs)$outcome"; return; fi
  if [ "$SUITE" = baseline ] && [[ "$cls" == approved:* ]] && { [ "$VM" = 1 ] || both_engines "$cls"; }; then superseded=$((superseded+1)); echo "SUPERSEDED $name ($cls;$diffs)$outcome"; return; fi
  if [ "$SUITE" = changes ] && [ "$EXPECT" = old ] && both_engines "$cls" && ls "$base".new.* >/dev/null 2>&1; then superseded=$((superseded+1)); echo "SUPERSEDED $name ($cls;$diffs; implemented on both engines, checked by --expect new)"; return; fi
  if [ "$SUITE" = changes ] && [ "$EXPECT" = new ] && [ "$VM" = 0 ] && ! both_engines "$cls" && ls "$base".new.* >/dev/null 2>&1; then superseded=$((superseded+1)); echo "VM-ONLY $name ($cls;$diffs; legacy keeps the old behaviour, checked by --expect old)"; return; fi
  fail=$((fail+1)); echo "FAIL $name ($cls;$diffs)$outcome"
  for k in out:stdout err:stderr exit:exit; do
    g="$(golden_for "$base" "${k##*:}")"
    cmp -s "$W/${k%%:*}" "$g" || diff "$g" "$W/${k%%:*}" | head -6 | sed 's/^/    /'
  done
}

run_scripts() {  # run_scripts DIR
  local dir="$1" f b
  for f in "$dir"/*.f "$dir"/*.args; do
    [ -e "$f" ] || continue
    b="$(basename "$f")"; b="${b%.f}"; b="${b%.args}"
    [ -n "$FILTER" ] && ! [[ "$b" =~ $FILTER ]] && continue
    rm -rf "$W/case" "$W/home" "$W/tmp"; mkdir -p "$W/case" "$W/home" "$W/tmp"
    (cd "$dir" && find . -type f ! -name '*.stdout' ! -name '*.stderr' ! -name '*.exit' ! -name '*.files' ! -name '*.tsv' -exec cp --parents {} "$W/case/" \;)
    local real; real="$(cd "$W/case" && pwd -P)"
    if [ "${f%.args}" != "$f" ]; then mapfile -t argv <"$f"; run_env "$W/case" "$NIFT" "${argv[@]}"
    else run_env "$W/case" "$NIFT" "$b.f"; fi
    append_files "$dir/$b.files"
    normalise "$real"
    compare "$b" "$dir/$b"
  done
}

if [ "$SUITE" = changes ]; then
  run_scripts "$HERE/changes"
else
  run_scripts "$HERE/scripts"
  for d in "$HERE"/sites/*/; do
    [ -d "$d" ] || continue
    b="$(basename "$d")"
    [ -n "$FILTER" ] && ! [[ "$b" =~ $FILTER ]] && continue
    rm -rf "$W/case" "$W/home" "$W/tmp"; mkdir -p "$W/case" "$W/home" "$W/tmp"
    run_env "$W/case" "$NIFT" init
    if [ "$(cat "$W/exit")" != 0 ]; then echo "FAIL $b (nift init failed)"; fail=$((fail+1)); n=$((n+1)); continue; fi
    cp -R "$d". "$W/case/"
    real="$(cd "$W/case" && pwd -P)"
    run_env "$W/case" "$NIFT" build --all
    sed -i '/^time taken:/d' "$W/out"
    append_files "$HERE/sites/$b.publish"
    if [ -e "$W/case/.nift/.unfinished" ]; then echo "== state: .nift/.unfinished present ==" >>"$W/out"; fi
    normalise "$real"
    compare "$b" "$HERE/sites/$b"
  done
fi

[ -n "$REGEN" ] && { echo "regenerated $n cases ($SUITE/$REGEN) from $NIFT"; exit 0; }
echo "diagnostic corpus ($SUITE/$EXPECT$([ "$VM" = 1 ] && echo ', NIFT_VM=1'), $(basename "$NIFT")): $n cases, $pass pass, $fail fail, $changed expected-change (site-dx), $superseded superseded (approved)"
[ "$fail" -eq 0 ]
