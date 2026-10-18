#!/usr/bin/env bash
# `nift serve` smoke test (Linux). Bounded waits; every server started here is
# stopped and checked for leftovers; the EXIT trap kills anything remaining.
set -u
set -m   # job control: background jobs keep SIGINT (non-interactive bash would ignore it)

NIFT_BIN=${NIFT_BIN:-$(pwd)/nift}
NIFT_BIN=$(cd "$(dirname "$NIFT_BIN")" && pwd)/$(basename "$NIFT_BIN")
TIMEOUT=$(command -v timeout)
PY=$(command -v python3) || { echo "SKIP: python3 not available"; exit 0; }
WORK=$(mktemp -d "${TMPDIR:-/tmp}/nift-serve-smoke.XXXXXX")
PIDS=()
fail=0; passes=0

cleanup() {
    for p in "${PIDS[@]}"; do kill -KILL "$p" 2>/dev/null; done
    [ -f "$WORK/holder.pid" ] && kill -KILL "$(cat "$WORK/holder.pid")" 2>/dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT

ok()  { passes=$((passes+1)); echo "ok   - $1"; }
bad() { fail=1; echo "FAIL - $1"; [ -n "${2:-}" ] && sed 's/^/       /' "$2"; }

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

# start NAME [serve args...]: launches in the background (cwd = $RUN_CWD),
# waits up to 10 s for the "Serving HTTP" line or an early exit.
start() {
    local name=$1; shift
    (cd "$RUN_CWD" && exec "$NIFT_BIN" serve "$@") >"$WORK/$name.out" 2>"$WORK/$name.err" &
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
    (cd "$RUN_CWD" && exec timeout 10 "$NIFT_BIN" serve "$@") >"$WORK/$name.out" 2>"$WORK/$name.err"
    STATUS=$?
}
tree_hash() { (cd "$1" && find . -print0 | LC_ALL=C sort -z | xargs -0 stat -c '%n %s %Y %f' && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum) | sha256sum; }

# --- fixture project -------------------------------------------------------
PROJ="$WORK/site"; mkdir -p "$PROJ"
(cd "$PROJ" && "$NIFT_BIN" init >/dev/null 2>&1 && "$NIFT_BIN" build >/dev/null 2>&1) || { echo "FAIL - fixture init/build"; exit 1; }
mkdir -p "$PROJ/content/sub"
RUN_CWD="$PROJ/content/sub"   # discovery walks up from a subdirectory
head -c 4096 /dev/urandom > "$PROJ/public/blob.bin"
BEFORE=$(tree_hash "$PROJ")

# 1. default output dir, default host, custom port, byte-exact fetch, request log, SIGINT
P=$(free_port)
if start default --port "$P"; then
    grep -qx "Serving: $PROJ/public" "$WORK/default.out" && grep -qx "URL:     http://127.0.0.1:$P/" "$WORK/default.out" \
        && grep -q "^Press Ctrl+C to stop.$" "$WORK/default.out" && ok "banner (default dir, custom port)" || bad "banner" "$WORK/default.out"
    # fds 0-2 are inherited from whoever runs the test; count only fds >= 3.
    socks=$(for fd in /proc/$SPID/fd/*; do [ "${fd##*/}" -ge 3 ] && readlink "$fd"; done 2>/dev/null | grep -c "^socket:")
    fds=$(for fd in /proc/$SPID/fd/*; do readlink "$fd"; done 2>/dev/null)
    # stdout/stderr point into $WORK (the test's own redirects); anything else
    # under a temp dir, or an unlinked file, would be a Nift temporary file.
    if echo "$fds" | grep -v "^$WORK/" | grep -q -e '^/tmp' -e '^/var/tmp' -e '(deleted)'; then bad "server holds a temporary file"; else ok "no temporary files held by the server"; fi
    echo "$fds" > "$WORK/fds.txt"; [ "$socks" = 1 ] && ok "only the listening socket is open (probe socket not inherited)" || bad "server has $socks sockets" "$WORK/fds.txt"
    fetch "http://127.0.0.1:$P/blob.bin" "$WORK/blob.got" && cmp -s "$WORK/blob.got" "$PROJ/public/blob.bin" \
        && ok "HTTP fetch matches bytes" || bad "HTTP fetch" "$WORK/default.err"
    fetch "http://127.0.0.1:$P/index.html" "$WORK/index.got" && cmp -s "$WORK/index.got" "$PROJ/public/index.html" \
        && ok "index.html fetched" || bad "index.html fetch"
    grep -q '"GET /blob.bin HTTP/1.1" 200' "$WORK/default.err" && ok "request log visible on stderr" || bad "request log" "$WORK/default.err"
    [ "$(cat /proc/$SPID/comm 2>/dev/null)" = "python3" ] && ok "nift exec'd Python (same PID)" || bad "exec: /proc/$SPID/comm=$(cat /proc/$SPID/comm 2>/dev/null)"
    stop INT; [ "$STATUS" = 0 ] && ok "SIGINT exits 0" || bad "SIGINT status $STATUS"
    port_free 127.0.0.1 "$P" && ! kill -0 "$SPID" 2>/dev/null && ok "SIGINT: no leftover process, port released" || bad "SIGINT leftovers"
    pgrep -f "http.server $P" >/dev/null && bad "orphan http.server $P" || ok "SIGINT: no orphan by command line"
else bad "default start" "$WORK/default.err"; fi

# 2. SIGTERM: no orphan, status reflects the signal
P=$(free_port)
if start term --port "$P"; then
    stop TERM; [ "$STATUS" = 143 ] && ok "SIGTERM status 143" || bad "SIGTERM status $STATUS"
    port_free 127.0.0.1 "$P" && ! pgrep -f "http.server $P" >/dev/null && ok "SIGTERM: no orphan" || bad "SIGTERM orphan"
else bad "term start" "$WORK/term.err"; fi

# 3. --dir outside the project, with spaces; = spelling; custom host
OUT="$WORK/other dir/with spaces"; mkdir -p "$OUT"; printf 'outside\n' > "$OUT/a file.txt"
P=$(free_port)
if start spaces "--dir=$OUT" --host 127.0.0.2 --port "$P"; then
    grep -qx "Serving: $OUT" "$WORK/spaces.out" && grep -qx "URL:     http://127.0.0.2:$P/" "$WORK/spaces.out" && ok "--dir with spaces + custom host banner" || bad "spaces banner" "$WORK/spaces.out"
    fetch "http://127.0.0.2:$P/a%20file.txt" "$WORK/a.got" && cmp -s "$WORK/a.got" "$OUT/a file.txt" && ok "fetch from --dir outside project on 127.0.0.2" || bad "spaces fetch" "$WORK/spaces.err"
    [ "$(http_status "http://127.0.0.1:$P/")" = down ] && ok "custom host: not bound on 127.0.0.1" || bad "custom host leaked to 127.0.0.1"
    stop INT; [ "$STATUS" = 0 ] && ok "stop --dir server" || bad "stop spaces $STATUS"
else bad "spaces start" "$WORK/spaces.err"; fi

# 4. relative --dir; no project needed when --dir is given
mkdir -p "$WORK/noproj/www"; printf 'np\n' > "$WORK/noproj/www/x.txt"
SAVE=$RUN_CWD; RUN_CWD="$WORK/noproj"; P=$(free_port)
if start noproj --dir www --port "$P"; then
    grep -qx "Serving: $WORK/noproj/www" "$WORK/noproj.out" && fetch "http://127.0.0.1:$P/x.txt" "$WORK/x.got" && ok "no project + relative --dir served" || bad "noproj" "$WORK/noproj.out"
    stop INT
else bad "noproj start" "$WORK/noproj.err"; fi
run_err noproj2
[ "$STATUS" = 1 ] && grep -q "not a Nift project" "$WORK/noproj2.err" && grep -q -- "--dir" "$WORK/noproj2.err" && ok "no project, no --dir: clear error" || bad "noproj2 $STATUS" "$WORK/noproj2.err"
RUN_CWD=$SAVE

# 5. IPv6 host (skipped if ::1 unavailable)
if has_ipv6; then
    P=$(free_port)
    if start v6 --host ::1 --port "$P"; then
        grep -qx "URL:     http://\[::1\]:$P/" "$WORK/v6.out" && fetch "http://[::1]:$P/index.html" "$WORK/v6.got" && ok "IPv6 ::1 served, URL bracketed" || bad "v6" "$WORK/v6.out"
        stop INT
    else bad "v6 start" "$WORK/v6.err"; fi
else echo "skip - IPv6 ::1 not available"; fi

# 6. missing output dir / missing --dir / --dir is a file
cp -a "$PROJ" "$WORK/site-copy"; rm -rf "$WORK/site-copy/public"
SAVE=$RUN_CWD; RUN_CWD="$WORK/site-copy"
run_err missing
[ "$STATUS" = 1 ] && grep -q "output directory does not exist" "$WORK/missing.err" && grep -q "nift build" "$WORK/missing.err" && ok "missing output dir suggests nift build" || bad "missing $STATUS" "$WORK/missing.err"
RUN_CWD=$SAVE
run_err missing2 --dir "$WORK/nope"
[ "$STATUS" = 1 ] && grep -q "directory does not exist" "$WORK/missing2.err" && ok "missing --dir" || bad "missing2" "$WORK/missing2.err"
run_err isfile --dir "$PROJ/public/blob.bin"
[ "$STATUS" = 1 ] && grep -q "not a directory" "$WORK/isfile.err" && ok "--dir is a file" || bad "isfile" "$WORK/isfile.err"

# 7. option validation
for bad_port in 0 65536 abc -1 ""; do
    run_err port --port "$bad_port"
    [ "$STATUS" = 1 ] && grep -q "invalid port" "$WORK/port.err" || bad "port '$bad_port' accepted" "$WORK/port.err"
done; ok "port range validated"
run_err opt --bogus; [ "$STATUS" = 1 ] && grep -q "unknown serve option" "$WORK/opt.err" && ok "unknown option" || bad "unknown option"
run_err dup --port 1 --port 2; [ "$STATUS" = 1 ] && grep -q "more than once" "$WORK/dup.err" && ok "duplicate option" || bad "duplicate option"
run_err novalue --port; [ "$STATUS" = 1 ] && grep -q "requires a value" "$WORK/novalue.err" && ok "missing option value" || bad "missing value"
run_err host --host no.such.host.invalid --port "$(free_port)"
[ "$STATUS" = 1 ] && grep -q "cannot resolve host" "$WORK/host.err" && ok "unresolvable host" || bad "bad host" "$WORK/host.err"

# 8. missing Python / Python 2 only (fake interpreters exit 3 like a <3.7 probe)
EMPTY="$WORK/emptybin"; mkdir -p "$EMPTY"
(cd "$PROJ" && PATH="$EMPTY" "$TIMEOUT" 10 "$NIFT_BIN" serve --port "$(free_port)") >"$WORK/nopy.out" 2>"$WORK/nopy.err"; STATUS=$?
[ "$STATUS" = 1 ] && grep -q "requires Python 3" "$WORK/nopy.err" && grep -q "tried python3: not found" "$WORK/nopy.err" && ! grep -q "Nift development server" "$WORK/nopy.out" \
    && ok "missing Python: clear error, no banner" || bad "nopy $STATUS" "$WORK/nopy.err"
PY2="$WORK/py2bin"; mkdir -p "$PY2"
printf '#!/bin/sh\necho "Python 2.7.18" >&2\nexit 3\n' > "$PY2/python"; chmod +x "$PY2/python"
(cd "$PROJ" && PATH="$PY2" "$TIMEOUT" 10 "$NIFT_BIN" serve --port "$(free_port)") >"$WORK/py2.out" 2>"$WORK/py2.err"; STATUS=$?
[ "$STATUS" = 1 ] && grep -q "python ($PY2/python): not Python 3.7 or newer" "$WORK/py2.err" && ok "Python 2 only: rejected" || bad "py2 $STATUS" "$WORK/py2.err"
if command -v python2 >/dev/null; then echo "note - real python2 present; fake used for determinism"; fi

# 9. occupied port: Nift probe error, nothing left running
P=$(free_port)
"$PY" -c 'import socket,sys,time;s=socket.socket();s.bind(("127.0.0.1",int(sys.argv[1])));s.listen();print(1,flush=True);time.sleep(30)' "$P" > "$WORK/hold.out" &
HOLD=$!; PIDS+=("$HOLD"); for _ in $(seq 50); do [ -s "$WORK/hold.out" ] && break; sleep 0.1; done
run_err busy --port "$P"
[ "$STATUS" = 1 ] && grep -q "port $P is already in use" "$WORK/busy.err" && ! grep -q "Nift development server" "$WORK/busy.out" && ok "occupied port: Nift error before launch" || bad "busy $STATUS" "$WORK/busy.err"
kill "$HOLD" 2>/dev/null; wait "$HOLD" 2>/dev/null

# 10. race: port taken after the probe. A python3 wrapper occupies the port
# when (and only when) it is asked to run http.server, then execs the real
# interpreter; Python's own bind failure must be visible and nonzero.
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
(cd "$PROJ" && PATH="$RACE:/usr/bin:/bin" timeout 10 "$NIFT_BIN" serve --port "$P") >"$WORK/race.out" 2>"$WORK/race.err"; STATUS=$?
[ "$STATUS" != 0 ] && [ "$STATUS" != 124 ] && grep -q "Address already in use" "$WORK/race.err" && grep -q "Nift development server" "$WORK/race.out" \
    && ok "port lost after probe: Python's bind error visible, status $STATUS" || bad "race $STATUS" "$WORK/race.err"
[ -f "$WORK/holder.pid" ] && kill "$(cat "$WORK/holder.pid")" 2>/dev/null; rm -f "$WORK/holder.pid"

# 11. served directory disappears while running: 404, server stays up, clean stop
GONE="$WORK/gone"; mkdir -p "$GONE"; echo hi > "$GONE/f.txt"; P=$(free_port)
if start gone --dir "$GONE" --port "$P"; then
    rm -rf "$GONE"
    s=$(http_status "http://127.0.0.1:$P/f.txt"); [ "$s" = 404 ] && kill -0 "$SPID" && ok "dir removed while serving: 404, still running" || bad "gone status $s" "$WORK/gone.err"
    stop INT; [ "$STATUS" = 0 ] && ok "stop after dir removal" || bad "gone stop $STATUS"
else bad "gone start" "$WORK/gone.err"; fi

# 12. nift serve modified nothing in the project (content, .nift metadata, deps, mtimes)
AFTER=$(tree_hash "$PROJ")
[ "$BEFORE" = "$AFTER" ] && ok "project tree unchanged (hash incl. .nift, sizes, mtimes)" || bad "project tree changed"

# No leftover servers from this run.
for p in "${PIDS[@]}"; do kill -0 "$p" 2>/dev/null && { bad "leftover pid $p"; kill -KILL "$p"; }; done
echo "serve_smoke: $passes passed, $([ $fail = 0 ] && echo 0 || echo some) failed"
exit $fail
