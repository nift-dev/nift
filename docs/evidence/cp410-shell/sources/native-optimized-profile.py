from pathlib import Path
import subprocess,json,re
p=Path('.build/cp410-shell').resolve();s=(p/'native.cpp').read_text().replace('const auto base=fs::current_path();','std::error_code bec;const auto base=fs::weakly_canonical(fs::current_path(),bec);').replace('auto r=fs::relative(m,base,ec);','auto r=fs::weakly_canonical(m,ec).lexically_relative(base);');(p/'native-optimized.cpp').write_text(s);subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(p/'native-optimized.cpp'),'-o',str(p/'native-optimized')],check=True);rows=[]
for n in (1000,10000,100000):
 root=p/f'official-traverse-{n}';cg=p/f'native-optimized-{n}.callgrind';log=p/f'native-optimized-{n}.memcheck';cmd=[str(p/'native-optimized')];r=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(cg),*cmd],cwd=root,text=True,capture_output=True,check=True);assert r.stdout.strip()==str(n+n//5)
 r=subprocess.run(['valgrind','--tool=memcheck','--leak-check=full','--error-exitcode=99','--log-file='+str(log),*cmd],cwd=root,text=True,capture_output=True,check=True);s=log.read_text();assert 'ERROR SUMMARY: 0 errors' in s and 'in use at exit: 0 bytes in 0 blocks' in s;m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',s)
 rows.append({'n':n,'matches':n+n//5,'instructions':int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]),'allocations':int(m[1].replace(',','')),'bytes':int(m[3].replace(',','')),'memcheck_errors':0});print(n,'PASS',flush=True)
 Path('docs/evidence/cp410-shell/native-optimized.json').write_text(json.dumps(rows,indent=2)+'\n')
print('COMPLETE stronger canonical-base-cached native control')
