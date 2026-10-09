from pathlib import Path
import subprocess,shlex,json
p=Path(__file__).parent.resolve();s=Path('src/ParserTemplate.cpp').read_text();mark='            if(method=="to_string"&&recv.is_number())';assert s.count(mark)==1
branch='''            // Research only: already evaluated plain-string operands.
            if(recv.is_string()&&recv.string.rfind("\\x1fnift:",0)!=0&&method=="replace"&&args.size()==2&&args[0].is_string()&&args[1].is_string()&&!args[0].string.empty()){
                std::string result;result.reserve(recv.string.size());std::size_t begin=0,pos=recv.string.find(args[0].string);
                while(pos!=std::string::npos){result.append(recv.string,begin,pos-begin);result.append(args[1].string);begin=pos+args[0].string.size();pos=recv.string.find(args[0].string,begin);}
                result.append(recv.string,begin,std::string::npos);out=nift::RuntimeValue(std::move(result));return true;
            }
''';(p/'string-direct-ParserTemplate.cpp').write_text(s.replace(mark,branch+mark))
flags=['g++','-Isrc','-Iinclude','-Iminifypp/include','-Iminifypp/src','-Imarkuppp/include','-Imarkuppp/vendor/cmark','-I.build/libffi/x86_64-linux-gnu-cc/install/include','-std=c++17','-O2','-Wall','-Wextra','-pedantic','-pthread'];compile=flags+['-c',str(p/'string-direct-ParserTemplate.cpp'),'-o',str(p/'string-direct-ParserTemplate.o')]
lines=Path('.build/cp410-wave3/build-string.log').read_text().splitlines();link=shlex.split(next(x for x in reversed(lines) if x.startswith('g++ ') and x.endswith('-o nift')));link=[str(p/'string-direct-ParserTemplate.o') if x=='src/ParserTemplate.o' else x for x in link];link[-1]=str(p/'string-direct-nift');(p/'string-direct-build.json').write_text(json.dumps([compile,link],indent=2)+'\n')
for cmd in [compile,link]:subprocess.run(cmd,check=True)
