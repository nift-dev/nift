# feature-serve — `nift serve`

Status: implemented on branch `feature-serve` (from `runtime-2` @`36a8e73`).
Feature commit holds `src/Serve.{h,cpp}`, `tests/serve_smoke.sh`, this record
and the README section. CLI dispatch, help row and Makefile wiring are a
separate patch (`serve-wiring.patch`), to be re-applied after feature-site-dx
integrates, because that work owns `src/CLI.cpp` and the `Makefile`. Not
certified (`make certify` is run by the owner after integration).

## What it is

A developer-experience convenience, equivalent to

    python3 -m http.server PORT --bind HOST --directory DIR

| Option | Default | Notes |
| --- | --- | --- |
| `--port N` / `--port=N` | `8000` | integer 1–65535 (0 is rejected: the banner URL would be wrong) |
| `--host H` / `--host=H` | `127.0.0.1` | name, IPv4 or IPv6 literal; IPv6 is bracketed in the URL |
| `--dir PATH` / `--dir=PATH` | configured `output-dir` | absolute, relative or outside the project; no containment check (filesystem freedom) |

Option errors (unknown option, positional argument, missing value, repeated
option, bad port) exit 1 with an `error:` line, like `build`.

Sequence: parse options → resolve the directory → find Python 3 → bind-probe
the port (POSIX) → print the banner → hand over to Python.

- **Directory.** Without `--dir`, the project root is discovered with the same
  rule as `ProjectInfo` (walk up to a directory holding both
  `.nift/config.json` and `.nift/tracked.json`) and the config is read through
  the pure `project_read::load_config`. No `ProjectInfo::open()`, no lock, no
  tracking or watch load, nothing written. Missing output dir: `error: output
  directory does not exist: …` and `run 'nift build' first, or pass --dir PATH`.
  Outside a project with no `--dir`: `not a Nift project` plus a `--dir` hint.
  With `--dir`, no project is needed.
- **Python discovery.** `python3`, then `python` on `PATH` (Windows: then
  `py -3`), each resolved with the existing `nift_find_executable` and probed
  with `nift_run_process` (argv vector, no capture, stdout/stderr to the null
  device) running `import sys; sys.exit(0 if sys.version_info >= (3, 7) else 3)`.
  The probe is valid Python 2 syntax, so a Python 2 `python` is reported as
  "not Python 3.7 or newer" instead of crashing. 3.7 is the floor because
  `--directory` appeared in 3.7. If nothing qualifies, every candidate tried is
  listed with its reason. Nothing is installed.
- **Launch argv.** `[python, (-3), -u, -m, http.server, PORT, --bind=HOST,
  --directory=ABSDIR]`. Never a shell string. The `--opt=value` spelling stops a
  host or path starting with `-` from being read as an argparse option. `-u`
  keeps the request log unbuffered when stdout is a pipe or a log file; it does
  not change behaviour.

## Lifecycle design (owner decision)

**POSIX (Linux, macOS): check everything, then `execvp` Python.** Nift's
process image is replaced, so the server *is* the process the user started:

- Ctrl+C (terminal SIGINT to the foreground group) and SIGTERM to the PID go
  straight to Python. `http.server` handles KeyboardInterrupt and exits 0.
  SIGTERM uses the default action, giving status 143.
- There is no parent left to die, so nothing can be orphaned. No supervisor, no
  signal forwarding, no wait loop.
- The exit status is Python's own. A Python failure (for example a bind error)
  is its nonzero exit and its traceback on stderr.
- No temporary files. The version probe uses `capture=false`, so the
  `O_TMPFILE` capture path in `Process.cpp` is never used, and the exec needs
  none. Python's request log (stderr) and banner (stdout) go to the user's
  terminal.
- Signal dispositions are inherited as-is. A user or shell that ignores
  SIGINT (for example a background job in a non-interactive shell) gets a
  server that also ignores it, as with any exec'd program. The smoke test uses
  `set -m` for that reason.

**Port probe (POSIX).** Before exec, Nift resolves the host with
`getaddrinfo(AI_PASSIVE)`, takes the first result (as `http.server` does), sets
`SO_REUSEADDR` and, for IPv6, `IPV6_V6ONLY=0` (as `http.server`'s dual-stack
server does), and binds. Errors: in use (`port N is already in use on H`, with a
`--port` hint), `EACCES` (privileged port hint), `EADDRNOTAVAIL`, unresolvable
host, and address family unsupported. The probe socket is `FD_CLOEXEC` and is
closed before exec. A never-listened socket leaves no `TIME_WAIT`. The smoke
test checks that the running server holds exactly one socket (fd ≥ 3).

*Race.* A free port at probe time is not a guarantee. Something can bind it
before Python does. That is acceptable for a development command, because
Python is then the process: its `OSError: [Errno 98] Address already in use`
traceback reaches the user's stderr and its status is nonzero. The race is
tested deterministically (test 10): a `python3` wrapper on `PATH` occupies the
port only when asked to run `http.server`, which is after Nift's probe, and
then execs the real interpreter. The test asserts that the banner was printed,
the status is nonzero (1) and `Address already in use` is on stderr.

**Windows: launch-and-wait** through the existing `nift_run_process` (direct
`CreateProcessW` with an argv-built command line, inherited console, no
capture), returning Python's exit code. There is no port probe on Windows,
which avoids adding Winsock and `ws2_32` linkage. A busy port shows as Python's
own bind error and a nonzero exit, and Nift adds `error: Python exited with
status N` plus a `--port` hint. Ctrl+C/Ctrl+Break reach every process on the
console: Nift installs a console control *handler* that returns TRUE (it keeps
waiting). Python receives the event, exits 0, and Nift returns 0. A handler is
used rather than `SetConsoleCtrlHandler(NULL, TRUE)` because that ignore flag
is inherited by children and would stop Python seeing Ctrl+C. Closing the
console window terminates both processes. Known limit, kept simple by owner
decision: if Nift alone is force-killed (Task Manager / `taskkill /F` on Nift's
PID), Python keeps running. A kill-on-close job object would close that gap
in about 8 lines if wanted later.

## Not in scope

No C++ HTTP server, no live reload, watching or rebuild, no proxy or HTTPS, no
new dependencies. A future `--watch` could run `build --auto` alongside the
server; it is only an idea here.

## Tests

`tests/serve_smoke.sh` (`make test-serve` after the wiring patch; added to
`make test`). It is Linux-only, every wait is bounded (≤10 s), an EXIT trap
kills anything started, and a final sweep checks that no PID survives. It
runs from a project subdirectory to exercise root discovery.

| # | Check |
| --- | --- |
| 1 | default output dir + custom port: banner text, byte-exact fetch of a 4 KiB random file and `index.html` via urllib, request log on stderr, `/proc/PID/comm` is `python3` (exec'd), exactly one socket and no temp/unlinked files held, SIGINT → status 0, nothing listening, no process |
| 2 | SIGTERM → status 143, no orphan, port released |
| 3 | `--dir=` outside the project with spaces in the path and file name, `--host 127.0.0.2`; not reachable on 127.0.0.1 |
| 4 | no project + relative `--dir` served; no project and no `--dir` → clear error |
| 5 | IPv6 `::1` serve + bracketed URL (skipped where the kernel has no IPv6) |
| 6 | missing output dir (on a copy of the project) → error + `nift build` hint; missing `--dir`; `--dir` is a file |
| 7 | ports 0, 65536, `abc`, `-1`, empty; unknown option; repeated option; missing value; unresolvable host |
| 8 | `PATH` without Python → error listing tried candidates, no banner; Python-2-only `PATH` (fake `python` exiting 3) → rejected |
| 9 | occupied port → Nift error before launch, no banner |
| 10 | port taken after the probe → Python's bind error visible, nonzero status |
| 11 | served dir deleted while running → 404, server stays up, clean SIGINT stop |
| 12 | project tree hash (names, sizes, mtimes, modes, SHA-256 of contents, `.nift` included) identical before and after all serve runs |

### Results (Linux x86_64 only)

- `bash tests/serve_smoke.sh`: 33 passed, 0 failed (final version run twice). The
  IPv6 case was **skipped**: this host's kernel has no IPv6, and
  `--host ::1` gives `error: cannot create a socket for host '::1':
  Address family not supported by protocol` (exit 1).
- Manual: real-terminal Ctrl+C through a pty → `^C`, Python's "Keyboard
  interrupt received, exiting.", exit 0, port closed, no process left. Symlinked
  `--dir`, a dir named `-dashdir`, a trailing-slash dir, `--host=-x` (resolve
  error), a non-local address (`EADDRNOTAVAIL` message), and
  `--platform=native serve` (rejected by the existing selector rule).
- `tests/commands_smoke.sh`: passed (help output changed).
- `src/Serve.cpp` builds warning-free with g++ (`-Wall -Wextra -pedantic`,
  also `-Wshadow -Wconversion -Werror`) and clang++ (`-Wall -Wextra -pedantic
  -Werror`).

**Not tested:** macOS (expected identical to Linux: same exec path, BSD
sockets), Windows (no MinGW toolchain or Windows headers on this host, so the
`_WIN32` branch is **not compiled** here), and IPv6 end-to-end. These are for
the next cross-platform milestone.

## Website

The public website source (`nift-dev.github.io`, `content/docs/commands.html`)
is not available in this environment. The in-repo user docs are the README
"Previewing locally with `nift serve`" section (build then serve, Python 3
required, development only). Release-coupled website item for the queue once
it exists on the integration branch: add a `nift serve` entry to
`content/docs/commands.html` with the same three points, earliest supporting
version = the release containing this feature.
