#!/usr/bin/env bash
# Node HTTP dogfood acceptance smoke test.
set -euo pipefail
cd "$(dirname "$0")"
NODE_DIR="$(cd .. && pwd)"

PORT="${NIFT_DOGFOOD_PORT:-5173}"
BASE="http://127.0.0.1:$PORT"
LOG="$(mktemp "${TMPDIR:-/tmp}/nift-node-dogfood.XXXXXX.log")"
trap 'kill -- -"$APP_PID" 2>/dev/null || true; rm -f "$LOG"' EXIT

echo "building node binding"
bash "$NODE_DIR/build.sh" >/dev/null

echo "starting node dogfood on :$PORT"
PORT="$PORT" setsid node "$NODE_DIR/app/server.js" >"$LOG" 2>&1 &
APP_PID=$!

for _ in $(seq 1 40); do
  if curl -sf "$BASE/" >/dev/null 2>&1; then break; fi
  sleep 0.5
done

fail() { echo "DOGFOOD FAIL: $*" >&2; cat "$LOG" >&2; exit 1; }

body="$(curl -sf "$BASE/")" || fail "GET / returned non-200"
printf '%s' "$body" | grep -Fq "Nift Node Dogfood" || fail "/ missing engine default title"
printf '%s' "$body" | grep -Fq "Hello world." || fail "/ missing context binding value"
printf '%s' "$body" | grep -Fq "1.0" || fail "/ missing environment callback value"

posts="$(curl -sf "$BASE/posts")" || fail "GET /posts returned non-200"
printf '%s' "$posts" | grep -Fq '"ok":true' || fail "/posts not ok"
printf '%s' "$posts" | grep -Fq '"page":2' || fail "/posts missing pagination page 2"

partial="$(curl -sf "$BASE/partial")" || fail "GET /partial returned non-200"
printf '%s' "$partial" | grep -Fq "from loader" || fail "/partial missing loader content"

concurrency="$(curl -sf "$BASE/concurrency")" || fail "GET /concurrency returned non-200"
printf '%s' "$concurrency" | grep -Fq '"rendered":32,"failures":0' || fail "/concurrency evidence missing: $concurrency"

pids=()
for _ in $(seq 1 24); do curl -sf "$BASE/" >/dev/null & pids+=("$!"); done
for p in "${pids[@]}"; do wait "$p" || fail "concurrent / requests failed"; done
echo "  concurrent / requests: 24/24 ok"

code="$(curl -s -o /dev/null -w '%{http_code}' "$BASE/error")"
[ "$code" = "500" ] || fail "/error expected 500, got $code"
curl -s "$BASE/error" | grep -Fq "forced host error" || fail "/error missing host diagnostic"

code="$(curl -s -o /dev/null -w '%{http_code}' "$BASE/malformed")"
[ "$code" = "500" ] || fail "/malformed expected 500, got $code"
curl -s "$BASE/malformed" | grep -Fq "json: failed to parse content/bad.json (" || fail "/malformed missing json parse family"

echo "DOGFOOD PASS: endpoints, concurrency, error paths, loader seam verified"
