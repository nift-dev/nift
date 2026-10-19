#!/usr/bin/env python3
"""CP17 typed handles: mutation check of the unit suite.

Each mutant removes one rule of the handle table (generation check, owner
check, LIFO cleanup, build-family detach ban, ...) in a private copy of
src/rt/handle* and src/rt/handles, rebuilds the unit and lifecycle suites and
requires one of them to fail (a hang counts: each run has a 120 s timeout). Usage: handle_mutants.py [workdir]
"""
import os, shutil, subprocess, sys, tempfile
R=os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
S=sys.argv[1] if len(sys.argv) > 1 else tempfile.mkdtemp(prefix='cp17-mutants-')
M=[
 ("closed-resolves","src/rt/handles/slot_store.cpp","if (generation < s.generation) return HandleStatus::Closed;\n            if (generation > s.generation","if (generation > s.generation"),
 ("closed-as-forged","src/rt/handles/slot_store.cpp","return generation < s.generation ? HandleStatus::Closed : HandleStatus::Forged;","return HandleStatus::Forged;"),
 ("no-gen-bump","src/rt/handles/slot_store.cpp","++s.generation;  // every","s.generation += 0;  // every"),
 ("no-owner-check-release","src/rt/handles/table.cpp","if (store_.slot(index).owner != expected_owner) return HandleStatus::NotOwned;\n    return close_locked","return close_locked"),
 ("close-not-idempotent","src/rt/handles/table.cpp","if (status == HandleStatus::Closed) return HandleStatus::Ok;  // close() is idempotent (D-035)",""),
 ("fifo-close","src/rt/handles/table_contexts.cpp","begin_cleanup_locked(c->handles.tail, ReleaseReason::ContextEnd);  // newest first","begin_cleanup_locked(c->handles.head, ReleaseReason::ContextEnd);"),
 ("detach-in-build","src/rt/handles/table_contexts.cpp","if (c->build_family) return HandleStatus::DetachForbidden;",""),
 ("transfer-out-of-build","src/rt/handles/table_contexts.cpp","if (from->build_family && !to->build_family) return HandleStatus::TransferForbidden;",""),
 ("transfer-into-cancelling","src/rt/handles/table_contexts.cpp","bool accepts_handles(const ExecContext& c) { return c.state == ContextState::Open; }","bool accepts_handles(const ExecContext& c) { return c.state != ContextState::Closing; }"),
 ("no-reclaim-on-refusal","src/rt/handles/table_contexts.cpp","if (policy == TransferPolicy::ReclaimOnRefusal) {","if (false) {"),
 ("access-anyone","src/rt/handles/context_tree.cpp","if (accessor == owner || owner == kDetached","if (true || owner == kDetached"),
 ("create-in-closing","src/rt/handles/table.cpp","if (c && c->state != ContextState::Open) status = HandleStatus::ContextClosed;",""),
 ("no-tag-check","src/rt/handles/slot_store.cpp","if (static_cast<std::uint32_t>(id >> 32) != tag_) return HandleStatus::Foreign;",""),
 ("no-kind-check","src/rt/handles/slot_store.cpp","generation > s.generation || s.kind != expected","generation > s.generation"),
 ("no-retire","src/rt/handles/slot_store.cpp","if (s.generation == kMaxGeneration) {  // a bump","if (false) {  // a bump"),
 ("failures-swallowed","src/rt/handles/lifecycle.cpp","if (record.state == LifeState::Completed) return true;","if (true) return true;"),
 ("double-termination","src/rt/handles/table.cpp","        } else if (r.state == LifeState::Active) {\n            r.state = LifeState::Terminating;\n            r.cause = cause;","        } else if (true) {\n            r.state = LifeState::Terminating;\n            r.cause = cause;"),
 ("close-ignores-obligations","src/rt/handles/table_contexts.cpp","return !c || (c->in_flight == 0 && c->obligations == 0);","return !c || c->in_flight == 0;"),
 ("unreported-ticket-forgotten","src/rt/handles/completion.cpp","    if (table_) {\n        complete(","    if (false) {\n        complete("),
 ("drop-no-abandon","src/rt/handles/table.cpp","--s.refs > 0) return HandleStatus::Ok;","--s.refs >= 0) return HandleStatus::Ok;"),
 ("no-capacity","src/rt/handles/slot_store.cpp","if (slots_.size() >= capacity_) return HandleStatus::LimitExceeded;",""),
 ("no-context-label","src/rt/handles/table.cpp","        started.label = c.label;\n",""),
 ("no-context-error","src/rt/handles/table.cpp","started.context_error = reason == ReleaseReason::ContextEnd && c.ending_on_error;","started.context_error = false;"),
 ("incomplete-as-failed","src/rt/handles/lifecycle.cpp","case CleanupOutcome::Result::CleanupIncomplete: return LifeState::CleanupIncomplete;","case CleanupOutcome::Result::CleanupIncomplete: return LifeState::Failed;"),
 ("failed-as-completed","src/rt/handles/lifecycle.cpp","case CleanupOutcome::Result::Failed: return LifeState::Failed;","case CleanupOutcome::Result::Failed: return LifeState::Completed;"),
 ("lost-as-completed","src/rt/handles/lifecycle.cpp","default: return LifeState::Lost;  // Lost, and Pending","default: return LifeState::Completed;  // Lost, and Pending"),
 ("incomplete-not-settled","src/rt/handles/lifecycle.cpp"," ||\n           state == LifeState::CleanupIncomplete;",";"),
 ("failure-means-alive","src/rt/handles/types.h","        return cleanup_incomplete(std::move(code), std::move(message));\n    }\n    static CleanupOutcome pending","        return CleanupOutcome{Result::Failed, std::move(code), std::move(message)};\n    }\n    static CleanupOutcome pending"),
 ("throw-as-incomplete","src/rt/handles/table.cpp","outcome = CleanupOutcome::lost(\"internal.handle_cleanup_threw\", e.what());","outcome = CleanupOutcome::cleanup_incomplete(\"internal.handle_cleanup_threw\", e.what());"),
 ("exit-status-ignores-failures","src/rt/handles/types.h","(cleanup_failed() ? 1 : 0)","0"),
 ("detach-while-terminating","src/rt/handles/table_contexts.cpp","if (r.state != LifeState::Active) return HandleStatus::DetachFailed;",""),
 ("detached-supervised","src/rt/handles/table.cpp","    if (owner == kDetached) {   // D-039","    if (false) {   // D-039"),
 ("detached-cancel-ticketed","src/rt/handles/table.cpp","if (store_.slot(index).owner == kDetached) {  // D-039","if (false) {  // D-039"),
 ("detached-failures-count","src/rt/handles/table_contexts.cpp","append(report.detached_failures, detached_failures_);","append(report.failures, detached_failures_);"),
 ("limit-setting-ignored","src/rt/handles/table.cpp","if (!invalid_setting_.empty()) status = HandleStatus::InvalidSetting;","if (false) status = HandleStatus::InvalidSetting;"),
]
res=[]
for name,f,old,new in M:
    d=os.path.join(S,'m',name); shutil.rmtree(d,ignore_errors=True)
    shutil.copytree(R+'/src/rt/handles',d+'/src/rt/handles'); 
    for x in ['handle.h','handle.cpp']: shutil.copy(R+'/src/rt/'+x,d+'/src/rt/'+x)
    p=os.path.join(d,f); t=open(p).read(); assert old in t,(name); open(p,'w').write(t.replace(old,new,1))
    srcs=[d+'/src/rt/handle.cpp']+[os.path.join(d,'src/rt/handles',x) for x in os.listdir(d+'/src/rt/handles') if x.endswith('.cpp')]+[R+'/src/rt/'+x for x in ['value.cpp','string.cpp','heap_account.cpp']]
    exe=d+'/t'
    flags=['nice','g++','-std=c++17','-O1','-pthread','-I'+d+'/src','-I'+R+'/src','-I'+R+'/tests/rt']
    c=subprocess.run(flags+[R+'/tests/rt/handle_table_unit.cpp',R+'/tests/rt/handle_table_ownership.cpp']+srcs+['-o',exe],capture_output=True,text=True)
    c2=subprocess.run(flags+[R+'/tests/rt/handle_lifecycle_unit.cpp']+srcs+['-o',exe+'2'],capture_output=True,text=True)
    if c.returncode or c2.returncode: res.append((name,'COMPILE-FAIL '+(c.stderr+c2.stderr)[:300])); shutil.rmtree(d); continue
    r=subprocess.run(['timeout','120',exe],capture_output=True,text=True)
    r2=subprocess.run(['timeout','120',exe+'2'],capture_output=True,text=True)
    killed = r.returncode!=0 or r2.returncode!=0
    r.stdout = r.stdout + r2.stdout + ('(timeout)' if r2.returncode == 124 or r.returncode == 124 else '')
    res.append((name,'KILLED' if killed else 'SURVIVED', (r.stdout.strip().splitlines() or [''])[-1]))
    shutil.rmtree(d)
for x in res: print(*x)
survivors=[x for x in res if x[1]!='KILLED']
print('mutants: %d, survived: %d' % (len(res), len(survivors)))
sys.exit(1 if survivors else 0)
