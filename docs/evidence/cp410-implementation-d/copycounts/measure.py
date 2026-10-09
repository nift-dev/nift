from pathlib import Path
import subprocess,shutil,os,tempfile,json
p=Path(__file__).resolve().parent;rows=[]
for label,original in [('accepted',p.parents[1]/'strings/prototype'),('final',p.parent/'prototype')]:
 stage=p/label
 if not stage.exists():shutil.copytree(original,stage)
 h=stage/'src/Parser.h';s=h.read_text();s=s.replace('#pragma once','#pragma once\n#include <cstddef>\nvoid nift_saved_copy(std::size_t);\nvoid nift_working_copy(std::size_t);',1)
 if label=='final':s=s.replace('saved=working;','saved=working;nift_saved_copy(working.size());')
 h.write_text(s)
 f=stage/'src/ParserExpression.cpp';s=f.read_text()
 if label=='accepted':
  s=s.replace('f->saved=data;','f->saved=data;nift_saved_copy(data.size());').replace('f->working=m=="w"?std::string():data;','f->working=m=="w"?std::string():data;nift_working_copy(m=="w"?0:data.size());').replace('f->saved=f->working;','f->saved=f->working;nift_saved_copy(f->working.size());').replace('f->working=f->saved;','f->working=f->saved;nift_working_copy(f->saved.size());')
 else:s=s.replace('if(!f->saved_is_working)f->working=f->saved;','if(!f->saved_is_working){f->working=f->saved;nift_working_copy(f->saved.size());}')
 f.write_text(s)
 (stage/'counter.cpp').write_text('''#include <cstddef>
#include <cstdlib>
#include <fstream>
static std::size_t saved_n=0,saved_bytes=0,working_n=0,working_bytes=0;
void nift_saved_copy(std::size_t n){if(n){++saved_n;saved_bytes+=n;}}
void nift_working_copy(std::size_t n){if(n){++working_n;working_bytes+=n;}}
struct Reporter{Reporter(){std::atexit([](){if(auto path=std::getenv("NIFT_FILE_COPY_TRACE")){std::ofstream f(path);f<<"{\\"saved_copies\\":"<<saved_n<<",\\"saved_bytes\\":"<<saved_bytes<<",\\"working_copies\\":"<<working_n<<",\\"working_bytes\\":"<<working_bytes<<"}\\n";}});}} reporter;
''')
 query=subprocess.check_output(['make','-s','--eval=print-copy-objects:; @echo $(CLI_OBJECTS)','print-copy-objects'],cwd=stage,text=True);objects=query.strip().split()
 flags=['g++','-Isrc','-Iinclude','-Iminifypp/include','-Iminifypp/src','-Imarkuppp/include','-Imarkuppp/vendor/cmark','-I.build/libffi/x86_64-linux-gnu-cc/install/include','-std=c++17','-O2','-Wall','-Wextra','-pedantic','-pthread']
 # Only static counter declarations were added: no layout changes. All reused
 # objects come from this same accepted/final stage, never mixed across versions.
 with (p/(label+'-build.log')).open('w') as log:
  for source in ['src/ParserExpression.cpp','src/ParserTemplate.cpp','counter.cpp']:
   out=source.replace('.cpp','-copy.o');subprocess.run([*flags,'-c',source,'-o',out],cwd=stage,stdout=log,stderr=subprocess.STDOUT,check=True)
  linked=[o.replace('.o','-copy.o') if o in ['src/ParserExpression.o','src/ParserTemplate.o'] else o for o in objects]
  subprocess.run(['g++','-std=c++17','-pthread',*linked,'counter-copy.o','.build/libffi/x86_64-linux-gnu-cc/install/lib/libffi.a','-ldl','-o','nift-copy'],cwd=stage,stdout=log,stderr=subprocess.STDOUT,check=True)
 for scenario in ['clean-open-close','create-save','mutate-revert']:
  with tempfile.TemporaryDirectory(prefix='nift-copy-count-') as directory:
   root=Path(directory);data=b'x'*1048576;(root/'payload').write_bytes(data)
   for i in range(32):
    if scenario!='create-save':(root/str(i)).write_bytes(data)
   body={'clean-open-close':'f.open("rw");f.close()','create-save':'f.open("w");f.write(payload);f.save();f.close()','mutate-revert':'f.open("rw");f.write("Y");f.revert();f.close()'}[scenario]
   source='payload:=open("payload");i:=0;while(i<32){f:=file(i.stringify());'+body+';i+=1};print("OK")';(root/'case.f').write_text(source)
   env=os.environ.copy();env['NIFT_FILE_COPY_TRACE']=str(root/'trace.json');q=subprocess.run([str(stage/'nift-copy'),str(root/'case.f')],cwd=root,env=env,capture_output=True,text=True,check=True);assert q.stdout=='OK\n' and not q.stderr
   assert all((root/str(i)).read_bytes()==data for i in range(32))
   row=dict(label=label,scenario=scenario,**json.loads((root/'trace.json').read_text()));rows.append(row);print(row,flush=True);(p/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
