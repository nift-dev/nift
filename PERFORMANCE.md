# Nift performance notes

## 10,000-page regression recovery

The v1.0.39 checkpoint exposed a large-project regression in `ProjectInfo::load_tracking()`: duplicate-name and derived-path collision validation repeatedly scanned the already-loaded tracking vector. At 10,000 tracked pages that created roughly 50 million comparisons plus repeated path derivation, making project open approximately O(n²).

v1.0.40 replaced those scans with reserved `unordered_set` indexes for tracked names, derived content paths, and derived output paths. `test-tracking-scaling` permanently compares 2,000-page and 10,000-page project-open times; linear scaling is about 5× and the guard currently permits 8× to tolerate machine noise while still rejecting quadratic behaviour.

## Incremental profiling

After the tracking fix, the remaining gap was concentrated in `build_reasons()`, not rendering. A no-op 10k run spends most of its time reading/validating page-info plus checking dependency paths. The expensive part of the latter was repeatedly canonicalising every dependency path to preserve symlink-escape safety.

v1.0.41 keeps that safety invariant but caches canonical containment for each parent directory. Ordinary non-symlink leaves then need only a cheap leaf status check; only symlink leaves require the full canonical containment operation. Page-info modification time is also read once per page and reused across its dependency checks.

On the development host after one untimed warm build, `benchmark-10k` measured:

| Modified-mode case | v1.0.41 checkpoint |
|---|---:|
| Full build, 10k pages | ~0.21–0.26 s median |
| No-op `build` (incremental) | ~0.09–0.10 s median |
| One content page changed | ~0.09–0.11 s |
| Shared template changed, rebuild 10k | ~0.31–0.35 s |

These numbers are checkpoint measurements, not portable performance guarantees. Use `make benchmark-10k` and `make test-tracking-scaling` on the target machine when evaluating changes.


## v4.0.4 transactional-write scaling regression recovery

Dogfooding after the v4.0.4 ternary fix exposed a much older full-build regression
introduced by Checkpoint 8 filesystem hardening. Transactional writes correctly
staged output to same-directory temporary files, but stale-temp recovery scanned
the entire parent directory before every generated file write. For a flat N-page
site this made output work approximately O(n²).

The fix preserves atomic same-directory staging while making stale-temp recovery
epoch-scoped and lazy. A build pass starts a new recovery epoch; each parent is
scanned at most once when it next receives a write in that epoch. That restores a
recovery opportunity in long-running `build --auto` sessions without scanning on
idle/no-write polls. Temporary files whose recorded owner PID is still live are
preserved so overlapping writers are not destroyed. PID reuse may conservatively
delay cleanup; Nift prefers leaking a stale temp over deleting a potentially live one.

Two independent scaling guards are now maintained:

- `make test-tracking-scaling` protects tracked-project loading;
- `make test-full-build-scaling` protects full-build output work;
- `make test-recovery-epoch` asserts one recovery directory scan per distinct touched parent per epoch;
- `make test-performance-scaling` runs all three guards.

The new full-build guard compares 1,000 and 4,000 flat pages and deliberately
toggles the shared template between runs so every output changes and must pass
through the transactional writer. Linear scaling is about 4× and the guard
permits 7× for platform/filesystem noise. On the fix workspace it measured about
3.1–4.3× across repeated runs. The historical buggy writer fails the same guard
family even at 100→400 changed pages (about 8.9× in the reproduction), and larger
fixtures become dramatically worse.

Repeated `build --all` of byte-identical output is also optimized safely: Nift still
renders and validates every page, but it avoids a temp-write/rename for unchanged
bytes and refreshes the file mtime instead so modified-mode state remains current.
Any changed bytes still use the transactional temp→replace path.

On the current container, the repaired retained 10,000-page benchmark measured
about 0.21–0.24 s median for repeated full builds, back in the retained ~0.21–0.26 s
v1.0.41 checkpoint range. The same container measured v4.0.1 at about 0.31 s.
Absolute timings remain machine-specific; the durable regression evidence is both
restored historical full-build performance and near-linear changed-output scaling.
The maintained Checkpoint 8 and independent black-box recovery cases also prove
that a dead-owner temp created after an earlier scan in a long-running
`build --auto` process survives idle time but is removed on the next relevant build
activity. That test is demonstrated to fail the previous once-per-process repair.

## v1.0.42 memory checkpoint

The 10,000-page memory pass separates fixed process overhead from project-scale
allocations and checks full, no-op, single-page, and shared-template rebuilds
with `/usr/bin/time -v`.

The main regression was not retained build state; it was temporary validation
state in `load_tracking()`. v1.0.41 used three node-based hash sets at once for
names/content/output collision checks. v1.0.42 parses first, then validates names
with a compact pointer vector and content/output paths one vector at a time.

Representative O2 measurements from the checkpoint environment:

- full 10k build: about 10.9 MiB peak RSS
- no-op 10k incremental: about 10.8 MiB
- one-page incremental: about 10.5 MiB
- shared-template rebuild of 10k: about 11.5 MiB
- process-only (`nift version`): about 4.6 MiB

The retained historical stripped-Nift benchmark is about 10.7–11.4 MiB for its
common modified-mode no-op/full cases, so current Nift is back in that range.
The remembered ~8 MiB low-water mark may have occurred in another workload or
earlier build, but it is not supported by the retained 10k benchmark CSV.

