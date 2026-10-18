#!/usr/bin/env python3
"""Extract every Nift script unit from pinned project checkouts into a fixture
tree: standalone .f/.nft files (entry or imported) and @script bodies from
templates. Writes OUT/<repo>/<path> (scripts) and
OUT/<repo>/<path>.script<N>.f (template-embedded), plus OUT/units.tsv."""
import hashlib, os, re, subprocess, sys
SRC, OUT = sys.argv[1], sys.argv[2]
SKIP = {'node_modules', '.git', '.nift', 'public', 'dist'}
def balanced(text, i):
    """text[i] == '{'; return index of the matching '}' (quote-aware) or -1."""
    depth, q = 0, None
    j = i
    while j < len(text):
        c = text[j]
        if q:
            if c == '\\': j += 1
            elif c == q: q = None
        elif c in '"\'': q = c
        elif c == '{': depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0: return j
        j += 1
    return -1
TEMPLATE_EXT = ('.html', '.htm', '.md', '.content', '.xml', '.txt')
imports = re.compile(r'''\bimport\(\s*["']([^"']+)["']''')
rows = []
for repo in sorted(os.listdir(SRC)):
    root = os.path.join(SRC, repo)
    if not os.path.isdir(root): continue
    if os.path.isdir(os.path.join(root, '.git')):
        commit = subprocess.run(['git', '-C', root, 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
        url = subprocess.run(['git', '-C', root, 'remote', 'get-url', 'origin'], capture_output=True, text=True).stdout.strip()
    else:  # a local snapshot without git (pinned by the per-file sha256 only)
        commit, url = 'local-snapshot', '(local snapshot) ' + os.path.realpath(root)
    units, imported = [], set()
    for d, dirs, names in os.walk(root):
        dirs[:] = [x for x in dirs if x not in SKIP]
        for n in names:
            p = os.path.join(d, n); rel = os.path.relpath(p, root)
            if not n.endswith(('.f', '.nft') + TEMPLATE_EXT): continue
            try: text = open(p, encoding='utf-8').read()
            except Exception: continue
            for m in imports.finditer(text): imported.add(os.path.normpath(os.path.join(os.path.dirname(rel), m.group(1))))
            if n.endswith(('.f', '.nft')):
                units.append((rel, rel, 'script', text))
            elif n.endswith(TEMPLATE_EXT) and '@script' in text and re.search(r'(^|/)(templates|content)/', rel):
                k = 0
                for m in re.finditer(r'@script\s*\{', text):
                    e = balanced(text, m.end() - 1)
                    if e < 0: continue
                    k += 1
                    body = text[m.end():e]
                    line = text.count('\n', 0, m.end()) + 1
                    units.append((f'{rel}.script{k}.f', f'{rel}:{line}', 'template-script', body))
    for out_rel, origin, kind, text in units:
        if kind == 'script':
            # Package modules (nift-packages/*/src/*.f) are loaded by consumers'
            # import(...): dynamically loaded units, validated when loaded.
            pkg_module = repo.startswith('nift-packages_') and out_rel.startswith('src/')
            kind = 'import' if os.path.normpath(out_rel) in imported or pkg_module else 'entry'
        dst = os.path.join(OUT, repo, out_rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        open(dst, 'w', encoding='utf-8').write(text)
        rows.append((repo, url, commit, origin, f'{repo}/{out_rel}', kind, hashlib.sha256(text.encode()).hexdigest()))
with open(os.path.join(OUT, 'units.tsv'), 'w') as f:
    f.write('# repo\turl\tcommit\torigin\tfixture\tkind\tsha256\n')
    for r in rows: f.write('\t'.join(r) + '\n')
print(len(rows), 'units')
