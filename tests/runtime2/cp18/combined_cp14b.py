"""CP14B collections through CP18's compiled-program API (combined-tree witness).

Owner request (2026-10-26) for the CP18 integration review: exercise CP14B's
admitted collection and copy-on-write behaviour through CP18's embedding API,
not each checkpoint alone. Loads the actual C ABI (argument: the shared
library; run for both VM dispatch builds) through the shipped Python wrapper.

  1. CP14B callbacks (map, filter, sort_by), mutation (push, splice) and a
     CP13B Error raised in a collection callback and caught, all on the VM;
     output exact and legacy-equal. `b := a; b.push(5)` grows `a` too
     ([9,1,2,4,5]; the program returns 5): B5 shared references, plan section 3
     "Assignment, declaration" (`b := a` shares; =L for arrays bound from a
     name), owner Q3. Until B5 this line pinned D-040 #1 copy semantics
     ([9,1,2,4], return 4); B5 fix-up conversion, recorded in
     docs/runtime-2/evidence/b5/expected-changes.tsv (cp18 combined_cp14b).
  2. Reuse: the same compiled program runs again with identical output.
  3. Resource failure: a heap budget that the setup fits stops the program
     exactly at the map callback's allocation (grow.f:3:17) as the VM's fatal
     "memory budget exceeded" (outcome INTERNAL, not catchable), after the
     earlier output; the same program then runs to completion.
  4. Ownership: results are read after their programs are closed; a further
     program compiles and runs after the failures.
No comparator sort (its VM path depends on the libstdc++ profile, covered by
the CP14B comparator gates). Allocation-failure (bad_alloc) cleanup through
the same API is covered by tests/runtime2/cp18/cow_allocation.cpp.
"""
import importlib.util
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('cp18_program', root/'bindings/python/nift/program.py')
mod = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = mod
spec.loader.exec_module(mod)
lib = sys.argv[1]
checks = 0


def check(label, value):
    global checks
    checks += 1
    if not value:
        raise AssertionError(f'{checks}: {label}')


COLLECTIONS = '''a := [3, 1, 2]
m := a.map(x => x * 10)
f := a.filter(x => x > 1)
s := a.sort_by(x => 0 - x)
a.push(4)
a.splice(0, 1, [9])
b := a
b.push(5)
print(m.stringify())
print(f.stringify())
print(s.stringify())
print(a.stringify())
print(b.stringify())
t := ""
fn(boom(x)) { throw error("boom", "user.boom") }
try { [1, 2].map(boom) } catch(e) { t = e.message + " " + e.code }
print(t)
return a.length()
'''
# B5 (plan section 3 Assignment, Q3): `b := a` shares, so `a` shows b's push, as legacy does.
# Was (D-040 #1, before B5): [9,1,2,4] and a return value of 4.
EXPECTED = b'[30,10,20]\n[3,2]\n[3,2,1]\n[9,1,2,4,5]\n[9,1,2,4,5]\nboom user.boom\n'

# range(4000) and the map's result reservation fit the budget (line 2 prints);
# the map callback's [x, x] (grow.f:3:17) is the allocation that exhausts it: the
# CP14B callback path. B5 fix-up: the former budget (1 << 18 = 262144) stops at the
# map call's own reservation (grow.f:3:6) on B5 AND on 17f7964c (identical through
# the program API, libnift_c.so and the switch library; masked at 17f7964c by the
# earlier failing check), so it no longer reached the callback. 393216 stops at
# 3:17 on both trees (budgets 300000 to 400000 do; 500000 completes).
GROW = 'a := range(4000)\nprint(a.length())\nr := a.map(x => [x, x])\nprint(r.length())\n'
BUDGET = 393216

with mod.Program(COLLECTIONS, 'collections.f', library=lib) as program:
    first = program.run()
    check('collections complete', first.ok and first.output == EXPECTED)
    check('return value', first.returned and first.return_text == '5')
    second = program.run()
    check('reuse: identical second run', second.ok and second.output == EXPECTED and second.return_text == '5')
check('result owned after close', first.output == EXPECTED and second.return_text == '5')

with mod.Program(GROW, 'grow.f', library=lib) as program:
    limited = program.run(heap_budget=BUDGET)
    # NIFT_PROGRAM_INTERNAL (4): the budget stop is fatal, never a catchable Error.
    check('heap budget stops the run as a fatal budget stop', limited.outcome == 4 and not limited.catchable)
    check('exact budget message', limited.message == 'memory budget exceeded' and limited.code == '')
    check('failing site is the map callback allocation',
          (limited.source, limited.line, limited.column) == ('grow.f', 3, 17))
    check('output before the failing map only', limited.output == b'4000\n')
    full = program.run()
    check('program reusable after the stop', full.ok and full.output == b'4000\n4000\n')
check('failed result owned after close', limited.outcome == 4 and limited.output == b'4000\n' and
      full.output == b'4000\n4000\n')

with mod.Program('print([1, 2, 3].filter(x => x != 2).stringify())', 'after.f', library=lib) as program:
    after = program.run()
    check('later program after failures', after.ok and after.output == b'[1,3]\n')

print(f'PASS CP14B collections through the CP18 program API: {checks} checks ({Path(lib).name})')
