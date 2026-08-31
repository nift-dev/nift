#!/usr/bin/env bash
# Install and assert the immutable pinned Snapcraft snap revision.
#
# Requires SNAPCRAFT_SNAP_REVISION and SNAPCRAFT_EXPECTED_VERSION in the
# environment. Installs the exact Store revision via `snap install --revision`,
# then asserts both the parsed version and the installed revision. It is shared
# by the manual non-publishing toolchain preflight and the tag release
# coordinator so a bad pin is discovered before any Nift publication.
set -euo pipefail

REVISION="${SNAPCRAFT_SNAP_REVISION:?SNAPCRAFT_SNAP_REVISION is required}"
EXPECTED="${SNAPCRAFT_EXPECTED_VERSION:?SNAPCRAFT_EXPECTED_VERSION is required}"

sudo snap install snapcraft --classic --revision="$REVISION"

# `snapcraft version` commonly prints "snapcraft 9.0.1"; extract the version
# token and compare that, never the raw whole-output string.
installed_version="$(snapcraft version | grep -oE '[0-9]+(\.[0-9]+){1,2}' | head -1)"
test -n "$installed_version" || { echo "could not parse snapcraft version" >&2; exit 1; }
installed_revision="$(snap list snapcraft | sed -n '2p' | awk '{print $3}')"
[ "$installed_version" = "$EXPECTED" ] || { echo "snapcraft version ${installed_version} != ${EXPECTED}" >&2; exit 1; }
[ "$installed_revision" = "$REVISION" ] || { echo "snapcraft revision ${installed_revision} != ${REVISION}" >&2; exit 1; }
echo "snapcraft ${installed_version} revision ${installed_revision} verified"