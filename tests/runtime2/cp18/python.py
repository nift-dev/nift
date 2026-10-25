"""Exercise the shipped Python wrapper, loading the actual compiled C ABI."""
import importlib.util
import os
from pathlib import Path
import sys
import threading

root = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('cp18_program', root/'bindings/python/nift/program.py')
mod = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = mod
spec.loader.exec_module(mod)
lib = sys.argv[1]
checks = 0

def check(value):
    global checks
    checks += 1
    if not value:
        raise AssertionError(checks)

# Neither NIFT_VM=0 nor classify can reroute the explicit compiled-program API.
for mode in ['0', '1', 'require', 'classify']:
    os.environ['NIFT_VM'] = mode
    with mod.Program('x := 0; x += 1; print(x); return 42;', library=lib) as p:
        for _ in range(10):
            result = p.run()
            check(result.ok and result.output == b'1\n' and result.return_text == '42' and result.returned)
        try:
            p.run(max_registers=1)
        except ValueError:
            check(True)
        else:
            check(False)
        check(p.run().ok)
    check(result.output == b'1\n')
    try:
        p.run()
    except RuntimeError:
        check(True)
    else:
        check(False)

with mod.Program('print(7);\nthrow error("boom");', logical_name='unicode-🦍.f', library=lib) as p:
    result = p.run()
    check(not result.ok and result.output == b'7\n' and result.message == 'boom')
    check(result.source == 'unicode-🦍.f' and result.line == 2 and result.catchable)
    errors = []
    def other_thread():
        for action in (p.run, p.close):
            try:
                action()
            except RuntimeError:
                errors.append(True)
    t = threading.Thread(target=other_thread); t.start(); t.join()
    check(errors == [True, True])
    check(p.run().message == 'boom')
    for limit in [-1, 2**100, 1.5]:
        try:
            p.run(heap_budget=limit)
        except ValueError:
            check(True)
        else:
            check(False)
check(result.message == 'boom')
for text, outcome in [('if (', 1), ('print(123); import("absent.f");', 2)]:
    try:
        mod.Program(text, library=lib)
    except mod.ProgramCompileError as error:
        check(error.result.outcome == outcome and error.result.output == b'')
    else:
        check(False)
# Retained origin survives a rethrow; callbacks/host handles are not involved.
with mod.Program('try { throw error("origin"); } catch(e) { throw e; }',
                 logical_name='rethrow.f', library=lib) as p:
    r = p.run()
    check(r.message == 'origin' and r.source == 'rethrow.f' and r.line == 1 and r.column == 7)
    check(r.code == 'user.raised' and r.catchable)
with mod.Program('fn(dive(n)) { if(n == 0) { return 0; } return dive(n-1); } print(dive(100));',
                 library=lib) as p:
    check(not p.run(max_frames=2).ok)
    check(p.run().output == b'0\n')
print(f'CP18 Python: {checks} checks passed')
