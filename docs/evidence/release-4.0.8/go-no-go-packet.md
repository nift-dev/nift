# Nift 4.0.8 release candidate — go/no-go packet (draft)

This packet records the local release-candidate evidence for Nift 4.0.8. No tag
was created and nothing was released, published, promoted or deployed. GitHub
workflow dispatch, the Snap real rehearsal and all downstream publication checks
are marked **pending** — they have not run and are not claimed.

## 1. Candidate heads and tag target

The v4.0.8 candidate is the lock/scaling series on `main`, ending at the
amended lock-correction head. The final release-preparation commit (release
notes + this packet) is added next and is recorded externally, since this
document cannot embed its own hash.

| Repository | Head | Role |
|---|---|---|
| canonical `nift` (candidate series) | `951659b` (amended lock-correction head) | the six-commit candidate series below |
| candidate series | `2250a34` `.lock` rename · `d60a013` scaling methodology · `df6fb0f` lock write hardening · `f0b6f7a` scaling invocation guards · `f3e60b0` init establishes `.lock` · `951659b` lock init contract correction | build-progress repair, Braille spinner, Snap release automation, toolchain preflight, persistent `.lock`, repaired scaling guard |
| canonical `nift` (release-preparation commit) | `TBD` (reported externally after commit) | release notes + this evidence packet |
| canonical `nift` (proposed `v4.0.8` tag target) | `TBD` | = the final release-preparation commit |
| website source (`nift-dev.github.io` `stage`) | `ade73ca` | rebuilt 75/75 with the candidate; no generated diff |
| CLI contract suite (`nift-regression-suite`) | `00b6d54` (scaling benchmark synced) | 22/22 modules + historical/ruthless PASS against the candidate |
| `nift-embed` | `a63e6e5` | unchanged, outside the release |
| `nift-embed-regression-suite` | `be6c28b` | unchanged |

Version: **Nift 4.0.8** (`src/CLI.cpp:34`, `snap/snapcraft.yaml`, release notes).
Post-release dev identity: **4.0.9**.

## 2. Four-platform release rehearsal

**PENDING.** Requires dispatching the non-publishing `release.yml` rehearsal
(`version=4.0.8`) on GitHub (manually, via the Actions web UI). Required
verification once dispatched: unix (linux-x86_64 / macos-arm64 / macos-x86_64)
and windows jobs each build and extract-smoke their archive;
installer-preflight; `rehearse` validates the exact four-asset set and builds
`SHA256SUMS`; `publish` and `installer-public-smoke` are skipped. The definitive
per-release archive hashes are recorded by the tag-triggered run.

## 3. Local test evidence (Linux, candidate series head `951659b` binary)

- Clean release build (g++ C++17 `-O2 -pthread`): PASS, `nift version` →
  `Nift v4.0.8`.
- Full implementation-local suite: `make test` **exit 0** (all targets incl.
  progress-render 69/69 snap-contract, PTY, zero-mutation, repair, ownership).
- External contract suite: `run-contract.sh` against the candidate binary —
  **22/22 modules + historical + ruthless PASS**.
- ASan/UBSan build: clean; sanitized binary passed `--version`, init, full and
  incremental builds, the parser/value adversarial battery (15 cases) and the
  init functional-truth battery with no findings.
- Performance/scaling guards: `benchmark-10k` full-build median 0.100 s,
  no-op 0.073 s; tracking-scaling 4.30x ratio PASS; recovery-epoch scan-bound
  PASS; memory peaks ≤ 13 560 KiB (guard 16 384) PASS.

### 3.1 Full-build scaling guard — repaired methodology

- **Original hosted guard failure:** during both v4.0.7 and v4.0.8 release
  preparation the unchanged benchmark failed intermittently on the shared
  runner (a v4.0.8 hosted run reported **8.15x**); independent good-candidate
  measurements were around **3.23x** and **3.62x**. The old sequential
  sub-second methodology (1,000/4,000 pages, all small runs before all large
  runs, three samples, medians only) was found to be flaky and was repaired
  before release; the original failure is not concealed.
- **Repaired methodology:** 2,000/8,000-page fixtures (4:1), both warmed with
  an untimed full rewrite before timing; seven balanced interleaved rounds
  (order alternates per round to cancel shared-runner load drift); every timed
  build toggles the shared template so all outputs traverse the real
  transactional write path; complete raw samples are printed (all small/large
  durations, every paired ratio, median/min/max per size, fixture sizes, run
  count, threshold); the **median paired ratio** is the primary decision value
  (threshold unchanged at 7.00x). If the primary median exceeds the threshold,
  one predeclared confirmation phase runs with the starting order reversed; the
  run fails only if the confirmation median also exceeds it (this is not
  retry-until-green).
- **Good-build evidence (local, candidate binary):** five consecutive repaired
  runs all PASS with median paired ratios 3.74–3.87x (3.71x in the first run).
- **Quadratic red-demonstration:** a temporary (uncommitted) worktree
  recreating the historical once-per-output recovery-scan defect produced
  median ratios 17.92x (primary) and 17.85x (confirmation) → **FAIL: repeated
  scaling violation**, so the guard detects the historical O(N²) defect and the
  confirmation protocol distinguishes a single noisy phase from a repeatable
  violation.
- **Failure-mode checks:** the benchmark fails closed when a timed build does
  not rewrite all expected outputs (exercised through `Fixture.timed_build()`
  with a mocked successful no-op subprocess), when the expected output count is
  incomplete, when a timed build returns non-zero, and for every documented
  invocation invariant (`--small > 0`, `--large == 4 * --small`,
  `--rounds >= 7`, `--confirm-rounds >= 7`, `--max-ratio > 0`) — offline
  `full_build_scaling_failmodes.py` PASS.
- **Structural guards preserved:** the recovery-epoch scan-bound guard and the
  BH8 complexity invariants remain required and are unchanged; wall-clock
  timing does not replace structural complexity evidence.
- **Repaired hosted workflow result: PENDING** (requires a GitHub run at the
  new exact head).
- Memory guard: `memory_10k_benchmark.py` all peaks ≤ 13 560 KiB (guard
  ≤ 16 384 KiB) PASS.
- Embedded synchronization: Jsonic++ sync PASS (20 files); Minify++
  synchronization reconciled (Makefile + `tests/cli_smoke.sh` adopted from the
  standalone checkout) and the sync check now PASSES (24 files). Minify++
  standalone gates PASS: CLI smoke, format idempotence (115 documents), generated
  JS semantic corpus (15 459 programs), JSX (180 programs), Node semantics.
- CLI behavior: `version`, `about`, `commands` correct; unknown command exits 1
  and directs to `nift commands`; `--help`/`-h` exit 0; bare `help` word is an
  unknown command (per contract).
- `packaging/install.sh` is unchanged since the v4.0.7 baseline, so no new
  pre-tag public-installer deployment is required; the rehearsal's
  byte-comparison `public-installer-preflight` gate still applies.

### 3.2 Persistent `.lock` initialization and migration coverage

- `nift init` establishes a populated `.nift/.lock` (exact canonical sentence)
  with a scoped `.gitignore` rule and never creates `.unfinished`.
- Partial-init is reachable through the CLI: an existing non-empty `.lock` is
  preserved (contents + file identity), an existing empty `.lock` is repaired,
  a directory/symlink at `.nift/.lock` is refused, and injected
  write/flush/size/parent-sync failures fail init before `config.json`.
- Runtime acquisition creates/repairs `.lock` for older, copied,
  hand-assembled, partially initialized and legacy `.ownership-gate` projects,
  and fails closed before `.unfinished` on population, parent-sync, or
  legacy-removal failure. Legacy `.ownership-gate` is never removed while a
  live legacy-version process holds it.
- Cross-platform ownership unit tests and the POSIX ownership/concurrency and
  crash-recovery suites all PASS (the Windows unit path is covered by the
  cross-platform unit target).

## 4. Snap coordinator evidence

- Offline contract suite: `tests/snap_release_contract.py` — **69/69 PASS**
  (six-architecture set; edge revision selection; strict candidate
  verification; legacy i386 protection; rollback snapshot validation; exact
  revision smoke; immutable Snapcraft pin `18514`/`9.0.1`; non-publishing
  toolchain preflight; convergence polling; fail-closed paths).
- Live read-only probe of `https://api.snapcraft.io/v2/snaps/info/nift?fields=
  channel-map,revision,version` parsed the current public channel map (14
  entries, none malformed; declared edge at 4.0.8 = amd64/arm64/armhf/ppc64el/
  riscv64, s390x lagging at 4.0.7; legacy i386 reported). No Store mutation.
- Real Snap rehearsal run: **PENDING** — requires a manual non-publishing
  `Snap` dispatch on GitHub to verify toolchain-preflight (Snapcraft 9.0.1
  revision 18514), amd64/arm64 validation builds, `release-coordination`
  skipped, and no Store mutation.
- **The Snap automation has not yet run against the real Store; it must not be
  described as succeeded publicly until the rehearsal and the tag run complete.**

## 5. Installer / upgrade / checksum evidence

- `make test-installer` PASS (in `make test`).
- Pre-tag public-installer gate: `packaging/install.sh` unchanged since v4.0.7;
  the rehearsal byte-comparison gate is **pending** (requires workflow dispatch).
- Local Linux archive layout validation (this environment): built
  `nift-4.0.8-linux-x86_64.tar.gz` (nift, README.md, LICENSE); fresh extract
  reports `Nift v4.0.8`, `about`/`commands` correct, unknown command exit 1,
  `--help` exit 0, LICENSE present, exec bit set, fresh-project
  `init`/`build`/`status` smoke PASS. Local archive SHA-256 (validation only;
  the definitive release hashes come from the tag-triggered run):
  `cdefb1bf5e17d1e2eb9e7ea0141c33d1aae55ef0723ecb799a759ee2ed1645a6`.

## 6. Website / documentation

- `docs/handover/PENDING-WEBSITE.md` reviewed: **"Open items: None currently."**
  recorded as a completed review of an empty queue.
- Website built with the exact candidate binary: **75/75 pages**, exit 0; the
  generated `public/` tree is byte-identical (no diff), so the homepage is
  unchanged. A stale 0-byte `.nift/.ownership-gate` marker that predated the
  build was removed and, after the `.lock` rename, the project's persistent
  serialization file is `.nift/.lock` (a normal artifact that residue checks
  allow); both website source `stage` and generated `public/` trees are clean.
- Website source/generated commits for the release: **pending** (no website
  content changes are required; none were made).

## 7. Proposed tag and release notes

- Tag: `v4.0.8` (annotated) at the final release-preparation commit (TBD,
  reported externally after this packet is committed).
- Release body = the reviewed notes file
  `docs/evidence/release-4.0.8/release-notes-4.0.8.md` (now including the
  persistent `.lock` section), published verbatim by
  `release.yml` (`--notes-file`). No Embedded Nift mention.

## 8. Outstanding items before tag authorization

- [ ] Hosted CI on the new exact head: cross-platform build/test, performance
  regression guards (the repaired full-build scaling guard must pass), packaging
  and integrity checks, init-target checks, Snap build validation.
- [ ] Manual non-publishing `Snap` dispatch on `main`: preflight (Snapcraft
  9.0.1, revision 18514) + amd64/arm64 validation builds pass,
  `release-coordination` skipped, no Store channel mutated.
- [ ] Non-publishing `Release artifacts` rehearsal (`version=4.0.8`): exact
  four-asset set, extracted-archive smokes, installer gates, checksums.
- [ ] Confirmation that local `main == origin/main` and that `v4.0.8` does not
  already exist locally, remotely, or as a GitHub release.
- [ ] Explicit tag authorization naming that one immutable commit.

## 9. Repository cleanliness

- canonical `nift`: clean after the release-preparation commit (0 porcelain
  residue; the local archive workspace lives under `/tmp`).
- website `stage`: clean; `public/` clean. `docs/handover/PENDING-WEBSITE.md`
  reviewed: **"Open items: None currently."**
- `nift-regression-suite`: clean; `nift-embed`, `nift-embed-regression-suite`:
  clean and unchanged.

## 10. Known limitations

Binaries unsigned; release is CLI-only; embedded engine/bindings/corpus/Rust
remain in-tree but unpublished and unpromoted. Snap and package-channel
publication is exercised by the real runs, not claimed from offline evidence.