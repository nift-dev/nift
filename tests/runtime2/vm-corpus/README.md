# VM regression corpus

Golden stdout/stderr/exit code from the canonical (legacy textual) evaluator.
`tests/runtime2/vm_corpus.sh` (`make test-runtime2-vm-corpus`) runs every
`*/*.f` and compares byte for byte. Regenerate a golden file only when
`docs/runtime-2/semantics.md` records an intentional change.

## cp04/
44 probes written while trying to run top-level code on the loop path's
prepared evaluator (CP04, deferred by D-006). The CP04 experiment fails 5 of
them; v4.12.0 and runtime-2 pass all. Further divergences the CP04 experiment
exposed, already covered by existing suites (a new engine must keep them):

| Behaviour | Existing test |
| --- | --- |
| `error("x").message` | test-v410-native-dispatch-parity |
| struct member/reference resolution | test-v42-struct |
| map equality | test-v48-map-set-index |
| embedded-script host binding `source` | test-cp18-bytes |
| warning output with embedded newline | test-v48-warn |
| binding-type error wording | test-call-semantics |
| glob, file-buffer, filesystem recipe, sort factory, object member, callback, identifier parity | test-v410-*-parity, test-v48-*-parity |

Loop-path behaviours that differ from canonical today (documented, not yet
fixed; the new engine must follow canonical semantics unless `semantics.md`
says otherwise): `"$[x]"` inside strings in loops is not interpolated; `const`
inside loops is mutable; a user function named like a builtin wins inside
loops; some assignment/return error wording and error locations differ.
