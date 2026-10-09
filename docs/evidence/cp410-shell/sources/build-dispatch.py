from pathlib import Path
import json,subprocess
p=Path('.build/cp410-shell');s=Path('src/ParserExpression.cpp').read_text();a="if(text.size()<name.size()+2||text.compare(0,name.size(),name.data(),name.size())!=0||text[name.size()]!='('||text.back()!=')')return false;";b="if(text.size()<name.size()+2||text[name.size()]!='('||text.back()!=')'||text.compare(0,name.size(),name.data(),name.size())!=0)return false;";assert s.count(a)==1;s=s.replace(a,b);(p/'dispatch-ParserExpression.cpp').write_text(s)
commands=json.loads((p/'build.json').read_text());compile=commands[0].copy();compile[compile.index(str(p/'ParserExpression.cpp'))]=str(p/'dispatch-ParserExpression.cpp');compile[-1]=str(p/'dispatch-ParserExpression.o');link=commands[1].copy();link=[str(p/'dispatch-ParserExpression.o') if x==str(p/'ParserExpression.o') else x for x in link];link[-1]=str(p/'dispatch-nift')
with(p/'dispatch-build.log').open('w') as log:
 for c in (compile,link):subprocess.run(c,stdout=log,stderr=subprocess.STDOUT,check=True)
print('Built isolated cheap builtin syntax rejection before prefix comparison')
