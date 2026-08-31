#!/usr/bin/env bash
# Candidate confinement smoke for the nift snap, run on a clean Ubuntu amd64
# host between candidate verification and explicit per-revision stable release.
#
# Installs the exact amd64 revision that was selected for candidate (the same
# revision number verified in the candidate channel map), then verifies the
# installed version and revision, exercises the strict-confinement surface
# (version/help, project creation, a real build, dependency-driven rebuilding,
# filesystem access under the home plug, and project-local .nift/ state), then
# removes the snap. Fails closed on any error so stable release never
# proceeds from an unvalidated candidate.
set -euo pipefail
VERSION="${1:?usage: snap-candidate-smoke.sh <version> <amd64-revision>}"
REVISION="${2:?}"

sudo snap remove nift >/dev/null 2>&1 || true
# Install the exact selected revision by number; "exact revision" is literal.
sudo snap install nift --revision="$REVISION"

line="$(snap list nift | sed -n '2p')"
installed_version="$(printf '%s\n' "$line" | awk '{print $2}')"
installed_revision="$(printf '%s\n' "$line" | awk '{print $3}')"
echo "installed nift ${installed_version} revision ${installed_revision} (exact selected revision)"
[ "$installed_version" = "$VERSION" ] || { echo "FAIL: installed version ${installed_version} != ${VERSION}" >&2; exit 1; }
[ "$installed_revision" = "$REVISION" ] || { echo "FAIL: installed revision ${installed_revision} != ${REVISION}" >&2; exit 1; }

# version and command/help output
[ "$(nift version)" = "Nift v${VERSION}" ] || { echo "FAIL: 'nift version' did not report ${VERSION}" >&2; exit 1; }
nift commands >/dev/null || { echo "FAIL: 'nift commands' failed" >&2; exit 1; }

# Disposable project under $HOME: strict confinement grants the home plug, so a
# project outside $HOME would not be representative of supported filesystem use.
proj="$(mktemp -d "$HOME/nift-candidate-smoke.XXXXXX")"
trap 'rm -rf "$proj"; sudo snap remove nift >/dev/null 2>&1 || true' EXIT
cd "$proj"

nift init
[ -d .nift ] || { echo "FAIL: project-local .nift/ missing after init" >&2; exit 1; }
[ -f .nift/config.json ] || { echo "FAIL: .nift/config.json missing" >&2; exit 1; }
[ -f .nift/tracked.json ] || { echo "FAIL: .nift/tracked.json missing" >&2; exit 1; }

mkdir -p templates content
cat > templates/template.html <<'HTML'
<!doctype html><title>$[page.title]</title><body>@content</body>
HTML
cat > content/index.html <<'HTML'
---
title: Home
template: templates/template.html
---
<p>hello candidate</p>
HTML

nift build --all
[ -f public/index.html ] || { echo "FAIL: no output after build" >&2; exit 1; }
grep -q "hello candidate" public/index.html || { echo "FAIL: built output content is wrong" >&2; exit 1; }

# Dependency-driven rebuild: touching the template must cause a rebuild.
echo "  <footer>v2</footer>" >> templates/template.html
nift build
grep -q "v2" public/index.html || { echo "FAIL: template change did not drive a rebuild" >&2; exit 1; }

echo "candidate smoke PASS: nift ${VERSION} revision ${REVISION}"