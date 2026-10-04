#!/usr/bin/env bash
# FS-TYPE: exists/is_file/is_dir/stat filesystem type-inspection primitives.
# Verifies ordinary semantics, symlink following, unicode/spaces/trailing
# separators, relative/absolute paths, and recoverable-error behaviour.
set -euo pipefail
NIFT="${NIFT:-./nift}"
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
fails=0
check() { local name="$1"; shift; if "$@"; then echo "PASS $name"; else echo "FAIL $name"; fails=1; fi }

mkdir -p "$t/dir" "$t/dir with spaces" "$t/unicode-dir-ü"
printf 'abc' > "$t/file.txt"
echo "x" > "$t/file with spaces.txt"
echo "y" > "$t/ünïcode.txt"

# Symlinks require native support (Windows needs privilege/Developer Mode); skip
# the symlink assertions when the environment cannot create them.
SYMLINKS=0
if ln -sf file.txt "$t/link_to_file" 2>/dev/null \
   && ln -sfn dir "$t/link_to_dir" 2>/dev/null \
   && ln -sfn missing_target "$t/dangling_link" 2>/dev/null; then
  SYMLINKS=1
fi

cat > "$t/probe.f" <<'EOF'
print("exists_file:" + exists("file.txt").to_string())
print("exists_dir:" + exists("dir").to_string())
print("exists_missing:" + exists("nope").to_string())
print("is_file_file:" + is_file("file.txt").to_string())
print("is_file_dir:" + is_file("dir").to_string())
print("is_file_missing:" + is_file("nope").to_string())
print("is_dir_dir:" + is_dir("dir").to_string())
print("is_dir_file:" + is_dir("file.txt").to_string())
print("is_dir_missing:" + is_dir("nope").to_string())
print("link_file_is_file:" + is_file("link_to_file").to_string())
print("link_file_is_dir:" + is_dir("link_to_file").to_string())
print("link_dir_is_dir:" + is_dir("link_to_dir").to_string())
print("dangling_exists:" + exists("dangling_link").to_string())
print("dangling_is_file:" + is_file("dangling_link").to_string())
print("dangling_is_dir:" + is_dir("dangling_link").to_string())
print("spaces_file:" + is_file("file with spaces.txt").to_string())
print("spaces_dir:" + is_dir("dir with spaces").to_string())
print("unicode_file:" + is_file("ünïcode.txt").to_string())
print("unicode_dir:" + is_dir("unicode-dir-ü").to_string())
print("trailing_slash:" + is_dir("dir/").to_string())
print("relative_dot:" + is_file("./file.txt").to_string())
print("nested_dotdot:" + is_file("dir/../file.txt").to_string())
print("absolute:" + is_file(pwd() + "/file.txt").to_string())
s := stat("file.txt")
print("stat_file_exists:" + s.exists.to_string())
print("stat_file_type:" + s.type)
print("stat_file_size:" + s.size.to_string())
d := stat("dir")
print("stat_dir_type:" + d.type)
print("stat_dir_has_size:" + d.has("size").to_string())
m := stat("nope")
print("stat_missing_exists:" + m.exists.to_string())
EOF

out=$(cd "$t" && "$NIFT" probe.f)
echo "--- probe output ---"
echo "$out"

check "exists file/dir/missing" [ "$(echo "$out" | grep -E '^exists_file:|^exists_dir:|^exists_missing:' | tr '\n' ' ')" = "exists_file:true exists_dir:true exists_missing:false " ]
check "is_file file/dir/missing" [ "$(echo "$out" | grep -E '^is_file_' | tr '\n' ' ')" = "is_file_file:true is_file_dir:false is_file_missing:false " ]
check "is_dir dir/file/missing" [ "$(echo "$out" | grep -E '^is_dir_dir:|^is_dir_file:|^is_dir_missing:' | tr '\n' ' ')" = "is_dir_dir:true is_dir_file:false is_dir_missing:false " ]
if [ "$SYMLINKS" -eq 1 ]; then
  check "symlink follows to file" [ "$(echo "$out" | grep -E '^link_file_' | tr '\n' ' ')" = "link_file_is_file:true link_file_is_dir:false " ]
  check "symlink follows to dir" [ "$(echo "$out" | grep -E '^link_dir_is_dir:')" = "link_dir_is_dir:true" ]
  check "dangling symlink all false" [ "$(echo "$out" | grep -E '^dangling_' | tr '\n' ' ')" = "dangling_exists:false dangling_is_file:false dangling_is_dir:false " ]
else
  echo "SKIP symlink assertions (cannot create symlinks here)"
fi
check "spaces/unicode/trailing/relative" [ "$(echo "$out" | grep -E '^spaces_|^unicode_|^trailing_slash:|^relative_dot:|^nested_dotdot:|^absolute:' | tr '\n' ' ')" = "spaces_file:true spaces_dir:true unicode_file:true unicode_dir:true trailing_slash:true relative_dot:true nested_dotdot:true absolute:true " ]
check "stat file exists/type/size" [ "$(echo "$out" | grep -E '^stat_file_' | tr '\n' ' ')" = "stat_file_exists:true stat_file_type:file stat_file_size:3 " ]
check "stat dir type / no size key / missing exists" [ "$(echo "$out" | grep -E '^stat_dir_type:|^stat_dir_has_size:|^stat_missing_exists:' | tr '\n' ' ')" = "stat_dir_type:directory stat_dir_has_size:false stat_missing_exists:false " ]

# Recoverable error on a genuine metadata failure (permission denied).
# POSIX permission bits are meaningless on Windows, so this reproduces only on
# POSIX hosts.
case "$(uname -s 2>/dev/null)" in
  MINGW*|MSYS*) IS_WINDOWS=1 ;;
  *) IS_WINDOWS=0 ;;
esac
if [ "$IS_WINDOWS" -eq 0 ]; then
mkdir -p "$t/locked"; echo "secret" > "$t/locked/secret.txt"; chmod 000 "$t/locked"
if [ "$(id -u)" != "0" ]; then
  cat > "$t/perm.f" <<'EOF'
caught := false
try {
    v := is_file("locked/secret.txt")
} catch(e) {
    caught = true
}
print("perm_is_file_error:" + caught.to_string())
EOF
  pout=$(cd "$t" && "$NIFT" perm.f)
  echo "--- perm output ---"; echo "$pout"
  check "permission-denied is recoverable error" [ "$(echo "$pout" | grep -E '^perm_is_file_error:')" = "perm_is_file_error:true" ]
fi
chmod 755 "$t/locked"
fi

# Windows-specific path forms (drive letters, backslashes, trailing backslash).
if [ "$IS_WINDOWS" -eq 1 ]; then
  cat > "$t/win.f" <<'WINEOF'
drive := getenv("SystemDrive")
if(drive == null) { drive = "C:" }
print("win_drive_file:" + is_file(drive + "/Windows/System32/notepad.exe").to_string())
print("win_backslash_dir:" + is_dir(drive + "\\Windows").to_string())
print("win_forward_dir:" + is_dir(drive + "/Windows").to_string())
print("win_trailing_backslash:" + is_dir(drive + "\\Windows\\").to_string())
print("win_missing:" + is_file(drive + "/Windows/definitely_missing.exe").to_string())
WINEOF
  wout=$(cd "$t" && "$NIFT" win.f)
  echo "--- windows output ---"; echo "$wout"
  check "win drive/backslash/forward/trailing" [ "$(echo "$wout" | grep -E '^win_' | tr '\n' ' ')" = "win_drive_file:true win_backslash_dir:true win_forward_dir:true win_trailing_backslash:true win_missing:false " ]
fi

if [ "$fails" -ne 0 ]; then echo "FAILED"; exit 1; fi
echo "PASS filesystem type inspection"