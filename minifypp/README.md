# Minify++

**Executable:** `minify`

A small conservative C++17 multi-format minifier library and CLI.

It can be used standalone and is also embedded by Nift as an independent subproject.

The architectural boundary is intentional: Nift consumes only the public API in `include/minify/Minify.h`. The minifier does not depend on Nift's project model, template parser, tracking state, CLI, or build engine. Its private JSON parser is carried inside this directory so the subtree can later be extracted into a standalone repository without importing Nift internals.

Supported formats in format version 1 are HTML, CSS, JavaScript, JSX, JSON, XML and SVG.

## Install

Linux x86-64 and macOS arm64/x86-64:

```bash
curl -fsSL https://minify.cx/install.sh | sh
```

The installer downloads the latest GitHub release, verifies its archive against
the published `SHA256SUMS`, and installs `minify` in `~/.local/bin` without
`sudo`. Set `MINIFY_INSTALL_DIR` to choose another directory or
`MINIFY_VERSION=1.1.1` to pin a release.

You can inspect the script before running it, and the release archive is
checksum-verified against `SHA256SUMS` before anything is installed.

```bash
# Download the verified archive without installing it.
curl -fsSL https://minify.cx/download.sh | sh

# Update to the latest release using the same install location.
curl -fsSL https://minify.cx/update.sh | sh

# Remove the installed executable.
curl -fsSL https://minify.cx/uninstall.sh | sh
```

Windows x86-64 users can download the `.zip` archive from the
[GitHub releases](https://github.com/minify-cx/minify/releases) page and verify it
with the accompanying `SHA256SUMS` file.

## Build from source

```bash
make
make test
ASAN_OPTIONS=detect_leaks=0 make test-sanitize # use only where LSan is unavailable
make benchmark
make distcheck       # from a clean committed standalone checkout
./minify app.js        # app.min.js
./minify -i app.js     # overwrite app.js
./minify --structured app.js # experimental, proven-safe binding renames
```

The library API accepts strings and returns strings/errors. File naming and destructive/non-destructive behavior belong to the calling CLI rather than the minification engine.

CLI output is prepared in a sibling temporary directory and committed only after
a complete successful write. Replacing an existing regular file preserves its
permission bits. Symbolic-link destinations are rejected rather than followed or
silently replaced.

## Experimental structured optimization

The default remains the conservative lexical minifier. `--structured` adds a
lossless token inventory, balanced delimiter model, scope graph, reference
resolution and deterministic printer, then renames only bindings whose safety
has been established. Dynamic lookup (`eval`/`with`), contextual names and
ambiguous syntax are excluded. `--structured-jsx-expressions` additionally
enables the same rewrite only inside JSX expression regions that were fully
parsed; JSX markup and surrounding JavaScript retain conservative behavior.

The token-inventory profile used 40 iterations. The former type-erased callback
manager ran about 60.4 million times and accounted for 4.1% of sampled time.
The conservative path now passes a nullable recorder pointer, so it does not pay
that callback dispatch cost when structured collection is disabled.

Checkpoints 11–20 establish collection, delimiter structure, scope discovery,
reference resolution, deterministic printing, safe parameter/local renaming,
object-shorthand expansion, JSX expression gating, and the explicit CLI release
gate. The structured mode remains opt-in while coverage expands to more binding
forms and transformations; aggressive compression remains reserved and inactive.

## Optional aggressive compression

`--aggressive` is an explicit CLI/API contract and is never selected by legacy
calls, `--structured`, or JSX defaults. It preserves successful execution and
produced values for supported transformations, but may change source shape,
stack/diagnostic detail and otherwise unobservable intermediate allocations.
Dynamic scope, unsupported syntax and transformations without a local proof are
left unchanged. Each aggressive transform is independently testable and must
fall back to structured output when its proof conditions are not met.

Property mangling has a stricter boundary: only names repeated through an
explicit `property_mangle_allowlist` (or CLI `--mangle-property=NAME`) are
eligible. Minify++ does not infer that a property is private or safe at an API
boundary; bracket-string, reflective and dynamically constructed access must be
accounted for by the caller before opting a name in.

The current aggressive implementation additionally covers locally proven exact
integer folding, literal conditional selection, narrowly unreachable debugger
removal, resolved compound assignment shortening, adjacent uninitialized `var`
joining and literal IIFE elimination. This is an experimental Linux checkpoint,
not a claim of broad optimizer parity with Terser, esbuild or Closure Compiler.

## Current adversarial gates

- Independent complete external checkpoints: CSS 31,155/31,155, HTML
  9,651/9,651, JavaScript 39,741/39,741 runtime-applicable, JSX/TSX 221/221,
  JSON 93/93, XML 535/535 and structural SVG 1,176/1,176.
- 15,459 executable JavaScript semantic programs.
- 180 JSX/TSX syntax + idempotence programs.
- 111 generated non-JavaScript idempotence documents.
- 70,000 deterministic mutation cases across all seven formats.
- standalone CLI smoke tests and format-specific C++ smoke tests.

The latest JSX fix distinguishes valid TSX generic arrows such as `<T,>(x:T) => ...` from nested JSX roots.

## Readiness

The scoped production-readiness decision has been revalidated after a real website
CSS failure forced it to be withdrawn. The repaired release passes substantially
expanded focused, semantic, fuzz, sanitizer, clean-package, embedded-integration,
and browser evidence. See the living assessment for its limits; do not infer
universal future-syntax or platform support.


## v1.0.1 hardening checkpoint

Minify++ remains format/API version 1.0, with the standalone CLI checkpointed as 1.0.1. The generated executable JavaScript differential corpus has grown to **10,707 programs**, and the JSX/TSX syntax + idempotence corpus to **160 programs**. This round added more async/generator/class/destructuring/regex contexts and deeper TSX generic-arrow/type-expression cases. All standalone C++, Node differential, generated, JSX and CLI gates pass.

## v1.0.3 generated-corpus checkpoint

The JavaScript semantic matrix has grown from **10,707 to 15,459 executable programs** and the JSX/TSX syntax + idempotence corpus from **160 to 180 programs**. The generated-JS harness now batches thousands of independent minification inputs through one native process, retaining per-program transformation isolation while eliminating process-startup overhead. New cases cover destructuring-heavy statement boundaries, additional async/generator/class/object contexts, harder regex forms, generic JSX utility/mapped/conditional types, TSX generic arrows, MathML/SVG and nested assertion/satisfies expressions.

## v1.0.2 non-JS contract checkpoint

Added a standalone generated/idempotence gate for **39 HTML/CSS/JSON/XML/SVG documents**. JSON additionally receives a structural semantic-equivalence oracle and malformed-input rejection. XML/SVG are deliberately documented and tested as conservative lexical minifiers rather than validating XML parsers. The public header guard was also renamed from its historical Nift-era name to the standalone `MINIFYPP_MINIFY_H` identity.
