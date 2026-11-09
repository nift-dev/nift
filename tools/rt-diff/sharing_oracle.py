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

THE FIVE CHECKS (all must hold to approve a row):
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
fails or when preservation cannot be established statically: rt-parse-dump cannot parse P, P has
a node kind outside the known list, P declares structs (struct `==` is identity), P calls an
identity-sensitive or handle/reference built-in (same, file, ifstream, ofstream, mutex, channel,
atomic, map, set, stack, ...), a closure captures a parameter of an enclosing function, a
declared function's name is rebound, P has no rewritable call site, or any run reports a
reference cycle. It is never approved by default.
"""
import re
import subprocess

# Node kinds of rt-parse-dump whose meaning T preserves (calls are rewritten only as above).
KNOWN_KINDS = {
    "Program", "Block", "ExprStmt", "Declare", "Assign", "CompoundAssign", "Return", "If", "While",
    "For", "ForBinding", "Break", "Continue", "Try", "Throw", "FnDecl", "Param", "Call", "Member",
    "Index", "Ident", "Number", "String", "StringPart", "True", "False", "Binary", "Unary",
    "Logical", "Array", "Object", "Property", "Lambda", "Await", "Import",
}
# Built-ins whose result is identity-sensitive or a handle/reference value (deepcopy would keep or
# change identity in ways the oracle cannot judge).
UNSAFE_CALLS = {
    "same", "file", "ifstream", "ofstream", "close", "mutex", "channel", "send", "recv", "cancel",
    "atomic", "atomic<int>", "atomic<bool>", "map", "set", "stack",
}
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


def transform(source, root):
    """(T(P) bytes, number of wrapped arguments, None) or (None, 0, reason)."""
    nodes = list(root.walk())
    unknown = sorted({n.kind for n in nodes} - KNOWN_KINDS)
    if unknown:
        return None, 0, "node kinds outside the oracle's known list: %s" % ", ".join(unknown)
    declared = {_name(n.children[0]) for n in nodes if n.kind == "FnDecl" and n.children}
    declared.discard(None)
    for n in nodes:
        if n.kind == "Call" and n.children and _name(n.children[0]) in UNSAFE_CALLS:
            return None, 0, "identity-sensitive or handle built-in %s()" % _name(n.children[0])
        if n.kind in ("Declare", "Assign", "CompoundAssign") and n.children and _name(n.children[0]) in declared:
            return None, 0, "declared function %s is rebound" % _name(n.children[0])
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
                return None, 0, "a closure captures parameter(s) %s of an enclosing function" % ", ".join(captured)
    inserts = []
    for n in nodes:
        if n.kind == "Call" and n.children and _name(n.children[0]) in declared:
            for arg in n.children[1:]:
                inserts.append((arg.start, 1, b"deepcopy("))
                inserts.append((arg.end, 0, b")"))
    if not inserts:
        return None, 0, "no call to a declared function: nothing to rewrite"
    out = bytearray(source)
    # From the end; at one offset close before open, so `f(a)(b)`-style neighbours stay balanced.
    for pos, _order, text in sorted(inserts, key=lambda t: (t[0], t[1]), reverse=True):
        if pos > len(out):
            return None, 0, "rt-parse-dump offset beyond the source"
        out[pos:pos] = text
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


def decide(a, b, at, bt, compare):
    """The five checks over Legacy(P)=a, VM(P)=b, Legacy(T(P))=at, VM(T(P))=bt. `compare(x, y)`
    returns rt-diff's list of differing channels. Returns (approved, reason, evidence)."""
    for run, label, vm in ((a, "Legacy(P)", False), (b, "VM(P)", True), (at, "Legacy(T(P))", False),
                           (bt, "VM(T(P))", True)):
        why = harness_failure(run, label, vm)
        if why:
            return False, "check 4: " + why, {}
    d_orig = sorted({d["channel"] for d in compare(a, b)})
    d1 = compare(a, at)
    if d1:
        return False, "check 1: the rewrite changes legacy (%s)" % ", ".join(sorted({d["channel"] for d in d1})), {}
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
