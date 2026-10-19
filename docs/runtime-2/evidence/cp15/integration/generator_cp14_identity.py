# Usage (from a scratch directory):
#   git show 4b2fc909:tests/runtime2/vm_subset_gen.py > gen_cp14.py
#   cp <tree>/tests/runtime2/vm_subset_gen.py gen_cp15.py; python3 generator_cp14_identity.py
"""Every program the CP15 supported/former/unsupported generators produce equals
the CP14 (4b2fc909) generator's program once the added `"v"` members are removed."""
import importlib.util, sys, collections
def load(path, name):
    sp = importlib.util.spec_from_file_location(name, path); m = importlib.util.module_from_spec(sp); sp.loader.exec_module(m); return m
old, new = load('gen_cp14.py', 'g14'), load('gen_cp15.py', 'g15')
def strip(text):
    out, i, n = [], 0, 0
    key = ', "v": '
    while True:
        j = text.find(key, i)
        if j < 0: out.append(text[i:]); return ''.join(out), n
        out.append(text[i:j]); k = j + len(key); depth = 0; q = None
        while k < len(text):
            c = text[k]
            if q:
                if c == '\\': k += 1
                elif c == q: q = None
            elif c in '"\'': q = c
            elif c in '[{': depth += 1
            elif c in ']}':
                if depth == 0: break
                depth -= 1
            k += 1
        i = k; n += 1
def as_text(x):
    # The generators return (text, ..., features); the features counter gains
    # only the new `json-only-value:*` keys, which are dropped here.
    def norm(v):
        if isinstance(v, dict):
            return {k: c for k, c in v.items() if not str(k).startswith('json-only-value')}
        if isinstance(v, (tuple, list)):
            return type(v)(norm(e) for e in v)
        return v
    return repr(norm(x))
total = diff = added = 0
shapes = collections.Counter()
for seed in (1, 2, 3):
    for fam in ('supported_program', 'former_program', 'unsupported_program'):
        for idx in range(500):
            a = as_text(getattr(old, fam)(seed, idx)); b = as_text(getattr(new, fam)(seed, idx))
            s, n = strip(b); total += 1; added += n
            if n: shapes[fam] += 1
            if s != a:
                diff += 1
                if diff <= 3: print('DIFF', fam, seed, idx); print(' cp14', a[:300]); print(' cp15', s[:300])
print(f'{total} programs compared (supported/former/unsupported x seeds 1-3 x 500); {diff} differ after removing added members; '
      f'{added} "v" members added in {sum(shapes.values())} programs {dict(shapes)}')
sys.exit(1 if diff else 0)
