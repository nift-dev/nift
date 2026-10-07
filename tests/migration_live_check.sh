#!/usr/bin/env bash
# Integration check (network): the generated MIGRATION.md must match the live
# canonical copy served at https://nift.dev/MIGRATION.md byte for byte.
# Gated behind NIFT_LIVE_TESTS=1. Run after updating the canonical migration
# fixture or the nift.dev copy.
set -u
[ "${NIFT_LIVE_TESTS:-0}" = "1" ] || { echo "migration live check skipped (NIFT_LIVE_TESTS != 1)"; exit 0; }
FIXTURE="$(pwd)/tests/fixtures/MIGRATION.md"
URL="${NIFT_MIGRATION_URL:-https://nift.dev/MIGRATION.md}"

fail() { echo "migration live check FAIL: $*" >&2; exit 1; }

LIVE="$(mktemp "${TMPDIR:-/tmp}/nift-migration-live.XXXXXX")"
trap 'rm -f "$LIVE"' EXIT
curl -fsSL --max-time 30 "$URL" -o "$LIVE" || fail "could not download $URL"
LIVE_SHA="$(sha256sum "$LIVE" | cut -d' ' -f1)"
FIXTURE_SHA="$(sha256sum "$FIXTURE" | cut -d' ' -f1)"
[ "$LIVE_SHA" = "$FIXTURE_SHA" ] || fail "live copy differs from vendored fixture ($LIVE_SHA vs $FIXTURE_SHA)"
echo "migration live check passed: $URL matches the vendored canonical copy"