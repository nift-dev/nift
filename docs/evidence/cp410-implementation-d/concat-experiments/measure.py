from pathlib import Path
import subprocess,tempfile,json,hashlib,re
p=Path(__file__).resolve().parent;source=(p.parents[1]/'cp410-shell/concat-1000/source.f').read_text();payload=next((p.parents[1]/'cp410-shell/concat-1000/files').iterdir()).read_bytes();rows=[]
binaries={'accepted-save':p.parent/'save/recipe-only-nift','owned-write':p/'prototype/nift','native-stream':p.parents[1]/'cp410-shell/secondary-control'}
for size,count in [(128,1000),(1048576,32)]:
 data=(payload*((size+len(payload)-1)//len(payload)))[:size];expected=hashlib.sha256(data*count).hexdigest()
 for label,binary in binaries.items():
  for tool in ['callgrind','memcheck']:
   with tempfile.TemporaryDirectory(prefix='nift-concat-probe-') as directory:
    root=Path(directory);(root/'files').mkdir();names=['files/'+str(i) for i in range(count)]
    for name in names:(root/name).write_bytes(data)
    (root/'targets.txt').write_text(''.join(name+'\n' for name in names));script=root/'source.f';script.write_text(source)
    log=p/f'{size}-{label}-{tool}.log';cg=p/f'{size}-{label}.callgrind';args=['concat'] if label=='native-stream' else [str(script)]
    opts=['--callgrind-out-file='+str(cg)] if tool=='callgrind' else ['--leak-check=full','--errors-for-leak-kinds=all','--error-exitcode=81']
    q=subprocess.run(['valgrind','--tool='+tool,'--log-file='+str(log),*opts,str(binary),*args],cwd=root,capture_output=True,text=True,check=True)
    assert q.stdout=='OK\n' and not q.stderr,(label,q.stdout,q.stderr)
    assert hashlib.sha256((root/'output').read_bytes()).hexdigest()==expected
    row=dict(size=size,count=count,label=label,tool=tool,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),output_sha256=expected)
    if tool=='callgrind':row['instructions']=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1])
    else:
     raw=log.read_text();assert 'ERROR SUMMARY: 0 errors' in raw and 'in use at exit: 0 bytes in 0 blocks' in raw
     m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',raw);row.update(allocations=int(m[1].replace(',','')),allocated_bytes=int(m[3].replace(',','')))
    rows.append(row);(p/'measure.json').write_text(json.dumps(rows,indent=2)+'\n');print(row,flush=True)
