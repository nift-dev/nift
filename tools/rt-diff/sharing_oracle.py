"""rt-diff sharing oracle (B07 option 2, owner 2026-11-10): a test-side attribution check for
the B5 shared-reference difference (D-NEXT-refsem-b; combined-b3-b5.md section 60).

WHAT IT ESTABLISHES, AND WHAT IT DOES NOT.
It establishes ATTRIBUTION: that a legacy/VM difference of one program P is caused by the VM
sharing call arguments by reference (B5) where legacy passes values. It does NOT establish that
the VM's output is CORRECT. A VM bug confined to the shared-mutation path (say `push(98)` stored
instead of `push(99)`) would be attributed to sharing and approved, because VM(P) is compared
with nothing. B5's semantic suites (tests/runtime2/b5, refsem, collections; hand-written
expectations) remain the correctness oracle and must stay in the gate.

THE REWRITE T. T(P) wraps every argument of every call to a function the program declares
(`fn(name(...))`, called as `name(...)`) in `deepcopy(...)`, at the call site, and changes
nothing else. deepcopy, not copy: copy is shallow (D-NEXT-refsem #3), so an array or object
nested inside an argument would still be shared and a callee's mutation through a nested element
would stay visible; deepcopy copies every reachable array and object, which is what legacy's
by-value passing gives the callee. copy and deepcopy are never used interchangeably here.

THE CONTROL REWRITE T' (review of C67-C71, O-1). Wrapping an argument changes how the VM
compiles the call: a bare argument or argument path (compiler_call.cpp: argument_path,
alias/escaping handling, bare_argument) becomes a call result. A VM bug that exists only on
those direct-argument paths would vanish under T and be approved. T'(P) wraps the same
arguments in an injected identity function (`fn(rtdiff_identity_N(v)) { return v }`, declared
at the start of line 1), so the call compiles as under T but nothing is copied: the sharing is
preserved. Two more checks must hold:
  1'. Legacy(T'(P)) == Legacy(P) exactly (T' is a no-op under value semantics).
  6.  VM(T'(P)) == VM(P) exactly, on every channel: the direct-argument code paths alone change
      nothing, so what T removes is the sharing and not a direct-path behaviour.

THE FIVE CHECKS (all must hold to approve a row, with 1' and 6 above):
  1. Legacy(T(P)) == Legacy(P) exactly on every compared channel (exit, stdout, stderr, combined,
     files, trace): T is behaviour-preserving under value semantics.
  2. VM(T(P)) == Legacy(P) exactly on every channel: with argument sharing removed, nothing else
     differs.
  3. VM(P) != VM(T(P)): the difference really comes from the rewritten call sites.
  4. No refusal and no failure of the harness kind: both VM runs ran on the VM once (statistics:
     outcome vm or vm-error, one VM execution, no legacy execution), no run timed out, crashed
     (signal), aborted or exited 70, and no VM stderr carries an internal tag.
  5. Exit status and stderr are each justified separately. Every channel that differs between
     Legacy(P) and VM(P) must also differ between VM(P) and VM(T(P)) (this per-channel
     attribution follows from checks 2 and 3 and is asserted anyway). On top of that, an exit or
     stderr difference ALONE (stdout equal) is never approved: the shared-reference cause shows
     up as a value change in stdout, and an exit or diagnostic change is accepted only next to
     such a change and only when the rewrite moves that very channel back to legacy's.

FAIL CLOSED. A row is UNCLASSIFIED (it stays a difference and blocks the gate) when any check
fails or when preservation cannot be established statically: rt-parse-dump cannot parse P or an
imported module, P or a module has a node kind outside the known list, declares structs (struct
`==` is identity), calls an identity-sensitive, handle/reference or concurrency built-in (same,
file, ifstream, ofstream, mutex, channel, atomic, map, set, stack, thread, async, parallel, ...),
awaits or declares an async/thread function, a closure captures a parameter of an enclosing
function, a declared function's name is rebound or shadowed by a parameter, an import cannot be
resolved to a module file, P has no rewritable call site, or any run reports a reference cycle.
It is never approved by default.
"""
import re
import subprocess

# Node kinds of rt-parse-dump whose meaning T preserves (calls are rewritten only as above).
KNOWN_KINDS = {
    "Program", "Block", "ExprStmt", "Declare", "Assign", "CompoundAssign", "Return", "If", "While",
    "For", "ForBinding", "Break", "Continue", "Try", "Throw", "FnDecl", "Param", "Call", "Member",
    "Index", "Ident", "Number", "String", "StringPart", "True", "False", "Binary", "Unary",
    "Logical", "Array", "Object", "Property", "Lambda", "Import", "Export",
}
# Built-ins whose result is identity-sensitive or a handle/reference value (deepcopy would keep or
# change identity in ways the oracle cannot judge).
UNSAFE_CALLS = {
    "same", "file", "ifstream", "ofstream", "close", "mutex", "channel", "send", "recv", "cancel",
    "atomic", "atomic<int>", "atomic<bool>", "map", "set", "stack",
    # concurrency (review of C67-C71): tasks share or transfer values outside the call graph T sees
    "thread", "async", "parallel", "spawn",
}
# function decorators that make a declared function a task (`@fn[async](...)`).
TASK_DECORATOR = re.compile(rb"@fn\[[^\]]*\b(async|thread|parallel)\b")
IDENTITY = "rtdiff_identity_"
LINE = re.compile(r"^( *)(\S+)(?: (\S+))? @(\d+)\.\.(\d+)(?: `(.*)`)?$")
CYCLE = re.compile(rb"cyclic|reference_cycle|reference cycle")
INTERNAL = re.compile(rb"compiler defect|internal error|\[internal\]|vm-internal")


class Node:
    __slots__ = ("kind", "start", "end", "text", "children", "parent")

    def __init__(self, kind, start, end, text, parent):
        self.kind, self.start, self.end, self.text, self.parent = kind, start, end, text, parent
        self.children = []

    def walk(self):
        yield self
        for child in self.children:
            yield from child.walk()


def parse(dump_binary, path):
    """The CP10 AST of `path` via rt-parse-dump, or (None, reason)."""
    try:
        out = subprocess.run([dump_binary, "dump", path], capture_output=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired) as error:
        return None, "rt-parse-dump failed: %s" % error
    if out.returncode != 0:
        return None, "rt-parse-dump cannot parse the program (exit %d)" % out.returncode
    root, stack = None, []
    for raw in out.stdout.decode("utf-8", "replace").splitlines():
        m = LINE.match(raw)
        if not m:
            return None, "unrecognised rt-parse-dump line %r" % raw[:80]
        depth = len(m.group(1)) // 2
        while len(stack) > depth:
            stack.pop()
        parent = stack[-1] if stack else None
        text = m.group(6) if m.group(6) is not None else None
        node = Node(m.group(2), int(m.group(4)), int(m.group(5)), text, parent)
        if parent is None:
            if root is not None:
                return None, "rt-parse-dump printed more than one root"
            root = node
        else:
            parent.children.append(node)
        stack.append(node)
    if root is None or root.kind != "Program":
        return None, "rt-parse-dump printed no Program"
    return root, None


def _name(node):
    return node.text if node is not None and node.kind == "Ident" else None


def static_reason(source, root):
    """Why P (or a module) cannot be judged statically, or None."""
    nodes = list(root.walk())
    unknown = sorted({n.kind for n in nodes} - KNOWN_KINDS)
    if unknown:
        return "node kinds outside the oracle's known list: %s" % ", ".join(unknown)
    if TASK_DECORATOR.search(source):
        return "an async/thread/parallel function declaration"
    declared = {_name(n.children[0]) for n in nodes if n.kind == "FnDecl" and n.children}
    declared.discard(None)
    for n in nodes:
        if n.kind == "Call" and n.children and _name(n.children[0]) in UNSAFE_CALLS:
            return "identity-sensitive, handle or concurrency built-in %s()" % _name(n.children[0])
        if n.kind in ("Declare", "Assign", "CompoundAssign") and n.children and _name(n.children[0]) in declared:
            return "declared function %s is rebound" % _name(n.children[0])
        if n.kind == "Param" and n.text in declared:
            return "parameter %s shadows a declared function" % n.text
        if n.kind == "Lambda":
            own = {c.text for c in n.children if c.kind == "Param"}
            enclosing, p = set(), n.parent
            while p is not None:
                if p.kind in ("FnDecl", "Lambda"):
                    enclosing |= {c.text for c in p.children if c.kind == "Param"}
                p = p.parent
            used = {_name(c) for c in n.walk() if c.kind == "Ident"}
            captured = sorted((used & enclosing) - own - {None})
            if captured:
                return "a closure captures parameter(s) %s of an enclosing function" % ", ".join(captured)
    return None


def imports(root):
    """The literal module paths P imports, or None when an import is not a literal path."""
    paths = []
    for n in root.walk():
        if n.kind == "Import":
            lits = [c for c in n.walk() if c.kind == "String" and c.text is not None]
            if len(lits) != 1 or any(c.kind not in ("String", "StringPart", "Import") for c in n.walk()):
                return None
            text = lits[0].text
            if len(text) < 2 or text[0] != '"' or text[-1] != '"' or "\\" in text or "${" in text:
                return None
            paths.append(text[1:-1])
    return paths


def check_modules(dump_binary, script_path, root, seen=None, depth=0):
    """Static checks over every module P imports (recursively): a reason, or None."""
    import os
    seen = set() if seen is None else seen
    if depth > 16:
        return "imports nested deeper than 16"
    paths = imports(root)
    if paths is None:
        return "an import that is not a literal module path"
    for rel in paths:
        path = os.path.normpath(os.path.join(os.path.dirname(script_path), rel))
        if path in seen:
            continue
        seen.add(path)
        if not os.path.isfile(path):
            return "imported module %s not found beside the script" % rel
        mod, why = parse(dump_binary, path)
        if mod is None:
            return "module %s: %s" % (rel, why)
        with open(path, "rb") as handle:
            why = static_reason(handle.read(), mod)
        if why:
            return "module %s: %s" % (rel, why)
        why = check_modules(dump_binary, path, mod, seen, depth + 1)
        if why:
            return why
    return None


def transform(source, root, identity=None):
    """(T(P) bytes, number of wrapped arguments, None) or (None, 0, reason). With `identity`,
    the control rewrite T'(P): the same arguments wrapped in the injected identity function
    `identity` (declared at the start of line 1) instead of deepcopy."""
    nodes = list(root.walk())
    why = static_reason(source, root)
    if why:
        return None, 0, why
    declared = {_name(n.children[0]) for n in nodes if n.kind == "FnDecl" and n.children}
    declared.discard(None)
    if IDENTITY.encode() in source:
        return None, 0, "the program already uses the oracle's identity name %s" % IDENTITY
    inserts = []
    names = []  # T': one identity function per wrapped argument, so each stays monomorphic
    for n in nodes:
        if n.kind == "Call" and n.children and _name(n.children[0]) in declared:
            for arg in n.children[1:]:
                if identity:
                    names.append("%s%d" % (identity, len(names)))
                inserts.append((arg.start, 1, (names[-1] + "(").encode() if identity else b"deepcopy("))
                inserts.append((arg.end, 0, b")"))
    if not inserts:
        return None, 0, "no call to a declared function: nothing to rewrite"
    out = bytearray(source)
    # From the end; at one offset close before open, so `f(a)(b)`-style neighbours stay balanced.
    for pos, _order, text in sorted(inserts, key=lambda t: (t[0], t[1]), reverse=True):
        if pos > len(out):
            return None, 0, "rt-parse-dump offset beyond the source"
        out[pos:pos] = text
    if identity:
        out[0:0] = "".join("fn(%s(v)) { return v }; " % name for name in names).encode()
    return bytes(out), len(inserts) // 2, None


def harness_failure(run, label, vm=False):
    """Check 4 for one run: a reason string, or None."""
    for code in run["exit"]:
        if code == "timeout":
            return "%s timed out" % label
        if not isinstance(code, int) or code < 0 or code >= 128:
            return "%s crashed or was killed (exit %r)" % (label, code)
        if code == 70:
            return "%s exited 70 (internal failure)" % label
    if CYCLE.search(run["stderr"]) or CYCLE.search(run["stdout"]):
        return "%s reports a reference cycle" % label
    if vm:
        if INTERNAL.search(run["stderr"]):
            return "%s stderr carries an internal tag" % label
        stats = run.get("vm_stats")
        if stats is None:
            return "%s has no VM statistics (run rt-diff with --b-vm-stats)" % label
        if (len(stats) != 1 or stats[0].get("outcome") not in ("vm", "vm-error")
                or stats[0].get("vm_executions", 0) != 1 or stats[0].get("legacy_executions", 0) != 0):
            return "%s did not run once on the VM (refused, replayed or fell back): %s" % (
                label, [{k: v for k, v in st.items() if k != "_source"} for st in stats])
    return None


def decide(a, b, at, bt, ac, bc, compare):
    """The checks over Legacy(P)=a, VM(P)=b, Legacy(T(P))=at, VM(T(P))=bt, Legacy(T'(P))=ac and
    VM(T'(P))=bc. `compare(x, y)` returns rt-diff's list of differing channels. Returns
    (approved, reason, evidence)."""
    for run, label, vm in ((a, "Legacy(P)", False), (b, "VM(P)", True), (at, "Legacy(T(P))", False),
                           (bt, "VM(T(P))", True), (ac, "Legacy(T'(P))", False), (bc, "VM(T'(P))", True)):
        why = harness_failure(run, label, vm)
        if why:
            return False, "check 4: " + why, {}
    d_orig = sorted({d["channel"] for d in compare(a, b)})
    d1 = compare(a, at)
    if d1:
        return False, "check 1: the rewrite changes legacy (%s)" % ", ".join(sorted({d["channel"] for d in d1})), {}
    d1c = compare(a, ac)
    if d1c:
        return False, "check 1': the control rewrite T' changes legacy (%s)" % ", ".join(sorted({d["channel"] for d in d1c})), {}
    d6 = compare(b, bc)
    if d6:
        return False, ("check 6: VM(T'(P)) differs from VM(P) (%s): the direct-argument code paths alone change "
                       "the result, so T would hide a direct-path behaviour" % ", ".join(sorted({d["channel"] for d in d6}))), {}
    d2 = compare(a, bt)
    if d2:
        return False, "check 2: VM(T(P)) still differs from legacy (%s)" % ", ".join(sorted({d["channel"] for d in d2})), {}
    d3 = sorted({d["channel"] for d in compare(b, bt)})
    if not d3:
        return False, "check 3: VM(P) == VM(T(P)); the difference does not come from argument sharing", {}
    unexplained = [c for c in d_orig if c not in d3]
    if unexplained:
        return False, "check 5: channel(s) %s differ from legacy but not between VM(P) and VM(T(P))" % ", ".join(unexplained), {}
    if ("exit" in d_orig or "stderr" in d_orig) and "stdout" not in d_orig:
        return False, "check 5: an exit/stderr difference alone (stdout equal) is not approved", {}
    return True, "", {"channels": d_orig, "vm_vs_vm_t": d3}
