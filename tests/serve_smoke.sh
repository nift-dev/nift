#!/usr/bin/env bash
# `nift serve` smoke test. Linux runs everything; macOS runs the portable
# lifecycle core (serve, fetch, Ctrl+C/SIGINT, SIGTERM, force-kill with no
# leftover child); other systems skip. Bounded waits; every server started
# here is stopped and checked; the EXIT trap kills anything remaining.
set -u
set -m   # job control: background jobs keep SIGINT (non-interactive bash would ignore it)

case "$(uname -s)" in
    Linux) FULL=1 ;;
    Darwin) FULL=0 ;;
    *) echo "SKIP: serve_smoke.sh runs on Linux (full) and macOS (lifecycle core); Windows uses tests/serve_windows_lifecycle.ps1"; exit 0 ;;
esac

NIFT_BIN=${NIFT_BIN:-$(pwd)/nift}
NIFT_BIN=$(cd "$(dirname "$NIFT_BIN")" && pwd)/$(basename "$NIFT_BIN")
PY=$(command -v python3) || { echo "SKIP: python3 not available"; exit 0; }
TIMEOUT=$(command -v timeout || true)
realpath_py() { "$PY" -c 'import os,sys;print(os.path.realpath(sys.argv[1]))' "$1"; }
WORK=$(realpath_py "$(mktemp -d "${TMPDIR:-/tmp}/nift-serve-smoke.XXXXXX")")
PIDS=()
fail=0; passes=0
START_ENV=()

cleanup() {
    for p in ${PIDS[@]+"${PIDS[@]}"}; do kill -KILL "$p" 2>/dev/null; done
    [ -f "$WORK/holder.pid" ] && kill -KILL "$(cat "$WORK/holder.pid")" 2>/dev/null
    pkill -KILL -f "sleep 3[123]\.5" 2>/dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT

ok()  { passes=$((passes+1)); echo "ok   - $1"; }
bad() { fail=1; echo "FAIL - $1"; [ -n "${2:-}" ] && [ -f "$2" ] && sed 's/^/       /' "$2"; }
skip() { echo "skip - $1"; }

free_port() { "$PY" -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])'; }
# port_free HOST PORT: 0 when nothing is listening (connection refused).
# A connect probe, not a bind: accepted connections leave TIME_WAIT behind.
port_free() { "$PY" - "$1" "$2" <<'EOF'
import socket, sys
h, p = sys.argv[1], int(sys.argv[2])
try: socket.create_connection((h, p), timeout=2).close()
except OSError: sys.exit(0)
sys.exit(1)
EOF
}
has_ipv6() { "$PY" -c 'import socket;s=socket.socket(socket.AF_INET6);s.bind(("::1",0))' 2>/dev/null; }
# fetch URL OUTFILE: GET with a short timeout; 0 on HTTP 200.
fetch() { "$PY" - "$1" "$2" <<'EOF'
import sys, urllib.request
try:
    data = urllib.request.urlopen(sys.argv[1], timeout=3).read()
except Exception as e:
    print(e, file=sys.stderr); sys.exit(1)
open(sys.argv[2], "wb").write(data)
EOF
}
http_status() { "$PY" - "$1" <<'EOF'
import sys, urllib.request, urllib.error
try: print(urllib.request.urlopen(sys.argv[1], timeout=3).status)
except urllib.error.HTTPError as e: print(e.code)
except Exception: print("down")
EOF
}
# wait_gone PATTERN: up to 5 s for no process matching PATTERN (pgrep -f).
wait_gone() { for _ in $(seq 50); do pgrep -f "$1" >/dev/null || return 0; sleep 0.1; done; return 1; }
# hold_port HOST PORT: a listener owned by the test; sets HOLD.
hold_port() {
    "$PY" -c 'import socket,sys,time;s=socket.socket();s.bind((sys.argv[1],int(sys.argv[2])));s.listen();print(1,flush=True);time.sleep(60)' "$1" "$2" > "$WORK/hold.out" &
    HOLD=$!; PIDS+=("$HOLD")
    for _ in $(seq 50); do [ -s "$WORK/hold.out" ] && return 0; sleep 0.1; done
    return 1
}
release_port() { kill "$HOLD" 2>/dev/null; wait "$HOLD" 2>/dev/null; rm -f "$WORK/hold.out"; }

# start NAME [serve args...]: launches in the background (cwd = $RUN_CWD,
# extra environment = START_ENV), waits up to 10 s for "Serving HTTP".
start() {
    local name=$1; shift
    (cd "$RUN_CWD" && exec env ${START_ENV[@]+"${START_ENV[@]}"} "$NIFT_BIN" serve "$@") >"$WORK/$name.out" 2>"$WORK/$name.err" &
    SPID=$!; PIDS+=("$SPID")
    for _ in $(seq 100); do
        grep -q "Serving HTTP" "$WORK/$name.out" 2>/dev/null && return 0
        kill -0 "$SPID" 2>/dev/null || return 1
        sleep 0.1
    done
    return 1
}
# stop SIG: signals $SPID, waits up to 10 s, sets STATUS.
stop() {
    kill "-$1" "$SPID" 2>/dev/null
    for _ in $(seq 100); do kill -0 "$SPID" 2>/dev/null || break; sleep 0.1; done
    if kill -0 "$SPID" 2>/dev/null; then kill -KILL "$SPID"; STATUS=timeout; wait "$SPID" 2>/dev/null; return 1; fi
    wait "$SPID"; STATUS=$?
}
# run_err NAME [args...]: foreground serve that must fail fast (10 s bound).
run_err() {
    local name=$1; shift
    (cd "$RUN_CWD" && exec "$TIMEOUT" 10 "$NIFT_BIN" serve "$@") >"$WORK/$name.out" 2>"$WORK/$name.err"
    STATUS=$?
}
# run_path NAME PATHVALUE [args...]: like run_err with a replaced PATH.
run_path() {
    local name=$1 path=$2; shift 2
    (cd "$PROJ" && PATH="$path" exec "$TIMEOUT" 20 "$NIFT_BIN" serve "$@") >"$WORK/$name.out" 2>"$WORK/$name.err"
    STATUS=$?
}
tree_hash() { (cd "$1" && find . -print0 | LC_ALL=C sort -z | xargs -0 stat -c '%n %s %Y %f' && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum) | sha256sum; }

# --- fixture project -------------------------------------------------------
PROJ="$WORK/site"; mkdir -p "$PROJ"
(cd "$PROJ" && "$NIFT_BIN" init >/dev/null 2>&1 && "$NIFT_BIN" build >/dev/null 2>&1) || { echo "FAIL - fixture init/build"; exit 1; }
mkdir -p "$PROJ/content/sub"
RUN_CWD="$PROJ/content/sub"   # discovery walks up from a subdirectory
head -c 4096 /dev/urandom > "$PROJ/public/blob.bin"
[ "$FULL" = 1 ] && BEFORE=$(tree_hash "$PROJ")

# ===== portable lifecycle core (Linux + macOS) ==============================

# 1. default output dir + custom port; byte-exact fetch; request log; SIGINT
P=$(free_port)
if start default --port "$P"; then
    grep -qx "Serving: $PROJ/public" "$WORK/default.out" && grep -qx "URL:     http://127.0.0.1:$P/" "$WORK/default.out" \
        && grep -q "^Press Ctrl+C to stop.$" "$WORK/default.out" && ok "banner (default dir, custom port)" || bad "banner" "$WORK/default.out"
    if [ "$FULL" = 1 ]; then
        # Ignore descriptors the test shell itself inherited (fds 0-2, a
        # `flock` lock file, a harness socket): only what Nift/Python opened
        # counts.
        inherited=$(for fd in /proc/$$/fd/*; do readlink "$fd"; done 2>/dev/null | sort -u)
        fds=$(for fd in /proc/$SPID/fd/*; do [ "${fd##*/}" -ge 3 ] && readlink "$fd"; done 2>/dev/null \
              | grep -vxF -f <(printf '%s\n' "$inherited"))
        socks=$(printf '%s\n' "$fds" | grep -c "^socket:")
        # stdout/stderr point into $WORK (the test's own redirects); anything
        # else under a temp dir, or an unlinked file, would be a Nift temp file.
        if echo "$fds" | grep -v "^$WORK/" | grep -q -e '^/tmp' -e '^/var/tmp' -e '(deleted)'; then bad "server holds a temporary file"; else ok "no temporary files held by the server"; fi
        echo "$fds" > "$WORK/fds.txt"
        [ "$socks" = 1 ] && ok "only the listening socket is open (probe socket not inherited)" || bad "server has $socks sockets" "$WORK/fds.txt"
        [ "$(cat /proc/$SPID/comm 2>/dev/null)" = "python3" ] && ok "nift exec'd Python (same PID)" || bad "exec: comm=$(cat /proc/$SPID/comm 2>/dev/null)"
    fi
    fetch "http://127.0.0.1:$P/blob.bin" "$WORK/blob.got" && cmp -s "$WORK/blob.got" "$PROJ/public/blob.bin" \
        && ok "HTTP fetch matches bytes" || bad "HTTP fetch" "$WORK/default.err"
    fetch "http://127.0.0.1:$P/index.html" "$WORK/index.got" && cmp -s "$WORK/index.got" "$PROJ/public/index.html" \
        && ok "index.html fetched" || bad "index.html fetch"
    grep -q '"GET /blob.bin HTTP/1.1" 200' "$WORK/default.err" && ok "request log visible on stderr" || bad "request log" "$WORK/default.err"
    stop INT; [ "$STATUS" = 0 ] && ok "SIGINT exits 0" || bad "SIGINT status $STATUS"
    port_free 127.0.0.1 "$P" && ! kill -0 "$SPID" 2>/dev/null && ok "SIGINT: no leftover process, port released" || bad "SIGINT leftovers"
    wait_gone "http.server $P" && ok "SIGINT: no orphan by command line" || bad "orphan http.server $P"
else bad "default start" "$WORK/default.err"; fi

# 2. SIGTERM: no orphan, status reflects the signal
P=$(free_port)
if start term --port "$P"; then
    stop TERM; [ "$STATUS" = 143 ] && ok "SIGTERM status 143" || bad "SIGTERM status $STATUS"
    port_free 127.0.0.1 "$P" && wait_gone "http.server $P" && ok "SIGTERM: no orphan" || bad "SIGTERM orphan"
else bad "term start" "$WORK/term.err"; fi

# 3. force-kill nift (SIGKILL to the PID the user started): no child left,
#    port free within 5 s. With exec there is no separate child by design.
P=$(free_port)
if start forcekill --port "$P"; then
    fetch "http://127.0.0.1:$P/blob.bin" "$WORK/fk.got" && cmp -s "$WORK/fk.got" "$PROJ/public/blob.bin" && ok "force-kill: fetched before kill" || bad "force-kill fetch"
    stop KILL; [ "$STATUS" = 137 ] && ok "force-kill: status 137" || bad "force-kill status $STATUS"
    wait_gone "http.server $P" && port_free 127.0.0.1 "$P" && ok "force-kill: no Python child left, port free" || bad "force-kill left a server"
else bad "forcekill start" "$WORK/forcekill.err"; fi

if [ "$FULL" != 1 ]; then
    for p in ${PIDS[@]+"${PIDS[@]}"}; do kill -0 "$p" 2>/dev/null && { bad "leftover pid $p"; kill -KILL "$p"; }; done
    echo "serve_smoke (macOS lifecycle core): $passes passed, $([ $fail = 0 ] && echo 0 || echo some) failed"
    exit $fail
fi

# ===== Linux-only checks ====================================================

# 4. --dir outside the project with spaces; = spelling; custom host. The test
#    itself holds 127.0.0.1:P, so the server can only have bound 127.0.0.2:P
#    (a wildcard or 127.0.0.1 bind would have failed).
OUT="$WORK/other dir/with spaces"; mkdir -p "$OUT"; printf 'outside\n' > "$OUT/a file.txt"
P=$(free_port)
if hold_port 127.0.0.1 "$P" && start spaces "--dir=$OUT" --host 127.0.0.2 --port "$P"; then
    grep -qx "Serving: $OUT" "$WORK/spaces.out" && grep -qx "URL:     http://127.0.0.2:$P/" "$WORK/spaces.out" && ok "--dir with spaces + custom host banner" || bad "spaces banner" "$WORK/spaces.out"
    fetch "http://127.0.0.2:$P/a%20file.txt" "$WORK/a.got" && cmp -s "$WORK/a.got" "$OUT/a file.txt" \
        && ok "custom host 127.0.0.2 served while 127.0.0.1:$P is held by the test" || bad "spaces fetch" "$WORK/spaces.err"
    stop INT; [ "$STATUS" = 0 ] && ok "stop --dir server" || bad "stop spaces $STATUS"
else bad "spaces start" "$WORK/spaces.err"; fi
release_port

# 5. relative --dir; no project needed when --dir is given
mkdir -p "$WORK/noproj/www"; printf 'np\n' > "$WORK/noproj/www/x.txt"
SAVE=$RUN_CWD; RUN_CWD="$WORK/noproj"; P=$(free_port)
if start noproj --dir www --port "$P"; then
    grep -qx "Serving: $WORK/noproj/www" "$WORK/noproj.out" && fetch "http://127.0.0.1:$P/x.txt" "$WORK/x.got" && ok "no project + relative --dir served" || bad "noproj" "$WORK/noproj.out"
    stop INT; [ "$STATUS" = 0 ] && ok "stop no-project server" || bad "stop noproj $STATUS"
else bad "noproj start" "$WORK/noproj.err"; fi
run_err noproj2
[ "$STATUS" = 1 ] && grep -q "not a Nift project" "$WORK/noproj2.err" && grep -q -- "--dir" "$WORK/noproj2.err" && ok "no project, no --dir: clear error" || bad "noproj2 $STATUS" "$WORK/noproj2.err"
RUN_CWD=$SAVE

# 6. IPv6 host (skipped if ::1 unavailable)
if has_ipv6; then
    P=$(free_port)
    if start v6 --host ::1 --port "$P"; then
        grep -qx "URL:     http://\[::1\]:$P/" "$WORK/v6.out" && fetch "http://[::1]:$P/index.html" "$WORK/v6.got" && ok "IPv6 ::1 served, URL bracketed" || bad "v6" "$WORK/v6.out"
        stop INT; [ "$STATUS" = 0 ] && ok "stop IPv6 server" || bad "stop v6 $STATUS"
    else bad "v6 start" "$WORK/v6.err"; fi
else skip "IPv6 ::1 not available on this host"; fi

# 7. symlink + '..': `link/../www` is resolved by the kernel (parent of the
#    link's target), not lexically (which would be ./www).
L="$WORK/linktest"; mkdir -p "$L/real/inner" "$L/real/www" "$L/www"
printf 'kernel\n' > "$L/real/www/which.txt"; printf 'lexical\n' > "$L/www/which.txt"
ln -s real/inner "$L/link"
SAVE=$RUN_CWD; RUN_CWD="$L"; P=$(free_port)
if start dotdot --dir link/../www --port "$P"; then
    grep -qx "Serving: $L/real/www" "$WORK/dotdot.out" && fetch "http://127.0.0.1:$P/which.txt" "$WORK/which.got" && [ "$(cat "$WORK/which.got")" = kernel ] \
        && ok "--dir link/../www serves and shows the kernel-resolved dir" || bad "dotdot" "$WORK/dotdot.out"
    stop INT
else bad "dotdot start" "$WORK/dotdot.err"; fi
RUN_CWD=$SAVE
# Same for a configured output-dir (on a copy of the project).
cp -a "$PROJ" "$WORK/site-link"; mkdir -p "$WORK/site-link/real/inner" "$WORK/site-link/real/out" "$WORK/site-link/out"
printf 'kernel\n' > "$WORK/site-link/real/out/which.txt"; printf 'lexical\n' > "$WORK/site-link/out/which.txt"
ln -s real/inner "$WORK/site-link/link"
"$PY" - "$WORK/site-link/.nift/config.json" <<'EOF'
import json, sys
p = sys.argv[1]; d = json.load(open(p)); d["config"]["output-dir"] = "link/../out/"; json.dump(d, open(p, "w"), indent=2)
EOF
SAVE=$RUN_CWD; RUN_CWD="$WORK/site-link"; P=$(free_port)
if start cfglink --port "$P"; then
    grep -qx "Serving: $WORK/site-link/real/out" "$WORK/cfglink.out" && fetch "http://127.0.0.1:$P/which.txt" "$WORK/cw.got" && [ "$(cat "$WORK/cw.got")" = kernel ] \
        && ok "configured output-dir link/../out resolved by the kernel" || bad "cfglink" "$WORK/cfglink.out"
    stop INT
elif grep -q "config" "$WORK/cfglink.err"; then skip "config rejects output-dir 'link/../out/': $(head -1 "$WORK/cfglink.err")"
else bad "cfglink start" "$WORK/cfglink.err"; fi
RUN_CWD=$SAVE

# 8. missing output dir / missing --dir / --dir is a file
cp -a "$PROJ" "$WORK/site-copy"; rm -rf "$WORK/site-copy/public"
SAVE=$RUN_CWD; RUN_CWD="$WORK/site-copy"
run_err missing
[ "$STATUS" = 1 ] && grep -q "output directory does not exist" "$WORK/missing.err" && grep -q "nift build" "$WORK/missing.err" && ok "missing output dir suggests nift build" || bad "missing $STATUS" "$WORK/missing.err"
RUN_CWD=$SAVE
run_err missing2 --dir "$WORK/nope"
[ "$STATUS" = 1 ] && grep -q "directory does not exist" "$WORK/missing2.err" && ok "missing --dir" || bad "missing2" "$WORK/missing2.err"
run_err isfile --dir "$PROJ/public/blob.bin"
[ "$STATUS" = 1 ] && grep -q "not a directory" "$WORK/isfile.err" && ok "--dir is a file" || bad "isfile" "$WORK/isfile.err"

# 9. option validation and help
port_ok=1
for bad_port in 0 65536 abc -1 ""; do
    run_err port "--port=$bad_port"
    if ! { [ "$STATUS" = 1 ] && grep -q "invalid port" "$WORK/port.err"; }; then port_ok=0; bad "port '$bad_port' accepted (status $STATUS)" "$WORK/port.err"; fi
done
[ "$port_ok" = 1 ] && ok "port range validated (0, 65536, abc, -1, empty)"
run_err opt --bogus; [ "$STATUS" = 1 ] && grep -q "unknown serve option" "$WORK/opt.err" && ok "unknown option" || bad "unknown option"
run_err dup --port 1 --port 2; [ "$STATUS" = 1 ] && grep -q "more than once" "$WORK/dup.err" && ok "duplicate option" || bad "duplicate option"
run_err novalue --port; [ "$STATUS" = 1 ] && grep -q "requires a value" "$WORK/novalue.err" && ok "missing option value" || bad "missing value"
run_err dashvalue --dir --port 8000; [ "$STATUS" = 1 ] && grep -q -- "--dir requires a value" "$WORK/dashvalue.err" && ok "--dir --port: value starting with -- is a missing value" || bad "dashvalue" "$WORK/dashvalue.err"
for h in --help -h; do
    run_err help "$h"
    [ "$STATUS" = 0 ] && grep -q "usage: nift serve" "$WORK/help.out" && ok "serve $h" || bad "serve $h status $STATUS" "$WORK/help.out"
done
run_err host --host no.such.host.invalid --port "$(free_port)"
[ "$STATUS" = 1 ] && grep -q "cannot resolve host" "$WORK/host.err" && ok "unresolvable host" || bad "bad host" "$WORK/host.err"

# 10. Python discovery: missing, Python 2 only, failing probe stderr shown,
#     hanging probe bounded, probe dies with nift.
EMPTY="$WORK/emptybin"; mkdir -p "$EMPTY"
run_path nopy "$EMPTY" --port "$(free_port)"
[ "$STATUS" = 1 ] && grep -q "requires Python 3.8" "$WORK/nopy.err" && grep -q "tried python3: not found" "$WORK/nopy.err" && ! grep -q "Nift development server" "$WORK/nopy.out" \
    && ok "missing Python: clear error, no banner" || bad "nopy $STATUS" "$WORK/nopy.err"
PY2="$WORK/py2bin"; mkdir -p "$PY2"
printf '#!/bin/sh\nexit 3\n' > "$PY2/python"; chmod +x "$PY2/python"
run_path py2 "$PY2" --port "$(free_port)"
[ "$STATUS" = 1 ] && grep -q "python ($PY2/python): not Python 3.8 or newer" "$WORK/py2.err" && ok "Python 2 only: rejected" || bad "py2 $STATUS" "$WORK/py2.err"
NOEXEC="$WORK/noexecbin"; mkdir -p "$NOEXEC"
printf '#!/bin/sh\nexit 0\n' > "$NOEXEC/python3"; chmod 0644 "$NOEXEC/python3"
run_path noexec "$NOEXEC" --port "$(free_port)"
[ "$STATUS" = 1 ] && grep -q "python3 ($NOEXEC/python3): not executable" "$WORK/noexec.err" && ok "non-executable python3 on PATH: reported as not executable" || bad "noexec $STATUS" "$WORK/noexec.err"
SHIM="$WORK/shimbin"; mkdir -p "$SHIM"
printf '#!/bin/sh\necho "pyenv: version \\`3.12.1'"'"' is not installed" >&2\nexit 1\n' > "$SHIM/python3"; chmod +x "$SHIM/python3"
run_path shim "$SHIM" --port "$(free_port)"
[ "$STATUS" = 1 ] && grep -q "version check failed with status 1" "$WORK/shim.err" && grep -q "pyenv: version" "$WORK/shim.err" \
    && ok "failed probe shows the interpreter's stderr (pyenv shim)" || bad "shim $STATUS" "$WORK/shim.err"
HANG="$WORK/hangbin"; mkdir -p "$HANG"
printf '#!/bin/sh\nexec /bin/sleep 31.5\n' > "$HANG/python3"; chmod +x "$HANG/python3"
t0=$(date +%s); run_path hang "$HANG" --port "$(free_port)"; t1=$(date +%s)
[ "$STATUS" = 1 ] && grep -q "did not respond to a version check within 5s" "$WORK/hang.err" && [ $((t1 - t0)) -le 8 ] \
    && ok "hanging probe: bounded to 5s ($((t1 - t0))s), clear error" || bad "hang $STATUS in $((t1 - t0))s" "$WORK/hang.err"
wait_gone "sleep 31.5" && ok "hanging probe killed" || bad "hanging probe left running"
printf '#!/bin/sh\nexec sleep 32.5\n' > "$HANG/python3"
(cd "$PROJ" && PATH="$HANG:/usr/bin:/bin" exec "$NIFT_BIN" serve --port "$(free_port)") >/dev/null 2>&1 &
NP=$!; PIDS+=("$NP")
for _ in $(seq 50); do pgrep -f "sleep 32.5" >/dev/null && break; sleep 0.1; done
if pgrep -f "sleep 32.5" >/dev/null; then
    kill -KILL "$NP"; wait "$NP" 2>/dev/null
    wait_gone "sleep 32.5" && ok "nift killed during the probe: probe dies too (PDEATHSIG)" || bad "probe orphaned after nift was killed"
else bad "probe did not start"; fi

# 11. occupied port: Nift probe error, nothing left running
P=$(free_port)
hold_port 127.0.0.1 "$P"
run_err busy --port "$P"
[ "$STATUS" = 1 ] && grep -q "port $P is already in use" "$WORK/busy.err" && ! grep -q "Nift development server" "$WORK/busy.out" && ok "occupied port: Nift error before launch" || bad "busy $STATUS" "$WORK/busy.err"
release_port

# 12. race: port taken after the probe. A python3 wrapper occupies the port
#     when (and only when) asked to run http.server, then execs the real
#     interpreter; Python's own bind failure must be visible and nonzero.
RACE="$WORK/racebin"; mkdir -p "$RACE"; P=$(free_port)
cat > "$RACE/python3" <<EOF
#!/bin/sh
case "\$*" in *http.server*)
  "$PY" -c 'import socket,time;s=socket.socket();s.bind(("127.0.0.1",$P));s.listen();open("$WORK/holder.ready","w").close();time.sleep(30)' &
  echo \$! > "$WORK/holder.pid"
  i=0; while [ ! -f "$WORK/holder.ready" ] && [ \$i -lt 100 ]; do sleep 0.05; i=\$((i+1)); done ;;
esac
exec "$PY" "\$@"
EOF
chmod +x "$RACE/python3"
run_path race "$RACE:/usr/bin:/bin" --port "$P"
[ "$STATUS" != 0 ] && [ "$STATUS" != 124 ] && grep -q "Address already in use" "$WORK/race.err" && grep -q "Nift development server" "$WORK/race.out" \
    && ok "port lost after probe: Python's bind error visible, status $STATUS" || bad "race $STATUS" "$WORK/race.err"
[ -f "$WORK/holder.pid" ] && kill "$(cat "$WORK/holder.pid")" 2>/dev/null; rm -f "$WORK/holder.pid"

# 13. served directory disappears while running: 404, server stays up
GONE="$WORK/gone"; mkdir -p "$GONE"; echo hi > "$GONE/f.txt"; P=$(free_port)
if start gone --dir "$GONE" --port "$P"; then
    rm -rf "$GONE"
    s=$(http_status "http://127.0.0.1:$P/f.txt"); [ "$s" = 404 ] && kill -0 "$SPID" && ok "dir removed while serving: 404, still running" || bad "gone status $s" "$WORK/gone.err"
    stop INT; [ "$STATUS" = 0 ] && ok "stop after dir removal" || bad "gone stop $STATUS"
else bad "gone start" "$WORK/gone.err"; fi

# 14. isolation: modules in the cwd (and on PYTHONPATH) must not shadow the
#     standard library, and nothing writes __pycache__.
SHADOW="$WORK/shadow"; mkdir -p "$SHADOW/www"; echo s > "$SHADOW/www/s.txt"
for m in socketserver mimetypes; do printf 'open(%s, "w").write("ran")\n' "\"$WORK/shadow-$m-ran\"" > "$SHADOW/$m.py"; done
SAVE=$RUN_CWD; RUN_CWD="$SHADOW"; START_ENV=(PYTHONPATH="$SHADOW" PYTHONDONTWRITEBYTECODE=); P=$(free_port)
if start shadow --dir www --port "$P"; then
    fetch "http://127.0.0.1:$P/s.txt" "$WORK/s.got" && ok "served from a cwd with shadowing modules" || bad "shadow fetch" "$WORK/shadow.err"
    stop INT
else bad "shadow start" "$WORK/shadow.err"; fi
START_ENV=(); RUN_CWD=$SAVE
ls "$WORK"/shadow-*-ran >/dev/null 2>&1 && bad "cwd/PYTHONPATH module shadowed the standard library" || ok "socketserver.py/mimetypes.py in cwd and PYTHONPATH not imported (-I)"
[ -z "$(find "$SHADOW" "$PROJ" -name __pycache__ 2>/dev/null)" ] && ok "no __pycache__ written in cwd or project" || bad "__pycache__ written"

# 15. nift serve modified nothing in the project (content, .nift metadata, deps, mtimes)
AFTER=$(tree_hash "$PROJ")
[ "$BEFORE" = "$AFTER" ] && ok "project tree unchanged (hash incl. .nift, sizes, mtimes)" || bad "project tree changed"

# No leftover servers from this run.
for p in ${PIDS[@]+"${PIDS[@]}"}; do kill -0 "$p" 2>/dev/null && { bad "leftover pid $p"; kill -KILL "$p"; }; done
pgrep -f "http.server" -a 2>/dev/null | grep -F "$WORK" >/dev/null && bad "an http.server serving under $WORK is still running"
echo "serve_smoke: $passes passed, $([ $fail = 0 ] && echo 0 || echo some) failed"
exit $fail
