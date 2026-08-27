#!/usr/bin/env bash
# Generate dist/embed-prefix/lib/pkgconfig/nift.pc for the CURRENT OS (dev
# convenience for the repository's Go/C builds). Release .pc files are produced
# per target by packaging/stage-release.sh / packaging/matrix-build.sh.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
case "$(uname -s)" in
  Darwin)   PC_LIBS='-L${libdir} -Wl,-force_load,${libdir}/libnift_c.a -lc++ -lm' ;;
  MINGW*|MSYS*|CYGWIN*) PC_LIBS='-L${libdir} -lnift_c' ;;
  *)        PC_LIBS='-L${libdir} -Wl,-Bstatic -lnift_c -Wl,-Bdynamic -lstdc++ -lm -pthread' ;;
esac
sed -e "s/__VERSION__/0.0.0-dev/" -e "s|__LIBS__|$PC_LIBS|" packaging/nift.pc.in \
  > dist/embed-prefix/lib/pkgconfig/nift.pc
