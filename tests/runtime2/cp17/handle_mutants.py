#!/usr/bin/env python3
"""CP17 typed handles: mutation check of the unit suite.

Each mutant removes one rule of the handle table (generation check, owner
check, LIFO cleanup, build-family detach ban, ...) in a private copy of
src/rt/handle* and src/rt/handles, rebuilds tests/rt/handle_table_unit and
requires the suite to fail. Usage: handle_mutants.py [workdir]
"""
import os, shutil, subprocess, sys, tempfile
R=os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
S=sys.argv[1] if len(sys.argv) > 1 else tempfile.mkdtemp(prefix='cp17-mutants-')
M=[
 ("stale-resolves","src/rt/handles/slot_store.cpp","if (generation < s.generation) return HandleStatus::Stale;\n            if (generation > s.generation","if (generation > s.generation"),
 ("no-gen-bump","src/rt/handles/slot_store.cpp","++s.generation;  // every","s.generation += 0;  // every"),
 ("no-owner-check-release","src/rt/handles/table.cpp","if (s.owner != expected_owner) return HandleStatus::NotOwned;\n    if (s.counted","if (s.counted"),
 ("no-inuse","src/rt/handles/table.cpp","if (s.counted && s.refs > 1) return HandleStatus::InUse;","") ,
 ("fifo-close","src/rt/handles/table.cpp","take_locked(c->handles.tail);  // newest first","take_locked(c->handles.head);"),
 ("detach-in-build","src/rt/handles/table.cpp","if (c->build_family) return HandleStatus::DetachForbidden;",""),
 ("transfer-out-of-build","src/rt/handles/table.cpp","if (from->build_family && !to->build_family) return HandleStatus::TransferForbidden;",""),
 ("access-anyone","src/rt/handles/context_tree.cpp","if (accessor == owner || owner == kDetached","if (true || owner == kDetached"),
 ("create-in-closing","src/rt/handles/table.cpp","if (c && c->state != ContextState::Open) status = HandleStatus::ContextClosed;",""),
 ("no-tag-check","src/rt/handles/slot_store.cpp","if (static_cast<std::uint32_t>(id >> 32) != tag_) return HandleStatus::Foreign;",""),
 ("no-kind-check","src/rt/handles/slot_store.cpp","generation > s.generation || s.kind != expected","generation > s.generation"),
 ("no-retire","src/rt/handles/slot_store.cpp","if (s.generation == kMaxGeneration) {  // a bump","if (false) {  // a bump"),
 ("hook-failure-swallowed","src/rt/handles/table.cpp","    if (outcome.ok) return true;\n    failure","    return true;\n    failure"),
 ("drop-no-abandon","src/rt/handles/table.cpp","--s.refs > 0) return HandleStatus::Ok;","--s.refs >= 0) return HandleStatus::Ok;"),
 ("no-capacity","src/rt/handles/slot_store.cpp","if (slots_.size() >= capacity_) return HandleStatus::LimitExceeded;",""),
]
res=[]
for name,f,old,new in M:
    d=os.path.join(S,'m',name); shutil.rmtree(d,ignore_errors=True)
    shutil.copytree(R+'/src/rt/handles',d+'/src/rt/handles'); 
    for x in ['handle.h','handle.cpp']: shutil.copy(R+'/src/rt/'+x,d+'/src/rt/'+x)
    p=os.path.join(d,f); t=open(p).read(); assert old in t,(name); open(p,'w').write(t.replace(old,new,1))
    srcs=[d+'/src/rt/handle.cpp']+[os.path.join(d,'src/rt/handles',x) for x in os.listdir(d+'/src/rt/handles') if x.endswith('.cpp')]+[R+'/src/rt/'+x for x in ['value.cpp','string.cpp','heap_account.cpp']]
    exe=d+'/t'
    c=subprocess.run(['nice','g++','-std=c++17','-O1','-pthread','-I'+d+'/src','-I'+R+'/src','-I'+R+'/tests/rt',R+'/tests/rt/handle_table_unit.cpp',R+'/tests/rt/handle_table_ownership.cpp']+srcs+['-o',exe],capture_output=True,text=True)
    if c.returncode: res.append((name,'COMPILE-FAIL '+c.stderr[:200])); continue
    r=subprocess.run(['timeout','120',exe],capture_output=True,text=True)
    killed = r.returncode!=0
    res.append((name,'KILLED' if killed else 'SURVIVED', (r.stdout.strip().splitlines() or [''])[-1]))
    shutil.rmtree(d)
for x in res: print(*x)
survivors=[x for x in res if x[1]!='KILLED']
print('mutants: %d, survived: %d' % (len(res), len(survivors)))
sys.exit(1 if survivors else 0)
