#!/usr/bin/env python3
"""Tooling checkpoint (components 3 and 2; review F4, F6; re-review N3, N4, N6):
the checks compile-object.sh --production runs on a production object after it
compiles.

    check-production-tu.py OUT DEPFILE COMPILER LANG

DEPFILE is the compiler's -MD dependency list for OUT (every header the
compiler read, system headers included). Each dependency is parsed with make's
escapes (a space is `\\ `, `#` is `\\#`, `$` is `$$`) and resolved, symlinks
included, to its real path:

  * a real path inside the repository's top-level tests/ fails (a test helper
    reached from production);
  * a real path outside the repository must lie in one of the compiler's own
    system include directories (`COMPILER -x LANG -E -v -`, without this
    command's flags); anything else fails (a header outside the repository,
    through an absolute include, an -isystem/-I path or a symlink, is not
    reviewed and not scanned);
  * every real path inside the repository is scanned for preprocessor
    directives as the compiler reads them: a leading UTF-8 byte order mark
    dropped, every line splice removed (a backslash, optional horizontal
    whitespace, then LF, CR LF or a lone CR), CR LF and lone CR read as line
    ends, `%:` read as `#`, comments allowed between the directive's tokens.
    It fails on an #undef of NIFT_PRODUCTION_TU (the production marker,
    review F4) and on a #define or #undef of a test-only switch (re-review
    N3: the mutant, test, probe, hook and bypass macros, which test builds set
    on their own command lines, never in a production object), except the
    reviewed default definitions in DEFAULTS.

The text check is conservative: such a directive inside a string literal or a
disabled #if block fails too.
"""
import hashlib
import os
import re
import subprocess
import sys

TEST_ONLY = re.compile(r'NIFT_\w*(MUTANT|TEST|PROBE|HOOK|BYPASS)\w*|NIFT_RT_BUILDER_(RELEASE|TRANSFER)$')
# (repository path, macro): the reviewed in-source definitions of test-only
# switches. Each is an "off" default (`#ifndef X` / `#define X 0`) or, in
# rt/build.h, the definition of the builder-mutant hooks themselves.
DEFAULTS = {
    ('src/rt/compiler_coro_loops.cpp', 'NIFT_CORO_MUTANT'),
    ('src/rt/compiler_coro_admit.cpp', 'NIFT_CORO_MODULE_MUTANT'),
    ('src/rt/compiler_coro_admit.cpp', 'NIFT_CORO_MUTANT'),
    ('src/rt/compiler_coro_module_effects.h', 'NIFT_CORO_MODULE_MUTANT'),
    ('src/rt/vm_coro.h', 'NIFT_CORO_MUTANT'),
    ('src/rt/compiler_coro.cpp', 'NIFT_CORO_MUTANT'),
    ('src/rt/build.h', 'NIFT_RT_BUILDER_RELEASE'),
    ('src/rt/build.h', 'NIFT_RT_BUILDER_TRANSFER'),
}
BLANK = r'(?:[ \t\f\v]|/\*.*?\*/)'
DIRECTIVE = re.compile(r'^' + BLANK + r'*#' + BLANK + r'*(define|undef)' + BLANK + r'+([A-Za-z_]\w*)',
                       re.M | re.S)
SPLICE = re.compile(rb'\\[ \t\f\v]*(?:\r\n|\r|\n)')


def deps_of(depfile):
    text = open(depfile, encoding='utf-8', errors='surrogateescape').read()
    text = text.replace('\\\n', ' ')
    names, cur, i, seen_colon = [], [], 0, False
    while i < len(text):
        c = text[i]
        if c == '\\' and i + 1 < len(text) and text[i + 1] in ' #\\':
            cur.append(text[i + 1]); i += 2; continue
        if c == '$' and text.startswith('$$', i):
            cur.append('$'); i += 2; continue
        if c in ' \t\n':
            if cur:
                names.append(''.join(cur)); cur = []
            i += 1; continue
        cur.append(c); i += 1
    if cur:
        names.append(''.join(cur))
    out = []
    for n in names:  # the first rule's prerequisites (-MP phony rules repeat them)
        if n.endswith(':'):
            if seen_colon:
                break
            seen_colon = True
            continue
        if seen_colon:
            out.append(n)
    return out


def system_dirs(compiler, lang, cache_dir):
    real = os.path.realpath(subprocess.run(['sh', '-c', 'command -v "$0"', compiler], capture_output=True,
                                          text=True).stdout.strip() or compiler)
    stamp = os.stat(real).st_mtime_ns if os.path.exists(real) else 0
    key = hashlib.sha256(f'{real}\0{stamp}\0{lang}'.encode()).hexdigest()[:16]
    cache = os.path.join(cache_dir, f'sysdirs-{key}.txt')
    try:
        return [l.rstrip('\n') for l in open(cache)]
    except OSError:
        pass
    out = subprocess.run([compiler, '-x', lang, '-E', '-v', '-'], input='', capture_output=True,
                         text=True).stderr
    dirs, on = [], False
    for line in out.splitlines():
        if line.startswith('#include <...> search starts here'):
            on = True
        elif line.startswith('End of search list'):
            break
        elif on and line.startswith(' '):
            dirs.append(os.path.realpath(line.strip().split(' (')[0]))
    if not dirs:
        raise SystemExit(f'error: {compiler} reports no system include directories')
    os.makedirs(cache_dir, exist_ok=True)
    tmp = f'{cache}.{os.getpid()}'
    with open(tmp, 'w') as handle:
        handle.write(''.join(d + '\n' for d in dirs))
    os.replace(tmp, cache)
    return dirs


def normalised(data):
    if data.startswith(b'\xef\xbb\xbf'):
        data = data[3:]
    data = SPLICE.sub(b'', data)
    data = data.replace(b'\r\n', b'\n').replace(b'\r', b'\n').replace(b'%:', b'#')
    return data.decode('utf-8', errors='replace')


def main():
    out, depfile, compiler, lang = sys.argv[1:5]
    root = os.path.realpath('.')
    sysdirs = system_dirs(compiler, lang, os.path.join(root, '.build', 'obj-config'))
    problems = []
    for dep in deps_of(depfile):
        real = os.path.realpath(dep)
        rel = os.path.relpath(real, root)
        if rel.startswith('..' + os.sep) or rel == '..':
            if not any(real == d or real.startswith(d + os.sep) for d in sysdirs):
                problems.append(f'depends on {dep} ({real}), outside the repository and the compiler\'s system '
                                'include directories')
            continue
        if rel.split(os.sep)[0] == 'tests':
            problems.append(f'depends on the test tree: {rel} (test helpers are unreachable from production)')
            continue
        try:
            text = normalised(open(real, 'rb').read())
        except OSError as error:
            problems.append(f'cannot read its dependency {rel}: {error}')
            continue
        for kind, name in DIRECTIVE.findall(text):
            if name == 'NIFT_PRODUCTION_TU' and kind == 'undef':
                problems.append(f'undefines the production marker NIFT_PRODUCTION_TU in: {rel}')
            elif TEST_ONLY.match(name) and (kind == 'undef' or (rel, name) not in DEFAULTS):
                problems.append(f'{kind}s the test-only switch {name} in: {rel} (test builds set it on their '
                                'command line)')
    for p in problems[:3]:
        print(f'error: production object {out} {p} (tooling.md 3)', file=sys.stderr)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
