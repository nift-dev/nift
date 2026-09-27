#!/usr/bin/env python3
"""Reproducible v4.5 runtime microbenchmarks. Absolute values are host-specific."""
from __future__ import annotations
import argparse, json, os, pathlib, statistics, subprocess, tempfile, time

ROOT=pathlib.Path(__file__).resolve().parents[1]

def run(cmd, *, cwd=ROOT, env=None, check=True):
    return subprocess.run(cmd, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=check)

def median_ms(cmd, reps=7, cwd=ROOT):
    vals=[]
    for _ in range(reps):
        t=time.perf_counter_ns(); run(cmd,cwd=cwd); vals.append((time.perf_counter_ns()-t)/1e6)
    return statistics.median(vals)

def timed_script(nift, text, reps=5, args=()):
    with tempfile.TemporaryDirectory(prefix='nift-v45-perf-') as td:
        p=pathlib.Path(td)/'bench.f'; p.write_text(text)
        return median_ms([str(nift),str(p),*args], reps=reps)

def max_rss_kb(cmd):
    p=run(['/usr/bin/time','-f','%M',*map(str,cmd)], check=True)
    return int(p.stderr.strip().splitlines()[-1])

def embed_bench(prefix: pathlib.Path):
    pc=prefix/'lib/pkgconfig'
    if not (pc/'nift.pc').is_file(): return {'skipped': f'missing {pc}/nift.pc'}
    src=r'''#include <nift/c_abi.h>
#include <chrono>
#include <cstring>
#include <iostream>
int main(){
  using clock=std::chrono::steady_clock;
  constexpr int N=1000;
  auto a=clock::now();
  for(int i=0;i<N;++i){ auto *e=nift_engine_new(); if(!e)return 2; nift_engine_free(e); }
  auto b=clock::now();
  auto *e=nift_engine_new(); if(!e)return 3;
  const char *s="return 40 + 2;";
  for(int i=0;i<100;++i){ nift_script_result *r=nullptr; if(nift_engine_execute(e,s,strlen(s),"<bench>",7,nullptr,nullptr,0,&r)!=NIFT_OK)return 4; nift_script_result_free(r); }
  auto c=clock::now();
  nift_engine_free(e);
  constexpr int E=8;
  nift_engine *engines[E];
  auto d=clock::now();
  for(int i=0;i<E;++i) engines[i]=nift_engine_new();
  for(int round=0;round<50;++round) for(int i=0;i<E;++i){ nift_script_result *r=nullptr; if(nift_engine_execute(engines[i],s,strlen(s),"<bench>",7,nullptr,nullptr,0,&r)!=NIFT_OK)return 5; nift_script_result_free(r); }
  auto f=clock::now();
  for(auto *x:engines)nift_engine_free(x);
  auto ns=[](auto x){return std::chrono::duration_cast<std::chrono::nanoseconds>(x).count();};
  std::cout << "create_destroy_ns=" << ns(b-a)/N << "\n";
  std::cout << "reused_execute_ns=" << ns(c-b)/100 << "\n";
  std::cout << "eight_engine_execute_ns=" << ns(f-d)/(E*50) << "\n";
}'''
    with tempfile.TemporaryDirectory(prefix='nift-v45-embed-perf-') as td:
        td=pathlib.Path(td); cpp=td/'b.cpp'; exe=td/'b'; cpp.write_text(src)
        env=os.environ.copy(); env['PKG_CONFIG_PATH']=str(pc)+((':'+env['PKG_CONFIG_PATH']) if env.get('PKG_CONFIG_PATH') else '')
        flags=run(['pkg-config','--cflags','--libs','nift'],env=env,cwd=td).stdout.split()
        run(['c++','-O2','-std=c++17',str(cpp),*flags,'-Wl,-rpath,'+str(prefix/'lib'),'-o',str(exe)],env=env,cwd=td)
        out=run([str(exe)],env=env,cwd=td).stdout
    return {k:int(v) for k,v in (line.split('=',1) for line in out.splitlines())}

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--nift',default=str(ROOT/'nift')); ap.add_argument('--prefix',default=str(ROOT/'dist/embed-prefix')); ap.add_argument('--json-out')
    a=ap.parse_args(); nift=pathlib.Path(a.nift).resolve(); prefix=pathlib.Path(a.prefix).resolve()
    ffi_lib=ROOT/'.build/libnift_ffi_perf.so'; ffi_lib.parent.mkdir(exist_ok=True)
    run(['cc','-std=c99','-O2','-fPIC','-shared','tests/ffi/fixture.c','-o',str(ffi_lib)])
    thread_script = 'fn(worker(x)) { return x }\n' + ''.join(f't{i} := thread(worker, {i})\nt{i}.join()\n' for i in range(100))
    async_script = '@fn[async](worker(x)) { return x }\n' + ''.join(f'a{i} := worker({i})\nawait a{i}\n' for i in range(100))
    result={
      'nift_version': run([str(nift),'--version']).stdout.strip(),
      'cli_inline_median_ms': median_ms([str(nift),'-e','return 1;'],15),
      'cli_inline_max_rss_kb': max_rss_kb([nift,'-e','return 1;']),
      'thread_100_create_join_ms': timed_script(nift, thread_script, 5),
      'async_100_submit_await_ms': timed_script(nift, async_script, 5),
      'mutex_20000_increments_ms': timed_script(nift,'fn(inc(m,n)) { i := 0; while(i<n){ m.lock(); m.set(m.get()+1); m.unlock(); i+=1 }; return true }\nm:=mutex(0)\na:=thread(inc,m,5000); b:=thread(inc,m,5000); c:=thread(inc,m,5000); d:=thread(inc,m,5000)\na.join();b.join();c.join();d.join()\n',3),
      'ffi_5000_i64_calls_ms': timed_script(nift,f'lib:=ffi_open("{ffi_lib}")\ni:=0\nwhile(i<5000){{ ffi_call(lib,"nift_ffi_add_i64","i64(i64,i64)",i,1); i+=1 }}\nffi_close(lib)\n',3),
      'embedding': embed_bench(prefix),
    }
    text=json.dumps(result,indent=2,sort_keys=True); print(text)
    if a.json_out: pathlib.Path(a.json_out).write_text(text+'\n')
if __name__=='__main__': main()
