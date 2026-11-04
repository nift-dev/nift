#!/usr/bin/env python3
"""Runtime 2.0 source lint for src/rt (CP09).

Rules
  function-length  no function body longer than 150 lines
  line-length      no line longer than 120 columns
  string-compare   hot-path heuristic: no comparison against a string literal
                   (`x == "lit"`, `"lit" != x`) and no strcmp/strncmp. Strings
                   must be resolved to atoms or enums before the hot path.
                   Allowed per line with `// rt-lint: allow-string-compare <why>`
                   or per file with `// rt-lint: cold-file <why>`.
  one-concern      every header states its single concern in a comment directly
                   after `#pragma once`; every .cpp includes its own header
                   first (one header/implementation pair per concern); no file
                   exceeds 400 lines.
  storage-write    B1 mutation protocol (b1-mutation.md): no direct write to
                   an aggregate's `items`/`entries`/`fields` storage, no raw
                   storage hand-out (array_mut, *_slot_mut) and no call of a
                   receiver-mutating commit primitive outside rt/mutation.cpp (and its
                   collection half, mutation_collection.cpp),
                   the commit primitives (aggregate*.cpp), the construction
                   builders (natives/support) and the decoders. A line that
                   builds or tears down a fresh value it alone holds may say
                   `// rt-lint: storage-ok <why>`. B2 (owner B): the commits
                   are private members of rt::Storage whose only production
                   friend is mutation::Protocol, so a bypass does not compile
                   (tests/rt/compile_fail); this rule is defence in depth and
                   also flags any `Storage::` or `StorageTestAccess` use.

  boundary         (tooling checkpoint, component 5) the storage-write,
                   construction, sort, paste and test-access rules over every
                   production root (src, include, bindings, markuppp, minifypp;
                   nested tests/ directories included, only the top-level
                   tests/ skipped), every C/C++ suffix and every file a scanned
                   file #includes, whatever its suffix; the source read as the
                   compiler reads it (lex below); annotations read only from
                   comments; every annotation pinned by path, label and count
                   in tools/rt-lint-annotations.txt.

Usage: rt-lint.py [paths...]   (default: src/rt)
       rt-lint.py --boundary [--root DIR] [--manifest FILE] [roots...]
       rt-lint.py --test-access [roots...]   (the boundary scan, test-access rule only)
       rt-lint.py --self-test  (each rule must fire on tests/rt/lint_fixtures)
"""
import os
import re
import sys

MAX_FUNCTION = 150
MAX_LINE = 120
MAX_FILE = 400
CONTROL = re.compile(r'\b(if|for|while|switch|catch|do|else|return|namespace|class|struct|union|enum)\b|=\s*$|\[[^\]]*\]\s*\(')
STORAGE_WRITE = re.compile(
    r'heap_as<\s*(ArrayObject|ObjectObject|StructObject|CollectionObject)\s*>\s*\(\s*\)\s*->\s*'
    r'(items|entries|fields)\b\s*(\[[^\]]*\])?\s*(=(?!=)|\.\s*(push_back|emplace_back|insert|erase|clear|resize|'
    r'pop_back|swap|assign|emplace)\s*\()'
    r'|(?<!mutation::)(?<!Protocol::)(?<!Construction::)\b(array_mut|array_slot_mut|object_slot_mut|struct_slot_mut|array_set|object_remove|'
    r'struct_set|map_set|set_add|collection_erase|stack_push|stack_pop|collection_clear)\s*\('
    r'|\bStorage::|\bStorageTestAccess\b')
# B2 review M1: the construction API (fresh values only; checked builds abort
# otherwise) and the protocol's private sort commits. Each use outside the
# files that implement them needs `// rt-lint: storage-ok <label>, <why>` on the
# line (or alone on the line directly above it) (the value is one the code alone holds and has just made), and the sort
# accessor (SortController, rt/sort_controller.h) is allowed only in the two
# sort files. Labels (re-review C2): `fresh` (a value the code alone holds and
# has just made), `private-descriptor` (a cell's own selector descriptor,
# refcount 1, never user-visible), `mutant-only` (inside a test-mutant #if).
# Re-review C2: identifier tokens, not call syntax, so a qualified
# (`rt::array_push`), parenthesised (`(array_push)(...)`), address-taken
# (`&array_build_set`) or `using`-imported reference counts too. Only the
# storage commits' own names (`Storage::`, `StorageTestAccess::`) are left to
# the storage-write rule above.
CONSTRUCTION = re.compile(
    r'(?<!Storage::)(?<!StorageTestAccess::)\b(array_push|array_build_storage|array_build_set|object_set|'
    r'object_build_slot|struct_add_field|map_build_set|set_build_add|stack_build_push)\b')
SORT_ACCESS = re.compile(r'\b(SortController|SortKey|sort_storage|sort_items|commit_permutation)\b')
SORT_INCLUDE = re.compile(r'#\s*include\s*[<"][^">]*sort_controller\.h')
ANNOTATION = re.compile(r'rt-lint: storage-ok (fresh|private-descriptor|mutant-only)')
ANNOTATION_LINE = re.compile(r'\s*// rt-lint: storage-ok (fresh|private-descriptor|mutant-only)')
# Token pasting can build any of the names above: each use needs `rt-lint: paste-ok <why>`.
TOKEN_PASTE = re.compile(r'##')
# Tooling checkpoint (component 5): the allowlists name repository paths, not
# basenames (a file of the same name in another directory is not the allowed one).
SORT_ALLOWED = {'src/rt/collection_sort.cpp', 'src/rt/collection_plain_sort.cpp', 'src/rt/sort_controller.h',
                'src/rt/mutation.cpp', 'src/rt/mutation.h'}
STORAGE_ALLOWED = {'src/rt/mutation.cpp', 'src/rt/mutation.h', 'src/rt/mutation_collection.cpp',
                   'src/rt/aggregate.cpp', 'src/rt/aggregate.h', 'src/rt/aggregate_collection.cpp',
                   'src/rt/aggregate_collection.h', 'src/rt/aggregate_store.h',
                   'src/rt/natives/support.cpp', 'src/rt/natives/support.h',
                   'src/rt/isolate_transfer_decode.cpp', 'src/rt/transfer_heap.cpp', 'src/rt/isolate_snapshot.cpp',
                   'src/rt/build.h'}  # tooling 2: the builders and the in-place key (rt/build.h)
# The only files where `rt-lint: test-access-decl` may stand (the friend and
# forward declarations, and the production tail that poisons the name).
TEST_ACCESS_ALLOWED = {'src/rt/aggregate.h', 'src/rt/build.h'}
TEST_ACCESS = re.compile(r'\bStorageTestAccess\b')
REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
LITERAL_COMPARE = re.compile(r'(==|!=)\s*"|"\s*(==|!=)|\bstrn?cmp\s*\(')


def strip(line, in_comment):
    """Remove comments, string and char literal contents; return (code, in_comment)."""
    out, i = [], 0
    while i < len(line):
        if in_comment:
            end = line.find('*/', i)
            if end < 0:
                return ''.join(out), True
            i, in_comment = end + 2, False
        elif line.startswith('//', i):
            break
        elif line.startswith('/*', i):
            in_comment, i = True, i + 2
        elif line[i] in '"\'':
            quote, j = line[i], i + 1
            while j < len(line) and line[j] != quote:
                j += 2 if line[j] == '\\' else 1
            out.append(quote + quote)
            i = j + 1
        else:
            out.append(line[i])
            i += 1
    return ''.join(out), in_comment


# Tooling checkpoint (component 5, b2-refrep.md 19.3 R3, RECHECK2): the
# storage-write, construction, sort, paste and test-access rules read the
# source as the compiler does, not line by line.
#   phase 2   every line splice (backslash-newline) is removed first, so a name
#             split by one is one identifier; the spliced logical line is
#             reported on the physical line it starts on;
#   phase 3   // and /* */ comments; string and character literals (contents
#             blanked); raw strings R"d(...)d" with any encoding prefix, over
#             any number of lines, so a quote or a comment opener inside one is
#             text; pp-numbers, so the digit separator of 1'0 is not a
#             character literal; the digraphs %: and %:%: (spelt # and ##);
#             the header name of an #include.
# Per physical line: the code (literal contents blanked, comments removed),
# the comment text (the only place an annotation is read), and the included
# header name.
RAW_PREFIXES = ('R', 'u8R', 'uR', 'UR', 'LR')
LEX_PLAIN = re.compile(r'[^/"\'<%0-9.A-Za-z_\n]+|[A-Za-z_][A-Za-z0-9_]*')
LEX_TAIL_IDENT = re.compile(r'([A-Za-z0-9_]+)$')
LEX_RAW_DELIM = re.compile(r'[^\s()\\"]{0,16}$')
LEX_PP_NUMBER = re.compile(r"\.?[0-9](?:[eEpP][+-]|'[A-Za-z0-9_]|[A-Za-z0-9_.])*")
INCLUDE_DIRECTIVE = re.compile(r'\s*#\s*include\s*$')


def splice(text):
    """Translation phase 2: (the text without line splices, the physical line of each character)."""
    out, where = [], []
    line = start = 1
    pos = 0
    for m in re.finditer(r'\\\r?\n|\n', text):
        seg = text[pos:m.start()]
        out.append(seg)
        where.extend([start] * len(seg))
        line += 1
        if m.group() == '\n':
            out.append('\n')
            where.append(start)
            start = line
        pos = m.end()
    seg = text[pos:]
    out.append(seg)
    where.extend([start] * len(seg))
    return ''.join(out), where, line


def lex(text):
    """Return (code, comment, include): per physical line (index 0 is line 1), the
    code with literal contents blanked, the comment text, and {line: header name}."""
    s, where, phys = splice(text)
    n = len(s)
    code = [[] for _ in range(phys + 1)]
    comment = [[] for _ in range(phys + 1)]
    include = {}

    def emit(piece, at):
        code[where[at]].append(piece)

    def line_code(at):
        return ''.join(code[where[at]])

    j = 0
    while j < n:
        m = LEX_PLAIN.match(s, j)
        if m:
            emit(m.group(), j)
            j = m.end()
            continue
        c = s[j]
        if c == '\n':
            j += 1
            continue
        if s.startswith('//', j):
            end = s.find('\n', j)
            end = n if end < 0 else end
            comment[where[j]].append(s[j:end])
            j = end
            continue
        if s.startswith('/*', j):
            end = s.find('*/', j + 2)
            end = n if end < 0 else end + 2
            emit(' ', j)
            seg = j
            while seg < end:  # each line of the comment on its own physical line
                nl = s.find('\n', seg, end)
                stop = end if nl < 0 else nl
                comment[where[seg]].append(s[seg:stop])
                seg = end if nl < 0 else nl + 1
            j = end
            continue
        if c == '"':
            tail = LEX_TAIL_IDENT.search(line_code(j))
            if tail and tail.group(1) in RAW_PREFIXES:
                paren = s.find('(', j + 1)
                if paren > 0 and LEX_RAW_DELIM.match(s, j + 1, paren):
                    delim = s[j + 1:paren]
                    end = s.find(')' + delim + '"', paren + 1)
                    end = n if end < 0 else end + len(delim) + 2
                    emit('""', j)
                    j = end
                    continue
            k = j + 1
            while k < n and s[k] != '"' and s[k] != '\n':
                k += 2 if s[k] == '\\' else 1
            if INCLUDE_DIRECTIVE.match(line_code(j)):
                include[where[j]] = s[j + 1:k]
            emit('""', j)
            j = k + 1
            continue
        if c == "'":
            k = j + 1
            while k < n and s[k] != "'" and s[k] != '\n':
                k += 2 if s[k] == '\\' else 1
            emit("''", j)
            j = k + 1
            continue
        if c.isdigit() or (c == '.' and j + 1 < n and s[j + 1].isdigit()):
            m = LEX_PP_NUMBER.match(s, j)
            emit(m.group().replace("'", ''), j)
            j = m.end()
            continue
        if s.startswith('%:%:', j):
            emit('##', j)
            j += 4
            continue
        if s.startswith('%:', j):
            emit('#', j)
            j += 2
            continue
        if c == '<' and INCLUDE_DIRECTIVE.match(line_code(j)):
            k = s.find('>', j)
            k = n if k < 0 else k
            include[where[j]] = s[j + 1:k]
            emit('<>', j)
            j = k + 1
            continue
        emit(c, j)
        j += 1
    return ([''.join(code[k]) for k in range(1, phys + 1)],
            [''.join(comment[k]) for k in range(1, phys + 1)], include)


ANNOTATION_ANY = re.compile(r'rt-lint: (storage-ok [a-z-]+|storage-ok|paste-ok|test-access-decl)')
CONSTRUCTION_LABELS = ('storage-ok fresh', 'storage-ok private-descriptor', 'storage-ok mutant-only')


def boundary_file(path, rel, text, problems, annotations, rules):
    """The boundary rules on one file (rel: its repository path, which the
    allowlists name). Adds every annotation found to annotations[(rel, label)]."""
    code, comment, include = lex(text)
    labels = [set(ANNOTATION_ANY.findall(c)) for c in comment]
    for number, found in enumerate(ANNOTATION_ANY.findall(c) for c in comment):
        for label in found:
            annotations[(rel, label)] = annotations.get((rel, label), 0) + 1
    storage_checked = rel not in STORAGE_ALLOWED
    for index, line in enumerate(code):
        number = index + 1
        here = labels[index]
        # An annotation stands on the line or alone (a comment-only line) directly above it.
        above = labels[index - 1] if index and not code[index - 1].strip() else set()
        storage_ok = any(l.startswith('storage-ok') for l in here)
        if 'storage' in rules and storage_checked and not storage_ok and STORAGE_WRITE.search(line):
            problems.append(f'{path}:{number}: storage-write: aggregate storage written outside the mutation protocol')
        uses = CONSTRUCTION.findall(line)
        annotated = any(l in CONSTRUCTION_LABELS for l in here | above)
        if 'storage' in rules and storage_checked and uses and not annotated:
            problems.append(f'{path}:{number}: storage-write: construction API without `rt-lint: storage-ok fresh`')
        elif 'storage' in rules and storage_checked and len(uses) > 1:  # one annotation covers one reference
            problems.append(f'{path}:{number}: storage-write: {len(uses)} construction API references on one line')
        header = include.get(number)
        sort_include = header is not None and os.path.basename(os.path.normpath(header)) == 'sort_controller.h'
        if 'storage' in rules and rel not in SORT_ALLOWED and (SORT_ACCESS.search(line) or sort_include):
            problems.append(f'{path}:{number}: storage-write: the protocol\'s sort commits outside the sort controller')
        if 'storage' in rules and TOKEN_PASTE.search(line) and 'paste-ok' not in here:
            problems.append(f'{path}:{number}: storage-write: token pasting without `rt-lint: paste-ok`')
        if 'test-access' in rules:
            declared = 'test-access-decl' in here and rel in TEST_ACCESS_ALLOWED
            if TEST_ACCESS.search(line) and not declared:
                problems.append(f'{path}:{number}: test-access: StorageTestAccess outside tests/ (it would '
                                'grant every storage commit)')
            if 'test-access-decl' in here and rel not in TEST_ACCESS_ALLOWED:
                problems.append(f'{path}:{number}: test-access: `rt-lint: test-access-decl` outside '
                                f'{" and ".join(sorted(TEST_ACCESS_ALLOWED))}')
    return include


# The production roots the boundary scan covers by default, the C/C++ suffixes
# it opens there, and the include search path it follows an #include on.
BOUNDARY_ROOTS = ('src', 'include', 'bindings', 'markuppp', 'minifypp')
INCLUDE_PATH = ('src', 'include', 'markuppp/include', 'markuppp/src', 'minifypp/include', 'minifypp/src')
SOURCE_SUFFIXES = ('.h', '.hh', '.hpp', '.hxx', '.h++', '.c', '.cc', '.cpp', '.cxx', '.c++', '.inc', '.ipp',
                   '.tpp', '.tcc', '.ixx', '.cppm', '.def', '.m', '.mm')
ANNOTATION_MANIFEST = 'tools/rt-lint-annotations.txt'


def repo_rel(path, root):
    return os.path.relpath(os.path.abspath(path), os.path.abspath(root)).replace(os.sep, '/')


def skipped(rel):
    """Only the top-level tests/ tree (and what lies outside the root) is not production source."""
    return rel == 'tests' or rel.startswith(('tests/', '../')) or rel.startswith('third_party/')


def resolve_include(header, including, root):
    for base in (os.path.dirname(including),) + tuple(os.path.join(root, d) for d in INCLUDE_PATH):
        candidate = os.path.normpath(os.path.join(base, header))
        if os.path.isfile(candidate):
            return candidate
    return None


def boundary_scan(roots, root='.', rules=('storage', 'test-access')):
    """Every C/C++ file under the roots (not the top-level tests/), then every file
    those #include, of any suffix; returns (problems, annotations, files)."""
    queue = []
    for start in roots:
        start = start if os.path.isabs(start) else os.path.join(root, start)
        if not os.path.exists(start):
            continue
        if os.path.isfile(start):
            queue.append(start)
            continue
        for d, dirs, fs in os.walk(start):
            dirs[:] = sorted(x for x in dirs if not skipped(repo_rel(os.path.join(d, x), root)))
            queue.extend(os.path.join(d, f) for f in sorted(fs) if f.endswith(SOURCE_SUFFIXES))
    problems, annotations, seen = [], {}, set()
    while queue:
        path = queue.pop(0)
        rel = repo_rel(path, root)
        if rel in seen or skipped(rel):
            continue
        seen.add(rel)
        with open(path, encoding='utf-8', errors='replace') as handle:
            text = handle.read()
        for header in boundary_file(path, rel, text, problems, annotations, rules).values():
            found = resolve_include(header, path, root)
            if found:
                queue.append(found)
    return problems, annotations, seen


def manifest_check(annotations, manifest):
    """Every annotation pinned by path, label and count: an annotation that is not
    listed, a listed one that is gone, and a changed count each fail."""
    want = {}
    with open(manifest) as handle:
        for line in handle:
            line = line.split('#', 1)[0].strip()
            if line:
                rel, rest = line.split(None, 1)
                label, count = rest.rsplit(None, 1)
                want[(rel, label)] = int(count)
    problems = []
    for key in sorted(set(want) | set(annotations)):
        have, need = annotations.get(key, 0), want.get(key, 0)
        if have != need:
            problems.append(f'{key[0]}: annotation-manifest: `rt-lint: {key[1]}` {have} time(s), '
                            f'{manifest} allows {need}')
    return problems


def function_spans(lines):
    """Yield (start_line, length) for function bodies (blocks whose header looks
    like a function signature and whose parent is not itself a function)."""
    stack, header, in_comment = [], '', False
    for number, raw in enumerate(lines, 1):
        code, in_comment = strip(raw, in_comment)
        for ch in code:
            if ch == '{':
                head = header.strip()
                inside_function = any(kind == 'function' for kind, _ in stack)
                is_function = (not inside_function and '(' in head
                               and re.search(r'\)\s*(const|noexcept|override|final|\s)*(->\s*[\w:<>]+)?$', head)
                               and not CONTROL.search(head.split('(')[0]))
                stack.append(('function' if is_function else 'block', number))
                header = ''
            elif ch == '}':
                if stack:
                    kind, start = stack.pop()
                    if kind == 'function':
                        yield start, number - start + 1
                header = ''
            elif ch == ';':
                header = ''
            else:
                header += ch
        header += ' '


def lint_file(path, problems):
    with open(path, encoding='utf-8') as handle:
        text = handle.read()
    lines = text.split('\n')
    if lines and lines[-1] == '':
        lines.pop()
    cold = 'rt-lint: cold-file' in text
    for number, line in enumerate(lines, 1):
        if len(line) > MAX_LINE:
            problems.append(f'{path}:{number}: line-length: {len(line)} > {MAX_LINE} columns')
    in_comment = False
    for number, line in enumerate(lines, 1):
        stripped, in_comment = strip(line, in_comment)
        quoted = re.sub(r'//.*', '', line) if not in_comment else ''
        if not cold and 'rt-lint: allow-string-compare' not in line and \
                LITERAL_COMPARE.search(quoted) and ('"' in stripped or 'cmp' in stripped):
            problems.append(f'{path}:{number}: string-compare: comparison against a string literal on a hot path')
    boundary_file(path, repo_rel(path, REPO), text, problems, {}, ('storage',))
    for start, length in function_spans(lines):
        if length > MAX_FUNCTION:
            problems.append(f'{path}:{start}: function-length: function body of {length} lines > {MAX_FUNCTION}')
    if len(lines) > MAX_FILE:
        problems.append(f'{path}:1: one-concern: {len(lines)} lines > {MAX_FILE}; split the file by concern')
    name = os.path.basename(path)
    if name.endswith('.h'):
        body = [l for l in lines if l.strip()]
        if len(body) < 2 or body[0].strip() != '#pragma once' or not body[1].startswith('//'):
            problems.append(f'{path}:1: one-concern: header must state its concern in a comment after #pragma once')
    elif name.endswith('.cpp'):
        includes = [l for l in lines if l.startswith('#include')]
        own = name[:-4] + '.h'
        header = os.path.join(os.path.dirname(path), own)
        if not os.path.exists(header) or not includes or not includes[0].rstrip().endswith(f'/{own}"'):
            problems.append(f'{path}:1: one-concern: must have {own} and include it first')


def lint(paths):
    problems = []
    for root in paths:
        files = [root] if os.path.isfile(root) else sorted(
            os.path.join(d, f) for d, _, fs in os.walk(root) for f in fs if f.endswith(('.h', '.cpp')))
        for path in files:
            lint_file(path, problems)
    return problems


def self_test():
    fixtures = os.path.join(os.path.dirname(__file__), '..', 'tests', 'rt', 'lint_fixtures')
    expected = {'bad_length.cpp': {'function-length', 'line-length'},
                'bad_compare.cpp': {'string-compare'},
                'bad_concern.h': {'one-concern'},
                'bad_concern.cpp': {'one-concern'},
                'bad_storage.cpp': {'storage-write'}, 'bad_storage.h': set(),
                'bad_construction.cpp': {'storage-write'}, 'bad_construction.h': set(),
                'bad_bypass_forms.cpp': {'storage-write'}, 'bad_bypass_forms.h': set(),
                'good.cpp': set(), 'good.h': set()}
    # B2 review M1: exactly the five bypass lines fire (not the annotated
    # construction of a fresh value, not the protocol call).
    lines = {'bad_construction.cpp': {6, 7, 8, 9, 10},
             # re-review C2: every call form of rr/lint/vm_bypass.cpp, sort_bypass.cpp
             # and the push_value patch
             'bad_bypass_forms.cpp': {2, 3, 9, 12, 13, 14, 15, 17, 18, 20, 22, 23, 24}}
    failed = False
    for name, rules in sorted(expected.items()):
        found = {p.split(': ')[1] for p in lint([os.path.join(fixtures, name)])}
        if found != rules:
            print(f'FAIL rt-lint self-test {name}: expected {sorted(rules)}, got {sorted(found)}')
            failed = True
        if name in lines:
            at = {int(p.split(':')[1]) for p in lint([os.path.join(fixtures, name)])}
            if at != lines[name]:
                print(f'FAIL rt-lint self-test {name}: expected lines {sorted(lines[name])}, got {sorted(at)}')
                failed = True
    # Re-check R2 control: the test-access scan flags a production definition
    # (the reviewer's rc/forge/f23) on exactly its two lines.
    access = {int(p.split(':')[1]) for p in test_access_scan([os.path.join(fixtures, 'bad_test_access.cpp')],
                                                               root=fixtures)}
    if access != {5, 9}:
        print(f'FAIL rt-lint self-test bad_test_access.cpp: expected lines [5, 9], got {sorted(access)}')
        failed = True
    # Tooling checkpoint (component 5): the boundary scan reads the source as the
    # compiler does. bad_lexical.cpp holds the re-checker's rc/lintb/rt/vm_bypass2.cpp
    # forms (digit separator, raw string, splice inside the name, %:%:, annotation
    # text in a string, the raw-string comment desync, the .inc it includes, the
    # statement split after one annotation), rc/m-digit's push_value line, a raw
    # string with a delimiter over three lines, an annotation inside a raw
    # string, a splice inside `Storage::`, and a line under a code line's annotation;
    # each must fire on exactly its line.
    lexical = {'bad_lexical.cpp': {2, 3, 7, 8, 10, 14, 17, 18, 19, 26, 29, 35, 36, 39, 40, 43},
               'bad_lexical_inc.inc': {2}}
    found = {}
    for problem in boundary_scan(['bad_lexical.cpp'], fixtures)[0]:
        path, number = problem.split(':')[:2]
        found.setdefault(os.path.basename(path), set()).add(int(number))
    if found != lexical:
        print(f'FAIL rt-lint self-test bad_lexical.cpp: expected {lexical}, got {found}')
        failed = True
    # The fixture tree (the re-checker's rc2/lint2 paths plus the location and
    # path controls): every production root, nested tests/ directories, every
    # suffix, a no-suffix file that something includes; the top-level tests/
    # is skipped; the allowlists and the annotation location go by path.
    tree = os.path.join(fixtures, 'tree')
    access_lines = {'include/api_extra.h': {2}, 'markuppp/src/Markup_extra.cpp': {2},
                    'markuppp/src/Markup_plain.cpp': {2}, 'minifypp/include/minify_extra.h': {2},
                    'src/embed/aggregate.h': {4}, 'src/embed/control_plain.cpp': {2},
                    'src/embed/control_using.h': {1}, 'src/embed/noext': {2}, 'src/embed/t1_annot.cpp': {2, 6},
                    'src/embed/t4_splice.cpp': {2}, 'src/embed/t5_digit.cpp': {2}, 'src/embed/t6_raw.cpp': {2},
                    'src/embed/x.def': {2}, 'src/embed/x.hxx': {2}, 'src/embed/x.inc': {2}, 'src/embed/x.ipp': {2},
                    'src/rt/tests/helper.h': {4}, 'src/tests/helper.h': {4}}
    # storage-write (StorageTestAccess and Storage:: are its tokens too), paste
    storage_lines = {'bindings/node/binding_extra.cc': {1}, 'include/api_extra.h': {2},
                     'markuppp/src/Markup_extra.cpp': {2, 3}, 'markuppp/src/Markup_plain.cpp': {2, 3},
                     'minifypp/include/minify_extra.h': {2, 3}, 'src/embed/aggregate.h': {4},
                     'src/embed/build.h': {3}, 'src/embed/control_plain.cpp': {2, 3},
                     'src/embed/control_using.h': {1}, 'src/embed/noext': {2, 3},
                     'src/embed/t1_annot.cpp': {2, 3, 6}, 'src/embed/t2_paste.cpp': {2, 4},
                     'src/embed/t3_digraph.cpp': {2, 4}, 'src/embed/t4_splice.cpp': {2, 4},
                     'src/embed/t5_digit.cpp': {2, 3}, 'src/embed/t6_raw.cpp': {2, 3}, 'src/embed/x.def': {2, 3},
                     'src/embed/x.hxx': {2, 3}, 'src/embed/x.inc': {2, 3}, 'src/embed/x.ipp': {2, 3},
                     'src/rt/tests/helper.h': {4, 5}, 'src/tests/helper.h': {4, 5}}
    problems, annotations, files = boundary_scan(list(BOUNDARY_ROOTS), tree)
    got_access, got_storage = {}, {}
    for problem in problems:
        path, number = problem.split(':')[:2]
        if 'test-access-decl` outside' in problem:
            continue  # the location rule, checked below
        target = got_access if ': test-access: ' in problem else got_storage
        target.setdefault(repo_rel(path, tree), set()).add(int(number))
    if got_access != access_lines or got_storage != storage_lines:
        print(f'FAIL rt-lint self-test tree: test-access {got_access} (expected {access_lines}); '
              f'storage-write {got_storage} (expected {storage_lines})')
        failed = True
    location = {(repo_rel(p.split(':')[0], tree), int(p.split(':')[1]))
                for p in problems if 'test-access-decl` outside' in p}
    if location != {('src/embed/aggregate.h', 4), ('src/embed/t1_annot.cpp', 2), ('src/embed/t1_annot.cpp', 6)} or 'src/embed/noext' not in files or 'tests/ok_helper.h' in files or \
            'src/rt/aggregate.h' not in files or 'src/rt/build.h' not in files:
        print(f'FAIL rt-lint self-test tree: location {sorted(location)}, files {sorted(files)}')
        failed = True
    # The manifest: exact as written; a missing, an extra and a changed entry each fail.
    manifest = os.path.join(tree, ANNOTATION_MANIFEST)
    if manifest_check(annotations, manifest):
        print(f'FAIL rt-lint self-test manifest: {manifest_check(annotations, manifest)}')
        failed = True
    changed = dict(annotations)
    changed[('src/rt/aggregate.h', 'test-access-decl')] += 1
    changed[('src/embed/x.ipp', 'storage-ok fresh')] = 1
    del changed[('src/embed/aggregate.h', 'test-access-decl')]
    if len(manifest_check(changed, manifest)) != 3:
        print(f'FAIL rt-lint self-test manifest mutations: {manifest_check(changed, manifest)}')
        failed = True
    print('FAIL rt-lint self-test' if failed else f'PASS rt-lint self-test: {len(expected) + 4} fixtures'
          f' (bad_lexical, tree: {len(files)} files over {len(BOUNDARY_ROOTS)} roots, manifest and 3 mutations)')
    return 1 if failed else 0


# Re-check R2: Storage befriends StorageTestAccess, which only the test tree
# defines (tests/rt/storage_access.h). Tooling checkpoint (component 5): this
# is now the boundary scan's test-access rule; the name may appear only on
# annotated lines of TEST_ACCESS_ALLOWED.
def test_access_scan(roots, root=REPO):
    return boundary_scan(roots, root, ('test-access',))[0]


def main(argv):
    if argv == ['--self-test']:
        return self_test()
    if argv[:1] == ['--test-access']:
        problems = test_access_scan(argv[1:] or list(BOUNDARY_ROOTS))
        for problem in problems:
            print(problem)
        print(f'{"FAIL" if problems else "PASS"} rt-lint test-access: {len(problems)} problem(s)')
        return 1 if problems else 0
    if argv[:1] == ['--boundary']:
        args, root, manifest = argv[1:], REPO, None
        while args[:1] in (['--root'], ['--manifest']):
            if args[0] == '--root':
                root = args[1]
            else:
                manifest = args[1]
            args = args[2:]
        manifest = manifest or os.path.join(root, ANNOTATION_MANIFEST)
        problems, annotations, files = boundary_scan(args or list(BOUNDARY_ROOTS), root)
        problems += manifest_check(annotations, manifest)
        for problem in problems:
            print(problem)
        print(f'{"FAIL" if problems else "PASS"} rt-lint boundary: {len(problems)} problem(s) in {len(files)} '
              f'files ({", ".join(args or BOUNDARY_ROOTS)} and their includes); '
              f'{sum(annotations.values())} annotations against {os.path.relpath(manifest, root)}')
        return 1 if problems else 0
    problems = lint(argv or ['src/rt'])
    for problem in problems:
        print(problem)
    print(f'{"FAIL" if problems else "PASS"} rt-lint: {len(problems)} problem(s)')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
