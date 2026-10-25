#!/usr/bin/env python3
"""Build both actual VM dispatches with ASan/UBSan and test the program ABI.

Two compiler workers; each object cache binds all local headers, source bytes,
compiler version and flags. Run under the shared heavy-build lock.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--out', required=True)
a = parser.parse_args()
out = Path(a.out).resolve(); out.mkdir(parents=True, exist_ok=True)
flags = ['-Isrc', '-Iinclude', '-std=c++17', '-O1', '-g1', '-pthread',
         '-fno-omit-frame-pointer', '-fsanitize=address,undefined', '-fno-pie']
sources = sorted({str(p) for pattern in ('src/rt/*.cpp', 'src/rt/natives/*.cpp',
                                       'src/rt/host/*.cpp', 'src/rt/handles/*.cpp')
                  for p in Path().glob(pattern)})
sources += ['src/Sha256.cpp', 'src/JsonSchema.cpp', 'src/ParserHelpers.cpp',
            'src/ParserHostHelpers.cpp', 'src/Process.cpp', 'src/FileSystem.cpp',
            'src/RuntimeValue.cpp', 'src/embed/program_api.cpp']
key = hashlib.sha256(json.dumps(flags).encode() + subprocess.check_output(['g++', '--version']))
for directory in ('src', 'include'):
    for p in sorted(Path(directory).rglob('*.h')):
        key.update(str(p).encode()); key.update(p.read_bytes())
headers = key.digest()

def compile_one(source, switch=False):
    obj = out / (source.replace('/', '_') + ('-switch' if switch else '') + '.o')
    digest = hashlib.sha256(headers + Path(source).read_bytes() + bytes([switch])).hexdigest()
    stamp = obj.with_suffix('.sha256')
    if not obj.exists() or not stamp.exists() or stamp.read_text() != digest:
        result = subprocess.run(['g++', *flags, *(['-DNIFT_RT_VM_SWITCH'] if switch else []),
                                 '-c', source, '-o', str(obj)], capture_output=True)
        (out / (obj.name + '.log')).write_bytes(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(source + ': ' + result.stderr.decode()[-3000:])
        stamp.write_text(digest)
    return obj

with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
    objects = list(pool.map(compile_one, sources))
switch_obj = compile_one('src/rt/vm.cpp', True)
env = {**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1', 'UBSAN_OPTIONS': 'halt_on_error=1'}
rows = []
for mode in ('default', 'switch'):
    selected = [switch_obj if mode == 'switch' and src == 'src/rt/vm.cpp' else obj
                for src, obj in zip(sources, objects)]
    for test in ('program.c', 'allocation.cpp'):
        binary = out / (Path(test).stem + '-' + mode)
        subprocess.run(['g++', *flags, '-no-pie', str(Path('tests/runtime2/cp18') / test),
                        *map(str, selected), '-ldl', '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], env=env, capture_output=True, timeout=180)
        (out / (binary.name + '.run.log')).write_bytes(result.stdout + result.stderr)
        rows.append({'mode': mode, 'test': test, 'exit': result.returncode,
                     'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest()})
        (out / 'results.json').write_text(json.dumps(rows, indent=2) + '\n')
        print(binary.name, 'PASS' if result.returncode == 0 else 'FAIL', flush=True)
# Negative control: restore only the inherited unowned-reserve factory. The
# same allocation campaign must reject its leaked partial array under LSan.
aggregate = Path('src/rt/aggregate.cpp').read_text()
safe = """    // Own the object before reserve: embedding contains bad_alloc and keeps
    // the process alive, so a failed reserve must release the partial array.
    Value owned = Value::adopt(new ArrayObject());
    auto* array = owned.heap_as<ArrayObject>();
    array->items.reserve(reserve);
    heap_retally(array);
    return owned;"""
unsafe = """    auto* array = new ArrayObject();
    array->items.reserve(reserve);
    heap_retally(array);
    return Value::adopt(array);"""
if aggregate.count(safe) != 1:
    raise RuntimeError('stale ownership negative control')
mutant_source = out / 'unsafe-aggregate.cpp'
mutant_source.write_text(aggregate.replace(safe, unsafe))
mutant_obj = out / 'unsafe-aggregate.o'
subprocess.run(['g++', *flags, '-c', str(mutant_source), '-o', str(mutant_obj)], check=True)
negative_rows = []
for mode in ('default', 'switch'):
    selected = [mutant_obj if src == 'src/rt/aggregate.cpp' else
                switch_obj if mode == 'switch' and src == 'src/rt/vm.cpp' else obj
                for src, obj in zip(sources, objects)]
    binary = out / ('unsafe-array-' + mode)
    subprocess.run(['g++', *flags, '-no-pie', 'tests/runtime2/cp18/allocation.cpp',
                    *map(str, selected), '-ldl', '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], env=env, capture_output=True, timeout=180)
    (out / (binary.name + '.run.log')).write_bytes(result.stdout + result.stderr)
    detected = (result.returncode != 0 and b'LeakSanitizer: detected memory leaks' in result.stderr
                and b'make_array' in result.stderr and b'byte(s) leaked' in result.stderr)
    negative_rows.append({'mode': mode, 'detected': detected, 'exit': result.returncode,
                          'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                          'mutant_source_sha256': hashlib.sha256(mutant_source.read_bytes()).hexdigest()})
    (out / 'negative-controls.json').write_text(json.dumps(negative_rows, indent=2) + '\n')
    if not detected:
        raise RuntimeError('ownership negative control failed for ' + mode)
    print('PASS ownership negative control', mode, flush=True)
if any(row['exit'] != 0 for row in rows):
    raise RuntimeError('CP18 sanitizer controls failed; see per-test logs and results.json')
print('PASS CP18 ASan/UBSan: C consumer and allocation controls, both dispatches')
