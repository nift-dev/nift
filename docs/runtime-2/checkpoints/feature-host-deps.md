# feature-host-deps — host-operation correctness before CP16 (D-028)

Status: **implemented, not certified**, on branch `feature-host-deps` (from
`runtime-2` @`59e9fa5`), local commits only. `make certify` is run by the
owner. Linux only; macOS/Windows pending the next CI milestone (D-004). The
Windows half of the process-capture bound (`src/Process.cpp`, `#ifdef _WIN32`)
was written but never compiled here.

Owner decision D-028: legacy gaps in host operations are fixed in legacy
first, so both engines share one corrected meaning, instead of the VM
reproducing them in CP16. Each change below is an **approved language change
under D-028** (old vs new behaviour stated per change). Related decisions:
D-002 (filesystem freedom, dependency tracking covers external files), D-020
(loud errors, success means success, failed builds keep last-good output),
D-022/D-025 (error identity, severity and catchability are separate; resource
failures get their own codes), D-024 (consistency register; this checkpoint
does not change anything listed there).

Input: the CP16 design record (`runtime-2-cp16-design` @`00bd241`,
`docs/runtime-2/checkpoints/cp16-host-ops-design.md`: finding 2/3/4, Q-H2,
Q-H4, Q-H8, O1, O3) and its fixtures `tests/runtime2/cp16/`.

## 1. Dependency tracking: every read is a dependency

### Design

All recording goes through the existing CP02 observation channels
(`src/ParserObservation.cpp`): `result_.dependencies` plus the hash/hybrid
snapshot (`RenderHost::observe_dependency_hash`), `observed.absent`, and one
new section, `observed.unreadable`. Standalone scripts record nothing, as
before.

| Situation | Recorded | Stale when |
|---|---|---|
| successful whole read (`open`, `cat`, `open_bytes`, `file().open("r"/"rw")`, `minify(file)`) | dependency + snapshot of the bytes read | content (hash/hybrid) or mtime (modified/hybrid) changes |
| file-backed input stream (`ifstream(p)`, `s.open(p)`) | dependency **at successful open**; hash/hybrid snapshot = streamed hash of the file at open | as above; an edit after open (even mid-stream) leaves the snapshot different from the file, so the next build rebuilds; modified mode sees the write as racy/newer |
| `page("x").content`, `page("x").metadata` | dependency on page x's content file (snapshot at read) | as above |
| read of a missing path (ENOENT, ENOTDIR, dangling symlink) | `absent` (as `exists()` and `sha256_file()` already did) | the path appears |
| read of an existing path that cannot be opened (EACCES, ELOOP, ENXIO, …) | `unreadable[path] = token` where token = `filesystem::read_access_error(path)` (`errno:13`) | the token changes (becomes readable, other error, disappears) |

Cost of the stream snapshot (review R3): in hash/hybrid page builds the
whole file is hashed at open, regardless of how many bytes the page then
consumes, and the build engine hashes it again when it writes the page's
metadata (twice in total, streamed, bounded memory). Follow-up F9: hash once.

Why "at open" for streams: what a stream will consume is not known at open,
and recording at read would need per-read bookkeeping; recording at open is
conservative (a stream opened and never read still makes the page depend on
the file, which is correct for "does this page depend on the file existing
and its content"). The snapshot is hashed at open by
`filesystem::hash_path_streamed` (64 KiB chunks, bounded memory, same value as
`hash_path`), never by reading devices or FIFOs (they hash as empty, exactly
as `hash_path` does). Cost: in hash/hybrid page builds a file streamed by a
page is read twice (once to hash, once by the page); modified-mode builds and
standalone scripts pay nothing.

`unreadable` is new page-info state: `"unreadable": {"<spelling>": "<token>"}`,
written only when non-empty, checked in `read_observation_reasons`
(`path readability changed: <path>`), dropped for paths the render wrote
(like `absent`/`kinds`), merged for build-script observations. The token is
computed by the same function at render and at check time (POSIX
`open(O_RDONLY|O_NONBLOCK|O_NOCTTY|O_CLOEXEC)` then `close`, never reads, never
blocks on a FIFO; Windows `CreateFileW(GENERIC_READ)`), so they agree by
construction. Older page-info has no section and is unaffected.

**Guarantee by mode (review R1).** Hash and hybrid compare content hashes and
see every content change. Modified mode compares mtimes only, so it misses
any change that leaves the dependency's mtime older than the page's build
metadata: a same-mtime rewrite, a file renamed over the dependency with an
older mtime (also mid-stream), a symlink re-pointed at an older file, and
permission changes on a file that was read (F6). The "incremental equals
clean" guarantee of this checkpoint is therefore stated for hash and hybrid;
modified mode keeps its documented mtime contract.

Symlinks: the recorded spelling is the path as given (lexically normal);
mtime checks use `stat` (follows links) and hashes read through the link, so
an edit of the **target** content is seen in every mode (tested). Re-pointing
a link at an older file is seen by hash/hybrid but not by modified mode
(mtime-only), the same as for every pre-existing read API: a modified-mode
limitation, not specific to this checkpoint (follow-up F6).

Permission changes: `unreadable → readable` is detected in every mode
(tested). `readable → unreadable` (chmod 000 on a file that was read) is seen
by hash/hybrid (the content hash becomes the empty hash) but not by modified
mode, which compares mtimes and chmod changes only ctime; this is the
pre-existing behaviour of every read API (follow-up F6).

### Old vs new (approved under D-028)

| API | Old | New |
|---|---|---|
| `open_bytes(p)` | not recorded: editing the file left the page "up to date" with stale output (CP16 site_deps steps 10–11) | dependency + snapshot; traced as `read` (res = byte count) like `open()` |
| `ifstream(p)` / `s.open(p)` and all stream reads | not recorded (site_deps 12–13) | dependency at open (no trace line: stream ops stay outside trace v1) |
| `file(p).open("rw")` | snapshot computed but no `dependencies` entry, so ignored (code reading, `FileValue does not implicitly declare dependencies`) | dependency like `"r"` (now also traced as `read`) |
| `page("x").content` / `.metadata` | only `.nift/hierarchy.fingerprint` recorded: editing x's content left the reader stale | dependency on x's content file |
| `minify(file)` | read and output not recorded | input is a dependency (traced `read`), output path recorded as written (untraced) |
| `open`, `cat`, `open_bytes`, `file().open("r"/"rw")`, `ifstream` on a missing path | nothing recorded (except `sha256_file`), so a page that caught the error stayed stale after the file appeared | `absent` |
| same, existing but unreadable (and `sha256_file` EACCES) | nothing recorded | `unreadable` |
| `open`, `cat`, `file().open`: read error after a successful open | silently returned the bytes read so far (`ss << rdbuf()`) | recoverable `io.read_failed` (`<op>: input failure`), like `open_bytes` always did |

## 2. Filesystem freedom: `file().copy()` / `file().move()`

`execute_file_method` refused a destination outside the project root in page
renders (`<method>: path must stay inside the Nift project`, CP16 Q-H4,
site_contain) while `cp()`/`mv()` allowed it. The check is replaced by the
same opt-in policies `cp()`/`mv()` apply: `enforce_project_root` (an embedder
policy, off for the CLI) and `--fs-root`/`NIFT_FS_ROOT`.

| | Old | New |
|---|---|---|
| page render, destination outside the project (absolute, `../`, symlinked directory) | fatal `copy: path must stay inside the Nift project` | copied/moved with OS semantics (copy onto a symlinked file writes through it, as `cp()`) |
| standalone script with `--fs-root=R`, destination outside R | allowed (the destination was never checked) | `copy: path escapes configured filesystem root: …` (the opt-in policy now covers the destination, as for `cp()`) |

Protections for Nift-owned state are elsewhere and unchanged: the build lock
and `.nift/.unfinished` protocol (`ProjectOwnership`), owned-output removal
only through `filesystem::remove_owned_file` for tracked outputs, `nift rm`/
untrack never touching non-owned files, page-info/failed-list atomic writes,
custom-build output checks (`path_within(root, output)`), build scripts must
be project-local `.f` files, and package-store containment (`must stay inside
the owning package`). None of these used the removed check. `@path`/`@pathto`
keep their containment: they compute site URLs, not filesystem accesses.

## 2b. Build writes respect output ownership (owner decision on review D6)

During a website build (page renders and build scripts, including their
async/thread workers), every script write API checks its destination
**immediately before the write** (`Parser::guard_build_write`):

| Destination | Result |
|---|---|
| inside the project's `.nift/` | refused: `<op>: refusing to write Nift build metadata during a build: <path>` |
| an output owned by **another** tracked page (its output file or a pagination output `…-N.html`/`N.html`, N ≥ 2) | refused: `<op>: refusing to overwrite` (or `remove`) `the build output of tracked page '<name>' during a build: <path>` |
| the current page's own outputs | allowed (tracking stays correct: the build writes them afterwards) |
| unowned files, including inside `public/` | allowed (existing output-management rules) |
| outside the project | allowed (OS permissions, optional `--fs-root`) |

Both errors are fatal (`policy.restricted_operation`, not catchable), so the
page fails loudly (D-020: last-good output kept) and nothing is rebuilt
automatically. Ownership is Nift's existing notion: the tracked pages'
output paths (`ProjectInfo::output_owner`, built lazily next to the existing
`is_tracked_output` index); no new declaration mechanism. Resolution uses
filesystem semantics: the canonical parent directory plus the final name
(what an unlink or rename replaces) and, for writes, also the fully resolved
path (a symlink to another page's output, inside or outside the project), so
`..`, symlinked directories, absolute and alternative spellings are caught;
owner outputs are canonicalised the same way (symlinked output directories,
D-026). Guarded APIs: `cp`/`mv` targets and move sources, `rm`, `touch`,
`mkdir`, `file().copy/move/remove` (and the move source), `file().save()`
(the write of `file().open("w"/"a"/"rw")`), `ofstream`/output `s.open()`,
`minify` output, and `cmd().stdout()/.stderr()` redirect targets. Standalone
scripts own no pages and are unaffected. Race: see F11.

Old vs new: before, `file("x").copy("public/other.html")`, `cp`, `rm`, `mv`,
`save`, `ofstream` or a `cmd().stdout()` redirect silently replaced or
removed another page's output and `nift status` still reported that page up
to date; a copy onto `.nift/tracked.json` broke the project. Now each is
refused with the owning page named (`tests/runtime2/build_write_ownership.sh`,
which fails on the base binary).

## 3. Bounded whole reads (`limit.exceeded`)

### Setting and default

`NIFT_READ_LIMIT=<bytes>[B|K|KB|KiB|M|MB|MiB|G|GB|GiB|T|TB|TiB]`
(case-insensitive; every suffix is **binary**, so `1kb` = 1024; ≥ 1 byte).
**Default 1 GiB (1073741824 bytes).** Owner (D-030): `NIFT_READ_LIMIT` (a
bound on materialising one whole read or capture) and the VM heap budget
(total VM-managed memory, `NIFT_VM_HEAP_BUDGET`) are **independent settings**
that both default to 1 GiB for now; neither tracks the other, and raising one
does not raise the other. The same default means a whole read that fits the
read limit is not, by itself, larger than the VM budget, and every whole read
that works today on realistic inputs keeps working.

The limit is **per read, not per process**: each operation's buffer is
bounded separately, and the value it produces is then ordinary program data
(assigning it, concatenating it or returning it may copy it). A whole read
peaks at about 2 × the file size of address space for sources of unknown size
(growth step: the replaced buffer ≤ limit/2 plus the new buffer ≤ limit) and
at about 1 × for regular files (pre-sized). With async/thread workers reading
concurrently the worst case is about (workers + 1) × 2 × limit. It follows the existing convention
for runtime limits (`NIFT_VM_HEAP_BUDGET`, `NIFT_VM_OUTPUT_LIMIT`: environment
variables) and is read per operation. An invalid value (`0`, `-1`, `abc`,
`12Q`, overflow) is a fatal `native.invalid_argument` naming the setting at
the first bounded read, never silently replaced by the default. The value is
in `src/ReadLimit.h` (`kDefaultReadLimit`).

### What is bounded (per buffer, not cumulative)

| Operation | Buffer bounded |
|---|---|
| `open(p)`, `cat(p)`, `open_bytes(p)`, `file(p).open(m)` (all modes: `w`/`a` still read the existing file first, Q-H8), `minify(file)` | the whole file |
| stream `read_all()`, `read_all_bytes()` | the rest of the stream |
| stream `read(n)`, `read_bytes(n)` | the returned buffer: `n` may exceed the limit as long as the data returned does not |
| stream `read_line()`, `read_val()`, `>>` extraction | one line / value / token |
| `inject(p)` | the source (regular files only, rejected from `st_size`) |
| `run(...)`, `cmd(...).run()` | each captured stream (stdout, stderr) read back from the capture file |

Streaming keeps working on inputs larger than the limit: a `read_line` loop
over a file 3× the limit, 64 × `read_bytes(limit/2)` from `/dev/zero`, and
`sha256_file` of a 3× file all pass under the hard address-space limit.

### Enforcement (`src/ReadLimit.h`)

* The bound is checked **while reading**; `st_size` is only a hint (regular
  files are pre-sized exactly, and a regular file already larger than the
  limit is rejected without reading). Pipes, devices and growing files are
  handled by the read loop.
* Each read asks for at most `room + 1` bytes; the one byte that proves
  "too large" lands in a separate 64 KiB stack chunk, never in the buffer.
* The buffer never holds or reserves more than the limit. Growth doubles
  until a doubling would pass limit/2, then jumps straight to the limit, and
  every growth step reserves into a fresh buffer (libstdc++'s
  `string::reserve` would otherwise round a request up to 2× the old
  capacity). So the buffer being replaced is ≤ limit/2 and a growth step's
  transient address space is ≤ 1.5 × limit; regular files are allocated once.
* Overflow-safe arithmetic (`limit - held` only when `held < limit`; the
  setting parser rejects values that overflow 64 bits).
* Past the bound: stop reading, release the buffer (`Buffer().swap(out)`),
  close the stream/file (RAII), fail with the fatal, uncatchable
  `native.limit_exceeded` (`fail_fatal`, any pending recoverable value is
  cleared first so `try/catch` cannot see it). Message:
  `<op>: <path or what> exceeds the read limit of <N> bytes (NIFT_READ_LIMIT)`.
* Nothing is truncated: the only outcomes are the complete value or the error.

Error code: legacy's taxonomy names it `native.limit_exceeded` (fatal);
CP13's taxonomy renames it `limit.exceeded` with `details.limit`, so after
CP13 integrates this error is `limit.exceeded` with no change here. There is
no JSON diagnostics path on this base (`--format=json` is CP13); the
structured `Diagnostic` (`result_.diagnostic`, embedding API) carries the code
and message.

Build state: a page that hits the limit is an ordinary failed page (D-020):
non-zero exit, last-good output kept, no `.unfinished`, the next build after
the fix succeeds without `--repair` (tested).

### Old vs new (approved under D-028)

| Input | Old | New |
|---|---|---|
| `open("/dev/zero")`, `/dev/full`, `file("/dev/full").open("w")`, a FIFO whose writer never stops, `read_all`/`read_line` on them | grows until killed (CP16 probe: OOM-killed at 5.9 GB RSS) | fatal limit error after limit + 1 bytes |
| a regular file larger than the limit | read whole (any size that fits in RAM) | fatal limit error (default 1 GiB) |
| `run()` capture larger than the limit | read back whole | fatal limit error (`run: captured stdout of head exceeds …`) |
| everything at or below the limit | unchanged | unchanged |

### Never-EOF sources (investigation; nothing implemented here)

A byte bound does not help when a source produces **no** data: `open()` of a
FIFO with no writer blocks in `open(2)`; with a writer that never writes it
blocks in `read(2)`; `sha256_file("/dev/zero")` and a `read_line` loop over an
endless device run forever with bounded memory. Today only killing the
process stops them (Ctrl-C works in the CLI; `build --auto` and embedders have
no way to cancel).

How the CP16 Host layer should support it (design input for CP16, owner
decision on any user-visible timeout):

1. **Cancellation first, timeouts as a policy on top.** Every potentially
   blocking Host op (`read_file`, `read_chunks`, `open_stream`,
   `stream_read_*`, `spawn`, `sleep_ms`) takes the isolate's `CancelToken`
   (already in the CP16 `SpawnRequest`). A deadline is just a token that fires
   at a time, so one mechanism serves Ctrl-C, `build --auto` shutdown, an
   embedder's cancel and an optional `NIFT_READ_TIMEOUT`-style policy.
2. **POSIX mechanics.** Open non-regular files with `O_NONBLOCK` (a FIFO open
   then never blocks), clear `O_NONBLOCK` only for regular files, and read via
   `poll({fd, cancel_fd})` where `cancel_fd` is the token's self-pipe/eventfd.
   A FIFO with no writer yet polls as not ready (Linux) rather than EOF, so the
   wait is still interruptible; regular files skip `poll` (always ready, no
   cost on the hot path). `sha256_file`/stream loops check the token between
   chunks.
3. **Windows mechanics.** Named pipes/devices via overlapped I/O waiting on
   `{io_event, cancel_event}`, or `CancelIoEx` from the cancelling thread.
4. **Outcome.** A cancelled or timed-out op returns `HostStatus::Limit` (or a
   distinct `Cancelled`) and surfaces as a fatal, uncatchable error with its
   own code (`limit.timeout` / `host.cancelled`, CP13 taxonomy), naming the
   operation and the deadline; never a partial value.
5. **Default.** No default timeout (a slow NFS read or a deliberate
   `open("/dev/stdin")` must keep working); the timeout is opt-in, cancellation
   is always on.

## 4. Bounded audit of filesystem APIs (legacy, after this checkpoint)

"Deps" = recorded in a page render (D = dependency + snapshot, A = absence,
U = unreadable, P = presence/kind, L = listing fingerprint, W = recorded as
written, — = nothing). "Root" = project-root containment in CLI page renders
(opt-in `enforce_project_root`/`--fs-root` policies exist everywhere and are
not counted). "Unbounded" = can read an unbounded amount into memory.

| API | Deps (incl. absence) | Root restriction | Unbounded read into memory |
|---|---|---|---|
| `open`, `cat` | D, A, U (**A/U new**) | none | no (**bounded, new**) |
| `open_bytes` | D, A, U (**new**) | none | no (**new**) |
| `file(p)` (handle) / `.exists()` | — / P | none | — |
| `file().open("r")` / `("rw")` | D (**rw new**), A, U (**new**) | none | no (**new**; `w`/`a` read too, Q-H8) |
| file-object reads (`read`, `read_line`, …) | (covered by open) | — | in memory already bounded at open |
| `cp()`, `file().copy` | source D/A (**new, review D4**); destination W (`file().copy`: **new**, untraced F1) | none (**`file().copy`: new; was project root**) | no (OS copy) |
| `mv()`, `file().move` | source A after the move (**new**: it reappearing rebuilds; D/A if the move failed); destination W; cross-device fallback (**new, R5**) | none (**`file().move`: new**) | no |
| `file().remove`, `save`, `touch`, `mkdir`, `rm`, `ofstream` | W | none; **writes into `.nift/` refused during builds (new, interim D6)** | no |
| `ifstream`/`s.open`, stream reads | D at open, A, U (**new**) | none | no (**per call, new**) |
| `exists`, `is_file`, `is_dir`, `stat` | P/A (+ D for `stat` of a file) | none | no |
| `ls`, `ls(glob)`, `glob` | L | none | no (entry names only) |
| `sha256_file` | D (streamed), A, U (**U new**) | none | no (streamed; never-EOF devices hash forever, §3) |
| `inject` | D | none | no (**regular files, checked by `st_size` at open only, new**: a file growing between the size check and the read is read up to its size at read time; narrow window, F3 class) |
| `minify(file)` | D, A/P (**new**), output W (**new**) | none | no (**new**) |
| `page(x).content` | D on x's content file (**new**) | n/a (tracked pages) | regular file sized by `stat` (F3) |
| `page(x).metadata` | D on x's content file, its external front-matter file and every configured schema and taxonomy file (**new**, review D2) | n/a | regular file sized by `stat` (F3) |
| any read inside `async` functions / `thread()` workers | recorded in the worker and merged into the page when the render ends (**new**, review D3) | as the API | as the API |
| `read()` (stdin line, standalone only) | n/a (standalone) | — | no (**bounded, new**, review D7) |
| `run`, `cmd().run()` | — (by design, CP02) | none | no (**captures bounded while the child runs, new**, review D5) |
| command-style statements (`git status` line) | — | none | **yes**: captured then printed (F2) |
| `import`, `module_path`, `package_path` | D (`read-source`) | package store only (integrity rule, not D-002) | regular file sized by `stat` (F3) |
| template `@input`, `@markup`, `@json`, `@script` sources, templates, content, pagination | D; missing/unreadable fails the page (D-020 rebuild) | none (CP03) | regular file sized by `stat` (F3) |
| `@path`/`@pathto` | requirement | URL containment (kept: not a filesystem access) | — |
| build scripts (hooks) | D | project-local `.f` (Nift-managed config, kept) | regular file sized by `stat` (F3) |
| dependency hashing (`hash_path`, build engine) | — | — | no (**now streamed, new**) |

### Follow-ups (not fixed here)

* **F1** `file().copy/move/remove` neither trace nor record their writes
  (Q-H1); a page that copies onto a file it also reads keeps the dependency.
  Trace v2 (O2) and `note_written` for them, both engines.
* **F2** Command-style statements capture the whole output before printing;
  they should stream (D-011 `Stream` mode, CP19/CP20) rather than be bounded.
* **F3** Build-engine source reads (`read_shared_source`/`read_file_checked`:
  templates, content, `@input`, JSON, imports, hooks, `page.content`) read
  regular files only, sized by `stat`; a huge regular file is still read
  whole. They are not Host ops; bounding them is a build-engine change.
* **F4** Never-EOF sources: cancellation/timeout in the CP16 Host layer (§3).
* **F5** `file().open("w")` reads the existing file (Q-H8); with the limit it
  is now loud on devices, but `w` should truncate without reading.
* **F6** Modified mode is mtime-only: re-pointing a symlink at an older file,
  or `chmod 000` on a file that was read, is not seen (hash/hybrid see both).
  Pre-existing for every read API.
* **F7** (fixed in review round 1) `tests/runtime2/fs_freedom.sh` checked
  `--fs-root` with `nift --fs-root=R s.f`, which fails as an unknown shell
  option, so the check passed vacuously; it now uses `nift s.f --fs-root=R`
  and asserts the restriction message. `NIFT_FS_ROOT`/`--fs-root` apply to
  standalone scripts only (`run_script` enables the filesystem root only when
  not building a project).
* **F8** A `--read-limit` CLI flag (like `--fs-root`) was not added; the
  setting is environment-only like the other runtime limits.
* **F9** Streams in hash/hybrid builds hash the whole file at open and again
  at metadata time (R3); hash once (e.g. reuse the open-time snapshot when the
  file is unchanged).
* **F10** Modified mode could compare ctime, inode and size in addition to
  mtime (R1) to see renames-over and permission changes.
* **F11** Build-write ownership (implemented, §2b) is checked immediately
  before each write, with no transaction: a path that changes between the
  check and the write (a symlink swapped in by another process) is not
  covered. The check covers `ProjectInfoHost` (CLI builds); embedding hosts
  own no outputs (`RenderHost::output_owner` returns none).
* **F12** Capture limit while the child runs: on Linux the stages and their
  descendants (via `/proc/<pid>/task/*/children`, stopped then killed) are
  killed; elsewhere only the stages. A descendant that already left the
  stage's tree (re-parented to init) keeps running until CP16's per-pipeline
  process groups and cleanup (D-030 C1–C3). The capture files can overshoot
  the limit by what the stages write in one check interval (≤ 2 ms).
* **NOTE** `nift rm <name>` exits 0 when `<name>` is not tracked (e.g.
  `nift rm index`, the index page is `/`), so a typo untracks nothing
  silently; reported, not changed here.

## 4b. Review round 1 (independent review of `6aeca9b`)

| # | Finding | Resolution |
|---|---|---|
| D1 | `read_line` on a directory or `/proc/self/mem`: the filebuf's read error escaped as an uncaught C++ exception (before: catchable `read_line: input failure`) | the bounded line loop catches it, sets badbit and raises the recoverable `stream.read_failed` again; same for `read()` |
| D2 | `page(x).metadata` stale when x uses an external front-matter file | `RenderHost::page_member_sources`: content file, external front matter, schema and taxonomy files are recorded |
| D3 | reads inside `async` functions and `thread()` workers were discarded (pre-existing) | **merge option**: workers of a page render or build script record like the page (`worker_observing_`), export their dependencies/requirements/observations (after settling their own workers) into the future/thread state under its mutex, and the render merges every worker's export once when it ends (`settle_worker_observations`, which waits for the workers exactly as the destructor already did). Snapshots go through the host's mutex-protected observation map. Un-awaited futures and un-joined threads are covered too |
| D4 | the source of `cp()`/`file().copy`/`file().move` never recorded (pre-existing) | copies: source dependency + snapshot (or absence); moves: source absence after the move (a dependency on a consumed source would rebuild and fail forever), so a new source rebuilds |
| D5 | captures unbounded while the child runs (disk fill, hang on `run("yes")`) | the captures are checked between short waits (pidfd poll on Linux, ≤ 2 ms); past the limit the stages and their descendants are SIGKILLed and reaped, then the normal limit error (F12 for the limits of this) |
| D6 | no protection of `.nift/` or other pages' outputs (pre-existing) | owner decision: ownership-based refusal (§2b): `.nift/` and other pages' outputs refused, own outputs/unowned/outside allowed, spelling-independent |
| D7 | `read()` (stdin line) unbounded | bounded line reader |
| R1–R3 | modified-mode rename-over; per-read limit; double hash | documented (§1, §3), follow-ups F9, F10 |
| R4 | `read_limit.py` could not tell `limit` from `2 × limit` | limit 64 MiB, slack 8 MiB |
| R5 | cross-device `move`/`mv()` failed with EXDEV | `filesystem::move_path`: copy to a temporary sibling (permissions and mtime kept), rename over the destination, remove the source; directories/symlinks keep EXDEV. Owner semantics: a failed copy leaves the original intact (temporary removed, `io.move_failed`); a source that cannot be removed after a complete copy is a recoverable `io.move_failed` naming both paths (`move: copied to <dest> but could not remove the source <src>: …`), never success |
| tests | vacuous untrack check, vacuous `B:` check, F7, `file().copy("dir")` | fixed; `file().copy/move` into an existing directory keep the file name, as `cp()`/`mv()` |

## 5. Tests

**Resource limits (owner standing rule).** Every test added by this
checkpoint runs nift inside hard limits even where the implementation
enforces its own: disk (`RLIMIT_FSIZE`/`ulimit -f`), memory
(`RLIMIT_AS`/`ulimit -v`), lifetime (`timeout`), and the capture cases check
for leftover child processes and zombies.


| Test | What it proves | Base `59e9fa5` | This branch |
|---|---|---|---|
| `tests/runtime2/host_deps.py` | incremental == clean build after each edit (open, open_bytes, file().open r/rw, ifstream line/all/extraction, external, parent traversal, symlink targets, missing files appearing/removed, unreadable → readable, `page(x).content/.metadata` incl. external front matter, reads in `async` functions and `thread()` workers, `cp()`/`file().copy()` sources) in modified, hash and hybrid, "up to date" afterwards; an edit while the page is part-way through a stream; `file().move`/`mv()` sources reappearing | 84 FAIL, 114 PASS | 198 PASS |
| `tests/runtime2/file_copy_move_freedom.sh` | external/absolute/parent/symlinked `file().copy/move` in a page, copy into an existing directory, `--fs-root` covers the destination, cross-device `file().move`/`mv()` to `/dev/shm`, `.nift/` writes refused, real untracking leaves external files | 23 FAIL, 2 PASS | all PASS |
| `tests/runtime2/build_write_ownership.sh` | §2b: 15 ways to overwrite/remove another page's output (copy, cp via symlinked dir, `..`, absolute, outside symlink, rm, file().remove, mv/move away and onto, save, ofstream, touch, cmd().stdout) refused naming the owner; `.nift/` refused via symlink; own output, unowned `public/` file, outside path allowed; standalone unaffected; R5 partial-failure semantics (copy failure keeps the original, undeletable source reported) | 25 FAIL, 4 PASS (the 4 do not discriminate) | all PASS |
| `tests/runtime2/read_limit.py` | boundaries (files, lines, FIFO, stdin, capture), devices, FIFOs, growing file, every read API, streaming beyond the limit, read_line I/O errors catchable (directory, `/proc/self/mem` EIO), `read()` bounded, captures stopped while the child runs (`yes`, `head -c 150000000`, pipeline, descendant of `sh -c`, stderr) with no leftover processes or zombies and the marker never created, setting parsing, default 1 GiB, page-build state; limit 64 MiB, RLIMIT_AS = baseline + 1.5 × limit + 8 MiB, peak RSS ≤ baseline + limit + 8 MiB | 52 FAIL, 63 PASS | 115 PASS |
| `tests/read_limit_unit.cpp` | parser, boundary, capacity ≤ limit, growth never replaces a buffer above limit/2, endless source pulls ≤ limit + 1 + one block, max-bytes reads, stream errors; `-O2` and ASan/UBSan | n/a | pass |

Make target: `make test-runtime2-host-deps` (unit + the four suites; part of `make test`).

### Results after review round 1 (binary built from the round-1 commit)

| Suite | Result |
|---|---|
| `make test-runtime2-host-deps` | pass (364 PASS lines) |
| `make test-runtime2-fs-freedom` (with the F7 fix) | pass |
| `make test-runtime2-dynamic-reads` | pass |
| `make test-contracts` | pass |
| `make test-site-dx` | pass |
| `make test-conformance` | 9/9 |
| `make test-test-integrity`, `make test-runtime2-certification-guard` | pass |
| `make test-build-recovery` | `build_recovery.py` 986 checks PASS (the known hybrid no-op flake did not occur in this run); `build_recovery_random.py` PASS |
| `read_limit_unit` under ASan/UBSan | pass |
| reviewer probes `exp1`–`exp5` (harness pointed at this worktree, under `ulimit -f/-v`) | `exp2`, `exp4`: pass. `exp1`: all hash/hybrid steps pass, including async; 5 modified-mode failures, all documented modified-mode limits (same-mtime edits, symlink re-pointed at an older file; R1). `exp3`: external front matter passes in all modes; rename-over with an older mtime fails in modified mode only (R1). `exp5`: the probe's own page fails to build on the base binary too (`unknown value or malformed expression: project` inside the `while` condition), so it tests nothing here |

CP16 fixtures (`capture.py --check`, 30 probes, scratch copy): base binary
30 OK. This binary, against the original expected files: 6 intended DIFFs:
`site_deps` (`open_bytes`/`ifstream` now dependencies; steps 10–13 detect
the edits), `site_contain` (`file().copy` outside the project succeeds, and
its source `data/p.txt` is now a dependency), `fs_read` (`open_bytes` traced
`read`), `fs_write` and `fs_eacces` (`open("rw")` traced `read`). Re-captured
(`--write --runs 2`, stable across directories), then 30/30 OK. Patch for the
CP16 branch: `docs/runtime-2/evidence/feature-host-deps/cp16-fixtures-recaptured.patch`.

Measured peak RSS in the exceeding cases (limit 64 MiB): 11 MiB (rejected
from `st_size`, capture stopped early) to 70 MiB (endless stdin read to the
limit) against a baseline VmHWM of 6 MiB, under RLIMIT_AS 114 MiB.

Not run: `make certify` (owner), sanitizer builds of `nift`, the regression
suite, macOS/Windows, a mutant build of `nift` for R4 (the unit test's
mutants cover the growth rule).

## 6. Note for CP16

CP16's Host layer implements these corrected semantics, not the pre-D-028
legacy ones:

* recording table (§2.5 of the CP16 design): `open_bytes` → `streamed_read`
  (traced `read`); `ifstream`/stream open → dependency at open with a
  streamed snapshot (untraced); `file().open("rw")` → `read`; failed reads →
  `absent` or `unreadable` via the shared `observe_failed_read`;
  `page(x).content/.metadata` → dependency; `minify(file)` → `read` + written.
  The W24 rule ("VM reproduces gaps until O1") is void: O1 is decided (fix).
* `file().copy/move` have no containment; only the opt-in policies in
  `Host::resolve` apply (§3 "Freedom": the "one remaining legacy containment
  check" no longer exists).
* §2.7 limits: whole reads, stream lines/tokens/reads and process captures
  use `NIFT_READ_LIMIT` (default 1 GiB) with `src/ReadLimit.h`'s semantics
  (per buffer, enforced while reading, ≤ limit held, fatal
  `limit.exceeded`). The VM charges the same bytes to its heap budget; the
  read limit and the heap budget are separate settings with equal defaults.
  The `Host::read_file(…, limit, …)` signature already carries it.
* Fixtures to re-capture against this binary (intended differences, §5
  RESULTS): `site_deps` (steps 10–13 now detect `open_bytes`/`ifstream`),
  `site_contain` (copy/move succeed), `fs_read` (`open_bytes` traced),
  and any probe whose trace includes `file().open("rw")`.
* Never-EOF sources: implement cancellation per §3 (F4).
