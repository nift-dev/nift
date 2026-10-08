# Final local certification

Runtime source is unchanged since `26ab438`. Guards/workflows were finalized at
`e80a95c`; certification results below apply to that runtime.

- Full `make -j2 test`: PASS.
- `make test-warnings`: GCC and Clang zero first-party warnings, PASS.
- Full lifetime ASan/UBSan/LSan corpus and use-after-scope canary: PASS.
- Deep sanitizer core lifecycle: PASS, 57 phases, four rounds.
- Lifetime-sanitized root/path reference smoke and six retained-pointer corruption reproducers: PASS.
- Memcheck: 31 baseline and 31 final probes, zero errors, all heaps freed.
- Independent NRS: 93/93, pinned `34b1c2ff4d1a3f591176f3ce79b74ad2958b6852`; embedding consumers enabled.
- Staged C and C++ public consumers: PASS.
- Package regression suite: 12/12, pinned `1da43659da269c96af21aea7234799d2ad56b2df`.
- Static test integrity: 290 files, zero findings.

Two local deep parser fuzz attempts timed out at `balanced-parens-100k`
(20-second existing bound), including a compiler-quiescent rerun. Neither
reported a sanitizer finding. The full unchanged hosted gate passed 1,219
cases. A separate diagnostic observation (not a gate change) completed the
identical boundary on original/final sanitized runtimes in 14.9/13.4 seconds
with matching exit and stderr. Full sequential original/final fuzz comparison
subsequently passed the unchanged gate on both: 1,219 cases each, 232 builds,
987 controlled errors, zero timeouts/crashes/findings. The 100k-parenthesis
boundary completed in 11.66/12.75 seconds. Earlier local timeout attempts are
retained transparently; no bound or assertion was weakened. The original sanitized compilation also reproduces the same
GCC 15 internal stable-sort diagnostic; it is not introduced by this tranche. Full `make test-bindings`: PASS; strict GCC/Clang native-wrapper warnings, Go including race, C# (29 tests), Node, and Python (25 tests). Native Node/Python private libffi and dependency audits pass. Final `make embed` and staged C/C++ consumers pass. No gate was weakened or suppressed.

Hosted certification initially exposed missing MSYS2 `python3` and `cmp` in
the newly wired Windows contracts. Commit `b7c5c33` adds Python and diffutils
to that runner; the runtime source and contracts are unchanged. The corrected
cross-platform run is 37766314649. This was a runner dependency failure, not
a runtime semantic failure or an assertion change.

The final strengthened cross-platform wall is **PASS** at
`6e67dd72267c2e0061dbf435b39a5b05ff3dc360`, run 37767455507: Linux, macOS,
Windows and normalized comparison all succeed. The final fixture uses native
absolute Windows paths and asserts the exact expected entries. POSIX symlink
fixtures remain enabled on native POSIX filesystems; MSYS link emulation is
excluded consistently with native Windows fixtures. No runtime behavior or
existing compatibility expectation was changed.

Final local conclusion: **PASS**. The unchanged local fuzz gate is green on
both original and final source, as is the hosted 1,219-case gate. The entire
local warning/test/binding/NRS/PRS/lifetime/deep-lifecycle certification is green.
Timing fluctuation in the earlier attempts is not evidence of a retained
regression; the complete original-baseline instruction/allocation controls and
matching clean fuzz outcomes substantiate the final result.

Fresh-cache local Go normal and race tests (`-count=1`, isolated `GOCACHE`):
PASS against the final staged native library. This removes the cached-result
ambiguity in the initial aggregate output. Hosted fresh Go/race also passes.

Final hosted conclusion: **PASS**, including clean serial bindings, parallel
`test-all`, all platform contracts and the non-destructive boundary proof.
See [selected certificates](hosted-certification.md).
