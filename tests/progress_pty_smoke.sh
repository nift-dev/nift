#!/usr/bin/env bash
# POSIX end-to-end coverage for the build-progress renderer. Runs the real Nift
# binary under a pseudo-terminal (via `script`) and reconstructs the terminal
# screen to assert that final messages are never written into an active progress
# line: no stale "building ..." prefix survives before the summary, on either the
# success or the failure path, and NO_COLOR keeps SGR colour sequences out of an
# interactive session. Skipped gracefully when `script` or `python3` is absent.
#
# Linux and macOS use different `script` CLIs:
#   Linux  (util-linux):  script -qec "CMD" FILE
#   macOS  (BSD/FreeBSD): script -q FILE /bin/sh -c "CMD"
# PTY_PLATFORM may override `uname -s`; the fake-`script` structural check below
# uses that to verify the Darwin argument order without a macOS runner.
set -euo pipefail
NIFT_BIN="${NIFT_BIN:?NIFT_BIN must point to the nift binary}"

if ! command -v script >/dev/null 2>&1; then
  echo "progress PTY smoke test SKIPPED ('script' unavailable): the PTY ordering guarantee is NOT verified" >&2
  exit 77
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "progress PTY smoke test SKIPPED ('python3' unavailable): the PTY ordering guarantee is NOT verified" >&2
  exit 77
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-progress-pty.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
P="$TMP/project"
mkdir -p "$P/.nift" "$P/content" "$P/templates" "$P/public" "$P/data"

cat > "$P/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":1,"incremental-mode":"modified"}}
JSON

printf '%s' '<!doctype html><title>$[page.title]</title><body>@json('"'"'data/big.json'"'"', d)@for(x : d.rows){<i>$[x.i]</i>}@content</body>' > "$P/templates/template.html"

python3 - "$P" <<'EOF'
import json, sys
root = sys.argv[1]
json.dump({"rows": [{"i": i} for i in range(600)]}, open(root + "/data/big.json", "w"))
tracked = [{"name": "p%d" % i, "title": "P %d" % i, "template": "templates/template.html"}
           for i in range(1, 1001)]
json.dump({"tracked": tracked}, open(root + "/.nift/tracked.json", "w"))
for i in range(1, 1001):
    with open(root + "/content/p%d.html" % i, "w") as f:
        f.write("---\ntitle: P %d\ntemplate: templates/template.html\n---\n<p>%d</p>\n" % (i, i))
EOF

# Invoke the build under a PTY. $1 = transcript file, $2... = build arguments.
# The inner command is assembled with printf '%q' so project or binary paths
# containing spaces or single quotes cannot change what is executed. The Nift
# command runs inside a wrapper that always prints an explicit status marker
# (__NIFT_PROGRESS_EXIT__=N) into the transcript, so the reported exit status
# never depends on which `script` implementation wrote the footer.
pty_invoke() {
  local transcript="$1"; shift
  local platform="${PTY_PLATFORM:-$(uname -s)}"
  local wrapper
  wrapper="{ cd $(printf '%q' "$P") && $(printf '%q' "$NIFT_BIN")"
  local arg
  for arg in "$@"; do wrapper+=" $(printf '%q' "$arg")"; done
  wrapper+=" ; } ; __status=\$? ; printf '\\n__NIFT_PROGRESS_EXIT__=%s\\n' \"\$__status\" ; exit \"\$__status\""
  case "$platform" in
    Darwin)
      script -q "$transcript" /bin/sh -c "$wrapper" >"$TMP/pty.out" 2>&1 || true
      ;;
    *)
      script -qec "$wrapper" "$transcript" >"$TMP/pty.out" 2>&1 || true
      ;;
  esac
}

# Reconstruct the terminal screen from a transcript and write the visible lines
# plus the child exit code (from the explicit marker) to "$TMP/screen.txt".
# $1 = transcript file.
pty_reconstruct() {
  python3 - "$1" "$TMP/screen.txt" <<'EOF'
import re, sys
transcript, out = sys.argv[1], sys.argv[2]
data = open(transcript, "rb").read().decode("utf-8", "replace")
marker = re.search(r"__NIFT_PROGRESS_EXIT__=([0-9]+)", data)
exit_code = marker.group(1) if marker else "unknown"
lines = [""]
cur = 0
i = 0
n = len(data)
while i < n:
    c = data[i]
    if c == "\r":
        cur = 0; i += 1
    elif c == "\n":
        lines.append(""); cur = 0; i += 1
    elif c == "\x1b":
        if data[i+1:i+2] == "[":
            m = data.find("m", i+2)
            k = data.find("K", i+2)
            if k != -1 and (m == -1 or k < m):
                params = data[i+2:k]
                if params == "2":
                    lines[-1] = ""; cur = 0
                elif params == "":
                    lines[-1] = lines[-1][:cur]
                i = k + 1
            else:
                i = (m + 1) if m != -1 else i + 2
        else:
            i += 2
    else:
        line = lines[-1]
        if cur < len(line):
            line = line[:cur] + c + line[cur+1:]
        else:
            line = line + c
        lines[-1] = line
        cur += 1
        i += 1
with open(out, "w") as f:
    for line in lines:
        if line.startswith("__NIFT_PROGRESS_EXIT__="):
            continue
        f.write(line + "\n")
    f.write("EXIT_CODE=" + exit_code + "\n")
EOF
}

run_pty() {
  pty_invoke "$@"
  pty_reconstruct "$1"
}

screen_line() {
  grep -v '^EXIT_CODE=' "$TMP/screen.txt"
}
exit_code() {
  sed -n 's/^EXIT_CODE=//p' "$TMP/screen.txt"
}

# Structural verification of the macOS/BSD `script` branch without a macOS
# runner: a fake `script` on PATH records the argv it receives, proving the
# Darwin invocation passes transcript first, then /bin/sh -c <command>, and that
# the supplied command carries the explicit exit-marker contract.
check_darwin_invocation() {
  local fake="$TMP/fakebin/script"
  mkdir -p "$TMP/fakebin"
  cat > "$fake" <<'FAKE'
#!/usr/bin/env bash
printf '%s\n' "$#" >> "$FAKE_ARGV"
printf '%s\n' "$@" >> "$FAKE_ARGV"
FAKE
  chmod +x "$fake"
  FAKE_ARGV="$TMP/darwin-argv.txt" PTY_PLATFORM=Darwin PATH="$TMP/fakebin:$PATH" \
    pty_invoke "$TMP/darwin.transcript" build --all
  local argc arg2 arg3 arg4 arg5
  argc="$(sed -n '1p' "$TMP/darwin-argv.txt")"
  arg2="$(sed -n '3p' "$TMP/darwin-argv.txt")"
  arg3="$(sed -n '4p' "$TMP/darwin-argv.txt")"
  arg4="$(sed -n '5p' "$TMP/darwin-argv.txt")"
  arg5="$(sed -n '6p' "$TMP/darwin-argv.txt")"
  test "$argc" = "5" || { echo "FAIL: Darwin script argv count (got $argc, want 5)"; exit 1; }
  test "$arg2" = "$TMP/darwin.transcript" \
    || { echo "FAIL: Darwin script transcript not first positional argument (got: $arg2)"; exit 1; }
  test "$arg3" = "/bin/sh" \
    || { echo "FAIL: Darwin script command not /bin/sh (got: $arg3)"; exit 1; }
  test "$arg4" = "-c" \
    || { echo "FAIL: Darwin script missing -c (got: $arg4)"; exit 1; }
  case "$arg5" in
    *__NIFT_PROGRESS_EXIT__=*) : ;;
    *) echo "FAIL: Darwin -c command lacks the exit-marker contract (got: $arg5)"; exit 1 ;;
  esac
  echo "Darwin script invocation structurally verified (script -q FILE /bin/sh -c CMD + exit marker)"
}

# Parser fixture: a BSD/macOS-style transcript has no util-linux COMMAND_EXIT_CODE
# footer. The explicit __NIFT_PROGRESS_EXIT__ marker must be the only source of
# status, must be parsed for both success and failure, and must never leak into
# the reconstructed user-facing screen.
check_marker_parsing() {
  local fixture
  fixture="$TMP/fixture-ok.transcript"
  printf 'Script started on 2026-01-01\r\noutput line\r\n__NIFT_PROGRESS_EXIT__=0\r\nScript done on 2026-01-01\r\n' > "$fixture"
  pty_reconstruct "$fixture"
  test "$(exit_code)" = "0" || { echo "FAIL: marker status 0 not parsed (got $(exit_code))"; exit 1; }
  screen_line | grep -q '__NIFT_PROGRESS_EXIT__' \
    && { echo "FAIL: exit marker leaked into reconstructed screen"; exit 1; }

  fixture="$TMP/fixture-fail.transcript"
  printf 'Script started on 2026-01-01\r\noutput line\r\n__NIFT_PROGRESS_EXIT__=1\r\nScript done on 2026-01-01\r\n' > "$fixture"
  pty_reconstruct "$fixture"
  test "$(exit_code)" = "1" || { echo "FAIL: marker status 1 not parsed (got $(exit_code))"; exit 1; }

  fixture="$TMP/fixture-none.transcript"
  printf 'Script started on 2026-01-01\r\noutput line\r\nScript done on 2026-01-01\r\n' > "$fixture"
  pty_reconstruct "$fixture"
  test "$(exit_code)" = "unknown" || { echo "FAIL: absent marker should yield unknown (got $(exit_code))"; exit 1; }

  echo "exit marker parsing verified (footer-free BSD-style transcripts)"
}

check_darwin_invocation
check_marker_parsing

# --- Success path -----------------------------------------------------------
run_pty "$TMP/ok.transcript" build
SUCCESS_SUMMARY="$(screen_line | grep -F 'built successfully' | tail -1)"
test -n "$SUCCESS_SUMMARY" || { echo "FAIL: no success summary in PTY screen"; exit 1; }
echo "$SUCCESS_SUMMARY" | grep -qE '[0-9]+ files (re)?built successfully$' \
  || { echo "FAIL: success summary does not describe built files: $SUCCESS_SUMMARY"; exit 1; }
echo "$SUCCESS_SUMMARY" | grep -q '^📦' \
  || { echo "FAIL: success summary has a stale prefix: $SUCCESS_SUMMARY"; exit 1; }
echo "$SUCCESS_SUMMARY" | grep -qE 'building|[·▓]' \
  && { echo "FAIL: success summary contains progress residue: $SUCCESS_SUMMARY"; exit 1; }
test "$(exit_code)" = "0" || { echo "FAIL: success build exited $(exit_code)"; exit 1; }

# --- Failure path -----------------------------------------------------------
cat > "$P/content/bad.html" <<'EOF'
---
title: Bad
template: templates/template.html
---
@pathto('../../escape')
EOF
python3 - "$P" <<'EOF'
import json, sys
root = sys.argv[1]
t = json.load(open(root + "/.nift/tracked.json"))
t["tracked"].append({"name": "bad", "title": "Bad", "template": "templates/template.html"})
json.dump(t, open(root + "/.nift/tracked.json", "w"))
EOF
run_pty "$TMP/bad.transcript" build --all
screen_line | grep -q 'error:' || { echo "FAIL: no error diagnostics on failure path"; exit 1; }
FAILURE_SUMMARY="$(screen_line | grep -F 'built successfully' | tail -1)"
test -n "$FAILURE_SUMMARY" || { echo "FAIL: no partial-success summary in PTY screen"; exit 1; }
echo "$FAILURE_SUMMARY" | grep -q 'building' \
  && { echo "FAIL: failure summary has a stale prefix: $FAILURE_SUMMARY"; exit 1; }
echo "$FAILURE_SUMMARY" | grep -q ' of ' || { echo "FAIL: unexpected failure summary: $FAILURE_SUMMARY"; exit 1; }
echo "$FAILURE_SUMMARY" | grep -q ' files built successfully$' \
  || { echo "FAIL: partial-success summary does not describe built files: $FAILURE_SUMMARY"; exit 1; }
test "$(exit_code)" = "1" || { echo "FAIL: failure build exited $(exit_code), expected 1"; exit 1; }

# --- NO_COLOR interactive behaviour -----------------------------------------
rm -f "$P/content/bad.html"
python3 - "$P" <<'EOF'
import json, sys
root = sys.argv[1]
t = json.load(open(root + "/.nift/tracked.json"))
t["tracked"] = [entry for entry in t["tracked"] if entry["name"] != "bad"]
json.dump(t, open(root + "/.nift/tracked.json", "w"))
EOF
rm -rf "$P/public" "$P/.nift/public"
rm -f "$P/.nift/.unfinished"
NO_COLOR=1 run_pty "$TMP/nocolor.transcript" build --all
NO_COLOR_RESULT="$(python3 - "$TMP/nocolor.transcript" <<'EOF'
import re, sys
data = open(sys.argv[1], "rb").read().decode("utf-8", "replace")
sgr = re.findall(r"\x1b\[[0-9;]*m", data)
lines = [""]; cur = 0; i = 0; n = len(data)
while i < n:
    c = data[i]
    if c == "\r": cur = 0; i += 1
    elif c == "\n": lines.append(""); cur = 0; i += 1
    elif c == "\x1b":
        if data[i+1:i+2] == "[":
            m = data.find("m", i+2); k = data.find("K", i+2)
            if k != -1 and (m == -1 or k < m):
                if data[i+2:k] == "2": lines[-1] = ""; cur = 0
                i = k + 1
            else: i = (m + 1) if m != -1 else i + 2
        else: i += 2
    else:
        line = lines[-1]
        if cur < len(line): line = line[:cur] + c + line[cur+1:]
        else: line = line + c
        lines[-1] = line; cur += 1; i += 1
summary = next((l for l in reversed(lines) if "built successfully" in l), "")
sys.stdout.write("SGR=%d\n%s" % (len(sgr), summary))
EOF
)"
NO_COLOR_SGR="$(echo "$NO_COLOR_RESULT" | sed -n 's/^SGR=//p')"
NO_COLOR_SUMMARY="$(echo "$NO_COLOR_RESULT" | sed -n '/^SGR=/d;p' | tail -1)"
test "$NO_COLOR_SGR" = "0" || { echo "FAIL: NO_COLOR interactive build emitted $NO_COLOR_SGR SGR colour sequences"; exit 1; }
echo "$NO_COLOR_SUMMARY" | grep -q '^📦' \
  || { echo "FAIL: NO_COLOR summary has a stale prefix: $NO_COLOR_SUMMARY"; exit 1; }

echo "progress PTY smoke passed (success, failure and NO_COLOR)"
