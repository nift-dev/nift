"""D-040 (owner, 2026-10-20; reaffirmed for CP14B automatic mode 2026-10-25):
exact independent-copy witnesses, never a general alias-difference policy.

Under D-040 an ordinary value assigned to a second binding is an independent
value: mutating one never mutates the other. Legacy aliases certain syntactic
shapes. CP14B admits collection mutation through such a binding, so the VM's
D-040 result becomes observable where legacy aliases.

A run pair is approved only when ALL of the following hold for one witness:
  - the program's bytes hash to the witness's sha256 (exact source);
  - legacy's (stdout, stderr, exit) equals the witness's legacy triple;
  - the VM's (stdout, stderr, exit) equals the witness's VM triple;
  - the VM statistics show the program ran on the VM: outcome "vm", exactly
    one VM execution and no legacy execution.
Any other difference -- another program, an extra or changed output line,
stderr, exit status, a refusal, a replay -- is not approved.

Each witness names where it is checked. tests/runtime2/d040_witnesses_selftest.py
pins the rejection of an unrelated difference and of D-040 lookalikes.
"""
import hashlib

DECISION = "D-040#1"


def _h(text):
    return hashlib.sha256(text.encode()).hexdigest()


_REPLAY_ALIAS = "a := [1, 2]\nb := a\nb.push(3)\nprint(a.length())\n"
_ALIAS_PLACE = ("a := [1, 2]\nb := a\nb.push(3)\nprint(a.stringify())\nb = [7]\n"
                "print(a.stringify())\nprint(b.stringify())\n")
_ADV_BODY = ('o := {"k": [1]}\na := [[1]]\nn := {"k": [[1]]}\nb := [1]\ny := b\ny.push(2)\n'
             'print(o.k.length())\nprint(b.length())\nprint(o.k[0])')
_ADV_TOP = _ADV_BODY + "\n"
_ADV_FN_TOP = "fn(w0()) {\n" + _ADV_BODY + "\nreturn 0\n}\nprint(w0())\n"

# (id, sha256 of the exact source, legacy triple, VM triple, where checked)
WITNESSES = (
    ("vm-corpus/cp11/replay_alias.f", _h(_REPLAY_ALIAS),
     (b"3\n", b"", 0), (b"2\n", b"", 0), "vm_parity.py corpus and rt-diff"),
    ("semantics/alias_place.f", _h(_ALIAS_PLACE),
     (b"[1,2,3]\n[1,2,3]\n[7]\n", b"", 0), (b"[1,2]\n[1,2]\n[7]\n", b"", 0), "vm_parity.py rt-diff"),
    ("adversarial alias-decl-5-y.push(2)-top", _h(_ADV_TOP),
     (b"1\n2\n1\n", b"", 0), (b"1\n1\n1\n", b"", 0), "vm_adversarial.py"),
    ("adversarial alias-decl-5-y.push(2)-fn-top", _h(_ADV_FN_TOP),
     (b"1\n2\n1\n0\n", b"", 0), (b"1\n1\n1\n0\n", b"", 0), "vm_adversarial.py"),
    # vm_review_cp12.py D1: 199/201-function chains then the alias tail; the
    # sources are generated there and pinned here by hash.
    ("review D1 chain199-alias", "a3b749fe3a8a99ffaba4642c54354297a4a25124bec1e9af6969c0c5a811b1f8",
     (b"2\n", b"", 0), (b"1\n", b"", 0), "vm_review_cp12.py"),
    ("review D1 chain201-alias", "72eaff58bcc0813ef0c473e41a7e3b27769624f8d164a08ffb17b4d8487259a6",
     (b"2\n", b"", 0), (b"1\n", b"", 0), "vm_review_cp12.py"),
)


def ran_once_on_vm(st):
    return bool(st) and st.get("outcome") == "vm" and st.get("vm_executions") == 1 and \
        st.get("legacy_executions") == 0


def match(source, legacy, vm, st):
    """The witness id this run pair is, or None. `source` is bytes or str;
    `legacy`/`vm` are (stdout bytes, stderr bytes, exit) triples."""
    if not ran_once_on_vm(st):
        return None
    if isinstance(source, str):
        source = source.encode()
    digest = hashlib.sha256(source).hexdigest()
    for wid, sha, want_legacy, want_vm, _where in WITNESSES:
        if digest == sha and tuple(legacy) == want_legacy and tuple(vm) == want_vm:
            return wid
    return None


def by_id(wid):
    for w in WITNESSES:
        if w[0] == wid:
            return w
    raise KeyError(wid)
