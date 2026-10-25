#!/usr/bin/env python3
"""Two-compiler, source-hashed ASan/UBSan harness build; run under heavy lock."""
import argparse,concurrent.futures,hashlib,json,os,subprocess
from pathlib import Path

def main():
    p=argparse.ArgumentParser();p.add_argument('--out',required=True);a=p.parse_args();out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
    sources=sorted(set(str(f) for glob in ['src/rt/*.cpp','src/rt/natives/*.cpp','src/rt/host/*.cpp','src/rt/handles/*.cpp'] for f in Path().glob(glob)))
    sources+=['src/Sha256.cpp','src/JsonSchema.cpp','src/ParserHelpers.cpp','src/ParserHostHelpers.cpp','src/Process.cpp','src/FileSystem.cpp','src/RuntimeValue.cpp','tests/runtime2/cp14b/vm_harness.cpp']
    flags=['-Isrc','-Iinclude','-Itests/rt','-std=c++17','-O1','-g1','-pthread','-fno-omit-frame-pointer','-fsanitize=address,undefined','-fno-pie']
    # Include all source/header bytes so a reused object never misses a changed
    # inline function or ABI. Flags/compiler version are part of this key.
    digest=hashlib.sha256(json.dumps(flags).encode()+subprocess.check_output(['g++','--version']))
    for f in sorted(Path('src').rglob('*.h')):digest.update(str(f).encode());digest.update(f.read_bytes())
    header_key=digest.digest()
    def compile_one(src):
        obj=out/(src.replace('/','_')+'.o');key=hashlib.sha256(header_key+Path(src).read_bytes()).hexdigest();stamp=obj.with_suffix('.sha256')
        if not obj.exists() or not stamp.exists() or stamp.read_text()!=key:
            proc=subprocess.run(['g++',*flags,'-c',src,'-o',str(obj)],capture_output=True,text=True)
            (out/(obj.name+'.log')).write_text(proc.stdout+proc.stderr)
            if proc.returncode:raise RuntimeError(src+' '+proc.stderr[-3000:])
            stamp.write_text(key)
        return obj
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:objects=list(pool.map(compile_one,sources))
    binary=out/'cp14b-san'
    subprocess.run(['g++','-pthread','-no-pie','-fsanitize=address,undefined',*map(str,objects),'-ldl','-o',str(binary)],check=True)
    switch=out/'vm-switch.o'
    subprocess.run(['g++',*flags,'-DNIFT_RT_VM_SWITCH','-c','src/rt/vm.cpp','-o',str(switch)],check=True)
    alternate=[switch if str(o).endswith('src_rt_vm.cpp.o') else o for o in objects]
    subprocess.run(['g++','-pthread','-no-pie','-fsanitize=address,undefined',*map(str,alternate),'-ldl','-o',str(out/'cp14b-san-switch')],check=True)
    print('PASS ASan/UBSan harnesses built',len(sources),'translation units',flush=True)
if __name__=='__main__':main()
