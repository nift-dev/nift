# Nift 4.0.9 release candidate — go/no-go packet (draft)

This packet records the local release-candidate evidence for Nift 4.0.9. It is a
**draft**: no tag has been created and nothing has been released, published,
promoted or deployed. GitHub workflow dispatch (the non-publishing release
rehearsal and the Snap rehearsal), hosted CI at the final head, the final
go/no-go decision and tag authorization are marked **pending** — they have not
run and are not claimed.

## 1. Candidate heads and tag target

| Repository | Head | Role |
|---|---|---|
| canonical `nift` (previous candidate base) | `d3341dec47ba42dfdb416913ba3e37bbbaaa8feb` | pre-fix `main`; does not contain the fix or reconciled checkpoint |
| canonical `nift` (implementation/fix commit) | `9aa3c4ba1c15c36bd7150aba6d827bbd1ec5553b` | unreadable-source fix + checkpoint-8 CP3 reconciliation + new tests |
| canonical `nift` (release-preparation commit) | **PENDING** | release notes + this packet + handover/evidence reconciliation, committed after review |
| canonical `nift` (proposed `v4.0.9` tag target) | **PENDING** | = the reviewed release-preparation commit, never `d3341de` directly |
| website source (`nift-dev.github.io` `stage`) | `6aa8421` | pushed; already documents direct asset ownership |
| website generated (`nift-dev.github.io` `public/main`) | `9738d43` | pushed; live Pages confirmed serving this commit |
| CLI contract suite (`nift-regression-suite`) | `a6d508c` | 23/23 modules PASS against the candidate binary |
| `nift-embed` | `a63e6e5` (unchanged) | outside the release |

Version: **Nift 4.0.9** (`src/CLI.cpp:34`, `snap/snapcraft.yaml`, release notes,
guarantee-registry baseline). Post-release dev identity: **4.0.10**.

## 2. Commit classification (v4.0.8 … final head)

The classified range below runs from the v4.0.8 tag through the eventual final
release-preparation head; it is not limited to `v4.0.8..d3341de`. The
implementation commit and the release-preparation commit are included.

| Commit | Class |
|---|---|
| `53a3e66` Replace whole-channel snapcraft promote with guarded per-revision stable releases | release automation correction |
| `eedb784` Advance development identity to v4.0.9 + record v4.0.8 report | post-release version advancement + documentation/evidence |
| `fdb4ae9` Post-release cleanup: correct Snap release history, honest rollback reporting | release automation correction + documentation/evidence correction |
| `22017f9` Classify any target-version stable entry as non-recoverable rollback state | release automation correction |
| `5ad4a95` Correct resume-note wording: target-version assignments are not all already correct | release automation correction |
| `d3341de` Simplify asset ownership and use file terminology | included user-facing change (direct CSS/JS ownership in the output tree; "page(s)" → "file(s)" build/status terminology) |
| `9aa3c4b` Fix unreadable-source handling and reconcile checkpoint 8 with direct writes | correctness fix + release-gate reconciliation (unreadable content/`@input`/template no longer silently render empty; checkpoint-8 write-interruption cases re-asserted against the CP3 direct-write contract) |
| `<release-preparation>` (pending) | release-preparation commit: release notes, this packet, HANDOVER/ReleaseNotes reconciliation, refreshed checkpoint-8 evidence |

Every commit in the range appears in the release record; none are omitted.

## 3. v4.0.9 correctness work (this candidate)

- **Unreadable-source CLI fix (release blocker found during preparation).**
  `ProjectInfo::read_shared_source` returned an empty string (via
  `filesystem::read_file`) instead of `nullptr` for unreadable files, so the CLI
  silently rendered unreadable content/`@input`/template sources as empty and
  reported success. It now mirrors `ProjectState` (`read_file_checked`,
  `nullptr` on failure, failed reads never cached). This was a latent defect
  affecting v4.0.7 and v4.0.8, introduced by the CP1 RenderHost seam
  (`c02e88d`). New coverage: `tests/unreadable_source_smoke.sh` (wired into
  `make test`), `test_read_parity()` in `tests/project_state_parity.cpp`, the
  `unreadable-input-preserves-last-good` checkpoint-8 case, and the black-box
  `contract/unreadable_source_smoke.sh` in `nift-regression-suite`.
- **Checkpoint-8 reconciliation to the CP3 direct-write contract.** The gate's
  write-interruption cases were written for the pre-CP3 transactional-output
  design and were masked by the unreadable-content failure. They are renamed and
  re-asserted against the documented CP3 contract: `partial-direct-write-marks-unfinished-and-repairs`
  and `sigkill-during-direct-write-marks-unfinished-and-repairs` now assert that
  a failed or interrupted build does not claim success, `.nift/.unfinished`
  remains, an ordinary build refuses, `build --repair` reconstructs the complete
  correct output, the marker clears only after success, and `nift status` is
  clean afterward. `HANDOVER.md` checkpoint-8 text and the committed evidence
  were refreshed accordingly (16 → 17 cases). Authoritative state files
  (`tracked.json` etc.) remain atomic temp+rename; regenerable outputs and
  `.info.json` are deliberately direct-written.

## 4. Local validation evidence (Linux)

**Provenance.** The broader validation below was executed from working trees
based on `d3341de` (nift) and `71049a2` (regression-suite) with the unreadable
source fix, the new tests and the reconciled checkpoint present as uncommitted
edits; it is not claimed for binaries or suites contained in those commits.
After the implementation commit `9aa3c4b` and the regression-suite commit
`a6d508c` were created, the focused gates were rerun at those exact committed
heads.

- **Focused committed-head reruns.** Clean build at `9aa3c4b` (g++ 15.2.0 C++17
  `-O2 -pthread`): PASS, `nift version` → `Nift v4.0.9`. `make test` at `9aa3c4b`
  → exit 0 (incl. `test-unreadable-source`). `make test-project-state` at
  `9aa3c4b` → PASS (incl. `test_read_parity`). `make
  checkpoint-8-filesystem-transaction` at `9aa3c4b` → **17/17 PASS**; the
  refreshed evidence records commit `9aa3c4ba1c15c36bd7150aba6d827bbd1ec5553b`
  (the code-bearing implementation commit). `./run-contract.sh` at regression
  head `a6d508c` → **23/23 modules PASS** (the 23rd module is contained in
  `a6d508c`).
- **Broader working-tree validation (pre-fix-commits).** `make test` exit 0;
  `make test-installer` PASS; `make test-embed` (conformance 9/9) PASS; `make
  test-bindings` (go/csharp/node/python) PASS; `make test-build-boundary` PASS;
  `make test-project-state` PASS; `run-contract.sh` 23/23 PASS; Checkpoint 7
  720/720 PASS; Checkpoint 9 1217 parser-fuzz/resource cases PASS (ASan/UBSan
  binary); Checkpoint 10 18/18 portable cases PASS (local runner);
  `test-performance-scaling` PASS (median paired ratio 3.83x ≤ 7.00x;
  recovery-epoch scan-bound PASS); `benchmark-10k` PASS (full 0.109 s / no-op
  0.073 s medians); `benchmark-memory-10k` PASS (peaks ≤ 13 160 KiB);
  `packaging/test-version-derivation.sh` PASS; Minify++ `check-nift-sync.sh`
  PASS (24 files). The source is identical between `9aa3c4b` and the
  documentation-only release-preparation commit, so these results also hold at
  the final head (rerun after the release-preparation commit is recorded).
- Jsonic++ synchronization: **maintained `make test-jsonic-sync` target
  UNAVAILABLE locally** because the external `jsonic/jsonic` checkout has three
  pre-existing mode-only changes (exec bit stripped from
  `scripts/check-{minify,nift}-sync.sh` and `scripts/memory_safety.py`), which
  are unrelated environment state and were not altered. The content-equivalent
  check (`bash scripts/check-nift-sync.sh <nift>`) PASSED (20 files). The
  relevant hosted CI gate must pass from a clean checkout before release
  authorization.
- Website built with the exact candidate binary: 74/74 pages, exit 0; the
  generated `public/main` tree is byte-identical (no diff); source `stage` and
  generated trees are clean; `scripts/check_handover_display.py` PASS.
- Live `https://nift.dev/install` is byte-identical to `packaging/install.sh`
  (SHA-256 `a98bdf72…`); live homepage matches generated `public/index.html`
  (Pages is serving `9738d43`). No website mutation is required.

## 5. Pending items before tag authorization

- [ ] Release-preparation commit reviewed and committed on `nift` `main` (notes,
      this packet, HANDOVER/ReleaseNotes reconciliation, refreshed checkpoint-8
      evidence), then pushed.
- [ ] Broader validation rerun at the final release-preparation head and results
      recorded in this packet.
- [ ] Hosted CI at the exact final head: init targets, checkpoint-10 matrix,
      test-integrity guards, packaging matrix, performance-regression guards.
- [ ] Non-publishing `Release artifacts` rehearsal (`version=4.0.9`): exact
      four-asset set, extracted-archive smokes, installer gates, checksums.
      **PENDING** (workflow_dispatch).
- [ ] Non-publishing `Snap` dispatch: toolchain preflight (Snapcraft 9.0.1,
      revision 18514) + amd64/arm64 validation builds; no Store mutation.
      **PENDING**.
- [ ] Jsonic++ sync gate re-run from a clean external checkout (hosted/CI) —
      required because the local gate is blocked by the pre-existing mode-only
      worktree state.
- [ ] Confirmation that local `main == origin/main`, that `v4.0.9` does not
      already exist locally, remotely, or as a GitHub release, and that the
      public installer is still byte-identical at rehearsal time.
- [ ] Explicit tag authorization naming the final immutable release-preparation
      commit.

## 6. Repository cleanliness

- canonical `nift`: no generated or debug residue; the intended tracked edits
  (`HANDOVER.md`, `ReleaseNotes.md`, refreshed checkpoint-8 evidence) and the
  untracked `docs/evidence/release-4.0.9/` release edits are present until the
  release-preparation commit is made; the tree is verified clean afterward.
- website `stage`: clean; `public/main`: clean. `PENDING-WEBSITE.md` has no open
  items.
- `nift-regression-suite`: clean at `a6d508c` (version assertions at 4.0.9; the
  23rd contract module is committed).
- `jsonic/jsonic`: 3 mode-only changes remain as pre-existing environment state
  (untouched).

## 7. Proposed tag and release notes

- Tag: `v4.0.9` (annotated) at the reviewed release-preparation commit
  (**PENDING**, reported after commit).
- Release body = the reviewed notes file
  `docs/evidence/release-4.0.9/release-notes-4.0.9.md`, published verbatim by
  `release.yml` (`--notes-file`). No Embedded Nift mention.

## 8. Known limitations

Binaries unsigned; release is CLI-only; embedded engine/bindings/corpus/Rust
remain in-tree but unpublished and unpromoted. Snap and package-channel
publication is exercised by the real runs, not claimed from offline evidence.
Checkpoint-8 claim scope remains limited to the tested Linux failure classes
(no direct ENOSPC; no Windows/macOS permission/locking semantics).