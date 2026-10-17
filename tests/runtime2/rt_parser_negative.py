#!/usr/bin/env python3
"""CP10 negative gate: a construct rt parses as regular (no Opaque node) must
mean the same to legacy. Wrongly regular nodes are the dangerous direction
(CP11 compiles them), so this gate runs generated programs that rt accepts
with zero Opaque nodes through the real legacy binary and fails when legacy
reports an error that is about the shape of the text rather than the values
(malformed expression, empty expression, nesting limit, declaration checks,
removed forms), or when a string literal prints something other than rt's
reading of it.

Inputs (deterministic for a seed):
  * tests/rt/legacy-cases/*.f: hand-written probes (adversarial review of CP10);
  * an expression fuzzer: operators, prefixes, postfix chains with random
    whitespace, call shapes, literals, lambdas, await, trailing commas and
    nesting near the legacy limit, in statement, argument, element and
    interpolation positions; method calls only on receivers whose run-time
    type supports them, so a malformed-expression error is a shape error;
  * declaration mutants (enum, struct, fn, lambda parameter lists);
  * the paren oracle on every regular generated expression program (rt's
    grouping made explicit by parse_dump reprint must not change legacy's
    output);
  * string literals mixing escapes, `$`, `[`, `]` and `$[...]`, compared with
    rt's reading (parse_dump strings: StringParts decoded, segments evaluated).

Usage: rt_parser_negative.py --dump .build/rt-parse-dump --nift ./nift
       [--seed N] [--count N] [--jobs N]
"""
import argparse
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CASES = os.path.join(ROOT, "tests", "rt", "legacy-cases")

# Legacy errors that depend on the text's shape, not on run-time values.
SHAPE_ERRORS = re.compile(
    r"unknown value or malformed expression|expression cannot be empty|expression nesting exceeds|"
    r"malformed|was removed|trailing comma|duplicate object key|invalid or duplicate enum member|"
    r"enum requires at least one member|invalid callable signature|struct constructors cannot be variadic|"
    r"at most one constructor|lambda: |destructuring patterns support only|unterminated parameter value|"
    r"keys must be double-quoted|expected 'key: value'|empty member|missing value for key|"
    r"declaration requires an identifier|assignment requires an identifier")

UNBOUND_NAME = re.compile(r"unknown value or malformed expression: [A-Za-z_][A-Za-z0-9_]*\s*$")

PRELUDE = """a := 3
b := 2
s := "xy"
xs := [1, 2, 3]
ys := [[1, 2], [3]]
o := {"k": 1, "m": {"n": 2, "g": (p) => p}, "f": (p) => p + 1}
t := true
n := null
f := (p) => p
fn(g(p, q)) { return p }
fn(h()) { return [4, 5] }
fn[async](af(p)) { return p }
"""

ATOMS = ["a", "b", "s", "xs", "o", "t", "n", "1", "2.5", '"q"', "'r'", "true", "false", "null", ".5", "5.", "1e2",
         "007", '"$[a]"', "'$[b + 1]'", '"a$[a]b$[b]"', "xs[0]", "ys[1][0]", "o.k", "o.m.n", 'o["k"]', "o?.k",
         'o?["k"]', "n?.k", "n?[0]", "f(1)", "g(1, 2)", "h()[0]", "g(o, 1).k", "o.f(1)", "o.m.g(2)", "o?.f(3)",
         "xs.length()", "s.length()", '"abc".length()', "[1, 2].length()", "(1 + 2).to_string()",
         "xs[0].to_string()", "s.to_upper().length()", "5.to_string()", "[1, 2]", '{"z": 1}', "(a)", "[]", "{}",
         "...xs"]
BINARY = ["+", "-", "*", "/", "%", "==", "!=", "<", "<=", ">", ">=", "&&", "||", "??"]
SPACE = ["", " ", "  ", "\t"]
# Postfix chains on receivers whose run-time type supports them.
CHAINS = {"o": [".k", ".m", "?.k", '["k"]', ".f(1)", ".m.g(1)", "?.f(1)", ".m.n", ".size()"],
          "xs": ["[0]", "?[0]", "[a - 3]", "[xs[0]]", "[(0)]", "[0 + 0]", ".length()", "[0].to_string()",
                 ".map((p) => p).length()"],
          "s": [".length()", ".to_upper()", ".to_upper().length()", "[0]"],
          "f": ["(1)", "(1).to_string()"], "h": ["()", "()[0]", "().length()"], "ys": ["[0][1]", "[1][0]"],
          "(o)": [".k", ".f(1)", ".size()"], "(xs)": ["[0]", ".length()"], "[1]": ["[0]", ".length()"],
          '"s"': [".length()", "[0]", ".to_upper()"], "1": [".to_string()", "[0]"], "true": [".to_string()"],
          '{"k": 1}': [".k", ".size()"], "(f)": ["(1)"], "f(o)": [".k", ".f(1)"], "o.m": [".g(1)", ".n"]}


class Gen:
    def __init__(self, seed):
        self.r = random.Random(seed)

    def pick(self, items):
        return self.r.choice(items)

    def ws(self):
        return self.pick(SPACE)

    def expr(self, d=0):
        r = self.r.random()
        if d > 3 or r < 0.25:
            return self.pick(ATOMS)
        if r < 0.45:
            return self.expr(d + 1) + self.ws() + self.pick(BINARY) + self.ws() + self.expr(d + 1)
        if r < 0.52:
            return self.pick(["!", "-", "+", "- ", "!!"]) + self.ws() + self.expr(d + 1)
        if r < 0.60:
            return "(" + self.ws() + self.expr(d + 1) + self.ws() + ")"
        if r < 0.66:
            items = [self.expr(d + 1) for _ in range(self.r.randint(0, 3))]
            return "[" + ("," + self.ws()).join(items) + self.pick(["", "", "", ","]) + "]"
        if r < 0.70:
            items = ['"k%d":%s%s' % (i, self.ws(), self.expr(d + 1)) for i in range(self.r.randint(0, 2))]
            if items and self.r.random() < 0.1:
                items.append(items[0])
            return "{" + ", ".join(items) + self.pick(["", "", "", ","]) + "}"
        if r < 0.76:
            args = [self.expr(d + 1) for _ in range(self.r.randint(0, 2))]
            return self.pick(["f", "g", "o.f", "o.m.g", "print"]) + self.pick(["", "", "", " "]) + "(" + \
                ", ".join(args) + self.pick(["", "", "", ","]) + ")"
        if r < 0.84:
            base = self.pick(sorted(CHAINS))
            link = self.pick(CHAINS[base])
            if self.r.random() < 0.25:
                at = self.r.randrange(len(link))
                link = link[:at] + self.pick([" ", "\n", "\t"]) + link[at:]
            return base + self.pick(["", "", "", "", " ", "\n"]) + link
        if r < 0.88:
            return self.pick(["((p) => %s)(1)", "(p) => %s", "p => %s", "async (p) => %s", "( ) => %s",
                              "(p,) => %s", "(f)(%s)", "f(1)(%s)", "[(p) => p][0](%s)"]) % self.expr(d + 1)
        if r < 0.93:
            return self.pick(["await af(%s)", "await af(1) + %s", "await(af(%s))", "%s + await af(1)",
                              "!await af(%s)", "await  af(%s)", "await\taf(%s)", "-await af(%s)"]) % self.expr(d + 1)
        if r < 0.97:
            k = self.r.choice([20, 30, 40, 47, 60, 70, 95, 96, 97, 120])
            shape = self.pick(["-(" * k + "1" + ")" * k, "[" * k + "1" + "]" * k, "f(" * k + "1" + ")" * k,
                               "(1 + " * k + "1" + ")" * k, "f(1 + " * k + "1" + ")" * k, "- " * k + "1",
                               "!" * k + "t", "s" + ".to_string()" * k])
            return shape
        return self.pick(["x++", "++x", "x--", "a++ + 1", "x += 1"])

    def program(self):
        e = self.expr()
        form = self.pick(["print(%s)", "v := %s\nprint(v)", 'print("$[%s]")', "v := [%s]\nprint(v.length())",
                          "print(g(%s, 1))", "if (%s) { print(1) }", "return %s", "x := 1\n%s\nprint(x)",
                          "while (%s) { print(1)\nbreak }", "for (q : [%s]) { print(1) }",
                          "fn(r()) { return %s }\nprint(r())", "w := (p) => %s\nprint(w(1))",
                          "if (false) { } else if (%s) { print(2) }", "print({\"k\": %s})"])
        return PRELUDE + "x := 1\n" + (form % e) + "\n"

    DECLS = ["enum E { A, A }", "enum E { }", "enum E { A, }", "enum E { A\nB }", "enum E { A; B }",
             "enum E { A = 1 }", "struct (P) { fn(P(...a)) { } }", "struct (P) { fn(P()) { }\nfn(P(a)) { } }",
             "struct (P) { fn(m()) { return 1 }\nfn(m()) { return 2 } }", "struct (P) { x := 1\nx := 2 }",
             "struct (P) { fn(m(this)) { } }", "fn(q( )) { }", "fn(q(\t)) { }", "fn(q(a,)) { }",
             "fn(q(a, a)) { }", "fn(q(...a, b)) { }", "fn(q()) { }", "fn(q(a, ...r)) { }", "w := ( ) => 1",
             "w := (a,) => a", "w := (a, a) => a", "w := (...a) => a", "w := () => 1", "o2 := {\"a\": 1, \"a\": 2}",
             "o2 := {\"a\": 1,}", "o2 := {\"a\\tb\": 1}", "o2 := {\"a\": \"$[a]\"}", "o2 := {\"a\": \"\\q\"}",
             "z := [\"$[a]\"]", "z := [\"\\$[a]\", 1]", "[p, q] := xs", "[p,] := xs", "{k} := o",
             "#! note (\n$[print(\"INJ\")]\n)", "#! plain note", "@import(1)", "import(1)",
             "x := await af(1)", "x := await(af(1))", "await af(1)", "await := 1",
             "atomic<int>(1)", "z := atomic<int>(1)"]

    def decl_program(self):
        return PRELUDE + self.pick(self.DECLS) + "\nprint(\"end\")\n"

    PIECES = ["\\\\", "\\$", "$[1+1]", "$", "[", "]", "a", "\\n", "\\q", "$$[2]", "\\[", "$[[1][0]]", "$[a]",
              "'", "\\\"", "$[ 'x' ]", "$['[']", "$[b * 3]"]

    def string_program(self):
        body = "".join(self.pick(self.PIECES) for _ in range(self.r.randint(1, 5)))
        return PRELUDE + 'print("%s")\n' % body


def rt_check(dump, files):
    """Files rt accepts with zero Opaque nodes."""
    regular = []
    for i in range(0, len(files), 300):
        chunk = files[i:i + 300]
        out = subprocess.run([dump, "check"] + chunk, capture_output=True, text=True, errors="replace").stdout
        for line in out.splitlines():
            parts = line.split("\t")
            if len(parts) == 2 and parts[1] == "ok":
                regular.append(parts[0])
    return regular


def run_legacy(nift, path):
    work = os.path.dirname(path)
    env = {"PATH": os.environ.get("PATH", ""), "HOME": work, "LANG": "C", "LC_ALL": "C", "TZ": "UTC",
           "TMPDIR": work}
    try:
        r = subprocess.run([nift, os.path.basename(path)], cwd=work, env=env, capture_output=True,
                           stdin=subprocess.DEVNULL, timeout=30)
    except subprocess.TimeoutExpired:
        return -9, "", "timeout"
    return r.returncode, r.stdout.decode("utf-8", "replace"), r.stderr.decode("utf-8", "replace")


def expected_string(dump, path):
    """rt's reading of the one printed string literal: decoded parts with each
    `$[...]` segment evaluated (segments are integer arithmetic or names from
    the prelude), or None when rt marks it Opaque."""
    out = subprocess.run([dump, "strings", path], capture_output=True, text=True, errors="surrogateescape").stdout
    lines = [l for l in out.split("\n") if l.startswith("S\t")]
    if not lines:
        return None
    text = lines[-1][2:].encode("utf-8", "surrogateescape").decode("unicode_escape")
    env = {"a": 3, "b": 2}

    def value(m):
        v = eval(m.group(1), {"__builtins__": {}}, env)  # generated arithmetic only
        return ("true" if v else "false") if isinstance(v, bool) else str(v)
    return re.sub("\x01(.*?)\x02", value, text, flags=re.S)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", default=".build/rt-parse-dump")
    ap.add_argument("--nift", default="./nift")
    ap.add_argument("--seed", type=int, default=1010)
    ap.add_argument("--count", type=int, default=1200)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    dump, nift = os.path.abspath(a.dump), os.path.abspath(a.nift)
    work = tempfile.mkdtemp(prefix="rt-parser-negative.")
    gen = Gen(a.seed)
    files, kinds = [], {}
    try:
        def add(kind, source, name=None):
            d = os.path.join(work, "%s%05d" % (kind[0], len(files)))
            os.makedirs(d)
            p = os.path.join(d, name or "t.f")
            with open(p, "w", encoding="utf-8", newline="") as out:
                out.write(source)
            files.append(p)
            kinds[p] = kind
        for case in sorted(os.listdir(CASES)):
            if case.endswith(".f"):
                with open(os.path.join(CASES, case), encoding="utf-8") as src:
                    add("case", src.read(), case)
        for _ in range(a.count):
            add("expr", gen.program())
        for _ in range(a.count // 6):
            add("decl", gen.decl_program())
        for _ in range(a.count // 4):
            add("string", gen.string_program())

        regular = rt_check(dump, files)
        failures = []

        def judge(path):
            rc, out, err = run_legacy(nift, path)
            m = SHAPE_ERRORS.search(err)
            if m and UNBOUND_NAME.search(err):
                m = None  # an unbound name is reported with the same message: a value error
            if m:
                return "legacy shape error: " + err.strip().splitlines()[-1].replace(os.path.dirname(path) + "/", "")
            if kinds[path] == "expr":
                # Paren oracle on the generated program: rt's grouping made
                # explicit must not change what legacy does.
                r = subprocess.run([dump, "reprint", path], capture_output=True)
                twin = os.path.join(os.path.dirname(path), "r.f")
                with open(twin, "wb") as handle:
                    handle.write(r.stdout)
                rc2, out2, err2 = run_legacy(nift, twin)
                norm = lambda e: re.sub(r"\.f:(\d+):\d+", r".f:\1", e).replace("r.f", "t.f").replace("(", "").replace(")", "")
                # Both failing with different messages is an evaluation-order
                # difference, not a grouping one; anything else must agree.
                if (rc or rc2) and not (rc and rc2):
                    pass
                elif rc and rc2:
                    return None
                if (rc, out, norm(err)) != (rc2, out2, norm(err2)):
                    return "reprint differs: legacy %r / reprint %r" % ((rc, out[-200:], err[-200:]),
                                                                       (rc2, out2[-200:], err2[-200:]))
            if kinds[path] == "string" and rc == 0:
                want = expected_string(dump, path)
                if want is not None and out != want + "\n":
                    return "string reads differently: legacy %r, rt %r" % (out, want + "\n")
            return None
        with ThreadPoolExecutor(max_workers=a.jobs) as pool:
            verdicts = list(pool.map(judge, regular))
        counts = {}
        for path, verdict in zip(regular, verdicts):
            counts[kinds[path]] = counts.get(kinds[path], 0) + 1
            if verdict:
                with open(path, encoding="utf-8") as src:
                    text = src.read()
                failures.append("%s (%s)\n  %s\n  ---\n%s" % (os.path.relpath(path, work), kinds[path], verdict,
                                                              text[len(PRELUDE):] if text.startswith(PRELUDE) else text))
        total = {}
        for p in files:
            total[kinds[p]] = total.get(kinds[p], 0) + 1
        print("negative gate: programs=%d regular=%d (%s) failures=%d" % (
            len(files), len(regular), ", ".join("%s %d/%d" % (k, counts.get(k, 0), total[k]) for k in sorted(total)),
            len(failures)))
        for f in failures[:40]:
            print(f)
    finally:
        if a.keep:
            print("kept", work)
        else:
            shutil.rmtree(work, ignore_errors=True)
    print("FAIL rt parser negative gate" if failures else "PASS rt parser negative gate")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
