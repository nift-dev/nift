#!/usr/bin/env python3
"""Link the actual CLI object set with an allocation-forbidden drain witness.

B10 (combined-b3-b5.md section 73): the compiler, flags, libffi archive and object
list come from the Makefile (tests/runtime2/build_toolchain.py), and a failed link prints its stderr."""
import argparse,subprocess
from pathlib import Path
import sys as _sys,os as _os
_sys.path.insert(0,_os.path.join(_os.path.dirname(_os.path.abspath(__file__)),'..'))
import build_toolchain as toolchain
p=argparse.ArgumentParser();p.add_argument('--out',required=True);a=p.parse_args()
out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True)
objects=toolchain.cli_objects(without_main=True)
if 'src/rt/vm_collection_sort.o' not in objects:raise SystemExit('src/rt/vm_collection_sort.o is not in nift\'s object list')

obj=out/'drain-test.o'
toolchain.run(toolchain.production_compile('-DNIFT_COLLECTION_ALLOC_TEST','-c','src/rt/vm_collection_sort.cpp','-o',str(obj)),'drain object compile')
driver=out/'drain-allocation-driver.o'
toolchain.run(toolchain.test_compile('-c','tests/runtime2/cp14b/drain_allocation.cpp','-o',str(driver)),'drain driver compile')
binary=out/'drain-allocation'
toolchain.run(toolchain.link([driver,obj,*[o for o in objects if o!='src/rt/vm_collection_sort.o']],binary),'drain link')
subprocess.run([str(binary)],check=True,timeout=30)

driver=out/'allocation-failure-driver.o'
toolchain.run(toolchain.test_compile('-c','tests/runtime2/cp14b/allocation_failure.cpp','-o',str(driver)),'allocation-failure driver compile')
oom=out/'allocation-failure'
toolchain.run(toolchain.link([driver,*objects],oom),'allocation-failure link')
subprocess.run([str(oom)],check=True,timeout=60)
