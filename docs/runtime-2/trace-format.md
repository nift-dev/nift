# Host-operation trace format (CP08)

Set `NIFT_TRACE_HOST=<fd>` to an open, writable file descriptor and every traced
Host operation appends one JSON object per line to it:

```
NIFT_TRACE_HOST=3 nift build --all 3>trace.jsonl
{"ctx":"/","op":"read-source","arg":"/abs/site/templates/template.html","res":"ok"}
```

The variable is read once per process, first thing in `main()`
(`nift::trace::initialize()`, `src/HostTrace.h`; embedders get the same check
lazily on the first trace point). Descriptor rules (CP08 review #3):

- the descriptor must be open and writable at that moment (`fstat` +
  `F_GETFL`), so a descriptor the caller closed is never confused with a file
  Nift opens later;
- 0, 1 and 2 are refused unless `NIFT_TRACE_HOST_ALLOW_STDIO=1`;
- it is duplicated to a private close-on-exec descriptor (≥ 100) and the
  original is closed: child processes never inherit it;
- `NIFT_TRACE_HOST` and `NIFT_TRACE_HOST_ALLOW_STDIO` are removed from the
  environment: scripts (`getenv`, `env()`) and children see the same
  environment with and without tracing.

When the variable is unset, empty, not a decimal number or fails a check,
tracing is off and each trace point costs one load of a cached integer.
Tracing writes nothing anywhere else and never changes other output
(`tests/runtime2/rt_diff_selftest.sh` runs the corpus with tracing on vs off
and compares every channel except the trace). Lines are written whole under a
mutex, so concurrent threads never interleave within a line. If the descriptor
cannot be written, the line is dropped silently.

## Fields

| Field | Meaning |
| --- | --- |
| `ctx` | Where the operation ran: the tracked page's name during a build (`/`, `about`, …), `script` for a standalone script or the main thread of a REPL/eval, and for work started from those, `<owner>/async#N` (an async call) or `<owner>/thread#N` (a `thread(...)` worker), numbered in creation order within the owner (nested: `script/async#1/async#2`). Creation happens in program order on the owning thread, so labels are stable across runs and engines. Order is meaningful within every context; contexts are unordered relative to each other. |
| `op` | Operation (table below). |
| `arg` | Absolute, lexically normalised path (generic separators), environment variable name, or for `spawn` the program and arguments joined by single spaces. |
| `res` | Result class (table below). Never file contents or environment values. |

## Operations

| `op` | Emitted by | `res` |
| --- | --- | --- |
| `stat` | every metadata query that goes through `Parser::inspect_path` (`exists`, `is_file`, `is_dir`, `stat`, `file().exists()`, `@input` shadowing probe, prepared-path metadata) | `file`, `directory`, `symlink`, `other`, `missing` or `error` |
| `list` / `glob` | `ls()` of a directory / of a glob pattern, prepared-path directory listing | empty |
| `read` | `cat`, `open`, `file().open("r")` | byte count |
| `open` | `file(...).open(mode)` | the mode (`r`, `w`, `a`, `rw`) |
| `save`, `touch`, `remove`, `mkdir`, `copy-to`, `move-to`, `move-from` | the corresponding write operations | empty |
| `read-source` | template, content, `@input`, pagination template and separator, script import reads through the host | `ok`, `missing` or `error` |
| `read-json` | `@json` data and schema, project contracts | `ok` or `error` |
| `getenv` | `getenv`, `@getenv` | `found`, `unset` or `error` |
| `env-all` | `env()` | empty |
| `spawn` | each stage of a `run`/`cmd`/shell pipeline, and job-control launches | working directory, or `job` for a job-control launch |

## Not traced (scope of v1)

Output files written by the build engine itself (rt-diff compares them
directly), `.nift/` metadata, clocks and randomness (scripts that read them are
excluded from differential runs; see `tools/rt-diff/ignore.txt`), PATH lookups
(`which`), FFI calls, and reads the build engine performs outside the parser
(dependency status checks). A Host-op layer in the new engine (CP16) will emit
the same records from one choke point; until then this list is the contract
both engines must meet.

## Normalisations applied by rt-diff

See `docs/runtime-2/rt-diff.md` (the complete list, each with the self-test
that shows it hides nothing else).
