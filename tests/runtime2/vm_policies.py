"""Runtime 2.0 CP12: the approved legacy/VM difference policies (shared).

Every differential harness (vm_adversarial.py, vm_parity.py,
vm_engine_selection.py, vm_former_replay.py, vm_d018.py, rt-diff's class
rules) compares a legacy run with a NIFT_VM=1 run through classify(). A run
pair is either identical or matches exactly one of these narrow, named
policies; anything else is a difference that fails the gate. Each policy
requires evidence from the VM statistics line, attributed to the very output
line or error it approves (review CP12 round 1: no broad masking; every
synthetic bug of tests/runtime2/vm_policy_negative.py must be rejected).

D-018b (print(path) with a missing element evaluates its keys once) has no
policy: in the compiled subset legacy's second resolution is not observable,
so any difference there fails the gate; vm_d018.py checks those programs are
identical.

  D-001#4 recursion   legacy fails with "callable recursion depth exceeded"
                      at its 64-frame limit; the VM went past it
                      (beyond_legacy_depth), legacy's output is a prefix of
                      the VM's, and the VM completed or stopped with a program
                      error.

  D-018a numbers      one number rule everywhere and exactly-once evaluation
                      (legacy computes loop arithmetic in doubles and
                      re-evaluates a loop statement whose value reaches 2^53).
                      Evidence per output line: the VM lists, for arithmetic
                      in loop-evaluator code that met a number legacy calls
                      large (|n| >= 2^53), the index of the output line printed
                      next (`large_lines`; `derived_lines` when an operand was
                      large). Approved line by line, nothing else:
                      * a changed line has the same text once numbers and
                        booleans are abstracted, its VM line is in
                        large_lines, and every changed number pair has a
                        large member (|n| >= 2^53, e-notation, inf, nan); small
                        numbers or booleans may change only on a line in
                        derived_lines (a large operand's small or boolean
                        consequence);
                      * a legacy line the VM does not print is an exact
                        duplicate of an adjacent legacy line (the re-evaluated
                        statement's calls printed twice) and a large-number
                        event is attributed to that position;
                      * after a VM stop: the VM error is "signed 64-bit integer
                        overflow" or "arithmetic result is not finite", it
                        happened in loop-evaluator code (error_in_loop_path),
                        the statistics name its line and column (equal to the
                        diagnostic's) and attribute a large-number event to the
                        stop; only then is legacy's continuation (output and
                        diagnostic after the VM's last line) not compared.
                      Otherwise stdout, stderr and exit status must match
                      (or the diagnostics match D-018c).

  D-018c diagnostics  errors reported where they occur (D-009#1, #5): same
                      stdout, both exit 1 with one diagnostic on the same file,
                      the statistics' line and column equal the VM diagnostic's.
                      Message: equal once legacy's "return: " prefixes are
                      removed or, only in loop-evaluator code
                      (error_in_loop_path), legacy's loop wording is the
                      canonical message's legacy form (MESSAGE_MAP: same
                      binding name; an index error for "invalid index"; an
                      element-assignment error for "invalid index target").
                      Position: the VM's must be the true failing site, checked
                      against the source (the expression the message quotes
                      starts there; the binding assigned there; an index,
                      member, `/`, `%` or arithmetic operator there), and
                      legacy's must be where legacy reports: on the same line
                      at or before the VM's column (the enclosing `if`,
                      `return`, index or statement), or, in loop-evaluator
                      code only, a `while` statement or a statement containing
                      a call (outermost loop statement, caller's call site).
                      Without the source only identical positions qualify.

classify(legacy, vm, st, source=None): when `source` is None it is taken from
st["_source"] (set by a harness) or read from the statistics' `path`.
"""
import difflib
import os
import re

RECURSION_ERROR = re.compile(rb"^error: [^\n]*: callable recursion depth exceeded: [A-Za-z_][A-Za-z0-9_]*\n\Z")
DIAGNOSTIC = re.compile(rb"^error: (.*):(\d+):(\d+): ([^\n]*)\n\Z")
# Number and boolean tokens: whole words only ("info", "user42", "x1" contain none).
TOKEN = re.compile(rb"(?<![A-Za-z0-9_.])(?:-?(?:\d+(?:\.\d+)?(?:[eE][+-]?\d+)?|inf|nan)|true|false)(?![A-Za-z0-9_.])")
NUMERIC_ERRORS = (b"signed 64-bit integer overflow", b"arithmetic result is not finite")
INDEX_ERROR = re.compile(rb"^(JSON array index \d+ is out of range in '(?P<e1>.*)'|JSON array indices must be "
                         rb"non-negative integers in '(?P<e2>.*)'|JSON object has no key '.*')\Z")
TARGET_ERROR = re.compile(rb"^(index out of range|assignment index does not address an array or object): (?P<k>.+)\Z")
BINDING_ERROR = re.compile(rb"^cannot assign \w+ to \w+ binding '(?P<n>\w+)'\Z")
RETURN_PREFIX = b"return: "
LARGE = 2.0 ** 53

POLICIES = ("D-001#4 recursion", "D-018a numbers", "D-018c diagnostics")


def _strip_return(message):
    while message.startswith(RETURN_PREFIX):
        message = message[len(RETURN_PREFIX):]
    return message


def _message_maps(legacy_msg, vm_msg):
    """Legacy loop-evaluator wording -> the canonical message (D-009#1)."""
    m = re.match(rb"^cannot change binding type: (\w+)\Z", legacy_msg)
    if m:
        b = BINDING_ERROR.match(vm_msg)
        return bool(b and b.group("n") == m.group(1))
    if legacy_msg == b"invalid index":
        return bool(INDEX_ERROR.match(vm_msg))
    if legacy_msg == b"invalid index target":
        return bool(TARGET_ERROR.match(vm_msg))
    return False


def _source_bytes(source, st):
    if source is None and st:
        source = st.get("_source")
    if source is None and st and st.get("path") and os.path.isfile(st["path"]):
        with open(st["path"], "rb") as f:
            source = f.read()
    if isinstance(source, str):
        source = source.encode()
    return source


def _at(source, line, column):
    """The rest of `line` from `column` (1-based), or None."""
    lines = source.split(b"\n")
    if not 1 <= line <= len(lines):
        return None
    text = lines[line - 1]
    if not 1 <= column <= len(text) + 1:
        return None
    return text[column - 1:]


def _site_ok(message, rest):
    """Is the VM diagnostic's position (rest of the line from it) the site
    of the error the message describes?"""
    if rest is None:
        return False
    m = INDEX_ERROR.match(message)
    if m:
        quoted = m.group("e1") or m.group("e2")
        if quoted is not None:
            return rest.startswith(quoted)
        return re.match(rb"^[A-Za-z_][\w.\[\]\"']*\[", rest) is not None  # JSON object has no key
    m = re.match(rb"^unknown value or malformed expression: (.+)\Z", message)
    if m:
        return rest.startswith(m.group(1))
    m = BINDING_ERROR.match(message)
    if m:
        return re.match(rb"^" + re.escape(m.group("n")) + rb"\b", rest) is not None
    m = TARGET_ERROR.match(message)
    if m:
        return re.match(rb"^[A-Za-z_]\w*\s*\[\s*" + re.escape(m.group("k")) + rb"\s*\]", rest) is not None
    m = re.match(rb"^value has no member: (\w+)\Z", message)
    if m:
        return re.match(rb"^[^;]*?\." + re.escape(m.group(1)) + rb"\b", rest) is not None
    # Arithmetic: the VM reports the operation's expression, never the
    # statement around it; its operator follows within the expression.
    if re.match(rb"^(if|while|return)\b", rest):
        return False
    expression = re.split(rb"[{;]", rest, maxsplit=1)[0]
    if message == b"division by zero":
        return b"/" in expression
    if message in (b"modulo by zero", b"modulo requires integer-valued operands"):
        return b"%" in expression
    if message in NUMERIC_ERRORS:
        return re.search(rb"[-+*/%]", expression) is not None
    return False


def _legacy_site_ok(source, legacy_pos, vm_pos, in_loop):
    if legacy_pos[0] == vm_pos[0] and legacy_pos[1] <= vm_pos[1]:
        return True  # the enclosing `if`, `return`, index expression or statement
    if not in_loop:
        return False
    rest = _at(source, *legacy_pos)
    if rest is None:
        return False
    # The outermost loop statement, or the call site of a loop-called function.
    return rest.startswith(b"while") or re.search(rb"\b[A-Za-z_]\w*\s*\(", rest) is not None


def diagnostics_match(legacy_err, vm_err, st, source=None):
    """D-018c on the two stderr streams (one diagnostic line each)."""
    lm, vm = DIAGNOSTIC.match(legacy_err), DIAGNOSTIC.match(vm_err)
    if not lm or not vm or lm.group(1) != vm.group(1):
        return False
    legacy_pos = (int(lm.group(2)), int(lm.group(3)))
    vm_pos = (int(vm.group(2)), int(vm.group(3)))
    if (st.get("line"), st.get("column")) != vm_pos:
        return False  # the statistics must locate the same error
    in_loop = bool(st.get("error_in_loop_path"))
    legacy_msg, vm_msg = _strip_return(lm.group(4)), vm.group(4)
    if legacy_msg != vm_msg and not (in_loop and _message_maps(legacy_msg, vm_msg)):
        return False
    source = _source_bytes(source, st)
    if source is None:
        return legacy_pos == vm_pos and legacy_msg == vm_msg
    if not _site_ok(vm_msg, _at(source, *vm_pos)):
        return False
    return legacy_pos == vm_pos or _legacy_site_ok(source, legacy_pos, vm_pos, in_loop)


def _recursion(legacy, vm, st):
    return (st.get("beyond_legacy_depth") and legacy[2] == 1 and RECURSION_ERROR.match(legacy[1])
            and vm[0].startswith(legacy[0])
            and ((st.get("outcome") == "vm" and vm[2] == 0 and vm[1] == b"")
                 or (st.get("outcome") == "vm-error" and vm[2] == 1)))


def _large_token(tok):
    if tok in (b"true", b"false"):
        return False
    if b"e" in tok.lower() or tok.lstrip(b"-") in (b"inf", b"nan"):
        return True
    try:
        return abs(float(tok)) >= LARGE
    except ValueError:
        return False


def _line_ok(x, y, j, large, derived):
    """A legacy line `x` printed as `y` (VM line index j)."""
    if j not in large or TOKEN.sub(b"#", x) != TOKEN.sub(b"#", y):
        return False
    for p, q in zip(TOKEN.findall(x), TOKEN.findall(y)):
        if p == q:
            continue
        if (p in (b"true", b"false")) != (q in (b"true", b"false")):
            return False
        if not (_large_token(p) or _large_token(q)) and j not in derived:
            return False  # a small number or a boolean: needs a large operand on that line
    return True


def _numeric_stop(vm, st):
    """The VM stopped with a numeric error attributed to loop-evaluator code."""
    m = DIAGNOSTIC.match(vm[1])
    if not (vm[2] == 1 and m and m.group(4) in NUMERIC_ERRORS and st.get("outcome") == "vm-error"
            and st.get("error_in_loop_path")):
        return False
    if (st.get("line"), st.get("column")) != (int(m.group(2)), int(m.group(3))):
        return False
    # The stop is attributed: a large-number event after the VM's last line.
    return vm[0].count(b"\n") in set(st.get("large_lines") or ())


def numbers_match(legacy, vm, st, source=None):
    if not st.get("large_in_loop_path") or st.get("outcome") not in ("vm", "vm-error"):
        return False
    large, derived = set(st.get("large_lines") or ()), set(st.get("derived_lines") or ())
    if not large:
        return False
    stop = _numeric_stop(vm, st)
    if not stop and not (legacy[1] == vm[1] and legacy[2] == vm[2]):
        if not (legacy[2] == 1 and vm[2] == 1 and diagnostics_match(legacy[1], vm[1], st, source)):
            return False
    a, b = legacy[0].split(b"\n"), vm[0].split(b"\n")
    if stop and b[-1] == b"":
        b = b[:-1]  # legacy went on past the VM's stop: only its lines up to the stop are compared
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes():
        if tag == "equal":
            continue
        tail = stop and i2 == len(a) and j2 == len(b)
        if tag == "replace" and (i2 - i1 == j2 - j1 or (tail and i2 - i1 > j2 - j1)):
            if all(_line_ok(a[i1 + k], b[j1 + k], j1 + k, large, derived) for k in range(j2 - j1)):
                continue
            return False
        if tag == "delete" and tail:
            continue  # legacy's continuation after the VM's attributed stop
        if tag == "delete":
            # Re-evaluation: each extra legacy line repeats an adjacent legacy
            # line, and a large-number event is attributed to this position
            # (or the equivalent positions within the run of repeated lines).
            if not all((k > 0 and a[k] == a[k - 1]) or (k + 1 < len(a) and a[k] == a[k + 1]) for k in range(i1, i2)):
                return False
            run = 0
            while i2 + run < len(a) and a[i2 + run] == a[i2 - 1]:
                run += 1
            if not any(j in large for j in range(j1, j1 + run + 1)):
                return False
            continue
        return False
    return True


def classify(legacy, vm, st, source=None):
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
    if _recursion(legacy, vm, st):
        return "approved", "D-001#4 recursion"
    if (st.get("outcome") == "vm-error" and legacy[0] == vm[0] and legacy[2] == 1 and vm[2] == 1
            and diagnostics_match(legacy[1], vm[1], st, source)):
        return "approved", "D-018c diagnostics"
    if numbers_match(legacy, vm, st, source):
        return "approved", "D-018a numbers"
    return "diff", None
