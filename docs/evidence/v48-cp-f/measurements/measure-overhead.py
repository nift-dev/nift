import json,subprocess,time,statistics
from pathlib import Path
root=Path(__file__).resolve().parent
base=(root/'baseline-src/nift').resolve();candidate=Path('nift').resolve()
fixture=root/'overhead-cases';fixture.mkdir(exist_ok=True)
programs={
 'small-native':'\n'.join(f'v{i} := {i}' for i in range(20))+'\nprint(v19)\n',
 'large-native':'\n'.join(f'v{i} := {i}' for i in range(10000))+'\nprint(v9999)\n',
 'native-loop':'fn(main(n)) { total := 0; i := 0; while(i < n) { total += i; i += 1 } print(total) }\nmain(200000)\n',
 'cached-callback':'a := ['+','.join(str(i) for i in range(1000))+']\nfor(i : [1,2,3,4,5,6,7,8,9,10]) { b := a.map(x => (x + 1) * 2) }\n',
}
commands=[]
for name,source in programs.items():
 p=fixture/(name+'.f');p.write_text(source);commands.append((name,[str(p.resolve())],Path.cwd()))
for name,n,template in [('template-heavy',1,'<main>'+('$[1 + 2]\n'*2000)+'@content</main>'),('migrated-site',500,'<html><h1>$[title]</h1>'+('<section>text</section>\n'*100)+'@content</html>')]:
 d=fixture/name
 for directory in ('.nift','templates','content','public'):(d/directory).mkdir(parents=True,exist_ok=True)
 (d/'.nift/config.json').write_text(json.dumps({'config':{'content-dir':'content/','content-ext':'.html','output-dir':'public/','output-ext':'.html','default-template':'templates/main.html','build-threads':1,'incremental-mode':'modified','minify-exts':[]}}))
 (d/'.nift/tracked.json').write_text(json.dumps({'tracked':[{'name':f'p{i}','title':f'Page {i}','template':'templates/main.html'} for i in range(n)]}))
 (d/'templates/main.html').write_text(template)
 for i in range(n):(d/'content'/f'p{i}.html').write_text(f'<p>Page {i}</p>\n')
 commands.append((name,['build','--all'],d.resolve()))
results=[]
for name,args,cwd in commands:
 samples={'baseline':[],'candidate':[]};rss={'baseline':[],'candidate':[]}
 for sample_round in range(9):
  for label,binary in ([('baseline',base),('candidate',candidate)] if sample_round%2==0 else [('candidate',candidate),('baseline',base)]):
   memory=fixture/'rss.txt';start=time.perf_counter()
   p=subprocess.run(['/usr/bin/time','-f','%M','-o',str(memory.resolve()),str(binary),*args],cwd=cwd,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
   if p.returncode:raise SystemExit(p.stderr.decode())
   elapsed=time.perf_counter()-start
   if sample_round>=2:samples[label].append(elapsed);rss[label].append(int(memory.read_text().strip()))
 row={'name':name,'seconds':samples,'peak_rss_kib':rss,'baseline_median_s':statistics.median(samples['baseline']),'candidate_median_s':statistics.median(samples['candidate'])}
 row['paired_median_ratio']=statistics.median([c/b for b,c in zip(samples['baseline'],samples['candidate'])]);results.append(row);print(name,round(row['paired_median_ratio'],3),flush=True)
(root/'overhead.json').write_text(json.dumps({'baseline_commit':'ab687d07d4f987b09ac0277142e2aaf1c0602af6','rounds':7,'warmup_rounds':2,'cases':results},indent=2)+'\n')
