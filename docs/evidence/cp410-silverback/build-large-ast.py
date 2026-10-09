from pathlib import Path
import subprocess,json
p=Path(__file__).parent.resolve();s=Path('src/Ast.cpp').read_text();assert s.count('source.size()>8192')==1;(p/'large-Ast.cpp').write_text(s.replace('source.size()>8192','source.size()>65536'))
compile=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-Isrc','-c',str(p/'large-Ast.cpp'),'-o',str(p/'large-Ast.o')];subprocess.run(compile,check=True)
base=json.loads((p/'string-direct-build.json').read_text())[1];cmds=[compile]
for label in ['large-ast-only','string-direct-large']:
 cmd=[str(p/'large-Ast.o') if x=='src/Ast.o' else 'src/ParserTemplate.o' if label=='large-ast-only' and x==str(p/'string-direct-ParserTemplate.o') else x for x in base];cmd[-1]=str(p/(label+'-nift'));subprocess.run(cmd,check=True);cmds.append(cmd)
(p/'large-ast-build.json').write_text(json.dumps(cmds,indent=2)+'\n')
