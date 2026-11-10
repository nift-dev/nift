#!/usr/bin/env python3
"""Two-compiler, source-hashed ASan/UBSan harness build; run under heavy lock.

B10 (combined-b3-b5.md section 73): the compiler, CPPFLAGS, the production guard,
the libffi archive and the RT source set (RT_VALUE_SOURCES plus RuntimeValue.cpp,
which includes PackageTransaction.cpp) come from the Makefile (tests/runtime2/build_toolchain.py); the
sanitizer flags stay this harness's own. A failed compile or link prints its stderr."""
import argparse,concurrent.futures,hashlib,json
from pathlib import Path
import sys as _sys,os as _os
_sys.path.insert(0,_os.path.join(_os.path.dirname(_os.path.abspath(__file__)),'..'))
import build_toolchain as toolchain

def main():
    p=argparse.ArgumentParser();p.add_argument('--out',required=True);a=p.parse_args();out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
    harness='tests/runtime2/cp14b/vm_harness.cpp'
    sources=sorted(set(toolchain.rt_sources()))+[harness]
    flags=['-std=c++17','-O1','-g1','-pthread','-fno-omit-frame-pointer','-fsanitize=address,undefined','-fno-pie']
    def command(src,*extra):
        # Production units get nift's guard; the harness (a test unit) never does.
        prefix=[*toolchain.cxx(),*([] if src.startswith('tests/') else toolchain.guard()),*toolchain.cppflags(),'-Itests/rt']
        return [*prefix,*flags,*extra]
    # Include all source/header bytes so a reused object never misses a changed
    # inline function or ABI. Flags/compiler version are part of this key.
    digest=hashlib.sha256(json.dumps([command('src/x'),command(harness)]).encode()+toolchain.version().encode())
    for f in sorted(Path('src').rglob('*.h')):digest.update(str(f).encode());digest.update(f.read_bytes())
    header_key=digest.digest()
    def compile_one(src):
        obj=out/(src.replace('/','_')+'.o');key=hashlib.sha256(header_key+Path(src).read_bytes()).hexdigest();stamp=obj.with_suffix('.sha256')
        if not obj.exists() or not stamp.exists() or stamp.read_text()!=key:
            toolchain.run(command(src,'-c',src,'-o',str(obj)),'sanitizer compile '+src,log=out/(obj.name+'.log'))
            stamp.write_text(key)
        return obj
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:objects=list(pool.map(compile_one,sources))
    link=['-pthread','-no-pie','-fsanitize=address,undefined']
    binary=out/'cp14b-san'
    toolchain.run([*toolchain.cxx(),*link,*map(str,objects),'-ldl',toolchain.libffi_a(),'-o',str(binary)],'sanitizer link cp14b-san')
    switch=out/'vm-switch.o'
    toolchain.run(command('src/rt/vm.cpp','-DNIFT_RT_VM_SWITCH','-c','src/rt/vm.cpp','-o',str(switch)),'sanitizer compile vm switch')
    alternate=[switch if str(o).endswith('src_rt_vm.cpp.o') else o for o in objects]
    toolchain.run([*toolchain.cxx(),*link,*map(str,alternate),'-ldl',toolchain.libffi_a(),'-o',str(out/'cp14b-san-switch')],'sanitizer link cp14b-san-switch')
    print('PASS ASan/UBSan harnesses built',len(sources),'translation units',flush=True)
if __name__=='__main__':main()
