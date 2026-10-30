"""Runtime 2.0 CP12: the approved legacy/VM difference policies (shared).

Every differential harness (vm_adversarial.py, vm_parity.py,
vm_engine_selection.py, vm_former_replay.py, vm_d018.py, rt-diff's class
rule) compares a legacy run with a NIFT_VM=1 run through classify(). A run
pair is either identical or matches one of these named policies; anything
else is a difference that fails the gate.

Review round 2: the VM never vouches for its own output. No policy reads
the VM's statistics as evidence of correctness; what a policy approves is
checked against legacy and against the program itself (its CP10 AST and
exact Python evaluation of constant operands, tests/runtime2/vm_sites.py).

  D-001#4 recursion   legacy fails with "callable recursion depth exceeded"
                      at its 64-frame limit, its output a prefix of the VM's;
                      the VM completed or stopped with a program error.
  D-040 independent copy  (CP14B integration, owner 2026-10-25) exactly the
                      programs pinned in d040_witnesses.py: exact source
                      hash, exact legacy and VM (stdout, stderr, exit), and
                      one VM execution with no legacy execution. Never a
                      general alias-difference rule.
  D-018c diagnostics  errors reported where they occur (D-009#1, #5): same
                      stdout, both exit 1 with one diagnostic on the same
                      file; messages equal once legacy's "return: " prefixes
                      are removed, or legacy's loop wording "invalid index" /
                      "invalid index target" with the canonical message whose
                      every field (index, key, expression) equals what the
                      source gives at the failing node (a constant; otherwise
                      not approved); the VM's position equal to the failing
                      node derived independently: the unique node that can
                      raise the message within the statement legacy reports
                      (or, where legacy reports a loop statement or a call
                      site, within the program), constant operands evaluated
                      exactly to find which operation fails.

No D-018a (numbers) policy and no deleted-line rule: generated programs keep
numbers beyond 2^53 out of code legacy runs on its loop evaluator, and the
real programs that need D-018a are pinned with independently computed
results (vm_golden.py, vm_review_cp12.py, vm_d018.py). Loop-code
assignments whose binding-type check could fail are refused at compile time
(context:assignment-error), so legacy's loop wording "cannot change binding
type" is never approved. tests/runtime2/vm_policy_negative.py pins the
rejection of the reviewer's synthetic bugs (mask.py, mask2.py).

classify(legacy, vm, st, source=None): `source` defaults to st["_source"] or
the file at the statistics' `path`.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vm_sites  # noqa: E402
import d040_witnesses  # noqa: E402

# Every consumer needs the independent AST oracle, including direct invocations.
vm_sites.require_tool()

RECURSION_ERROR = re.compile(rb"^error: [^\n]*: callable recursion depth exceeded: [A-Za-z_][A-Za-z0-9_]*\n\Z")
DIAGNOSTIC = re.compile(rb"^error: (.*):(\d+):(\d+): ([^\n]*)\n\Z")
RETURN_PREFIX = b"return: "
TASK_CONTEXTS = (b"thread: ", b"future: ")

POLICIES = ("D-001#4 recursion", "D-018c diagnostics", "D-009#5 return prefix", "D-040 independent copy",
            "D-018c interpolation", "CP23 thread host-error context")


IMPORT_PREFIX = b"import: "


def _strip_return(message):
    # CP12b modules: legacy's `import: ` prefixes (one per active import,
    # reproduced by the VM) come first; `return: ` ones follow them.
    imports = b""
    while message.startswith(IMPORT_PREFIX):
        imports += IMPORT_PREFIX
        message = message[len(IMPORT_PREFIX):]
    while message.startswith(RETURN_PREFIX):
        message = message[len(RETURN_PREFIX):]
    return imports + message


def _split_imports(message):
    count = 0
    while message.startswith(IMPORT_PREFIX):
        count += 1
        message = message[len(IMPORT_PREFIX):]
    return count, message


def _source_bytes(source, st):
    if source is None and st:
        source = st.get("_source")
    if source is None and st and st.get("path") and os.path.isfile(st["path"]):
        with open(st["path"], "rb") as f:
            source = f.read()
    if isinstance(source, str):
        source = source.encode()
    return source


def _fields_ok(source, root, legacy_msg, vm_msg, site):
    """Legacy's loop wording omits fields the canonical message carries: each
    must equal what the source says at the failing node (a constant)."""
    node = None
    for n in root.walk():
        if vm_sites.position(source, n.begin) == site and n.kind in ("Index", "Assign"):
            node = n if node is None or n.end > node.end else node
    if legacy_msg == b"invalid index":
        if node is None or node.kind != "Index":
            return False
        key = vm_sites.constant(source, root, node.children[1])
        m = re.match(rb"^JSON array index (\d+) is out of range in '", vm_msg)
        if m:
            return isinstance(key, (int, float)) and key >= 0 and key == int(m.group(1))
        if vm_msg.startswith(b"JSON array indices must be non-negative integers in '"):
            return isinstance(key, (int, float)) and (key < 0 or key != int(key))
        m = re.match(rb"^JSON object has no key '(.*)'\Z", vm_msg)
        if m:
            return isinstance(key, str) and key.encode() == m.group(1)
        return False
    if legacy_msg == b"invalid index target":
        if node is None or node.kind != "Assign" or node.children[0].kind != "Index":
            return False
        key = vm_sites.constant(source, root, node.children[0].children[1])
        if not isinstance(key, (int, float)):
            return False
        if vm_msg.startswith(b"index out of range: "):
            return key >= 0 and key == int(key)
        if vm_msg.startswith(b"assignment index does not address an array or object: "):
            return key < 0 or key != int(key)
    return False


def diagnostics_match(legacy_err, vm_err, st, source=None):
    """D-018c on the two stderr streams (one diagnostic line each); the VM's
    position and fields are checked against the source, never taken from
    the VM (review round 2)."""
    lm, vm = DIAGNOSTIC.match(legacy_err), DIAGNOSTIC.match(vm_err)
    if not lm or not vm or lm.group(1) != vm.group(1):
        return False
    legacy_pos = (int(lm.group(2)), int(lm.group(3)))
    vm_pos = (int(vm.group(2)), int(vm.group(3)))
    legacy_msg, vm_msg = _strip_return(lm.group(4)), vm.group(4)
    # Owner 2026-10-30 (CP23 parity attribution): an error raised in a thread
    # or future keeps its `thread: ` / `future: ` context prefix on both
    # sides; legacy's redundant `return: ` prefixes directly after it are
    # removed exactly as at the front. Nothing else is stripped, and the
    # failing node is located from the message without the context prefix.
    for ctx in TASK_CONTEXTS:
        if legacy_msg.startswith(ctx) and vm_msg.startswith(ctx):
            legacy_msg, vm_msg = _strip_return(legacy_msg[len(ctx):]), vm_msg[len(ctx):]
            break
    # CP12b modules: the error is in an imported module's file (the path
    # both report): its source is the one the positions refer to.
    reported_file = lm.group(1).decode(errors="replace")
    if os.path.isabs(reported_file) and os.path.isfile(reported_file):
        with open(reported_file, "rb") as f:
            reported = f.read()
        if reported != _source_bytes(source, st):
            source = reported
    # The same number of leading `import: ` prefixes; positions and fields
    # are checked on the rest.
    legacy_imports, legacy_msg = _split_imports(legacy_msg)
    vm_imports, vm_msg = _split_imports(vm_msg)
    if legacy_imports != vm_imports:
        return False
    mapped = legacy_msg in (b"invalid index", b"invalid index target")
    if legacy_msg != vm_msg and not mapped:
        return False
    source = _source_bytes(source, st)
    root = vm_sites.parse(source) if source else None
    if root is None:
        return False
    offset = vm_sites.offset_of(source, *legacy_pos)
    reported = vm_sites.statement_at(source, root, offset) if offset is not None else None
    if reported is None:
        return False
    # The failing node: unique within what legacy reported or, where legacy
    # reports a loop statement or a call site (loop-evaluator code), unique
    # in the program.
    site = vm_sites.expected_site(source, vm_msg, vm_sites.region_of(reported))
    if site is None:
        rest = source[reported.begin:reported.end]
        if reported.kind == "While" or re.search(rb"\b[A-Za-z_]\w*\s*\(", rest):
            site = vm_sites.expected_site(source, vm_msg, None)
    if site is None or site != vm_pos:
        return False
    return not mapped or _fields_ok(source, root, legacy_msg, vm_msg, site)


PRINT_PREFIX = b"print: "


def interpolation_origin(legacy_err, vm_err, st, source=None):
    """D-018c subcase (owner, 2026-10-30): an arithmetic Error raised by an
    operation inside `$[...]`. Legacy reports it at the interpolated string
    literal, or, when that string is the single argument of `print`, at the
    print statement with a `print: ` prefix; the VM reports the failing
    operation itself. Approved only when:
      - both report one diagnostic in the same file, the message is one of
        the arithmetic errors (vm_sites.ARITHMETIC) and, with any `print: `
        prefix removed, identical;
      - legacy's position is the start of an interpolated String (no
        prefix), or of a `print(<that string>)` call with exactly one
        argument (prefix), never anything else;
      - the string holds no nested interpolated string;
      - exactly one operation inside that string can raise the message
        (vm_sites.expected_site, constants evaluated exactly), and the VM's
        position is that operation.
    Everything else (stdout, exit status) must already be equal (caller)."""
    lm, vm = DIAGNOSTIC.match(legacy_err), DIAGNOSTIC.match(vm_err)
    if not lm or not vm or lm.group(1) != vm.group(1):
        return False
    legacy_msg, vm_msg = lm.group(4), vm.group(4)
    prefixed = legacy_msg.startswith(PRINT_PREFIX)
    if (legacy_msg[len(PRINT_PREFIX):] if prefixed else legacy_msg) != vm_msg or vm_msg not in vm_sites.ARITHMETIC:
        return False
    source = _source_bytes(source, st)
    root = vm_sites.parse(source) if source else None
    if root is None:
        return False
    offset = vm_sites.offset_of(source, int(lm.group(2)), int(lm.group(3)))
    if offset is None:
        return False

    def interpolated(n):
        return n.kind == "String" and any(c.kind != "StringPart" for c in n.children)

    string = None
    for n in root.walk():
        if n.begin != offset:
            continue
        if not prefixed and interpolated(n):
            string = n
        elif (prefixed and n.kind == "Call" and len(n.children) == 2 and n.children[0].kind == "Ident"
              and source[n.children[0].begin:n.children[0].end] == b"print" and interpolated(n.children[1])):
            string = n.children[1]
    if string is None or any(interpolated(n) for n in string.walk() if n is not string):
        return False
    site = vm_sites.expected_site(source, vm_msg, (string.begin, string.end))
    return site is not None and site == (int(vm.group(2)), int(vm.group(3)))


THREAD_PREFIX = b"thread: "
THREAD_HOST_ERRORS = re.compile(rb"^sha256_file: cannot open path\Z")


def thread_host_context(legacy_err, vm_err, st, source=None):
    """D-060 (owner, 2026-10-30): an uncaught Host-operation Error raised in a
    thread and reported after `join` keeps a `thread: ` context prefix on
    the VM (CP23 R3/R4 thread-reporting contract); legacy reports the bare
    Host message. Approved only when:
      - one diagnostic each, same file; the VM message is exactly one
        `thread: ` followed by legacy's message, which is an approved Host
        failure (THREAD_HOST_ERRORS: sha256_file's "cannot open path");
      - the location is equal, or differs only as the D-018c host-location
        rule allows: the unique failing call inside the statement legacy
        reports (vm_sites.expected_site), at the VM's position;
      - that call lies in a named function F that is only ever started as a
        thread (never called directly), each `thread(F, ...)` either at top
        level or inside a function that is itself only started as a thread,
        all at one depth; the number of `thread: ` prefixes equals that
        depth (nested boundaries get one prefix each);
      - the program has no try/catch (the Error is genuinely uncaught).
    The prefix must never appear on a synchronous or caught Host error."""
    lm, vm = DIAGNOSTIC.match(legacy_err), DIAGNOSTIC.match(vm_err)
    if not lm or not vm or lm.group(1) != vm.group(1):
        return False
    legacy_msg, vm_msg = lm.group(4), vm.group(4)
    prefixes = 0
    while vm_msg.startswith(THREAD_PREFIX):
        prefixes += 1
        vm_msg = vm_msg[len(THREAD_PREFIX):]
    if prefixes == 0 or vm_msg != legacy_msg or not THREAD_HOST_ERRORS.match(legacy_msg):
        return False
    source = _source_bytes(source, st)
    root = vm_sites.parse(source) if source else None
    if root is None or any(n.kind == "Try" for n in root.walk()):
        return False
    vm_pos = (int(vm.group(2)), int(vm.group(3)))
    if (int(lm.group(2)), int(lm.group(3))) != vm_pos:
        offset = vm_sites.offset_of(source, int(lm.group(2)), int(lm.group(3)))
        reported = vm_sites.statement_at(source, root, offset) if offset is not None else None
        if reported is None or vm_sites.expected_site(source, legacy_msg, vm_sites.region_of(reported)) != vm_pos:
            return False
    vm_offset = vm_sites.offset_of(source, *vm_pos)
    call = next((n for n in root.walk() if n.kind == "Call" and n.begin == vm_offset), None)
    if call is None:
        return False

    def enclosing(n):
        """The innermost FnDecl around n; a Lambda first means not recognised."""
        p = n.parent
        while p is not None:
            if p.kind == "Lambda":
                return False
            if p.kind == "FnDecl":
                return p
            p = p.parent
        return None

    def name_of(fn):
        return source[fn.children[0].begin:fn.children[0].end] if fn.children and fn.children[0].kind == "Ident" \
            else None

    calls = [n for n in root.walk() if n.kind == "Call" and n.children and n.children[0].kind == "Ident"]

    def depth(fn, seen):
        """Thread boundaries between top level and fn, or None if not unique."""
        name = name_of(fn)
        if name is None or name in seen:
            return None
        found = set()
        for n in calls:
            callee = source[n.children[0].begin:n.children[0].end]
            if callee == name:
                return None  # also called directly: the Error may be synchronous
            if callee == b"thread" and len(n.children) >= 2 and n.children[1].kind == "Ident" and \
                    source[n.children[1].begin:n.children[1].end] == name:
                outer = enclosing(n)
                if outer is False:
                    return None
                found.add(1 if outer is None else (lambda d: None if d is None else d + 1)(depth(outer, seen | {name})))
        if len(found) != 1 or None in found:
            return None  # never started, or started at different depths
        return found.pop()

    fn = enclosing(call)
    return bool(fn) and depth(fn, frozenset()) == prefixes


def _return_prefix(legacy, vm, st, source):
    """D-009#5: the VM reports the same diagnostic at the same position; the
    only difference is one or more legacy `return: ` prefixes."""
    if st.get("outcome") != "vm-error" or legacy[0] != vm[0] or legacy[2] != 1 or vm[2] != 1:
        return False
    lm, vmm = DIAGNOSTIC.match(legacy[1]), DIAGNOSTIC.match(vm[1])
    if not lm or not vmm or lm.group(1) != vmm.group(1) or lm.group(2) != vmm.group(2) or lm.group(3) != vmm.group(3):
        return False
    if not lm.group(4).startswith(RETURN_PREFIX):
        return False
    msg = vmm.group(4)
    # Only two exact legacy-prefix families are approved: the print failure
    # (the position must be the call) and the binding-type assignment error
    # (the position must be the assignment target). Other return-prefixed
    # diagnostics still need D-018c's source proof.
    if not (msg.startswith(b"print: ") or msg.startswith(b"cannot assign ")):
        return False
    src = _source_bytes(source, st)
    if not src:
        return False
    lines = src.splitlines()
    line, col = int(vmm.group(2)), int(vmm.group(3))
    if line < 1 or line > len(lines) or col < 1:
        return False
    rest = lines[line - 1][col - 1:]
    if msg.startswith(b"print: ") and not rest.startswith(b"print("):
        return False
    if msg.startswith(b"cannot assign ") and not rest[:1].isalpha() and rest[:1] != b"_":
        return False
    return _strip_return(lm.group(4)) == msg


def _recursion(legacy, vm, st):
    return (st.get("beyond_legacy_depth") and legacy[2] == 1 and RECURSION_ERROR.match(legacy[1])
            and vm[0].startswith(legacy[0])
            and ((st.get("outcome") == "vm" and vm[2] == 0 and vm[1] == b"")
                 or (st.get("outcome") == "vm-error" and vm[2] == 1)))


def _log(policy, legacy, vm, st, source):
    """VM_POLICY_LOG=<file>: append every approval (review tooling)."""
    target = os.environ.get("VM_POLICY_LOG")
    if not target:
        return
    import json
    source = _source_bytes(source, st)
    cut = lambda b: b[:4000].decode("utf-8", "replace") + ("...[%d bytes]" % len(b) if len(b) > 4000 else "")  # noqa
    record = {"policy": policy, "legacy": [cut(legacy[0]), cut(legacy[1]), legacy[2]],
              "vm": [cut(vm[0]), cut(vm[1]), vm[2]],
              "st": {k: v for k, v in st.items() if k != "_source"},
              "source": source.decode("utf-8", "replace") if source else None}
    with open(target, "a") as f:
        f.write(json.dumps(record) + "\n")


def classify(legacy, vm, st, source=None):
    verdict = _classify(legacy, vm, st, source)
    if verdict[0] == "approved":
        _log(verdict[1], legacy, vm, st or {}, source)
    return verdict


def _classify(legacy, vm, st, source=None):
    """(verdict, policy): verdict 'same', 'approved', 'endless' or 'diff';
    policy is one of POLICIES for 'approved', else None. `legacy` and `vm`
    are (stdout, stderr, exit status) with bytes streams; `st` the VM
    statistics (NIFT_VM_STATS) of the VM run; `source` the program (bytes or
    str; default: read from st["path"])."""
    if legacy == vm:
        return "same", None
    if legacy[2] == "timeout" and vm[2] == "timeout":
        return "endless", None
    st = st or {}
    if st.get("outcome") not in ("vm", "vm-error"):
        return "diff", None
    if d040_witnesses.match(_source_bytes(source, st) or b"", legacy, vm, st):
        return "approved", "D-040 independent copy"
    if _recursion(legacy, vm, st):
        return "approved", "D-001#4 recursion"
    if _return_prefix(legacy, vm, st, source):
        return "approved", "D-009#5 return prefix"
    if (st.get("outcome") == "vm-error" and legacy[0] == vm[0] and legacy[2] == 1 and vm[2] == 1
            and diagnostics_match(legacy[1], vm[1], st, source)):
        return "approved", "D-018c diagnostics"
    if (st.get("outcome") == "vm-error" and legacy[0] == vm[0] and legacy[2] == 1 and vm[2] == 1
            and interpolation_origin(legacy[1], vm[1], st, source)):
        return "approved", "D-018c interpolation"
    if (st.get("outcome") == "vm-error" and legacy[0] == vm[0] and legacy[2] == 1 and vm[2] == 1
            and thread_host_context(legacy[1], vm[1], st, source)):
        return "approved", "CP23 thread host-error context"
    return "diff", None
