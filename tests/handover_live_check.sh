#!/usr/bin/env bash
# Integration check (network): the generated handover must match the live
# canonical copy served at https://nift.dev/HANDOVER.md byte for byte.
# Gated behind NIFT_LIVE_TESTS=1 so the deterministic CI chain never depends on
# network availability. Run it after updating the canonical handover.
set -u
[ "${NIFT_LIVE_TESTS:-0}" = "1" ] || { echo "handover live check skipped (NIFT_LIVE_TESTS != 1)"; exit 0; }
FIXTURE="$(pwd)/tests/fixtures/HANDOVER.md"
URL="${NIFT_HANDOVER_URL:-https://nift.dev/HANDOVER.md}"

fail() { echo "handover live check FAIL: $*" >&2; exit 1; }

LIVE="$(mktemp "${TMPDIR:-/tmp}/nift-handover-live.XXXXXX")"
trap 'rm -f "$LIVE"' EXIT
curl -fsSL --max-time 30 "$URL" -o "$LIVE" || fail "could not download $URL"
LIVE_SHA="$(sha256sum "$LIVE" | cut -d' ' -f1)"
FIXTURE_SHA="$(sha256sum "$FIXTURE" | cut -d' ' -f1)"
[ "$LIVE_SHA" = "$FIXTURE_SHA" ] || fail "live copy differs from vendored fixture ($LIVE_SHA vs $FIXTURE_SHA)"
echo "handover live check passed: $URL matches the vendored canonical copy"
