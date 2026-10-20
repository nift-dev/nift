#!/usr/bin/env bash
# Runtime 2.0 CP07: process capture uses anonymous regular files. Expected
# values are the pre-CP07 (v4.12.0 / runtime-2 c5db14d) behaviour; CP07 must
# not change any of them (D-006/D-010), and must never leave capture files.
# CP16 changes one on purpose: case 4 (C2, D-030).
set -u -o pipefail
NIFT="${NIFT_BIN:-$(pwd)/nift}"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
export TMPDIR="$W/tmpdir"; mkdir -p "$TMPDIR"
cd "$W" || exit 2
fail=0
ok() { echo "PASS $1"; }
bad() { echo "FAIL $1"; fail=1; }
expect() { # name expected actual
  if [ "$2" = "$3" ]; then ok "$1"; else bad "$1: expected [$2] got [$3]"; fi
}
leaks_before=$(ls /tmp | grep -c '^nift-proc-' || true)

# 1. The child's stdout/stderr are seekable regular files (unchanged).
cat > fst.f <<'EOF'
print(cmd("stat", "-L", "-c", "%F", "/dev/stdout").run().stdout)
print(cmd("sh", "-c", "echo first; echo second > /dev/stdout").run().stdout)
print(cmd("sh", "-c", "echo x >&2; echo y > /dev/stderr").run().stderr)
print(cmd("sh", "-c", "echo hello | tee /dev/stdout").run().stdout)
EOF
expect "stdout is a regular file; /dev/stdout reopen truncates; tee" \
  "regular empty file||second||y||hello||" "$("$NIFT" fst.f 2>&1 | tr '\n' '|')"
if command -v python3 >/dev/null; then
  cat > seek.f <<'EOF'
print(cmd("python3", "-c", "import os,stat;print(stat.S_ISREG(os.fstat(1).st_mode), stat.S_ISFIFO(os.fstat(1).st_mode))").run().stdout)
print(cmd("sh", "-c", "echo AAAA; python3 -c 'import os; os.lseek(1,0,0); os.write(1,b\"BB\")'").run().stdout)
EOF
  expect "fstat regular, lseek on captured stdout" "True False||BBAA||" "$("$NIFT" seek.f 2>&1 | tr '\n' '|')"
fi

# 2. Large output on both streams, binary data with NUL bytes, exit codes.
cat > big.f <<'EOF'
r := cmd("sh", "-c", "head -c 4000000 /dev/zero | tr '\\0' a; head -c 2000000 /dev/zero | tr '\\0' b >&2").run()
print(r.stdout.length())
print(r.stderr.length())
b := cmd("printf", "a\\000b").run()
print(b.stdout.length())
print(cmd("sh", "-c", "exit 7").run().exit_code)
print(cmd("sh", "-c", "kill -9 $$").run().exit_code)
EOF
expect "large stdout+stderr, NUL bytes, exit codes" "4000000|2000000|3|7|137|" "$("$NIFT" big.f 2>&1 | tr '\n' '|')"

# 3. A background descendant neither blocks the call nor is captured after
#    the launched process exits (writes go to the unlinked file and are lost).
cat > bg.f <<'EOF'
r := cmd("sh", "-c", "(sleep 0.3; echo late) & echo parent").run()
print("[" + r.stdout + "]")
EOF
start=$(date +%s%N)
out="$("$NIFT" bg.f 2>&1 | tr '\n' '|')"
ms=$(( ($(date +%s%N) - start) / 1000000 ))
expect "background descendant output after exit not captured" "[parent|]|" "$out"
if [ "$ms" -lt 280 ]; then ok "background descendant does not block (${ms} ms)"; else bad "call blocked ${ms} ms"; fi

# 4. CP16 C2 (D-030): a child still running when nift receives SIGTERM is
#    terminated with it, and nift ends with SIGTERM. (Pre-CP16 the child kept
#    running: it wrote to a file, not a pipe, so it got no SIGPIPE.)
cat > kill.f <<'EOF'
r := cmd("sh", "-c", "sleep 0.5; echo progress; echo done > side_effect").run()
print(r.stdout)
EOF
"$NIFT" kill.f >/dev/null 2>&1 & pid=$!
sleep 0.2; kill -TERM "$pid"; wait "$pid" 2>/dev/null; status=$?
sleep 0.8
[ -f side_effect ] && bad "child survived nift's SIGTERM" || ok "child terminated with nift (C2)"
expect "nift ends with SIGTERM" "143" "$status"

# 5. Capture still works with very few descriptors.
cat > emf.f <<'EOF'
r := cmd("echo", "hi").run()
print(r.stdout)
print(r.exit_code)
EOF
expect "capture under ulimit -n 6" "hi||0|" "$(bash -c "ulimit -n 6; \"$NIFT\" emf.f" 2>&1 | tr '\n' '|')"

# 6. No capture files are ever left, even when nift is killed mid-capture.
"$NIFT" kill.f >/dev/null 2>&1 & pid=$!
sleep 0.2; kill -KILL "$pid"; wait "$pid" 2>/dev/null
sleep 0.6
leaks_after=$(ls /tmp | grep -c '^nift-proc-' || true)
if [ "$leaks_after" -le "$leaks_before" ]; then ok "no /tmp/nift-proc-* files left (even after SIGKILL)"; else bad "$((leaks_after - leaks_before)) capture files leaked into /tmp"; fi
if [ -z "$(ls -A "$TMPDIR")" ]; then ok "nothing created in TMPDIR"; else bad "files in TMPDIR: $(ls -A "$TMPDIR" | tr '\n' ' ')"; fi

exit $fail
