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

## Channels compared

| Channel | What |
| --- | --- |
| exit | exit code of every step |
| stdout, stderr | per step, with a step marker for multi-step entries |
| combined | stdout and stderr on one pipe (write order), for scripts that do not use `async`/`thread` (their stream order is legitimately nondeterministic) |
| files | every file, directory (including empty ones) and symlink under the copy: mode, content hash; `.nift/` included |
| home, tmp | files created under HOME and TMPDIR (distinct directories) |
| trace | Host-op trace grouped by context (page, `script`, `<owner>/async#N`, `<owner>/thread#N`), compared in order within each context |

Eval output is compared byte for byte (key order and number formatting are
behaviour).

## Normalisations (complete list)

Each rule has a self-test in `tests/runtime2/rt_diff_selftest.sh` showing the
rule hides its own variation and nothing adjacent.

| # | Rule | Why | Self-test |
| --- | --- | --- | --- |
| 1 | the copy's path (and realpath) → `$ROOT` | temporary location | `home-tmp-swap` (other paths still distinct) |
| 2 | HOME → `$HOME`, TMPDIR → `$TMPDIR`, their parent → `$BASE` | temporary location | `home-tmp-swap` is caught |
| 3 | `time taken: <number> seconds` → `time taken: $T seconds` | wall clock | `time-number` hidden, `time-word` caught |
| 4 | page-info (`.nift/**/*.info.json`): drop `racy-dependencies` and `tick-hashes` | record timing races, not behaviour (CP01) | normaliser unit check (other keys stay visible); `nift-garble` caught |
| 5 | page-info `listings` values → `$FP` (keys kept) | glob fingerprints hash absolute paths | normaliser unit check (listing keys stay visible) |

Not normalised (decided): process ids (no output contains them), directory
enumeration order (the language defines it: `ls` sorts), timestamps other than
rule 3 (none observed), thread scheduling (handled by context grouping, not by
normalisation). Entries whose output is nondeterministic by design are excluded
with a reason in `tools/rt-diff/ignore.txt`.

## Approved differences (policies)

`--policy FILE` (JSON list). A rule names a decision, one exact entry id, one
channel and an exact substitution:

```json
{"id": "loop_interpolation-stdout", "decision": "D-009#6",
 "entry": "semantics/loop_interpolation.f", "channel": "stdout",
 "old": "<exact text in A>", "new": "<exact text in B>", "count": 1,
 "justification": "...", "test": "tests/runtime2/semantics/loop_interpolation.f"}
```

The difference is approved only if replacing exactly `count` occurrences of
`old` in A's channel yields B's channel byte for byte. Patterns are refused.
Approved differences are reported as `APPROVED` and do not fail the run;
anything else is a regression. `tools/rt-diff/policies/d009.json` holds the
D-009 rules; their intended outputs are the `<probe>.r2.*` goldens beside the
legacy goldens (`NIFT_VM_CORPUS_EXPECT=r2 tests/runtime2/vm_corpus.sh`).

## Reproducibility

Generated programs start with `// rt-diff gen.py --seed S index I`; the same
seed regenerates byte-identical programs. `--repro-dir DIR` delta-minimises each
differing generated program (line-based, while the difference persists) and
writes `<id>.min.f` plus a `replay.sh`. Reports (`--report`) record the exact
command line.

## Coverage of the trace

`scripts/trace-coverage` exercises every traced operation; the self-test drops
each operation category from the candidate's trace and requires a difference
(17 categories). Untraced operations are listed in `trace-format.md`.
