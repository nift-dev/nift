# CP49-1 — binding warning maintenance

Baseline core: `9de5c3e`; living-roadmap commit `ba09953`. C ABI 1.3 and public binding signatures are unchanged. No package publication or packaging redesign.

GCC/Clang `-std=c++17 -Wall -Wextra -Wpedantic` reproduced Node unused callback parameters (6) plus GCC misleading indentation (2); Python unused callback parameter, partial PyTypeObject initializers (100 GCC field diagnostics / 2 Clang aggregate diagnostics) and four PyModuleDef slots (4 GCC / 1 Clang diagnostic). Fixes omit unused parameter names, split control flow, zero-initialize type slots with the version-specific CPython object header, and explicitly initialize module slots. No suppressions.

Strict GCC+Clang wrapper checks now pass with `-Werror`; `make test-binding-warnings` is a dependency of `test-bindings`. Go build/race tests PASS; C# build/tests 29 PASS and additional warning-as-error build 0 warnings/errors; Node native build and 28 tests PASS; Python 3.14 native build and 25 tests PASS. The aggregate `make test-bindings` completed successfully; both native artifacts were built after the wrapper edits. Go/C# emitted no first-party warnings. Initial restricted Go-cache write failure was environmental; authorized retry passed. Native tests cover existing disposal/callback/lifetime behavior.

Local supported toolchains: GCC, Clang, Go 1.26.0, .NET 10.0.112, Node 22.22.1, Python 3.14.4 on Linux. This checkpoint does not claim new Windows/macOS or older-Python certification. These four bindings remain maintained experimental wrappers; formal package publication/support reassessment remains deferred. Raw logs and warning counts retained under `.build/cp49/`.
