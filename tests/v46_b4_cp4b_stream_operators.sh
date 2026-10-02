#!/usr/bin/env bash
set -euo pipefail

NIFT=${NIFT_BIN:-${NIFT:-./nift}}
case "$NIFT" in /*) ;; *) NIFT="$(pwd)/$NIFT" ;; esac
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/wd"
cd "$TMP/wd"

# ---------------------------------------------------------------- << insertion
# ofstream(path) << scalar
[[ "$("$NIFT" -e 'o := ofstream("r.txt"); o << "hello"; o.close(); print(open("r.txt"))')" == 'hello' ]]
# default ofstream + open + <<
[[ "$("$NIFT" -e 'o := ofstream(); o.open("r.txt"); o << "data"; o.close(); print(open("r.txt"))')" == 'data' ]]
# chained << with mixed scalars
[[ "$("$NIFT" -e 'o := ofstream("r.txt"); o << "a" << " " << 42 << " " << true; o.close(); print(open("r.txt"))')" == 'a 42 true' ]]
# explicit write() remains available and equivalent
[[ "$("$NIFT" -e 'o := ofstream("r.txt"); o.write("x"); o.write_line("y"); o.close(); print(open("r.txt"))')" == $'xy' ]]

# ---------------------------------------------------------------- >> extraction
printf 'hello 42\nworld 7\n' > d.txt
# ifstream(path) >> string and number
[[ "$("$NIFT" -e 's := ""; i := 0; f := ifstream("d.txt"); f >> s >> i; print(s); print(i); f.close()')" == $'hello\n42' ]]
# default ifstream + open + >>
[[ "$("$NIFT" -e 's := ""; f := ifstream(); f.open("d.txt"); f >> s; f.close(); print(s)')" == 'hello' ]]
# chained >> across whitespace tokens
printf 'a b c' > c.txt
[[ "$("$NIFT" -e 'a := ""; b := ""; c := ""; f := ifstream("c.txt"); f >> a >> b >> c; print(a + "," + b + "," + c); f.close()')" == 'a,b,c' ]]
# bool extraction
printf 'true false' > bool.txt
[[ "$("$NIFT" -e 'x := false; y := true; f := ifstream("bool.txt"); f >> x >> y; print(x); print(y); f.close()')" == $'true\nfalse' ]]

# ---------------------------------------------------------------- EOF behavior
# EOF before another token leaves the destination unchanged (not read_failed)
printf 'one' > eof.txt
[[ "$("$NIFT" -e 'a := ""; b := "sentinel"; f := ifstream("eof.txt"); f >> a; f >> b; print(a); print(b); f.close()')" == $'one\nsentinel' ]]
[[ "$("$NIFT" -e 'i := 0; f := ifstream("/dev/null"); f >> i; print(i); f.close()')" == '0' ]]

# ---------------------------------------------------------------- partial-chain semantics
# a assigned, b conversion fails (not committed), c untouched
printf '12 abc 99' > chain.txt
if "$NIFT" -e 'a := 0; b := 7; c := 8; f := ifstream("chain.txt"); f >> a >> b >> c' >"$TMP/ch.out" 2>"$TMP/ch.err"; then
    echo 'conversion failure unexpectedly succeeded' >&2; exit 1
fi
grep -q 'extraction conversion failed' "$TMP/ch.err" || { echo 'conversion error not reported' >&2; exit 1; }

# ---------------------------------------------------------------- backend read/write taxonomy
printf 'x' > bf.txt
[[ "$("$NIFT" -e 'o := ofstream("/dev/full"); big := ""; for(i : range(1,5000)) { big += "0123456789abcdef" }; try { o << big } catch(e) { print(e.code); print(e.category) }')" == $'stream.write_failed\nstream' ]]
# operator read backend failure reuses stream.read_failed
[[ "$("$NIFT" -e 's := ""; f := ifstream("/dev"); try { f >> s } catch(e) { print(e.code) }')" == 'stream.read_failed' ]]

# ---------------------------------------------------------------- Error insertion rejected
if "$NIFT" -e 'o := ofstream("e.txt"); o << error("boom"); o.close()' >"$TMP/err.out" 2>"$TMP/err.err"; then
    echo 'Error insertion unexpectedly succeeded' >&2; exit 1
fi
grep -q 'not directly renderable' "$TMP/err.err" || { echo 'Error insertion error not reported' >&2; exit 1; }
# explicit stringify() remains the deliberate path
[[ "$("$NIFT" -e 'o := ofstream("e.txt"); o << error("boom").stringify(); o.close(); print(open("e.txt"))')" == '{"message":"boom","code":"user.raised","category":"user","source":"","line":0,"column":0,"cause":null}' ]]

# ---------------------------------------------------------------- fatal misuse bypasses catch
for script in \
    'o := ofstream("x.txt"); r := ""; try { o >> r } catch(e) { print("caught") }' \
    'f := ifstream("d.txt"); i := 0; try { f >> 5 } catch(e) { print("caught") }' \
    'f := ifstream("d.txt"); const c := 0; try { f >> c } catch(e) { print("caught") }' \
    'o := ofstream("x.txt"); try { o << {1,2} } catch(e) { print("caught") }' \
    'o := ofstream("x.txt"); try { o << timer() } catch(e) { print("caught") }' \
    'f := ifstream("d.txt"); try { f >> [] } catch(e) { print("caught") }'; do
    if "$NIFT" -e "$script" >"$TMP/fs.out" 2>"$TMP/fs.err"; then
        echo "stream operator misuse unexpectedly succeeded: $script" >&2; exit 1
    fi
    grep -q 'caught' "$TMP/fs.out" && { echo "stream operator misuse was caught: $script" >&2; exit 1; }
done

# closed/unopened stream operators are fatal lifecycle misuse
if "$NIFT" -e 'f := ifstream("d.txt"); f.close(); try { f >> "" } catch(e) { print("caught") }' >"$TMP/c.out" 2>&1; then
    echo 'operator on closed stream unexpectedly succeeded' >&2; exit 1
fi
grep -q 'caught' "$TMP/c.out" && { echo 'operator on closed stream was caught' >&2; exit 1; }
if "$NIFT" -e 'f := ifstream(); try { f >> "" } catch(e) { print("caught") }' >"$TMP/u.out" 2>&1; then
    echo 'operator on unopened stream unexpectedly succeeded' >&2; exit 1
fi
grep -q 'caught' "$TMP/u.out" && { echo 'operator on unopened stream was caught' >&2; exit 1; }

# ---------------------------------------------------------------- prepared/legacy and function bodies
# stream operators work in a loop body (prepared body falls back to legacy parse)
[[ "$("$NIFT" -e 'o := ofstream("lp.txt"); i := 0; while(i < 3) { o << "v"; i = i + 1 }; o.close(); print(open("lp.txt"))')" == 'vvv' ]]
# stream operators work inside a named function body
[[ "$("$NIFT" -e 'fn(emit(o)) { o << "from-fn" }; o := ofstream("fn.txt"); emit(o); o.close(); print(open("fn.txt"))')" == 'from-fn' ]]

# ---------------------------------------------------------------- command fallback preserved
# unrecognized statements still follow the normal shell-command path
[[ "$("$NIFT" -e 'echo hello')" == 'hello' ]]
# shell-shaped lines are not stolen by the stream parser (identical to baseline behavior)
for line in 'printf x > s1.txt' 'cat s2.txt >> s2.txt' 'true'; do
    if "$NIFT" -e "$line" >"$TMP/cmd.out" 2>"$TMP/cmd.err"; then :; fi
done
# a stream-operator statement whose first token is NOT a stream binding is not stolen
printf 'x' > nos.txt
if "$NIFT" -e 'nos.txt >> nos2.txt' >"$TMP/nos.out" 2>"$TMP/nos.err"; then :; fi

# precedence: writing a comparison result needs parentheses (documented caveat)
[[ "$("$NIFT" -e 'o := ofstream("p.txt"); o << (1 < 2); o.close(); print(open("p.txt"))')" == 'true' ]]

echo 'v4.6 Batch 4 CP4b+ stream operators: PASS'