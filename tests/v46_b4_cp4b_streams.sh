#!/usr/bin/env bash
set -euo pipefail

NIFT=${NIFT_BIN:-${NIFT:-./nift}}
case "$NIFT" in /*) ;; *) NIFT="$(pwd)/$NIFT" ;; esac
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/wd"
cd "$TMP/wd"

catch_code() {
    local expected=$1 script=$2
    local actual
    actual=$("$NIFT" -e "$script" 2>"$TMP/cc.err") || {
        echo "catch_code($expected) script failed:" >&2; cat "$TMP/cc.err" >&2; exit 1
    }
    local code category source_ok line_ok message
    IFS='|' read -r code category source_ok line_ok message <<<"$actual"
    [[ "$code" == "$expected" ]] || { echo "catch_code($expected): code=$code" >&2; exit 1; }
    [[ "$category" == "stream" ]] || { echo "catch_code($expected): category=$category" >&2; exit 1; }
    [[ "$source_ok" == "true" ]] || { echo "catch_code($expected): source not populated" >&2; exit 1; }
    [[ "$line_ok" == "true" ]] || { echo "catch_code($expected): line not populated" >&2; exit 1; }
    [[ -n "$message" ]] || { echo "catch_code($expected): empty message" >&2; exit 1; }
}

# ---------------------------------------------------------------- API parity
printf 'hello\nworld\n' > data.txt
# constructor-with-path and explicit close method
[[ "$("$NIFT" -e 's := ifstream("data.txt"); print(s.read_line()); s.close(); print("done")')" == $'hello\ndone' ]]
# default construction + open + close
[[ "$("$NIFT" -e 's := ifstream(); s.open("data.txt"); print(s.read_all()); s.close(); print("done")')" == $'hello\nworld\n\ndone' ]]
# ofstream default construction + open + close
[[ "$("$NIFT" -e 'o := ofstream(); o.open("out.txt"); o.write_line("x"); o.close(); print(open("out.txt"))')" == 'x' ]]
# global close remains equivalent
[[ "$("$NIFT" -e 's := ifstream("data.txt"); print(s.read_line()); close(s); print("done")')" == $'hello\ndone' ]]
# reopen after close
[[ "$("$NIFT" -e 'o := ofstream(); o.open("a.txt"); o.write("a"); o.close(); o.open("b.txt"); o.write("b"); o.close(); print(open("a.txt") + open("b.txt"))')" == 'ab' ]]
# write_bytes
[[ "$("$NIFT" -e 'o := ofstream(); o.open("bb.bin"); o.write_bytes(bytes([65,66,67])); o.close(); b := open_bytes("bb.bin"); print(b[0]); print(b[1]); print(b[2])')" == $'65\n66\n67' ]]

# ---------------------------------------------------------------- lifecycle state machine (fatal misuse)
for script in \
    's := ofstream(); s.open("x.txt"); s.open("y.txt")' \
    's := ifstream(); s.close()' \
    's := ifstream("data.txt"); s.close(); s.close()' \
    's := ifstream("data.txt"); s.close(); print(s.read_line())' \
    's := ifstream(); print(s.read(1))' \
    's := ifstream(); print(s.write("x"))'; do
    if "$NIFT" -e "$script" >"$TMP/lc.out" 2>"$TMP/lc.err"; then
        echo "lifecycle misuse unexpectedly succeeded: $script" >&2; exit 1
    fi
done

# ---------------------------------------------------------------- recoverable stream codes
catch_code stream.open_failed 's := ifstream(); try { s.open("missing.txt") } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'
catch_code stream.open_failed 'try { s := ofstream("nodir/x.txt") } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'
catch_code stream.read_failed 's := ifstream("/dev"); try { print(s.read(1)) } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'
catch_code stream.read_failed 's := ifstream("/dev"); try { print(s.read_all()) } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'
catch_code stream.write_failed 'big := ""; for(i : range(1,5000)) { big += "0123456789abcdef" }; o := ofstream("/dev/full"); try { o.write(big) } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'
catch_code stream.flush_failed 'o := ofstream("/dev/full"); o.write("x"); try { o.flush() } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'
catch_code stream.close_failed 'o := ofstream("/dev/full"); o.write("x"); try { o.close() } catch(err) { print(err.code + "|" + err.category + "|" + (err.source != "") + "|" + (err.line > 0) + "|" + err.message) }'

# ---------------------------------------------------------------- EOF is not read_failed
[[ "$("$NIFT" -e 's := ifstream("data.txt"); print(s.read_line()); print(s.read_line()); print(s.read_line() == null); print(s.eof()); s.close()')" == $'hello\nworld\ntrue\ntrue' ]]
[[ "$("$NIFT" -e 's := ifstream("data.txt"); print(s.read_all()); print(s.eof()); s.close()')" == $'hello\nworld\n\ntrue' ]]

# ---------------------------------------------------------------- fatal stays fatal
for script in \
    's := ofstream(); try { s.read(1) } catch(err) { print("caught") }' \
    's := ifstream(); try { s.write("x") } catch(err) { print("caught") }' \
    's := ifstream("data.txt"); try { s.read("x") } catch(err) { print("caught") }' \
    's := ifstream("data.txt"); try { s.write("x") } catch(err) { print("caught") }' \
    's := ifstream(); try { s.open(42) } catch(err) { print("caught") }' \
    'o := ofstream("x.txt"); try { o.write(error("boom")) } catch(err) { print("caught") }' \
    'o := ofstream("x.txt"); try { o.write(fn(f()) { return 1 }) } catch(err) { print("caught") }' \
    'o := ofstream("x.txt"); try { o.write({1,2}) } catch(err) { print("caught") }'; do
    if "$NIFT" -e "$script" >"$TMP/fs.out" 2>"$TMP/fs.err"; then
        echo "fatal stream misuse unexpectedly succeeded or was caught: $script" >&2; exit 1
    fi
    grep -q 'caught' "$TMP/fs.out" && { echo "fatal stream misuse was caught: $script" >&2; exit 1; }
done

# ofstream direction misuse remains fatal, not caught
if "$NIFT" -e 's := ofstream("x.txt"); try { s.read_all() } catch(err) { print("caught") }' >"$TMP/d.out" 2>&1; then
    echo 'read on ofstream unexpectedly succeeded' >&2; exit 1
fi
grep -q 'caught' "$TMP/d.out" && { echo 'read on ofstream was caught' >&2; exit 1; }

# Error values are never silently serialized through stream write.
! grep -q '"code"\|"cause"' "$TMP/x.txt" 2>/dev/null || { echo 'Error leaked into stream file' >&2; exit 1; }

# ---------------------------------------------------------------- uncaught compatibility
if "$NIFT" -e 's := ifstream("missing.txt")' >"$TMP/u.out" 2>"$TMP/u.err"; then
    echo 'uncaught open failure unexpectedly succeeded' >&2; exit 1
fi
grep -q 'ifstream: cannot open path' "$TMP/u.err" || { echo 'uncaught open message mismatch' >&2; cat "$TMP/u.err" >&2; exit 1; }
if grep -q '"code"\|"category"\|"cause"' "$TMP/u.err"; then
    echo 'uncaught stream failure exposed Error serialization' >&2; exit 1
fi

echo 'v4.6 Batch 4 CP4b streams: PASS'