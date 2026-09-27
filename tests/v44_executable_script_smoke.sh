#!/usr/bin/env bash
# v4.4 executable .f scripts: nift script.f shorthand, shebang handling,
# ./script.f from the host shell and plain `nift` shell, command-style and run() from
# another script, script arguments, permission/restriction semantics, paths
# with spaces, Unicode and environment inheritance.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
BIN="$(dirname "$NIFT_ABS")"
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
t_physical=$(cd "$t" && pwd -P)
case "$(uname -s)" in MINGW*|MSYS*) t_physical=$(cygpath -am "$t");; esac

cat > "$t/hello.f" <<'F'
#!/usr/bin/env nift

/*
    Executable script fixture.
*/
print("argc=" + args.size().to_string())
for(a : args) { print("arg:" + a) }
print("env=" + getenv("NIFT_EXEC_TEST"))
print("hello")
F
chmod +x "$t/hello.f"

# nift script.f shorthand (absolute path, with args)
out=$("$NIFT_ABS" "$t/hello.f" staging --force)
[ "$(sed -n '1p' <<<"$out")" = "argc=2" ] || { echo "$out" >&2; exit 1; }
[ "$(sed -n '2p' <<<"$out")" = "arg:staging" ] || exit 1
[ "$(sed -n '3p' <<<"$out")" = "arg:--force" ] || exit 1
grep -q "^hello$" <<<"$out" || exit 1

# direct nift script execution of the same shebang file (shebang stripped)
out=$("$NIFT_ABS" "$t/hello.f" one)
grep -q "^argc=1$" <<<"$out" || exit 1
grep -q "^arg:one$" <<<"$out" || exit 1

# relative path shorthand from a different cwd
(cd /tmp && "$NIFT_ABS" "$t/hello.f" >/dev/null) || exit 1

# ./script.f from the host shell via the shebang (local nift on PATH)
out=$(cd "$t" && PATH="$BIN:$PATH" ./hello.f x y)
grep -q "^argc=2$" <<<"$out" || { echo "$out" >&2; exit 1; }

# ./script.f from plain `nift` shell
out=$(cd "$t" && printf './hello.f from-sh\n' | PATH="$BIN:$PATH" "$NIFT_ABS")
grep -q "^arg:from-sh$" <<<"$out" || { echo "$out" >&2; exit 1; }

# command-style from another .f script
cat > "$t/parent.f" <<'NIFT'
#!/usr/bin/env nift
./hello.f a b
print("parent-done")
NIFT
chmod +x "$t/parent.f"
out=$(cd "$t" && PATH="$BIN:$PATH" "$NIFT_ABS" parent.f)
grep -q "^arg:a$" <<<"$out" || { echo "$out" >&2; exit 1; }
grep -q "^parent-done$" <<<"$out" || exit 1

# run() structured invocation
cat > "$t/r.f" <<'NIFT'
r := run("./hello.f", "one", "two")
print("exit=" + r.exit_code.to_string())
NIFT
out=$(cd "$t" && PATH="$BIN:$PATH" "$NIFT_ABS" r.f)
grep -q "^exit=0$" <<<"$out" || { echo "$out" >&2; exit 1; }

# executable permission failure: chmod -x must NOT fall back to direct nift script execution.
# The exec permission bit is POSIX-only; Windows files have no exec-bit
# semantics (chmod -x is a no-op there), so this assertion is POSIX-only.
case "$(uname -s)" in MINGW*|MSYS*) ;; *)
chmod -x "$t/hello.f"
if (cd "$t" && PATH="$BIN:$PATH" ./hello.f >/dev/null 2>&1); then echo "exec -x succeeded" >&2; exit 1; fi
chmod +x "$t/hello.f"
;; esac

# --no-process blocks external execution (command-style and run())
cat > "$t/c.f" <<'NIFT'
./hello.f
NIFT
if (cd "$t" && PATH="$BIN:$PATH" NIFT_NO_PROCESS=1 "$NIFT_ABS" c.f >/dev/null 2>&1); then echo "command-style not blocked" >&2; exit 1; fi
cat > "$t/r2.f" <<'NIFT'
run("./hello.f")
NIFT
if (cd "$t" && PATH="$BIN:$PATH" NIFT_NO_PROCESS=1 "$NIFT_ABS" r2.f >/dev/null 2>&1); then echo "run() not blocked" >&2; exit 1; fi

# paths with spaces and Unicode
# Non-ASCII paths/args are POSIX-clean; on Windows the MSYS2 -> native-binary
# argument boundary delivers them in the ANSI codepage, so Nift (which treats
# argv as UTF-8) cannot resolve them. The space-path case is portable and is
# still tested; the Unicode case is POSIX-only.
mkdir -p "$t/my dir" "$t/üni"
cp "$t/hello.f" "$t/my dir/with space.f"
cp "$t/hello.f" "$t/üni/child.f"
chmod +x "$t/my dir/with space.f" "$t/üni/child.f"
out=$(cd "$t" && PATH="$BIN:$PATH" ./my\ dir/with\ space.f sp)
grep -q "^arg:sp$" <<<"$out" || { echo "$out" >&2; exit 1; }
case "$(uname -s)" in MINGW*|MSYS*) ;; *)
out=$(cd "$t" && PATH="$BIN:$PATH" ./üni/child.f "héllo wörld")
grep -q "^arg:héllo wörld$" <<<"$out" || { echo "$out" >&2; exit 1; }
;; esac

# environment inheritance
out=$(cd "$t" && PATH="$BIN:$PATH" NIFT_EXEC_TEST="envval" "$NIFT_ABS" "$t/hello.f")
grep -q "^env=envval$" <<<"$out" || { echo "$out" >&2; exit 1; }

# completion: ./paths discoverable
out=$(cd "$t" && "$NIFT_ABS" complete "./he")
grep -q "^./hello.f$" <<<"$out" || { echo "$out" >&2; exit 1; }
out=$(cd "$t" && "$NIFT_ABS" complete "./my ")
grep -q "^./my dir/$" <<<"$out" || { echo "$out" >&2; exit 1; }
out=$(cd "$t" && "$NIFT_ABS" complete "./my dir/with ")
grep -q "^./my dir/with space.f$" <<<"$out" || { echo "$out" >&2; exit 1; }


# command-style external commands from script land: word + args resolves on PATH
mkdir -p "$t/bin"
cat > "$t/bin/fixtool" <<'B'
#!/bin/sh
echo "fixtool-ran $1"
B
chmod +x "$t/bin/fixtool"
# Windows cannot CreateProcess a shebang script directly; provide a .cmd twin
# (nift_find_executable prefers .cmd/.bat there) so command-style bare commands
# exercise the same contract, matching the exec-shell test's fixtool.
cat > "$t/bin/fixtool.cmd" <<'B'
@echo off
echo fixtool-ran %1
B
cat > "$t/cc.f" <<'NIFT'
fixtool one
fixtool one two three
print("cc-done")
NIFT
out=$(cd "$t" && PATH="$t/bin:$PATH" "$NIFT_ABS" cc.f 2>&1) || { echo "script command-style: $out" >&2; exit 1; }
grep -q 'fixtool-ran one' <<<"$out" || { echo "$out" >&2; exit 1; }
grep -q '^cc-done$' <<<"$out" || exit 1
# command-style in script land is blocked by --no-process
if (cd "$t" && PATH="$t/bin:$PATH" NIFT_NO_PROCESS=1 "$NIFT_ABS" cc.f 2>/dev/null | grep -q 'fixtool-ran'); then echo "script command-style ran under --no-process" >&2; exit 1; fi


# v4.5 shebang certification: invocation identity, cwd, args and environment.
cat > "$t/certify.f" <<'F'
#!/usr/bin/env nift
print(cmd)
print(args.join("|"))
print(pwd())
print(getenv("NIFT_EXEC_TEST"))
F
chmod +x "$t/certify.f"
out=$(cd "$t" && PATH="$BIN:$PATH" NIFT_EXEC_TEST=certified ./certify.f one two)
[ "$(sed -n '1p' <<<"$out")" = './certify.f' ] || { echo "$out" >&2; exit 1; }
[ "$(sed -n '2p' <<<"$out")" = 'one|two' ] || exit 1
[ "$(sed -n '3p' <<<"$out")" = "$t_physical" ] || exit 1
[ "$(sed -n '4p' <<<"$out")" = 'certified' ] || exit 1

# A runtime error reached through the shebang must remain a non-zero process exit.
cat > "$t/fail-shebang.f" <<'F'
#!/usr/bin/env nift
break
F
chmod +x "$t/fail-shebang.f"
if (cd "$t" && PATH="$BIN:$PATH" ./fail-shebang.f >/dev/null 2>&1); then
  echo 'failing shebang script returned zero' >&2; exit 1
fi

# The shebang marker is only special on source line one.
cat > "$t/not-first.f" <<'F'
print("before")
#!/usr/bin/env nift
print("after")
F
out=$("$NIFT_ABS" "$t/not-first.f")
[ "$out" = $'before\nafter' ] || { echo 'non-leading shebang text changed script execution' >&2; echo "$out" >&2; exit 1; }

# POSIX signal delivery remains ordinary process behavior for an executable script.
case "$(uname -s)" in MINGW*|MSYS*) ;; *)
cat > "$t/signal.f" <<'F'
#!/usr/bin/env nift
i := 0
while(i < 1000000000) { i += 1 }
F
chmod +x "$t/signal.f"
(
  cd "$t"
  PATH="$BIN:$PATH" ./signal.f >/dev/null 2>&1 &
  pid=$!
  sleep 0.05
  kill -TERM "$pid"
  set +e
  wait "$pid"
  rc=$?
  set -e
  [ "$rc" -eq 143 ]
) || { echo 'shebang script did not preserve SIGTERM exit status' >&2; exit 1; }
;; esac
echo 'PASS v4.5 executable .f scripts'
