#!/usr/bin/env bash
# feature-site-dx: defects found while dogfooding the website generator.
# See docs/runtime-2/checkpoints/feature-site-dx.md. Each section names the
# scope item it covers. Usage: NIFT_BIN=/path/to/nift tests/site_dx_smoke.sh
set -uo pipefail
NIFT=${NIFT_BIN:-./nift}
NIFT=$(cd "$(dirname "$NIFT")" && pwd)/$(basename "$NIFT")
work=$(mktemp -d)
auto_pid=""
cleanup() { [ -n "$auto_pid" ] && kill "$auto_pid" 2>/dev/null; rm -rf "$work"; }
trap cleanup EXIT
failures=0
fail() { echo "FAIL: $*" >&2; failures=$((failures + 1)); }
pass() { echo "ok: $*"; }
expect_contains() { # haystack needle label
  if grep -qF -- "$2" <<<"$1"; then pass "$3"; else fail "$3 (missing: $2)"; printf '%s\n' "$1" | head -20 >&2; fi
}
expect_absent() {
  if grep -qF -- "$2" <<<"$1"; then fail "$3 (unexpected: $2)"; printf '%s\n' "$1" | head -20 >&2; else pass "$3"; fi
}
new_project() { # dir
  mkdir -p "$1" && (cd "$1" && "$NIFT" init >/dev/null 2>&1) || { fail "init $1"; return 1; }
}

# ---------------------------------------------------------------------------
# Item 2: unterminated string / array literal and $[ in scripts
# ---------------------------------------------------------------------------
cd "$work"
printf 'print("before")\ns := "unterminated\nprint("after")\n' > r1a.f
out=$("$NIFT" r1a.f 2>&1); rc=$?
[ $rc -ne 0 ] && pass "unterminated string exits non-zero" || fail "unterminated string exit $rc"
expect_contains "$out" "r1a.f:2:6: unterminated string literal opened at 2:6" "unterminated string location"
expect_absent "$out" "after" "rest of program not run"

printf 'print("before")\na := [1, 2, 3\nprint("after")\n' > r1b.f
out=$("$NIFT" r1b.f 2>&1); rc=$?
[ $rc -ne 0 ] && pass "unterminated array exits non-zero" || fail "unterminated array exit $rc"
expect_contains "$out" "r1b.f:2:6: unterminated array literal or index: '[' opened at 2:6 has no closing ']'" "unterminated array location"

printf 'x := "a"\ny := x + "b\n' > r1d.f
out=$("$NIFT" r1d.f 2>&1); rc=$?
[ $rc -ne 0 ] && pass "unterminated string on last line" || fail "last-line string exit $rc"
expect_contains "$out" "r1d.f:2:10: unterminated string literal opened at 2:10" "last-line string location"

out=$("$NIFT" -e 's := "x' 2>&1); rc=$?
[ $rc -ne 0 ] && pass "-e unterminated string" || fail "-e exit $rc"
expect_contains "$out" "unterminated string literal opened at 1:6" "-e location"
out=$("$NIFT" -c 'a := [1, 2' 2>&1); rc=$?
[ $rc -ne 0 ] && pass "-c unterminated array" || fail "-c exit $rc"
expect_contains "$out" "unterminated array literal or index: '[' opened at 1:6" "-c location"
out=$("$NIFT" -e 'print("a"); $[x := 1' 2>&1); rc=$?
[ $rc -ne 0 ] && pass "unterminated \$[ in a script is an error" || fail "unterminated \$[ exit $rc"
expect_contains "$out" "unterminated '\$[' opened at 1:13 has no closing ']'" "unterminated \$[ location"

# Control: valid programs with quotes/brackets across lines are unchanged.
printf 's := "multi\nline"\na := [1,\n 2]\nprint(s)\nprint(a[1].to_string())\n' > ok.f
out=$("$NIFT" ok.f 2>&1); rc=$?
[ $rc -eq 0 ] && [ "$out" = $'multi\nline\n2' ] && pass "multi-line literals still valid" || fail "multi-line literals: rc=$rc out=$out"
# Control: an unclosed '(' keeps its existing diagnostic.
printf 'x := (1 + 2\nprint("after")\n' > paren.f
out=$("$NIFT" paren.f 2>&1); rc=$?
[ $rc -ne 0 ] && grep -q 'paren.f:1:6: unknown value or malformed expression' <<<"$out" && pass "unclosed ( unchanged" || fail "unclosed (: $out"

# ---------------------------------------------------------------------------
# Item 3: caught-error locations are exact
# ---------------------------------------------------------------------------
cat > try.f <<'F'
// line1
// line2
try {
    c := 3
    d := cat("/nonexistent/raised-on-line-5")
} catch(err) {
    print(err.line.to_string() + ":" + err.column.to_string())
}
try {
  x := 1
  y := cat("/nonexistent/line11")
} catch(e) { print(e.line.to_string() + ":" + e.column.to_string()) }
try { z := cat("/nonexistent/line13") } catch(e) { print(e.line.to_string() + ":" + e.column.to_string()) }
try {
    try {
        q := 1
        w := cat("/nonexistent/line17")
    } catch(inner) {
        print("inner " + inner.line.to_string() + ":" + inner.column.to_string())
        k := cat("/nonexistent/line20")
    }
} catch(outer) {
    print("outer " + outer.line.to_string() + ":" + outer.column.to_string())
}
fn(load()) {
    a := 1
    t := cat("/nonexistent/line27")
}
try {
    load()
} catch(f) { print("fn " + f.line.to_string() + ":" + f.column.to_string()) }
if(true) {
    try {
        v := cat("/nonexistent/line34")
    } catch(g) { print("if " + g.line.to_string() + ":" + g.column.to_string()) }
}
F
out=$("$NIFT" try.f 2>&1)
expected=$'5:10\n11:8\n13:12\ninner 17:14\nouter 20:14\nfn 27:10\nif 34:14'
[ "$out" = "$expected" ] && pass "try/catch exact locations" || { fail "try/catch locations"; diff <(echo "$expected") <(echo "$out") >&2; }

cat > arity.f <<'F'
// line1
try {
    d := cat("/nonexistent/x")
} catch(err) {
    print(err.line, ":", err.column)
}
F
out=$("$NIFT" arity.f 2>&1)
expect_contains "$out" "arity.f:5:5: print: expected one value" "error in catch body at its own line"
# Uncaught (fatal) errors raised in try or catch bodies are located exactly too.
uncaught() { # label program expected-location-and-message
  printf '%s' "$2" > unc.f
  local o; o=$("$NIFT" unc.f 2>&1)
  expect_contains "$o" "unc.f:$3" "uncaught in try/catch: $1"
}
uncaught "division by zero" $'try {\n  print(1 / 0)\n} catch(e) {\n  print(e.message)\n}\n' "2:9: division by zero"
uncaught "undefined callable" $'print("start")\ntry {\n  x := 1\n  undefined_fn(1)\n} catch(e) {\n  print(e.message)\n}\n' "4:3: undefined callable: undefined_fn"
uncaught "index out of range" $'try {\n  a := [1]\n  x := a[5]\n} catch(e) {\n  print(e.message)\n}\n' "3:8: JSON array index 5 is out of range"
uncaught "binding type" $'x := 1\ntry {\n  x = "s"\n} catch(e) {\n  print(e.message)\n}\n' "3:3: cannot assign string to int binding 'x'"
uncaught "throw of a non-Error" $'try { throw "plain" } catch(e) { print(e) }\n' "1:7: throw expression must evaluate to Error"
uncaught "unknown value in catch" $'try {\n  throw error("first")\n} catch(e) {\n  print("handling")\n  print(nosuch)\n}\n' "5:9: unknown value or malformed expression: nosuch"
uncaught "rethrow keeps origin" $'try {\n  throw error("first")\n} catch(e) {\n  throw e\n}\n' "2:3: first"
out=$("$NIFT" -e $'if(true) {\n  print(1, 2)\n}' 2>&1)
expect_contains "$out" "<command-line>:2:3: print: expected one value" "@if contrast (already exact)"

# ---------------------------------------------------------------------------
# Item 4: run() of a missing or non-executable program
# ---------------------------------------------------------------------------
out=$("$NIFT" -e 'r := run("definitely-not-a-cmd-sitedx"); print(r.stringify())' 2>&1)
[ "$out" = '{"exit_code":127,"stdout":"","stderr":"","launched":false,"error":"executable not found: definitely-not-a-cmd-sitedx"}' ] \
  && pass "run() missing executable" || fail "run() missing executable: $out"
printf '#!/bin/sh\nexit 0\n' > notexec; chmod 644 notexec
out=$("$NIFT" -e "r := run(\"$work/notexec\"); print(r.stringify())" 2>&1)
[ "$out" = "{\"exit_code\":126,\"stdout\":\"\",\"stderr\":\"\",\"launched\":false,\"error\":\"permission denied: $work/notexec\"}" ] \
  && pass "run() non-executable" || fail "run() non-executable: $out"
out=$("$NIFT" -e 'r := run("sh", "-c", "exit 127"); print(r.launched.to_string() + " " + r.exit_code.to_string())' 2>&1)
[ "$out" = "true 127" ] && pass "a launched program exiting 127 is still launched" || fail "launched 127: $out"
# Round 2 (N2): a pipeline's top-level launched/error keep their pre-feature
# meaning; the stage that could not be launched is reported in stages.
out=$("$NIFT" -e 'r := cmd("definitely-not-a-cmd-sitedx").pipe(cmd("cat")).run(); print(r.launched.to_string() + " " + r.stages[0].launched.to_string() + " " + r.stages[0].error)' 2>&1)
[ "$out" = "true false executable not found: definitely-not-a-cmd-sitedx" ] && pass "pipeline: a missing first stage is reported in stages" || fail "pipeline launched: $out"
# Review R6: per-stage results; exit_code keeps the shell convention (last stage).
pipe() { "$NIFT" -e "r := $1.run(); print(r.stringify())" 2>&1; }
out=$(pipe 'cmd("printf", "a").pipe(cmd("cat"))')
[ "$out" = '{"exit_code":0,"stdout":"a","stderr":"","launched":true,"stages":[{"launched":true,"exit_code":0},{"launched":true,"exit_code":0}]}' ] && pass "pipeline: all stages succeed" || fail "pipeline all ok: $out"
out=$(pipe 'cmd("sh", "-c", "exit 3").pipe(cmd("cat"))')
[ "$out" = '{"exit_code":0,"stdout":"","stderr":"","launched":true,"stages":[{"launched":true,"exit_code":3},{"launched":true,"exit_code":0}]}' ] && pass "pipeline: early stage exits nonzero, last succeeds" || fail "pipeline early nonzero: $out"
# Round 2 (N1): the first stage writes nothing, so it can never hit SIGPIPE.
out=$(pipe 'cmd("true").pipe(cmd("definitely-not-a-cmd-sitedx")).pipe(cmd("cat"))')
[ "$out" = '{"exit_code":0,"stdout":"","stderr":"","launched":true,"stages":[{"launched":true,"exit_code":0},{"launched":false,"exit_code":127,"error":"executable not found: definitely-not-a-cmd-sitedx"},{"launched":true,"exit_code":0}]}' ] \
  && pass "pipeline: a middle stage fails to launch" || fail "pipeline launch failure: $out"
out=$(pipe 'cmd("printf", "a").pipe(cmd("sh", "-c", "cat >/dev/null; exit 5"))')
[ "$out" = '{"exit_code":5,"stdout":"","stderr":"","launched":true,"stages":[{"launched":true,"exit_code":0},{"launched":true,"exit_code":5}]}' ] && pass "pipeline: last stage exits nonzero" || fail "pipeline last nonzero: $out"
# Review R6: a missing #! interpreter, and child setup failures.
printf '#!/nonexistent-interp-sitedx\necho hi\n' > badshebang; chmod +x badshebang
out=$("$NIFT" -e "r := run(\"./badshebang\"); print(r.stringify())" 2>&1)
[ "$out" = '{"exit_code":127,"stdout":"","stderr":"","launched":false,"error":"interpreter not found: /nonexistent-interp-sitedx (from ./badshebang)"}' ] && pass "bad shebang names the interpreter" || fail "bad shebang: $out"
out=$("$NIFT" -e 'r := cmd("true").cwd("/nonexistent-dir-sitedx").run(); print(r.launched.to_string() + " " + r.exit_code.to_string() + " " + r.error)' 2>&1)
[ "$out" = "false 126 cannot change directory to /nonexistent-dir-sitedx: No such file or directory" ] && pass "chdir failure reported" || fail "chdir failure: $out"
out=$("$NIFT" -e 'r := cmd("cat").stdin("/nonexistent-in-sitedx").run(); print(r.launched.to_string() + " " + r.exit_code.to_string() + " " + r.error)' 2>&1)
[ "$out" = "false 126 cannot open stdin /nonexistent-in-sitedx: No such file or directory" ] && pass "stdin open failure reported" || fail "stdin failure: $out"
# Command-style statements keep their behaviour: exit 127, script continues.
printf 'print("a")\ndefinitely-not-a-cmd-sitedx arg\nprint("b")\n' > cmdstyle.f
out=$("$NIFT" cmdstyle.f 2>&1); rc=$?
[ $rc -eq 0 ] && [ "$out" = $'a\nb' ] && pass "command-style missing executable unchanged" || fail "command style: rc=$rc out=$out"

# ---------------------------------------------------------------------------
# Items 5 and 6: template warnings, strict values
# ---------------------------------------------------------------------------
new_project "$work/site" || exit 1
cd "$work/site"
"$NIFT" track about About >/dev/null
mkdir -p public/assets
echo 'img' > public/assets/logo.svg
echo 'faq' > content/faq.txt
cat > content/index.html <<'EOF'
@fn(greet()){hi}
<p>A @greet() B</p>
<p>@inptu("templates/head.html") @Input("templates/head.html") @contnet @Content</p>
<p id="u">$[nosuch] $[titel] $[site.titel]</p>
<p>$[title</p>
<p><a href="@path('content/faq.txt')">faq</a> <a href="@path('about')">ok</a> <img src="@path('public/assets/logo.svg')"></p>
EOF
cat > content/about.html <<'EOF'
<!-- legitimate @word and $[ uses: none of these may warn -->
<p>Mail me@example.com or a.b@c.org, ping @nick on chat, version 1.0@beta.</p>
<style>@media (max-width: 600px) { p { color: red } } @media(min-width:1px){} @import url(a.css); @font-face { font-family: x } @supports (display:grid) {} @keyframes spin {} @charset "utf-8"; @layer base; @container (min-width: 1px) {} @page { margin: 1cm } @counter-style x {}</style>
<p>npm i @types/node @babel/core @angular/core; deps "@scope/pkg": "1"</p>
<p>@app.route("/") @pytest.fixture @Override @property @staticmethod @dataclass</p>
<pre>@Component({selector: 'x'}) @Injectable() @Input() name; @inptu("in a code sample") $[nosuch_in_pre]</pre>
<p>Inline <code>@Input() $[inline_code]</code> and <code class="x">@Content()</code>.</p>
<script>class A { @Input() x; @Prop() p; } const t = `$[tpl_in_script]`;</script>
<script type="application/ld+json">{"@context": "https://schema.org", "@type": "Organization"}</script>
<style>.a { color: red } @Media (x) {} /* @Content() */</style>
<p>JSON shown as text: "@context": "https://schema.org", '@type': 'Thing'</p>
<!-- @Content() $[commented_out] -->
<p>Maths: $[n]$ and $[k]$ and $$[x]$$ and the known $[title]$.</p>
```
@Input() fenced $[fenced_value]
```
<p>Interval $[0,1]$ and $[a,b]$; shell echo $[ x + 1 ] and $[$x+1]; \$[title] \$[nosuch] \@input("x")</p>
<p>Mention @input in prose; @if @for @import @while in prose.</p>
EOF
"$NIFT" build --all > ../build1.out 2> ../build1.err; rc=$?
err=$(cat ../build1.err)
[ $rc -eq 0 ] && pass "warnings keep exit status 0" || fail "build exit $rc: $err"
expect_contains "$err" "content/index.html:2:6: @greet(...) is published literally: 'greet' is a function, not a directive; call it as \$[greet(...)] [unknown-directive]" "user function called as directive"
expect_contains "$err" "content/index.html:3:4: unknown directive @inptu published literally (did you mean @input?) [unknown-directive]" "@inptu typo"
expect_contains "$err" "content/index.html:3:34: unknown directive @Input published literally: Nift directives are lowercase; did you mean @input? [unknown-directive]" "@Input case-only match in plain text"
expect_contains "$err" "content/index.html:3:64: unknown directive @contnet published literally (did you mean @content?) [unknown-directive]" "@contnet typo"
expect_contains "$err" "content/index.html:3:73: unknown directive @Content published literally: Nift directives are lowercase; did you mean @content?" "@Content case"
expect_contains "$err" "content/index.html:4:11: unknown value \$[nosuch] published literally; write \\\$[ for literal text [unknown-value]" "unknown root"
expect_contains "$err" "content/index.html:4:21: unknown value \$[titel] published literally (did you mean \$[title]?)" "unknown root suggestion"
expect_contains "$err" "content/index.html:4:30: unknown value \$[site.titel] published literally" "unknown root member path"
expect_contains "$err" "content/index.html:5:4: unterminated \$[title has no closing ']' and is published literally" "unterminated \$[ in a template warns"
expect_contains "$err" "content/index.html:6:13: @path('content/faq.txt') resolves to '../content/faq.txt', outside the output directory public/" "@path outside output"
expect_contains "$err" "    <p>@inptu(\"templates/head.html\")" "excerpt line"
warnings_only=$(grep '^warning: ' ../build1.err)
expect_absent "$warnings_only" "about.html" "legitimate @word / \$[ uses do not warn"
expect_absent "$warnings_only" "logo.svg" "@path under the output directory does not warn"
expect_absent "$warnings_only" "@path('about')" "@path to a tracked name does not warn"
n=$(grep -c '^warning: ' ../build1.err)
[ "$n" -eq 10 ] && pass "exactly 10 warnings" || fail "warning count $n"
grep -qF '<p>Maths: $[n]$ and $[k]$ and $$[x]$$ and the known About$.</p>' public/about.html \
  && pass "\$[name]\$ maths left literal; a resolved name still substitutes" || { fail "maths output"; grep Maths public/about.html >&2; }
grep -qF '<p>@inptu("templates/head.html") @Input("templates/head.html") @contnet @Content</p>' public/index.html \
  && grep -qF '<p id="u">$[nosuch] $[titel] $[site.titel]</p>' public/index.html \
  && grep -qF '<p>$[title</p>' public/index.html && pass "default output unchanged (literal passthrough)" || fail "passthrough output"
grep -qF '<p>Interval $[0,1]$ and $[a,b]$; shell echo $[ x + 1 ] and $[$x+1]; $[title] $[nosuch] @input("x")</p>' public/about.html \
  && pass "legitimate passthrough output" || { fail "about output"; grep Interval public/about.html >&2; }
cp public/index.html ../index.warned.html

# Silencing: CLI and config; output identical, exit status unchanged.
"$NIFT" build --all --no-warn=all > /dev/null 2> ../build2.err; rc=$?
[ $rc -eq 0 ] && [ ! -s ../build2.err ] && pass "--no-warn=all silences everything" || fail "--no-warn=all: rc=$rc $(cat ../build2.err)"
cmp -s public/index.html ../index.warned.html && pass "warnings never change output" || fail "output differs with --no-warn"
"$NIFT" build --all --no-warn=unknown-directive --no-warn=unknown-value > /dev/null 2> ../build3.err
[ "$(grep -c '^warning: ' ../build3.err)" -eq 1 ] && grep -q '\[path-outside-output\]' ../build3.err && pass "--no-warn=<kind> per kind" || fail "per-kind silencing: $(cat ../build3.err)"
"$NIFT" build --no-warn=bogus > /dev/null 2> ../build4.err; rc=$?
[ $rc -ne 0 ] && grep -q "unknown warning kind 'bogus'" ../build4.err && pass "unknown --no-warn kind rejected" || fail "bogus kind rc=$rc"
cp .nift/config.json ../config.json.orig
python3 - <<'PY'
import json; p='.nift/config.json'; d=json.load(open(p)); d['config']['no-warn']=['path-outside-output','unknown-directive']; json.dump(d,open(p,'w'),indent=2)
PY
"$NIFT" build --all > /dev/null 2> ../build5.err
[ "$(grep -c '^warning: ' ../build5.err)" -eq 4 ] && ! grep -q 'path-outside-output\|unknown-directive' ../build5.err && pass "\"no-warn\" config key" || fail "config no-warn: $(cat ../build5.err)"
python3 - <<'PY'
import json; p='.nift/config.json'; d=json.load(open(p)); d['config']['no-warn']=['typo']; json.dump(d,open(p,'w'),indent=2)
PY
out=$("$NIFT" build 2>&1); rc=$?
[ $rc -ne 0 ] && grep -q 'unknown no-warn entry' <<<"$out" && pass "invalid no-warn entry rejected" || fail "invalid no-warn: $out"
cp ../config.json.orig .nift/config.json

# Item 6(b): strict values, opt in by flag or config; error with location and caret.
sed -i 's/^<p>\$\[title<\/p>$/<p>\\$[title<\/p>/' content/index.html
"$NIFT" build --all --strict > ../strict.out 2> ../strict.err; rc=$?
[ $rc -ne 0 ] && pass "--strict fails the build" || fail "--strict exit $rc"
expect_contains "$(cat ../strict.err)" "content/index.html:4:11" "strict location"
expect_contains "$(cat ../strict.err)" "unknown value \$[nosuch]; strict-values is on: write \\\$[nosuch] to publish it literally" "strict message"
expect_contains "$(cat ../strict.err)" "    <p id=\"u\">\$[nosuch] \$[titel] \$[site.titel]</p>" "strict excerpt"
expect_contains "$(cat ../strict.err)" "              ^~~~~~~~~" "strict caret"
"$NIFT" build --repair > /dev/null 2>&1
python3 - <<'PY'
import json; p='.nift/config.json'; d=json.load(open(p)); d['config']['strict-values']=True; json.dump(d,open(p,'w'),indent=2)
PY
out=$("$NIFT" build --all 2>&1); rc=$?
[ $rc -ne 0 ] && grep -q 'unknown value \$\[nosuch\]; strict-values is on' <<<"$out" && pass "\"strict-values\": true" || fail "strict config: $out"
printf '<p>$[title] \\$[nosuch] $[0,1]$ $[ x + 1 ]</p>\n' > content/index.html
# about.html has unknown values inside <pre>/<script>/comments, which strict
# rejects (strict is context-free); make it clean for this check.
printf '<p>About</p>\n' > content/about.html
out=$("$NIFT" build 2>&1); rc=$?
[ $rc -eq 0 ] && grep -qF '<p>index $[nosuch] $[0,1]$ $[ x + 1 ]</p>' public/index.html && pass "strict leaves escapes and non-identifier text alone" || fail "strict passthrough: rc=$rc $out"
python3 - <<'PY'
import json; p='.nift/config.json'; d=json.load(open(p)); d['config']['strict-values']='yes'; json.dump(d,open(p,'w'),indent=2)
PY
out=$("$NIFT" build 2>&1); rc=$?
[ $rc -ne 0 ] && grep -q 'strict-values must be true or false' <<<"$out" && pass "strict-values type checked" || fail "strict type: $out"
cp ../config.json.orig .nift/config.json
grep -q 'strict-values' .nift/config.json && fail "init must not enable strict-values" || pass "new projects are not strict"

# Pagination pages share the page's warnings.
new_project "$work/pg" || exit 1
cd "$work/pg"
python3 - <<'PY'
import json; p='.nift/tracked.json'; d=json.load(open(p))
d['tracked'][0]['paginate']={'items-per-page':1}
json.dump(d,open(p,'w'),indent=2)
PY
cat > content/index.html <<'EOF'
@item{<li>a</li>}
@item{<li>b</li>}
@paginate
EOF
printf '<ul>$[paginate.items]</ul> $[paginaet.total]\n' > content/index.paginate.html
out=$("$NIFT" build --all 2>&1); rc=$?
[ $rc -eq 0 ] && pass "paginated build" || fail "paginated build: $out"
expect_contains "$out" "content/index.paginate.html:1:28: unknown value \$[paginaet.total] published literally (did you mean \$[paginate.total]?)" "pagination template warning"
[ "$(grep -c '^warning: ' <<<"$out")" -eq 1 ] && pass "pagination warning reported once" || fail "pagination warning count: $out"

# A warning in a shared template is reported once per build, not per page.
new_project "$work/shared" || exit 1
cd "$work/shared"
"$NIFT" track about About >/dev/null; "$NIFT" track contact Contact >/dev/null
sed -i 's#<body>#<body><footer>$[sitename]</footer>#' templates/template.html
out=$("$NIFT" build --all 2>&1); rc=$?
[ $rc -eq 0 ] && [ "$(grep -c '^warning: templates/template.html:.*\$\[sitename\]' <<<"$out")" -eq 1 ] \
  && pass "shared-template warning reported once" || fail "shared-template warning: $out"

# Review R4 control: a case-only @Content() in plain template text warns.
printf '<p>@Content()</p>\n' > content/about.html
out=$("$NIFT" build 2>&1)
expect_contains "$out" "content/about.html:1:4: unknown directive @Content published literally: Nift directives are lowercase; did you mean @content?" "@Content() in plain text warns"

# Review R1: toggling strict (CLI and config) with no source edits; build,
# status and exit codes agree at every step.
new_project "$work/toggle" || exit 1
cd "$work/toggle"
"$NIFT" track about About >/dev/null
printf '<p>$[nosuch_toggle]</p>\n' > content/about.html
"$NIFT" build --all >/dev/null 2>&1
set_strict() { python3 - "$1" <<'PY'
import json,sys; p='.nift/config.json'; d=json.load(open(p))
if sys.argv[1]=='on': d['config']['strict-values']=True
else: d['config'].pop('strict-values',None)
json.dump(d,open(p,'w'),indent=2)
PY
}
check_step() { # label build-args status-args expect(fail|ok)
  local b s rb rs
  s=$("$NIFT" status $3 2>&1); rs=$?
  b=$("$NIFT" build $2 2>&1); rb=$?
  if [ "$4" = fail ]; then
    [ $rb -ne 0 ] && grep -q 'strict-values is on' <<<"$b" && grep -q 'strict-values is on and the page published unknown' <<<"$s" \
      && pass "toggle $1: status reports, build fails" || fail "toggle $1: rb=$rb build=$b status=$s"
  else
    # After a failed strict build, build recovery (D-020) records the page as
    # failed, so status lists it once ("previous build of this page failed")
    # and the next ordinary build rebuilds it successfully; afterwards both
    # agree that everything is up to date.
    local s2; s2=$("$NIFT" status 2>&1)
    { grep -q 'up to date' <<<"$s" || grep -q 'previous build of this page failed' <<<"$s"; } && [ $rb -eq 0 ] && grep -q 'up to date' <<<"$s2" \
      && pass "toggle $1: build ok, then status up to date" || fail "toggle $1: rb=$rb build=$b status=$s after=$s2"
  fi
}
check_step "cli on" "--strict" "--strict" fail
check_step "cli off" "" "" ok
check_step "cli on again" "--strict" "--strict" fail
check_step "cli off again" "" "" ok
set_strict on;  check_step "config on" "" "" fail
set_strict off; check_step "config off" "" "" ok
set_strict on;  check_step "config on again" "" "" fail
set_strict off; check_step "config off again" "" "" ok
out=$("$NIFT" status 2>&1)
expect_contains "$out" "outstanding build warning" "status lists outstanding warnings"
out=$("$NIFT" build 2>&1)
expect_contains "$out" "1 earlier warning in 1 unchanged page: about (nift status lists them)" "build summarises earlier warnings"

# ---------------------------------------------------------------------------
# Round 2 (B1): strict never trusts missing or invalid metadata.
# ---------------------------------------------------------------------------
new_project "$work/b1" || exit 1
cd "$work/b1"
"$NIFT" track dirty Dirty >/dev/null; "$NIFT" track clean Clean >/dev/null
printf '<p>$[nosuch_b1]</p>\n' > content/dirty.html
printf '<p>$[title]</p>\n' > content/clean.html
"$NIFT" build --all --no-warn=all >/dev/null 2>&1
grep -q '"site-unknown-values": 0' .nift/public/clean.info.json && pass "B1: a clean page records 0" || fail "B1: 0 not recorded: $(grep site-unknown .nift/public/clean.info.json)"
edit_field() { # page mode(remove|text)
  python3 - "$1" "$2" <<'PY'
import json,sys; p='.nift/public/%s.info.json' % sys.argv[1]; d=json.load(open(p))
if sys.argv[2]=='remove': d.pop('site-unknown-values',None)
else: d['site-unknown-values']='zero'
json.dump(d,open(p,'w'),indent=2)
PY
}
for mode in remove text; do
  edit_field dirty $mode; edit_field clean $mode
  out=$("$NIFT" status 2>&1)
  grep -q 'up to date' <<<"$out" && pass "B1 $mode: without strict a missing/invalid field changes nothing" || fail "B1 $mode: non-strict status: $out"
  out=$("$NIFT" status --strict 2>&1)
  grep -q 'needs verification' <<<"$out" && ! grep -q 'all .* up to date' <<<"$out" && pass "B1 $mode: status --strict lists unverified pages" || fail "B1 $mode: status --strict: $out"
  out=$("$NIFT" build --strict 2>&1); rc=$?
  [ $rc -ne 0 ] && grep -q 'unknown value $\[nosuch_b1\]' <<<"$out" && pass "B1 $mode: build --strict verifies and fails the dirty page" || fail "B1 $mode: rc=$rc $out"
  grep -q '"site-unknown-values": 0' .nift/public/clean.info.json && pass "B1 $mode: the clean page was verified" || fail "B1 $mode: clean page not re-verified"
  "$NIFT" build --all --no-warn=all >/dev/null 2>&1
done
if [ -n "${NIFT_OLD_BIN:-}" ]; then
  "$NIFT_OLD_BIN" build --all >/dev/null 2>&1
  out=$("$NIFT" build --strict 2>&1); rc=$?
  [ $rc -ne 0 ] && pass "B1: metadata written by NIFT_OLD_BIN is not trusted under strict" || fail "B1 old binary: rc=$rc $out"
fi

# ---------------------------------------------------------------------------
# Round 2 (B2): strict follows Nift's actual substitution; contexts only quiet
# warnings; badly closed markup and @input files do not suppress anything.
# ---------------------------------------------------------------------------
new_project "$work/b2" || exit 1
cd "$work/b2"
for page in pre scr com code inc b c d typos unterm; do "$NIFT" track $page $page >/dev/null; done
printf '<pre>title=$[title] unknown=$[nosuch_pre]</pre>\n' > content/pre.html
printf '<script>var t="$[title]"; var u="$[nosuch_script]";</script>\n' > content/scr.html
printf '<!-- t=$[title] u=$[nosuch_comment] -->\n' > content/com.html
printf '<p><code>t=$[title] u=$[nosuch_code]</code></p>\n' > content/code.html
printf '<p>inc: $[nosuch_inc]</p>\n' > inc.txt
printf '@input("inc.txt")\n' > content/inc.html
printf '<h1>Writing posts</h1>\n<pre>\n```js\nconsole.log(1)\n</pre>\n<p>Welcome to $[titel]</p>\n' > content/b.html
printf '<p>a <code>x</p>\n<p>$[titel]</p>\n' > content/c.html
printf '<span><code>snippet</span>\n' > snip.txt
printf '@input("snip.txt")\n<p>$[titel]</p>\n' > content/d.html
printf '<p>@inptu("x") @Content() @patch("x") @Path("/u") @contnet</p>\n' > content/typos.html
printf '<p>$[title</p>\n' > content/unterm.html
out=$("$NIFT" build --all 2>&1); rc=$?
[ $rc -eq 0 ] && pass "B2: default build succeeds" || fail "B2 default: $out"
for name in nosuch_pre nosuch_script nosuch_comment nosuch_code; do
  grep -q "\$\[$name\]" <<<"$(grep '^warning' <<<"$out")" && fail "B2: warning inside a closed context ($name)" || pass "B2: no warning inside a closed context ($name)"
done
expect_contains "$out" "inc.txt:1:9: unknown value \$[nosuch_inc]" "B2: @input file value warns"
expect_contains "$out" "content/b.html:6:15: unknown value \$[titel]" "B2: unpaired fence does not silence the page"
expect_contains "$out" "content/c.html:2:4: unknown value \$[titel]" "B2: unclosed <code> does not silence the page"
expect_contains "$out" "content/d.html:2:4: unknown value \$[titel]" "B2: <code> left open in an @input file does not leak"
for page in pre scr com code inc b c d; do
  o=$("$NIFT" build $page --strict 2>&1); r=$?
  [ $r -ne 0 ] && grep -q 'strict-values is on: write \\\$\[' <<<"$o" && pass "B2 strict: $page fails" || fail "B2 strict: $page rc=$r $o"
done
o=$("$NIFT" build pre --strict 2>&1)
expect_contains "$o" 'unknown value $[nosuch_pre]; strict-values is on: write \$[nosuch_pre] to publish it literally' "B2: strict message names the exact escape"
o=$("$NIFT" build typos unterm --strict 2>&1); r=$?
[ $r -eq 0 ] && pass "B2: directive near-misses and unterminated \$[ are never fatal under strict" || fail "B2: typos/unterm strict rc=$r $o"
"$NIFT" build --repair >/dev/null 2>&1

# ---------------------------------------------------------------------------
# Round 2 (N3): context detection is linear in the number of candidates.
# ---------------------------------------------------------------------------
new_project "$work/n3" || exit 1
cd "$work/n3"
"$NIFT" track small Small >/dev/null; "$NIFT" track big Big >/dev/null
python3 - <<'PY'
def page(n): return ''.join('<p>v $[u%d] <code>c</code></p>\n' % i for i in range(n))
open('content/small.html','w').write(page(5000))
open('content/big.html','w').write(page(20000))
PY
t0=$(date +%s%N); "$NIFT" build small --no-warn=all >/dev/null 2>&1; t1=$(date +%s%N)
"$NIFT" build big --no-warn=all >/dev/null 2>&1; t2=$(date +%s%N)
small=$(( (t1 - t0) / 1000000 )); big=$(( (t2 - t1) / 1000000 ))
[ "$big" -lt $(( small * 8 + 500 )) ] && [ "$big" -lt 30000 ] && pass "N3: 5,000 -> 20,000 candidates: ${small} ms -> ${big} ms (linear)" || fail "N3: not linear: ${small} ms -> ${big} ms"

# ---------------------------------------------------------------------------
# YAML flow list: the error names the front matter line
# ---------------------------------------------------------------------------
new_project "$work/yaml" || exit 1
cd "$work/yaml"
mkdir -p model
printf '@schema(post) {\n title: string\n tags: string[]\n}\n' > model/post.schema
python3 - <<'PY'
import json; p='.nift/config.json'; d=json.load(open(p)); d['config']['schemas']=['model/post.schema']; json.dump(d,open(p,'w'),indent=2)
PY
"$NIFT" track post Post >/dev/null 2>&1
printf -- '---\ntype: post\ntitle: Hi\ntags: [a, b]\n---\n<p>x</p>\n' > content/post.html
out=$("$NIFT" build --all 2>&1); rc=$?
if grep -q "expected array" <<<"$out"; then
  expect_contains "$out" "front matter line 4: schema post field 'tags': expected array (YAML flow lists such as [a, b] are not supported in front matter" "YAML flow list error has a line number"
else
  echo "note: schema fixture not accepted; output: $out" >&2
  fail "YAML flow list fixture"
fi

# ---------------------------------------------------------------------------
# Item 1: build --auto survives errors and recovers
# ---------------------------------------------------------------------------
new_project "$work/auto" || exit 1
cd "$work/auto"
"$NIFT" track about About >/dev/null
echo '<h1>About v1</h1>' > content/about.html
"$NIFT" build --all >/dev/null 2>&1
"$NIFT" build --auto > ../auto.out 2> ../auto.err < /dev/null &
auto_pid=$!
wait_for() { # condition-command timeout-tenths
  local n=0; while [ $n -lt "$2" ]; do eval "$1" 2>/dev/null && return 0; sleep 0.1; n=$((n + 1)); done; return 1
}
wait_for 'grep -q "non-interactive" ../auto.out' 50 || fail "auto did not start"
sleep 0.5
echo '<h1>About v2</h1>@input("templates/missing.html")' > content/about.html
wait_for 'grep -q "@input path does not exist" ../auto.err' 80 && pass "auto prints the build error to stderr" || fail "auto error not printed: $(cat ../auto.err)"
grep -q "build failed; still watching" ../auto.err && pass "auto says it keeps watching" || fail "auto banner"
grep -q "@input path does not exist" .nift/build-auto.log && pass "error also in build-auto.log" || fail "log"
sleep 1
kill -0 "$auto_pid" 2>/dev/null && pass "auto stays alive after an error" || fail "auto exited"
[ "$(grep -c '@input path does not exist' ../auto.err)" -eq 1 ] && pass "unchanged failure printed once" || fail "repeated failure output"
echo '<h1>About v3</h1>' > content/about.html
wait_for 'grep -q "About v3" public/about.html' 80 && pass "auto recovers when the error is fixed" || fail "auto did not rebuild after fix"
wait_for 'grep -q "build succeeded again" ../auto.err' 30 && pass "auto reports recovery" || fail "recovery message"
# End to end with build recovery (D-020): an ordinary page error while other
# pages build fine never needs --repair; the watcher never runs it (a
# "pre build --repair" hook would record it), the fixed build matches a clean
# build, and status shows no failed pages.
kill "$auto_pid" 2>/dev/null; wait "$auto_pid" 2>/dev/null
python3 - <<'PY'
import json; p='.nift/config.json'; d=json.load(open(p)); d['config']['pre build --repair']='repair-ran.f'
json.dump(d,open(p,'w'),indent=2)
PY
printf 'run("sh", "-c", "echo repair >> repair-ran.log")\n' > repair-ran.f
"$NIFT" build --auto > ../auto2.out 2> ../auto2.err < /dev/null &
auto_pid=$!
wait_for 'grep -q "non-interactive" ../auto2.out' 50
echo '<h1>Home v2</h1>' > content/index.html
echo '<h1>About v4</h1>@input("nope.html")' > content/about.html
wait_for 'grep -q "nope.html" ../auto2.err' 100 && pass "recovery: page error observed" || fail "recovery: error not reported"
[ ! -e .nift/.unfinished ] && pass "recovery: an ordinary page error leaves no .unfinished marker" || fail "recovery: marker left"
wait_for 'grep -q "Home v2" public/index.html' 50 && pass "recovery: the other page was built" || fail "recovery: index not built"
echo '<h1>About v5</h1>' > content/about.html
wait_for 'grep -q "About v5" public/about.html' 100 && pass "recovery: fixed page rebuilt by --auto" || fail "recovery: fix not built"
wait_for 'grep -q "build succeeded again" ../auto2.err' 50 && pass "recovery: success reported" || fail "recovery: no success message"
[ ! -e repair-ran.log ] && ! grep -q 'repair' ../auto2.err .nift/build-auto.log && pass "recovery: no --repair path ran" || fail "recovery: repair ran ($(cat repair-ran.log 2>/dev/null))"
kill "$auto_pid" 2>/dev/null; wait "$auto_pid" 2>/dev/null; auto_pid=""
out=$("$NIFT" status 2>&1)
grep -q 'up to date' <<<"$out" && ! grep -q 'previous build of this page failed' <<<"$out" && pass "recovery: status shows no failed pages" || fail "recovery: status: $out"
python3 - <<'PY' && pass "recovery: no failed pages recorded" || fail "recovery: failed.json not empty"
import json,os,sys
p='.nift/failed.json'
sys.exit(0 if not os.path.exists(p) or not json.load(open(p)).get('failed') else 1)
PY
rm -rf ../auto-clean && cp -a . ../auto-clean && (cd ../auto-clean && rm -f public/*.html && "$NIFT" build --all >/dev/null 2>&1)
diff -r public ../auto-clean/public >/dev/null && pass "recovery: output matches a clean build" || fail "recovery: output differs from a clean build: $(diff -r public ../auto-clean/public | head -5)"
"$NIFT" build --auto > ../auto.out 2> ../auto.err < /dev/null &
auto_pid=$!
wait_for 'grep -q "non-interactive" ../auto.out' 50
kill -0 "$auto_pid" 2>/dev/null && pass "auto still alive" || fail "auto exited later"
kill "$auto_pid" 2>/dev/null; wait "$auto_pid" 2>/dev/null; auto_pid=""

# Review D1: whatever the output-dir spelling, a pre-build hook that writes
# into the output directory (or the project) while a page fails must not make
# the watcher loop: at most the failed build plus one settling retry run.
auto_variant() { # label output-dir-value output-dir-filesystem-path
  local dir="$work/auto-$1" out_fs
  new_project "$dir" || return
  cd "$dir"
  out_fs=$3; [ -z "$out_fs" ] && out_fs="$dir/public"
  if [ "$1" = symlink ]; then mkdir -p real; ln -s real site; fi
  mkdir -p "$out_fs"
  python3 - "$2" <<'PY'
import json,sys; p='.nift/config.json'; d=json.load(open(p))
d['config']['output-dir']=sys.argv[1]; d['config']['pre build']='hook.f'
json.dump(d,open(p,'w'),indent=2)
PY
  printf 'run("sh", "-c", "echo run >> \\"%s/hook.log\\"; date +%%%%N > \\"%s/generated.txt\\"")\n' "$out_fs" "$dir" > hook.f
  "$NIFT" build --all >/dev/null 2>&1 || { fail "D1 $1: initial build ($(cat .nift/config.json))"; cd "$work"; return; }
  "$NIFT" build --auto > ../auto-$1.out 2> ../auto-$1.err < /dev/null &
  auto_pid=$!
  wait_for "grep -q non-interactive ../auto-$1.out" 50
  sleep 0.5
  echo '@input("missing.html")' > content/index.html
  wait_for "grep -q 'build failed' ../auto-$1.err" 80 || fail "D1 $1: failure not reported"
  sleep 0.6
  local before; before=$(grep -c . "$out_fs/hook.log")
  sleep 2
  local after; after=$(grep -c . "$out_fs/hook.log")
  [ "$after" -le "$((before + 1))" ] && pass "D1 $1: no rebuild loop while failing ($before -> $after hook runs)" || fail "D1 $1: watcher loops ($before -> $after)"
  [ "$(grep -c 'build failed; still watching' ../auto-$1.err)" -eq 1 ] && pass "D1 $1: failure banner printed once" || fail "D1 $1: banner repeated $(grep -c 'build failed' ../auto-$1.err)x"
  echo '<p>fixed</p>' > content/index.html
  wait_for "grep -q fixed '$out_fs/index.html'" 80 && pass "D1 $1: recovers" || fail "D1 $1: no recovery"
  kill "$auto_pid" 2>/dev/null; wait "$auto_pid" 2>/dev/null; auto_pid=""
  cd "$work"
}
auto_variant public public ""
auto_variant slash public/ ""
auto_variant absolute "$work/auto-absolute/public" ""
mkdir -p "$work/outside-out"
auto_variant outside "$work/outside-out" "$work/outside-out"
auto_variant symlink site "$work/auto-symlink/real"

# ---------------------------------------------------------------------------
# Round 2 (B3): the watcher never loses a user edit and never loops on the
# build's own writes. Deterministic window: pages build in order with one
# thread; the second page ("zslow") has a per-page pre-build hook that sleeps
# AFTER the edited page ("/") has been read, and it is rebuilt every build
# because it includes public/stamp.txt, which the project pre-build hook
# rewrites (the output directory is not watched). The project hook can also
# write a watched file (scenarios 5 and 6).
# ---------------------------------------------------------------------------
watch_start() { # name extra-project-hook-command
  local dir="$work/watch-$1"
  new_project "$dir" || return 1
  cd "$dir"
  mkdir -p public
  "$NIFT" track zslow Zslow >/dev/null
  python3 - <<'PY'
import json; p='.nift/config.json'; d=json.load(open(p))
d['config']['pre build']='hook.f'; d['config']['build-threads']=1
json.dump(d,open(p,'w'),indent=2)
PY
  printf 'run("sh", "-c", "date +%%%%N > public/stamp.txt; %s")\n' "$2" > hook.f
  printf 'r := run("sh", "-c", "echo start >> public/hook.log; sleep 1; echo end >> public/hook.log")\n' > content/zslow.pre-build.f
  echo '<p>slow</p>@input("public/stamp.txt")' > content/zslow.html
  echo '<p>v0</p>' > content/index.html
  "$NIFT" build --all >/dev/null 2>&1 || fail "watch $1: initial build: $("$NIFT" build --all 2>&1 | head -5)"
  "$NIFT" build --auto > ../watch-$1.out 2> ../watch-$1.err < /dev/null &
  auto_pid=$!
  wait_for "grep -q non-interactive ../watch-$1.out" 50
}
watch_stop() { kill "$auto_pid" 2>/dev/null; wait "$auto_pid" 2>/dev/null; auto_pid=""; cd "$work"; }
starts() { if [ -f public/hook.log ]; then awk '/start/{n++} END{print n+0}' public/hook.log; else echo 0; fi; }
wait_new_start() { # wait until a build starts after the current count
  local n; n=$(starts); wait_for "[ \$(starts) -gt $n ]" 100
}
bounded_while_failing() { # label
  sleep 3; local a; a=$(starts); sleep 3; local b; b=$(starts)
  [ "$b" -eq "$a" ] && pass "$1: no rebuild loop while failing ($a -> $b builds)" || fail "$1: watcher keeps rebuilding ($a -> $b)"
}

# 1. Fix saved while the failing rebuild is still running (after it read the
# page): the build that started after the error was saved reads the error,
# and the fix lands during its slow second page.
watch_start fixduring 'true'
echo '@input("missing.html")' > content/index.html
wait_new_start; echo '<p>fixed-1</p>' > content/index.html
wait_for 'grep -q fixed-1 public/index.html' 150 && pass "watch 1: a fix saved during the failing rebuild is built" || fail "watch 1: fix lost ($(cat public/index.html))"
watch_stop

# 2. Fix saved during the follow-up retry (review repro w1).
watch_start fixretry 'true'
echo '@input("missing.html")' > content/index.html
wait_for "grep -q 'build failed' ../watch-fixretry.err" 150
wait_new_start; echo '<p>fixed-2</p>' > content/index.html
wait_for 'grep -q fixed-2 public/index.html' 150 && pass "watch 2: a fix saved during the follow-up retry is built" || fail "watch 2: fix lost ($(cat public/index.html))"
watch_stop

# 3. Several saves within one build; the last one is the fix.
watch_start rapid 'true'
echo '@input("missing.html")' > content/index.html
wait_for "grep -q 'build failed' ../watch-rapid.err" 150
wait_new_start
echo '@input("missing-a.html")' > content/index.html; sleep 0.1
echo '@input("missing-b.html")' > content/index.html; sleep 0.1
echo '<p>fixed-3</p>' > content/index.html
wait_for 'grep -q fixed-3 public/index.html' 150 && pass "watch 3: the last of rapid saves is built" || fail "watch 3: ($(cat public/index.html))"
watch_stop

# 4. Atomic editor saves (write a temp file, rename over the page), during a
# build and between builds.
watch_start atomic 'true'
echo '@input("missing.html")' > content/index.html
wait_for "grep -q 'build failed' ../watch-atomic.err" 150
sleep 3   # idle in the failed state: no build running
printf '<p>between</p>\n@input("missing2.html")\n' > content/.index.tmp && mv content/.index.tmp content/index.html
wait_for "grep -q missing2 ../watch-atomic.err" 150 && pass "watch 4: atomic save between builds triggers a build" || fail "watch 4: atomic save between builds ignored"
wait_new_start
printf '<p>fixed-4</p>\n' > content/.index.tmp && mv content/.index.tmp content/index.html
wait_for 'grep -q fixed-4 public/index.html' 150 && pass "watch 4: atomic save during a build is built" || fail "watch 4: atomic save lost ($(cat public/index.html))"
watch_stop

# 5. A hook that rewrites unchanged content does not loop.
watch_start same 'echo same > generated.txt'
echo '@input("missing.html")' > content/index.html
wait_for "grep -q 'build failed' ../watch-same.err" 150
bounded_while_failing "watch 5"
grep -q 'modifies watched files' ../watch-same.err && fail "watch 5: same-content rewrite reported as a loop" || pass "watch 5: same-content rewrite not reported"
echo '<p>fixed-5</p>' > content/index.html
wait_for 'grep -q fixed-5 public/index.html' 150 && pass "watch 5: recovers" || fail "watch 5: no recovery"
watch_stop

# 6. A hook that writes new content on every build is reported once.
watch_start fresh 'date +%N > generated.txt'
echo '@input("missing.html")' > content/index.html
wait_for "grep -q 'build failed' ../watch-fresh.err" 150
bounded_while_failing "watch 6"
[ "$(grep -c 'a build hook modifies watched files on every build (generated.txt)' ../watch-fresh.err)" -eq 1 ] \
  && pass "watch 6: hook rewriting watched files reported once" || fail "watch 6: report: $(grep -c 'modifies watched' ../watch-fresh.err)"
echo '<p>fixed-6</p>' > content/index.html
wait_for 'grep -q fixed-6 public/index.html' 150 && pass "watch 6: a user edit still triggers a build" || fail "watch 6: no recovery"
watch_stop

if [ $failures -ne 0 ]; then echo "site-dx: $failures failure(s)" >&2; exit 1; fi
echo "site-dx: all checks passed"
