from pathlib import Path
import json,subprocess,shutil,difflib,hashlib
p=Path('.build/cp410-followup/phases');p.mkdir(parents=True,exist_ok=True)
old=Path('.build/cp410/phases');s=(old/'ParserExpression.cpp').read_text();s=s.replace('#include "Phases.h"','#include "Phases.h"\n#include <string_view>')
s=s.replace('for(const auto& scope:variable_scopes_) for(const auto& kv:scope) {++cp51::stats.capture_entries;li->captures[kv.first]=kv.second;}','for(const auto& scope:variable_scopes_) {cp51::stats.capture_entries+=scope.size();copy_capture_bindings(li->captures,scope);}')
s=s.replace('for(const auto& kv:fn->captures){++cp51::stats.frame_capture_entries;sc[kv.first]=kv.second;}','cp51::stats.frame_capture_entries+=fn->captures.size();copy_capture_bindings(sc,fn->captures);')
s=s.replace('for(const auto& kv:fn->captures)sc[kv.first]=kv.second;','copy_capture_bindings(sc,fn->captures);')
s=s.replace('auto call_args=[&](const std::string& name','auto call_args=[&](std::string_view name').replace('if(text.rfind(name+"(",0)!=0||text.back()!=\')\')return false;','if(text.size()<name.size()+2||text.compare(0,name.size(),name.data(),name.size())!=0||text[name.size()]!=\'(\'||text.back()!=\')\')return false;').replace('error=name+": malformed arguments"','error=std::string(name)+": malformed arguments"').replace('error=name+": spread value must be an array"','error=std::string(name)+": spread value must be an array"')
assert 'name+"("' not in s;assert 'li->captures[kv.first]' not in s;assert 'sc[kv.first]=kv.second' not in s
(p/'ParserExpression.cpp').write_text(s)
s=Path('src/Parser.cpp').read_text().replace('Parser::~Parser() {','Parser::~Parser() { cp51::Phase destruction(cp51::Destruction);');(p/'Parser.cpp').write_text('#include "Phases.h"\n'+s)
s=(old/'Phases.h').read_text().replace('bytes=0;unsigned depth','bytes=0,frees=0;unsigned depth').replace('bytes=%llu\\n','bytes=%llu frees=%llu\\n').replace('entries[i].bytes);','entries[i].bytes,entries[i].frees);');s=s.replace('struct Phase {','inline void deallocation(void* p){if(p)for(int i=0;i<Count;++i)if(stats.entries[i].depth)++stats.entries[i].frees;}\nstruct Phase {');(p/'Phases.h').write_text(s)
s=(old/'Alloc.cpp').read_text().replace('std::free(p);','cp51::deallocation(p);std::free(p);');(p/'Alloc.cpp').write_text(s)
cmds=json.loads((old/'build.json').read_text());cmds=[[x.replace('.build/cp410/phases/','.build/cp410-followup/phases/') for x in c] for c in cmds];parsercmd=[x.replace('ParserExpression.cpp','Parser.cpp').replace('ParserExpression.o','Parser.o') for x in cmds[1]];cmds.insert(2,parsercmd);cmds[-1]=[str(p/'Parser.o') if x=='src/Parser.o' else x for x in cmds[-1]]
(p/'build.json').write_text(json.dumps(cmds,indent=2)+'\n')
for filename in ['Parser.cpp','ParserExpression.cpp']:(p/(filename+'.patch')).write_text(''.join(difflib.unified_diff(Path('src/'+filename).read_text().splitlines(True),(p/filename).read_text().splitlines(True),fromfile='src/'+filename,tofile=str(p/filename))))
with (p/'build.log').open('w') as log:
 for c in cmds:print('BUILD',c[-1],flush=True);subprocess.run(c,stdout=log,stderr=subprocess.STDOUT,check=True)
shutil.copy2('nift','.build/cp410-followup/accepted-nift');print('PASS fresh accepted-state phase binary',flush=True)
