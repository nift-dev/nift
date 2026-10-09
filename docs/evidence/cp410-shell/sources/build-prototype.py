from pathlib import Path
import subprocess,json
p=Path('.build/cp410-shell');s=Path('src/ParserExpression.cpp').read_text()
a='bool absolute=fs::path(raw).is_absolute();for(const auto&m:matches)'
if a not in s:
 start=s.index('if(call_args("ls",');end=s.index('\n',start);print(s[start:end]);raise SystemExit('Inspect exact replacement')
s=s.replace(a,'bool absolute=fs::path(raw).is_absolute();std::error_code base_ec;const auto canonical_base=absolute?base:fs::weakly_canonical(base,base_ec);for(const auto&m:matches)',1)
a='auto shown=absolute?m:fs::relative(m,base,rec);'
assert a in s
s=s.replace(a,'fs::path shown=m;if(!absolute){if(base_ec)rec=base_ec;else{auto canonical_match=fs::weakly_canonical(m,rec);if(!rec)shown=canonical_match.lexically_relative(canonical_base);}}',1)
(p/'ParserExpression.cpp').write_text(s)
cmd=json.loads(Path('.build/cp410/phases/build.json').read_text())[-1]
objects=[x for x in cmd if x.endswith('.o') and not x.startswith('.build/cp410/')]+['src/ParserExpression.o'];libs=[x for x in cmd if x.endswith('.a')]
flags=['-std=c++17','-O2','-pthread','-Isrc','-Iinclude','-Iminifypp/include','-Iminifypp/src','-Imarkuppp/include','-Imarkuppp/vendor/cmark','-I.build/libffi/x86_64-linux-gnu-cc/install/include']
commands=[['g++',*flags,'-c',str(p/'ParserExpression.cpp'),'-o',str(p/'ParserExpression.o')],['g++',*flags,*[str(p/'ParserExpression.o') if x=='src/ParserExpression.o' else x for x in objects],*libs,'-lstdc++fs','-ldl','-o',str(p/'prototype-nift')]]
(p/'build.json').write_text(json.dumps(commands,indent=2)+'\n')
with(p/'build.log').open('w') as log:
 for c in commands:subprocess.run(c,stdout=log,stderr=subprocess.STDOUT,check=True)
print('Built isolated canonical-base prototype')
