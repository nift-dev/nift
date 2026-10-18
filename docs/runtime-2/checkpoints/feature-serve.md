# feature-serve — `nift serve`

Status: implemented on branch `feature-serve` (from `runtime-2` @`36a8e73`),
revised after an independent adversarial review. Not certified (`make certify`
is run by the owner after integration).

Where things live:

- **Feature commits:** `src/Serve.{h,cpp}`, `tests/serve_smoke.sh`,
  `tests/serve_windows_lifecycle.ps1`, this record and the README section.
- **Wiring commit:** CLI dispatch and help row (`src/CLI.cpp`), Makefile
  (sources, `-lws2_32` on Windows, the `test-serve` target) and the CI steps
  (`.github/workflows/checkpoint-10-cross-platform.yml`). Before feature-site-dx
  integrated, this was a local "WIP wiring" commit plus a scratch patch. It is
  now an ordinary commit on top of `runtime-2` @`8957559`.

## What it is

A developer-experience convenience, equivalent to

    python3 -m http.server PORT --bind HOST --directory DIR

| Option | Default | Notes |
| --- | --- | --- |
| `--port N` / `--port=N` | `8000` | integer 1–65535 (0 is rejected: the banner URL would be wrong) |
| `--host H` / `--host=H` | `127.0.0.1` | name, IPv4 or IPv6 literal; IPv6 is bracketed in the URL |
| `--dir PATH` / `--dir=PATH` | configured `output-dir` | absolute, relative or outside the project; no containment check (filesystem freedom) |
| `--help` / `-h` | | usage, exit 0 |

Option errors exit 1 with an `error:` line, like `build`. They are: an unknown
option, a positional argument, a missing value, a repeated option and a bad
port. A separate value starting with `--` counts as missing, so
`--dir --port 8000` is an error; use `--dir=--name` for such a directory.

Sequence: parse the options, resolve the directory, find Python, probe the
port, print the banner, then hand over to Python.

- **Directory.** Without `--dir`, the project root is discovered with the same
  rule as `ProjectInfo`: walk up to a directory holding both
  `.nift/config.json` and `.nift/tracked.json`. The config is read through the
  pure `project_read::load_config`. There is no `ProjectInfo::open()`, no lock
  and no tracking or watch load, and nothing is written. The path is made
  absolute **without lexical normalisation**. After the existence check it is
  replaced by `fs::canonical`, so `link/../www` means the parent of the link's
  target, exactly as the kernel resolves it, and the canonical path is what is
  both served and shown. This applies to `--dir` and to a configured
  `output-dir`. Errors:
  - a missing output dir gives `error: output directory does not exist: …` and
    `run 'nift build' first, or pass --dir PATH`;
  - outside a project with no `--dir`, `not a Nift project` plus a `--dir` hint;
  - with `--dir`, no project is needed.
- **Python discovery.** On POSIX the order is `python3`, then `python`. On
  Windows it is `py -3` first, then `python3` and `python`, and **only `.exe`
  files are accepted**. A `.bat`/`.cmd` shim would route the argv through
  `cmd.exe`, where `&`, `|` and `%` in paths are interpreted. Shims are skipped
  with a note, and if nothing else is found the error says to put a
  `python.exe` on PATH or install the py launcher.

  Each candidate is resolved with the existing `nift_find_executable` and
  version-probed with `-E -s -B -c "import sys; sys.exit(0 if
  sys.version_info >= (3, 8) else 3)"`. The probe is valid Python 2 syntax, so
  Python 2 is reported as "not Python 3.8 or newer". The **floor is 3.8**:
  3.7 lacks `http.server`'s address-family selection for an IPv6 `--bind` and
  is end-of-life.
  - **POSIX probe:** a small local fork/exec, not `nift_run_process`, which
    has no timeout. stdin and stdout go to `/dev/null` and stderr comes back
    through a pipe. It is **bounded to 5 s**, then SIGKILLed and reported as
    `did not respond to a version check within 5s`. On failure the
    interpreter's stderr is shown, so pyenv shim messages are visible. On
    Linux the probe is spawned with `PR_SET_PDEATHSIG(SIGKILL)`, so it dies
    if Nift is killed during the probe.
  - **Residual windows:** on macOS, a Nift killed with SIGKILL during the probe
    leaves the probe until it exits. A terminal Ctrl+C still reaches both,
    because they are in the same process group. The Windows probe uses the
    existing launcher with no timeout; it does die with Nift through the
    kill-on-close job, which is joined before probing.

  Nothing is ever installed.
- **Launch argv.** `[python, (-3), -I, -B, -u, -m, http.server, PORT,
  --bind=HOST, --directory=CANONICALDIR]`, never a shell string.
  - `-I` (isolated) keeps the cwd and project off `sys.path`, so a
    `socketserver.py` or `mimetypes.py` there cannot shadow the standard
    library. It also ignores `PYTHON*` variables and user site-packages.
  - `-B` means no `__pycache__` is written anywhere.
  - `-u` keeps the request log unbuffered through pipes.
  - The `--opt=value` spelling stops a host or path starting with `-` from
    being read as an argparse option.

## Lifecycle design (owner decision)

**POSIX (Linux, macOS): check everything, then `execvp` Python.** Nift's
process image is replaced, so the server *is* the process the user started:

- Ctrl+C (terminal SIGINT) and SIGTERM go straight to Python. On Ctrl+C,
  `http.server` exits 0; SIGTERM gives 143.
- A force-kill (SIGKILL) kills the server itself (137). Nothing can be
  orphaned, because there is no second process.
- There is no supervisor, signal forwarding or wait loop. The exit status is
  Python's own.
- No temporary files are created, and Python's request log (stderr) and banner
  (stdout) go to the user's terminal.
- Inherited signal dispositions are kept: a shell that ignores SIGINT (for
  example a background job in a non-interactive shell) gets a server that
  ignores it, as with any exec'd program. The smoke test uses `set -m` for this
  reason.

**Windows: launch-and-wait** through the existing `nift_run_process` (direct
`CreateProcessW` with an argv-built command line, inherited console, no
capture). It returns Python's exit code.

- **Ctrl+C/Ctrl+Break** reach every process on the console. Nift installs a
  console control *handler* that returns TRUE and keeps waiting. It uses a
  handler rather than `SetConsoleCtrlHandler(NULL, TRUE)` because that ignore
  flag is inherited and would stop Python seeing Ctrl+C. Results:
  - Ctrl+C: Python gets KeyboardInterrupt, exits 0, and Nift returns 0.
  - Ctrl+Break: Python is terminated with `STATUS_CONTROL_C_EXIT`
    (0xC000013A). Nift treats that as a normal interrupted exit, returns 130
    and prints no port hint.
  - Any other nonzero status gets `error: Python exited with status N` plus a
    `--port` hint.
  - Closing the console terminates both processes.
- **Force-kill.** Before probing, Nift creates a job object with
  `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` and assigns *itself* to it, because
  `nift_run_process` exposes no child handle. The probes and Python, including
  the real `python.exe` below a `py.exe` launcher, inherit the membership.
  Nift never closes the handle, so when Nift dies for any reason, including
  `taskkill /F`, the handle closes and Windows kills Python.
  - **No-orphans on Windows holds only when the job was established.** If
    creating, configuring or assigning the job fails (for example an enclosing
    job that forbids nesting on Windows before 8), serve still runs. It prints
    `nift serve: warning: could not arrange for the server to stop if nift is
    force-killed (<reason>); stop it with Ctrl+C`. In that case a force-killed
    Nift leaves Python running, though Ctrl+C and closing the console still
    stop it.
  - This is about 20 lines with no monitoring thread or signal forwarding, so
    it is not supervision machinery.

**Port probe (all platforms, best effort and racy).** Before launching, Nift
resolves the host with `getaddrinfo(AI_PASSIVE)` and takes the first result,
as `http.server` does. For IPv6 it sets `IPV6_V6ONLY=0`, matching the
dual-stack server, then binds.

- **POSIX** uses `SO_REUSEADDR`, as Python does, so a lingering `TIME_WAIT`
  does not block either side.
- **Windows** uses `SO_EXCLUSIVEADDRUSE` through Winsock, with
  `-lws2_32` added in the wiring. Python's own `SO_REUSEADDR` on Windows would
  otherwise let it share a port another server holds, so without this a busy
  port would go unnoticed.

Errors: port in use (with a `--port` hint), permission denied or reserved
port, address not available, unresolvable host, and unsupported address
family. The probe socket is closed before launch; on POSIX it is also
`FD_CLOEXEC`, and the smoke test checks the server holds exactly one socket
(fd ≥ 3).

*Race.* The probe is best effort on every platform. A port that is free at
probe time can be taken before Python binds it. Python's bind error then
reaches the user's stderr and its status is nonzero. Test 12 covers this
deterministically: a `python3` wrapper occupies the port only when asked to
run `http.server`, after Nift's probe, then execs the real interpreter. It
gets status 1 with `Address already in use` on stderr.

## Not in scope

No C++ HTTP server, no live reload, watching or rebuild, no proxy or HTTPS, no
new dependencies. A future `--watch` could run `build --auto` alongside the
server; it is only an idea here.

## Tests

### `tests/serve_smoke.sh` (`make test-serve`, in `make test` via the wiring)

Linux runs everything. macOS runs the portable lifecycle core (1–3) and
reports checks 4–15 as SKIPPED; they are counted as skipped, never as passed.
Other systems, or a host without `python3`, exit 77, the project's honest-skip
status, which `make test-serve` reports as `test-serve: SKIPPED`. The script
runs under `set -o pipefail`. Exit statuses that legitimately mean "no match"
(`grep` 1, `pgrep` 1) are handled explicitly, and any other status fails the
check. A failure of `find`, `stat` or `sha256sum` in the tree hash fails the
test. Every wait is bounded, an EXIT trap kills anything
started, and a final sweep checks that no PID survives. The test runs from a
project subdirectory to exercise root discovery.

| # | Check |
| --- | --- |
| 1 | default output dir + custom port: banner, byte-exact fetch of a 4 KiB random file and `index.html`, request log on stderr, SIGINT → 0, port released, no process. Linux also checks `/proc`: exec'd (`comm` = `python3`), one socket, no temp/unlinked files |
| 2 | SIGTERM → 143, no orphan, port released |
| 3 | force-kill (SIGKILL to the PID the user started) after a fetch → 137, no Python left, port free within 5 s |
| 4 | `--dir=` outside the project with spaces in the path and file name, `--host 127.0.0.2`, while the test itself holds `127.0.0.1:P` (so only a specific 127.0.0.2 bind can succeed) |
| 5 | no project + relative `--dir` (stop status checked); no project and no `--dir` → error |
| 6 | IPv6 `::1` + bracketed URL (stop status checked; skipped without kernel IPv6) |
| 7 | `--dir link/../www` and configured `output-dir: link/../out/` serve, and show, the kernel-resolved dir |
| 8 | missing output dir → `nift build` hint; missing `--dir`; `--dir` is a file |
| 9 | ports 0, 65536, `abc`, `-1`, empty (each failure recorded); unknown, repeated and missing-value options; `--dir --port 8000`; `--help`, `-h`; unresolvable host |
| 10 | no Python on PATH; Python-2-only `python`; pyenv-style failing shim (its stderr is shown); a hanging `python3` → error within 5 s and the probe killed; Nift SIGKILLed mid-probe → probe dies (PDEATHSIG) |
| 11 | occupied port → Nift error before launch, no banner |
| 12 | port taken after the probe → Python's bind error visible, nonzero status |
| 13 | served dir deleted while running → 404, server stays up, clean stop |
| 14 | `socketserver.py`/`mimetypes.py` in the cwd and on `PYTHONPATH` are not imported; no `__pycache__` in the cwd or project |
| 15 | project tree hash (names, sizes, mtimes, modes, SHA-256 of contents, `.nift` included) unchanged |

### `tests/serve_windows_lifecycle.ps1` (Windows CI, pwsh)

1. Start `nift serve` on a free port with a `--dir` containing a space, fetch
   and compare bytes, and confirm a Python child exists and no job warning was
   printed. Then `Stop-Process -Force` Nift (TerminateProcess, like
   `taskkill /F`), and require every descendant gone and the port free within
   5 s.
2. Ctrl+Break: a Python helper starts Nift with `CREATE_NEW_PROCESS_GROUP`,
   waits for a fetch, sends `CTRL_BREAK_EVENT` to the group, and requires
   exit 130, no Python left and the port free.

Ctrl+C is **not** tested in CI. Windows disables `CTRL_C_EVENT` for a new
process group, and sending it to the runner's shared console group would
interrupt the CI shell itself. Ctrl+C uses the same console delivery as
Ctrl+Break and Python handles it as KeyboardInterrupt (exit 0).

The script is wired into the Windows leg of `checkpoint-10-cross-platform.yml`
(in the wiring commit), and `make test-serve` into the Linux and macOS legs. Both
run for the first time at the next cross-platform milestone.

### Results actually run (Linux x86_64 only)

After the rebase onto `runtime-2` @`8957559`, with the wiring commit applied
and run through the shared heavy-job gate:

- `bash tests/serve_smoke.sh` (via `make test-serve`): **49 passed, 1 skipped
  (IPv6), 0 failed**. `make test-test-integrity` is clean. The skip paths were
  checked with a fake `uname`: an unknown OS gives exit 77 (and `make
  test-serve` prints SKIPPED), no `python3` gives exit 77, and "Darwin" runs
  the portable core (12 passed) with checks 4–15 reported as SKIPPED. That is
  a simulation on Linux, not a macOS run. IPv6 was **skipped**:
  this host's kernel has no IPv6, and `--host ::1` gives `error: cannot create
  a socket for host '::1': Address family not supported by protocol`.
- Manual confirmations:
  - `python3 -B -m http.server` run from a directory containing a
    `socketserver.py` executes it (exit 9 in the check), and adding `-I`
    prevents it. Test 14 is therefore meaningful.
  - A real-terminal Ctrl+C through a pty gives exit 0 with the port closed
    (first revision; the exec path is unchanged).
- `tests/commands_smoke.sh`: passed.
- `make test-site-dx`: all checks passed.
- `make test-process-hardening`: PASS, 19 cases.
- `src/Serve.cpp` compiles warning-free with g++ and clang++ (`-Wall -Wextra
  -pedantic -Wshadow -Werror`). It was built and linked into `nift` with the
  repo flags.

**Not run here:** macOS (the core of `serve_smoke.sh`), Windows (no MinGW
toolchain, Windows headers or PowerShell on this host, so the `_WIN32` code
(Winsock probe, job object, `.exe` filter, Ctrl+Break mapping) is **neither
compiled nor run**, and `serve_windows_lifecycle.ps1` has never executed), and
IPv6 end-to-end. These are for the next cross-platform milestone.

## Integration notes

Rebased onto `runtime-2` @`8957559` (feature-site-dx integrated). The former
WIP wiring commit was dropped and the wiring re-applied as an ordinary commit:
CLI dispatch and help row, Makefile (`src/Serve.cpp`, Windows `-lws2_32`,
`test-serve` in `make test`), and the CI steps. It applied cleanly on top of
site-dx's `src/CLI.cpp` and `Makefile`.

- **site-dx `src/Process.cpp`.** `nift serve` is unaffected on POSIX: the
  version probe is its own fork/exec and the server is exec'd directly. On
  Windows, the probe and the launch use `nift_run_process`, and now benefit
  from site-dx's wording. A program that is missing or not permitted gives
  `launched=false` with `executable not found: …` / `permission denied: …`,
  which `nift serve` prints after `cannot start`. A single command has no
  pipeline, so the per-stage results are not needed.
- **Non-executable `python3` on PATH.** This used to be reported as "not
  found", because `nift_find_executable` (shared with the shell and
  `which()`) skips files without execute permission. Changing that lookup
  would change PATH resolution for every caller, so the fix is local to
  `src/Serve.cpp`. After a miss, PATH is scanned for an existing non-executable
  file of that name, and it is reported as
  `tried python3 (/path/python3): not executable`. Test 10 covers it.
- The `-lws2_32` link flag is added to the global Windows `LDLIBS`, because
  `src/Serve.o` is part of every core-object link set (engine and C ABI tests,
  `libnift_c`). On Windows those links need Winsock too.
- The website item is now queued in `docs/handover/PENDING-WEBSITE.md`.

## Website

The public website source (`nift-dev.github.io`, `content/docs/commands.html`)
is not available in this environment. The in-repo user docs are the README
section "Previewing locally with `nift serve`" (build then serve, Python 3.8+
required, development only).

Queued in `docs/handover/PENDING-WEBSITE.md` ("Local preview server
(`nift serve`)"): a `nift serve` entry in `content/docs/commands.html` and the
getting-started page, with Python 3.8+ required, local development only, and
`nift build` shown before it.
