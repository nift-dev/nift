#!/usr/bin/env bash
# feature-sha256: language-level tests for sha256(value) and sha256_file(path).
#  - FIPS 180-4 known answers through the language, string vs bytes, UTF-8,
#    embedded NULs, files around block/buffer boundaries, random binary files
#    cross-checked against python3 hashlib and sha256sum;
#  - errors (missing, directory, unreadable, wrong types), paths outside the
#    project, symlinks, repeated and concurrent (thread/async) hashing,
#    user definitions shadowing the new natives, --fs-root;
#  - page builds in modified/hash/hybrid modes: the hashed file is a
#    dependency (edit rebuilds, unchanged stays up to date), external files,
#    a caught missing file that later appears, and the Host-op trace.
set -u -o pipefail
NIFT="${NIFT_BIN:-$(pwd)/nift}"
fail=0; passed=0
pass() { echo "PASS $1"; passed=$((passed+1)); }
bad() { echo "FAIL $1"; fail=1; }
expect() { # name expected actual
  if [ "$2" = "$3" ]; then pass "$1"; else bad "$1 (expected '$2', got '$3')"; fi
}
W="$(mktemp -d)"; trap 'chmod -R u+rwx "$W" 2>/dev/null; rm -rf "$W"' EXIT
pyhash() { python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$1"; }

# ---------------------------------------------------------------- values
cd "$W"
cat > vectors.f <<'EOF'
print(sha256(""))
print(sha256("abc"))
print(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))
print(sha256("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"))
s := "a"
i := 0
while(i < 20) { s = s + s; i += 1 }
print(sha256(s.substr(0, 1000000)))
print(sha256(bytes([])))
print(sha256(bytes([97, 98, 99])))
EOF
mapfile -t got < <("$NIFT" vectors.f 2>&1)
expect "NIST empty" e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 "${got[0]:-}"
expect "NIST abc" ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad "${got[1]:-}"
expect "NIST 448-bit" 248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1 "${got[2]:-}"
expect "NIST 896-bit" cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1 "${got[3]:-}"
expect "NIST one million a" cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0 "${got[4]:-}"
expect "empty bytes" e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 "${got[5]:-}"
expect "bytes equal string bytes" ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad "${got[6]:-}"

# UTF-8 is hashed as stored, without normalisation (composed vs decomposed é).
printf 'print(sha256("h\xc3\xa9llo w\xc3\xb6rld \xe2\x9c\x93 \xe6\x97\xa5\xe6\x9c\xac"))\nprint(sha256("e\xcc\x81"))\nprint(sha256("\xc3\xa9"))\n' > utf8.f
mapfile -t got < <("$NIFT" utf8.f 2>&1)
expect "UTF-8 text" "$(printf 'h\xc3\xa9llo w\xc3\xb6rld \xe2\x9c\x93 \xe6\x97\xa5\xe6\x9c\xac' | sha256sum | cut -d' ' -f1)" "${got[0]:-}"
expect "NFD not normalised" "$(printf 'e\xcc\x81' | sha256sum | cut -d' ' -f1)" "${got[1]:-}"
expect "NFC not normalised" "$(printf '\xc3\xa9' | sha256sum | cut -d' ' -f1)" "${got[2]:-}"

# Embedded NULs: bytes values, and strings read from a file containing NULs.
printf 'a\0b\0\0c\r\n' > nul.bin
cat > nul.f <<'EOF'
print(sha256(bytes([0, 0, 1, 0])))
print(sha256(open("nul.bin")))
print(sha256(open_bytes("nul.bin")))
print(sha256_file("nul.bin"))
print(sha256("line\r\n") == sha256("line\n"))
EOF
mapfile -t got < <("$NIFT" nul.f 2>&1)
expect "bytes with NULs" "$(printf '\0\0\1\0' | sha256sum | cut -d' ' -f1)" "${got[0]:-}"
want="$(sha256sum nul.bin | cut -d' ' -f1)"
expect "string with NULs (open)" "$want" "${got[1]:-}"
expect "bytes with NULs (open_bytes)" "$want" "${got[2]:-}"
expect "file with NULs and CRLF" "$want" "${got[3]:-}"
expect "line endings not normalised" false "${got[4]:-}"

# ---------------------------------------------------------------- files
mkdir -p files
sizes="0 1 55 56 63 64 65 119 120 128 4095 4096 65535 65536 65537 131072 196625 5242883"
python3 - "$W/files" $sizes <<'PY'
import os, sys, random
random.seed(7)
d = sys.argv[1]
for n in map(int, sys.argv[2:]):
    open(os.path.join(d, f"s{n}.bin"), "wb").write(bytes(random.getrandbits(8) for _ in range(n)) if n < 300000 else os.urandom(n))
for i in range(20):
    n = random.randint(0, 300000)
    open(os.path.join(d, f"r{i:02d}.bin"), "wb").write(os.urandom(n))
PY
{
  echo 'for(f : ls("files")) { print(f + " " + sha256_file("files/" + f)) }'
} > files.f
"$NIFT" files.f > files.out 2>&1 || bad "hashing files failed: $(head -3 files.out)"
mismatch=0; count=0
while read -r name digest; do
  count=$((count+1))
  [ "$digest" = "$(pyhash "files/$name")" ] || { mismatch=1; echo "  hashlib mismatch: $name"; }
  [ "$digest" = "$(sha256sum "files/$name" | cut -d' ' -f1)" ] || { mismatch=1; echo "  sha256sum mismatch: $name"; }
done < files.out
[ "$count" -eq 38 ] && [ "$mismatch" -eq 0 ] && pass "$count files (block/buffer boundaries, 5 MiB, random binary) match hashlib and sha256sum" || bad "file digests ($count files, mismatch=$mismatch)"

cat > repeat.f <<'EOF'
a := sha256_file("files/s65537.bin")
ok := true
for(i : range(50)) { if(sha256_file("files/s65537.bin") != a) { ok = false } }
print(ok)
print(sha256(open_bytes("files/s65537.bin")) == a)
fn(h(p)) { return sha256_file(p) }
i := 0
while(i < 3) { if(h("files/s1.bin") != sha256_file("files/s1.bin")) { ok = false }; i += 1 }
print(ok)
EOF
mapfile -t got < <("$NIFT" repeat.f 2>&1)
expect "repeated hashing is stable" true "${got[0]:-}"
expect "sha256(open_bytes(p)) == sha256_file(p)" true "${got[1]:-}"
expect "loops and functions" true "${got[2]:-}"

cat > concurrent.f <<'EOF'
fn(work(p)) { return sha256_file(p) }
fn[async](later(p)) { return sha256_file(p) }
names := ["files/s0.bin", "files/s64.bin", "files/s65536.bin", "files/s5242883.bin"]
ts := []
fs := []
for(n : names) { ts.push(thread(work, n)); fs.push(later(n)) }
i := 0
for(t : ts) { print(names[i] + " " + t.join()); i += 1 }
i = 0
for(f : fs) { print(names[i] + " " + await f); i += 1 }
EOF
"$NIFT" concurrent.f > concurrent.out 2>&1
ok=1; lines=0
while read -r name digest; do lines=$((lines+1)); [ "$digest" = "$(pyhash "$name")" ] || ok=0; done < concurrent.out
[ "$lines" -eq 8 ] && [ "$ok" -eq 1 ] && pass "threads and async tasks hash concurrently" || bad "concurrent hashing: $(head -3 concurrent.out)"

# ---------------------------------------------------------------- errors
mkdir -p adir; echo x > plain.txt
cat > errors.f <<'EOF'
try { sha256_file("missing.bin") } catch(e) { print(e.code + "|" + e.message) }
try { sha256_file("plain.txt/child") } catch(e) { print(e.code + "|" + e.message) }
try { open("missing.bin") } catch(e) { print(e.code + "|" + e.message) }
EOF
mapfile -t got < <("$NIFT" errors.f 2>&1)
expect "missing file is a recoverable io.open_failed" "io.open_failed|sha256_file: cannot open path" "${got[0]:-}"
expect "file used as a directory is missing" "io.open_failed|sha256_file: cannot open path" "${got[1]:-}"
expect "same code as open()" "io.open_failed|open: cannot open path" "${got[2]:-}"
err_of() { printf '%s\n' "$1" > err.f; "$NIFT" err.f 2>&1 | sed 's/^error: [^ ]*: //'; }
expect "directory" "sha256_file: path is a directory" "$(err_of 'print(sha256_file("adir"))')"
expect "open() wording for directories" "open: path is a directory" "$(err_of 'print(open("adir"))')"
expect "number rejected" "sha256: expected a string or bytes value" "$(err_of 'print(sha256(1))')"
expect "null rejected" "sha256: expected a string or bytes value" "$(err_of 'print(sha256(null))')"
expect "array rejected" "sha256: expected a string or bytes value" "$(err_of 'print(sha256([1, 2]))')"
expect "callable rejected" "sha256: expected a string or bytes value" "$(err_of 'f := (x) => x
print(sha256(f))')"
expect "arity" "sha256: expected one string or bytes value" "$(err_of 'print(sha256("a", "b"))')"
expect "path type" "sha256_file: expected string path" "$(err_of 'print(sha256_file(1))')"
expect "path arity" "sha256_file: expected one path" "$(err_of 'print(sha256_file())')"

# Unreadable file: root bypasses permission bits, so as root run the check as
# an unprivileged user when setpriv is available (otherwise skip, as other
# suites do).
mkdir -p perm; echo secret > perm/locked.txt; chmod 000 perm/locked.txt
printf 'try { sha256_file("%s/perm/locked.txt") } catch(e) { print(e.code) }\n' "$W" > perm/unreadable.f
if [ "$(id -u)" != 0 ]; then
  expect "unreadable file is a recoverable io.open_failed" io.open_failed "$("$NIFT" perm/unreadable.f 2>&1)"
elif command -v setpriv >/dev/null 2>&1; then
  chmod 755 "$W" "$W/perm"; chmod 644 perm/unreadable.f
  # nobody cannot read a file with mode 000 owned by root.
  expect "unreadable file is a recoverable io.open_failed (as nobody)" io.open_failed \
    "$(cd / && setpriv --reuid=65534 --regid=65534 --clear-groups "$NIFT" "$W/perm/unreadable.f" 2>&1)"
else
  echo "SKIP unreadable file (running as root without setpriv)"
fi
chmod 644 perm/locked.txt

# ---------------------------------------------------------------- paths
mkdir -p "$W/outside/deep" "$W/project"
printf 'outside' > "$W/outside/deep/o.bin"
ln -s "$W/outside/deep/o.bin" "$W/project/link.bin"
ln -s "$W/outside/deep/nowhere" "$W/project/dangling.bin"
cat > "$W/project/paths.f" <<EOF
print(sha256_file("$W/outside/deep/o.bin"))
print(sha256_file("../outside/deep/o.bin"))
print(sha256_file("link.bin"))
try { sha256_file("dangling.bin") } catch(e) { print(e.code) }
EOF
mapfile -t got < <(cd "$W/project" && "$NIFT" paths.f 2>&1)
want="$(printf outside | sha256sum | cut -d' ' -f1)"
expect "absolute path outside the project" "$want" "${got[0]:-}"
expect "relative path with .." "$want" "${got[1]:-}"
expect "symlink followed" "$want" "${got[2]:-}"
expect "dangling symlink is missing" io.open_failed "${got[3]:-}"
if (cd "$W/project" && "$NIFT" --fs-root="$W/project" paths.f >/dev/null 2>&1); then bad "--fs-root did not restrict sha256_file"; else pass "--fs-root restricts sha256_file when requested"; fi

# ---------------------------------------------------------------- shadowing
cat > shadow.f <<'EOF'
fn(sha256(x)) { return "user:" + x }
fn(sha256_file(p)) { return "userfile:" + p }
print(sha256("abc"))
print(sha256_file("x"))
for(i : range(1)) { print(sha256("loop")); print(sha256_file("loop")) }
EOF
mapfile -t got < <("$NIFT" shadow.f 2>&1)
expect "user sha256 wins (top level)" "user:abc" "${got[0]:-}"
expect "user sha256_file wins (top level)" "userfile:x" "${got[1]:-}"
expect "user sha256 wins (loop)" "user:loop" "${got[2]:-}"
expect "user sha256_file wins (loop)" "userfile:loop" "${got[3]:-}"

# ---------------------------------------------------------------- builds
for mode in modified hash hybrid; do
  P="$W/site-$mode"; X="$W/ext-$mode"; mkdir -p "$P" "$X"
  (cd "$P" && "$NIFT" init >/dev/null 2>&1) || { bad "[$mode] init"; continue; }
  cd "$P"
  sed -i "s/\"incremental-mode\": \"modified\"/\"incremental-mode\": \"$mode\"/" .nift/config.json
  mkdir -p data; printf 'v1' > data/x.bin; printf 'e1' > "$X/e.bin"; printf 't1' > data/t.txt
  cat > content/index.html <<EOF
H:@script { return sha256_file("data/x.bin") }
E:@script { return sha256_file("$X/e.bin") }
M:@script { try { return sha256_file("data/maybe.bin") } catch(e) { return "none" } }
T:@script { r := "t0"; try { r = open("data/t.txt"); x := open("data/never.txt") } catch(e) { r = r + "-caught" }; return r }
EOF
  digest_of() { printf '%s' "$1" | sha256sum | cut -d' ' -f1; }
  has() { grep -q -- "$2" public/index.html && pass "[$mode] $1" || bad "[$mode] $1 (missing '$2')"; }
  up_to_date() { if "$NIFT" status | grep -q "up to date"; then pass "[$mode] $1"; else bad "[$mode] $1: $("$NIFT" status | sed -n 2,4p | tr '\n' ' ')"; fi; }
  NIFT_TRACE_HOST=3 "$NIFT" build --all 3>trace.jsonl >/dev/null 2>&1
  has "initial digest" "H:$(digest_of v1)"
  has "external digest" "E:$(digest_of e1)"
  has "caught missing file" "M:none"
  has "read kept by a failed try block" "T:t1-caught"
  grep -q "\"op\":\"read\",\"arg\":\"$P/data/x.bin\",\"res\":\"2\"" trace.jsonl && pass "[$mode] Host-op trace records the read" || bad "[$mode] trace: $(grep x.bin trace.jsonl)"
  up_to_date "unchanged inputs stay up to date"
  sleep 1.1
  printf 'v2' > data/x.bin; "$NIFT" build >/dev/null 2>&1
  has "edited hashed file rebuilds the page" "H:$(digest_of v2)"
  up_to_date "up to date after rebuild"
  printf 'e2' > "$X/e.bin"; "$NIFT" build >/dev/null 2>&1
  has "edited external file rebuilds the page" "E:$(digest_of e2)"
  printf 'm' > data/maybe.bin; "$NIFT" build >/dev/null 2>&1
  has "caught missing file appearing rebuilds the page" "M:$(digest_of m)"
  # Pre-existing defect fixed with this checkpoint: a try block that fails
  # recoverably used to discard the dependencies it had already recorded.
  printf 't2' > data/t.txt; "$NIFT" build >/dev/null 2>&1
  has "read inside a failed try block stays a dependency" "T:t2-caught"
  up_to_date "up to date at the end"
  # Hash mode compares content snapshots; hybrid (like @input) rebuilds on a
  # newer mtime, and modified mode is mtime-only by design.
  if [ "$mode" = hash ]; then
    sleep 1.1; touch data/x.bin
    up_to_date "touch without content change (content snapshot)"
  fi
  cd "$W"
done

echo "sha256 builtins: $passed passed, fail=$fail"
exit $fail
