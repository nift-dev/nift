# Resolve the launcher before changing into the fixture's scratch directory.
case $(uname -s) in
  Darwin) darwin_address_limit="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)/.build/darwin-address-limit" ;;
  *) darwin_address_limit= ;;
esac

test_run_bounded() (
  file_blocks="$1"
  shift
  ulimit -f "$file_blocks" || exit 2
  if [ -n "$darwin_address_limit" ]; then
    exec timeout 60 "$darwin_address_limit" 2147483648 -- "$@"
  fi
  ulimit -v 2097152 || exit 2
  exec timeout 60 "$@"
)
