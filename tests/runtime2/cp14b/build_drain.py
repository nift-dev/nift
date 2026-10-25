#!/usr/bin/env python3
"""Link the actual CLI object set with an allocation-forbidden drain witness."""
import argparse,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out',required=True);a=p.parse_args()
out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True)
flags=['g++','-Isrc','-Iinclude','-Itests/rt','-std=c++17','-O2','-pthread']
obj=out/'drain-test.o'
subprocess.run([*flags,'-DNIFT_COLLECTION_ALLOC_TEST','-c','src/rt/vm_collection_sort.cpp','-o',str(obj)],check=True)
objects=[str(p) for root in ['src','minifypp','markuppp'] for p in Path(root).rglob('*.o') if str(p) not in ['src/nift.o','src/rt/vm_collection_sort.o']]
binary=out/'drain-allocation'
subprocess.run([*flags,'tests/runtime2/cp14b/drain_allocation.cpp',str(obj),*objects,'-ldl','.build/libffi/x86_64-linux-gnu-cc/install/lib/libffi.a','-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True,timeout=30)

ordinary=[str(p) for root in ['src','minifypp','markuppp'] for p in Path(root).rglob('*.o') if str(p)!='src/nift.o']
oom=out/'allocation-failure'
subprocess.run([*flags,'tests/runtime2/cp14b/allocation_failure.cpp',*ordinary,'-ldl','.build/libffi/x86_64-linux-gnu-cc/install/lib/libffi.a','-o',str(oom)],check=True)
subprocess.run([str(oom)],check=True,timeout=60)
