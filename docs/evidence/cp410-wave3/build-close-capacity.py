from pathlib import Path
import subprocess,shlex,json
p=Path(__file__).parent.resolve();s=Path('src/ParserExpression.cpp').read_text();old='f->open=false;f->mode.clear();f->working.clear();f->saved.clear();f->cursor=0;';new='f->open=false;f->mode.clear();std::string().swap(f->working);std::string().swap(f->saved);f->cursor=0;';assert s.count(old)==1;s=s.replace(old,new);source=p/'close-capacity-ParserExpression.cpp';source.write_text(s)
compile_cmd=['g++','-Isrc','-Iinclude','-Iminifypp/include','-Iminifypp/src','-Imarkuppp/include','-Imarkuppp/vendor/cmark','-I.build/libffi/x86_64-linux-gnu-cc/install/include','-std=c++17','-O2','-Wall','-Wextra','-pedantic','-pthread','-c',str(source),'-o',str(p/'close-capacity-ParserExpression.o')]
lines=(p/'build-string.log').read_text().splitlines();link=shlex.split(next(x for x in reversed(lines) if x.startswith('g++ ') and x.endswith('-o nift')));link=[str(p/'close-capacity-ParserExpression.o') if x=='src/ParserExpression.o' else x for x in link];link[-1]=str(p/'close-capacity-nift');(p/'close-capacity-build.json').write_text(json.dumps([compile_cmd,link],indent=2)+'\n')
for cmd in [compile_cmd,link]:subprocess.run(cmd,check=True)
