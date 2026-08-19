#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-install-test.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
version=9.8.7
release="$TMP/release/v$version"
root="nift-$version-linux-x86_64"
mkdir -p "$release/$root" "$TMP/bin"
cat > "$release/$root/nift" <<EOF2
#!/bin/sh
echo 'Nift v$version'
EOF2
chmod +x "$release/$root/nift"
( cd "$release" && tar -czf "$root.tar.gz" "$root" && sha256sum "$root.tar.gz" > SHA256SUMS )
NIFT_VERSION="$version" NIFT_RELEASE_BASE="file://$release" NIFT_INSTALL_DIR="$TMP/bin" "$ROOT/packaging/install.sh" >/dev/null
[ "$("$TMP/bin/nift")" = "Nift v$version" ]
# A mismatched checksum must fail without replacing an existing install.
printf '#!/bin/sh\necho preserved\n' > "$TMP/bin/nift"; chmod +x "$TMP/bin/nift"
printf '%064d  %s\n' 0 "$root.tar.gz" > "$release/SHA256SUMS"
if NIFT_VERSION="$version" NIFT_RELEASE_BASE="file://$release" NIFT_INSTALL_DIR="$TMP/bin" "$ROOT/packaging/install.sh" >/dev/null 2>&1; then
  echo 'installer accepted bad checksum' >&2; exit 1
fi
[ "$("$TMP/bin/nift")" = preserved ]
echo 'Installer smoke test passed'
