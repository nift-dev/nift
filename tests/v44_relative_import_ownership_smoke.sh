#!/usr/bin/env bash
# Relative imports are owned by their source module, not by the consumer cwd.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT

# Direct/legacy parity, nested paths, Windows separators, and no root fallback.
mkdir -p "$t/direct/modules/nested"
cat > "$t/direct/helper.f" <<'F'
@fn(value()) { return "decoy" }
export(value)
F
cat > "$t/direct/modules/helper.f" <<'F'
@fn(value()) { return "owned" }
export(value)
F
cat > "$t/direct/modules/win.f" <<'F'
@fn(win_value()) { return "win" }
export(win_value)
F
cat > "$t/direct/modules/nested/one.f" <<'F'
@import("../two.f")
export(two)
F
cat > "$t/direct/modules/two.f" <<'F'
@fn(two()) { return "nested" }
export(two)
F
cat > "$t/direct/modules/main.f" <<'F'
@import("./helper.f")
import(".\\win.f")
import("./nested/one.f")
print(value())
print(win_value())
print(two())
F
out=$(cd "$t/direct" && "$NIFT_ABS" modules/main.f)
[ "$out" = "owned
win
nested" ] || { printf 'direct relative import output:\n%s\n' "$out" >&2; exit 1; }
cat > "$t/direct/modules/missing-main.f" <<'F'
import("./missing.f")
F
cp "$t/direct/helper.f" "$t/direct/missing.f"
if (cd "$t/direct" && "$NIFT_ABS" modules/missing-main.f >"$t/missing.out" 2>"$t/missing.err"); then
  echo 'missing sibling fell through to consumer-root decoy' >&2
  exit 1
fi
grep -F 'modules/missing.f' "$t/missing.err" >/dev/null

# Canonical identity catches a source imported again through a symlink alias.
if ln -s main-cycle.f "$t/direct/modules/main-cycle-link.f" 2>/dev/null; then
  cat > "$t/direct/modules/main-cycle.f" <<'F'
import("./main-cycle-link.f")
F
  if (cd "$t/direct" && "$NIFT_ABS" modules/main-cycle.f >/dev/null 2>"$t/cycle.err"); then
    echo 'symlink self-import cycle succeeded' >&2
    exit 1
  fi
  grep -F 'script import cycle through' "$t/cycle.err" >/dev/null
else
  echo '  (symlink cycle skipped: native symlinks unavailable)'
fi

# Package functions, methods, lambdas, callback values, and child re-exports
# retain their defining module even when consumer names collide.
site="$t/site"
pkg="$site/.nift/packages/owner"
mkdir -p "$pkg/src/sub" "$site/.nift/packages/bare/src"
printf '{"name":"owner","version":"0.1.0","entry":"src/main.f"}\n' > "$pkg/manifest.json"
printf '{"name":"bare","version":"0.1.0","entry":"src/main.f"}\n' > "$site/.nift/packages/bare/manifest.json"
cat > "$site/.nift/packages/bare/src/main.f" <<'F'
@fn(bare_value()) { return "bare-package" }
export(bare_value)
F
cat > "$pkg/src/dynamic.f" <<'F'
@fn(dynamic_value()) { return "function-owned" }
export(dynamic_value)
F
cat > "$pkg/src/method-helper.f" <<'F'
@fn(method_value()) { return "method-owned" }
export(method_value)
F
cat > "$pkg/src/lambda-helper.f" <<'F'
@fn(lambda_value()) { return "lambda-owned" }
export(lambda_value)
F
cat > "$pkg/src/runtime-child.f" <<'F'
@fn(runtime_child()) { import("./runtime-grand.f"); return runtime_grand() }
export(runtime_child)
F
cat > "$pkg/src/runtime-grand.f" <<'F'
@fn(runtime_grand()) { return "runtime-transitive-owned" }
export(runtime_grand)
F
cat > "$pkg/src/sub/child.f" <<'F'
@fn(child_value()) { import("./grand.f"); return grand_value() }
export(child_value)
F
cat > "$pkg/src/sub/grand.f" <<'F'
@fn(grand_value()) { return "transitive-owned" }
export(grand_value)
F
cat > "$pkg/src/grand.f" <<'F'
@fn(grand_value()) { return "package-root-decoy" }
export(grand_value)
F
cat > "$pkg/src/main.f" <<'F'
@fn(private_callback(x)) { return "package-" + x }
callback := private_callback
@fn(load_function()) { import("./dynamic.f"); return dynamic_value() }
@fn(load_runtime_child()) { import("./runtime-child.f"); return runtime_child() }
@fn(traversal_escape()) { import("../../../../package-decoy.f"); return package_decoy() }
@fn(symlink_escape()) { import("./outside-link.f"); return outside_value() }
@struct(loader) { fn(load()) { import("./method-helper.f"); return method_value() } }
make_loader := loader()
load_lambda := () => { import("./lambda-helper.f"); return lambda_value() }
import("./sub/child.f")
import("bare")
export(callback)
export(load_function)
export(load_runtime_child)
export(traversal_escape)
export(symlink_escape)
export(loader)
export(make_loader)
export(load_lambda)
export(child_value)
export(bare_value)
F
cat > "$site/main.f" <<'F'
@fn(private_callback(x)) { return "consumer-" + x }
@fn(dynamic_value()) { return "consumer-dynamic" }
@fn(grand_value()) { return "consumer-grand" }
import("owner")
print(load_function())
print(load_runtime_child())
print(make_loader.load())
print(load_lambda())
print(["callback"].map(callback)[0])
print(child_value())
print(bare_value())
F
out=$(cd "$site" && "$NIFT_ABS" main.f)
[ "$out" = "function-owned
runtime-transitive-owned
method-owned
lambda-owned
package-callback
transitive-owned
bare-package" ] || { printf 'package ownership output:\n%s\n' "$out" >&2; exit 1; }
cat > "$site/leak.f" <<'F'
import("owner")
load_function()
print(dynamic_value())
F
if (cd "$site" && "$NIFT_ABS" leak.f >/dev/null 2>&1); then
  echo 'child module export leaked into consumer namespace' >&2
  exit 1
fi
cat > "$site/package-decoy.f" <<'F'
@fn(package_decoy()) { return "consumer-decoy" }
export(package_decoy)
F
cat > "$site/traversal.f" <<'F'
import("owner")
print(traversal_escape())
F
if (cd "$site" && "$NIFT_ABS" traversal.f >/dev/null 2>"$t/traversal.err"); then
  echo 'package-relative traversal reached consumer decoy' >&2
  exit 1
fi
grep -F 'package-relative import escapes package root' "$t/traversal.err" >/dev/null
cat > "$t/outside-package.f" <<'F'
@fn(outside_value()) { return "outside" }
export(outside_value)
F
if ln -s "$t/outside-package.f" "$pkg/src/outside-link.f" 2>/dev/null; then
  cat > "$site/symlink-escape.f" <<'F'
import("owner")
print(symlink_escape())
F
  if (cd "$site" && "$NIFT_ABS" symlink-escape.f >/dev/null 2>"$t/symlink-escape.err"); then
    echo 'package-relative symlink escaped package root' >&2
    exit 1
  fi
  grep -F 'package-relative import escapes package root' "$t/symlink-escape.err" >/dev/null
else
  echo '  (package symlink escape skipped: native symlinks unavailable)'
fi

# Template/build mode resolves from the template containing the import.
mkdir -p "$t/template/.nift" "$t/template/content" "$t/template/templates/modules" "$t/template/public"
cat > "$t/template/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/modules/page.html","build-threads":1,"incremental-mode":"modified"}}
JSON
cat > "$t/template/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"/","title":"relative owner","template":"templates/modules/page.html"}]}
JSON
printf 'body\n' > "$t/template/content/index.html"
cat > "$t/template/templates/helper.f" <<'F'
@fn(template_value()) { return "decoy" }
export(template_value)
F
cat > "$t/template/templates/modules/helper.f" <<'F'
@fn(template_value()) { return "template-owned" }
export(template_value)
F
cat > "$t/template/templates/modules/page.html" <<'F'
@import("./helper.f")$[template_value()] @content
F
(cd "$t/template" && "$NIFT_ABS" build --all >/dev/null)
grep -F 'template-owned' "$t/template/public/index.html" >/dev/null
if grep -F 'decoy' "$t/template/public/index.html" >/dev/null; then
  echo 'template import selected project-root decoy' >&2
  exit 1
fi

echo 'PASS v4.4 relative import module ownership'
