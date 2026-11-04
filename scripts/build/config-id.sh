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
config_text() {
  local cc=$1; shift
  local path; path=$(command -v "$cc" 2>/dev/null || echo "$cc")
  path=$(readlink -f "$path" 2>/dev/null || echo "$path")
  printf 'nift-object-config-v3\ncompiler %s\nresolved %s\nversion-sha256 %s\n' "$cc" "$path" \
    "$("$cc" --version 2>&1 | sha256sum | cut -d' ' -f1)"
  local skip=0 a
  for a in "$@"; do
    if [ "$skip" = 1 ]; then skip=0; continue; fi
    if [ "$a" = -c ]; then skip=1; continue; fi
    printf 'arg %s\n' "$a"
  done
}
config_id() { config_text "$@" | sha256sum | cut -d' ' -f1; }
