"""Runtime 2.0 CP12: independent error-site oracle for the D-018c policy.

The VM never vouches for its own diagnostics (review round 2). Where an
error's legacy position differs from the VM's, the expected VM position is
derived here from the program's CP10 AST (tools: .build/rt-parse-dump, the
parser alone, not the compiler or the VM) and the message:

  expected_site(source, message, region) -> (line, column) or None

`region` is the byte span of the code legacy names (its reported statement:
the condition of an `if`/`while`, the expression of a `return`, the whole
statement otherwise); candidates are the nodes that can raise `message`
(the `/` operations for "division by zero", the expression a message
quotes, the assignment to the binding a message names, ...). Exactly one
candidate gives the expected position (its first byte); none or several
give None, and the policy does not approve.

  resolve_constant(ast, node) -> value or None

A literal, `-` literal, or a name declared exactly once from such a literal
and never assigned: the value a message field (index, key) must carry when
legacy's wording omits it.
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DUMP = os.environ.get("VM_PARSE_DUMP", os.path.join(REPO, ".build", "rt-parse-dump"))
LINE = re.compile(r"^( *)(\w+)(?: ([^@]+?))? @(\d+)\.\.(\d+)(?: `.*`)?$")
_cache = {}


def require_tool():
    """Fail as infrastructure, never as an unapproved semantic difference."""
    if not os.path.isfile(DUMP) or not os.access(DUMP, os.X_OK):
        print(f"HARNESS INFRASTRUCTURE FAILURE: D-018c requires executable parser: {DUMP} "
              "(build make .build/rt-parse-dump or set VM_PARSE_DUMP)", file=sys.stderr)
        raise SystemExit(2)


class Node:
    __slots__ = ("kind", "op", "begin", "end", "children", "parent")

    def __init__(self, kind, op, begin, end):
        self.kind, self.op, self.begin, self.end = kind, op, begin, end
        self.children, self.parent = [], None

    def walk(self):
        yield self
        for c in self.children:
            yield from c.walk()


def parse(source):
    """The CP10 AST of `source` (bytes), or None (parse error). Missing tooling is fatal."""
    require_tool()
    key = hashlib.sha256(source).hexdigest()
    if key in _cache:
        return _cache[key]
    root = None
    with tempfile.NamedTemporaryFile(suffix=".f", delete=False) as f:
        f.write(source)
    try:
        out = subprocess.run([DUMP, "dump", f.name], capture_output=True, timeout=60).stdout.decode()
    finally:
        os.unlink(f.name)
    stack = []
    for line in out.splitlines():
        m = LINE.match(line)
        if not m:
            continue
        depth = len(m.group(1)) // 2
        node = Node(m.group(2), (m.group(3) or "").strip(), int(m.group(4)), int(m.group(5)))
        del stack[depth:]
        if stack:
            node.parent = stack[-1]
            stack[-1].children.append(node)
        else:
            root = node
        stack.append(node)
    _cache[key] = root
    return root


def position(source, offset):
    line = source.count(b"\n", 0, offset) + 1
    return line, offset - (source.rfind(b"\n", 0, offset) + 1) + 1


def offset_of(source, line, column):
    lines = source.split(b"\n")
    if not 1 <= line <= len(lines):
        return None
    return sum(len(x) + 1 for x in lines[:line - 1]) + column - 1


def text(source, node):
    return source[node.begin:node.end]


def _candidates(source, root, message):
    """Nodes that can raise `message` (bytes)."""
    nodes = list(root.walk())
    m = re.match(rb"^(?:JSON array index \d+ is out of range in |JSON array indices must be non-negative integers "
                 rb"in )'(.*)'\Z", message)
    if m:
        # The index message quotes the whole member/index chain the failing
        # index belongs to (stage 4 D1: `a[5].y` -> 'a[5].y'), or the index
        # itself: every Index node of a chain with that text.
        out = [n for n in nodes if n.kind == "Index" and text(source, n) == m.group(1)]
        for top in nodes:
            if top.kind in ("Member", "Index") and text(source, top) == m.group(1):
                c = top
                while c.kind in ("Member", "Index") and c.children:
                    if c.kind == "Index" and all(c is not o for o in out):
                        out.append(c)
                    c = c.children[0]
        return out
    m = re.match(rb"^unknown value or malformed expression: (.+)\Z", message)
    if m:
        return [n for n in nodes if n.kind in ("Call", "Index", "Member") and text(source, n) == m.group(1)]
    if re.match(rb"^JSON object has no key '.*'\Z", message):
        return [n for n in nodes if n.kind == "Index"]
    m = re.match(rb"^value has no member: (\w+)\Z", message)
    if m:
        return [n for n in nodes if n.kind == "Member" and text(source, n.children[1]) == m.group(1)]
    m = re.match(rb"^cannot assign \w+ to \w+ binding '(\w+)'\Z", message)
    if m:
        return [n for n in nodes if n.kind in ("Assign", "CompoundAssign", "IncDec") and n.children
                and n.children[0].kind == "Ident" and text(source, n.children[0]) == m.group(1)]
    m = re.match(rb"^(?:index out of range|assignment index does not address an array or object): (.+)\Z", message)
    if m:
        return [n for n in nodes if n.kind == "Assign" and n.children and n.children[0].kind == "Index"
                and text(source, n.children[0].children[1]) == m.group(1)]
    # Owner 2026-10-30: the one host-operation failure approved for D-018c
    # location recognition. The failing node is a call of that built-in by
    # name; expected_site still requires it to be unique where legacy reports.
    m = re.match(rb"^(sha256_file): cannot open path\Z", message)
    if m:
        return [n for n in nodes if n.kind == "Call" and n.children and n.children[0].kind == "Ident"
                and text(source, n.children[0]) == m.group(1)]
    ops = {b"division by zero": ("/", "/="), b"modulo by zero": ("%", "%="),
           b"modulo requires integer-valued operands": ("%", "%="),
           b"signed 64-bit integer overflow": ("+", "-", "*", "/", "%", "+=", "-=", "*=", "/=", "%=", "neg"),
           b"arithmetic result is not finite": ("+", "-", "*", "/", "%", "+=", "-=", "*=", "/=", "%=", "neg")}
    if message in ops:
        allowed = ops[message]
        cands = [n for n in nodes if (n.kind in ("Binary", "CompoundAssign", "Unary") and n.op in allowed)
                 or (n.kind == "IncDec" and message in (b"signed 64-bit integer overflow",))]
        # Where the operands are constants, an exact evaluation (Python ints)
        # says which operation fails; the others are dropped.
        return cands
    return []


ARITHMETIC = (b"division by zero", b"modulo by zero", b"modulo requires integer-valued operands",
              b"signed 64-bit integer overflow", b"arithmetic result is not finite")


def _narrow(source, root, cands, message):
    """Arithmetic candidates: an exact evaluation (Python ints) of constant
    operands says which operation fails."""
    verdicts = [(n, _fails(source, root, n, message)) for n in cands]
    unknown = [n for n, v in verdicts if v is None]
    confirmed = [n for n, v in verdicts if v is True]
    if unknown or not confirmed:
        return unknown + confirmed
    # Confirmed failing operations are never nested (a failing operand would
    # fail first); evaluation is left to right, so the first one fails,
    # unless a short-circuit operator may skip it.
    if any(_conditional(n) for n in confirmed):
        return confirmed
    return [min(confirmed, key=lambda n: n.begin)]


def _conditional(node):
    while node.parent is not None:
        if node.parent.kind in ("Logical", "Ternary", "Coalesce") and node is not node.parent.children[0]:
            return True
        node = node.parent
    return False


INT64 = (-(1 << 63), (1 << 63) - 1)


class _Err(Exception):
    pass


def _integral(v):
    return isinstance(v, int) or (isinstance(v, float) and v.is_integer() and abs(v) < 2.0 ** 63)


def evaluate(source, root, node):
    """Exact value of a constant expression (Python ints/floats), None when
    not constant; raises _Err(message) for a failing operation."""
    k = node.kind
    if k in ("Number", "String", "Ident"):
        return resolve_constant(source, root, node)
    if k == "Unary" and node.op == "neg":
        v = evaluate(source, root, node.children[0])
        if v is None or isinstance(v, str):
            return None
        if isinstance(v, int) and -v > INT64[1]:
            return -v  # -INT64_MIN is exact (a Decimal), not an error
        return -v
    if k == "Binary" and node.op in ("+", "-", "*", "/", "%"):
        a = evaluate(source, root, node.children[0])
        b = evaluate(source, root, node.children[1])
        if not isinstance(a, (int, float)) or not isinstance(b, (int, float)):
            return None
        if node.op == "/":
            if b == 0:
                raise _Err(b"division by zero")
            return a / b
        if node.op == "%":
            if b == 0:
                raise _Err(b"modulo by zero")
            if a != int(a) or b != int(b):
                raise _Err(b"modulo requires integer-valued operands")
            a, b = int(a), int(b)
            r = abs(a) % abs(b)
            return -r if a < 0 else r
        if _integral(a) and _integral(b):  # CP09: integral operands use checked int64 arithmetic
            a, b = int(a), int(b)
        r = {"+": a + b, "-": a - b, "*": a * b}[node.op]
        if isinstance(r, int) and not INT64[0] <= r <= INT64[1]:
            raise _Err(b"signed 64-bit integer overflow")
        return r
    return None


def _fails(source, root, node, message):
    """True: the operation itself raises `message` (operands constant and
    fine); False: it cannot; None: unknown."""
    try:
        for child in node.children:
            evaluate(source, root, child)
    except _Err:
        return False  # an operand fails first
    if node.kind != "Binary" and not (node.kind == "Unary" and node.op == "neg"):
        return None
    try:
        v = evaluate(source, root, node)
    except _Err as e:
        return e.args[0] == message
    return False if v is not None else None


def statement_at(source, root, offset):
    """The outermost node starting at `offset` (what legacy reports; legacy
    includes a parenthesised expression's parentheses, which leave no node)."""
    def at(o):
        best = None
        for n in root.walk():
            if n.begin == o and n.kind not in ("Program", "Block") and (best is None or n.end > best.end):
                best = n
        return best
    found = at(offset)
    while found is None and offset < len(source) and source[offset:offset + 1] in (b"(", b" ", b"!"):
        offset += 1
        found = at(offset)
    return found


def region_of(node):
    """The part of a reported statement legacy's position stands for."""
    if node.kind in ("If", "While") and node.children:
        return node.children[0].begin, node.children[0].end
    return node.begin, node.end


def expected_site(source, message, region):
    """(line, column) of the unique node in `region` (or the whole program
    when region is None) that can raise `message`; None if not unique."""
    root = parse(source)
    if root is None:
        return None
    cands = _candidates(source, root, message)
    if region is not None:
        cands = [n for n in cands if region[0] <= n.begin and n.end <= region[1]]
    if message in ARITHMETIC:
        cands = _narrow(source, root, cands, message)
    sites = {position(source, n.begin) for n in cands}  # nested nodes can share a first byte
    return sites.pop() if len(sites) == 1 else None


def resolve_constant(source, root, node):
    """The constant value of an expression node, or None."""
    if node.kind == "Number":
        t = text(source, node).decode()
        try:
            return int(t) if re.fullmatch(r"\d+", t) else float(t)
        except ValueError:
            return None
    if node.kind == "String":
        t = text(source, node)
        return t[1:-1].decode() if len(t) >= 2 and b"\\" not in t else None
    if node.kind == "Unary" and node.op == "neg" and node.children:
        v = resolve_constant(source, root, node.children[0])
        return -v if isinstance(v, (int, float)) else None
    if node.kind == "Ident":
        name = text(source, node)
        decls, writes = [], 0
        for n in root.walk():
            if n.kind in ("Declare",) and n.children and text(source, n.children[0]) == name:
                decls.append(n)
            if n.kind in ("Assign", "CompoundAssign", "IncDec", "Param") and n.children and \
                    text(source, n.children[0]) == name:
                writes += 1
            if n.kind == "Param" and text(source, n) == name:
                writes += 1
        if len(decls) == 1 and writes == 0 and len(decls[0].children) == 2 and decls[0].children[1] is not node:
            try:
                return evaluate(source, root, decls[0].children[1])
            except _Err:
                return None
    return None


def constant(source, root, node):
    """evaluate() without errors: the value or None."""
    try:
        return evaluate(source, root, node)
    except _Err:
        return None
