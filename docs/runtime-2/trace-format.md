# Host-operation trace format (CP08)

Set `NIFT_TRACE_HOST=<fd>` to an open, writable file descriptor and every traced
Host operation appends one JSON object per line to it:

```
NIFT_TRACE_HOST=3 nift build --all 3>trace.jsonl
{"ctx":"/","op":"read-source","arg":"/abs/site/templates/template.html","res":"ok"}
```

The variable is read once per process (`src/HostTrace.h`). When it is unset,
empty or not a decimal number, tracing is off and each trace point costs one
load of a cached integer. Tracing writes nothing anywhere else and never changes
other output. Lines are written whole under a mutex, so concurrent threads never
interleave within a line. If the descriptor cannot be written, the line is
dropped silently.

## Fields

| Field | Meaning |
| --- | --- |
| `ctx` | Where the operation ran: the tracked page's name during a build (`/`, `about`, …), `script` for a standalone script or the main thread of a REPL/eval, `async` for the async worker pool, `thread` for `thread(...)` workers. Order is meaningful within `script` and page contexts; `async` and `thread` operations are unordered relative to each other and to the page. |
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

rt-diff replaces exactly these before comparing stdout, stderr, output files
and traces:

1. the entry's working copy path (and its `realpath`) → `$ROOT`;
2. the run's HOME/TMPDIR directory → `$TMP`, and their common parent → `$BASE`;
3. `time taken: <number> seconds` → `time taken: $T seconds`.

Eval entries' JSON output is parsed and re-serialised with sorted keys. Nothing
else is normalised.
