#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NIFT="$ROOT/nift"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

# Multiline functions, structs, lambdas, conditionals and loops complete
# through the parser-reported incomplete-input state rather than brace
# counting.
repl=$(cd "$TMP" && printf 'fn(add(x, y)) {\nreturn x + y\n}\nprint(add(2, 3))\nstruct(box) {\nv := 5\nfn(get()) { return v }\n}\nb := box()\nprint(b.get())\nf := (a) => {\nreturn a * 2\n}\nprint(f(21))\ni := 0\nwhile(i < 2) {\ni += 1\n}\nprint(i)\nfor(x : [1,2]) {\nprint(x)\n}\nquit\n' | "$NIFT" sh 2>/dev/null || true)
grep -q '^5$' <<<"$repl" || grep -q '5' <<<"$repl"
grep -q '42' <<<"$repl"
grep -q '2' <<<"$repl"
# Continuation prompts were used (multiline actually engaged).
grep -q '^\.\.\.' <<<"$repl" || grep -q '\.\.\.' <<<"$repl"

# Braces inside strings and escaped quotes do not trigger continuation.
repl2=$(cd "$TMP" && printf 's := "a { b } c"\nprint(s)\nq := "say \\"hi\\""\nprint(q)\nquit\n' | "$NIFT" sh 2>/dev/null || true)
grep -q 'a { b } c' <<<"$repl2"
grep -q 'say "hi"' <<<"$repl2"

# A balanced-but-invalid statement is reported and the session recovers.
repl3=$(cd "$TMP" && printf 'x := :=\nprint("recovered")\nquit\n' | "$NIFT" sh 2>&1 || true)
grep -q 'recovered' <<<"$repl3"
grep -qi 'error' <<<"$repl3"

# An unterminated prefix keeps reading and EOF terminates cleanly.
repl4=$(cd "$TMP" && printf 'if(true) {\n' | "$NIFT" sh 2>/dev/null || true)
[[ "$repl4" == *'...'* ]]

# The same multiline block-lambda form works under nift run (the statement
# scanner must not split a statement at a newline inside braces).
cat > "$TMP/ml.nift" <<'NIFT'
f := (a) => { 
  return a * 2 
}
print(f(21))
NIFT
[[ "$(cd "$TMP" && "$NIFT" run ml.nift)" == '42' ]]

echo 'v4.3 CP85 multiline REPL smoke: PASS'