# rt-diff — differential harness (CP08)

`tools/rt-diff/rt-diff --a REF --b CANDIDATE --corpus DIR ...` runs every corpus
entry with both binaries in fresh temporary copies and reports every difference.
It is a mandatory certification gate (D-010): `make test-runtime2-rt-diff`
(same binary on both sides: proves the corpus is deterministic) and
`make test-runtime2-rt-diff-selftest` (proves the harness catches differences).

Same-binary runs establish the comparison infrastructure, not language
correctness; correctness comes from comparing the legacy engine with the new
one, with approved differences expressed as policies.

## Entries

| Kind | Discovered as | Run |
| --- | --- | --- |
| script | `*.f` | `nift <name>.f [args]` in a copy of its directory |
| tree | directory with `rt-diff.only` | the whole directory is copied; only the named script is an entry (extracted smoke scripts, generated programs with modules) |
| eval | `*.eval` | one `nift eval --json <expr>` per line |
| site | directory with `.nift/config.json` | steps (default `nift build --all`) |
| workspace | directory with `rt-diff.workspace` (names the project subdirectory) | the whole directory is copied, so a project can use `..`, neighbours, external output directories and relative symlinks |

Sidecars: scripts take `<name>.args`, `<name>.env`, `<name>.stdin`; sites and
workspaces take `rt-diff.steps` / `rt-diff.env` inside the directory or
`<dir>.rt-diff.steps` / `<dir>.rt-diff.env` beside it (non-invasive for shared
fixtures). Steps: `nift ARGS`, `write PATH TEXT`, `append PATH TEXT`,
`remove PATH`, `symlink TARGET PATH`, `sleep SECONDS`; `$WS` expands to the
copy's absolute path (absolute-path cases).

Corpus errors are failures (exit 2): a missing or empty corpus directory, fewer
entries than `--min-entries`, duplicate entry ids, an exclusion without a
reason. Entry ids are `<label>/<path>`; the label is the corpus directory name,
extended by parent names on collision (`python/app`, `node/app`).

## Isolation

- A and B run one after the other in the **same absolute directory** (a fresh
  `mkdtemp` per entry, emptied between the two runs). Path-dependent values
  that Nift writes (glob listing fingerprints hash absolute paths) are
  therefore directly comparable; nothing in page-info is masked.
- The working copy sits three levels below the snapshot root
  (`$BASE/s/t/u/w`, HOME and TMPDIR beside it); the whole root is
  snapshotted, so writes that escape the copy through `..` are compared.
- Every nift step runs in its own session and process group; the group is
  killed on timeout and again after the step, so no background descendant
  outlives a run. A timeout on either side is reported as a `timeout`
  difference, never as equal.
- After a `write`/`append`/`remove`/`symlink` step the harness waits 20 ms
  before the next nift step, so an edit never shares the filesystem
  timestamp tick of the render that follows. Without this, whether CP01
  records a `tick-hashes` entry depended on scheduling (observed:
  `workspaces/missing-external` differed between identical runs). This
  removes a harness-made race; it masks nothing.

## Channels compared

| Channel | What |
| --- | --- |
| timeout | either side timed out |
| exit | exit code of every step |
| stdout, stderr | per step, with a step marker for multi-step entries |
| combined | stdout and stderr on one pipe (write order), for scripts. A runs twice; the channel is compared only when A's two runs agree (measured stability, reported as `combined order unstable` counts) — no text heuristics |
| files | every file, directory (including empty ones) and symlink under the snapshot root: mode and exact bytes (page-info included, byte for byte) |
| home, tmp | the same, for HOME and TMPDIR |
| trace | Host-op trace grouped by context (page, `script`, `<owner>/async#N`, `<owner>/thread#N`); every event is compared as a whole JSON object (unknown fields included), in order within each context |
| ctx-order | order in which pages first appear in the trace, for builds whose config sets `build-threads: 1` (deterministic page order) |

Every site and workspace entry ends with `nift status`, which reads back the
page-info Nift wrote, so wrong metadata also surfaces as behaviour.

Eval output is compared byte for byte (key order and number formatting are
behaviour).

## Normalisations (complete list)

| # | Rule | Why | Self-test |
| --- | --- | --- | --- |
| 1 | the copy's path → `$ROOT`, HOME → `$HOME`, TMPDIR → `$TMPDIR`, snapshot root → `$BASE`; each also under its realpath **only if** that differs (macOS `/var` → `/private/var`) | temporary location (identical for A and B; mapped so reports are stable across runs) | `home-tmp-swap` caught |
| 2 | for site/workspace steps only, a whole line `time taken: <number> seconds` on nift's stdout/stderr → `time taken: $T seconds` | Nift's build summary prints wall-clock time | `time-number` hidden; `time-word` and `x-time-user` (a script printing the same text) caught |

Removed in round 2: JSON re-serialisation of page-info, dropping
`racy-dependencies`/`tick-hashes`, masking `listings` values, the text-based
combined-channel skip.

Not normalised (decided): process ids (no output contains them), directory
enumeration order (the language defines it: `ls` sorts), timestamps other than
rule 2 (none observed), thread scheduling (handled by per-task contexts, not by
normalisation).

## Not compared, precisely

- Order **across** contexts, except page order in single-threaded builds: the
  trace records no happens-before edges (e.g. a parent's operations after
  `await`/`join` relative to the child task), so cross-task order cannot be
  told apart from scheduling. Per-task order is compared.
- Untraced operations (`trace-format.md`).

## Exclusions

`tools/rt-diff/ignore.txt` (or `rt-diff.ignore` in a corpus):
`<pattern> channels=<list|all> # reason`. Only the named channels are skipped;
the rest of the entry is still compared.

## Approved differences (policies)

`--policy FILE` (JSON list). A rule names a decision, one exact entry id, one
channel and an exact substitution:

```json
{"id": "loop_interpolation-stdout", "decision": "D-009#6",
 "entry": "semantics/loop_interpolation.f", "channel": "stdout",
 "old": "<exact text in A>", "new": "<exact text in B>", "count": 1,
 "justification": "...", "test": "tests/runtime2/semantics/loop_interpolation.f"}
```

The difference is approved only for that entry id **and** source
(`source_sha256`: the script file, or every file of a site/workspace), and only
if replacing exactly `count` occurrences of `old` in A's channel yields B's
channel byte for byte. Refused at load (exit 2): patterns, empty `old`,
`old == new`, a channel other than stdout/stderr/combined/exit, a `count`
below 1, a `decision` not shaped like `D-009#6`, a `test` whose first token
is not an existing repository path. A policy that approves nothing in a run is
reported as `UNUSED-POLICY` and fails the run.
Approved differences are reported as `APPROVED` and do not fail the run;
anything else is a regression. `tools/rt-diff/policies/d009.json` holds the
D-009 rules; their intended outputs are the `<probe>.r2.*` goldens beside the
legacy goldens (`NIFT_VM_CORPUS_EXPECT=r2 tests/runtime2/vm_corpus.sh`).

## Reproducibility

Generated programs start with `// rt-diff gen.py --seed S index I`; the same
seed regenerates byte-identical programs. `--repro-dir DIR` delta-minimises each
differing generated program (line-based, while the difference persists). It
copies the whole entry directory (modules, data), first re-checks that the
difference recurs in isolation, and re-checks the final program before
reporting it (`REPRO … verified`; otherwise `REPRO-WARNING`). The output is a
directory holding the minimised program, its module/data files and a
`replay.sh`. Reports (`--report`) record the exact command line.

## Coverage of the trace

`scripts/trace-coverage` exercises every traced operation; the self-test drops
each operation category from the candidate's trace and requires a difference
(17 categories). Untraced operations are listed in `trace-format.md`.
