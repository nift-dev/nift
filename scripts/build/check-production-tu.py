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
    review F4) and on a #define or #undef of a test-only switch, except the
    reviewed default definitions in DEFAULTS, matched by their exact directive
    text, value included (check C3).

The test-only switches (re-review N3, check C2) are the NIFT_* names matching
TEST_ONLY (mutant, test, probe, hook and bypass macros, the builder-mutant
hooks), the source-only measurement switches in SOURCE_ONLY, and every NIFT_*
name any Makefile rule passes with -D (read from the Makefile at each run, so
a new test or configuration define is covered without an edit here). On a
production command line (check-production-tu.py --args) they are refused too,
in every spelling the drivers accept (-D, -U, -D NAME, -Wp,-D..., -Xpreprocessor,
--define-macro, --undefine-macro), except the defines its own rt-obj
configuration passes: CONFIG_DEFINES, read from the Makefile's RT_OBJ_CONFIG
lines (an object under .build/rt-obj/<config>/ may carry exactly its
configuration's -D names, on the command line only, never in source).

Supported production configuration (check C7): the system include directories
are those the compiler path reports with no other flags; a compiler wrapper
(ccache) or a flag that moves them (--sysroot, -stdlib, -nostdinc, an
-isystem outside the repository) is not supported for production compiles: its
headers are refused as outside the repository.

The text check is conservative: such a directive inside a string literal or a
disabled #if block fails too.
"""
import hashlib
import os
import re
import subprocess
import sys

TEST_ONLY = re.compile(r'NIFT_\w*(MUTANT|TEST|PROBE|HOOK|BYPASS)\w*|NIFT_RT_BUILDER_(RELEASE|TRANSFER)$')
SOURCE_ONLY = {'NIFT_RT_BUILDER_UNCHECKED'}  # measurement only: drops the spent-builder check
# (repository path, the directive's normalised text): the reviewed in-source
# definitions of test-only switches: "off" defaults under #ifndef (value
# included), the production values of two test-sized constants, and, in
# rt/build.h, the builder-mutant hooks themselves.
DEFAULTS = {
    ('src/rt/compiler_coro_loops.cpp', '#define NIFT_CORO_MUTANT 0'),
    ('src/rt/compiler_coro_admit.cpp', '#define NIFT_CORO_MODULE_MUTANT 0'),
    ('src/rt/compiler_coro_admit.cpp', '#define NIFT_CORO_MUTANT 0'),
    ('src/rt/compiler_coro_module_effects.h', '#define NIFT_CORO_MODULE_MUTANT 0'),
    ('src/rt/vm_coro.h', '#define NIFT_CORO_MUTANT 0'),
    ('src/rt/compiler_coro.cpp', '#define NIFT_CORO_MUTANT 0'),
    ('src/rt/build.h', '#define NIFT_RT_BUILDER_RELEASE(v) (v)'),
    ('src/rt/build.h', '#define NIFT_RT_BUILDER_RELEASE(v) static_cast<Value&&>(v)'),
    ('src/rt/build.h', '#define NIFT_RT_BUILDER_TRANSFER(v) (v)'),
    ('src/rt/build.h', '#define NIFT_RT_BUILDER_TRANSFER(v) static_cast<Value&&>(v)'),
    ('src/rt/heap_account.cpp', '#define NIFT_RT_SATURATED_SLOTS 256'),
    ('src/rt/value.cpp', '#define NIFT_RT_DESTROY_LOCAL 32'),
}
BLANK = r'(?:[ \t\f\v]|/\*.*?\*/)'
DIRECTIVE = re.compile(r'^' + BLANK + r'*#' + BLANK + r'*(define|undef)' + BLANK + r'+([A-Za-z_]\w*)([^\n]*)',
                       re.M | re.S)
MAKE_DEFINE = re.compile(r'-D\s*(NIFT_[A-Za-z0-9_]+)')
RT_OBJ_CONFIG = re.compile(r'^\$\(eval \$\(call RT_OBJ_CONFIG,([\w-]+),(.*)\)\)\s*$', re.M)


def makefile_switches(root):
    """(every NIFT_* name a Makefile rule passes with -D, {rt-obj config: its -D names})."""
    text = open(os.path.join(root, 'Makefile')).read()
    configs = {name: set(MAKE_DEFINE.findall(flags)) for name, flags in RT_OBJ_CONFIG.findall(text)}
    return set(MAKE_DEFINE.findall(text)), configs


def test_only(name, makefile_names):
    return bool(TEST_ONLY.match(name)) or name in SOURCE_ONLY or name in makefile_names


def directive_text(kind, name, rest):
    rest = re.sub(r'/\*.*?\*/', ' ', rest, flags=re.S).split('//')[0]
    return ' '.join(f'#{kind} {name}{rest}'.split())
SPLICE = re.compile(rb'\\[ \t\f\v]*(?:\r\n|\r|\n)')


def deps_of(depfile, out):
    """The prerequisites of OUT's rule: the first logical line of the -MD file
    (continuations joined), its `OUT:` target dropped, split with make's escapes
    (a space is `\\ `, `#` is `\\#`, `$` is `$$`). Check C1: a dependency whose
    own name ends in `:` is a dependency, not the start of the -MP rules."""
    text = open(depfile, encoding='utf-8', errors='surrogateescape').read().replace('\\\n', ' ')
    line = text.split('\n', 1)[0]
    if not line.startswith(out + ':'):
        raise SystemExit(f'error: {depfile}: the dependency list does not start with {out}:')
    line = line[len(out) + 1:]
    names, cur, i = [], [], 0
    while i < len(line):
        c = line[i]
        if c == '\\' and i + 1 < len(line) and line[i + 1] in ' #\\':
            cur.append(line[i + 1]); i += 2; continue
        if c == '$' and line.startswith('$$', i):
            cur.append('$'); i += 2; continue
        if c in ' \t':
            if cur:
                names.append(''.join(cur)); cur = []
            i += 1; continue
        cur.append(c); i += 1
    if cur:
        names.append(''.join(cur))
    return names


def system_dirs(compiler, lang, cache_dir):
    # B18: `command -v` only resolves the path for the cache key; when it finds nothing the
    # compiler name itself is the key (the probe below then fails if the compiler is missing).
    found = subprocess.run(['sh', '-c', 'command -v "$0"', compiler], capture_output=True, text=True)
    real = os.path.realpath((found.stdout.strip() if found.returncode == 0 else '') or compiler)
    stamp = os.stat(real).st_mtime_ns if os.path.exists(real) else 0
    key = hashlib.sha256(f'{real}\0{stamp}\0{lang}'.encode()).hexdigest()[:16]
    cache = os.path.join(cache_dir, f'sysdirs-{key}.txt')
    try:
        return [l.rstrip('\n') for l in open(cache)]
    except OSError:
        pass
    probe = subprocess.run([compiler, '-x', lang, '-E', '-v', '-'], input='', capture_output=True, text=True)
    if probe.returncode != 0:  # B18: no search list would make every system header look local
        sys.exit(f'check-production-tu: {compiler} -x {lang} -E -v failed (exit {probe.returncode})')
    out = probe.stderr
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


def command_switches(args):
    """(kind, name) for every macro the arguments define (D) or undefine (U), in
    each spelling GCC and Clang accept (check C5)."""
    found, i = [], 0
    args = list(args)
    while i < len(args):
        a = args[i]
        nxt = args[i + 1] if i + 1 < len(args) else ''
        if a in ('-D', '-U'):
            found.append((a[1], nxt)); i += 2; continue
        if a.startswith(('-D', '-U')):
            found.append((a[1], a[2:])); i += 1; continue
        if a.startswith('-Wp,'):
            found += command_switches(a[4:].split(',')); i += 1; continue
        if a == '-Xpreprocessor':
            rest = [nxt]
            if nxt in ('-D', '-U') and i + 3 < len(args) + 1 and args[i + 2:i + 3] == ['-Xpreprocessor']:
                rest.append(args[i + 3] if i + 3 < len(args) else ''); i += 2
            found += command_switches(rest); i += 2; continue
        for long_form, kind in (('--define-macro', 'D'), ('--undefine-macro', 'U')):
            if a == long_form:
                found.append((kind, nxt)); i += 1; break
            if a.startswith(long_form + '='):
                found.append((kind, a[len(long_form) + 1:])); break
        i += 1
    return [(k, n.split('=', 1)[0]) for k, n in found]


def config_of(out):
    m = re.match(r'(?:.*/)?\.build/rt-obj/([\w-]+)/', out)
    return m.group(1) if m else None


def check_args(out, args, root):
    names, configs = makefile_switches(root)
    allowed = configs.get(config_of(out), set())
    problems = []
    for kind, name in command_switches(args):
        if name == 'NIFT_PRODUCTION_TU' and kind == 'U':
            problems.append('the command line undefines NIFT_PRODUCTION_TU')
        elif test_only(name, names) and not (kind == 'D' and name in allowed):
            problems.append(f'the command line sets the test-only switch {name}'
                            + (f' (its configuration {config_of(out)} may pass only '
                               f'{", ".join(sorted(allowed)) or "none"})' if config_of(out) else ''))
    for p in problems[:3]:
        print(f'error: production object {out}: {p} (tooling.md 3)', file=sys.stderr)
    return 1 if problems else 0


def allowed_default(rel, directive, separator=os.sep):
    # rel is already relative to the repository after resolving the dependency.
    # On POSIX a literal backslash can be part of a different filename.
    return (rel.replace(separator, '/'), directive) in DEFAULTS


def self_test(root):
    """The refused set and the allowances follow the Makefile and the sources."""
    fail = []
    names, configs = makefile_switches(root)
    for known in ('NIFT_RT_NO_CYCLE_CHECK', 'NIFT_RT_DESTROY_LOCAL', 'NIFT_SOURCEMAP_COUNTER',
                  'NIFT_RT_SATURATED_SLOTS', 'NIFT_REFSEM_MUTANT', 'NIFT_RT_VM_SWITCH', 'NIFT_RT_CHECKED_REFS'):
        if known not in names:
            fail.append(f'{known} not read from the Makefile')
    if len(configs) < 9:
        fail.append(f'only {len(configs)} RT_OBJ_CONFIG lines read')
    for config, defines in configs.items():
        if not defines <= names:
            fail.append(f'{config}: {defines - names} not in the Makefile set')
    for rel, line in sorted(DEFAULTS):
        text = normalised(open(os.path.join(root, rel), 'rb').read())
        if not any(directive_text(k, n, r) == line for k, n, r in DIRECTIVE.findall(text)):
            fail.append(f'DEFAULTS entry missing from {rel}: {line}')
    builder = '#define NIFT_RT_BUILDER_RELEASE(v) (v)'
    for rel, separator, directive, expected in (
        ('src/rt/build.h', '/', builder, True),
        (r'src\rt\build.h', '\\', builder, True),
        ('src/rt/build.h', '\\', builder, True),
        (r'src\rt\build.h', '/', builder, False),
        (r'other\rt\build.h', '\\', builder, False),
        (r'src\rt\build.h.bak', '\\', builder, False),
        (r'src\rt\build.h', '\\', '#undef NIFT_RT_BUILDER_RELEASE', False),
    ):
        if allowed_default(rel, directive, separator) != expected:
            fail.append(f'allowance for {rel!r} with separator {separator!r}: {directive}')
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        d = os.path.join(tmp, 'x.d')
        with open(d, 'w') as handle:
            handle.write('o.o: a.cpp m: b\\ c.h \\\n  d$$.h e\\#f.h\n\nm:\n\nb\\ c.h:\n')
        if deps_of(d, 'o.o') != ['a.cpp', 'm:', 'b c.h', 'd$.h', 'e#f.h']:
            fail.append(f'deps_of: {deps_of(d, "o.o")}')
    cases = {('-DNIFT_REFSEM_MUTANT=26',): [('D', 'NIFT_REFSEM_MUTANT')],
             ('-D', 'NIFT_X'): [('D', 'NIFT_X')],
             ('-Wp,-DNIFT_A,-UNIFT_B',): [('D', 'NIFT_A'), ('U', 'NIFT_B')],
             ('-Xpreprocessor', '-DNIFT_C'): [('D', 'NIFT_C')],
             ('--define-macro=NIFT_E=1',): [('D', 'NIFT_E')],
             ('--define-macro', 'NIFT_F'): [('D', 'NIFT_F')],
             ('--undefine-macro', 'NIFT_PRODUCTION_TU'): [('U', 'NIFT_PRODUCTION_TU')]}
    for args, want in cases.items():
        if command_switches(args) != want:
            fail.append(f'command_switches{args} = {command_switches(args)}')
    for f in fail:
        print(f'FAIL production-tu self-test: {f}')
    table = '; '.join(f'{c}: {", ".join(sorted(d)) or "none"}' for c, d in sorted(configs.items()))
    print(('FAIL' if fail else 'PASS') + f' production-tu self-test: {len(names)} Makefile -D switches refused in '
          f'production sources; command-line allowances by rt-obj configuration: {table}')
    return 1 if fail else 0


def main():
    if sys.argv[1:2] == ['--self-test']:
        return self_test(os.path.realpath('.'))
    if sys.argv[1:2] == ['--args']:
        return check_args(sys.argv[2], sys.argv[3:], os.path.realpath('.'))
    out, depfile, compiler, lang = sys.argv[1:5]
    root = os.path.realpath('.')
    sysdirs = system_dirs(compiler, lang, os.path.join(root, '.build', 'obj-config'))
    problems = []
    makefile_names, _ = makefile_switches(root)
    for dep in deps_of(depfile, out):
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
        for kind, name, rest in DIRECTIVE.findall(text):
            if name == 'NIFT_PRODUCTION_TU' and kind == 'undef':
                problems.append(f'undefines the production marker NIFT_PRODUCTION_TU in: {rel}')
            elif test_only(name, makefile_names) and not allowed_default(rel, directive_text(kind, name, rest)):
                problems.append(f'{kind}s the test-only switch {name} in: {rel} (test builds set it on their '
                                'command line)')
    for p in problems[:3]:
        print(f'error: production object {out} {p} (tooling.md 3)', file=sys.stderr)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
