from pathlib import Path
import subprocess,re,json
p=Path('.build/cp410-implementation');rows=[]
for width in (0,100):
 source=Path(f'.build/cp410-capture-frame/cases/n016000/u{width:03}/identity____/official-equivalent.f').resolve()
 for label,binary in [('baseline',p/'baseline-nift'),('snapshot',Path('nift'))]:
  log=p/f'{label}-u{width}.memcheck';q=subprocess.run(['valgrind','--tool=memcheck','--leak-check=full','--show-leak-kinds=all','--errors-for-leak-kinds=all','--error-exitcode=99','--log-file='+str(log),str(binary.resolve()),str(source)],capture_output=True,text=True,check=True);assert q.stdout=='1\n16000\n'
  text=log.read_text();m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',text);assert 'in use at exit: 0 bytes in 0 blocks' in text;rows.append(dict(width=width,label=label,allocations=int(m[1].replace(',','')),allocated_bytes=int(m[3].replace(',',''))));(p/'allocations.json').write_text(json.dumps(rows,indent=2)+'\n');print(rows[-1],flush=True)
