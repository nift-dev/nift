"""Ordinary-build control: an unsafe explicit clone must fail real allocation accounting.

B2 (b2-refrep.md): retargeted from unshare (deleted) to array_shallow_clone,
the explicit clone that replaced it (loop/callback snapshots, splice, cell
descriptors): the mutant leaves the new array unowned while its items are
copied, so an allocation failure there leaks it."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(); parser.add_argument('--out',required=True)
# The exact libffi of the ordinary build (Makefile $(LIBFFI_A)), never a glob: a
# sanitizer libffi may sit beside it once the sanitizer tier has run.
parser.add_argument('--libffi',required=True);args=parser.parse_args()
out=Path(args.out).resolve();out.mkdir(parents=True,exist_ok=True)
source=Path('src/rt/aggregate.cpp').read_text()
start=source.index('Value array_shallow_clone(const Value& array) {');end=source.index('\n\nValue object_shallow_clone',start)
if 'Value owned = make_array(0);  // owned before copying (bad_alloc)' not in source[start:end]:raise RuntimeError('stale clone control')
mutant=out/'unsafe-cow.cpp'
mutant.write_text(source[:start]+Path('tests/runtime2/cp18/unsafe_shallow_clone.txt').read_text().rstrip()+source[end:])
obj=out/'unsafe-cow.o'
subprocess.run(['g++','-std=c++17','-O2','-Isrc','-Iinclude','-fPIC','-c',str(mutant),'-o',str(obj)],check=True)
objects=sorted(Path('.build/pic').rglob('*.o'))
ffi=[Path(args.libffi)]
if not ffi[0].is_file():raise RuntimeError('missing libffi: '+args.libffi)
rows=[]
for mode in ('default','switch'):
    chosen=[obj if p==Path('.build/pic/src/rt/aggregate.o') else
            Path('.build/cp18/vm-switch.o') if mode=='switch' and p==Path('.build/pic/src/rt/vm.o') else p
            for p in objects]
    binary=out/('unsafe-cow-'+mode)
    subprocess.run(['g++','-std=c++17','-O2','-Isrc','-Iinclude','-pthread',
                    'tests/runtime2/cp18/cow_allocation.cpp',*map(str,chosen),str(ffi[0]),'-ldl','-o',str(binary)],check=True)
    result=subprocess.run([str(binary)],capture_output=True,timeout=120)
    (out/(binary.name+'.run.log')).write_bytes(result.stdout+result.stderr)
    leak=re.search(rb'Outstanding allocations after program destruction delta ([1-9][0-9]*)',result.stdout)
    detected=(result.returncode==1 and b'outstanding_allocations == empty_allocations' in result.stderr)
    rows.append({'mode':mode,'detected':detected,'exit':result.returncode,
                 'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
                 'mutant_source_sha256':hashlib.sha256(mutant.read_bytes()).hexdigest()})
    (out/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
    if not detected:raise RuntimeError('ordinary COW control failed: '+mode)
    print('PASS ordinary COW negative control',mode)
