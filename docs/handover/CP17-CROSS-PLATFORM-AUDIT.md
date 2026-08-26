# CP17 cross-platform binding behaviour audit

Status: 2026-08-26.

The C ABI is the cross-platform boundary: the canonical C++ Embed is exercised
on Linux, macOS and Windows by the Checkpoint-10 workflow (3-OS behavioural
corpus + comparison). The production bindings (C#, Node, Python) are thin
adapters over that frozen ABI and are built+tested on Linux (the binding-gates
CI job and local). This audit records the platform-sensitive surface and the
residual items scheduled for the release/packaging phase.

## Loader-key separator normalization (fixed in CP17)

The engine reports loader keys and dependencies with forward slashes
(`path.lexically_normal().generic_string()`) on every platform, while a
Windows corpus root is expressed with backslashes. All five harness adapters
(c-abi, go, cs, js, py) previously trimmed only the root's trailing
separators without normalizing both sides, so a Windows root would never
match a forward-slash key prefix and loaderKeys would stay absolute. Each
adapter now normalizes `\` -> `/` on both the root prefix and every key before
stripping. Covered by Go unit tests (Windows-style root) and direct checks of
the other four; the shared corpus remains 36/36 on Linux.

## Native-library resolution (documented; packaged at release)

- C#: `DllImport("libnift_c")` with a `DllImportResolver` honouring
  `NIFT_NATIVE_LIB`; per-OS library name (libnift_c.so/.dylib/nift_c.dll) is
  a packaging concern.
- Node: `build.sh` locates N-API headers via `NIFT_NODE_INCLUDE` then the
  distro include dirs; the `.node` extension suffix differs per OS.
- Python: `build.sh` uses `python3-config`; the extension suffix
  (`_nift<EXT_SUFFIX>.so`/`.pyd`) differs per OS.

## Callback exception/panic boundaries (verified across bindings)

- Go: host callbacks recover panics (TestPanicContainedInCallback).
- Node: thrown callbacks contribute their message as the exact diagnostic
  (exception-containment test).
- Python: raised callbacks contribute their message (exception-containment
  test).
- C#: FIXED in CP17 - user delegates could previously throw across the native
  callback boundary (undefined behaviour in C++); they are now contained and
  surfaced as a controlled host Error whose diagnostic is the exception
  message (new test, suite 20 -> 21).

## Residual (release/packaging phase, not CP17 blockers)

Building and exercising the C#/Node/Python bindings on Windows and macOS
native runners (per-OS library naming, header discovery, extension suffix)
remains for the packaging/release stage; the underlying C ABI they consume is
already 3-OS verified.
