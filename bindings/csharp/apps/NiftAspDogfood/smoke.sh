#!/usr/bin/env bash
# ASP.NET Core dogfood acceptance smoke test.
#
# Starts the dogfood app, exercises every endpoint (including concurrency and
# both error paths), and asserts the evidence markers. Nift remains the
# renderer; ASP.NET remains the web framework.
set -euo pipefail
cd "$(dirname "$0")"

PORT="${NIFT_DOGFOOD_PORT:-5123}"
BASE="http://127.0.0.1:$PORT"
LOG="$(mktemp "${TMPDIR:-/tmp}/nift-dogfood.XXXXXX.log")"
# The app runs in its own process group so shutdown is deterministic: the trap
# kills the whole group (dotnet run + the spawned app child).
trap 'kill -- -"$APP_PID" 2>/dev/null || true; rm -f "$LOG"' EXIT

echo "building dogfood app"
dotnet build -v q --nologo >/dev/null

echo "starting dogfood app on :$PORT"
ASPNETCORE_URLS="http://127.0.0.1:$PORT" setsid dotnet run --no-build >"$LOG" 2>&1 &
APP_PID=$!

for _ in $(seq 1 60); do
    if curl -sf "$BASE/" >/dev/null 2>&1; then break; fi
    sleep 0.5
done

fail() { echo "DOGFOOD FAIL: $*" >&2; cat "$LOG" >&2; exit 1; }

# Root: Engine default (site) + Context binding (who) + env callback (version).
body="$(curl -sf "$BASE/")" || fail "GET / returned non-200"
printf '%s' "$body" | grep -Fq "Nift AspNetDogfood" || fail "/ missing engine default site title"
printf '%s' "$body" | grep -Fq "Hello world." || fail "/ missing context binding value"
printf '%s' "$body" | grep -Fq "1.0" || fail "/ missing environment callback value"

# Pagination page render.
posts="$(curl -sf "$BASE/posts")" || fail "GET /posts returned non-200"
printf '%s' "$posts" | grep -Fq '"ok":true' || fail "/posts not ok"
printf '%s' "$posts" | grep -Fq '"page":2' || fail "/posts missing pagination page 2"

# Loader seam render.
partial="$(curl -sf "$BASE/partial")" || fail "GET /partial returned non-200"
printf '%s' "$partial" | grep -Fq "from loader" || fail "/partial missing loader-served content"

# Concurrency endpoint: 32 parallel renders inside the request.
concurrency="$(curl -sf "$BASE/concurrency")" || fail "GET /concurrency returned non-200"
printf '%s' "$concurrency" | grep -Fq '"rendered":32,"failures":0' || fail "/concurrency evidence missing: $concurrency"

# External concurrency: 24 parallel requests must all succeed.
pids=()
for _ in $(seq 1 24); do curl -sf "$BASE/" >/dev/null & pids+=("$!"); done
for p in "${pids[@]}"; do wait "$p" || fail "concurrent / requests failed"; done
echo "  concurrent / requests: 24/24 ok"

# Error(diagnostic) path: host callback failure -> 500 with the verbatim diagnostic.
code="$(curl -s -o /dev/null -w '%{http_code}' "$BASE/error")"
[ "$code" = "500" ] || fail "/error expected 500, got $code"
curl -s "$BASE/error" | grep -Fq "forced host error" || fail "/error missing host diagnostic"

# Malformed JSON failure family: controlled 500 with the frozen prefix.
code="$(curl -s -o /dev/null -w '%{http_code}' "$BASE/malformed")"
[ "$code" = "500" ] || fail "/malformed expected 500, got $code"
curl -s "$BASE/malformed" | grep -Fq "json: failed to parse content/bad.json (" || fail "/malformed missing json parse failure family"

echo "DOGFOOD PASS: endpoints, concurrency, error paths, loader seam verified"
