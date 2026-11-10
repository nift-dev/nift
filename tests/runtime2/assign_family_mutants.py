#!/usr/bin/env python3
"""Checkpoint 1 mutation controls (docs/runtime-2/checkpoints/combined-b3-b5.md section 95).

  assign_family_mutants.py --nift NIFT

Each mutant is an exact text edit of one production source, built by tests/runtime2/sanctioned_mutant.py
(only the edited unit, with nift's compile line, into .build/mutant-nift/assign-family/<name>; an edit
that no longer matches, a failed build or a mutant object identical to production is a harness
failure, never a detection). Each mutant must make tests/runtime2/assign_family.py fail, and the
named case must be among the failures. Run through make (HARNESS_TOOLCHAIN_ENV) after `make nift`.
Every mutant is reported; the directory is removed after each one.
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import sanctioned_mutant  # noqa: E402

CPATH = "src/rt/compiler_element_path.cpp"
VPATH = "src/rt/vm_element_path.cpp"
REPORT = "src/rt/vm_report.cpp"

STORE_ROOT = "    if (!root(c, c.ast_.at(path.front()->a), target, node.b, r) || !c.expr_any(node.b, value)) return false;\n"
STORE_KEY = "    if (!c.expr_any(path.back()->b, key)) return false;\n"
COMPOUND_RHS = "    if (!c.expr_any(node.b, value)) return false;\n    c.fb_->set_offset(node.begin);\n    switch (node.op) {"

MUTANTS = [
    # (name, the path it breaks, [(source, old, new)], a case that must fail)
    ("store-keys-before-value", "store: the value after the keys (H-A / option C order)",
     [(CPATH, STORE_ROOT, "    if (!root(c, c.ast_.at(path.front()->a), target, node.b, r)) return false;\n"),
      (CPATH, STORE_KEY, "    if (!c.expr_any(path.back()->b, key) || !c.expr_any(node.b, value)) return false;\n")],
     "order_store_value_then_keys"),
    ("compound-rhs-first", "compound: the rhs before the target is captured (D2 capture left out)",
     [(CPATH, "    std::uint16_t container = r.reg, key = 0, element = 0, value = 0;\n",
       "    std::uint16_t container = r.reg, key = 0, element = 0, value = 0;\n"
       "    if (!c.expr_any(node.b, value)) return false;\n"),
      (CPATH, COMPOUND_RHS, "    c.fb_->set_offset(node.begin);\n    switch (node.op) {")],
     "order_compound_index"),
    ("compound-key-reread", "compound: the final key read again after the rhs (legacy's quirk)",
     [(CPATH, COMPOUND_RHS, "    if (!c.expr_any(node.b, value) || !c.expr_any(path.back()->b, key)) return false;\n"
       "    c.fb_->set_offset(node.begin);\n    switch (node.op) {")],
     "order_compound_index"),
    ("compound-key-not-retained", "compound: a key in its binding's register is not retained across the rhs",
     [(CPATH, "!c.retain_receiver(key, key < mark, node.b)", "false")],
     "order_compound_index"),
    ("receiver-not-retained", "store/compound: the receiver is not retained across the rhs (read late)",
     [(CPATH, "    return c.retain_receiver(out.reg, !out.global && !out.indirect, rhs);\n", "    return true;\n")],
     "receiver_rebound_lambda_store"),
    ("compound-partial-commit", "compound: the rhs is committed before the operation (no atomicity)",
     [(CPATH, COMPOUND_RHS, "    if (!c.expr_any(node.b, value)) return false;\n"
       "    c.fb_->emit(OpCode::SetIndex, container, key, value);\n    c.fb_->set_offset(node.begin);\n"
       "    switch (node.op) {")],
     "atomic_session_failures"),
    ("store-step-omitted", "store: an intermediate step is left out (the write goes to the receiver)",
     [(CPATH, "        container = next;\n    }\n    std::uint16_t key = 0;",
       "        (void)next;\n    }\n    std::uint16_t key = 0;")],
     "nested_grid"),
    ("store-step-read-wording", "store: an intermediate step reports the read wording (no assign_path note)",
     [(CPATH, "        c.fb_->mark_assign_path(c.fb_->note(at, std::string(c.text(c.ast_.at(path[l]->b)))));\n",
       "        c.fb_->note(at, std::string(c.text(c.ast_.at(path[l]->b))));\n")],
     "fail_intermediate_range"),
    ("step-copies-container", "path step: the intermediate container is copied (value semantics, no alias)",
     [(REPORT, "                return note && note->assign_path && path_step(note, R[in.b], RK(in.c), out, fault);\n",
       "                return note && note->assign_path && path_step(note, R[in.b], RK(in.c), out, fault);\n"
       "            if (note && note->assign_path) (void)shallow_copy(Value(out), out, ~std::size_t(0));\n")],
     "alias_nested_extracted"),
    ("member-store-cycle-commits", "object store: a refused cycle still commits (nothing is atomic)",
     [(VPATH, "    if (out.failure == mutation::Failure::Cycle && cycle) {\n",
       "    if (out.failure == mutation::Failure::Cycle && cycle) {\n"
       "        (void)mutation::object_put(object, atom(string_view_of(key)), Value::integer(0), env);\n")],
     "cycle_object"),
    ("member-store-missing", "object store: SetIndex on an object is an internal failure again",
     [("src/rt/vm_ops.cpp", "        if (object.is(Kind::Object)) return set_member(",
       "        if (false) return set_member(")],
     "object_member_store"),
    ("step-missing-member-wording", "path step: a missing member reports the read wording",
     [(VPATH, '"object has no member: " + std::string(string_view_of(key))',
       '"JSON object has no key \'" + std::string(string_view_of(key)) + "\'"')],
     "fail_missing_intermediate"),
    ("future-path-admitted", "CP22 admission: a future stored through a nested path is admitted",
     [("src/rt/compiler_coro.cpp",
       "(ast_.at(p.a).kind == NodeKind::Index && ast_.at(ast_.at(p.a).a).kind == NodeKind::Ident)",
       "ast_.at(p.a).kind == NodeKind::Index")],
     "refused_future_nested"),
    # Review of C105-C108, P2-2: the two admission checks that keep legacy-error forms refused.
    ("object-number-key-admitted", "admission: an object level accepts a key of any kind",
     [(CPATH, "    const bool object = (receiver & ~kKindObject) == 0 && (key & ~kKindString) == 0;\n",
       "    const bool object = (receiver & ~kKindObject) == 0;\n")],
     "refused_object_number_key"),
    ("string-compound-any-op", "admission: two strings admitted for every compound operator, not only +=",
     [(CPATH, "    const bool strings = node.op == Op::AddAssign && l == kKindString && rk == kKindString;\n",
       "    const bool strings = l == kKindString && rk == kKindString;\n")],
     "refused_string_compound_sub"),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    args = ap.parse_args()
    os.chdir(ROOT)
    base = ROOT / ".build" / "mutant-nift" / "assign-family"
    failures = 0
    try:
        for name, what, edits, must in MUTANTS:
            out = base / name
            try:
                binary = sanctioned_mutant.build(out, edits=edits, quiet=True)
            except SystemExit as e:
                print("FAIL mutant %s: harness failure (build): %s" % (name, e))
                failures += 1
                continue
            p = subprocess.run([sys.executable, "-B", str(HERE / "assign_family.py"), "--nift", str(binary)],
                               capture_output=True, text=True)
            failed = [l[5:].split(":", 1)[0] for l in p.stdout.splitlines() if l.startswith("FAIL ")]
            if p.returncode == 0:
                print("FAIL mutant %s (%s): not caught" % (name, what))
                failures += 1
            elif must not in failed:
                print("FAIL mutant %s (%s): caught, but not by %s (failing: %s)"
                      % (name, what, must, ", ".join(failed[:6])))
                failures += 1
            else:
                print("PASS mutant %s (%s): caught by %d case(s), including %s"
                      % (name, what, len(failed) - 1, must))
            shutil.rmtree(out, ignore_errors=True)
    finally:
        shutil.rmtree(base, ignore_errors=True)
    if failures:
        print("FAIL assignment-family mutants: %d of %d" % (failures, len(MUTANTS)))
        return 1
    print("PASS assignment-family mutants: %d of %d caught" % (len(MUTANTS), len(MUTANTS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
