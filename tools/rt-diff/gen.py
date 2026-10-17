#!/usr/bin/env python3
"""Seeded generator of small, deterministic Nift scripts for rt-diff (CP08).

Each program mixes the constructs listed in docs/runtime-2/semantics.md:
declarations and assignment, integer/float/string arithmetic, interpolation,
arrays and maps (aliasing and copy-at-call), functions, recursion, closures,
dynamic scoping, structs, if/else chains, while/for loops (which exercise the
loop path's prepared evaluator), try/catch/throw, and Host operations (file
writes/reads, exists, ls, getenv). Most programs are valid; a small share ends
in a deliberate runtime error so diagnostics are compared too.

Also: floats, booleans and && || !, break/continue, string methods, modules
(import/export), async functions and threads, and generated template sites
(partials, @if, @for, @json, @pathto, <pre>, comments, @script; 1-4 build
threads). Every program's first line records its seed and index.

Usage: gen.py --count N --seed S --out DIR [--sites M]
       (writes DIR/gNNNNN/main.f and DIR/siteNNNN/)
Programs never read clocks, randomness, the process id or the network.
"""
import argparse
import os
import random


class Gen:
    def __init__(self, rng):
        self.r = rng
        self.lines = []
        self.ints = []
        self.strs = []
        self.arrs = []
        self.maps = []
        self.fns = []        # (name, arity)
        self.structs = []    # (name, fields)
        self.counter = 0
        self.depth = 0
        self.files = {}      # extra files (modules) written beside main.f

    def name(self, prefix):
        self.counter += 1
        return "%s%d" % (prefix, self.counter)

    def emit(self, text):
        self.lines.append("  " * self.depth + text)

    # expressions -----------------------------------------------------------
    def int_expr(self, d=0):
        r = self.r
        choices = ["lit", "lit"]
        if self.ints:
            choices += ["var", "var"]
        if d < 2:
            choices += ["bin", "bin"]
        if self.arrs and d < 2:
            choices.append("len")
        if self.fns and d < 1:
            choices.append("call")
        kind = r.choice(choices)
        if kind == "lit":
            return str(r.choice([0, 1, 2, 3, 5, 7, 10, 42, 100, -4]))
        if kind == "var":
            return r.choice(self.ints)
        if kind == "len":
            return "%s.length()" % r.choice(self.arrs)
        if kind == "call":
            fname, arity = r.choice(self.fns)
            return "%s(%s)" % (fname, ", ".join(self.int_expr(d + 1) for _ in range(arity)))
        op = r.choice(["+", "-", "*", "%", "/", "+", "*"])
        right = self.int_expr(d + 1)
        if op in ("%", "/"):
            right = str(r.choice([1, 2, 3, 7]))
        return "(%s %s %s)" % (self.int_expr(d + 1), op, right)

    def str_expr(self):
        r = self.r
        kind = r.choice(["lit", "interp", "concat", "method"] + (["var"] if self.strs else []))
        if kind == "method":
            base = self.str_expr_simple()
            return r.choice(["%s.to_upper()", '%s.replace("p", "P")', "%s.substr(0, 1)",
                             '%s.split("-").stringify()']) % base
        if kind == "lit":
            return '"%s"' % r.choice(["a", "hello", "x,y,z", "", "Nift", "$", "tab\\tend"])
        if kind == "interp":
            inner = r.choice(self.ints) if self.ints else "1 + 2"
            return '"v=$[%s]!"' % inner
        if kind == "var":
            return r.choice(self.strs)
        return "%s + %s" % (self.str_expr_simple(), self.str_expr_simple())

    def str_expr_simple(self):
        if self.strs and self.r.random() < 0.5:
            return self.r.choice(self.strs)
        return '"%s"' % self.r.choice(["p", "q", "-"])

    def cond(self):
        a, b = self.int_expr(1), self.int_expr(1)
        simple = "%s %s %s" % (a, self.r.choice(["<", ">", "==", "!=", "<=", ">="]), b)
        roll = self.r.random()
        if roll < 0.15:
            return "(%s) && (%s)" % (simple, self.cond_simple())
        if roll < 0.3:
            return "(%s) || !(%s)" % (simple, self.cond_simple())
        return simple

    def cond_simple(self):
        return "%s %s %s" % (self.int_expr(1), self.r.choice(["<", "==", ">="]), self.int_expr(1))

    def float_expr(self):
        r = self.r
        return "(%s %s %s)" % (r.choice(["0.5", "1.25", "2.0", "0.1", "3.75"]), r.choice(["+", "*", "-", "/"]),
                               r.choice(["0.2", "2", "4.5", "1.0"]))

    # statements ------------------------------------------------------------
    def stmt(self):
        r = self.r
        options = ["decl_int", "decl_int", "decl_str", "print", "print", "decl_arr", "if"]
        if self.ints:
            options += ["assign", "compound"]
        if self.arrs:
            options += ["push", "alias", "index", "sort"]
        if self.depth < 2:
            options += ["while", "for", "try"]
        if self.depth < 2:
            options += ["float", "bool", "strmethods", "loopctl"]
        if self.depth == 0:
            options += ["fn", "fn", "closure", "struct", "map", "fs", "env", "recursion", "copycall", "dynscope",
                        "module", "async", "thread"]
        getattr(self, "s_" + r.choice(options))()

    def s_decl_int(self):
        n = self.name("i")
        self.emit("%s := %s" % (n, self.int_expr()))
        if self.depth == 0:
            self.ints.append(n)

    def s_decl_str(self):
        n = self.name("s")
        self.emit("%s := %s" % (n, self.str_expr()))
        if self.depth == 0:
            self.strs.append(n)

    def s_decl_arr(self):
        n = self.name("a")
        items = ", ".join(self.int_expr(1) for _ in range(self.r.randint(0, 4)))
        self.emit("%s := [%s]" % (n, items))
        if self.depth == 0:
            self.arrs.append(n)

    def show(self, name):
        return name + ".stringify()" if name in self.arrs or name.startswith(("m", "al", "a")) else name

    def s_print(self):
        pool = self.ints + self.strs + self.arrs
        if pool and self.r.random() < 0.7:
            self.emit("print(%s)" % self.show(self.r.choice(pool)))
        else:
            self.emit("print(%s)" % self.str_expr())

    def s_assign(self):
        self.emit("%s = %s" % (self.r.choice(self.ints), self.int_expr()))

    def s_compound(self):
        self.emit("%s %s %s" % (self.r.choice(self.ints), self.r.choice(["+=", "-=", "*="]), self.r.choice(["1", "2", "3"])))

    def s_push(self):
        self.emit("%s.push(%s)" % (self.r.choice(self.arrs), self.int_expr(1)))

    def s_sort(self):
        a = self.r.choice(self.arrs)
        self.emit("%s.sort()" % a)
        self.emit("print(%s.stringify())" % a)

    def s_index(self):
        a = self.r.choice(self.arrs)
        self.emit("if(%s.length() > 0) { print(%s[0]) } else { print(\"empty\") }" % (a, a))

    def s_alias(self):
        a = self.r.choice(self.arrs)
        b = self.name("al")
        self.emit("%s := %s" % (b, a))
        self.emit("%s.push(%d)" % (b, self.r.randint(0, 9)))
        self.emit("print(%s.stringify())" % a)
        self.emit("print(%s.stringify())" % b)
        self.emit("%s = [0]" % b)
        self.emit("print(%s.stringify())" % a)
        self.emit("print(%s.stringify())" % b)

    def s_if(self):
        self.emit("if(%s) {" % self.cond())
        self.block(1, 2)
        if self.r.random() < 0.4:
            self.emit("} else if(%s) {" % self.cond())
            self.block(1, 2)
        if self.r.random() < 0.6:
            self.emit("} else {")
            self.block(1, 2)
        self.emit("}")

    def s_while(self):
        v = self.name("w")
        self.emit("%s := 0" % v)
        self.emit("while(%s < %d) {" % (v, self.r.randint(0, 3)))
        self.block(1, 3)
        self.depth += 1
        self.emit("%s += 1" % v)
        self.depth -= 1
        self.emit("}")

    def s_for(self):
        v = self.name("e")
        src = self.r.choice(self.arrs) if self.arrs else "[1, 2, 3]"
        self.emit("for(%s : %s) {" % (v, src))
        self.depth += 1
        self.emit("print(%s)" % v)
        self.emit('print("item $[%s]")' % v)
        self.depth -= 1
        self.block(0, 1)
        self.emit("}")

    def s_try(self):
        self.emit("try {")
        self.depth += 1
        if self.r.random() < 0.5:
            self.emit('throw error("boom %d")' % self.r.randint(0, 9))
        else:
            self.emit("print(%s)" % self.int_expr(1))
        self.depth -= 1
        e = self.name("err")
        self.emit("} catch(%s) {" % e)
        self.depth += 1
        self.emit("print(%s.message)" % e)
        self.depth -= 1
        self.emit("}")

    def s_fn(self):
        n = self.name("f")
        arity = self.r.randint(0, 2)
        params = [self.name("p") for _ in range(arity)]
        saved = self.ints
        self.ints = saved + params
        body = self.int_expr(1)
        self.ints = saved
        self.emit("fn(%s(%s)) { return %s }" % (n, ", ".join(params), body))
        self.fns.append((n, arity))
        self.emit("print(%s(%s))" % (n, ", ".join(self.int_expr(1) for _ in range(arity))))

    def s_closure(self):
        n = self.name("mk")
        self.emit("fn(%s(k)) { return (x) => x * k + %s }" % (n, self.r.choice(self.ints) if self.ints else "1"))
        c = self.name("cl")
        self.emit("%s := %s(%d)" % (c, n, self.r.randint(1, 4)))
        self.emit("print(%s(%d))" % (c, self.r.randint(0, 5)))
        if self.ints:
            self.emit("%s = %s + 1" % (self.ints[0], self.ints[0]))
            self.emit("print(%s(1))" % c)

    def s_struct(self):
        n = self.name("S")
        self.emit("struct(%s) { a := 0; b := 1; fn(total()) { return a + b } }" % n)
        v = self.name("o")
        self.emit("%s := %s()" % (v, n))
        self.emit("%s.b = %d" % (v, self.r.randint(0, 5)))
        w = self.name("o")
        self.emit("%s := %s" % (w, v))
        self.emit("%s.a = %d" % (w, self.r.randint(10, 20)))
        self.emit("print(%s.total())" % v)
        self.emit("print(%s.a)" % v)

    def s_map(self):
        m = self.name("m")
        self.emit('%s := {"k": %s, "s": %s}' % (m, self.int_expr(1), self.str_expr_simple()))
        self.emit('print(%s["k"])' % m)
        self.emit('print(%s["s"])' % m)
        self.emit("print(%s.stringify())" % m)

    def s_float(self):
        n = self.name("fl")
        self.emit("%s := %s" % (n, self.float_expr()))
        self.emit("print(%s)" % n)

    def s_bool(self):
        n = self.name("b")
        self.emit("%s := %s" % (n, self.r.choice(["true", "false", "(%s)" % self.cond()])))
        self.emit("print(%s && !%s)" % (n, self.r.choice(["false", "true", n])))
        self.emit("print(%s || false)" % n)

    def s_strmethods(self):
        n = self.name("t")
        self.emit('%s := "%s"' % (n, self.r.choice(["alpha-beta", "Nift", "x-y-z", "pq"])))
        self.emit("print(%s.length())" % n)
        self.emit("print(%s.to_upper())" % n)
        self.emit('print(%s.contains("a"))' % n)
        self.emit('print(%s.starts_with("N"))' % n)
        self.emit('print(%s.split("-").stringify())' % n)

    def s_loopctl(self):
        v = self.name("lc")
        stop = self.r.randint(1, 4)
        skip = self.r.randint(0, 3)
        self.emit("%s := 0" % v)
        self.emit("while(true) {")
        self.depth += 1
        self.emit("%s += 1" % v)
        self.emit("if(%s > %d) { break }" % (v, stop))
        self.emit("if(%s == %d) { continue }" % (v, skip))
        self.emit("print(%s)" % v)
        self.depth -= 1
        self.emit("}")
        e = self.name("fe")
        self.emit("for(%s : [1, 2, 3, 4]) {" % e)
        self.depth += 1
        self.emit("if(%s == %d) { continue }" % (e, self.r.randint(1, 4)))
        self.emit("if(%s == %d) { break }" % (e, self.r.randint(2, 5)))
        self.emit("print(%s)" % e)
        self.depth -= 1
        self.emit("}")

    def s_module(self):
        m = self.name("mod")
        fname = self.name("mf")
        self.files["%s.f" % m] = "fn(%s(x)) { return x * %d + 1 }\nexport(%s)\n" % (fname, self.r.randint(2, 5), fname)
        self.emit('import("%s.f")' % m)
        self.emit("print(%s(%s))" % (fname, self.int_expr(1)))

    def s_async(self):
        f = self.name("af")
        self.emit("@fn[async](%s(n)) {" % f)
        self.depth += 1
        path = "async%d.txt" % self.counter
        self.emit('h := file("%s" + n); h.open("w"); h.write("v" + n); h.save(); h.close()' % path)
        self.emit('ok := exists("%s" + n)' % path)
        self.emit('rm("%s" + n)' % path)
        self.emit("return n * 2")
        self.depth -= 1
        self.emit("}")
        a, b = self.name("fu"), self.name("fu")
        # Distinct arguments: the two tasks run concurrently and must not
        # race on the same file (a same-binary run would then differ).
        first = self.r.randint(1, 9)
        second = self.r.choice([k for k in range(1, 10) if k != first])
        self.emit("%s := %s(%d)" % (a, f, first))
        self.emit("%s := %s(%d)" % (b, f, second))
        self.emit("print(await %s)" % a)
        self.emit("print(await %s)" % b)

    def s_thread(self):
        f = self.name("tw")
        self.emit("fn(%s(n)) { return is_dir(\".\") }" % f)
        t = self.name("th")
        self.emit("%s := thread(%s, %d)" % (t, f, self.r.randint(0, 9)))
        self.emit("print(%s.join())" % t)

    def s_fs(self):
        f = "f%d.txt" % self.counter
        h = self.name("h")
        self.emit('%s := file("%s")' % (h, f))
        self.emit('%s.open("w")' % h)
        # A non-empty write: saving an empty buffer creates no file (review #8).
        self.emit('%s.write("data-" + %s)' % (h, self.str_expr()))
        self.emit("%s.save()" % h)
        self.emit("%s.close()" % h)
        self.emit('print(exists("%s"))' % f)
        self.emit('print(is_file("%s"))' % f)
        self.emit('print(is_dir("."))')
        self.emit('print(open("%s"))' % f)
        self.emit('print(ls("*.txt").length() > 0)')
        if self.r.random() < 0.5:
            self.emit('rm("%s")' % f)
            self.emit('print(exists("%s"))' % f)

    def s_env(self):
        self.emit('print(getenv("NIFT_RT_DIFF_UNSET"))')
        self.emit('print(getenv("LANG"))')

    def s_recursion(self):
        n = self.name("rec")
        self.emit("fn(%s(n)) { if(n <= 1) { return 1 } return n + %s(n - 1) }" % (n, n))
        depth = self.r.choice([5, 10, 30, 63, 64, 70])
        self.emit("print(%s(%d))" % (n, depth))

    def s_copycall(self):
        n = self.name("mut")
        self.emit("fn(%s(xs)) { xs.push(99); return xs.length() }" % n)
        a = self.name("a")
        self.emit("%s := [1, 2]" % a)
        self.emit("print(%s(%s))" % (n, a))
        self.emit("print(%s.stringify())" % a)

    def s_dynscope(self):
        f = self.name("free")
        g = self.name("outer")
        z = self.name("zz")
        self.emit("fn(%s()) { return %s }" % (f, z))
        self.emit("fn(%s()) { %s := %d; return %s() }" % (g, z, self.r.randint(0, 9), f))
        self.emit("print(%s())" % g)

    def block(self, low, high):
        self.depth += 1
        for _ in range(self.r.randint(low, high)):
            self.stmt()
        self.depth -= 1

    def program(self):
        for _ in range(self.r.randint(4, 14)):
            self.stmt()
        if self.r.random() < 0.08:
            # Deliberate final runtime error, so diagnostics are compared too.
            self.emit(self.r.choice(["print(undefined_name_%d)" % self.counter,
                                     "x%d := [1][5]" % self.counter,
                                     "print(1 / 0)",
                                     "print(9223372036854775807 + 1)",
                                     'throw error("final")']))
        return "\n".join(self.lines) + "\n"


def site(rng, index, seed):
    """A generated template site: partials, conditionals, loops, JSON data,
    interpolation, pagination-free multi-page output with 1-4 build threads.
    (Directives inside HTML comments are executed by Nift, so comments here
    hold plain text only.)"""
    pages = rng.randint(1, 5)
    files = {}
    files[".nift/config.json"] = ('{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/",'
                                  '"output-ext":".html","default-template":"templates/template.html",'
                                  '"build-threads":%d,"incremental-mode":"modified"}}\n' % rng.randint(1, 4))
    names = ["/"] + ["pg%d" % i for i in range(1, pages)]
    files[".nift/tracked.json"] = '{"tracked":[%s]}\n' % ",".join(
        '{"name":"%s","title":"T%d","template":"templates/template.html"}' % (n, i) for i, n in enumerate(names))
    files["templates/template.html"] = rng.choice([
        "<html><body>\n@content\n</body></html>\n",
        "<html><head>@input(\"templates/head.html\")</head><body>\n  @content\n</body></html>\n"])
    files["templates/head.html"] = "<title>$[title]</title>\n"
    files["data/d.json"] = '{"items":[%s],"flag":%s}\n' % (
        ",".join('{"n":%d}' % rng.randint(0, 9) for _ in range(rng.randint(0, 4))), rng.choice(["true", "false"]))
    for i, n in enumerate(names):
        body = []
        for _ in range(rng.randint(2, 6)):
            k = rng.random()
            if k < 0.2:
                j = "d%d" % len(body)
                body.append('@json(%s, "data/d.json")\n@for(x : %s.items){<li>$[x.n]</li>}' % (j, j))
            elif k < 0.35:
                body.append('@if(%d > %d){<b>yes</b>}else{<i>no</i>}' % (rng.randint(0, 5), rng.randint(0, 5)))
            elif k < 0.5:
                body.append('<a href="@pathto(%s)">l</a>' % rng.choice(names))
            elif k < 0.6:
                body.append('@script { v := %d; return v * 2 }' % rng.randint(1, 9))
            elif k < 0.7:
                body.append('<pre>\n  keep  {braces}\n</pre>')
            elif k < 0.8:
                body.append('<!-- comment with {braces} -->')
            elif k < 0.9:
                body.append('@script { return exists("data/d.json") }')
            else:
                body.append("plain text %d" % rng.randint(0, 99))
        files["content/%s.html" % ("index" if n == "/" else n)] = "\n".join(body) + "\n"
    return files


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--count", type=int, required=True)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--out", required=True)
    parser.add_argument("--sites", type=int, default=-1, help="generated template sites (default count/20)")
    options = parser.parse_args()
    os.makedirs(options.out, exist_ok=True)
    for index in range(options.count):
        rng = random.Random("%d:%d" % (options.seed, index))
        directory = os.path.join(options.out, "g%05d" % index)
        os.makedirs(directory, exist_ok=True)
        gen = Gen(rng)
        program = gen.program()
        # Every program records how to regenerate it exactly.
        with open(os.path.join(directory, "main.f"), "w") as handle:
            handle.write("// rt-diff gen.py --seed %d index %d\n" % (options.seed, index) + program)
        with open(os.path.join(directory, "rt-diff.only"), "w") as handle:
            handle.write("main.f\n")
        for name, content in gen.files.items():
            with open(os.path.join(directory, name), "w") as handle:
                handle.write(content)
    sites = options.count // 20 if options.sites < 0 else options.sites
    for index in range(sites):
        rng = random.Random("site:%d:%d" % (options.seed, index))
        directory = os.path.join(options.out, "site%04d" % index)
        for name, content in site(rng, index, options.seed).items():
            full = os.path.join(directory, name)
            os.makedirs(os.path.dirname(full), exist_ok=True)
            with open(full, "w") as handle:
                handle.write(content)


if __name__ == "__main__":
    main()
