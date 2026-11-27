#!/usr/bin/env python3
"""CP12 phase B: modules on the VM (rt/module_link.h) against legacy.

  vm_modules.py --nift ./nift [--jobs 2] [--table]

Programs: every legacy semantics probe (tests/runtime2/semantics-modules),
the script fixtures of tests/runtime2/modules-d041 that agree with today's
legacy, and the shapes below (module functions and exported bindings used
in every context, nested modules, errors in a module's initialisation and
in its functions). Each runs in a fresh directory on legacy and on
NIFT_VM=1: the results must be identical or match a CP12 policy
(vm_policies.py: D-018c positions, legacy's `return: ` prefixes; the
`import: ` prefixes must be the same), and the outcome is pinned (`vm`:
ran on the VM; otherwise the refusal reason). --table prints outcomes
instead of checking the pins.
"""
import argparse
import concurrent.futures as cf
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import vm_policies  # noqa: E402

COUNTER = 'n := 0\nfn(inc()) {\n  n += 1\n  return n\n}\nexport(n)\nexport(inc)\n'
LOOPS = {
    "top": "{call}\n",
    "loop": "i0 := 0\nwhile (i0 < 2) {{\n  i0 += 1\n  {call}\n}}\n",
    "fn": "fn(wcall()) {{\n  {call}\n  return 0\n}}\nprint(wcall())\n",
    "fnloop": "fn(wcall()) {{\n  {call}\n  return 0\n}}\ni0 := 0\nwhile (i0 < 2) {{\n  i0 += 1\n  print(wcall())\n}}\n",
}

# name -> ({file: text}, expected outcome); main.f is the entry
SHAPES = {}
for ctx, tmpl in LOOPS.items():
    SHAPES[f"counter-{ctx}"] = ({"m.nft": COUNTER,
                                 "main.f": 'import("m.nft")\n' + tmpl.format(call="print(inc())") + "print(n)\n"}, "vm")
    SHAPES[f"exported-write-{ctx}"] = ({"m.nft": COUNTER,
                                        "main.f": 'import("m.nft")\n' + tmpl.format(call="n += 10") +
                                        "print(inc())\n"}, "vm")
    SHAPES[f"string-fn-{ctx}"] = ({"m.nft": 'sep := "-"\nfn(join2(a, b)) {\n  return a + sep + b\n}\nexport(join2)\n',
                                   "main.f": 'import("m.nft")\n' + tmpl.format(call='print(join2("x", "y"))')}, "vm")
SHAPES.update({
    "nested-chain": ({"sub/a.nft": 'import("../c.nft")\nfn(af()) {\n  return cf() * 2\n}\nexport(af)\n',
                      "c.nft": 'k := 21\nfn(cf()) {\n  return k\n}\nexport(cf)\n',
                      "main.f": 'import("sub/a.nft")\nprint(af())\n'}, "vm"),
    "init-order": ({"m.nft": 'print("m runs")\nv := [1, 2]\nexport(v)\n',
                    "main.f": 'print("before")\nimport("m.nft")\nprint(v.length())\nprint("after")\n'}, "vm"),
    "error-in-init": ({"m.nft": 'q := [1]\nx := q[3]\n', "main.f": 'print("pre")\nimport("m.nft")\nprint("post")\n'},
                      "vm"),
    "error-in-nested-init": ({"b.nft": 'import("c.nft")\n', "c.nft": 'x := [1]\nprint(x[2])\n',
                              "main.f": 'import("b.nft")\nprint(1)\n'}, "vm"),
    "error-in-fn-during-init": ({"m.nft": 'fn(mf(x)) {\n  y := [x]\n  return y[x]\n}\nz := mf(1)\n',
                                 "main.f": 'import("m.nft")\n'}, "vm"),
    "error-in-fn-after-init": ({"m.nft": 'fn(mf(x)) {\n  y := [x]\n  return y[x]\n}\nexport(mf)\n',
                                "main.f": 'print(1)\nimport("m.nft")\nprint(mf(2))\n'}, "vm"),
    "private-helper": ({"m.nft": 'fn(helper(x)) {\n  return x * 3\n}\nfn(api(x)) {\n  return helper(x) + 1\n}\nexport(api)\n',
                        "main.f": 'import("m.nft")\nprint(api(4))\n'}, "vm"),
    # Refused: the importer names the module's private function.
    "private-helper-used": ({"m.nft": 'fn(helper(x)) {\n  return x * 3\n}\nfn(api(x)) {\n  return helper(x)\n}\n'
                                      'export(api)\n',
                             "main.f": 'import("m.nft")\nprint(helper(4))\n'}, "import-scope"),
    # records20k: a user-call argument path rooted at a module's binding is
    # refused (the stage 4 lift passes a value; legacy a location).
    "path-arg-imported": ({"m.nft": 'o := {"k": 1}\nexport(o)\n',
                           "main.f": 'fn(f(s)) {\n  return s + 1\n}\nimport("m.nft")\nprint(f(o.k))\n'},
                          "path-in-call-argument:imported"),
    "path-arg-own-global": ({"main.f": 'fn(f(s)) {\n  return s + 1\n}\no := {"k": 1}\nprint(f(o.k))\n'}, "vm"),
    # D-182: both units own a separate binding; the module binding is private.
    "name-clash": ({"m.nft": 'tmp := 1\nfn(mf()) {\n  return tmp\n}\nexport(mf)\n',
                    "main.f": 'tmp := 2\nimport("m.nft")\nprint(mf() + tmp)\n'}, "vm"),
    "private-overlap-multiple": ({
        "m.nft": 'left := 1\nright := 2\nfn(mf()) { return left + right }\nexport(mf)\n',
        "main.f": 'left := 10\nright := 20\nimport("m.nft")\nprint(mf()); print(left + right)\n',
    }, "vm"),
    "private-overlap-inaccessible": ({
        "m.nft": 'hidden := 1\nfn(read_hidden()) { return hidden }\nexport(read_hidden)\n',
        "other.nft": 'fn(other_read()) { return hidden }\nexport(other_read)\n',
        "main.f": 'hidden := 2\nimport("m.nft")\nimport("other.nft")\nprint(other_read())\n',
    }, "import-scope"),
    "local-binding-not-import": ({
        "a.f": "hidden := 99\nfn(other()) { return hidden }\nexport(other)\n",
        "b.f": "fn(read_local(hidden)) { local := hidden; return local }\nexport(read_local)\n",
        "main.f": 'import("a.f")\nimport("b.f")\nprint(read_local(4))\n',
    }, "vm"),
    "local-declaration-not-import": ({
        "a.f": "hidden := 99\nfn(other()) { return hidden }\nexport(other)\n",
        "b.f": "fn(read_local()) { hidden := 4; return hidden }\nexport(read_local)\n",
        "main.f": 'import("a.f")\nimport("b.f")\nprint(read_local())\n',
    }, "vm"),
    "local-initializer-not-yet-bound": ({
        "a.f": "hidden := 99\nfn(other()) { return hidden }\nexport(other)\n",
        "b.f": "fn(read_local()) { hidden := hidden + 1; return hidden }\nexport(read_local)\n",
        "main.f": 'import("a.f")\nimport("b.f")\nprint(read_local())\n',
    }, "import-scope"),
    "local-callable-not-import": ({
        "a.f": "fn(hidden(x)) { return x + 99 }\nfn(other()) { return hidden(0) }\nexport(other)\n",
        "b.f": "fn(read_local()) { hidden := (x) => x + 1; return hidden(4) }\nexport(read_local)\n",
        "main.f": 'import("a.f")\nimport("b.f")\nprint(read_local())\n',
    }, "vm"),
    "local-block-exit-not-bound": ({
        "a.f": "hidden := 99\nfn(other()) { return hidden }\nexport(other)\n",
        "b.f": "fn(read_local()) { if (true) { hidden := 4 }; return hidden }\nexport(read_local)\n",
        "main.f": 'import("a.f")\nimport("b.f")\nprint(read_local())\n',
    }, "import-scope"),
    "struct-method-namespace": ({
        "a.f": "struct(Alpha) { private x := 3; fn(read()) { return this.x } }\nexport(Alpha)\n",
        "b.f": "struct(Beta) { private x := 5; fn(read()) { return this.x } }\nexport(Beta)\n",
        "main.f": 'import("a.f")\nimport("b.f")\na := Alpha()\nb := Beta()\nprint(a.read() + b.read())\n',
    }, "vm"),
    "struct-method-module-scalar-path": ({
        "m.f": 'struct(Box) { private fn(report(code)) { return {"code": code} }\n'
               'fn(read()) { result := {"exit_code": 7}; return this.report(result.exit_code) } }\nexport(Box)\n',
        "main.f": 'import("m.f")\nb := Box()\nr := b.read()\nprint(r.code)\n',
    }, "vm"),
    "struct-method-module-aggregate-path": ({
        "m.f": 'struct(Box) { private fn(report(value)) { return {"value": value} }\n'
               'fn(read()) { result := {"nested": [7]}; return this.report(result.nested) } }\nexport(Box)\n',
        "main.f": 'import("m.f")\nb := Box()\nr := b.read()\nprint(r.value)\n',
    }, "path-in-call-argument"),
    "struct-method-module-forwarded-path": ({
        "m.f": 'struct(Box) { private fn(change(code)) { return {"code": code} }\n'
               'private fn(report(code)) { return this.change(code) }\n'
               'fn(read()) { result := {"exit_code": 7}; return this.report(result.exit_code) } }\nexport(Box)\n',
        "main.f": 'import("m.f")\nb := Box()\nr := b.read()\nprint(r.code)\n',
    }, "path-in-call-argument"),
    "struct-method-module-variadic": ({
        "m.f": 'struct(ArgsBox) { fn(gather(head, ...tail)) { tail.push(head); return tail }\n'
               'fn(forward(head, ...tail)) { return this.gather(head, ...tail) } }\nexport(ArgsBox)\n',
        "main.f": 'import("m.f")\nb := ArgsBox()\n'
                  'collected_empty := b.gather(1)\ncollected_first := b.gather(2, 3, 4)\n'
                  'data := [5, 6]\ncollected_spread := b.gather(7, ...data, 8)\n'
                  'collected_forwarded := b.forward(9, 10)\n'
                  'print(collected_empty.stringify())\nprint(collected_first.stringify())\nprint(collected_spread.stringify())\n'
                  'print(data.stringify())\nprint(collected_forwarded.stringify())\n',
    }, "vm"),
    "struct-method-module-variadic-order": ({
        "m.f": 'struct(ArgsBox) { fn(gather(head, ...tail)) { tail.push(head); return tail } }\nexport(ArgsBox)\n',
        "main.f": 'import("m.f")\nb := ArgsBox()\ndata := [1, 2]\n'
                  'later_ordinary := () => { print("fixed"); data.push(9); return 4 }\n'
                  'early_expansion := () => { print("spread"); return data }\n'
                  'r := b.gather(later_ordinary(), ...early_expansion())\n'
                  'print(r.stringify())\nprint(data.stringify())\n',
    }, "vm"),
    "struct-method-entry-variadic-refusal": ({
        "main.f": 'struct(ArgsBox) { fn(gather(...tail)) { return tail } }\nprint("must not execute")\n',
    }, "fn-param"),
    "struct-method-module-args-parameter": ({
        "m.f": 'struct(Box) { fn(size_of(args)) { return args.size() } }\nexport(Box)\n',
        "main.f": 'import("m.f")\nb := Box()\nprint(b.size_of([3, 4]))\nprint(args.size())\n',
    }, "vm"),
    "struct-method-module-args-callee-refusal": ({
        "m.f": 'struct(Box) { fn(call_args(args)) { return args() } }\nexport(Box)\n',
        "main.f": 'import("m.f")\nprint("must not execute")\n',
    }, "param-name-reserved"),
    "struct-method-module-args-write-refusal": ({
        "m.f": 'struct(Box) { fn(change(args)) { args = []; return args } }\nexport(Box)\n',
        "main.f": 'import("m.f")\nprint("must not execute")\n',
    }, "param-name-reserved"),
    "struct-method-module-args-mutation-refusal": ({
        "m.f": 'struct(Box) { fn(change(args)) { args.push(3); return args.size() } }\nexport(Box)\n',
        "main.f": 'import("m.f")\nprint("must not execute")\n',
    }, "param-name-reserved"),
    "struct-method-module-args-custom-size-refusal": ({
        "m.f": 'struct(Box) { fn(check_size(args)) { return args.size() } }\n'
               'struct(Other) { fn(size()) { print("effect"); return 6 } }\nexport(Box)\nexport(Other)\n',
        "main.f": 'import("m.f")\nb := Box()\nc := Other()\nprint(b.check_size(c))\n',
    }, "param-name-reserved"),
    "struct-method-negated-boolean-short-circuit": ({
        "m.f": 'struct(Checks) { private fn(probe(value, label)) { print(label); return value }\n'
               'fn(check(left, right)) { if(!this.probe(left, "left") || !this.probe(right, "right")) { return true }; '
               'return false } }\nexport(Checks)\n',
        "main.f": 'import("m.f")\nc := Checks()\n'
                  'print(c.check(false, true))\nprint(c.check(true, true))\n',
    }, "vm"),
    "struct-method-negated-mixed-operators-refusal": ({
        "m.f": 'struct(Checks) { private fn(probe(value)) { return value }\n'
               'fn(check(left, right)) { return !this.probe(left) && !this.probe(right) } }\nexport(Checks)\n',
        "main.f": 'import("m.f")\nc := Checks()\nprint(c.check(false, true))\n',
    }, "prefix-not-precedence"),
    "struct-method-local-len-value": ({
        "m.f": 'struct(Box) { fn(count(value)) { len := value.length(); return len } }\nexport(Box)\n',
        "main.f": 'import("m.f")\nb := Box()\nprint(b.count("abc"))\n',
    }, "vm"),
    "struct-method-local-len-callee-refusal": ({
        "m.f": 'struct(Box) { fn(count(value)) { len := value.length(); return len() } }\nexport(Box)\n',
        "main.f": 'import("m.f")\nb := Box()\nprint(b.count("abc"))\n',
    }, "binding-name-reserved"),
    "struct-method-local-size-value": ({
        "m.f": 'struct(Box) { fn(size()) { return "method" }\nfn(render()) { size := ""; size = "large"; return size + this.size() } }\nexport(Box)\n',
        "main.f": 'import("m.f")\nb := Box()\nprint(b.render())\n',
    }, "vm"),
    "struct-method-local-size-callee-refusal": ({
        "m.f": 'struct(Box) { fn(render()) { size := ""; return size() } }\nexport(Box)\n',
        "main.f": 'import("m.f")\nprint("must not execute")\n',
    }, "binding-name-reserved"),
    "imported-prepared-integer-copy": ({
        "m.f": 'fn(count_cost()) {\n'
               'row := [3]\nk := 0\nwhile(k < 1) {\n deletion_cost := row[k] + 1\n'
               'best_cost := deletion_cost\n print(best_cost)\n k += 1\n }\n}\nexport(count_cost)\n',
        "main.f": 'import("m.f")\ncount_cost()\n',
    }, "vm"),
    "imported-prepared-shadowed-copy": ({
        "m.f": 'fn(count_cost()) {\n deletion_cost := 2.5\n row := [3]\nk := 0\nwhile(k < 1) {\n'
               ' deletion_cost := row[k] + 1\n best_cost := deletion_cost\n print(best_cost)\nk += 1\n }\n}\n'
               'export(count_cost)\n',
        "main.f": 'import("m.f")\ncount_cost()\n',
    }, "prepared-decl-type"),
    "imported-prepared-fractional-declaration-copy": ({
        "m.f": 'fn(count_cost()) {\nk := 0\nwhile(k < 1) {\n deletion_cost := 2.0\n'
               'best_cost := deletion_cost\n print(best_cost)\n k += 1\n }\n}\nexport(count_cost)\n',
        "main.f": 'import("m.f")\ncount_cost()\n',
    }, "prepared-decl-type"),
    "struct-method-owner-state": ({
        "m.f": 'counter_local := 6\nstruct(Box) {\nfn(read()) { return counter_local }\n'
               'fn(advance()) { counter_local += 1; return counter_local }\n}\nexport(Box)\n',
        "main.f": 'import("m.f")\nb := Box()\nc := Box()\n'
                  'print(b.read())\nprint(b.advance())\nprint(c.read())\n',
    }, "vm"),
    "struct-method-owner-state-late-refusal": ({
        "m.f": 'struct(Box) { fn(read()) { return counter_local } }\n'
               'counter_local := 6\nexport(Box)\n',
        "main.f": 'import("m.f")\nb := Box()\nprint(b.read())\n',
    }, "free-name"),
    "imported-guarded-string-contains": ({
        "m.f": 'needle_text := "a"\nstruct(Box) { fn(check(value)) { return type(value) == "string" && '
               'value.contains(needle_text) } }\nexport(Box)\n',
        "main.f": 'import("m.f")\nb := Box()\nprint(b.check("abc"))\nprint(b.check(3))\nprint(b.check(map()))\nprint(b.check(b))\nprint(b.check("xyz"))\n',
    }, "vm"),
    "imported-guarded-different-receiver-refusal": ({
        "m.f": 'struct(Box) { private fn(needle()) { print("argument"); return "a" }\n'
               'fn(check(value, other)) { return type(value) == "string" && '
               'other.contains(this.needle()) } }\nexport(Box)\n',
        "main.f": 'import("m.f")\nb := Box()\nprint(b.check("abc", 3))\nprint(b.check(3, "abc"))\n',
    }, "builtin-receiver:contains"),
    "struct-method-mixed-negation-short-circuit": ({
        "m.f": 'struct(Checks) {\n private fn(test_bool(v)) { print("left"); return v }\n'
               'private fn(test_number(v)) { print("right"); return v }\n'
               'fn(check(left, right)) { return !this.test_bool(left) || !(this.test_number(right) > 0) }\n'
               'fn(check_negative(left, right)) { return !this.test_bool(left) || this.test_number(right) < 0 } }\n'
               'export(Checks)\n',
        "main.f": 'import("m.f")\nc := Checks()\n'
                  'print(c.check(false, 2))\nprint(c.check(true, 0))\nprint(c.check(true, 2))\n'
                  'print(c.check_negative(false, -1))\nprint(c.check_negative(true, -1))\n'
                  'print(c.check_negative(true, 1))\n',
    }, "vm"),
    "imported-prepared-other-integer-binding-copy": ({
        "m.f": 'fn(outer_cost()) {\n minc := 0\n return count_cost()\n}\n'
               'fn(count_cost()) {\n row := [3]\n k := 0\n while(k < 1) {\n'
               ' minc := row[k]\n j := minc\n print(j)\n k += 1\n }\n}\nexport(outer_cost)\n',
        "main.f": 'import("m.f")\nouter_cost()\n',
    }, "vm"),
    "imported-branch-short-circuit-type-proof": ({
        "m.f": 'struct(Box) {\nfn(check(items)) {\nfor(value : items) {\n'
               'if(type(value) == "object" && value.has("key")) { print("hit") }\n'
               'else { print("skip") }\n}\n}\n}\nexport(Box)\n',
        "main.f": 'import("m.f")\nb := Box()\nb.check(["text"])\n',
    }, "vm"),
    "imported-successive-branch-type-proofs": ({
        "m.f": 'struct(Box) {\nfn(check(items)) {\nfor(value : items) {\n'
               'if(type(value) == "object" && value.has("key")) {\n'
               'payload := value.get("data", [])\n'
               'if(type(payload) != "array") { return null }\n'
               'print(value.get("key"))\nprint(payload.size())\n'
               '}\n}\nreturn null\n}\n}\nexport(Box)\n',
        "main.f": 'import("m.f")\nb := Box()\n'
                  'b.check(["skip", {"key":"hit","data":[1]}])\n'
                  'b.check([{"key":"unreachable","data":false}])\n',
    }, "vm"),
    "imported-else-if-proof-does-not-escape": ({
        "m.f": 'struct(Box) {\nfn(check(skip, value)) {\n'
               'if(skip) { print("skip") }\n'
               'else if(type(value) != "object") { return null }\n'
               'return value.has("key")\n}\n}\nexport(Box)\n',
        "main.f": 'import("m.f")\nb := Box()\nprint(b.check(true, "text"))\n',
    }, "builtin-receiver:has"),
    "imported-utf8-encode": ({
        "m.f": 'struct(Box) { fn(to_bytes(text)) { return text.encode("utf-8") } }\nexport(Box)\n',
        "main.f": 'import("m.f")\nb := Box()\na := b.to_bytes("é😀")\n'
                  'print(a.length())\nprint(a[0])\nprint(a[2])\n',
    }, "vm"),
    "imported-utf8-encode-legacy-prefix": ({
        "m.f": 'struct(Box) { fn(convert(text)) { data := text.encode("utf-8"); return data } }\nexport(Box)\n',
        "main.f": 'import("m.f")\nb := Box()\ns := open("input.txt")\nprint(b.convert(s).size())\n',
        "input.txt": "\x1fnift:ordinary",
    }, "vm"),
    "imported-utf8-encode-legacy-prefix-return": ({
        "m.f": 'struct(Box) { fn(convert(text)) { return text.encode("utf-8") } }\nexport(Box)\n',
        "main.f": 'import("m.f")\nb := Box()\ns := open("input.txt")\nprint(b.convert(s).size())\n',
        "input.txt": "\x1fnift:ordinary",
    }, "vm"),
    "imported-cached-numeric-type": ({
        "m.f": 'struct(Box) { fn(show(value)) { label := type(value); '
               'if(label == "int" || label == "float") { return value.to_string() }; '
               'return "other" } }\nbox := Box()\nexport(box)\n',
        "main.f": 'import("m.f")\nprint(box.show(7))\nprint(box.show(1.5))\nprint(box.show([]))\n',
    }, "vm"),
    "imported-cached-numeric-type-rebound": ({
        "m.f": 'struct(Box) { fn(show(value)) { label := type(value); label = "int"; '
               'if(label == "int" || label == "float") { return value.to_string() }; '
               'return "other" } }\nbox := Box()\nexport(box)\n',
        "main.f": 'import("m.f")\nprint(box.show(7))\nprint(box.show("kept"))\n',
    }, "vm"),
    "imported-cached-string-type": ({
        "m.f": 'struct(Box) { private fn(skip(value)) { print(value); return value == "skip" }\n'
               'fn(show(value)) { label := type(value); if(label == "string") { '
               'if(this.skip(value)) { return null }; return "[" + value.replace("a", "b") + "]" }; '
               'return "other" } }\nbox := Box()\nexport(box)\n',
        "main.f": 'import("m.f")\nprint(box.show("a"))\nprint(box.show("skip"))\nprint(box.show([]))\n',
    }, "vm"),
    "imported-nonnull-return-members": ({
        "m.f": 'struct(Box) {\n private fn(data(flag)) { if(flag) { return {"code": 7} }; return null }\n'
               'private fn(report(code)) { return {"message": code} }\n'
               'fn(read(flag)) { item := this.data(flag); if(item != null) { return this.report(item.code) }; '
               'return {"message": 0} } }\nexport(Box)\n',
        "main.f": 'import("m.f")\nb := Box()\na := b.read(true)\nc := b.read(false)\n'
                  'print(a.message)\nprint(c.message)\n',
    }, "vm"),
    "imported-nonnull-return-result": ({
        "m.f": 'struct(Box) {\n private fn(data(flag)) { if(flag) { return {"code": 7} }; return null }\n'
               'fn(read(flag)) { item := this.data(flag); if(item != null) { return item }; '
               'return {"code": 0} } }\nbox := Box()\nexport(box)\n',
        "main.f": 'import("m.f")\na := box.read(true)\nb := box.read(false)\nprint(a.code)\nprint(b.code)\n',
    }, "vm"),
    "imported-null-return-result": ({
        "m.f": 'struct(Box) {\n private fn(data(flag)) { if(flag) { return {"code": 7} }; return null }\n'
               'fn(read(flag)) { item := this.data(flag); if(item == null) { return item }; '
               'return {"code": 0} } }\nbox := Box()\nexport(box)\n',
        "main.f": 'import("m.f")\na := box.read(false)\nprint(a.code)\n',
    }, "type:member"),
    "imported-nonnull-return-intervening-statement": ({
        "m.f": 'struct(Box) {\n private fn(data(flag)) { if(flag) { return {"code": 7} }; return null }\n'
               'fn(read(flag)) { item := this.data(flag); if(item != null) { print("before"); return item.code }; '
               'return 0 } }\nexport(Box)\n',
        "main.f": 'import("m.f")\nb := Box()\nprint(b.read(true))\n',
    }, "type:member"),
    "struct-private-type": ({
        "m.f": "struct(Secret) { x := 3 }\n",
        "main.f": 'import("m.f")\np := Secret()\nprint(p.x)\n',
    }, "import-scope"),
    "struct-before-import": ({
        "m.f": "struct(Box) { x := 3 }\nexport(Box)\n",
        "main.f": 'print("before")\np := Box()\nimport("m.f")\nprint(p.x)\n',
    }, "struct-after-statement"),
    "struct-after-module-data": ({
        "m.f": 'prefix := "A" + ":"\ncache := map()\nyields := [1, 2, 4, 8]\n'
               'struct(Box) { x := 3 }\nexport(Box)\nexport(yields)\n',
        "main.f": 'import("m.f")\np := Box()\nprint(p.x)\nprint(yields.stringify())\n',
    }, "vm"),
    "struct-after-effectful-array-prefix": ({
        "m.f": 'fn(effect()) { print("before"); return 3 }\n'
               'values := [effect()]\nstruct(Box) { x := 3 }\nexport(Box)\n',
        "main.f": 'import("m.f")\nprint("after")\n',
    }, "struct-after-statement"),
    "struct-after-module-ascii-bytes": ({
        "m.f": 'prefix := bytes([0, 65, 27]).decode("utf-8") + ":"\n'
               'struct(Box) { x := 3 }\nexport(Box)\nexport(prefix)\n',
        "main.f": 'import("m.f")\nb := Box()\nprint(b.x)\nprint(prefix)\n',
    }, "vm"),
    "struct-after-module-unproved-bytes": ({
        "m.f": 'prefix := bytes([255]).decode("utf-8")\n'
               'struct(Box) { x := 3 }\nexport(Box)\n',
        "main.f": 'import("m.f")\nprint("after")\n',
    }, "struct-after-statement"),
    "struct-after-module-tag-prefix": ({
        "m.f": 'prefix := bytes([31, 110, 105, 102, 116, 58]).decode("utf-8")\n'
               'struct(Box) { x := 3 }\nexport(Box)\n',
        "main.f": 'import("m.f")\nprint("after")\n',
    }, "struct-after-statement"),
    "struct-after-module-user-call": ({
        "m.f": 'fn(announce_prefix()) { print("module effect"); return 7 }\ncount := announce_prefix()\n'
               'struct(Box) { x := 3 }\nexport(Box)\n',
        "main.f": 'import("m.f")\np := Box()\nprint(p.x)\n',
    }, "struct-after-statement"),
    "struct-after-independent-module-construction": ({
        "a.f": "struct(Alpha) { x := 3 }\na := Alpha()\nexport(a)\n",
        "b.f": "struct(Beta) { y := 5 }\nb := Beta()\nexport(b)\n",
        "main.f": 'import("a.f")\nimport("b.f")\nprint(a.x)\nprint(b.y)\n',
    }, "vm"),
    "struct-after-foreign-type-reference": ({
        "a.f": 'print("before")\na := Beta()\n',
        "b.f": "struct(Beta) { y := 5 }\nexport(Beta)\n",
        "main.f": 'import("a.f")\nimport("b.f")\nprint("after")\n',
    }, "import-scope"),
    "struct-after-dependent-module-effect": ({
        "a.f": 'print("before")\nimport("b.f")\n',
        "b.f": "struct(Beta) { y := 5 }\nexport(Beta)\n",
        "main.f": 'import("a.f")\nprint("after")\n',
    }, "struct-after-statement"),
    "struct-reexport": ({
        "a.f": "struct(Token) { n := 3 }\nexport(Token)\n",
        "b.f": 'import("a.f")\nexport(Token)\n',
        "main.f": 'import("b.f")\nt := Token()\nprint(t.n)\n',
    }, "vm"),
    # Refused: a module's bare top-level return ends only the module in legacy.
    "module-bare-return": ({"m.nft": 'print("a")\nreturn\n', "main.f": 'import("m.nft")\nprint("b")\n'}, "module-bare-return"),
})

# Expected outcomes of the legacy probes and fixtures (directory -> outcome).
PROBES = {
    "cycle": "import-error:cycle", "diamond": "import-reinitialised", "dynamic_path": "import-dynamic",
    "export_collision": "import-scope:collision", "export_copy": "binding-name-reserved", "export_missing": "module-error:export-missing",
    "in_block": "import-nested", "in_function": "import-nested", "missing": "import-error:missing",
    "module_bare_return": "module-bare-return", "module_error": "type:-", "module_free_unbound": "free-name",
    # D-182: module-private hidden and the importer binding stay distinct.
    "module_private": "vm", "module_private_late": "vm", "module_return": "module-error:return-value",
    "module_vs_caller_local": "import-scope", "nested_relative": "vm", "not_reexported": "import-scope",
    "package_missing": "import-error:package", "repeated": "import-reinitialised",
    "same_file_two_spellings": "import-reinitialised", "self_cycle": "import-error:cycle", "side_effect_order": "vm",
    "struct_export": "vm", "template_import": "binding-name-reserved",
}


def run(nift, d, env):
    p = subprocess.run([os.environ.get("NIFT_TEST_TIMEOUT", "timeout"), "30", nift, "main.f"], cwd=d, capture_output=True,
                       env=dict({"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "LANG": "C"}, **env))
    stats = os.path.join(d, "stats.jsonl")
    st = json.loads(open(stats).read().splitlines()[-1]) if os.path.exists(stats) else {}
    if os.path.exists(stats):
        os.remove(stats)
    return p.returncode, p.stdout, p.stderr, st


def check(args, work, item):
    name, files, want = item
    d = tempfile.mkdtemp(dir=work)
    if isinstance(files, str):
        shutil.copytree(files, d, dirs_exist_ok=True)
    else:
        for rel, text in files.items():
            os.makedirs(os.path.dirname(os.path.join(d, rel)), exist_ok=True)
            with open(os.path.join(d, rel), "w") as f:
                f.write(text)
    a = run(args.nift, d, {"NIFT_VM": "0"})  # legacy oracle: explicit (VM-default policy)
    b = run(args.nift, d, {"NIFT_VM": "1", "NIFT_VM_STATS": os.path.join(d, "stats.jsonl")})
    src = open(os.path.join(d, "main.f"), "rb").read()
    verdict, policy = vm_policies.classify((a[1], a[2], a[0]), (b[1], b[2], b[0]), b[3], src)
    outcome = b[3].get("outcome")
    label = "vm" if outcome in ("vm", "vm-error") else (b[3].get("reason") or outcome)
    problems = [] if verdict in ("same", "approved") else [f"legacy {a[:3]!r} != vm {b[:3]!r}"]
    if not args.table and want is not None and label != want:
        problems.append(f"expected {want}, got {label}")
    # Owner 2026-11-10 13:26, item 1 (combined-b3-b5.md section 81): the approved B4 C1 behaviour of an
    # import-scope collision is legacy's exact error line, rejected before any part of the program runs.
    if want == "import-scope:collision" and label == want:
        c = run(args.nift, d, {"NIFT_VM_STATS": os.path.join(d, "stats.jsonl")})  # the VM default
        legacy_error = [l for l in a[2].splitlines() if l.startswith(b"error: ")]
        if (c[3].get("vm_executions"), c[3].get("legacy_executions")) != (0, 0) or c[1] != b"" or c[0] != a[0] \
                or not legacy_error or c[2].splitlines()[:1] != legacy_error[:1]:
            problems.append(f"import-scope:collision is not legacy's error rejected before running: {c[:3]!r}")
    return name, label if verdict != "approved" else f"{label} ({policy})", problems


# ---- random module programs (--gen) ------------------------------------

def random_modules(seed, index):
    """A random import tree: modules with private and exported bindings and
    functions (names prefixed per unit, so they stay apart), the entry
    calling exported functions and writing exported bindings in every
    context, occasional run-time errors."""
    import random
    rng = random.Random(seed * 7919 + index)
    files = {}
    count = rng.randint(1, 3)
    exported = {}  # module -> [(name, kind)]

    def body_for(prefix, names):
        nums = [n for n, k in names if k == "int"]
        arrs = [n for n, k in names if k == "array"]
        strs = [n for n, k in names if k == "string"]
        options = []
        if nums:
            n = rng.choice(nums)
            options += [f"{n} += {rng.randint(1, 3)}", f"{n} = {n} * 2", f"{n}++"]
        if arrs:
            a = rng.choice(arrs)
            options += [f"{a}.push({rng.randint(0, 9)})", f"{a}[0] = {rng.randint(0, 9)}"]
        if strs:
            options.append(f'{rng.choice(strs)} += "{rng.choice("xyz")}"')
        return rng.choice(options) if options else "q0 := 1"

    def show(n, k):
        return f"{n}.stringify()" if k == "array" else n

    for m in range(count):
        prefix = f"m{m}"
        names = [(f"{prefix}v{k}", rng.choice(["int", "array", "string"])) for k in range(rng.randint(1, 3))]
        lines = []
        if m + 1 < count and rng.random() < 0.5:
            lines.append(f'import("m{m + 1}.nft")')
            exported_below = exported.setdefault(m + 1, [])
        else:
            exported_below = None
        for n, k in names:
            lines.append(f"{n} := " + {"int": str(rng.randint(0, 5)), "array": "[1, 2]", "string": '"s"'}[k])
        if rng.random() < 0.3:
            lines.append(f'print("{prefix} init")')
        fns = []
        for f in range(rng.randint(1, 2)):
            fname = f"{prefix}f{f}"
            stmts = [body_for(prefix, names) for _ in range(rng.randint(1, 2))]
            if rng.random() < 0.2:
                stmts.append(f"e0 := [1]\n  print(e0[{rng.randint(0, 3)}])")
            n, k = rng.choice(names)
            stmts.append(f"return {show(n, k)}")
            lines.append(f"fn({fname}()) {{\n  " + "\n  ".join(stmts) + "\n}")
            fns.append(fname)
        if exported_below is not None and rng.random() < 0.5:
            lines.append("placeholder-calls")
        for fname in fns:
            lines.append(f"export({fname})")
        for n, k in names:
            if rng.random() < 0.5:
                lines.append(f"export({n})")
                exported.setdefault(m, []).append((n, k))
        exported.setdefault(m, [])
        exported[m] += [(fname, "fn") for fname in fns]
        files[f"m{m}.nft"] = lines
    # Calls of the imported module's functions inside a module (after its import).
    for m in range(count):
        lines = files[f"m{m}.nft"]
        if "placeholder-calls" in lines:
            below = [n for n, k in exported.get(m + 1, []) if k == "fn"]
            lines[lines.index("placeholder-calls")] = f"print({rng.choice(below)}())" if below else ""
        files[f"m{m}.nft"] = "\n".join(lines) + "\n"
    top = [n for n, k in exported[0]]
    calls = []
    fns = [n for n, k in exported[0] if k == "fn"]
    vals = [(n, k) for n, k in exported[0] if k != "fn"]
    for _ in range(rng.randint(1, 4)):
        stmt = f"print({rng.choice(fns)}())"
        if vals and rng.random() < 0.3:
            n, k = rng.choice(vals)
            stmt = {"int": f"{n} += 5", "array": f"{n}.push(8)", "string": f'{n} += "w"'}[k]
        r = rng.random()
        if r < 0.25:
            stmt = "i0 = 0\nwhile (i0 < 2) {\n  i0 += 1\n  " + stmt + "\n}"
        elif r < 0.4:
            stmt = "fn(w%d()) {\n  %s\n  return 0\n}\nprint(w%d())" % (len(calls), stmt, len(calls))
        calls.append(stmt)
    shows = [f"print({show(n, k)})" for n, k in vals]
    files["main.f"] = 'import("m0.nft")\ni0 := 0\n' + "\n".join(calls + shows) + "\n"
    del top
    return files


def check_random(args, work, index):
    files = random_modules(args.seed, index)
    name, label, problems = check(args, work, (f"random #{index}", files, None))
    return index, label, [p for p in problems if not p.startswith("expected")], files


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--table", action="store_true")
    ap.add_argument("--gen", type=int, default=0)
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    args.nift = os.path.abspath(args.nift)
    items = [(n, files, want) for n, (files, want) in SHAPES.items()]
    probes = os.path.join(HERE, "semantics-modules")
    for d in sorted(os.listdir(probes)):
        if os.path.isdir(os.path.join(probes, d, "p")):
            items.append((f"probe:{d}", os.path.join(probes, d, "p"), PROBES.get(d, "?")))
    fixtures = os.path.join(HERE, "modules-d041")
    for d in sorted(os.listdir(fixtures)):
        spec = json.load(open(os.path.join(fixtures, d, "spec.json")))
        if spec["legacy"] == "agrees" and spec["steps"][0].get("run") == ["main.f"]:
            items.append((f"d041:{d}", os.path.join(fixtures, d, "p"), FIXTURES.get(d, "vm")))
    failures, tally = 0, {}
    with tempfile.TemporaryDirectory(prefix="vm-modules-") as work:
        with cf.ThreadPoolExecutor(args.jobs) as pool:
            for name, label, problems in pool.map(lambda it: check(args, work, it), items):
                tally[label] = tally.get(label, 0) + 1
                if args.table:
                    print(f"{name:36} {label}{'  DIFF' if problems else ''}")
                if problems:
                    failures += 1
                    print(f"FAIL {name}: " + "; ".join(problems))
            if args.gen:
                gen_tally = {}
                for index, label, problems, files in pool.map(lambda i: check_random(args, work, i), range(args.gen)):
                    gen_tally[label] = gen_tally.get(label, 0) + 1
                    if problems:
                        failures += 1
                        print(f"FAIL random seed {args.seed} #{index}: " + "; ".join(problems))
                        for rel, text in sorted(files.items()):
                            print(f"--- {rel}\n{text}")
                on_vm = sum(n for k, n in gen_tally.items() if k.startswith("vm"))
                print(f"random: {args.gen} module programs (seed {args.seed}); on the VM {on_vm}; "
                      f"outcomes {dict(sorted(gen_tally.items(), key=str))}")
    status = "PASS" if failures == 0 else "FAIL"
    print(f"{status} vm modules: {len(items)} programs; outcomes {dict(sorted(tally.items(), key=str))}; "
          f"{failures} failure(s)")
    return 1 if failures else 0


# Expected outcomes of the agreeing D-041 fixtures that are not on the VM.
FIXTURES = {"export_clash": "import-scope:collision", "circular": "import-error:cycle", "self_cycle": "import-error:cycle",
            "doc_try_import": "import-nested", "doc_math": "vm", "import_path_print": "vm",
            "import_path_read": "path-in-call-argument", "import_path_write": "alias-argument",
            "nested_chain": "import-scope"}

if __name__ == "__main__":
    sys.exit(main())
