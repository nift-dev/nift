# CP16 — Host operations on the VM (design and test plan)

Status: **design only** (`runtime-2-cp16-design`, local, not pushed). Base:
`runtime-2` @`b411f3f`. No source file changes; implementation starts after
CP14/CP15 integrate (D-027 order: CP14 → CP13 → CP15). Checkpoint row
(`../checkpoints.md`): "Built-ins batch C: filesystem and process via Host
ops; pipe-based capture/streaming with explicit ownership (D-011) — gate: as
CP14; Host-op trace parity; pipe mode ≥ competitive with Bash on the D-011
benchmark set; no hangs/leaks".

This record covers, in order: the inventory of legacy host behaviour (§1),
the Host-op interface natives call (§2), filesystem (§3), process (§4) and
clock (§5) semantics, the compiler wiring rules W12–W24 (§6), the test plan
(§7), benchmark readiness (§8), and risks, quirks and open decisions (§9).
Fixtures captured from legacy are in `tests/runtime2/cp16/` (§7.1).

Everything stated as legacy behaviour below is either pinned by a fixture
(probe name in brackets) or marked **(code reading)**. Line references are to
`src/` at `b411f3f`.

---

## 0. Findings that affect planning (read first)

1. **The legacy oracle binary named in the brief is stale.**
   `/home/claude/review/nift/nift` (mtime 2026-10-18 17:27, sha256
   `1ca8c3d3…`) predates feature-site-dx: `run("missing")` gives
   `launched:true` and pipelines have no `stages`. The fixtures were captured
   with `/home/claude/review/nift-serve/nift` (2026-10-19 07:29): the
   `nift-serve` worktree is clean at `b411f3f`, every source file is older than
   the binary, and `git diff 5022d35 b411f3f -- src` is empty, so it is built
   from `b411f3f`'s sources (not proven by a rebuild: builds were not allowed).
   CP14/CP15 evidence names the stale binary as their oracle; their natives are
   unaffected by site-dx as far as I can tell, but the coordinator should
   rebuild the oracle before CP16 differential runs.
2. **Legacy dependency-recording gaps** (correctness, D-002): `open_bytes()` and
   `ifstream` reads are not recorded; editing such a file leaves the page "up
   to date" with stale output [site_deps steps 10–13]. Needs a decision before
   the VM reproduces it (§9, O1).
3. **A D-002 containment remnant**: `file().copy()`/`.move()` still refuse
   paths outside the project in page renders ("path must stay inside the Nift
   project"), while `cp()`/`mv()` allow them [site_contain] (§9, O1).
4. **Unbounded reads**: `file("/dev/full").open("w")` reads the device's
   existing "content" before truncating and was OOM-killed at 5.9 GB RSS
   during probing (memcg-limited to this task's shell; no other job was
   affected). Same class: `open`/`cat` of `/dev/zero` or a FIFO (code reading).
   (§9, O3.)
5. Trace coverage is narrower than `trace-format.md` implies: `open_bytes`,
   stream I/O, `inject`, `file().copy/move/remove`, `cd`, `setenv`/`unsetenv`
   emit no trace line [fs_read, fs_write]. Writes are traced *before* the
   attempt with an empty result, so a failed write looks like a success in the
   trace [fs_errors, fs_eacces].

---

## 1. Inventory of host-touching built-ins and statements

Columns: legacy semantics; errors (R = recoverable/catchable with code, F =
fatal, message prefix is the built-in name); trace v1 (what `NIFT_TRACE_HOST`
emits, *when*); dependency recording in a page render (standalone scripts
record nothing: `standalone_script_host_`); CP15 deferral covered.

Path resolution for every path argument (`resolve_path`,
`ParserExpression.cpp` ~2519): `~`/`~/` expands to `HOME` (`USERPROFILE` on
Windows) **only in standalone scripts**; relative paths join `cwd`
(standalone) or the project root (page); then `fs::absolute(...).
lexically_normal()` — `..` is removed lexically before the OS sees it
[fs_lexical_dotdot]. Containment checks run only when
`enforce_project_root` (opt-in, CP03) or `--fs-root` is set.

### 1.1 Filesystem (FS)

| # | Built-in | Semantics | Errors | Trace v1 | Recorded (page) | CP15 deferral |
|---|---|---|---|---|---|---|
| FS-01 | `open(p)` | whole file as string | dir: F `open: path is a directory`; R `io.open_failed` `open: cannot open path` (ENOENT and EACCES alike) | `read` after success, res = byte count; nothing on failure | dependency + content hash | — |
| FS-02 | `open_bytes(p)` | whole file as bytes, 64 KiB chunks | dir F; R `io.open_failed`; R `io.read_failed` `open_bytes: input failure` | **none** | **none** (gap, Q-H2) | host bytes |
| FS-03 | `cat(p)` | writes file to the process stdout (also inside a page render: to the terminal, not the page) [site_deps step 1] | as FS-01 with `cat:` | `read` after success | dependency + hash | — |
| FS-04 | `sha256_file(p)` | streamed SHA-256 hex | R `io.open_failed`, R `io.read_failed`, plain error for a directory (feature-sha256) | `read` after success, res = size | dependency + streamed hash | host bytes |
| FS-05 | `file(p)` | FileValue handle (`\x1fnift:file:N`), no I/O | `file: expected string path` F | none | none | — |
| FS-06 | `f.open([mode])` `r w a rw` | reads the whole existing file into a working buffer for **every** mode (w keeps it as `saved`) | F already open / bad mode / directory; R `io.open_failed` `open: file does not exist` (r, rw), `open: cannot read path` | `open` res = mode, **before** the existence check | `r`: dependency + hash; `rw`: hash only, no dependency entry (code reading, line 989) | — |
| FS-07 | `f.read([n]) read_all read_line eof tell seek modified` | in-memory cursor ops; `read_line` strips one trailing `\r` | F arity/mode messages | none | none | — |
| FS-08 | `f.read_bytes(n) read_all_bytes` | bytes from the buffer | F | none | none | host bytes |
| FS-09 | `ifstream(p)`, `s.read_line() read_bytes read_all_bytes read_val`, `close(s)` | std::ifstream; `read_line` keeps `\r` [fs_read] | R `stream.open_failed`, `stream.read_failed` | **none** | **none** (gap) | host bytes, read_val |
| FS-10 | `exists(p)` | follows symlinks; dangling = false | R `io.metadata_failed` `exists: <strerror>` | `stat` res `file directory other missing error` | exists → `reqs` + `kinds`; missing → `absent` | — |
| FS-11 | `is_file(p)`, `is_dir(p)` | as FS-10 | as FS-10 | `stat` | as FS-10 | — |
| FS-12 | `stat(p)` | `{exists, type, size?}` | R `io.metadata_failed` | `stat` | as FS-10, plus dependency + `hash_path` for files | — |
| FS-13 | `f.exists()` | `inspect_path` | never errors (error → false) | `stat` | as FS-10 | — |
| FS-14 | `ls()`, `ls(dir)`, `ls(glob)` | names sorted bytewise; dot files listed for a directory, excluded by `*`; glob display paths relative to cwd/root, absolute for an absolute pattern [fs_list] | R `io.directory_read_failed` `ls: <strerror>`; F `ls: path is not a readable directory` | `list`/`glob`, **before** the attempt | `listings[dir:…/glob:…]` fingerprint | — |
| FS-15 | `pwd()` | cwd (standalone) or project root | — | none | none | — |
| FS-16 | `cd(p)` | standalone only, changes process cwd | F in pages; R `io.change_directory_failed` `cd: <strerror>` | **none** | n/a | — |
| FS-17 | `inject(p)` | path joins `host_.root()` (cwd in scripts); a quoted literal argument is used without evaluation; strict JSON fast path, else the file is evaluated as a Nift expression in a new scope (`$[...]` in a JSON string interpolates) [json_host] | **F** `inject: source is not readable` (missing, unlike FS-01) [fs_fatal_inject_missing]; F cycle `inject: inject: source cycle through …`; F parse errors of the fallback [json_host_bad] | **none** | dependency + hash (also recorded into `result_.dependencies` in scripts, harmless) | inject |
| FS-18 | `f.read_val()`, `f.write_val(v)`, stream `read_val`/`write_val` | token scan then **Nift expression evaluation** of the token (`readval-1` is `readval - 1`); write_val = compact JSON + `\n` | F `read_val: …` | none | none beyond FS-06 | read_val, write_val |
| FS-19 | `touch(p)` | `ofstream(p, app)` | R `io.create_failed` `touch: cannot open path` | `touch` before | path → `written` (removed from observations) | — |
| FS-20 | `f.write write_line write_val replace replace_once insert insert_before insert_after prepend append revert flush`, `f.close()` | buffer edits; `close` with unsaved changes F; end of run with an unsaved open file: F `managed file left open: <path> (unsaved changes discarded)` [fs_eacces, fs_fatal_close_dirty] | F messages per method | none | none | — |
| FS-21 | `f.save()` | **atomic**: temp sibling `<path>.nift-tmp-<steady_clock>` + `replace_file_atomic`; copies permissions; **replaces a symlink with a regular file** [fs_symlink] | R `io.write_failed` `save: parent directory does not exist` / `cannot create temporary file` / `temporary write failed`; R `io.atomic_replace_failed` | `save` **before** anything, even when not dirty | `written` | — |
| FS-22 | `mkdir(p)`/`make_dir(p)` | `create_directories`; existing dir ok | R `io.create_failed` `make_dir: <strerror>` | `mkdir` before | `written` | — |
| FS-23 | `cp(...)`/`copy(src…, dst)` (≥2 args), `mv`/`move` | sources: strings, arrays, globs; dst directory for several; `copy_file(overwrite_existing)` writes **through** a symlinked destination; `rename` for move; `copy(v)` with one argument is the CP15 value copy [copy_overload] | R `io.copy_failed`/`io.move_failed` `<op>: <strerror>`; F `glob matched no source files`; F `destination must be an existing directory for multiple sources` | per source: `copy-to`/`move-to` (+`move-from`) before the attempt | `written` | — |
| FS-24 | `f.copy(dst)`, `f.move(dst)`, `f.remove()` | file must be closed; returns a new FileValue | **F `<m>: path must stay inside the Nift project` in page renders** (Q-H4); R `io.copy_failed` etc. | **none** | none | — |
| FS-25 | `rm(p…)`/`remove(p…)` | strings/arrays/globs; missing path: silent success; directory: F | R `io.remove_failed` | `remove` before | `written` | — |
| FS-26 | `ofstream(p)`, `o.write write_line write_bytes write_val flush`, `close(o)` | truncating std::ofstream; buffered: ENOSPC appears at flush/close [fs_enospc] | R `stream.open_failed` / `stream.write_failed` / `stream.flush_failed` `flush: output failure` / `stream.close_failed` `close: output flush failed` | **none** | none | host bytes (`write_bytes`) |
| FS-27 | `which(n)` | `nift:<n>` for 16 built-in names, else PATH search (POSIX: regular + X_OK; Windows: PATHEXT order) | — | none | none (by design) | — |
| FS-28 | `module_path`, `package_path`, `import` | resource resolution / module source reads | (CP12 owns imports) | `read-source` | dependency | — |

### 1.2 Process (PR)

| # | Built-in | Semantics | Errors | Trace v1 | Recorded |
|---|---|---|---|---|---|
| PR-01 | `run(prog, args…)` | argv only, never a shell; array arguments flattened; numbers/bools rendered (`2.5`, `true`); stdin inherited [proc_stdin]; non-CLOEXEC descriptors inherited (code reading + probe); capture through anonymous regular files (CP07); background descendants are not waited for [proc_run]; result `{exit_code, stdout, stderr, launched[, error]}` | F `run: external process execution disabled` (`NIFT_NO_PROCESS`) [proc_disabled]; F `run: arguments must be scalar`; launch failures are **values**: `launched:false`, 127 `executable not found: x`, 126 `permission denied: x`, `interpreter not found: /i (from x)`; signals 128+n | `spawn` per stage **before** launch: `arg` = program and args joined by single spaces (not absolute), `res` = cwd **as spelled** (`rel`, not absolutised) or empty | nothing (CP02: by design) |
| PR-02 | `cmd(…)`, `.pipe(cmd)`, `.cwd(d)`, `.env(k,v)`, `.stdin(p)`, `.stdout(p[, "append"])`, `.stderr(p[, "append"])`, `.run()` | builder handle `\x1fnift:cmd:N`; `.stdin` applies to the first stage and `.stdout`/`.stderr` to the **last stage at call time**; any mode other than `"append"` truncates; stderr of all stages goes to one capture; `stages: [{launched, exit_code[, error]}]` only for >1 stage; top-level `exit_code` = last stage; setup failures in the child (chdir, redirections, dup2) are `launched:false`, 126 [proc_cmd] | as PR-01 | as PR-01 | nothing |
| PR-03 | `which` | FS-27 | | | |
| PR-04 | command-style statements (`git status` on a script line; `@__nift_cmd`) | argv split on spaces, no shell (`ls \| wc` passes `\|` as an argument), stdout/stderr inherited and ordered with `print`; a missing program exits 127 **silently** and the script continues [proc_command_style] | — | `spawn` | nothing |
| PR-05 | interactive shell job control (`JobControl.cpp`) | foreground process groups | — | `spawn` res `job` | — |
| PR-06 | `build build_all build_names build_repair track untrack status tracked project_root` | in-process project automation, standalone only | F outside scripts | via the build engine | — |

### 1.3 Environment (EN), clock (TI), random (RN), other

| # | Built-in | Semantics | Errors | Trace v1 | Recorded | CP15 |
|---|---|---|---|---|---|---|
| EN-01 | `getenv(k)` | value or null; empty string ≠ unset [env_ops] | F `getenv: <host error>` (no trace on that path) | `getenv` `found`/`unset` | `environment[k]` (except `NIFT_HOOK_*`) | — |
| EN-02 | `env()` | object snapshot (no `NIFT_TRACE_HOST*`) | F host without enumeration | `env-all` | `environment-snapshot` fingerprint (volatile names excluded) | — |
| EN-03 | `setenv(k,v)`, `unsetenv(k)` | standalone only; process-global, children see it; `note_environment_changed` | F in pages; `setenv: failed` | **none** | n/a | — |
| EN-04 | `os()`, `arch()` | compile-time strings | — | none | none | — |
| EN-05 | `platform()` | host platform (`--platform`) | — | none | `platform` | — |
| EN-06 | `hardware_concurrency()` | ≥ 1 | — | none | none | — |
| EN-07 | `@getenv(k)` (template) | as EN-01 | render failure on host error | `getenv` incl. `error` | as EN-01 | — |
| TI-01 | `epoch()` | Unix ms, `CLOCK_REALTIME` / `GetSystemTimeAsFileTime`, exact int | F `epoch: cannot read the system clock`, range errors | none | none | epoch |
| TI-02 | `sleep(ms)` | `this_thread::sleep_for`; uninterruptible | **F** `sleep: milliseconds must be a non-negative signed 64-bit integer` [time_fatal_sleep] | none | none | sleep |
| TI-03 | `timer()`; `start stop pause resume reset elapsed running paused` | steady clock (injectable `timer_clock_` in C++ only); `start` restarts from 0; `pause` freezes, `resume` continues, `stop` freezes and `resume` after `stop` does nothing [time_ops]; handles are per parser (`handle belongs to a different parser`), not transferable to threads | F `<m>: expected no arguments`, `timer: invalid handle` | none | none | timer + methods |
| TI-04 | template `$[build-time build-date build-UTC-time build-UTC-date build-YYYY build-YY build-timezone build-OS]` | `Parser::metadata`: **each variable reads the clock separately**; local time from TZ; `build-OS` compile-time | — | none | none (a page using them is not rebuilt as time passes) [site_buildvars] | build-time variables |
| RN-01 | `secure_random_bytes(n)` | getrandom / BCryptGenRandom; ≤ 10,000,000 | **F** `result exceeds 10000000 bytes`, `byte count must be a non-negative integer`, `operating-system random source failed` | none | none | secure_random_bytes |
| OT-01 | template directives `@input @json @markup @script @dep @paginate @path/@pathto` and template/content reads | build engine reads (`read-source`, `read-json`) | render failures | `read-source`/`read-json` | dependencies/requirements | `@json` |
| OT-02 | `ffi_*`, `thread`, `async`/`await`, `mutex`, `atomic<…>` | not in CP16 (FFI later; concurrency CP22/CP23) | | | | — |
| OT-03 | `print`, `err`, `warn` | process stdout/stderr (already VM output, CP11) | | none | none | — |
| OT-04 | `minify` | pure | | | | — |

No network built-in exists in legacy (only `nift serve`, a CLI command).

### 1.4 CP15 deferrals → where they land

| CP15 deferral | Rows | CP16 natives |
|---|---|---|
| time: `epoch`, `sleep`, `timer` + methods | TI-01..03 | `time_methods.cpp` over `Host::clock()` |
| build-time template variables | TI-04 | `Host::wall_time()` provided now; wired with the template compiler (CP24) |
| host bytes: file read/write, `secure_random_bytes`, `sha256_file` | FS-02, FS-04, FS-08, FS-09, FS-26, RN-01 | `fs_methods.cpp`, `stream_methods.cpp`, `random_methods.cpp` |
| `inject`, `@json`, `read_val`, `write_val` | FS-17, FS-18, OT-01 | `write_val` uses CP15 F6 serialization; `inject` and `read_val` are refused (W20, W21: dynamic evaluation, risk R3) — the deferral is only partly closed; `@json` with CP24 |

---

## 2. One Host-op interface

The VM never calls the OS. Natives receive a `Host&` through their
`Context`; every effect goes through one choke point that resolves paths,
performs the operation, classifies the result, emits the trace and records
observations — in that fixed order per operation class, so trace and
recording parity follow from construction rather than from per-native care.

### 2.1 Files (new, all under `src/rt/host/`)

| File | Content |
|---|---|
| `host.h` | `Host` abstract interface, `HostOp`, `HostStatus`, `HostError`, request/result structs |
| `host_posix.cpp`, `host_win.cpp` | OS implementations (reuse `src/Process.cpp`, `src/FileSystem.cpp`, `src/Sha256.cpp`, `nift_unix_epoch_milliseconds`, `nift_secure_random_bytes` — moved behind functions shared with legacy, not copied) |
| `host_call.h` | `HostCall` RAII helper: resolve → trace (pre/post per op table §2.4) → record |
| `observer.h`, `observer.cpp` | `Observer` interface for dependency recording; `PageObserver` adapter over the shared observation code (§2.5) |
| `host_fault.h`, `host_fault.cpp` | `FaultHost` decorator for tests (errno injection, short I/O, EINTR); compiled only into test binaries |
| `handles.h` | per-run `HostHandles` (registries for File, Stream, Command, Timer; §2.3) |
| `../natives/{fs,stream,process,env,time,random}_methods.{h,cpp}` | the natives, registered in CP14's `builtin_list.inc` |

### 2.2 Interface

```cpp
namespace nift::rt::host {

enum class HostOp : std::uint8_t {   // trace v1 names in comments
  Stat, List, Glob, Read, Open, Save, Touch, Remove, Mkdir, CopyTo, MoveTo, MoveFrom,  // "stat" ... "move-from"
  ReadSource, ReadJson, Getenv, EnvAll, Spawn,                                          // "read-source" ... "spawn"
  // not traced in v1 (trace-format.md "Not traced"; §2.4 v2 proposal):
  ReadUntraced, StreamOpen, StreamWrite, Chdir, Setenv, Unsetenv, Which, Clock, Sleep, Random };

enum class HostStatus : std::uint8_t { Ok, Recoverable, Fatal, Limit, Internal };

struct HostError {
  std::string_view code;      // "io.open_failed", "stream.flush_failed", "limit.exceeded", "host.service_failed", ...
  std::string message;        // legacy's exact text, built-in name prefix included
  int sys_error = 0;          // errno / GetLastError, for tests and JSON details only
};

struct Resolved {             // one resolution per call, reused by trace, op and recording
  std::string spelled;        // argument as given
  std::filesystem::path path; // legacy resolve_path(): ~ (scripts), base join, absolute, lexically_normal
  bool glob = false;
};

struct StatInfo { bool exists = false; bool error = false; std::string type; std::uint64_t size = 0; std::string error_message; };

class Host {
public:
  virtual ~Host() = default;
  // context
  virtual bool page_context() const = 0;                       // false = standalone script (no recording)
  virtual Resolved resolve(std::string_view raw, std::string_view builtin, HostError&) = 0; // + containment policy
  virtual std::filesystem::path base() const = 0;              // cwd (script) or project root (page)
  // filesystem
  virtual StatInfo stat(const Resolved&) = 0;                  // follows symlinks (inspect_path_unobserved)
  virtual HostStatus read_file(const Resolved&, std::string& out, std::uint64_t limit, HostError&) = 0;
  virtual HostStatus read_chunks(const Resolved&, ChunkSink, HostError&) = 0;   // sha256_file, open_bytes
  virtual HostStatus list_dir(const Resolved&, std::vector<std::string>& names, HostError&) = 0;
  virtual HostStatus glob(const Resolved&, GlobDisplay, std::vector<std::string>& out, HostError&) = 0;
  virtual HostStatus replace_atomic(const Resolved&, std::string_view bytes, HostError&) = 0; // FS-21
  virtual HostStatus append_create(const Resolved&, HostError&) = 0;                          // touch
  virtual HostStatus make_dirs(const Resolved&, HostError&) = 0;
  virtual HostStatus copy_file(const Resolved& from, const Resolved& to, HostError&) = 0;
  virtual HostStatus rename(const Resolved& from, const Resolved& to, HostError&) = 0;
  virtual HostStatus remove(const Resolved&, HostError&) = 0;
  virtual HostStatus chdir(const Resolved&, HostError&) = 0;
  virtual HostStatus open_stream(const Resolved&, StreamMode, StreamId&, HostError&) = 0;
  virtual HostStatus stream_write(StreamId, std::string_view, HostError&) = 0;  // buffered like std::ofstream
  virtual HostStatus stream_read_line(StreamId, std::string&, bool& eof, HostError&) = 0;
  virtual HostStatus stream_close(StreamId, HostError&) = 0;
  // environment
  virtual HostStatus getenv(std::string_view name, std::optional<std::string>&, HostError&) = 0;
  virtual HostStatus env_snapshot(std::vector<std::pair<std::string,std::string>>&, HostError&) = 0;
  virtual HostStatus setenv(std::string_view, std::string_view, HostError&) = 0;
  virtual HostStatus unsetenv(std::string_view, HostError&) = 0;
  virtual std::string platform() const = 0;
  virtual std::optional<std::string> which(std::string_view) = 0;
  // process (§4)
  virtual HostStatus spawn(const SpawnRequest&, SpawnResult&, HostError&) = 0;
  // clock and random (§5)
  virtual HostStatus wall_ms(std::int64_t&, HostError&) = 0;
  virtual std::chrono::steady_clock::time_point monotonic() = 0;
  virtual HostStatus sleep_ms(std::int64_t, HostError&) = 0;
  virtual HostStatus random_bytes(std::size_t, std::string& out, HostError&) = 0;
};

} // namespace nift::rt::host
```

Native side (extends CP14's `Context` and CP15's statuses, no new status
kinds in the VM):

```cpp
struct Context { std::size_t heap_limit; host::Host* host; host::HostHandles* handles; bool over_budget() const; };
// HostStatus → natives Status:  Ok→Ok, Recoverable→Recoverable (code via recoverable_code/HostError),
//                                Fatal→Error, Limit→Limit, Internal→Internal
```

### 2.3 Typed handles (CP09 registries with generation checks)

`HostHandles` owns one `rt::HandleRegistry` per kind for the run of one
compilation unit (one isolate). CP09's `HandleKind` has `File`, `Process`,
`Timer`; CP16 adds **`Stream`** and **`Command`** (a `value.h` enum change —
coordinate with CP17, which owns handles end to end). Mapping:

| Legacy string | VM handle | Resource |
|---|---|---|
| `\x1fnift:file:N` | `HandleKind::File` | `FileState` (path, mode, working/saved buffers, cursor, dirty, existed_at_open) |
| `\x1fnift:stream:N` | `HandleKind::Stream` | `StreamId` in the Host |
| `\x1fnift:cmd:N` | `HandleKind::Command` | `std::vector<SpawnStage>` builder |
| timer value | `HandleKind::Timer` | `TimerState` |

Rules: a stale, foreign or released handle never resolves (`resolve` returns
null); because the compiler admits handle values only in the positions of W13
(§6), a failed resolve is `Status::Internal` (exit 70, "never guessed"), not a
user error. Legacy never releases file/cmd/timer instances during a run; the
VM releases them at the end of the unit, after the **end-of-run check**: any
open FileState with unsaved changes produces legacy's fatal `managed file
left open: <path> (unsaved changes discarded)` (exit 1) after normal program
completion [fs_eacces]. Closing a stream releases its Host stream but not the
handle slot (legacy: `close(s)` twice → `close: …` message; code reading,
`stream_close`).

### 2.4 Effect tracing (rt-diff Host-op trace v1 parity)

The VM emits through the same `nift::trace::emit` (`src/HostTrace.h`), so
descriptor handling, context labels (`script`, page name,
`<owner>/async#N`) and JSON escaping are shared, not reimplemented. Trace
spelling of a path is `trace_path()` (absolute, lexically normal, generic) —
moved from `ParserObservation.cpp` into a shared function.

**Emission rules, v1 (exactly legacy; pinned by the fixtures):**

| Class | Ops | When | `res` |
|---|---|---|---|
| metadata | `stat` | after the query | `file directory other missing error` (symlinks followed, so `symlink` never appears in practice) |
| listing | `list`, `glob` | **before** the attempt (also when it then fails) | empty |
| reads | `read` | **after success only** (failed reads emit nothing) | byte count |
| open | `open` (file objects) | before existence/type checks | mode |
| writes | `save touch remove mkdir copy-to move-to move-from` | **before** the attempt; `save` even when not dirty; `move-to` precedes `move-from` | empty |
| env | `getenv` | after a successful lookup (an `error` result only from `@getenv`) | `found`/`unset`/`error` |
| env | `env-all` | before the snapshot | empty |
| spawn | `spawn` per stage | before launch (also for failed launches) | cwd as spelled, or empty; `job` for job control |
| untraced | open_bytes, stream ops, inject, file().copy/move/remove, cd, setenv, unsetenv, which, clock, sleep, random | — | — |

`HostCall` carries a per-op `TracePoint { Pre, PostOk, PostAlways, None }`
table (`host_call.h`) that encodes this column, so a native cannot emit at the
wrong time. The trace-neutrality test (`rt_diff_selftest.sh` tracing on/off)
applies unchanged.

**Proposal, trace v2 (not for parity; coordinator decision, §9 O2):** one
line per Host op including the untraced ones, emitted after the attempt,
`res` = result class plus `"err":"<code>"` on failure, enabled by
`NIFT_TRACE_HOST_VERSION=2`; v1 stays the parity channel until legacy emits v2
too (both engines, D-018). v2 makes failed writes distinguishable from
successful ones and closes the coverage gaps of finding 5.

### 2.5 Dependency recording (D-002), identical to legacy by construction

Legacy records through `Parser::observe_presence / observe_read /
observe_streamed_read / observe_metadata_read / note_written /
observe_listing / observe_environment` plus `result_.observed.
environment_snapshot` and `.platform` (`ParserObservation.cpp`,
`ParserExpression.cpp`). Plan:

1. **Extract** these bodies (unchanged) into `src/PageObservation.{h,cpp}`:
   `class PageObservation { PageObservation(RenderHost&, RenderResult&, bool standalone); void presence(path, exists, kind); void read(path, bytes); void streamed_read(path, size, const uint64_t* hash); void metadata_read(path); void written(path); void listing(key, path); void environment(name, const HostResult&); void environment_snapshot(); void platform(const std::string&); bool snapshot_wanted() const; }`.
   Legacy's `Parser` methods become one-line forwarders (behaviour-neutral
   refactor: rt-diff same-binary plus `dynamic_reads*.sh`, `fs_freedom.sh`,
   `randomized_incremental_differential.py` must stay green). This is the only
   legacy-side change CP16 needs and it touches shared files: it is
   scheduled as its own commit (no other agent may edit `Parser*.cpp` at the
   same time, D-021).
2. `rt::host::PageObserver : Observer` holds a `PageObservation`; the
   standalone Host passes a null observer (legacy records nothing there).
3. `HostCall` invokes the observer per the **recording table** (pinned by
   `site_deps`, `site_observe`, `site_run`):

| Op | Recording (page context) |
|---|---|
| `open`, `cat`, `file().open("r")`, `inject`, `@input`, `@json`, template/content | `read(path, bytes)` → `dependencies` + content hash |
| `file().open("rw")` | content hash only, no `dependencies` entry (code reading; pin in implementation) |
| `sha256_file` | `streamed_read` (hash from the same chunks when `snapshot_wanted`) |
| `stat` on a file | `presence` + `metadata_read` |
| `exists`, `is_file`, `is_dir`, `stat`, `file().exists()` | `presence(exists, kind)` (`reqs`/`kinds` or `absent`) |
| `ls(dir)`, `ls()`, `ls(glob)` | `listing(dir:/glob: key)` via `RenderHost::listing_fingerprint` (per-build cache) |
| `getenv`, `@getenv` | `environment(name)` (skips `NIFT_HOOK_*`) |
| `env()` | `environment_snapshot()` |
| `platform()` | `platform()` |
| writes (`touch mkdir save cp mv rm`) | `written(path)` (drops the path from observations) |
| `open_bytes`, `ifstream` reads, `file().copy/move/remove`, `run`, `cmd`, clock, random | nothing (legacy; gaps Q-H2 pending O1) |

Because both engines call the same `PageObservation`, page-info is
byte-identical by construction; the dependency-equivalence tests (§7.3) prove
the call sites match. Async/thread workers (CP02 review #8, still open) stay
out of CP16 (concurrency is refused on the VM, W18).

### 2.6 Errors: codes and catchability (D-022/D-025)

Identity, severity and catchability are separate (D-022). The VM reproduces
today's split exactly (CP13 option D): every `HostError` carries legacy's
code; catchable = exactly the legacy `fail_recoverable` sites (codes `io.*`,
`stream.*`, `json.parse_failed`). Codes used by Host ops:

| Code (CP13 taxonomy) | Catchable today | Raised by |
|---|---|---|
| `io.open_failed` | yes | open, cat, open_bytes, sha256_file, file().open |
| `io.read_failed` | yes | open_bytes, sha256_file read errors |
| `io.create_failed` | yes | touch, mkdir |
| `io.write_failed` | yes | save (parent missing, temp create/write) |
| `io.atomic_replace_failed` | yes | save |
| `io.copy_failed`, `io.move_failed`, `io.remove_failed` | yes | cp, mv, rm, file().copy/move/remove |
| `io.directory_read_failed` | yes | ls |
| `io.change_directory_failed` | yes | cd |
| `io.metadata_failed` | yes | exists, is_file, is_dir, stat |
| `stream.open_failed`, `stream.read_failed`, `stream.write_failed`, `stream.flush_failed`, `stream.close_failed` | yes | ifstream/ofstream |
| `runtime.invalid_argument` / `runtime.arity_error` / `runtime.type_error` (CP13 mapping of the plain messages) | no | argument checks of every host built-in |
| `runtime.invalid_operation` | no | `open: path is a directory`, `remove: directories are not removed recursively`, `close: file has unsaved changes…`, `inject: source is not readable`, `cp: glob matched no source files`, `run: external process execution disabled` (legacy has no specific code; CP13 table decides the exact code, the VM uses whatever CP13 maps) |
| `limit.exceeded` | no (never ordinary exceptions) | `secure_random_bytes: result exceeds 10000000 bytes` (legacy plain error; same text), new read limit (O3) |
| `host.service_failed` | no | clock unreadable, random source failure, `setenv: failed` |
| `internal.*` | no | handle resolution failures (impossible by W13), Host invariant breaks |

Message text: byte-identical to legacy, including the `std::error_code::
message()` strings (`ls: No such file or directory`, `make_dir: Permission
denied`). These come from the C++ library and are platform-dependent; the
POSIX and Windows Host implementations must use the same
`std::error_code` → message function legacy uses (`ec.message()`), never
`strerror` directly, or Windows/macOS text drifts (risk R5).

Process launch failures are not errors: they are result values
(`launched:false`, `error`, 127/126) exactly as feature-site-dx defined.

### 2.7 Limits (D-020: loud)

| Limit | Legacy | VM |
|---|---|---|
| `secure_random_bytes` ≤ 10,000,000 | fatal error | same text, code `limit.exceeded` |
| reads into memory (`open`, `cat`, `file().open`, `inject`, captures) | unbounded (OOM on devices/FIFOs, finding 4) | charged to the VM heap budget before allocation (`Context::heap_limit`); exceeding → `limit.exceeded` with `<builtin>: file exceeds the memory budget` **(new message; O3/O4)** |
| non-regular files in whole-file reads (char devices, FIFOs) | read until EOF (never for `/dev/zero`) | proposal: refuse with `io.open_failed` … or read with the budget cap — **O3** |
| capture size of `run()` | unbounded on disk, read back fully | read back under the heap budget (O4) |
| argv/env size | E2BIG from exec → `cannot execute x: Argument list too long` (code reading, ExecOther) | same |
| `sleep` range | int64 ms, fatal on negative | same; plus cancellable by the VM's termination path (§5) |

---

## 3. Filesystem semantics

* **Freedom (D-002, CP03).** No project-root containment by default; absolute
  paths, `..`, neighbouring repositories and external output directories work.
  `--fs-root`/embedder policies stay opt-in in `Host::resolve` (shared
  `nift_fs_root_allowed`). The VM reproduces the one remaining legacy
  containment check (FS-24 in pages) only until O1 decides; it is a D-002
  defect, not a policy.
* **Path resolution** is legacy's (`resolve_path`): `~` only in scripts,
  lexical `..` [fs_lexical_dotdot]. OS-semantics `..` (CP02 10a, CP03 residual
  risk) is a language-cleanup item (Q-H6); changing it changes which file a
  path names, so both engines must change together.
* **Symlinks.** Metadata queries follow links (dangling = missing, a link to a
  FIFO is `other`) [fs_meta]. Writes: `cp`, `ofstream`, `touch`, `mkdir`
  through a linked directory all write **through** the link; `rm` removes the
  link itself; `file().save()` **replaces the link** with a regular file
  [fs_symlink]. Build recovery (feature-build-recovery F3) restores symlinked
  outputs *through* the link; `save()` is the one operation inconsistent with
  that rule (Q-H3). The VM reproduces it (parity) and the register records the
  intended rule ("atomic replace of the link's target: resolve the final
  target, write the temp sibling next to the target, rename over the target").
* **Atomicity.** Only `file().save()` is atomic (temp sibling
  `<name>.nift-tmp-<steady_clock ticks>` in the same directory, permissions
  copied, `replace_file_atomic`, temp removed on write failure). `ofstream`,
  `touch`, `cp` write in place; build outputs are in place (CP3 decision);
  page-info and failed-list writes are atomic and belong to the build engine,
  not to Host ops. The VM must use the same temp naming (it is visible in a
  crash) and the same `filesystem::replace_file_atomic`.
* **Glob and listing** use the shared `glob_expand_shared` and display logic
  (no reimplementation); ordering is bytewise after sort; `ls(dir)` includes
  dot files, `*` excludes them [fs_list].
* **Text vs bytes.** Whole-file reads return the exact bytes as a string
  (NUL, CR and invalid UTF-8 preserved); `read_line` on file objects strips
  one trailing CR, on streams it does not (Q-H7).

---

## 4. Process semantics

### 4.1 Existing APIs: exact legacy behaviour (default output mode `FileCapture`)

* **argv vs shell.** `run`/`cmd` are argv-only; no shell parsing, globbing or
  variable expansion (`run("./args.sh", "*", "$HOME", "a;b")` passes them
  literally) [proc_run]. Only `ENOEXEC` falls back to `/bin/sh <file>`
  (historical, `PreparedProcessPOSIX`). Users who want a shell call `sh -c`.
* **PATH** search in the parent before fork (`PreparedProcessPOSIX`), relative
  PATH entries resolve after the child's chdir; Windows: PATHEXT order,
  extensionless last.
* **Per-stage status and `launched`** (feature-site-dx, R6/N2): single command
  → `launched:false` + `error` for exec/setup failures, 127/126; pipeline →
  top-level `launched` = stages were started, `stages[i]` per stage, top-level
  `exit_code` = last stage, later stages `not started` after a parent-side
  failure. Signals: `128 + signo` (143, 137) [proc_run].
* **Capture.** stdout of the last stage and stderr of every stage go to two
  anonymous regular files (CP07: `O_TMPFILE`/unlinked `mkstemp`, Windows
  delete-on-close keepers); read back after all stages are reaped; children
  see a regular, seekable stdout (`test -f /dev/stdout`) [proc_run];
  background descendants are not waited for and their later output is lost.
* **stdin** inherited from Nift unless `.stdin(path)` [proc_stdin];
  **descriptors**: everything not close-on-exec is inherited (like Bash);
  Nift's own descriptors (trace, captures, pipes) are CLOEXEC.
* **Environment**: the parent's environment at launch (including `setenv`)
  plus `.env(k, v)` overrides for every stage.
* **Timeouts and signals**: none in legacy; `run` blocks until all stages
  exit. If Nift is killed the children keep running (CP07 test).
* **Launch lock**: launches are serialised by `nift_process_launch_mutex`
  (fork safety with threads).

### 4.2 D-011 objective: pipe capture and streaming in the Host process layer

`SpawnRequest { std::vector<SpawnStage> stages; OutputMode mode; std::size_t
capture_limit; std::optional<std::chrono::milliseconds> deadline; CancelToken*
cancel; }` with

| `OutputMode` | Ownership and lifetime | Used by |
|---|---|---|
| `FileCapture` | as 4.1 (default; the only mode the existing APIs use, so their documented behaviour is unchanged) | `run`, `cmd().run()` |
| `PipeCapture` | parent owns both read ends; drained with `poll` (Windows: overlapped I/O or a drain thread) into bounded buffers charged to the heap budget; **completion = all stages reaped**, never pipe EOF and never an idle timeout; after the last stage exits, descendants still holding the pipe are not waited for: the parent closes its read ends (a descendant then gets SIGPIPE/EPIPE — this is the observable difference D-011 recorded, so the mode is opt-in) | new API (O5), benchmarks |
| `Stream` | child inherits Nift's stdout/stderr after Nift flushes its own buffered output (order with `print` preserved, as command-style statements do today [proc_command_style]) | command statements (CP19/CP20), new API |
| `Inherit` | no capture, no flush semantics beyond Stream | interactive shell (CP21) |
| `Detached` | own session/process group, stdio to the null device, never waited, reaped by a SIGCHLD-safe reaper; returns pid | new API (O5) |

Cancellation and termination (designed, not idle timeouts): a `deadline` or
cancel request sends SIGTERM to the pipeline's process group, waits a fixed
grace period, then SIGKILL; results carry `timed_out`/`cancelled` (new keys —
O5). Each pipeline runs in its own process group (POSIX) or job object
(Windows; `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` only where the guarantee is
claimed, D-027). Backpressure: a full capture buffer stops draining that pipe
(the child blocks), never grows beyond `capture_limit`; overflow ends the run
with `limit.exceeded` after terminating the group. Low fd limits: pipes are
created per stage just before fork and closed immediately; EMFILE is reported
as `launched:false` (`cannot create pipe: Too many open files`) without
leaking.

The language surface for `PipeCapture`, `Detached`, deadlines and
cancellation is **not** designed here (O5). CP16 implements the Host layer
and benchmarks both modes through a C++ harness and, if approved, the new API.

### 4.3 Windows differences (CI only, D-004)

`CreateProcessW` with an argv-built command line (`quote_win_arg`); `.bat`/
`.cmd` targets are run by `cmd.exe` with its own parsing (argument injection
risk, documented; feature-serve accepts only `.exe` for that reason); launch
failure is synchronous (`ERROR_FILE_NOT_FOUND` → 127 `executable not found`,
`ERROR_ACCESS_DENIED` → 126); no signals (exit codes are process exit codes,
`STATUS_CONTROL_C_EXIT` on Ctrl+Break); capture files are named and deleted
on close; command-style missing executables are hard errors on Windows but not
POSIX (site-dx P5). Pipe mode on Windows needs overlapped named pipes or a
drain thread per stream; job objects replace process groups.

---

## 5. Clock and time

* `Host::wall_ms` (TI-01), `Host::monotonic` (TI-03), `Host::sleep_ms`
  (TI-02), `Host::wall_time()` for TI-04. Natives never call `std::chrono`
  directly.
* **Deterministic tests.** The differential corpus prints only
  clock-independent facts (types, ordering, booleans with generous bounds)
  [time_ops]; rt-diff keeps excluding programs that print clock values
  (`ignore.txt`). VM unit tests inject `FakeClock` through `FaultHost` (C++
  only, like legacy's `timer_clock_`). No user-visible clock override (an
  environment variable that fakes time would be user-controllable; same rule
  as `ProcessTestHooks.h`).
* **sleep** blocks the VM thread (no event loop until CP22). It must be
  interruptible by the VM's termination path: implemented as a wait on the
  isolate's cancel condition with a deadline (`sleep_ms` returns early only on
  cancel, which ends the run; normal behaviour is unchanged).
* **timer** state machine exactly as `Parser::call_timer_method` (resume
  after stop is a no-op; `start` restarts at 0; `elapsed` exact int ms).
  Timers are refused in thread captures (legacy `non-transferable timer`);
  irrelevant on the VM until CP22.
* **build-* variables** are template-side (CP24). Legacy reads the clock once
  per variable, so `build-date` and `build-time` can straddle midnight in one
  page (Q-H15). For CP24 parity the VM must also read per variable; the
  register records "one clock read per page render" as the intended rule.
  Pages using them are not rebuilt as time passes (no recording; by design).

---

## 6. Compiler wiring rules (continuing CP14 W1–W7, CP15 W8–W11)

What the VM accepts: every built-in in §1 rows FS-01..FS-27, PR-01, PR-02,
EN-01..EN-06, TI-01..TI-03, RN-01, called with a statically known argument
count inside the documented positions below. Everything else is refused
before execution (whole-unit fallback, D-001/D-013) with the reason shown.

| Rule | Reason string | Refuses | Why |
|---|---|---|---|
| W12 | `host-before-replay-removed` | any Host native while a replay path exists | D-013: no effectful VM op while replay exists (CP12 exit criterion; assert, should never fire) |
| W13 | `host-handle-flow` | file/stream/cmd/timer values anywhere except: a local binding, a method receiver, the argument of `close(s)` / `.pipe(c)`; refused: storing in arrays/objects, passing to or returning from user functions, comparison, `print`/`stringify`/`type`/`is_*`, interpolation | legacy handles are strings that can be printed, compared and forged; typed handles cannot reproduce that; CP17 owns full handle semantics |
| W14 | `host-literal-arg` | `inject("…")` with a quoted literal containing `$[`, `\` escapes or quotes | legacy strips the quotes and uses the raw text (no interpolation/escape processing), unlike an evaluated string (shape-dependent, as CP15 J-Q2) |
| W15 | `host-dynamic-arity` | spread arguments (`f(...xs)`) to any host built-in; `copy(...)` whose argument count is not static | `copy` is a value copy with one argument and a file copy with two or more (Q-H9); arity must be known to bind |
| W16 | `host-command-statement` | command-style statements (`@__nift_cmd`) | shell front end (CP19/CP20); their missing-executable rule is unresolved (P5) |
| W17 | `host-automation` | `build build_all build_names build_repair track untrack status tracked project_root` | re-entrant build engine; not a Host op; later checkpoint |
| W18 | `host-concurrency`, `host-ffi` | `thread async await mutex atomic<…>`, `ffi_*` | CP22/CP23; FFI after CP17 |
| W19 | `host-page-cat` | `cat()` in page-render units | it writes to the process stdout, not the page (Q-H14); page units are CP24 anyway |
| W20 | `host-read-val` | `read_val()` on file objects and streams | it evaluates the token as a Nift expression in the current scope (dynamic code); refuse until an expression-evaluation native exists (needs CP12 `GETDYN`-style dynamic evaluation) |
| W21 | `host-inject` | every `inject(...)` | whether legacy takes the JSON path or evaluates the file as a Nift expression (dynamic code) depends on the file's content at run time (invalid JSON, a top-level JSON string with `$[...]`), so the engine cannot be chosen before execution (D-013); refused until dynamic evaluation exists on the VM or Q-H5 makes `inject` JSON-only (risk R3) |
| W22 | `host-env-in-page` | `setenv`, `unsetenv`, `cd` in page units | legacy errors there (`only available in standalone…`); page units are CP24 |
| W23 | `host-file-open-device` | none statically (runtime: limit, O3) | — |
| W24 | `host-legacy-gap` | nothing; the VM reproduces gaps until O1 | listed so the decision is explicit |

Kinds (CP12 inference, CP14 `KindSet`): `open cat`-family → string;
`open_bytes read_bytes read_all_bytes secure_random_bytes` → bytes (CP15
W8 bit); `exists is_file is_dir eof modified running paused` → bool; `stat
run cmd().run()` → object (members fixed: `exists type size` /
`exit_code stdout stderr launched error stages`); `ls` → array of string;
`epoch elapsed tell replace` → int; `getenv` → string ∪ null; `env` → object
of string; handles → new kind bits `kFile kStream kCommand kTimer` (CP12's
set has no room: 16-bit `KindSet` from CP15 has bits 8–15 free).

Alias analysis (CP12 `alias.cpp`): host natives never mutate their Nift
arguments; file-object methods mutate only Host-side state; results are fresh.

---

## 7. Test plan

### 7.1 Fixtures prepared now (`tests/runtime2/cp16/`)

| File | Content |
|---|---|
| `MANIFEST.tsv` | 30 probes (25 scripts, 5 sites) → inventory rows, CP15 deferrals, what each pins |
| `probes/scripts/<id>/main.f` (+ `setup.sh`, `env`, `stdin`, `priv`) | legacy probe programs |
| `probes/sites/<id>/site/`, `steps`, `normalize` | sites with build/edit/status steps and page-info snapshots |
| `expected/<id>/` | captured `stdout stderr exit trace.jsonl files.txt` (scripts) / `steps.txt trace.jsonl deps.json files.txt` (sites) |
| `capture.py` | capture (`--write`) and comparison (`--check`); every probe run twice in different absolute directories, normalised results must match (all 30 do); complete list of normalisations in its docstring |
| `mutant.py`, `selfcheck.sh` | seeded-defect self-check (§7.4) |

Captured with `/home/claude/review/nift-serve/nift` (finding 1),
`TZ=UTC LC_ALL=C`, `PATH=/usr/local/bin:/usr/bin:/bin`, `HOME`/`TMPDIR` inside
the run directory. `fs_eacces` runs without `CAP_DAC_OVERRIDE`/
`CAP_DAC_READ_SEARCH` (`setpriv`), so EACCES is real as root; it is SKIPPED
where `setpriv` is unavailable. Not wired into the Makefile (brief). Known
host dependencies of the expected files: `/usr/bin/sh` resolving `sh`,
dash's `echo: I/O error` wording in `fs_enospc`, `which("sh")` only printed
as a boolean. `expected/fs_read/stdout` contains CR bytes on purpose.

### 7.2 Differential tests (legacy vs VM, output + Host-op trace)

* `tests/runtime2/rt-diff-corpus/host/` — the fixtures above as rt-diff
  entries. rt-diff needs two small additions (CP16 implementation tasks):
  a `setup` step for script/tree entries (symlinks, FIFOs, modes cannot live
  in git) and a `priv nodac` sidecar. Run with `--a legacy --b
  'nift with NIFT_VM=require'`; channels: exit, stdout, stderr, combined,
  files, home/tmp, **trace**. Gate: 0 differences; `NIFT_VM_STATS` outcome
  `vm` for every entry not refused by W12–W22.
* `tests/runtime2/vm_host.py` (D-016 suites): **supported** — every accepted
  host built-in in all five CP12 contexts (top level, loop body, function from
  top level, function from loop, nested function) with 100% VM and 0
  differences; **unsupported** — one program per W-rule proving fallback
  before execution with its reason; **generated** — `vm_gen.py` extended with
  host calls over a generated sandbox tree (coverage measured, no threshold).
* `make test-rt-host-diff` wraps both; `capture.py --check --nift <VM
  binary>` remains a quick local loop.

### 7.3 Dependency-recording equivalence

* `test-rt-host-deps`: every `probes/sites/*` built by legacy and by the VM
  (once page `@script` units run on the VM — CP24 or earlier if engine
  selection admits them); compare `deps.json` (page-info dependency sections,
  listing/snapshot fingerprints compared **unmasked** because both runs use
  the same directory, as rt-diff does) and `status` after every edit step.
* `site_deps` doubles as the regression test for O1: today steps 10–13 show
  `open_bytes`/`ifstream` edits undetected; if O1 = fix, the new expected
  output lists them and both engines must agree.
* `tests/randomized_incremental_differential.py`: add a `--engine vm` axis
  with host-reading operations (`dynamic_open`, `dynamic_stat`,
  `dynamic_glob`, `dynamic_env`); incremental result must equal a clean
  rebuild for both engines.
* C++ unit `tests/rt/host_observer_unit.cpp`: for each HostOp, the
  `PageObservation` calls made (recorded through a spy) equal the table in
  §2.5.

### 7.4 Mutation-style check of the comparison (done for the fixtures)

`selfcheck.sh /home/claude/review/nift-serve/nift` — 18/18 pass: the oracle
compares OK on 7 probes, and each seeded defect is reported as DIFF:

| Mutant | Defect class | Probes | Detected in |
|---|---|---|---|
| `trace-path` | wrong path | fs_read, site_deps | trace.jsonl |
| `trace-res` | wrong result class | fs_meta | trace.jsonl |
| `trace-drop:read` / `:spawn` / `:save` | missing effect | fs_read, site_deps, proc_run, fs_write | trace.jsonl |
| `trace-swap` | effects reordered | fs_write | trace.jsonl |
| `code` | wrong error code (`io.open_failed`→`io.read_failed`) | fs_errors | stdout |
| `dep-drop` | missing dependency | site_deps | deps.json, steps.txt (status no longer sees the edit), files.txt, trace |
| `absent-drop` | missing absence observation | site_observe | deps.json, steps.txt |

For CP16 the same modes are added to `tests/runtime2/rt_diff_mutant.py`
(`trace-path`, `trace-res`, `dep-drop`, `code`) and to
`rt_diff_selftest.sh`, and a VM-side mutant set (`host_mutants.py`, like
`builtins_mutants.py`) seeds defects into the Host layer itself: trace
emitted after instead of before a write, `read` traced on failure, resolve
without `lexically_normal`, `observe_read` skipped for `cat`, `save` without
temp+rename, `launched` true on exec failure, stderr of non-last stages
dropped, `exists` recording `reqs` for a missing path. Each must fail
`test-rt-host-diff` or `test-rt-host-deps`.

### 7.5 Fault injection (`tests/rt/host_faults.cpp`, `FaultHost`)

`FaultHost` wraps the real Host and injects, per call index or path pattern:
`ENOENT EACCES EPERM ENOSPC EIO EISDIR ENOTDIR EMFILE E2BIG EINTR EAGAIN`,
short reads/writes (n−1 bytes, then the rest), a failing `rename`
(temp file must be removed and the original intact), fork/pipe/dup2 failures
(reusing `ProcessTestHooks.h` names `fork pipe dup2-in dup2-out dup2-err
temp`), clock and random-source failures. For each case the test asserts: the
VM result (value or error code + catchability + exact message), the trace
lines, the observations recorded, and the filesystem state. Expected values:
legacy's, from the fixtures where a probe exists (`fs_errors`, `fs_eacces`,
`fs_enospc`), otherwise from code reading, listed in the test with the source
line. Required properties: **EINTR** is retried transparently everywhere
(legacy's `read_fd_all`, `waitpid`, `getrandom` loops; std streams retry
internally) and never surfaces; **partial writes** are completed or reported
as the operation's write error, never silently truncated; a failed `save`
leaves neither a partial target nor a stray `.nift-tmp-*` file; no descriptor
leaks after any injected failure (`/proc/self/fd` count before/after).

### 7.6 Performance guards

* Per-op instruction guards under callgrind (D-005), locked like CP14:
  `host.stat`, `host.exists.missing`, `host.read.4k`, `host.read.1m`,
  `host.ls.1k`, `host.glob.recursive.2k`, `host.sha256_file.10m`,
  `host.save.4k`, `host.getenv`, `host.spawn.true` (Nift-side instructions
  only), each with the trace off and on and the observer off and on.
  Gate: VM ≤ legacy for every op; trace-off overhead = one cached-flag load
  (as legacy); observer-off = one null check.
* No-op incremental build instructions unchanged (CP02 gate, +5%).
* D-011 process benchmark set (both modes): 300× `/bin/true`, small and 4 MB
  captures, simultaneous stdout/stderr, 3-stage pipeline, background
  descendant, long-running child with cancellation, 64 concurrent launches,
  `ulimit -n 32`; wall time (preflighted, D-026), CPU, peak RSS, fd and
  process leak counts. Gate: pipe mode ≤ Bash `$()` + small margin;
  file mode reports OS cost separately (D-011).

### 7.7 Cross-platform notes

Linux verified locally only (D-004). macOS: no `O_TMPFILE` (mkstemp+unlink
path), `/private/var` realpaths (rt-diff rule 1), `posix_spawn` not used,
`setpriv` absent (fs_eacces SKIPPED; use a non-root CI user instead). Windows:
the trace `arg` uses generic separators; `~` uses `USERPROFILE`; some
invalid-name paths are "not found" instead of errors (FILESYSTEM-TYPE-API.md);
error-message texts from `std::error_code::message()` differ (fixtures are
POSIX-only; Windows gets its own expected files from CI, never hand-written);
symlink creation needs privileges (fs_symlink SKIPPED without them);
`/dev/full` probes have no equivalent. The cross-platform gate exercises
process termination and child cleanup, not only compilation (D-027).

---

## 8. Benchmark readiness (planned e2e workloads)

`benchmarks/runtime2/suite/workloads.py` `PLANNED_E2E` (read-only) lists
stubs; what each needs from CP16, and what would make it fall back:

| Workload | Needs from CP16 | Also needs | Fallback risks to avoid in the program |
|---|---|---|---|
| `dir_manifest` (2,000-file tree, SHA-256 manifest) | `ls("root/**/*")` (recursive glob, sorted), `is_file`, `sha256_file`, writing the manifest (`file().open("w")`+`save` or `ofstream`) | CP14 `join`, string concat | W13: keep file/stream handles in locals, no helper functions taking handles; glob display paths are relative — the manifest must use them as-is in every runtime for equal checksums |
| `subprocesses` (50× `true`, capture 5 commands) | `run`, `cmd().pipe().run()`; for the D-011 comparison, pipe mode (O5) | — | none; results are plain objects |
| `log_parse` (100k lines) | `open(path)` whole file (or `ifstream.read_line`) | CP14 `split`, `to_int`, object counting; F5 for `sort_by`/group-by or a manual loop | `ifstream` handle in a local only; `read_val` refused (W20) |
| `file_transform` | `open`, `file().open("w")`+`write`+`save` (atomic) or `ofstream` | CP14 `replace trim to_upper` | as above |

Fixture generation (the 2,000-file tree, the 100k-line log) belongs to the
suite, outside the timed region; CP16's per-op guards (§7.6) give the native
cost, the suite gives end-to-end time. records20k needs nothing from CP16.

---

## 9. Risks, quirks, open decisions

### 9.1 Decisions needed

| # | Question | Options | Recommendation | Who |
|---|---|---|---|---|
| O1 | Legacy gaps: `open_bytes`/`ifstream` reads not recorded (stale output, D-002); `file().copy/move` containment in pages (D-002 remnant) | (a) VM reproduces; fix later in language cleanup; (b) fix legacy now (small change in `ParserExpression.cpp`), then both engines match | (b): these are defects against decisions already taken (D-002, D-020 "success means success"), not language changes; old/new tests from `site_deps`/`site_contain` | **owner** (changes incremental rebuild results and an error into success; could be judged coordinator-level under D-002 — flagged so the owner can say) |
| O2 | Trace v2 (complete coverage, post-attempt, error codes) | keep v1 only; add v2 behind a version switch for both engines | add v2 after v1 parity is reached | coordinator (test infrastructure; no user-visible behaviour) |
| O3 | Whole-file reads of non-regular files (devices, FIFOs) and unbounded reads | reproduce (OOM/hang); loud `limit.exceeded` at the heap budget (CP15 `validate` precedent); refuse non-regular files with `io.open_failed` | loud limit, both engines | **owner** (deliberate behaviour change, even if from a crash) |
| O4 | VM heap budget (1 GiB) vs host data: a 2 GiB `open()` or `run()` capture works in legacy, fails on the VM | count host strings in the budget (loud failure); raise the budget for host data; exempt | count them, make the budget configurable (`--memory-limit`), document | **owner** (performance/compatibility trade-off) |
| O5 | Language surface for D-011 modes (pipe capture, streaming, detached, deadlines, cancellation, `timed_out` result keys) | new `cmd()` methods; new built-in; Host-layer only for now | Host layer + C++ benchmarks in CP16; syntax proposal for the owner before exposing | **owner** (new capability surface) |
| O6 | Catchability of host failures that are fatal today: `inject` missing source, `open(dir)`, `rm(dir)`, glob-no-match, `sleep(-1)`, random limit, `NIFT_NO_PROCESS` | part of the D-022 "catchable runtime errors" checkpoint | defer to that checkpoint (both engines) | owner (already scheduled by D-022) |
| O7 | `file().save()` through a symlink | keep (replace link) or write the target atomically | target (consistent with build recovery F3) | language cleanup (owner) |

### 9.2 Candidate quirks for `language-consistency.md` (recorded, not decided)

| # | Legacy behaviour | Proposed single semantics | Kind | Evidence |
|---|---|---|---|---|
| Q-H1 | Trace omits `open_bytes`, streams, `inject`, `file().copy/move/remove`, `cd`, `setenv`, `unsetenv` | trace every Host op (v2) | test infrastructure | fs_read, fs_write |
| Q-H2 | `open_bytes()` and `ifstream` reads are not dependencies; `file().open("rw")` records a hash without a dependency entry | every read is a dependency | correctness (D-002) | site_deps; code reading for `rw` |
| Q-H3 | `file().save()` replaces a symlink; `cp`/`ofstream`/`touch` write through | writes go through links; atomic replace of the target | consistency | fs_symlink |
| Q-H4 | `file().copy/move` refuse paths outside the project in pages; `cp`/`mv` do not | no containment (D-002) | correctness | site_contain |
| Q-H5 | `inject` of a missing file is fatal, `open` is catchable `io.open_failed`; invalid JSON in `inject` is re-evaluated as a Nift expression | one error class for unreadable sources; JSON files are JSON | consistency | fs_fatal_inject_missing, json_host_bad |
| Q-H6 | `..` after a symlink is removed lexically (`link/../x` ≠ what `cat` opens) | OS semantics | correctness | fs_lexical_dotdot (CP02 10a) |
| Q-H7 | `file().read_line()` strips a trailing CR, `ifstream.read_line()` keeps it | one rule | consistency | fs_read |
| Q-H8 | `file().open("w")` reads the whole existing file first (unbounded; OOM on devices) | `w` truncates without reading | correctness/limits | finding 4 (not a fixture: it OOMs) |
| Q-H9 | `copy` is a value copy with one argument and a file copy with two | separate names (`copy_file` or `cp` only) | naming | copy_overload |
| Q-H10 | Writes are traced before the attempt with an empty result; reads only on success; `list` before a failing attempt | trace after the attempt with the outcome | test infrastructure | fs_errors, fs_eacces |
| Q-H11 | `spawn` trace: arguments joined by spaces (ambiguous), cwd as spelled (not absolute) | JSON array argv; absolute cwd | test infrastructure | proc_cmd |
| Q-H12 | `open`/`cat`/`touch` say `cannot open path` for every errno; `ls`/`mkdir`/`cp` append the platform `strerror` text | one message shape with a stable reason | diagnostics | fs_errors, fs_eacces |
| Q-H13 | `rm` of a missing path succeeds silently; `rm(dir)` fatal; `cp("*.none", d)` fatal while `cp("missing", d)` is catchable | consistent missing-source rules | consistency | fs_errors, fs_fatal_rm_dir |
| Q-H14 | `cat()` inside a page render prints to the terminal, not the page | write into the page (or refuse) | correctness | site_deps step 1 |
| Q-H15 | each `build-*` variable reads the clock separately | one timestamp per render | consistency | code reading (`Parser::metadata`) |
| Q-H16 | a `#!` interpreter in a missing directory reports `executable not found: x`, a missing interpreter file reports `interpreter not found` | one message (site-dx R6 intent) | diagnostics (possible site-dx defect) | proc_run `badinterp.sh` |
| Q-H17 | `cmd().stdout(p, m)`: any `m` other than `"append"` truncates; `.stdin` binds the first and `.stdout` the last stage *at call time*, so `.stdout()` before `.pipe()` is silently ignored | validate mode; bind at `run()` | correctness | proc_cmd; code reading for ordering |
| Q-H18 | `read_val` evaluates each token as a Nift expression in the caller's scope | parse a JSON value | correctness/safety | fs_read, site_deps |
| Q-H19 | after a failed `save()` inside `try`, the program completes and then exits 1 with `managed file left open` | report at the failing statement or allow discard | consistency | fs_eacces |

### 9.3 Risks

* **R1 Shared-file refactor** (§2.5 extraction) touches `Parser*.cpp`, which
  CP12/CP13/site-dx follow-ups also edit: sequence it as its own commit after
  CP13 lands; behaviour-neutral gate listed in §2.5.
* **R2 Handle kinds** (`Stream`, `Command`) change `rt/value.h` (CP09) and
  CP12's kind set: coordinate with CP17 so the enum is changed once.
* **R3 `inject`** cannot be compiled safely in general: whether legacy takes
  the JSON path or the Nift-expression path depends on the file's content at
  run time, and the expression path is dynamic code the VM cannot run. Until
  the VM can evaluate dynamic expressions (or the language makes `inject`
  JSON-only, Q-H5), W21 refuses `inject`; the CP15 deferral is therefore only
  partly closed. `@json` (directive) is always JSON and is CP24.
* **R4 Oracle drift**: expected files depend on dash wording (`echo: I/O
  error`) and on the oracle binary; re-capture on a rebuilt oracle before the
  first differential run (finding 1).
* **R5 Message portability**: legacy messages embed `std::error_code::
  message()`; any reimplementation via `strerror` drifts on non-glibc
  platforms.
* **R6 Pipe mode** reintroduces the CP07 rejection reasons if it ever becomes
  the default for existing APIs; the design keeps it opt-in (O5).
* **R7 Page units on the VM**: dependency recording only matters in page
  renders; if engine selection does not admit page `@script` units before
  CP24, §7.3 runs against the C++ observer spy only until then.

---

## 10. Ready for implementation

* Inventory, trace-timing table, recording table, error-code table and
  wiring rules (this record).
* Legacy fixtures with expected outputs and Host-op traces (30 probes, stable
  across directories), capture/check tool, and a seeded-defect self-check
  that passes (`tests/runtime2/cp16/`).
* Implementation order once CP14/CP15 integrate: (1) `PageObservation`
  extraction in legacy (behaviour-neutral); (2) `rt/host/` interface + POSIX
  Host + `HostCall` with trace/recording tables; (3) FS natives (read family,
  metadata, listing, writes, file objects, streams), EN, TI, RN natives;
  (4) PR natives on `FileCapture`; (5) wiring W12–W22 and the D-016 suites;
  (6) rt-diff `setup`/`priv` support and the host corpus; (7) fault injection,
  guards; (8) D-011 pipe/stream/detached modes in the Host layer and the
  benchmark set; (9) Windows Host (CI).
* Blocked on decisions: O1 before step 6's expected outputs are final; O3/O4
  before the read paths are frozen; O5 before any new process API.

## Evidence

Fixture capture: `capture.py --nift /home/claude/review/nift-serve/nift
--runs 2 --write` → 30 probes WROTE, none UNSTABLE. Self-check:
`selfcheck.sh` → 18/18 pass. No builds, sanitizers or benchmarks were run.
