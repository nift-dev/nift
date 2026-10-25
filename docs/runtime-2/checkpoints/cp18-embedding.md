# CP18 embedding candidate

Development base: `70fda90227b31f6c7e5ea4ede4b9451047e94c17`, tree
`ad66b69f72494c036a5987837f71aa871f9ede20` (accepted CP13B).
This isolated candidate does not modify Claude's integration checkout.

The checkpoint ledger specifies ABI 1.4 compile/run and C/Python tests, but
contains no frozen C signatures. The additive interface in `nift/c_abi.h` is
therefore a proposed implementation surface for independent review. Historical
CP18 byte, asynchronous and shell checkpoints are different projects; their
existing tests do not implement this Runtime 2.0 program API.

`nift_program_compile` copies a TEXT source and its diagnostic name, validates
it using CP13, and compiles the complete accepted VM subset. A syntax rejection
or unsupported construct returns a semantic result without a program. Compile
never executes a prefix, invokes a host callback, loads a source path or falls
back to the legacy engine. Static imports remain refused by the existing
compiler: adding embedding loader/resolver seams is outstanding scope, not a
temporary CP17 interface. This is explicit in the public header and tests.

`nift_program_run` uses the compiled immutable program and a fresh VM. SourceMap
and constants belong to the program; copied output, return text and diagnostic
metadata belong to each result. A result may outlive the program and other
runs. Runtime Error code, catchability and actual origin are copied from
accepted CP13B VmResult fields without modifying any script-visible Error.
Mechanical ABI failures return existing status values; semantic stops return
NIFT_OK plus a typed outcome. Exceptions never cross the C boundary. A failed
run does not poison the reusable program. There is no streaming callback or
process-global stdout/stderr redirection; print output stays in the VM buffer.

The accepted heap uses non-atomic refcounts and thread-local accounting.
Programs must compile/run/free on the same living thread, with no concurrent
operations. Wrong-thread run/free is rejected before touching VM values.
Programs must be freed before the compiling thread exits; CP23 isolate transfer
and thread retirement are not implemented here. Results contain only ordinary
copied storage. Zero resource limits select existing VM defaults; VM limits do
not bound compiler memory, host allocations or captured output.

Python `Program`/`ProgramResult` expose the same surface, with copied results,
context-manager ownership, explicit close and pre-call thread checks. The
installed native extension supplies ABI symbols; an explicit library argument
supports standalone ABI installations. Python packaging adds the full VM and
compiler to the existing staged source build. Existing Engine execution,
contexts, loader callbacks and ABI 1.3 layouts/signatures remain unchanged.

No CP17 handle table, file-handle ownership, CP14B collection implementation,
Frame/Fault/Handler layout, exception routing or ordinary call entry is changed.
File handles and FFI remain refused according to the accepted compiler; their
lifting belongs to their existing owners and eventual combined-tree review.
The current API has no dynamic host-object or Engine-context injection; those
cannot be represented safely by borrowing legacy runtime values.

Permanent C/Python gates, both dispatch variants and sanitizer allocation
controls are reachable through `test-embed` / `certify-targets`. Standalone
acceptance is distinct from combined-tree integration. No integration, push,
CI, release or spending is authorized. Cross-platform evidence is pending.
