# VM refusal: entry points and propagation

Runtime 2.0 VM-default policy (`NIFT_VM` unset or `require`). A refusal is
always reported with the full positioned diagnostic (location, feature, stable
refusal id, what was not executed, the `NIFT_VM=0 nift ...` instruction, the
issues URL) and is never followed by a silent legacy fallback.

"Interactive" means stdin is a terminal (`isatty`, the test the shell loop
already uses to pick its line reader). Invariant for shell lines: once a VM
refusal is reported while a logical command line runs, no later segment of
that line executes (one check at the head of the segment loop in
`execute_shell_command`).

Witnesses are checks in `tests/runtime2/vm_default/vm_default.py` (section
number, `§`) unless another file is named.

| Path | Scope a refusal aborts | Exit / prompt | Witness |
|---|---|---|---|
| Script file (`nift f.f`, shebang) | Whole program (nothing runs; imports included) | Exit 1 | §2 "unsupported script refused with the full diagnostic"; §9 module refusal |
| `-e` / `-c` program | Whole program | Exit 1 | §5 "-e program refused by default before effects" |
| `-` (program on stdin) | Whole program | Exit 1 | §19 "`-` stdin program: refused as a whole before effects" |
| `nift eval` | Whole expression | Exit 1 | §5 "eval refused by default" |
| Piped session (`nift`, `nift -i` reading a pipe) | Current statement; the session stops there (earlier effects kept, later input not run, resources finalised and reported) | Exit 1 | §7 piped-session checks; §14 unsaved-file reporting; `cp19/session_refusal_cli.py`, `cp19/resource_cli.py`, `cp19/jobs_cli.py` |
| `-i file` / `-i -e` without a tty | The initial unit (nothing of it runs); the session stops | Exit 1 | §7 "-i script ..." and "-i -e ..."; §15 "... piped: the session still stops with exit 1" |
| `~/.niftrc` without a tty | The whole file (nothing of it runs); the session stops | Exit 1 | §15 "~/.niftrc piped ..." |
| Typed tty REPL | Current statement only (transactional: no definition or effect) | Back at the prompt; session usable | §7 "tty REPL: refusal reported ..."; `cp19/session_refusal_cli.py` pty runs; `cp23/cp19_combined.py` job-refusal-tty |
| `~/.niftrc` on a tty | Fed through the shell loop exactly as typed input (the REPL's own statement accumulation): the first refused input and the rest of the file; earlier input ran once | Prompt; session usable | §15 "~/.niftrc on a tty ..."; §18 "~/.niftrc on a tty: ..."; §18b multi-line |
| `-i file` on a tty | The same path as `~/.niftrc` | Prompt; session usable | §15 "nift -i file on a tty ..."; §18 "nift -i file on a tty: ..."; §18b multi-line |
| `-i -e` / `-i -c` on a tty | The initial unit (nothing of it runs); the session stops | Exit 1 | §17c "-i -e on a tty: ..." |
| Shell-form user function / builtin (`f 3`, `pwd a b`) | Logical command line (no later `;` / `&&` / `\|\|` segment) | Piped: session stops, exit 1. Tty: prompt | §16 (piped and tty, `\|\|`, `;`, `&&`) |
| `$[...]` interpolation, incl. `;`, `&&`, `\|\|`, pipelines, redirects, `&` | Logical command line (the containing command and every later segment) | Piped: session stops, exit 1. Tty: prompt | §12; §13 (`;`, `\|\|`, `&&`, `;` then `\|\|`, pipeline stages); §19 (redirect, `&`) |
| `$[...]` calling a function from an earlier unit | Logical command line; located at the defining unit (`~/.niftrc`/`-i` path and position, or labelled earlier stdin input) | Piped: exit 1. Tty: prompt | §17b "$[...] calling an earlier-stdin function ..." / "... a ~/.niftrc function ..." |
| `--platform=<non-native>` | Same as the entry point used (the VM honours the selection) | Same as the entry point | §8 "--platform: unsupported script file / -e / stdin program refused ..."; "--platform REPL ..." |
| Build-hook child `nift child.f` | The child's whole program (website build and rendering unaffected) | Child exits 1 to the hook; build continues | §11 "build hook: a child `nift child.f` is refused ..." |
| `NIFT_VM=1` (fallback mode) | Files, `-e`, `-`, eval: falls back to legacy (no refusal). Sessions are VM-owned: as the default session rows | Files: legacy exit status. Piped session: exit 1. Tty: prompt | §4 "NIFT_VM=1 still falls back"; §19 "NIFT_VM=1 piped session ..."; `cp19/session_refusal_cli.py` (mode 1) |
| `NIFT_VM=0` | No refusals: the legacy interpreter runs everything | Legacy behaviour (unchanged) | §3; §5 "-e program runs on legacy under NIFT_VM=0"; §7 "NIFT_VM=0 piped session is legacy and unchanged" |
| Invalid `NIFT_VM` (empty, `off`, ...) | Configuration error before anything runs, on every scripting entry point | Exit 1 | §10 |
