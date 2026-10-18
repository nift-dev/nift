#!/usr/bin/env bash
# CP13 diagnostic corpus runner (design phase: not wired into `make test`).
#
# Runs every case under tests/runtime2/diagnostics/ on one Nift binary and
# compares stdout, stderr and exit status byte for byte with the recorded
# expectations. See docs/runtime-2/checkpoints/cp13-diagnostics-design.md,
# section (b), for what "byte-identical" means and which golden applies.
#
# Usage: run.sh [options] [name-filter-regex]
#   --binary PATH     binary under test (default: $NIFT_BIN or ./nift)
#   --expect SET      legacy (default): <case>.stdout/.stderr/.exit
#                     v412:   <case>.v412.* where present, else legacy
#                     r2:     <case>.r2.*   where present, else legacy
#                             (Runtime 2.0 intent: D-009/D-018c approved changes)
#   --vm              run scripts with NIFT_VM=1 and report the engine outcome
#                     per case (vm / vm-error / fallback:<reason>)
#   --regenerate SET  write goldens for SET (legacy|v412|r2) from this binary
#                     instead of comparing; v412/r2 files are only written
#                     where they differ from the legacy golden
#   --list            print the cases and their manifest class, then exit
#
# Case kinds:
#   scripts/<name>.f      `nift <name>.f`, run in a fresh copy of scripts/
#   scripts/<name>.args   argv, one argument per line (e.g. -e, eval --json)
#   sites/<name>/         overlay copied onto a fresh `nift init` project, then
#                         `nift build --all`; stdout drops "time taken:" lines;
#                         files listed in sites/<name>.publish and the presence
#                         of .nift/.unfinished are appended to the stdout capture
# Manifest classes (MANIFEST.tsv): parity, approved:<decision>, site-dx,
# defect, catchability. A difference on a `site-dx` case is reported as
# EXPECTED-CHANGE (the behaviour is to change under site-dx / D-020) and does
# not fail the run; every other difference is a FAIL.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
NIFT="${NIFT_BIN:-$(pwd)/nift}"
EXPECT=legacy; VM=0; REGEN=""; LIST=0; FILTER=""
while [ $# -gt 0 ]; do
  case "$1" in
    --binary) NIFT="$2"; shift 2;;
    --expect) EXPECT="$2"; shift 2;;
    --vm) VM=1; shift;;
    --regenerate) REGEN="$2"; shift 2;;
    --list) LIST=1; shift;;
    -h|--help) sed -n '2,32p' "$0"; exit 0;;
    *) FILTER="$1"; shift;;
  esac
done
case "$EXPECT" in legacy|v412|r2) ;; *) echo "unknown --expect $EXPECT" >&2; exit 2;; esac
[ -z "$REGEN" ] || case "$REGEN" in legacy|v412|r2) ;; *) echo "unknown --regenerate $REGEN" >&2; exit 2;; esac

class_of() { awk -F'\t' -v n="$1" '$1==n {print $2; f=1} END {if(!f) print "unlisted"}' "$HERE/MANIFEST.tsv"; }

W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
pass=0; fail=0; changed=0; n=0

# run_env DIR CMD...: fixed minimal environment (as tests/runtime2/vm_corpus.sh).
run_env() {
  local dir="$1"; shift
  local vmenv=()
  if [ "$VM" = 1 ]; then vmenv=(NIFT_VM=1 NIFT_VM_STATS="$W/stats.jsonl"); fi
  ( cd "$dir" && env -i PATH="$PATH" HOME="$W/home" TMPDIR="$W/tmp" LANG=C LC_ALL=C TZ=UTC "${vmenv[@]}" \
      timeout 20 "$@" >"$W/out" 2>"$W/err" </dev/null; echo $? >"$W/exit" )
}

normalise() {  # $1 = real path of the case root
  for s in out err; do sed -i "s#$1#\$ROOT#g; s#$W/case#\$ROOT#g" "$W/$s"; done
}

golden_for() {  # $1 = base path without extension, $2 = stdout|stderr|exit
  if [ "$EXPECT" != legacy ] && [ -f "$1.$EXPECT.$2" ]; then echo "$1.$EXPECT.$2"; else echo "$1.$2"; fi
}

compare() {  # $1 = case name, $2 = golden base path
  local name="$1" base="$2" bad=0 k
  n=$((n+1))
  if [ -n "$REGEN" ]; then
    for k in out:stdout err:stderr exit:exit; do
      if [ "$REGEN" = legacy ]; then cp "$W/${k%%:*}" "$base.${k##*:}"
      elif ! cmp -s "$W/${k%%:*}" "$base.${k##*:}"; then cp "$W/${k%%:*}" "$base.$REGEN.${k##*:}"
      else rm -f "$base.$REGEN.${k##*:}"; fi
    done
    echo "WROTE $name"; return
  fi
  local diffs=""
  for k in out:stdout err:stderr exit:exit; do
    local g; g="$(golden_for "$base" "${k##*:}")"
    if ! cmp -s "$W/${k%%:*}" "$g"; then bad=1; diffs="$diffs ${k##*:}"; fi
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
  if [ "$cls" = site-dx ]; then changed=$((changed+1)); echo "EXPECTED-CHANGE $name ($cls;$diffs)$outcome"; return; fi
  fail=$((fail+1)); echo "FAIL $name ($cls;$diffs)$outcome"
  for k in out:stdout err:stderr exit:exit; do
    local g; g="$(golden_for "$base" "${k##*:}")"
    cmp -s "$W/${k%%:*}" "$g" || diff "$g" "$W/${k%%:*}" | head -6 | sed 's/^/    /'
  done
}

if [ "$LIST" = 1 ]; then
  for f in "$HERE"/scripts/*.f "$HERE"/scripts/*.args "$HERE"/sites/*/; do
    b="$(basename "$f")"; b="${b%.f}"; b="${b%.args}"; printf '%-40s %s\n' "$b" "$(class_of "$b")"
  done
  exit 0
fi
NIFT="$(cd "$(dirname "$NIFT")" 2>/dev/null && pwd)/$(basename "$NIFT")"
[ -x "$NIFT" ] || { echo "binary not found: $NIFT" >&2; exit 2; }

# Scripts: each case runs in a fresh copy of scripts/ (goldens not copied).
for f in "$HERE"/scripts/*.f "$HERE"/scripts/*.args; do
  [ -e "$f" ] || continue
  b="$(basename "$f")"; b="${b%.f}"; b="${b%.args}"
  [ -n "$FILTER" ] && ! [[ "$b" =~ $FILTER ]] && continue
  rm -rf "$W/case" "$W/home" "$W/tmp"; mkdir -p "$W/case" "$W/home" "$W/tmp"
  (cd "$HERE/scripts" && find . -type f ! -name '*.stdout' ! -name '*.stderr' ! -name '*.exit' -exec cp --parents {} "$W/case/" \;)
  real="$(cd "$W/case" && pwd -P)"
  if [ "${f%.args}" != "$f" ]; then
    mapfile -t argv <"$f"; run_env "$W/case" "$NIFT" "${argv[@]}"
  else
    run_env "$W/case" "$NIFT" "$b.f"
  fi
  normalise "$real"
  compare "$b" "$HERE/scripts/$b"
done

# Sites: overlay on a fresh project created by the binary under test.
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
  if [ -f "$HERE/sites/$b.publish" ]; then
    while IFS= read -r p; do
      [ -n "$p" ] || continue
      echo "== $p ==" >>"$W/out"
      if [ -f "$W/case/$p" ]; then cat "$W/case/$p" >>"$W/out"; else echo "(absent)" >>"$W/out"; fi
    done <"$HERE/sites/$b.publish"
  fi
  if [ -e "$W/case/.nift/.unfinished" ]; then echo "== state: .nift/.unfinished present ==" >>"$W/out"; fi
  normalise "$real"
  compare "$b" "$HERE/sites/$b"
done

[ -n "$REGEN" ] && { echo "regenerated $n cases ($REGEN) from $NIFT"; exit 0; }
echo "diagnostic corpus ($EXPECT, $(basename "$NIFT")): $n cases, $pass pass, $fail fail, $changed expected-change (site-dx)"
[ "$fail" -eq 0 ]
