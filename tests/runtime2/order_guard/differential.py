#!/usr/bin/env python3
"""Differential harness for OrderGuard: every generated program runs on legacy
(NIFT_VM=0) and on the VM (NIFT_VM=require when available, else default).
A VM refusal (no part ran) is counted, never compared; anything else must
match legacy exactly: stdout, stderr (paths normalised) and exit status."""
import itertools, os, re, subprocess, sys, tempfile

NIFT = sys.argv[1]
OUT = sys.argv[2] if len(sys.argv) > 2 else None

VALUES = ['5', '-1', '1.5', '0', '"x"', '"b"', '""', 'null', 'true', '[1]', '{"k":1}']
RIGHT = ['0', '"a"', '2.5']
OPS = ['>', '<', '>=', '<=']

# Contexts: (name, body template with {c} = comparison expression, uses param a)
CONTEXTS = {
    'if':        'if({c}) {{ return "t" }} return "f"',
    'if_not':    'if(!({c})) {{ return "t" }} return "f"',
    'if_or':     'if(a == "zz" || {c}) {{ return "t" }} return "f"',
    'if_and':    'if(a != "zz" && {c}) {{ return "t" }} return "f"',
    'elseif':    'if(a == "zz") {{ return "z" }} else if({c}) {{ return "t" }} return "f"',
    'ret':       'return {c}',
    'ret_or':    'return a == "zz" || {c}',
    'ret_not':   'return !({c})',
    'decl':      'y := {c}\n    return y',
    'decl_and':  'y := a != "zz" && ({c})\n    return y',
    'assign':    'y := false\n    y = {c}\n    return y',
    'while':     'n := 0\n    while({c} && n < 1) {{ n += 1 }}\n    return n',
    'for_if':    'for(i : [1]) {{ if({c}) {{ return "t" }} }}\n    return "f"',
    'for_decl':  'r := 0\n    for(i : [1]) {{ y := {c}\n        r = y }}\n    return r',
    'for_ret':   'for(i : [1]) {{ return {c} }}\n    return "f"',
    'while_if':  'n := 0\n    while(n < 1) {{ n += 1\n        if({c}) {{ return "t" }} }}\n    return "f"',
}
CALLERS = {
    'direct':    'print(h({v}))',
    'decl':      'z := h({v})\nprint(z)',
    'ret':       'fn(g(a)) {{ return h(a) }}\nprint(g({v}))',
    'ret2':      'fn(g(a)) {{ return h(a) }}\nfn(k(a)) {{ return g(a) }}\nprint(k({v}))',
    'decl_ret':  'fn(g(a)) {{ q := h(a)\n    return q }}\nprint(g({v}))',
    'loop_ret':  'fn(g(a)) {{ for(i : [1]) {{ return h(a) }}\n    return 0 }}\nprint(g({v}))',
    'try':       'fn(g(a)) {{ try {{ return h(a) }} catch(e) {{ return "caught" }} }}\nprint(g({v}))',
    'top_if':    None,  # top-level statement forms below
}
TOPS = {
    'top_if':   'a := {v}\nprint("before")\nif({c}) {{ print("t") }} else {{ print("f") }}',
    'top_decl': 'a := {v}\nprint("before")\ny := {c}\nprint(y)',
    'top_for':  'a := {v}\nfor(i : [1]) {{ if({c}) {{ print("t") }} else {{ print("f") }} }}',
}


def run(env_vm, path, cwd):
    env = dict(os.environ, NIFT_VM=env_vm)
    p = subprocess.run([NIFT, os.path.basename(path)], cwd=cwd, env=env, capture_output=True, text=True, timeout=30)
    return p.returncode, p.stdout, p.stderr


def main():
    work = tempfile.mkdtemp(prefix='og-diff-')
    stats = {'same': 0, 'refused': 0, 'mismatch': 0}
    mismatches = []
    programs = []
    for (cname, ctx), op, v, r in itertools.product(CONTEXTS.items(), OPS, VALUES, RIGHT):
        c = f'a {op} {r}'
        for kname, caller in CALLERS.items():
            if caller is None:
                continue
            src = 'fn(h(a)) {\n    ' + ctx.format(c=c) + '\n}\nprint("start")\n' + caller.format(v=v) + '\nprint("end")\n'
            programs.append((f'{cname}/{kname}/{op}/{v}/{r}', src))
    for (tname, tpl), op, v, r in itertools.product(TOPS.items(), OPS, VALUES, RIGHT):
        programs.append((f'{tname}/{op}/{v}/{r}', tpl.format(v=v, c=f'a {op} {r}') + '\nprint("end")\n'))
    for i, (name, src) in enumerate(programs):
        path = os.path.join(work, f'p{i}.f')
        with open(path, 'w') as f:
            f.write(src)
        lg = run('0', path, work)
        vm = run('1', path, work)
        if vm[0] != 0 and 'not compiled for the VM' in vm[2] and 'no part of it ran' in vm[2]:
            stats['refused'] += 1
            continue
        # D-018: the VM keeps canonical wording and does not add the caller
        # return: prefixes legacy's prepared path may show. Compare the same
        # text after those prefixes are removed.
        def d018(r):
            return (r[0], r[1], re.sub(r'(error: [^\n]*?: )(return: )+', r'\1', r[2]))
        if d018(lg) == d018(vm):
            stats['same'] += 1
        else:
            stats['mismatch'] += 1
            mismatches.append((name, src, lg, vm))
    print(f"programs={len(programs)} same={stats['same']} refused={stats['refused']} mismatch={stats['mismatch']}")
    for name, src, lg, vm in mismatches[:15]:
        print('---', name)
        print('    legacy:', lg[0], repr(lg[1][-80:]), repr(lg[2][:160]))
        print('    vm    :', vm[0], repr(vm[1][-80:]), repr(vm[2][:160]))
    if OUT:
        with open(OUT, 'w') as f:
            for name, src, lg, vm in mismatches:
                f.write(f'--- {name}\n{src}\nLEGACY {lg}\nVM {vm}\n')
    return 1 if stats['mismatch'] else 0


if __name__ == '__main__':
    sys.exit(main())
