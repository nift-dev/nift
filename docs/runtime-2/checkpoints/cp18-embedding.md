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

## Resource correction and evidence

The in-process allocation campaign exposed an inherited CP09 factory leak:
`make_array(reserve)` allocated an ArrayObject before a potentially throwing
vector reserve. The same aggregate source is present in accepted CP13B and the
reviewed CP14B candidate. A separate resource-ownership commit adopts the object
into a Value before reserve; it preserves the existing bad_alloc propagation,
Error semantics, VM layout, collection method code and exception routing.
Both dispatches pass 321 compile and 16 run allocation-failure sites under
ASan/UBSan with leak detection. Restoring only the unsafe factory is detected
as a 32-byte leak in both dispatches. No temporary CP17 mechanisms are added.

Each dispatch passes 1,044 pure-C assertions and 70 focused Python assertions.
The standalone Python extension links from exactly the same 176 native sources
selected by setup.py and build.sh, and passes 28 public-package tests (25
unchanged legacy tests, three CP18 tests). The version-1.3 C smoke test passes.
The unchanged C adversarial suite has exactly one failure: its hard-coded
`nift_abi_version_minor() == 3` conflicts with the required ABI 1.4 version.
The owner was asked whether that single metadata assertion may be updated.
Until that decision arrives, retain the assertion unchanged and report this
as an unresolved acceptance gate; do not call the entire ABI suite green.
All prior C ABI layouts, declarations and definitions are unchanged apart from
the additive program section and version macros. The new public header is
byte-identical to the old header after removing that section and restoring the
two version macros.

The existing VM starts with 256 registers. Nonzero max_registers below 256 is
an invalid API argument; larger limits use accepted VM growth/budget rules.
The API does not advertise these limits as a sandbox or total process budget.
Cross-platform runs, full combined-tree certification and independent CP18
review remain outstanding. Focused evidence is standalone candidate evidence,
not permission to integrate.
