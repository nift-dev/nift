#!/usr/bin/env bash
# POSIX end-to-end coverage for the build-progress renderer. Runs the real Nift
# binary under a pseudo-terminal (via `script`) and reconstructs the terminal
# screen to assert that final messages are never written into an active progress
# line: no stale "building ..." prefix survives before the summary, on either the
# success or the failure path, and NO_COLOR keeps SGR colour sequences out of an
# interactive session. Skipped gracefully when `script` or `python3` is absent.
set -euo pipefail
NIFT_BIN="${NIFT_BIN:?NIFT_BIN must point to the nift binary}"

if ! command -v script >/dev/null 2>&1; then
  echo "SKIP: 'script' (util-linux) is unavailable"
  exit 0
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "SKIP: 'python3' is unavailable"
  exit 0
fi

case "$(uname -s)" in
  Darwin) SCRIPT_OPTS="-q" ;;
  *)      SCRIPT_OPTS="-qec" ;;
esac

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

# Run the build under a PTY and reconstruct the screen. $1 = transcript file,
# $2... = build arguments. Writes the reconstructed lines and the exit code to
# a second file.
run_pty() {
  local transcript="$1"; shift
  script $SCRIPT_OPTS "cd $P && $NIFT_BIN $*" "$transcript" >"$TMP/pty.out" 2>&1 || true
  python3 - "$transcript" "$TMP/screen.txt" <<'EOF'
import re, sys
transcript, out = sys.argv[1], sys.argv[2]
data = open(transcript, "rb").read().decode("utf-8", "replace")
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
exit_code = "unknown"
footer = re.search(r"COMMAND_EXIT_CODE=\"([^\"]*)\"", data)
if footer:
    exit_code = footer.group(1)
with open(out, "w") as f:
    for line in lines:
        f.write(line + "\n")
    f.write("EXIT_CODE=" + exit_code + "\n")
EOF
}

screen_line() {
  grep -v '^EXIT_CODE=' "$TMP/screen.txt"
}
exit_code() {
  sed -n 's/^EXIT_CODE=//p' "$TMP/screen.txt"
}

# --- Success path -----------------------------------------------------------
run_pty "$TMP/ok.transcript" build
SUCCESS_SUMMARY="$(screen_line | grep -F 'built successfully' | tail -1)"
test -n "$SUCCESS_SUMMARY" || { echo "FAIL: no success summary in PTY screen"; exit 1; }
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
NO_COLOR=1 script $SCRIPT_OPTS "cd $P && $NIFT_BIN build --all" "$TMP/nocolor.transcript" >"$TMP/nocolor.out" 2>&1 || true
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