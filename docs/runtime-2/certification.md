# Checkpoint certification (D-010)

## Tiers

| Tier | When | Command |
| --- | --- | --- |
| 1. Implementation | while editing | the affected suites: `python3 scripts/certify.py --targets <t>...`, or `--quick` (runtime and Runtime 2.0 smoke set), plus focused rt-diff runs |
| 2. Checkpoint | before committing a checkpoint | `make certify` (full, isolated, classified), regression suite (`nift-regression-suite` `runtime-2`), rt-diff gate; sanitizers and performance gates where the checkpoint requires them |
| 3. Post-push | after pushing to `runtime-2` | CI: `checkpoint-10-cross-platform`, `v44-cross-platform`, `test-integrity`, `runtime2-regression-suite` (Linux, macOS, Windows; toolchains unavailable locally) |
| 4. Milestone | major architectural milestones | broad differential campaigns, fuzzing, full benchmark suites |

The full matrix is not re-run after every small edit; tier 1 is enough until a
checkpoint is ready.

## `make certify`

`make certify` runs `scripts/certify.py` over `certify-targets` (= `test-all`
plus every suite the CI certification workflows run). It:

1. builds every non-test prerequisite once (`make -k`), so concurrent targets do
   not race to build shared objects;
2. runs parallel-safe targets concurrently (`CERTIFY_JOBS`, default `nproc`)
   and **exclusive** targets serially afterwards;
3. re-runs every failure alone, then classifies it;
4. writes `.build/certify/<target>.log` and `.build/certify/summary.{txt,json}`.

`tests/runtime2/certification_guard.sh` (part of `make test`) keeps
`certify-targets` complete: it fails if any `make` target, or any test/script a
workflow launches directly (`bash tests/…`, `python scripts/…`, `./tests/…`),
is not reached by a `certify-targets` recipe. The only CI-only script is
`scripts/compare_checkpoint10.py` (compares three runners' outputs).

## Classifications

| Status | Meaning | Counts as pass? |
| --- | --- | --- |
| PASS | passed in the certification run | yes |
| FLAKY | failed in the concurrent run, passed alone (load-sensitive; usually a wall-clock guard) | no — reported |
| SKIPPED-ENVIRONMENTAL | failed here only because of the environment, shown by re-running: e.g. fails as root but passes as the unprivileged user (`--unprivileged-user`, default `tester`; without one the failure stays FAIL) | no |
| UNVERIFIED | a dependency or platform is unavailable, detected from the environment and the log: tool not installed (e.g. `dotnet`), Go toolchain download blocked, `/usr/bin/time` missing, sibling `nift-packages` checkout missing | no — CI covers it |
| BASELINE-FAILURE | fails the same way on the v4.12.0 baseline tree (`../baseline-src`, built from `ce8cd36`) under the same conditions | no — pre-existing |
| FAIL | anything else | no; `make certify` exits 1 |

Classification never relies on a target's name alone. `--strict` also fails on
anything that is not PASS or FLAKY.

## Exclusive targets

From the isolation audit (`scripts/certify.py --audit report.json`, run in a
disposable copy: every target run serially, recording every file it changed in
the checkout and any new TMPDIR entry):

| Target | Why it runs alone |
| --- | --- |
| `test-build-boundary-nondestructive` | snapshots the whole checkout before/after; a concurrent writer is reported as a modification |
| `test-build-boundary` | builds from a clean copy of the checkout |
| `test-cp18-bytes` | rebuilds `src/embed/*.o` in the source tree |
| `test-cp21-bytes` | rebuilds `libnift_c.a` |
| `test-v45-embed-staged-consumer` | restages `dist/embed-prefix` |
| `test-go-binding`, `test-csharp-binding` | restage `dist/embed-prefix` and build binding products |
| `test-node-binding`, `test-python-binding` | build the binding native modules |
| `test-minify` | sub-make writing `minifypp/.build` |

Any target whose recipe runs a sub-make is also treated as exclusive
automatically.

Every target runs with a **private TMPDIR** (removed afterwards). Many C++
tests create fixed names under the temp directory
(`temp_directory_path() / "nift-host-seam-pg"`, `nift-c-abi-pagination`,
`nift-engine-*`, …); without a private TMPDIR they collide with concurrent
targets, with other checkouts running the same tests, and with a different
user's leftovers (a non-root run failed `test-c-abi` and `test-host-seam` with
"Permission denied" on root-owned `/tmp/nift-*` directories).

Audit results (2026-10-17, 159 targets): only the targets above wrote outside
`.build/`; the only output written by two targets was `dist/embed-prefix`
(`test-go-binding`, `test-v45-embed-staged-consumer`); no target left a
fixed-name file in TMPDIR. Three tests that used fixed `/tmp` names
(`v43_cp137_cp140_frontmatter.sh`, `v43_cp141_cp163_content_model.sh`,
`v44_ast_constant_fold_smoke.sh`) now use private `mktemp` directories; they
broke whenever a root-owned file with the same name was left behind.

## What CI covers that local runs cannot

UNVERIFIED items are validated by the post-push tier: the binding gates (Go,
C#, Node, Python) in `test-integrity`; macOS and Windows in
`checkpoint-10-cross-platform` and `v44-cross-platform`; package dogfood with
the pinned `nift-packages` checkouts in the regression-suite workflow. A
platform is claimed only after its CI job passes on the pushed commit.
