#!/usr/bin/env bash
# B4b 23 (b4b-element-types.md section 15): build tests/rt/b4b_work.cpp against the kind
# inference of another commit, to measure that commit's analysis work with the same counters.
#   work_build.sh COMMIT OUT     (run in the repository; the current tree's objects must be built:
#                                 make .build/rt/b4b-types-unit)
#   COMMIT `.`: the working tree's inference; MUTANT=n compiles it with -DNIFT_B4B_MUTANT=n
# COMMIT's src/rt/infer*.{cpp,h} are extracted into a scratch directory; the current rt/infer.h
# (UnitKinds::work; the public interface is otherwise identical from B4a 0df0df6c to B4b 23)
# replaces COMMIT's, and the counters are added at the points the current inference counts:
#   nodes             the first line of Inference::expr and Inference::statement
#   enqueues          Inference::enqueue, where a unit is pushed onto the worklist
#   reader_checks     B4b 28: the first line of Inference::enqueue: every call, whether it pushes
#                     the unit or finds it queued (every reader check of every dependency list)
#   materializations  a site read computed (B4b before the memo of B4b 15: every aggregate read)
#   site_unions       Inference::unite_sites, after its early return (B4b 8 on)
#   reader_moves      the readers moved into the root's list by a union (B4b 8 on)
#   site_items        the site entries Inference::classes maps to classes (B4b 8 on)
#   unite_items       in unite (infer_types.cpp), after the early returns for empty, identical and
#                     unknown operands: one type plus both operands' entries (sites, fields,
#                     items, callables): the size of the type unions (B4a has them too)
# B4a has no sites: its last three counts are zero. The harness links COMMIT's inference
# objects with the current tree's other unit objects (plan, parser, resolver, globals).
set -euo pipefail
commit=$1; out=$2
root=$(git rev-parse --show-toplevel)
cd "$root"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
if [ "$commit" = . ]; then  # the working tree's inference (with MUTANT=n: a mutant of it)
  mkdir -p "$tmp/src/rt" && cp src/rt/infer*.cpp src/rt/infer*.h "$tmp/src/rt/"
else
  git archive "$commit" src/rt | tar -x -C "$tmp" --wildcards 'src/rt/infer*'
fi
counted=0  # B4b 23 on (7011492f): the commit's inference counts already (all but reader_checks)
if [ "$commit" != . ] && git show "$commit:src/rt/infer.h" | grep -q "struct Work"; then counted=1; fi
cp src/rt/infer.h "$tmp/src/rt/infer.h"
cd "$tmp"
if [ "$commit" = . ]; then
  : # counted already
elif [ $counted = 1 ]; then
  python3 - src/rt/infer.cpp <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
old = "void Inference::enqueue(std::size_t unit) {\n"
assert s.count(old) == 1, "enqueue"
open(p, "w").write(s.replace(old, old + "    ++result_.work.reader_checks;\n"))
PY
else
patch() {  # file, exact line, the line to put after it; fails if the line is not there once
  python3 - "$1" "$2" "$3" <<'PY'
import sys
path, line, add = sys.argv[1:]
text = open(path).read()
if text.count(line + "\n") != 1:
    sys.exit("work_build.sh: %s: %r found %d times" % (path, line, text.count(line + "\n")))
open(path, "w").write(text.replace(line + "\n", line + "\n" + add + "\n"))
PY
}
patch src/rt/infer.cpp "TyRef Inference::expr(NodeId id) {" "    ++result_.work.nodes;"
patch src/rt/infer.cpp "void Inference::enqueue(std::size_t unit) {" "    ++result_.work.reader_checks;"
python3 - src/rt/infer.cpp src/rt/infer_types.cpp <<'PY'
import sys
run, types = sys.argv[1:]
s = open(run).read()
old = "UnitKinds Inference::run(std::size_t max_walks) {\n"
assert s.count(old) == 1, "run"
s = s.replace(old, "extern thread_local std::size_t* unite_work;\n" + old + """    struct Counting {
        std::size_t* saved;
        explicit Counting(std::size_t* to) : saved(unite_work) { unite_work = to; }
        ~Counting() { unite_work = saved; }
    } counting(&result_.work.unite_items);
""")
open(run, "w").write(s)
s = open(types).read()
head = "TyRef unite(const TyRef& a, const TyRef& b, int depth) {\n"
anyblock = """        return top_ty();
    }
"""
assert s.count(head) == 1, "unite"
i = s.index(head)
j = s.index(anyblock, i) + len(anyblock)
s = s[:i] + "thread_local std::size_t* unite_work = nullptr;\n\n" + s[i:j] + """    if (unite_work)
        *unite_work += 1 + a->sites.size() + b->sites.size() + a->fields.size() + b->fields.size() +
                       a->items.size() + b->items.size() + a->callables.size() + b->callables.size();
""" + s[j:]
open(types, "w").write(s)
PY
patch src/rt/infer_walk.cpp "void Inference::statement(NodeId id) {" "    ++result_.work.nodes;"
python3 - src/rt/infer.cpp <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
old = "    if (!queued_[unit]) queue_.push_back(unit);\n"
assert s.count(old) == 1, "enqueue"
open(p, "w").write(s.replace(old, "    if (!queued_[unit]) queue_.push_back(unit), ++result_.work.enqueues;\n"))
PY
if [ -f src/rt/infer_sites.cpp ]; then
  if grep -q "Inference::unite_sites" src/rt/infer_sites.cpp; then  # site classes: B4b 8 on
  patch src/rt/infer_sites.cpp "    parent_[b] = a;" "    ++result_.work.site_unions;"
  patch src/rt/infer_sites.cpp "std::vector<std::uint32_t> Inference::classes(const std::vector<std::uint32_t>& sites, bool& open) {" "    result_.work.site_items += sites.size();"
  fi
  if [ -f src/rt/infer_prune.cpp ]; then  # B4b 15 on: the memo, a computed read is a miss
    patch src/rt/infer_prune.cpp "    TyRef out = materialize_now(t, read);" "    ++result_.work.materializations;"
  else
    patch src/rt/infer_sites.cpp "TyRef Inference::materialize(const TyRef& t) {" "    if (t && !t->any && (t->kinds & (kKindArray | kKindObject))) ++result_.work.materializations;"
  fi
  grep -q "Inference::unite_sites" src/rt/infer_sites.cpp && python3 - src/rt/infer_sites.cpp <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
old = "        readers_[a].insert(readers.begin(), readers.end());\n"
if s.count(old) != 1:  # B4b 15 on: UnitSet, smaller into larger
    old = "        for (std::size_t u : readers.list) into_readers.insert(u);\n"
assert s.count(old) == 1, "reader moves"
open(p, "w").write(s.replace(old, "        result_.work.reader_moves += readers.size();\n" + old))
PY
fi
fi
cd "$root"
CPP="-include src/build/production_tu.h -I$tmp/src -Isrc -Iinclude -Iminifypp/include -Iminifypp/src -Imarkuppp/include -Imarkuppp/vendor/cmark -I.build/libffi/$(${CC:-cc} -dumpmachine 2>/dev/null || uname -m)-$(basename "${CC:-cc}")/install/include"
FLAGS="-std=c++17 -O2 -Wall -Wextra -pedantic -pthread"
[ -n "${MUTANT:-}" ] && FLAGS="$FLAGS -DNIFT_B4B_MUTANT=$MUTANT"
objs=()
for src in "$tmp"/src/rt/infer*.cpp; do
  o="$tmp/$(basename "${src%.cpp}").o"
  ${CXX:-g++} $CPP $FLAGS -c "$src" -o "$o"
  objs+=("$o")
done
unit=$(make -s -n .build/rt/b4b-types-unit -W tests/rt/b4b_types_unit.cpp 2>/dev/null | grep -o 'tests/rt/b4b_types_unit.cpp .* -o' | sed 's|^tests/rt/b4b_types_unit.cpp ||; s| -o$||')
[ -n "$unit" ] || { echo "work_build.sh: could not read the unit objects from the Makefile"; exit 2; }
keep=$(printf '%s\n' $unit | grep -v '^src/rt/infer[a-z_]*\.o$' | tr '\n' ' ')
${CXX:-g++} -Isrc -Iinclude -Itests/rt $FLAGS tests/rt/b4b_work.cpp $keep "${objs[@]}" -ldl -o "$out"
echo "built $out from the inference of $commit"
