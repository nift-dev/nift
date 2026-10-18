"""Runtime 2.0 CP12: the approved legacy/VM difference policies (shared).

Every differential harness (vm_adversarial.py, vm_parity.py,
vm_engine_selection.py, vm_former_replay.py, vm_d018.py) compares a legacy
run with a NIFT_VM=1 run through classify(). A run pair is either identical
or matches exactly one of these narrow, named policies; anything else is a
difference that fails the gate. Each policy requires evidence from the VM
statistics line, so a policy can only approve a difference in the situation
it was written for (tests/runtime2/vm_d018.py shows old and new behaviour
side by side for each, and the non-approval cases).

D-018b (print(path) with a missing element evaluates its keys once) has no
policy: in the compiled subset legacy's second resolution is not observable
(keys with calls make legacy run the statement on its textual evaluator, and
the re-resolution's message is the canonical one), so any difference there
fails the gate; vm_d018.py checks those programs are identical.

  D-001#4 recursion   legacy fails with "callable recursion depth exceeded"
                      at its 64-frame limit; the VM went past it
                      (beyond_legacy_depth), legacy's output is a prefix of
                      the VM's, and the VM completed or stopped with a program
                      error.
  D-018a numbers      one number rule everywhere and exactly-once
                      evaluation: legacy computes loop arithmetic in doubles
                      and re-evaluates a loop statement whose value reaches
                      2^53 (calls included). Requires large_in_loop_path > 0
                      (the VM met such a number in code legacy runs on its
                      loop evaluator). Output lines may differ only in their
                      numbers, legacy may print extra lines that repeat lines
                      it printed (the re-evaluated calls), and the VM may stop
                      with "signed 64-bit integer overflow" / "arithmetic
                      result is not finite" where legacy continued.
  D-018c diagnostics  errors reported where they occur (D-009#1, #5): same
                      output, both exit 1 with one diagnostic line on the same
                      file; the messages are equal once legacy's "return: "
                      prefixes are removed or, only where the error happened
                      in loop-evaluator code (error_in_loop_path), legacy's
                      loop wording maps to the canonical one (MESSAGE_MAP);
                      the VM position is not before legacy's (legacy reports
                      the enclosing `if`, `return` or index expression), or
                      the error happened in loop-evaluator code (legacy
                      reports the statement, the outermost loop statement or
                      the caller's call site there).
"""
import difflib
import re

RECURSION_ERROR = re.compile(rb"^error: [^\n]*: callable recursion depth exceeded: [A-Za-z_][A-Za-z0-9_]*\n\Z")
DIAGNOSTIC = re.compile(rb"^error: (.*):(\d+):(\d+): ([^\n]*)\n\Z")
NUMBER = re.compile(rb"-?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?|-?inf|-?nan")
NUMERIC_ERRORS = (b"signed 64-bit integer overflow", b"arithmetic result is not finite")
MISSING_ELEMENT = re.compile(rb"^(JSON array index \d+ is out of range in |JSON array indices must be non-negative "
                             rb"integers in |JSON object has no key |value has no member: )")
# Legacy loop-evaluator wording -> canonical wording (VM), D-009#1.
MESSAGE_MAP = [
    (re.compile(rb"^cannot change binding type: (\w+)\Z"),
     lambda m: re.compile(rb"^cannot assign \w+ to \w+ binding '" + re.escape(m.group(1)) + rb"'\Z")),
    (re.compile(rb"^invalid index\Z"), lambda m: MISSING_ELEMENT),
    (re.compile(rb"^invalid index target\Z"),
     lambda m: re.compile(rb"^(index out of range: |assignment index does not address an array or object: )")),
]
RETURN_PREFIX = b"return: "

POLICIES = ("D-001#4 recursion", "D-018a numbers", "D-018c diagnostics")


def _lines(data):
    return data.split(b"\n")


def _numeric_equal(a, b):
    return NUMBER.sub(b"N", a) == NUMBER.sub(b"N", b)


def _strip_return(message):
    while message.startswith(RETURN_PREFIX):
        message = message[len(RETURN_PREFIX):]
    return message


def diagnostics_match(legacy_err, vm_err, st):
    """D-018c on the two stderr streams (one diagnostic line each)."""
    lm, vm = DIAGNOSTIC.match(legacy_err), DIAGNOSTIC.match(vm_err)
    if not lm or not vm or lm.group(1) != vm.group(1):
        return False
    in_loop = bool(st.get("error_in_loop_path"))
    legacy_msg, vm_msg = _strip_return(lm.group(4)), vm.group(4)
    if legacy_msg != vm_msg:
        if not in_loop:
            return False
        if not any(p.match(legacy_msg) and q(p.match(legacy_msg)).match(vm_msg) for p, q in MESSAGE_MAP):
            return False
    legacy_pos = (int(lm.group(2)), int(lm.group(3)))
    vm_pos = (int(vm.group(2)), int(vm.group(3)))
    return vm_pos >= legacy_pos or in_loop


def _recursion(legacy, vm, st):
    return (st.get("beyond_legacy_depth") and legacy[2] == 1 and RECURSION_ERROR.match(legacy[1])
            and vm[0].startswith(legacy[0])
            and ((st.get("outcome") == "vm" and vm[2] == 0 and vm[1] == b"")
                 or (st.get("outcome") == "vm-error" and vm[2] == 1)))


def numbers_match(legacy, vm, st):
    if not st.get("large_in_loop_path") or st.get("outcome") not in ("vm", "vm-error"):
        return False
    vm_numeric_stop = vm[2] == 1 and any(e in vm[1] for e in NUMERIC_ERRORS) and DIAGNOSTIC.match(vm[1])
    if not vm_numeric_stop and not (legacy[1] == vm[1] and legacy[2] == vm[2]):
        if not (legacy[2] == 1 and vm[2] == 1 and diagnostics_match(legacy[1], vm[1], st)):
            return False
    a, b = _lines(legacy[0]), _lines(vm[0])
    if vm_numeric_stop and b[-1] == b"":
        b = b[:-1]  # legacy went on past the VM's stop: compare the VM's lines with a prefix of legacy's
    printed = {NUMBER.sub(b"N", line) for line in b}  # VM lines, numbers abstracted
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes():
        if tag == "equal":
            continue
        if tag == "replace" and i2 - i1 == j2 - j1 and all(_numeric_equal(x, y) for x, y in zip(a[i1:i2], b[j1:j2])):
            continue
        if tag == "delete" and all(NUMBER.sub(b"N", x) in printed for x in a[i1:i2]):
            continue  # a re-evaluation repeats lines the program also printed once
        if vm_numeric_stop and i2 == len(a) and j2 == len(b) and tag in ("delete", "replace"):
            continue  # after the VM's overflow stop legacy went on (its last line may be partial)
        return False
    return True


def classify(legacy, vm, st):
    """(verdict, policy): verdict 'same', 'approved', 'endless' or 'diff';
    policy is one of POLICIES for 'approved', else None. `legacy` and `vm`
    are (stdout, stderr, exit status) with bytes streams; `st` the VM
    statistics (NIFT_VM_STATS) of the VM run."""
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
            and diagnostics_match(legacy[1], vm[1], st)):
        return "approved", "D-018c diagnostics"
    if numbers_match(legacy, vm, st):
        return "approved", "D-018a numbers"
    return "diff", None
