#!/bin/sh
set -eu

website="${MINIFY_WEBSITE_BASE:-https://nift-dev.github.io/minify-website}"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/minify-update.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

command -v curl >/dev/null 2>&1 || { echo "minify update: required command not found: curl" >&2; exit 1; }
curl --retry 5 --retry-all-errors --retry-delay 2 -fsSL "$website/install" -o "$tmp/install.sh"
sh -n "$tmp/install.sh"
sh "$tmp/install.sh"
