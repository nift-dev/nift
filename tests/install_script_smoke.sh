#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-install-test.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
version=9.8.7
release="$TMP/release/v$version"
case "$(uname -s)/$(uname -m)" in
  Linux/x86_64) platform="linux-x86_64" ;;
  Linux/amd64) platform="linux-x86_64" ;;
  Darwin/arm64) platform="macos-arm64" ;;
  Darwin/aarch64) platform="macos-arm64" ;;
  Darwin/x86_64) platform="macos-x86_64" ;;
  Darwin/amd64) platform="macos-x86_64" ;;
  *) echo "installer smoke: unsupported fixture platform $(uname -s)/$(uname -m)" >&2; exit 2 ;;
esac
root="nift-$version-$platform"
mkdir -p "$release/$root" "$TMP/bin"
cat > "$release/$root/nift" <<EOF2
#!/bin/sh
echo 'Nift v$version'
EOF2
chmod +x "$release/$root/nift"
( cd "$release" && tar -czf "$root.tar.gz" "$root"
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$root.tar.gz" > SHA256SUMS
  else shasum -a 256 "$root.tar.gz" > SHA256SUMS; fi )
NIFT_VERSION="$version" NIFT_RELEASE_BASE="file://$release" NIFT_INSTALL_DIR="$TMP/bin" "$ROOT/packaging/install.sh" >/dev/null
[ "$("$TMP/bin/nift")" = "Nift v$version" ]
# A mismatched checksum must fail without replacing an existing install.
printf '#!/bin/sh\necho preserved\n' > "$TMP/bin/nift"; chmod +x "$TMP/bin/nift"
printf '%064d  %s\n' 0 "$root.tar.gz" > "$release/SHA256SUMS"
if NIFT_VERSION="$version" NIFT_RELEASE_BASE="file://$release" NIFT_INSTALL_DIR="$TMP/bin" "$ROOT/packaging/install.sh" >/dev/null 2>&1; then
  echo 'installer accepted bad checksum' >&2; exit 1
fi
[ "$("$TMP/bin/nift")" = preserved ]

# macOS default installs must make ~/.local/bin usable without sudo or a manual move.
mac_home="$TMP/mac-home"
mac_release="$TMP/mac-release/v$version"
mac_root="nift-$version-macos-arm64"
mkdir -p "$mac_home" "$mac_release/$mac_root" "$TMP/mockbin"
cat > "$mac_release/$mac_root/nift" <<EOF2
#!/bin/sh
echo 'Nift v$version'
EOF2
chmod +x "$mac_release/$mac_root/nift"
( cd "$mac_release" && tar -czf "$mac_root.tar.gz" "$mac_root"
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$mac_root.tar.gz" > SHA256SUMS
  else shasum -a 256 "$mac_root.tar.gz" > SHA256SUMS; fi )
cat > "$TMP/mockbin/uname" <<'EOF2'
#!/bin/sh
case "${1:-}" in
  -s|'') echo Darwin ;;
  -m) echo arm64 ;;
  *) /usr/bin/uname "$@" ;;
esac
EOF2
chmod +x "$TMP/mockbin/uname"
mac_path="$TMP/mockbin:/usr/bin:/bin"
HOME="$mac_home" SHELL="/bin/zsh" PATH="$mac_path" NIFT_VERSION="$version" NIFT_RELEASE_BASE="file://$mac_release" "$ROOT/packaging/install.sh" >/dev/null
[ "$("$mac_home/.local/bin/nift")" = "Nift v$version" ]
grep -Fqx 'export PATH="$HOME/.local/bin:$PATH"' "$mac_home/.zprofile"
HOME="$mac_home" SHELL="/bin/zsh" PATH="$mac_path" NIFT_VERSION="$version" NIFT_RELEASE_BASE="file://$mac_release" "$ROOT/packaging/install.sh" >/dev/null
[ "$(grep -Fc 'export PATH="$HOME/.local/bin:$PATH"' "$mac_home/.zprofile")" -eq 1 ]

echo 'Installer smoke test passed'
