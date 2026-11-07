#!/usr/bin/env bash
# CP06: the template scanner rewrite must be byte-identical. Builds every
# project site in the repository, a generated template-structure corpus and a
# Markdown site with both the candidate binary (NIFT_BIN) and a reference
# binary (NIFT_REF), then compares output trees, stdout, stderr and exit codes.
# It also runs the script probe corpus through both binaries.
#
#   NIFT_BIN=./nift NIFT_REF=/path/to/reference/nift tests/runtime2/scanner_parity.sh
#   NIFT_SITE=/path/to/extra/site  (optional, e.g. the 300-page benchmark site)
set -u
NIFT="${NIFT_BIN:-$(pwd)/nift}"
REF="${NIFT_REF:-}"
REPO="$(pwd)"
# A parity check without a reference proves nothing, so it is an error rather
# than a skip; the target is therefore not part of plain `make test`.
if [ -z "$REF" ]; then
  echo "ERROR scanner parity: NIFT_REF must name a reference nift binary" >&2; exit 2
fi
if [ ! -x "$REF" ]; then echo "FAIL scanner parity: NIFT_REF is not an executable: $REF"; exit 2; fi
W="${NIFT_PARITY_WORK:-$(mktemp -d)}"; [ -n "${NIFT_PARITY_WORK:-}" ] || trap 'rm -rf "$W"' EXIT
fail=0; sites=0; scripts=0

normalize() { sed -E 's/time taken: [0-9.e-]+ seconds/time taken: T seconds/; s#'"$1"'#<ROOT>#g'; }

compare_site() { # name source-dir
  local name="$1" src="$2" a="$W/a/$1" b="$W/b/$1"
  mkdir -p "$(dirname "$a")" "$(dirname "$b")"
  cp -r "$src" "$a"; cp -r "$src" "$b"
  rm -rf "$a/.nift/public" "$b/.nift/public"
  (cd "$a" && "$NIFT" build --all >"$W/a.out" 2>"$W/a.err"; echo $? >"$W/a.rc")
  (cd "$b" && "$REF" build --all >"$W/b.out" 2>"$W/b.err"; echo $? >"$W/b.rc")
  sites=$((sites+1))
  local bad=0
  cmp -s "$W/a.rc" "$W/b.rc" || bad=1
  diff -q <(normalize "$a" <"$W/a.out") <(normalize "$b" <"$W/b.out") >/dev/null || bad=1
  diff -q <(normalize "$a" <"$W/a.err") <(normalize "$b" <"$W/b.err") >/dev/null || bad=1
  diff -r -q -x '.nift' -x '*.log' "$a" "$b" >/dev/null || bad=1
  if [ $bad -ne 0 ]; then
    echo "FAIL site $name"; diff -r -x '.nift' "$a" "$b" | head -20; fail=1
  fi
}

# NIFT_PARITY_SCRIPTS=FILE: only the scripts listed in FILE, no sites (the
# regression test scanner_parity_selftest.sh; never a certification run).
RESTRICTED="${NIFT_PARITY_SCRIPTS:-}"

# 1. Project sites in the repository.
[ -n "$RESTRICTED" ] || while IFS= read -r config; do
  dir="$(dirname "$(dirname "$config")")"
  compare_site "repo-$(echo "${dir#./}" | tr '/' '_')" "$REPO/$dir"
done < <(cd "$REPO" && git ls-files '*/.nift/config.json')

# 2. Generated template-structure corpus: <pre>, comments, code, indentation,
#    nested includes and control blocks, the constructs the scanner rewrite
#    touched.
python3 - "$W/gen" <<'PY'
import os, random, sys, json
root = sys.argv[1]
rnd = random.Random(20261017)
# Inline fragments that are always well formed on their own.
atoms = ["é あ ü", "@script { return 1 }", "plain text", "a < b", "x-->y", "--", "-", "a<b>c", "<p>para</p>", "\\@", "\\$",
         "<code>c</code>", "<!-- note -->", "<!-- {brace} -->", "@/* comment */", "&lt;",
         "<pref>", "</pr", "`{`", "'{'", "\"}\"", "<!---->", "<!-- <pre> -->", "\t", "  ", "@@x"]
safe = lambda t: "".join(ch for ch in t if ch not in "{}`'\"")
def inline(n):
    return "".join(rnd.choice(atoms) + rnd.choice(["", " ", "\n", "\n  ", "\n\t"]) for _ in range(n))
def block(n):
    k = rnd.random()
    if k < 0.3:
        return "<pre>" + inline(n) + "\n  indented in pre\n</pre>"
    if k < 0.45:
        return "<pre class=\"x\">\n<code>" + inline(n) + "</code>\n</pre>"
    if k < 0.6:
        return "<!--\n" + inline(n) + "\n<pre>\n-->"
    return inline(n)
for case in range(60):
    d = os.path.join(root, f"case{case}")
    for sub in ("content", "templates", ".nift"):
        os.makedirs(os.path.join(d, sub), exist_ok=True)
    json.dump({"config": {"content-dir": "content/", "content-ext": ".html", "output-dir": "public/",
               "output-ext": ".html", "default-template": "templates/template.html", "build-threads": 1,
               "incremental-mode": "modified"}}, open(os.path.join(d, ".nift/config.json"), "w"))
    json.dump({"tracked": [{"name": "/", "title": "t", "template": "templates/template.html"}]},
              open(os.path.join(d, ".nift/tracked.json"), "w"))
    open(os.path.join(d, "templates/part.html"), "w").write(
        block(6) + "\nline two\n" + block(4) + "\n")
    open(os.path.join(d, "templates/template.html"), "w").write(
        "<html>\n  <body>\n    @content\n    <div>\n      @input(\"templates/part.html\")\n    </div>\n  </body>\n</html>\n")
    body = []
    for _ in range(rnd.randint(3, 12)):
        k = rnd.random()
        if k < 0.25:
            body.append(rnd.choice(["", "  ", "\t", "    "]) + "@input(\"templates/part.html\")\n")
        elif k < 0.4:
            body.append("@if(1 == 1) {\n  " + safe(block(5)) + "\n}\n")
        elif k < 0.5:
            body.append("@for(item : [1, 2, 3]) {\n  $[item] " + safe(inline(3)) + "\n  @if(item == 2) { " + rnd.choice(["@break", "@continue"]) + " }\n}\n")
        elif k < 0.55:
            body.append("@for(item : [1, 2]) {\n  $[item] " + safe(inline(3)) + "\n}\n")
        elif k < 0.65:
            body.append("@// a line comment\n")
        else:
            body.append(block(rnd.randint(3, 12)) + "\n")
    text = "".join(body)
    if case % 5 == 4:
        text = text.replace("\n", "\r\n")   # CRLF sources
    open(os.path.join(d, "content/index.html"), "w", newline="").write(text)
PY
[ -n "$RESTRICTED" ] || for d in "$W"/gen/case*; do compare_site "gen-$(basename "$d")" "$d"; done

# 3. Optional extra site (e.g. the 300-page benchmark site).
[ -n "${NIFT_SITE:-}" ] && [ -d "${NIFT_SITE}" ] && compare_site "extra-site" "$NIFT_SITE"

# 4. Script probe corpus.
# Flakiness fix (2026-11-08), three causes found in two runs on one tree:
# - The cp22 VM-only fixtures (tests/runtime2/cp22/vm-only/*.f: thread and task
#   ordering, preemption) have a deterministic VM contract that
#   test-runtime2-cp22 --mode vm-only asserts against their recorded .vm.*
#   expectations; on the legacy reference their order and results vary by
#   design (real threads), so a parity comparison with it has no stable answer.
#   They are counted and named here, not compared.
# - When the reference is killed (timeout), bash reports the job ("line N: PID
#   Killed ...") asynchronously, at a later script's FAIL output, with a pid:
#   the runs' own shell messages now go to $W/shell.msgs; the exit status
#   (124/137) is still compared.
# - Values that legitimately differ per run (scanner_parity_masks.tsv: the
#   socket probe binds port 0 and prints the port the kernel allocated) are
#   compared and reported with digits masked; a reference that differs between
#   two runs is masked as before, and the reported diff is now the masked one.
MASKS="$REPO/tests/runtime2/scanner_parity_masks.tsv"
mask_port() { sed -E 's/port [0-9]+/port <PORT>/g'; }
# NIFT_PARITY_UNFIXED=1 restores the three old behaviours (the regression test
# shows they make two runs differ); only with NIFT_PARITY_SCRIPTS.
UNFIXED=0; [ -n "$RESTRICTED" ] && [ "${NIFT_PARITY_UNFIXED:-}" = 1 ] && UNFIXED=1
MSGS="$W/shell.msgs"; [ "$UNFIXED" -eq 1 ] && MSGS=/dev/stderr
vmonly=0
while IFS= read -r script; do
  [ "$UNFIXED" -eq 1 ] || case "$script" in tests/runtime2/cp22/vm-only/*.f) vmonly=$((vmonly+1)); continue ;; esac
  mkdir -p "$W/sa" "$W/sb"
  (cd "$W/sa" && timeout 30 "$NIFT" "$REPO/$script" >"$W/s.a.out" 2>"$W/s.a.err" </dev/null; echo $? >"$W/s.a.rc") 2>>"$MSGS"
  (cd "$W/sb" && timeout 30 "$REF" "$REPO/$script" >"$W/s.b.out" 2>"$W/s.b.err" </dev/null; echo $? >"$W/s.b.rc") 2>>"$MSGS"
  scripts=$((scripts+1))
  listed=0; [ "$UNFIXED" -eq 0 ] && grep -q "^$script	port	" "$MASKS" 2>/dev/null && listed=1
  if ! cmp -s "$W/s.a.rc" "$W/s.b.rc" || ! cmp -s "$W/s.a.out" "$W/s.b.out" || ! cmp -s "$W/s.a.err" "$W/s.b.err"; then
    # Only a script whose reference output is itself nondeterministic (timing
    # probes) is compared with digits masked; stdout, stderr and exit code are
    # always all compared.
    (cd "$W/sb" && timeout 30 "$REF" "$REPO/$script" >"$W/s.c.out" 2>"$W/s.c.err" </dev/null) 2>>"$MSGS"
    mask=cat
    if [ "$listed" -eq 1 ]; then mask=mask_port   # only the allocated port number (review of a977376e)
    elif ! cmp -s "$W/s.b.out" "$W/s.c.out" || ! cmp -s "$W/s.b.err" "$W/s.c.err"; then mask="tr -d 0-9"; fi
    if ! cmp -s "$W/s.a.rc" "$W/s.b.rc" ||
       ! diff -q <($mask <"$W/s.a.out") <($mask <"$W/s.b.out") >/dev/null ||
       ! diff -q <($mask <"$W/s.a.err") <($mask <"$W/s.b.err") >/dev/null; then
      rmask=$mask; [ "$UNFIXED" -eq 1 ] && rmask=cat
      echo "FAIL script $script"; diff <($rmask <"$W/s.a.out") <($rmask <"$W/s.b.out") | head -5
      diff <($rmask <"$W/s.a.err") <($rmask <"$W/s.b.err") | head -5; fail=1
    fi
  fi
done < <(if [ -n "$RESTRICTED" ]; then cat "$RESTRICTED"; else cd "$REPO" && git ls-files 'docs/evidence/cp410-competitiveness/probes/*.f' 'tests/*.f'; fi)

echo "scanner parity: $sites sites, $scripts scripts compared; $vmonly cp22 VM-only fixtures left to test-runtime2-cp22 --mode vm-only"
# Guard against a silently empty corpus (e.g. run outside a git checkout).
if [ -n "$RESTRICTED" ]; then echo "FAIL scanner parity: restricted corpus (a self-test, never a certification run)"; fail=1
elif [ "$sites" -lt 70 ] || [ "$scripts" -lt 100 ]; then echo "FAIL scanner parity: corpus too small"; fail=1; fi
[ $fail -eq 0 ] && echo "PASS scanner parity" || echo "FAIL scanner parity"
exit $fail
