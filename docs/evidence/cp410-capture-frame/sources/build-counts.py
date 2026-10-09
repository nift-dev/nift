from pathlib import Path
import subprocess,json,shutil
p=Path('.build/cp410-capture-frame/phases');p.mkdir(parents=True,exist_ok=True);old=Path('.build/cp410-followup/phases')
for name in ('Phases.h','Alloc.cpp','ParserExpression.cpp'):
 shutil.copy2(old/name,p/name)
s=(p/'Phases.h').read_text();s=s.replace('struct Entry {','struct BindingCounts {unsigned long long entries=0,inserts=0,overwrites=0,value_refs=0,slot_refs=0,root_refs=0,path_vectors=0,path_components=0,path_key_bytes=0,default_slots=0,explicit_slots=0;};\nstruct Entry {',1).replace('struct Stats {Entry entries[Count];','struct Stats {BindingCounts binding[2]; Entry entries[Count];',1)
s=s.replace('~Stats(){for(int i=0;', '~Stats(){for(int j=0;j<2;++j){const auto& b=binding[j];std::fprintf(stderr,"bindings %s entries=%llu inserts=%llu overwrites=%llu value_refs=%llu slot_refs=%llu root_refs=%llu path_vectors=%llu path_components=%llu path_key_bytes=%llu default_slots=%llu explicit_slots=%llu\\n",j?"frame":"capture",b.entries,b.inserts,b.overwrites,b.value_refs,b.slot_refs,b.root_refs,b.path_vectors,b.path_components,b.path_key_bytes,b.default_slots,b.explicit_slots);}\nfor(int i=0;',1)
s=s.replace('inline void allocation','inline void binding_slot(bool explicit_slot){for(int j=0;j<2;++j)if(stats.entries[j?Frame:Capture].depth){if(explicit_slot)++stats.binding[j].explicit_slots;else ++stats.binding[j].default_slots;}}\ninline void allocation',1);s=s.replace('for(int i=0;i<Count;++i)std::fprintf','for(int i=0;i<Count;++i){std::fprintf').replace('entries[i].frees);std::fprintf','entries[i].frees);}std::fprintf');(p/'Phases.h').write_text(s)
h=Path('src/Parser.h').read_text();h='#include "Phases.h"\n'+h;h=h.replace('VariableBinding() : slot(std::make_shared<std::shared_ptr<nift::RuntimeValue>>(value)) {}','VariableBinding() : slot(std::make_shared<std::shared_ptr<nift::RuntimeValue>>(value)) {cp51::binding_slot(false);}')
h=h.replace('slot(std::make_shared<std::shared_ptr<nift::RuntimeValue>>(value)) {}','slot(std::make_shared<std::shared_ptr<nift::RuntimeValue>>(value)) {cp51::binding_slot(true);}')
(p/'Parser.h').write_text(h)
s=Path('src/Parser.cpp').read_text();a='''    for (const auto& binding : source)
        destination.insert_or_assign(binding.first, binding.second);''';b='''    for (const auto& binding : source) {
        for(int j=0;j<2;++j)if(cp51::stats.entries[j?cp51::Frame:cp51::Capture].depth){auto& c=cp51::stats.binding[j];++c.entries;if(destination.count(binding.first))++c.overwrites;else ++c.inserts;
            const auto& v=binding.second;c.value_refs+=bool(v.value);c.slot_refs+=bool(v.slot);c.root_refs+=bool(v.ref_root_slot);c.path_vectors+=!v.ref_path.empty();c.path_components+=v.ref_path.size();for(const auto& key:v.ref_path)c.path_key_bytes+=key.key.size();}
        destination.insert_or_assign(binding.first,binding.second);
    }''';assert a in s;s=s.replace(a,b).replace('Parser::~Parser() {','Parser::~Parser() {cp51::Phase destruction(cp51::Destruction);');(p/'Parser.cpp').write_text('#include "Phases.h"\n'+s)
commands=json.loads((old/'build.json').read_text());commands=[[x.replace(str(old)+'/',str(p)+'/') for x in c] for c in commands];commands[-1][-1]=str(p/'nift-counts')
(p/'build.json').write_text(json.dumps(commands,indent=2)+'\n')
with(p/'build.log').open('w') as log:
 for c in commands:print('BUILD',c[-1],flush=True);subprocess.run(c,stdout=log,stderr=subprocess.STDOUT,check=True)
print('PASS isolated capture/frame structural counters; no production architecture change')
