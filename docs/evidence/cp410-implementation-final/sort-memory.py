from pathlib import Path
import subprocess,json,re,hashlib
p=Path(__file__).resolve().parent;binary=Path('nift').resolve();rows=[]
for width in [0,100]:
 source=Path(f'.build/cp410-capture-frame/cases/n016000/u{width:03}/identity____/official-equivalent.f').resolve();log=p/f'sort-u{width}-memcheck.log'
 q=subprocess.run(['valgrind','--tool=memcheck','--leak-check=full','--errors-for-leak-kinds=all','--error-exitcode=81','--log-file='+str(log),str(binary),str(source)],capture_output=True,text=True,check=True);assert q.stdout=='1\n16000\n' and not q.stderr
 raw=log.read_text();assert 'ERROR SUMMARY: 0 errors' in raw and 'in use at exit: 0 bytes in 0 blocks' in raw;m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',raw)
 rows.append(dict(width=width,allocations=int(m[1].replace(',','')),allocated_bytes=int(m[3].replace(',','')),binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest()));print(rows[-1],flush=True)
(p/'sort-memory.json').write_text(json.dumps(rows,indent=2)+'\n')
