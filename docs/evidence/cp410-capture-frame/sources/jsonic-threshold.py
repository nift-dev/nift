from pathlib import Path
import shutil,json,subprocess,re,os,hashlib
p=Path('.build/cp410-capture-frame/jsonic');p.mkdir(parents=True,exist_ok=True);canonical=Path('/home/nick/Repositories/nift/jsonic/jsonic/include/json.h');original=canonical.read_bytes();s=original.decode();s=s.replace('            result.object.reserve(4);','''            result.object.reserve(4);
            std::unordered_set<std::string> membership;
            bool indexed = false;''',1)
a='''                if (options_.duplicate_keys == DuplicateKeyPolicy::Reject && result.has(key))
                    fail("duplicate object key '" + key + "'");''';b='''                if (options_.duplicate_keys == DuplicateKeyPolicy::Reject) {
                    if (!indexed && result.object.size() >= CP_THRESHOLD) {
                        membership.reserve(CP_THRESHOLD * 2);
                        for (const auto& member : result.object) membership.insert(member.first);
                        indexed = true;
                    }
                    if (indexed ? !membership.insert(key).second : result.has(key))
                        fail("duplicate object key '" + key + "'");
                }''';assert s.count(a)==1;(p/'json.h').write_text(s.replace(a,b));(p/'canonical.json').write_text(json.dumps({'header':str(canonical),'sha256':hashlib.sha256(original).hexdigest(),'production_modified':False},indent=2)+'\n')
shutil.copy2('.build/cp410-followup/jsonic/probe.cpp',p/'probe.cpp')
(p/'Phases.h').write_text('''#pragma once
#include <cstdio>
#include <cstdlib>
#include <new>
#include <valgrind/callgrind.h>
namespace cp51 { enum Id {Factory};
struct Stats {unsigned long long allocations=0,bytes=0,live=0,peak=0,base=0; bool active=false;~Stats(){std::fprintf(stderr,"parse allocations=%llu bytes=%llu peak_payload=%llu\\n",allocations,bytes,peak);}};
inline Stats stats;
inline void allocated(std::size_t n){stats.live+=n;if(stats.active){++stats.allocations;stats.bytes+=n;if(stats.live-stats.base>stats.peak)stats.peak=stats.live-stats.base;}}
inline void freed(std::size_t n){stats.live-=n;}
struct Phase {Phase(Id){stats.base=stats.live;stats.active=true;CALLGRIND_TOGGLE_COLLECT;}~Phase(){CALLGRIND_TOGGLE_COLLECT;stats.active=false;}};
}
''')
(p/'Alloc.cpp').write_text('''#include "Phases.h"
#include <cstddef>
#include <cstdint>
struct alignas(std::max_align_t) Header {void* raw;std::size_t size;};
void* get(std::size_t n,std::size_t a){const auto size=n?n:1;void* raw=std::malloc(size+sizeof(Header)+a);if(!raw)throw std::bad_alloc();auto u=(reinterpret_cast<std::uintptr_t>(raw)+sizeof(Header)+a-1)&~(a-1);auto* h=reinterpret_cast<Header*>(u)-1;h->raw=raw;h->size=n;cp51::allocated(n);return reinterpret_cast<void*>(u);}
void release(void* p){if(!p)return;auto* h=static_cast<Header*>(p)-1;cp51::freed(h->size);std::free(h->raw);}
void* operator new(std::size_t n){return get(n,alignof(std::max_align_t));}void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{release(p);}void operator delete[](void*p)noexcept{release(p);}void operator delete(void*p,std::size_t)noexcept{release(p);}void operator delete[](void*p,std::size_t)noexcept{release(p);}
void* operator new(std::size_t n,std::align_val_t a){return get(n,static_cast<std::size_t>(a));}void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void*p,std::align_val_t)noexcept{release(p);}void operator delete[](void*p,std::align_val_t)noexcept{release(p);}void operator delete(void*p,std::size_t,std::align_val_t)noexcept{release(p);}void operator delete[](void*p,std::size_t,std::align_val_t)noexcept{release(p);}
''')
binaries={}
# The baseline include must precede the scratch directory containing json.h.
(p/'baseline-probe.cpp').write_text((p/'probe.cpp').read_text().replace('#include "json.h"','#include "'+str(canonical)+'"'))
for threshold in ('baseline',16,32,64,128):
 binary=p/('probe-'+str(threshold));cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(p),str(p/('baseline-probe.cpp' if threshold=='baseline' else 'probe.cpp')),str(p/'Alloc.cpp'),'-o',str(binary)]
 if threshold!='baseline':cmd.insert(4,'-DCP_THRESHOLD='+str(threshold))
 subprocess.run(cmd,check=True);binaries[str(threshold)]=str(binary.resolve())
rows=[]
for width in (8,16,32,64,128,256,512,1000,2000,4000,8000):
 for kind in ('unique','early','middle','end','escaped'):
  entries=[json.dumps('k'+str(i))+':'+str(i) for i in range(width)]
  if kind=='early':entries[2]='"k0":2'
  if kind=='middle':entries[width//2]='"k0":2'
  if kind=='end':entries[-1]='"k0":2'
  if kind=='escaped':entries[-1]='"\\u006b0":2'
  source='{'+','.join(entries)+'}';path=p/(kind+'-'+str(width)+'.json');path.write_text(source);expected=None
  for label,binary in binaries.items():
   cg=p/f'{kind}-{width}-{label}.callgrind';r=subprocess.run(['valgrind','--tool=callgrind','--collect-atstart=no','--callgrind-out-file='+str(cg),binary,str(path.resolve()),'reject'],capture_output=True,text=True,check=True)
   if expected is None:expected=r.stdout
   assert r.stdout==expected,(kind,width,label,r.stdout,expected)
   m=re.search(r'parse allocations=(\d+) bytes=(\d+) peak_payload=(\d+)',r.stderr);assert m
   row={'width':width,'kind':kind,'threshold':label,'instructions':int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]),'allocations':int(m[1]),'bytes':int(m[2]),'peak_payload_bytes':int(m[3]),'stdout':r.stdout};rows.append(row);Path('docs/evidence/cp410-capture-frame/jsonic-thresholds.json').write_text(json.dumps(rows,indent=2)+'\n');print(width,kind,label,row['instructions'],row['allocations'],row['peak_payload_bytes'],flush=True)
assert canonical.read_bytes()==original
print('PASS 275 threshold profiles; canonical and vendored implementations unchanged')
