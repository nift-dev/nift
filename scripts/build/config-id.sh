# Tooling checkpoint (component 1): the configuration identity of a compile
# command, shared by compile-object.sh, write-signature.sh and
# check-config.sh (sourced, not run).
#   config_text COMPILER [ARGS...]   the identity, as text
#   config_id COMPILER [ARGS...]     its sha256 (64 hex digits)
# The identity is the compiler's resolved path, the sha256 of its complete
# `--version` output (a different compiler, or another version of the same
# one, is another configuration) and every argument except the `-c SOURCE`
# pair (the source is not part of the configuration). v3 (tooling review F7):
# the sidecar format gained the object's sha256, so every object is rebuilt once.
# v4 (Linode certification of 4e97e34e, item 4): the location of the build
# output root is not configuration. NIFT_TEST_DIR (the Makefile's TEST_DIR,
# exported) is written as $TEST_DIR in every argument that names it (for
# example -I.build/libffi/.../include), so `make TEST_DIR=/tmp/x TARGET` (the
# mutant provenance rebuild) computes the same identity for the in-tree objects
# and reuses them instead of rebuilding them into the source tree with another
# configuration. A compiler's version hash is computed once per shell.
# B18: pipefail in every shell that sources this file (compile-object.sh, write-signature.sh,
# check-config.sh); none of their pipelines' statuses decided anything before.
set -o pipefail
declare -A NIFT_VERSION_SHA 2>/dev/null || true
config_text() {
  local cc=$1; shift
  local path; path=$(command -v "$cc" 2>/dev/null || echo "$cc")
  path=$(readlink -f "$path" 2>/dev/null || echo "$path")
  local vsha=${NIFT_VERSION_SHA[$path]:-}
  if [ -z "$vsha" ]; then
    vsha=$("$cc" --version 2>&1 | sha256sum | cut -d' ' -f1)
    NIFT_VERSION_SHA[$path]=$vsha
  fi
  printf 'nift-object-config-v4\ncompiler %s\nresolved %s\nversion-sha256 %s\n' "$cc" "$path" "$vsha"
  local skip=0 a d=${NIFT_TEST_DIR:-}
  d=${d%/}
  for a in "$@"; do
    if [ "$skip" = 1 ]; then skip=0; continue; fi
    if [ "$a" = -c ]; then skip=1; continue; fi
    if [ -n "$d" ]; then
      case "$a" in
        "$d") a='$TEST_DIR' ;;
        "$d"/*) a="\$TEST_DIR/${a#"$d"/}" ;;
        -I"$d"/*) a="-I\$TEST_DIR/${a#-I"$d"/}" ;;
      esac
    fi
    printf 'arg %s\n' "$a"
  done
}
config_id() { config_text "$@" | sha256sum | cut -d' ' -f1; }
